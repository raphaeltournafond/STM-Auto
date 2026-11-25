#!/usr/bin/env python3
"""Analyse STM-Auto telemetry logs and propose vehicle-calibrated low-pressure floors.

The firmware prints one CSV line every 500 ms on the debug Serial (USART2):

    T,t_ms,ohm,degC,volt,bar,situation,flap,ack

plus '#' header lines (firmware revision, floors in use). Record a few drives
(see README "Calibration"), then:

    python scripts/analyze_log.py drive1.log drive2.log
    python scripts/analyze_log.py drive*.log --plot calib.png
    python scripts/analyze_log.py drive*.log --apply      # rewrite the floors in decision.cpp

What it does
- Drops sensor-fault samples and "engine off" samples (pressure below
  --engine-on-bar, plus --settle-s around them: spin-up / spin-down).
- Per 10 °C oil-temperature bin: sample count, pressure P5 / P50 / P95 / max.
- Per §4 temperature band (<70, 70-89, 90-110, 110-122 °C): P5 of the pressure,
  i.e. the hot-idle pressure (at a given temperature the lowest pressure is at
  idle), and the proposed floors:
      critical = max(PRESS_CRITICAL_MIN, alarm_ratio x P5)   (default 50 %)
      rearm    = max(PRESS_REARM_MIN, critical + generic gap)
  The generic floors in decision.h are a hard minimum: a calibration can only
  raise them (decision.cpp static_asserts it too). Band 4 (> 122 °C) is the
  stop-engine pairing and is never changed.
- Reports overpressure margin (vs PRESS_OVER) and time spent hot.

Only the Python standard library is required; --plot needs matplotlib.
"""

import argparse
import math
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DECISION_H = os.path.join(ROOT, "lib", "decision", "decision.h")
DECISION_CPP = os.path.join(ROOT, "lib", "decision", "decision.cpp")

BAND_NAMES = ["<70 C", "70-89 C", "90-110 C", "110-122 C", ">122 C"]
CALIBRATED_BANDS = 4  # bands 0..3; band 4 (stop-engine pairing) stays as-is
FAULT_SITUATIONS = {"TEMP SENS ERR", "PRESS SENS ERR"}

LINE_RE = re.compile(
    r"(?:^|[^A-Za-z0-9])T,(\d+),([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+),([^,]+),([01]),([01])\s*$")
FW_RE = re.compile(r"#\s*STM-Auto fw\s+(.*)$")


# ---------------------------------------------------------------- source constants

def _floats(text):
    return [float(x.rstrip("fF")) for x in re.findall(r"-?\d+(?:\.\d+)?[fF]?", text)]


def read_array(path, name):
    src = open(path, encoding="utf-8").read()
    m = re.search(name + r"\s*\[\s*\d*\s*\]\s*=\s*\{([^}]*)\}", src)
    if not m:
        sys.exit("error: %s not found in %s" % (name, path))
    return _floats(m.group(1))


def read_scalar(path, name):
    src = open(path, encoding="utf-8").read()
    m = re.search(r"\b" + name + r"\s*=\s*(-?\d+(?:\.\d+)?)[fF]?", src)
    if not m:
        sys.exit("error: %s not found in %s" % (name, path))
    return float(m.group(1))


# ---------------------------------------------------------------- log parsing

class Sample:
    __slots__ = ("t_ms", "ohm", "temp", "volt", "bar", "sit", "file")

    def __init__(self, t_ms, ohm, temp, volt, bar, sit, file):
        self.t_ms, self.ohm, self.temp, self.volt, self.bar, self.sit, self.file = \
            t_ms, ohm, temp, volt, bar, sit, file


def parse_logs(paths):
    samples, firmwares = [], set()
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                fw = FW_RE.search(line)
                if fw:
                    firmwares.add(fw.group(1).strip())
                    continue
                m = LINE_RE.search(line)
                if not m:
                    continue
                try:
                    samples.append(Sample(int(m.group(1)), float(m.group(2)), float(m.group(3)),
                                          float(m.group(4)), float(m.group(5)),
                                          m.group(6).strip(), path))
                except ValueError:
                    continue  # truncated / garbled line
    return samples, firmwares


def segments(samples):
    """Split each file's samples into runs of increasing t_ms (a reboot restarts t)."""
    run, prev = [], None
    for s in samples:
        if prev is not None and (s.file != prev.file or s.t_ms < prev.t_ms):
            yield run
            run = []
        run.append(s)
        prev = s
    if run:
        yield run


