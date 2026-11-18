/*
 * ipc_client.c - client for the QNX message-passing RTT benchmark,
 *                with an optional competing-load thread.
 *
 * Usage:
 *   ipc_client <payload> <fifo|rr> <priority> <cpu> <none|below|equal|above>
 *              <run_id> [samples] [burst_us] [idle_us]
 *
 *   e.g. ipc_client 32 fifo 50 0 equal 1
 *
 * Metric: client-observed round-trip time, from just before MsgSend() to
 * just after it returns, read with ClockCycles().
 *
 * Load: one extra thread on the same CPU that busy-loops for burst_us and
 * then sleeps for idle_us, at priority (priority - 5), (priority) or
 * (priority + 5). The load is duty-cycled, not continuous, because a
 * continuous higher-priority load would starve the benchmark for ever.
 * The built-in default is a 5-ms burst and 5-ms sleep; the published
 * campaign (run_all.sh) uses 40 ms / 40 ms. Either way the burst is longer
 * than the round-robin timeslice (4 clock periods, 3.75 ms on the test
 * guest), which is what lets SCHED_FIFO and SCHED_RR behave differently
 * in the equal-priority case.
 *
 * Before measuring, the client busy-loops for SPIN_WARMUP_MS so that the
 * host CPU has left any low-power or low-frequency state in every mode,
 * not only in the modes that have a load thread.
 *
 * Output: rtt_<policy>_p<prio>_<load>_<payload>_run<id>.csv
 *         columns: sample,cycles,time_us
 * A one-line summary is printed to stdout. "over_1ms" counts samples above
 * 1 ms and "bursts" counts load bursts completed during the measurement,
 * so you can see how many bursts the run actually overlapped.
 */
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#define ATTACH_POINT   "qnx_rtt_bench"
#define MAX_PAYLOAD    8192
#define MIN_PAYLOAD    16
#define WARMUP         100
#define SPIN_WARMUP_MS 200              /* CPU warm-up before measuring     */
#define MSG_TYPE_BENCH (_IO_MAX + 1)    /* outside the system message range */
#define LOAD_DELTA     5                /* priority offset of the load      */

typedef union {
    uint16_t type;
    char     data[MAX_PAYLOAD];
} bench_msg_t;

typedef struct {
    int      policy;
    int      prio;
    int      cpu;
    uint64_t burst_cycles;
    long     idle_ns;
} load_cfg_t;

static volatile int g_stop = 0;
static volatile unsigned long g_bursts = 0;

