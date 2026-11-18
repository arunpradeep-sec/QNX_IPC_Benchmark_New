#!/usr/bin/env python3
"""
analyze_raw.py - reproduce Tables I-II and Figs. 1-4 of the letter
"Tail Latency of Synchronous Message Passing in QNX SDP 8.0 Under VMware
Virtualization" from the raw per-sample CSV files in this repository.

Usage:  python3 analyze_raw.py [data_dir] [out_dir]
        (defaults: current directory, ./figures)
Needs:  numpy, matplotlib
"""
import os, sys, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.transforms as mt

D   = sys.argv[1] if len(sys.argv) > 1 else "."
OUT = sys.argv[2] if len(sys.argv) > 2 else "figures"
os.makedirs(OUT, exist_ok=True)
PAYS, RUNS, T95 = [32, 64, 128, 512, 1024, 4096, 8192], range(1, 11), 2.262  # t(0.975, 9)

def load(pol, ld, p, r):
    return np.loadtxt(f"{D}/rtt_{pol}_p50_{ld}_{p}_run{r}.csv",
                      delimiter=",", skiprows=1, usecols=2)   # time_us

def ci(v):
    v = np.asarray(v); return T95 * v.std(ddof=1) / math.sqrt(len(v))

# ---------------- Table I: no load ----------------
print("TABLE I  (us; median = mean of 10 run medians +/- 95% CI half-width)")
print(f"{'payload':>7} | {'FIFO med':>13} {'P99':>6} {'P99.9':>6} {'max*':>6} | "
      f"{'RR med':>13} {'P99':>6} {'P99.9':>6} {'max*':>6}")
for p in PAYS:
    row = f"{p:>7} |"
    for pol in ("fifo", "rr"):
        x = [load(pol, "none", p, r) for r in RUNS]
        med = [np.median(a) for a in x]
        row += (f" {np.mean(med):6.3f}+/-{ci(med):5.3f}"
                f" {np.mean([np.percentile(a, 99) for a in x]):6.2f}"
                f" {np.mean([np.percentile(a, 99.9) for a in x]):6.1f}"
                f" {np.median([a.max() for a in x]):6.0f} |")
    print(row)
print("  * median over runs of the per-run maximum\n")

# ---------------- Table II: competing load (32 B) ----------------
print("TABLE II  (32 B; delay = exchange > 1 ms; blocked = sum of delays per run)")
for ld in ("below", "equal", "above"):
    for pol in ("fifo", "rr"):
        x = [load(pol, ld, 32, r) for r in RUNS]
        d = [a[a > 1000] for a in x]
        alld = np.concatenate(d) / 1000
        blk = [s.sum() / 1000 for s in d]
        print(f"{ld:>6} {pol:>4}: median {np.mean([np.median(a) for a in x]):.2f} us,"
              f" P99 {np.mean([np.percentile(a, 99) for a in x]):.2f} us,"
              f" delays/run {np.mean([len(s) for s in d]):.1f},"
              f" median delay {np.median(alld) if len(alld) else float('nan'):.2f} ms,"
              f" max {max(a.max() for a in x) / 1000:.2f} ms,"
              f" blocked {np.mean(blk):.1f} +/- {ci(blk):.1f} ms/run")

# ---------------- RR band check (Sec. IV-C) ----------------
print("\n30-120 us exchanges per ms of run time, no load (median over 70 runs):")
for pol in ("fifo", "rr"):
    rate = [((a > 30) & (a < 120)).sum() / (a.sum() / 1000)
            for p in PAYS for a in (load(pol, "none", p, r) for r in RUNS)]
    print(f"  {pol}: {np.median(rate):.2f}")

# ---------------- Figures ----------------
plt.rcParams.update({"font.family": "serif", "font.size": 8, "axes.spines.top": False,
                     "axes.spines.right": False, "legend.frameon": False,
                     "savefig.dpi": 400, "grid.color": "#e3e3e3", "grid.linewidth": 0.4})
C, LS = {"fifo": "#2a78d6", "rr": "#eb6834"}, {"fifo": "-", "rr": "--"}
NM, W = {"fifo": "SCHED_FIFO", "rr": "SCHED_RR"}, 3.45

