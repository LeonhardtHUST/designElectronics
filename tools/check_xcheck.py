#!/usr/bin/env python3
"""Checks the report written by `designElectronics --spice-xcheck`.

CI runs this after installing libngspice: a report full of `error` rows means the backend or a
fixture broke, and a report whose ngspice values are all identical is the signature of the reused
`ngGet_Vec_Info` structure (every vector holding the last query's samples, i.e. the time scale).

    python3 tools/check_xcheck.py spice-xcheck.md
"""
import re
import sys

# Expected model-fidelity deviations, not harness failures: the exported SPICE models are generic
# (one LED diode model for every colour, a fixed resistor for the light-dependent photocell), so
# those rows are allowed to be BAD. Anything else turning BAD is a regression.
EXPECTED_BAD = {"Blue LED limiter", "Green LED limiter", "Photocell divider"}


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def main(path):
    text = open(path, encoding="utf-8").read()
    rows = []
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        c = cells(line)
        if len(c) < 7 or c[0] == "Circuit" or set(c[0]) <= {"-", ":"}:
            continue
        rows.append(c)
    ok = [r for r in rows if r[6].startswith("`OK")]
    warn = [r for r in rows if r[6].startswith("`WARN")]
    bad = [r for r in rows if r[6].startswith("`BAD")]
    errored = [r for r in rows if r[6].startswith("`error") or r[6].startswith("`skipped")]

    problems = []
    if "Backend: `available`" not in text:
        problems.append("ngspice backend is not available")
    if len(ok) + len(warn) + len(bad) < 10:
        problems.append("fewer than 10 numeric rows (fixtures or backend broke)")
    if errored:
        problems.append("error rows: " + ", ".join(r[0] for r in errored))
    values = {r[3] for r in ok + warn + bad}
    if len(values) < 2:
        problems.append("every ngspice value is identical (vector mapping broken?)")
    unexpected = [r[0] for r in bad if r[0] not in EXPECTED_BAD]
    if unexpected:
        problems.append("unexpected BAD rows: " + ", ".join(unexpected))

    print(f"{len(rows)} rows: {len(ok)} ok, {len(warn)} warn, {len(bad)} bad, {len(errored)} error")
    for p in problems:
        print("FAIL:", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "spice-xcheck.md"))
