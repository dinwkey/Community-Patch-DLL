"""Parse Turn-perf.csv and aggregate timing by subsystem.

CSV format (per line):
  , 0, SubsystemLabel, Turn NNN, CivName, TimeInSeconds

Usage:
  python tools/parse_turn_perf.py [path_to_Turn-perf.csv]
"""
import collections, os, sys

def find_csv():
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        return sys.argv[1]
    home = os.environ.get("USERPROFILE", os.path.expanduser("~"))
    for docs in ["OneDrive/Documents", "Documents"]:
        p = os.path.join(home, docs, "My Games",
                         "Sid Meier's Civilization 5", "Logs", "Turn-perf.csv")
        if os.path.isfile(p):
            return p
    return None

PERF_CSV = find_csv()
if not PERF_CSV:
    print("ERROR: Turn-perf.csv not found"); sys.exit(1)
print(f"Reading: {PERF_CSV}\n")

# Parse rows — format depends on nesting depth:
# depth 0: ", 0, Label, Turn NNN, CivName, 0.123"
# depth 1: ", 1, Label, Turn NNN, CivName,, 0.123"
# depth 2: ", 2, Label, Turn NNN, CivName,,, 0.123"
rows = []  # (subsystem, turn_num, civ, seconds, depth)
with open(PERF_CSV, "r") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        # Split on comma (not ", ") to handle extra commas
        raw = line.split(",")
        # Find the time value as the last non-empty field
        time_val = None
        for i in range(len(raw) - 1, -1, -1):
            s = raw[i].strip()
            if s:
                try:
                    time_val = float(s)
                    break
                except ValueError:
                    break
        if time_val is None:
            continue
        # Fields: raw[0]=empty, raw[1]=depth, raw[2]=subsystem, raw[3]=turn, raw[4]=civ, raw[5..]=commas+time
        if len(raw) < 6:
            continue
        try:
            depth = int(raw[1].strip())
        except ValueError:
            continue
        subsystem = raw[2].strip()
        turn_str = raw[3].strip()       # e.g. "Turn 018"
        civ = raw[4].strip()
        try:
            turn_num = int(turn_str.split()[1])
        except (IndexError, ValueError):
            turn_num = -1
        rows.append((subsystem, turn_num, civ, time_val, depth))

turns = sorted(set(r[1] for r in rows))
print(f"Turns captured: {turns}")
print(f"Total rows: {len(rows)}\n")

# --- Aggregate by subsystem (depth-0 only = top-level, all depths = full) ---
# Show both: depth-0 = wall-clock top-level, all = per-subsystem breakdown
agg_all = collections.defaultdict(lambda: [0, 0.0])
agg_d0 = collections.defaultdict(lambda: [0, 0.0])
for sub, tn, civ, t, depth in rows:
    agg_all[sub][0] += 1
    agg_all[sub][1] += t
    if depth == 0:
        agg_d0[sub][0] += 1
        agg_d0[sub][1] += t

n_turns = len(turns) or 1

print("=" * 80)
print("TOP-LEVEL TIMERS (depth=0) — wall-clock time per subsystem")
print("=" * 80)
header = f"{'Subsystem':<40} {'Count':>6} {'Total(s)':>10} {'Per-Turn(s)':>12} {'% total':>8}"
print(header)
print("-" * len(header))
grand_d0 = sum(v[1] for v in agg_d0.values())
for k, v in sorted(agg_d0.items(), key=lambda x: -x[1][1]):
    pct = 100 * v[1] / grand_d0 if grand_d0 > 0 else 0
    print(f"{k:<40} {v[0]:>6} {v[1]:>10.3f} {v[1]/n_turns:>12.3f} {pct:>7.1f}%")
print(f"\n{'GRAND TOTAL (depth-0)':<40} {'':>6} {grand_d0:>10.3f} {grand_d0/n_turns:>12.3f}")

print("\n")
print("=" * 80)
print("ALL TIMERS (all depths) — includes nested subsystem breakdowns")
print("=" * 80)
print(header)
print("-" * len(header))
grand_all = sum(v[1] for v in agg_all.values())
for k, v in sorted(agg_all.items(), key=lambda x: -x[1][1]):
    pct = 100 * v[1] / grand_all if grand_all > 0 else 0
    print(f"{k:<40} {v[0]:>6} {v[1]:>10.3f} {v[1]/n_turns:>12.3f} {pct:>7.1f}%")
print(f"\n{'GRAND TOTAL (all depths)':<40} {'':>6} {grand_all:>10.3f} {grand_all/n_turns:>12.3f}")

# --- Per-turn breakdown (depth-1 subsystems = actual workload) ---
print("\n\n=== Per-turn breakdown (depth-1 = inner subsystems) ===")
per_turn = collections.defaultdict(lambda: collections.defaultdict(float))
for sub, tn, civ, t, depth in rows:
    if depth == 1:
        per_turn[tn][sub] += t

for turn in sorted(per_turn):
    total = sum(per_turn[turn].values())
    print(f"\nTurn {turn:03d} total (inner): {total:.3f}s")
    for label, time in sorted(per_turn[turn].items(), key=lambda x: -x[1]):
        pct = 100 * time / total if total > 0 else 0
        print(f"  {label:<40} {time:>8.3f}s ({pct:>5.1f}%)")

# --- Top 20 slowest individual entries (any depth) ---
print("\n\n=== Top 30 slowest individual entries ===")
rows_sorted = sorted(rows, key=lambda r: -r[3])
print(f"{'Subsystem':<40} {'D':>2} {'Turn':>5} {'Civ':<25} {'Time(s)':>10}")
print("-" * 87)
for sub, tn, civ, t, depth in rows_sorted[:30]:
    print(f"{sub:<40} {depth:>2} {tn:>5} {civ:<25} {t:>10.4f}")

# --- Per-civ breakdown for the slowest civs ---
print("\n\n=== Per-civ breakdown (depth-1, top 10 slowest civs) ===")
civ_total = collections.defaultdict(float)
civ_detail = collections.defaultdict(lambda: collections.defaultdict(float))
for sub, tn, civ, t, depth in rows:
    if depth == 1:
        civ_total[civ] += t
        civ_detail[civ][sub] += t

top_civs = sorted(civ_total, key=civ_total.get, reverse=True)[:10]
for civ in top_civs:
    print(f"\n{civ}: {civ_total[civ]:.3f}s total")
    for sub, t in sorted(civ_detail[civ].items(), key=lambda x: -x[1]):
        print(f"  {sub:<40} {t:>8.4f}s")
