"""Set the status of keys in docs/design/PARITY_PLAN.md and print the counts per tier.

    python tools/dev/parity_status.py "key=done:note" ["key2=skip:why" ...]

Rows are the plan table's: | box | category | key | type | label | default | tier | status | note |
"""
import os
import sys
from collections import Counter

PLAN = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "design", "PARITY_PLAN.md")


def main(args):
    with open(PLAN, encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    for arg in args:
        key, _, rest = arg.partition("=")
        status, _, note = rest.partition(":")
        hits = 0
        for i, line in enumerate(lines):
            cells = line.split("|")
            # ['', box, category, key, type, label, default, tier, status, note, '']
            if len(cells) == 11 and cells[3].strip() == key:
                cells[8] = f" {status} "
                cells[9] = f" {note} " if note else " "
                lines[i] = "|".join(cells)
                hits += 1
        if hits == 0:
            print(f"no row for {key}")
    with open(PLAN, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))

    counts = Counter()
    for line in lines:
        cells = line.split("|")
        if len(cells) == 11 and cells[7].strip() in ("1", "2", "3", "4", "-"):
            counts[(cells[7].strip(), cells[8].strip())] += 1
    print("Counts: " + ", ".join(f"tier {t} {s}: {n}" for (t, s), n in sorted(counts.items())))


if __name__ == "__main__":
    main(sys.argv[1:])
