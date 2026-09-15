import csv
import glob
import os
import sys

# Summarize request timings, CPU usage, and adaptive traces
resultsDir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.realpath(__file__)), "results")

cpu = {}
cpuPath = os.path.join(resultsDir, "cpu.csv")
if os.path.exists(cpuPath):
    for row in csv.reader(open(cpuPath)):
        cpu[row[0]] = {"wall": float(row[6]), "jiffies": int(row[7])}

print(f"{'name':38} {'Gbit/s':>8} {'steady':>8} {'tail s':>7} {'GB':>8} {'wall s':>7} {'cpu-s':>8} {'cpu-s/TB':>9} {'avg cpu':>8} {'avg thr':>8} {'t95 s':>6}")
for path in sorted(glob.glob(os.path.join(resultsDir, "*.csv"))):
    if path.endswith("cpu.csv") or path.endswith(".adaptive"):
        continue
    name = os.path.basename(path)[:-4]
    rows = list(csv.DictReader(open(path)))
    if not rows or "Start" not in rows[0]:
        continue
    start = min(int(r["Start"]) for r in rows)
    finish = max(int(r["Finish"]) for r in rows)
    size = sum(int(r["Size"]) for r in rows)
    wall = (finish - start) / 1e6
    gbit = size * 8 / wall / 1e9

    # Median bytes per second and tail after 99% of bytes
    buckets = {}
    for r in rows:
        b = (int(r["Finish"]) - start) // 1000000
        buckets[b] = buckets.get(b, 0) + int(r["Size"])
    perSec = [buckets.get(b, 0) for b in range(max(buckets) + 1)]
    steady = sorted(perSec)[len(perSec) // 2] * 8 / 1e9
    cum = 0
    t99 = 0
    for b, v in enumerate(perSec):
        cum += v
        if cum >= 0.99 * size:
            t99 = b
            break
    tail = wall - t99

    cpuSec = cpu.get(name, {}).get("jiffies", 0) / 100
    cpuPerTb = cpuSec / (size / 1e12) if size else 0
    avgCpu = cpuSec / wall if wall else 0

    # Time-weighted threads and time to 95% of steady bandwidth
    avgThreads = ""
    t95 = ""
    trace = path + ".adaptive"
    if os.path.exists(trace):
        t = [(int(r["Time"]), int(r["Threads"]), int(r["TransferredBytes"])) for r in csv.DictReader(open(trace))]
        if len(t) > 2:
            intervals = [(t[i + 1][0] - t[i][0], t[i][1], (t[i + 1][2] - t[i][2]) * 8e6 / (t[i + 1][0] - t[i][0]) / 1e9) for i in range(len(t) - 1)]
            total = sum(d for d, _, _ in intervals)
            avgThreads = f"{sum(d * thr for d, thr, _ in intervals) / total:.1f}"
            steady = sorted(bw for _, _, bw in intervals)[len(intervals) // 2]
            elapsed = 0
            for d, _, bw in intervals:
                elapsed += d
                if bw >= 0.95 * steady:
                    t95 = f"{elapsed / 1e6:.0f}"
                    break

    print(f"{name:38} {gbit:8.1f} {steady:8.1f} {tail:7.1f} {size / 1e9:8.1f} {wall:7.1f} {cpuSec:8.1f} {cpuPerTb:9.1f} {avgCpu:8.1f} {avgThreads:>8} {t95:>6}")
