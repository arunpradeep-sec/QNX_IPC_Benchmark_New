# QNX SDP 8.0 Message-Passing Latency Benchmark (VMware guest)

Benchmark sources and raw data for the letter

> A. Pradeep and O. Otto C., "Tail Latency of Synchronous Message Passing in
> QNX SDP 8.0 Under VMware Virtualization," submitted to *IEEE Embedded Systems Letters*.

The benchmark measures the client-observed round-trip time (RTT) of QNX
synchronous message passing (`MsgSend()` / `MsgReceive()` / `MsgReply()`)
under `SCHED_FIFO` and `SCHED_RR`, without load and with a competing
CPU-bound thread at a lower, equal or higher priority.

## Contents

| File | Purpose |
|---|---|
| `ipc_server.c` | Echo server: `name_attach()`, replies at once with a payload of the same size |
| `ipc_client.c` | Client: times each `MsgSend()` with `ClockCycles()`, optional competing-load thread, writes one CSV per run |
| `run_all.sh` | Full campaign: 2 policies x (7 payloads x 10 runs without load + 3 load levels x 10 runs) = 200 runs |
| `ipc_server`, `ipc_client` | x86-64 binaries used for the published data |
| `rtt_<policy>_p50_<load>_<payload>_run<n>.csv` | Raw data, one file per run: `sample,cycles,time_us` |
| `summary.log` | Console output of `run_all.sh` (one summary line per run) |
| `analyze_raw.py` | Reproduces Tables I-II and Figs. 1-4 of the letter from the raw CSV files |
| `analyze_summary.py` | Quick per-condition summary from `summary.log` (cross-check only) |

## Platform used for the published data

- Guest: QNX SDP 8.0 (x86-64), image built with `mkqnximage` (Momentics IDE), 2 virtual CPUs
- Hypervisor: VMware Workstation 17 Pro 17.6.4 (build 24832109)
- Host: Intel Core i5-1335U, 16 GB RAM, Windows 11 Home (build 26200)
- All benchmark threads bound to CPU 0; priority 50; `mlockall()` in both processes

## Build

With the QNX SDP 8.0 environment loaded (or from a Momentics project):

```sh
qcc -Vgcc_ntox86_64 -O2 -o ipc_server ipc_server.c
qcc -Vgcc_ntox86_64 -O2 -o ipc_client ipc_client.c
```

## Run

Copy `ipc_server`, `ipc_client` and `run_all.sh` to a writable directory on the
target (the script uses `/data`; the guest needs about 150 MB free for the CSV
files), then as root:

```sh
chmod +x run_all.sh ipc_server ipc_client
./run_all.sh | tee summary.log
```

Single runs:

```sh
./ipc_server fifo 50 0 &
./ipc_client 32 fifo 50 0 none 1 10000                 # no load, 10 000 samples
./ipc_client 32 fifo 50 0 equal 1 100000 40000 40000   # equal-priority load, 40 ms burst / 40 ms sleep
```

Client arguments: `<payload> <fifo|rr> <priority> <cpu> <none|below|equal|above> <run_id> [samples] [burst_us] [idle_us]`.
The load thread runs at priority -5, +0 or +5 relative to the IPC pair.

## Analysis

```sh
pip install numpy matplotlib
python3 analyze_raw.py . figures
```

prints Tables I and II, the 30-120 us band rates of Section IV-C, and writes
`figures/fig1_payload.png` ... `fig4_delays.png`.

## Citation

If you use this code or data, please cite the letter above.
