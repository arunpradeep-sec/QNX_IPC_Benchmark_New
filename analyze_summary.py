#!/usr/bin/env python3
"""
analyze_summary.py  —  QNX IPC benchmark analysis
Parses the summary.log produced by run_all.sh and prints a
per-condition table: mean median, 95% t-CI, mean P99,
mean-of-maxima, max-of-maxima, total over_1ms count.

Usage:
    python3 analyze_summary.py summary.log
"""

import re, sys, math, statistics

T95 = {1:12.706, 2:4.303, 3:3.182, 4:2.776, 5:2.571,
       6:2.447,  7:2.365, 8:2.306, 9:2.262, 19:2.093,
       29:2.045, 99:1.984}

def t95(n):
    df = n - 1
    return T95.get(df, 1.960)

def main(logfile):
    results = {}
    with open(logfile) as fh:
        for line in fh:
            m = re.match(
                r'(rtt_\w+)\.csv: n=(\d+) min=([\d.]+) '
                r'median=([\d.]+) p99=([\d.]+) max=([\d.]+) '
                r'us over_1ms=(\d+)', line)
            if not m:
                continue
            fname, n, mn, med, p99, mx, over = m.groups()
            parts   = fname.split('_')
            policy  = parts[1]
            load    = parts[3]
            payload = parts[4]
            key = (policy, load, payload)
            d = results.setdefault(key, {'medians':[], 'p99s':[],
                                         'maxes':[], 'over1ms':[]})
            d['medians'].append(float(med))
            d['p99s'].append(float(p99))
            d['maxes'].append(float(mx))
            d['over1ms'].append(int(over))

    hdr = (f"{'Policy':<6} {'Load':<6} {'Payload':>8}  "
           f"{'Med_mean':>9} {'Med_CI95':>9} {'P99_mean':>9} "
           f"{'Max_mean':>10} {'Max_max':>10} {'Over1ms':>7}")
    print(hdr)
    print("-" * len(hdr))

    load_order = {'none':0, 'below':1, 'equal':2, 'above':3}

    def sort_key(k):
        policy, load, payload = k
        return (policy, load_order.get(load, 9), int(payload))

    for key in sorted(results.keys(), key=sort_key):
        policy, load, payload = key
        d = results[key]
        meds = d['medians']
        n = len(meds)
        med_mean = statistics.mean(meds)
        p99_mean = statistics.mean(d['p99s'])
        max_mean = statistics.mean(d['maxes'])
        max_max  = max(d['maxes'])
        over_sum = sum(d['over1ms'])
        ci = t95(n) * statistics.stdev(meds) / math.sqrt(n) if n > 1 else 0.0
        print(f"{policy:<6} {load:<6} {payload:>8}  "
              f"{med_mean:>9.3f} {ci:>9.3f} {p99_mean:>9.3f} "
              f"{max_mean:>10.1f} {max_max:>10.1f} {over_sum:>7}")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} summary.log")
        sys.exit(1)
    main(sys.argv[1])