# Fig. 1: median and P99 vs payload
fig, ax = plt.subplots(figsize=(W, 2.0))
for pol, off in (("fifo", 0.96), ("rr", 1.04)):
    med, cis, p99 = [], [], []
    for p in PAYS:
        x = [load(pol, "none", p, r) for r in RUNS]
        m = [np.median(a) for a in x]
        med.append(np.mean(m)); cis.append(ci(m))
        p99.append(np.mean([np.percentile(a, 99) for a in x]))
    xs = np.array(PAYS) * off
    ax.errorbar(xs, med, yerr=cis, color=C[pol], ls=LS[pol], marker="o", ms=3.5,
                capsize=2, label=f"{NM[pol]} median")
    ax.plot(xs, p99, color=C[pol], ls=LS[pol], marker="^", ms=3.5, alpha=0.7,
            label=f"{NM[pol]} P99")
ax.set_xscale("log", base=2); ax.set_xticks(PAYS); ax.set_xticklabels(PAYS)
ax.set_xlabel("Payload (bytes)"); ax.set_ylabel("RTT (µs)"); ax.grid(True, axis="y")
ax.legend(loc="upper left", ncol=2)
fig.tight_layout(pad=0.3); fig.savefig(f"{OUT}/fig1_payload.png"); plt.close(fig)

# Fig. 2: CCDF without load
fig, ax = plt.subplots(figsize=(W, 2.15))
for pol in ("fifo", "rr"):
    for p, a in ((32, 1.0), (8192, 0.55)):
        x = np.sort(np.concatenate([load(pol, "none", p, r) for r in RUNS]))
        ax.step(x, 1 - np.arange(len(x)) / len(x), where="post", color=C[pol],
                ls=LS[pol], alpha=a, label=f"{NM[pol]}, {p} B")
ax.set_xscale("log"); ax.set_yscale("log"); ax.set_xlim(1, 1500); ax.set_ylim(5e-6, 1.2)
ax.set_xlabel("Round-trip time (µs)"); ax.set_ylabel("P(RTT > x)"); ax.grid(True)
ax.legend(loc="upper right")
fig.tight_layout(pad=0.3); fig.savefig(f"{OUT}/fig2_ccdf.png"); plt.close(fig)

# Fig. 3: timeline of one equal-priority run
fig, axs = plt.subplots(2, 1, figsize=(W, 2.7), sharex=True)
for ax, pol in zip(axs, ("fifo", "rr")):
    x = load(pol, "equal", 32, 1); t = np.cumsum(x) / 1000
    ax.scatter(t, x / 1000, s=1.2, color=C[pol], linewidths=0, rasterized=True)
    k = x > 1000
    ax.scatter(t[k], x[k] / 1000, s=9, color=C[pol], linewidths=0)
    ax.set_yscale("log"); ax.set_ylim(5e-4, 80); ax.set_ylabel("RTT (ms)")
    tr = mt.blended_transform_factory(ax.transAxes, ax.transData)
    for v, lab in ((3.75, "3.75 ms\nslice"), (40, "40 ms\nburst")):
        ax.axhline(v, color="#888888", lw=0.6, ls=":")
        ax.text(1.01, v, lab, transform=tr, va="center", fontsize=6.5, color="#555555")
    ax.set_title(NM[pol], loc="left", fontsize=7.5, pad=2); ax.grid(True, axis="y")
axs[1].set_xlabel("Elapsed time (ms)")
fig.tight_layout(pad=0.3); fig.subplots_adjust(right=0.86)
fig.savefig(f"{OUT}/fig3_timeline.png"); plt.close(fig)

# Fig. 4: distribution of delays > 1 ms
fig, axs = plt.subplots(1, 2, figsize=(W, 1.9), sharey=True)
bins = np.logspace(0, np.log10(50), 40)
for ax, ld, ttl in zip(axs, ("equal", "above"), ("(a) Equal priority", "(b) Higher priority")):
    for pol in ("fifo", "rr"):
        v = np.concatenate([(lambda a: a[a > 1000])(load(pol, ld, 32, r)) for r in RUNS]) / 1000
        ax.hist(v, bins=bins, histtype="step", color=C[pol], ls=LS[pol], label=NM[pol])
    ax.set_xscale("log"); ax.set_yscale("log"); ax.set_title(ttl, fontsize=7.5, pad=2)
    ax.set_xlabel("Delay > 1 ms (ms)"); ax.grid(True, axis="y")
    for v in (3.75, 40): ax.axvline(v, color="#888888", lw=0.6, ls=":")
    ax.set_xticks([1, 3.75, 10, 40]); ax.set_xticklabels(["1", "3.75", "10", "40"])
axs[0].set_ylabel("Count (10 runs)"); axs[1].legend(loc="upper left")
fig.tight_layout(pad=0.3); fig.savefig(f"{OUT}/fig4_delays.png"); plt.close(fig)
print(f"\nFigures written to {OUT}/")