def filter_samples(samples, engine_on_bar, settle_ms):
    kept, n_fault, n_off = [], 0, 0
    for run in segments(samples):
        off_times = [s.t_ms for s in run if s.sit not in FAULT_SITUATIONS and s.bar < engine_on_bar]
        j = 0
        for s in run:
            if s.sit in FAULT_SITUATIONS:
                n_fault += 1
                continue
            while j < len(off_times) and off_times[j] < s.t_ms - settle_ms:
                j += 1
            if j < len(off_times) and abs(off_times[j] - s.t_ms) <= settle_ms:
                n_off += 1
                continue
            kept.append(s)
    return kept, n_fault, n_off


def total_time_ms(samples, pred):
    """Time spent where pred(sample) holds, from successive sample intervals (gaps capped)."""
    total = 0
    for run in segments(samples):
        for a, b in zip(run, run[1:]):
            if pred(a):
                total += min(b.t_ms - a.t_ms, 2000)
    return total


# ---------------------------------------------------------------- statistics

def percentile(values, p):
    """Linear-interpolated percentile (0..100) of a non-empty list."""
    v = sorted(values)
    if len(v) == 1:
        return v[0]
    k = (len(v) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def band_of(temp):
    # mirrors temperatureBand() in lib/decision/decision.cpp
    if temp < 70:
        return 0
    if temp < 90:
        return 1
    if temp < 110:
        return 2
    if temp <= 122:
        return 3
    return 4


def round_to(x, step=0.05):
    return round(round(x / step) * step, 2)


def propose(kept, crit_min, rearm_min, current_crit, current_rearm, alarm_ratio, min_samples):
    rows, crit, rearm = [], list(current_crit), list(current_rearm)
    for b in range(CALIBRATED_BANDS):
        pressures = [s.bar for s in kept if band_of(s.temp) == b]
        row = {"band": b, "n": len(pressures), "p5": None, "note": ""}
        gap = max(rearm_min[b] - crit_min[b], 0.2)
        if len(pressures) < min_samples:
            row["note"] = "not enough data (< %d samples), kept current" % min_samples
        else:
            p5 = percentile(pressures, 5)
            row["p5"] = p5
            crit[b] = max(crit_min[b], round_to(alarm_ratio * p5))
            rearm[b] = max(rearm_min[b], round_to(crit[b] + gap))
            if rearm[b] > p5:
                row["note"] = "WARNING: rearm above P5 - the alarm might not clear at idle"
        rows.append(row)
    return rows, crit, rearm


# ---------------------------------------------------------------- output

def fmt_ms(ms):
    s = int(ms // 1000)
    return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def cpp_arrays(crit, rearm):
    f = lambda xs: ", ".join("%.2ff" % x for x in xs)
    return ("static constexpr float pressCritical[] = {%s}; // alarm below\n"
            "static constexpr float pressRearm[]    = {%s}; // clear at/above" % (f(crit), f(rearm)))


def apply_to_source(crit, rearm):
    src = open(DECISION_CPP, encoding="utf-8").read()
    new = re.sub(r"(static constexpr float pressCritical\[\]\s*=\s*\{)[^}]*(\})",
                 lambda m: m.group(1) + ", ".join("%.2ff" % x for x in crit) + m.group(2), src)
    new = re.sub(r"(static constexpr float pressRearm\[\]\s*=\s*\{)[^}]*(\})",
                 lambda m: m.group(1) + ", ".join("%.2ff" % x for x in rearm) + m.group(2), new)
    if new == src:
        print("decision.cpp already has these floors - nothing to change.")
        return
    with open(DECISION_CPP, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    print("Updated %s - review the diff, run the tests, rebuild and flash."
          % os.path.relpath(DECISION_CPP, ROOT))


def plot(kept, current_crit, proposed_crit, press_over, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("error: --plot needs matplotlib (pip install matplotlib)")
    edges = [40, 70, 90, 110, 122, 150]

    def steps(values):
        xs, ys = [], []
        for b in range(5):
            xs += [edges[b], edges[b + 1]]
            ys += [values[b], values[b]]
        return xs, ys

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.scatter([s.temp for s in kept], [s.bar for s in kept], s=3, alpha=0.3, label="samples")
    ax.plot(*steps(current_crit), "k--", label="current alarm floor")
    ax.plot(*steps(proposed_crit), "r-", label="proposed alarm floor")
    ax.axhline(press_over, color="orange", linestyle=":", label="overpressure (PRESS_OVER)")
    ax.set_xlabel("oil temperature (C)")
    ax.set_ylabel("oil pressure (bar)")
    ax.set_xlim(38, 152)
    ax.set_ylim(bottom=0)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right")
    ax.set_title("STM-Auto oil pressure vs temperature (%d samples)" % len(kept))
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    print("Plot written to %s" % path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="+", help="serial log files (lines 'T,...')")
    ap.add_argument("--alarm-ratio", type=float, default=0.5,
                    help="alarm floor = ratio x P5 (hot-idle pressure) of the band (default 0.5)")
    ap.add_argument("--min-samples", type=int, default=240,
                    help="samples needed in a band to calibrate it (default 240 = 2 min)")
    ap.add_argument("--engine-on-bar", type=float, default=0.3,
                    help="below this the engine is considered off (default 0.3 bar)")
    ap.add_argument("--settle-s", type=float, default=3.0,
                    help="also drop samples this close to an engine-off sample (default 3 s)")
    ap.add_argument("--plot", metavar="PNG", help="write a pressure-vs-temperature plot (matplotlib)")
    ap.add_argument("--apply", action="store_true",
                    help="write the proposed floors into lib/decision/decision.cpp")
    args = ap.parse_args(argv)
    if not 0.1 <= args.alarm_ratio <= 0.9:
        ap.error("--alarm-ratio must be between 0.1 and 0.9")

    crit_min = read_array(DECISION_H, "PRESS_CRITICAL_MIN")
    rearm_min = read_array(DECISION_H, "PRESS_REARM_MIN")
    current_crit = read_array(DECISION_CPP, "pressCritical")
    current_rearm = read_array(DECISION_CPP, "pressRearm")
    press_over = read_scalar(DECISION_H, "PRESS_OVER")
    temp_overheat = read_scalar(DECISION_H, "TEMP_OVERHEAT")

    samples, firmwares = parse_logs(args.logs)
    if not samples:
        sys.exit("error: no 'T,...' telemetry lines found in %s" % ", ".join(args.logs))
    kept, n_fault, n_off = filter_samples(samples, args.engine_on_bar, args.settle_s * 1000)

    print("Logs: %d file(s), %d samples, firmware: %s"
          % (len(args.logs), len(samples), ", ".join(sorted(firmwares)) or "(no header seen)"))
    print("Dropped: %d sensor-fault, %d engine-off / settling (< %.2f bar +- %.0f s) -> %d kept"
          % (n_fault, n_off, args.engine_on_bar, args.settle_s, len(kept)))
    if not kept:
        sys.exit("error: nothing left to analyse")

    print("\nOil temp   n       P5     P50    P95    max   (bar)")
    for lo in range(40, 150, 10):
        bin_p = [s.bar for s in kept if lo <= s.temp < lo + 10 or (lo == 140 and s.temp >= 150)]
        if bin_p:
            print("%3d-%-3d  %6d  %6.2f %6.2f %6.2f %6.2f" % (
                lo, lo + 9, len(bin_p), percentile(bin_p, 5), percentile(bin_p, 50),
                percentile(bin_p, 95), max(bin_p)))
    print("(below 40 C the sensor table bottoms out: cold readings show as 40 C)")

    p_max = max(kept, key=lambda s: s.bar)
    near_over = sum(1 for s in kept if s.bar > press_over - 0.5)
    print("\nMax pressure %.2f bar at %.0f C; %d sample(s) within 0.5 bar of PRESS_OVER (%.1f bar)"
          % (p_max.bar, p_max.temp, near_over, press_over))
    t_max = max(s.temp for s in kept)
    hot_ms = total_time_ms(kept, lambda s: s.temp >= temp_overheat)
    print("Max oil temp %.1f C; time >= %.0f C (overheat alarm): %s"
          % (t_max, temp_overheat, fmt_ms(hot_ms)))

    rows, crit, rearm = propose(kept, crit_min, rearm_min, current_crit, current_rearm,
                                args.alarm_ratio, args.min_samples)
    print("\nLow-pressure floors (DECISION_MATRIX 4): alarm = %.0f %% of P5, never below the minimum"
          % (args.alarm_ratio * 100))
    print("band        n      P5    minimum      current      proposed    (crit/rearm, bar)")
    for row in rows:
        b = row["band"]
        p5 = "%5.2f" % row["p5"] if row["p5"] is not None else "   --"
        print("%-9s %6d  %s   %4.2f/%4.2f    %4.2f/%4.2f    %4.2f/%4.2f   %s" % (
            BAND_NAMES[b], row["n"], p5, crit_min[b], rearm_min[b], current_crit[b],
            current_rearm[b], crit[b], rearm[b], row["note"]))
    print("%-9s  (stop-engine pairing, not calibrated)   %4.2f/%4.2f"
          % (BAND_NAMES[4], current_crit[4], current_rearm[4]))

    print("\nFor lib/decision/decision.cpp:\n" + cpp_arrays(crit, rearm))
    if args.plot:
        plot(kept, current_crit, crit, press_over, args.plot)
    if args.apply:
        apply_to_source(crit, rearm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