static int parse_policy(const char *s)
{
    if (strcmp(s, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(s, "rr") == 0)   return SCHED_RR;
    fprintf(stderr, "policy must be fifo or rr\n");
    exit(EXIT_FAILURE);
}

static int pin_to_cpu(int cpu)
{
    uintptr_t runmask = (uintptr_t)1 << cpu;
    return ThreadCtl(_NTO_TCTL_RUNMASK, (void *)runmask);
}

static void *load_thread(void *arg)
{
    const load_cfg_t *cfg = arg;

    if (pin_to_cpu(cfg->cpu) == -1) {
        perror("load: ThreadCtl(RUNMASK)");
        return NULL;
    }

    struct timespec idle;
    idle.tv_sec  = cfg->idle_ns / 1000000000L;
    idle.tv_nsec = cfg->idle_ns % 1000000000L;

    while (!g_stop) {
        uint64_t end = ClockCycles() + cfg->burst_cycles;
        while (ClockCycles() < end) {
            /* burn CPU */
        }
        g_bursts++;
        nanosleep(&idle, NULL);
    }
    return NULL;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char *argv[])
{
    if (argc < 7) {
        fprintf(stderr,
            "usage: %s <payload> <fifo|rr> <priority> <cpu> "
            "<none|below|equal|above> <run_id> [samples] [burst_us] [idle_us]\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    size_t      payload  = (size_t)atol(argv[1]);
    const char *pol_name = argv[2];
    int         policy   = parse_policy(pol_name);
    int         prio     = atoi(argv[3]);
    int         cpu      = atoi(argv[4]);
    const char *load     = argv[5];
    int         run_id   = atoi(argv[6]);
    long        samples  = (argc > 7) ? atol(argv[7]) : 10000;
    long        burst_us = (argc > 8) ? atol(argv[8]) : 5000;
    long        idle_us  = (argc > 9) ? atol(argv[9]) : 5000;

    if (payload < MIN_PAYLOAD || payload > MAX_PAYLOAD || samples < 1) {
        fprintf(stderr, "payload must be %d..%d bytes, samples >= 1\n",
                MIN_PAYLOAD, MAX_PAYLOAD);
        return EXIT_FAILURE;
    }

    int load_delta = 0, use_load = 1;
    if      (strcmp(load, "none")  == 0) use_load = 0;
    else if (strcmp(load, "below") == 0) load_delta = -LOAD_DELTA;
    else if (strcmp(load, "equal") == 0) load_delta = 0;
    else if (strcmp(load, "above") == 0) load_delta = +LOAD_DELTA;
    else {
        fprintf(stderr, "load must be none, below, equal or above\n");
        return EXIT_FAILURE;
    }

    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    /* Scheduling for the measuring thread. */
    if (pin_to_cpu(cpu) == -1) {
        perror("ThreadCtl(RUNMASK)");
        return EXIT_FAILURE;
    }
    struct sched_param sp;
    memset(&sp, 0, sizeof sp);
    sp.sched_priority = prio;
    int rc = pthread_setschedparam(pthread_self(), policy, &sp);
    if (rc != 0) {
        fprintf(stderr, "pthread_setschedparam: %s\n", strerror(rc));
        return EXIT_FAILURE;
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall (continuing)");
    }

    /* Buffers are allocated and touched before any measurement. */
    static bench_msg_t smsg, rmsg;
    memset(&smsg, 0xA5, sizeof smsg);
    memset(&rmsg, 0, sizeof rmsg);
    smsg.type = MSG_TYPE_BENCH;

    uint64_t *rtt = calloc((size_t)samples, sizeof *rtt);
    if (rtt == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    int coid = name_open(ATTACH_POINT, 0);
    if (coid == -1) {
        perror("name_open (is ipc_server running?)");
        return EXIT_FAILURE;
    }

    /* Cost of the timestamp pair itself: minimum of back-to-back reads. */
    uint64_t ts_overhead = UINT64_MAX;
    for (int i = 0; i < 1000; i++) {
        uint64_t a = ClockCycles();
        uint64_t b = ClockCycles();
        if (b - a < ts_overhead) ts_overhead = b - a;
    }

    /* Bring the host CPU out of any low-power state, in every mode. */
    {
        uint64_t end = ClockCycles() + (uint64_t)SPIN_WARMUP_MS * cps / 1000u;
        while (ClockCycles() < end) {
            /* spin */
        }
    }

    /* Warm-up exchanges, without load. */
    for (int i = 0; i < WARMUP; i++) {
        if (MsgSend(coid, &smsg, payload, &rmsg, payload) == -1) {
            perror("MsgSend (warm-up)");
            return EXIT_FAILURE;
        }
    }

    /* Start the competing load. */
    pthread_t  tid;
    load_cfg_t cfg;
    if (use_load) {
        cfg.policy       = policy;
        cfg.prio         = prio + load_delta;
        cfg.cpu          = cpu;
        cfg.burst_cycles = (uint64_t)burst_us * cps / 1000000u;
        cfg.idle_ns      = idle_us * 1000L;

        pthread_attr_t attr;
        struct sched_param lsp;
        memset(&lsp, 0, sizeof lsp);
        lsp.sched_priority = cfg.prio;
        pthread_attr_init(&attr);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, policy);
        pthread_attr_setschedparam(&attr, &lsp);
        rc = pthread_create(&tid, &attr, load_thread, &cfg);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            fprintf(stderr, "pthread_create(load): %s\n", strerror(rc));
            return EXIT_FAILURE;
        }
    }

    /* Measurement. Nothing but the two timestamps surrounds MsgSend(). */
    unsigned long bursts_before = g_bursts;
    for (long i = 0; i < samples; i++) {
        uint64_t t0 = ClockCycles();
        long     r  = MsgSend(coid, &smsg, payload, &rmsg, payload);
        uint64_t t1 = ClockCycles();
        if (r == -1) {
            perror("MsgSend");
            g_stop = 1;
            return EXIT_FAILURE;
        }
        rtt[i] = t1 - t0;
    }
    unsigned long bursts_seen = g_bursts - bursts_before;

    if (use_load) {
        g_stop = 1;
        pthread_join(tid, NULL);
    }
    name_close(coid);

    /* Drop to a normal priority before doing file I/O. */
    sp.sched_priority = 10;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);

    char fname[128];
    snprintf(fname, sizeof fname, "rtt_%s_p%d_%s_%zu_run%d.csv",
             pol_name, prio, load, payload, run_id);
    FILE *fp = fopen(fname, "w");
    if (fp == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    fprintf(fp, "sample,cycles,time_us\n");
    for (long i = 0; i < samples; i++) {
        fprintf(fp, "%ld,%" PRIu64 ",%.3f\n", i + 1, rtt[i],
                (double)rtt[i] * 1e6 / (double)cps);
    }
    fclose(fp);

    /* Summary. Sorting happens after the raw order has been saved. */
    qsort(rtt, (size_t)samples, sizeof *rtt, cmp_u64);
    double us = 1e6 / (double)cps;
    long over_1ms = 0;
    for (long i = samples - 1; i >= 0 && rtt[i] * us > 1000.0; i--) over_1ms++;
    struct timespec slice;
    sched_rr_get_interval(0, &slice);

    printf("%s: n=%ld min=%.3f median=%.3f p99=%.3f max=%.3f us over_1ms=%ld | "
           "cps=%" PRIu64 " ts_overhead=%.1f ns rr_slice=%.3f ms",
           fname, samples,
           rtt[0] * us,
           rtt[samples / 2] * us,
           rtt[(size_t)((double)(samples - 1) * 0.99)] * us,
           rtt[samples - 1] * us,
           over_1ms,
           cps,
           (double)ts_overhead * 1e9 / (double)cps,
           slice.tv_sec * 1e3 + slice.tv_nsec / 1e6);
    if (use_load) {
        printf(" | load prio=%d burst=%ld us idle=%ld us bursts=%lu",
               prio + load_delta, burst_us, idle_us, bursts_seen);
    }
    printf("\n");

    free(rtt);
    return EXIT_SUCCESS;
}
