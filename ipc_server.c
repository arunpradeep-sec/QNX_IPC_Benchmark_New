/*
 * ipc_server.c - echo server for the QNX message-passing RTT benchmark.
 *
 * Usage: ipc_server <fifo|rr> <priority> <cpu>
 *   e.g. ipc_server fifo 50 0
 *
 * Receives a message on a name_attach() channel and replies at once with a
 * payload of the same size. Runs until slain.
 *
 * Priority inheritance is left at the QNX default (enabled), so while it
 * handles a request this thread runs at the sending client's priority.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iomsg.h>
#include <sys/mman.h>
#include <sys/neutrino.h>

#define ATTACH_POINT "qnx_rtt_bench"
#define MAX_PAYLOAD  8192

typedef union {
    uint16_t       type;               /* first field of every QNX message   */
    struct _pulse  pulse;
    char           data[MAX_PAYLOAD];
} bench_msg_t;

static int parse_policy(const char *s)
{
    if (strcmp(s, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(s, "rr") == 0)   return SCHED_RR;
    fprintf(stderr, "policy must be fifo or rr\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <fifo|rr> <priority> <cpu>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int policy = parse_policy(argv[1]);
    int prio   = atoi(argv[2]);
    int cpu    = atoi(argv[3]);

    /* Pin to one CPU so client, server and load compete for the same core. */
    uintptr_t runmask = (uintptr_t)1 << cpu;
    if (ThreadCtl(_NTO_TCTL_RUNMASK, (void *)runmask) == -1) {
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

    /* Avoid page faults during the measurement. Not fatal if refused. */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall (continuing)");
    }

    name_attach_t *att = name_attach(NULL, ATTACH_POINT, 0);
    if (att == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }

    static bench_msg_t msg;
    memset(&msg, 0, sizeof msg);        /* touch the buffer before measuring */

    printf("server ready: policy=%s priority=%d cpu=%d\n", argv[1], prio, cpu);
    fflush(stdout);

    for (;;) {
        struct _msg_info info;
        rcvid_t rcvid = MsgReceive(att->chid, &msg, sizeof msg, &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {               /* a pulse, not a message */
            if (msg.pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(msg.pulse.scoid);
            }
            continue;
        }

        /* name_open() sends an _IO_CONNECT message; acknowledge it. */
        if (msg.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }

        /* Reject any other system I/O message. */
        if (msg.type > _IO_BASE && msg.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        /* Benchmark message: echo a reply of the same size. */
        size_t len = (size_t)info.msglen;
        if (len > sizeof msg) len = sizeof msg;
        MsgReply(rcvid, EOK, &msg, len);
    }

    name_detach(att, 0);
    return EXIT_SUCCESS;
}
