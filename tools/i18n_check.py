#!/usr/bin/env python3
"""Check the translation packs against the keys the code asks for.

    python3 tools/i18n_check.py            # errors only
    python3 tools/i18n_check.py --strict    # warnings fail too (used by CI)

What it catches:
  * a key used in the source but missing from i18n/en.json (the UI would show the key)
  * a key in en.json that no pack fills in for another language
  * placeholder mismatches ("{0} … {1}" translated as "{0} …") — the classic
    bug that silently drops a number from a sentence
  * keys nobody references any more, so the packs do not grow forever

Keys built at runtime from data (part.<type>.title, cat.<slug>, …) cannot be
found by scanning, so their prefixes are allowed without a literal call site.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK_DIR = os.path.join(ROOT, "i18n")
SRC_DIR = os.path.join(ROOT, "src")

# Built from data at runtime: part.<type>.title, cat.<slug>, propval.<value>, …
DYNAMIC_PREFIXES = ("part.", "cat.", "prop.", "propval.", "entry.", "ex.", "view.", "action.", "mouse.")

CALL_RE = re.compile(r'(?<![A-Za-z0-9_>.])tr([fn]?)\(\s*"((?:[^"\\]|\\.)*)"')
# Only key-shaped literals. A general "..." pattern would pair the quote inside a
# character literal like c == '"' with the next real quote and desynchronise the
# whole scan; keys never contain quotes, spaces or slashes.
LITERAL_RE = re.compile(r'"([A-Za-z0-9_.\-]+)"')
PLACEHOLDER_RE = re.compile(r"\{(\d+)\}")


def load_pack(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return {k: v for k, v in data.items() if k != "_meta"}, data.get("_meta", {})


def strip_comments(text):
    """Blanks out // and /* */ comments, keeping newlines so line numbers survive.
    String and character literals are respected, so "http://…" is not a comment."""
    out = list(text)
    i, n, state = 0, len(text), None
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            i += 1
            continue
        if state in ("str", "chr"):
            if c == "\\":
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = None
            i += 1
            continue
        # inside a comment
        if state == "line":
            if c == "\n":
                state = None
            else:
                out[i] = " "
            i += 1
            continue
        if c == "*" and nxt == "/":
            out[i] = out[i + 1] = " "
            state = None
            i += 2
            continue
        if c != "\n":
            out[i] = " "
        i += 1
    return "".join(out)


def scan_source():
    """Returns ({key: [(file, line), ...]} for literal tr() calls,
    {plural root: [...]} for trn(), and every string literal in the tree).

    Keys passed through a variable — tr(nameKeys[i]), tr(cond ? "a.b" : "c.d") —
    are not visible to CALL_RE, so the literal set is what decides whether a key
    is really dead.
    """
    used = {}
    plurals = {}
    literals = set()
    for name in sorted(os.listdir(SRC_DIR)):
        if not name.endswith((".cpp", ".h")):
            continue
        path = os.path.join(SRC_DIR, name)
        with open(path, "r", encoding="utf-8") as f:
            raw = f.read()
        code = strip_comments(raw)
        literals.update(LITERAL_RE.findall(raw))
        for lineno, line in enumerate(code.splitlines(), 1):
            for fn, key in CALL_RE.findall(line):
                if key.startswith("_meta"):
                    continue
                if fn == "n":
                    plurals.setdefault(key, []).append((name, lineno))
                else:
                    used.setdefault(key, []).append((name, lineno))
    return used, plurals, literals


def main():
    strict = "--strict" in sys.argv
    errors, warnings = [], []

    packs = {}
    metas = {}
    for name in sorted(os.listdir(PACK_DIR)):
        if not name.endswith(".json"):
            continue
        code = name[:-5]
        packs[code], metas[code] = load_pack(os.path.join(PACK_DIR, name))

    if "en" not in packs:
        print("i18n/en.json is missing")
        return 1
    en = packs["en"]

    used, plurals, literals = scan_source()

    # 1. every literal key must exist in English
    for key, sites in sorted(used.items()):
        if key not in en:
            for f, line in sites:
                errors.append(f'{f}:{line}: key "{key}" is not in i18n/en.json')

    # 2. plural families need .other (and .one if the language distinguishes it)
    for root, sites in sorted(plurals.items()):
        for suffix in (".one", ".other"):
            key = root + suffix
            if key not in en:
                for f, line in sites:
                    errors.append(f'{f}:{line}: "{key}" is missing (used by trn("{root}"))')

    # 3. English is the reference: every other language must cover it
    for code, table in sorted(packs.items()):
        if code == "en":
            continue
        for key, text in en.items():
            if key not in table:
                errors.append(f"{code}.json: missing \"{key}\"")
                continue
            want = set(PLACEHOLDER_RE.findall(text))
            have = set(PLACEHOLDER_RE.findall(table[key]))
            if want != have:
                errors.append(
                    f'{code}.json: "{key}" placeholders {sorted(have)} != English {sorted(want)}'
                )
        for key in table:
            if key not in en:
                warnings.append(f'{code}.json: "{key}" is not in i18n/en.json')

    # 4. dead keys (dynamic prefixes are allowed to have no call site)
    for key in en:
        if key in used or key in literals:
            continue
        root = key.rsplit(".", 1)[0]
        if root in plurals:
            continue
        if key.startswith(DYNAMIC_PREFIXES):
            continue
        warnings.append(f'i18n/en.json: "{key}" is never used')

    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error:   {e}")

    total = sum(len(t) for t in packs.values())
    print(
        f"\n{len(en)} English keys, {len(used)} referenced in code, "
        f"{len(packs)} pack(s) ({total} strings): "
        f"{len(errors)} error(s), {len(warnings)} warning(s)"
    )
    if errors or (strict and warnings):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
