#!/usr/bin/env python3
"""Rebuild the bundled fonts as smaller subsets.

The app ships two faces: Sarasa Gothic SC for the interface and JetBrains Maple
Mono for code. Both carry ~21 000 hanzi, which is where nearly all of the 40 MB
goes; the binary only ever draws a few thousand of them.

What is kept:
  * Latin, Greek, punctuation, arrows, maths, box drawing, geometric shapes,
    kana, CJK punctuation and fullwidth forms (everything the UI draws:
    Ω µ ° ± ² → ⇧ ▸ ⌘ ─ └ …)
  * every character that appears in i18n/*.json and in the C++ sources, so a
    string added tomorrow cannot lose its glyph without this script noticing
  * one CJK repertoire, chosen with --set:
      gb2312  6763 hanzi, the everyday simplified set   (default, ~8.7 MB total)
      gbk     21 003 hanzi, adds traditional forms      (~29 MB)
      full    whatever the master has                   (~41 MB)

Glyph outlines and hinting are untouched, so a subset looks identical at the
same size — it just has fewer glyphs. A character outside the repertoire is
drawn by the fallback chain in gfx.cpp (the other bundled face, then system
fonts such as Microsoft YaHei or PingFang), which is why the desktop builds
still show a rare name or a Japanese kanji correctly. The web and mobile builds
have no system fonts to fall back on, so there a missing glyph is a box: build
them with --set gbk if that matters for your users.

Usage:
  python3 tools/subset_fonts.py                 # rebuild assets/fonts from the cache
  python3 tools/subset_fonts.py --set gbk
  python3 tools/subset_fonts.py --masters DIR   # use already-downloaded TTFs

The masters are not in the repository (they are the 40 MB this script removes).
It downloads them from the upstream releases into .cache/fonts on first use.
"""
import argparse
import glob
import os
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Upstream releases the masters come from (see assets/fonts/OFL-*.txt).
MASTERS = {
    "SarasaGothicSC-Regular.ttf": (
        "https://github.com/be5invis/Sarasa-Gothic/releases/download/v1.0.43/"
        "SarasaGothicSC-TTF-1.0.43.7z",
        "SarasaGothicSC-TTF/SarasaGothicSC-Regular.ttf",
    ),
    "JetBrainsMapleMono-Regular.ttf": (
        "https://github.com/SpaceTimee/Fusion-JetBrainsMapleMono/releases/download/1.2304.79/"
        "JetBrainsMapleMono-XX-XX-NL-HT.zip",
        "JetBrainsMapleMono-XX-XX-NL-HT/JetBrainsMapleMono-Regular.ttf",
    ),
}

# Character ranges the interface draws with these faces.
RANGES = [
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x00FF),  # Latin-1: ° ± ² µ · ×
    (0x0370, 0x03FF),  # Greek: Ω β
    (0x2000, 0x206F),  # general punctuation: – — “ ” • …
    (0x2070, 0x209F),  # super/subscripts
    (0x20A0, 0x20CF),  # currency
    (0x2100, 0x214F),  # letterlike: ℃ №
    (0x2190, 0x21FF),  # arrows: → ↔ ⇧
    (0x2200, 0x22FF),  # maths: − ≈ ≥
    (0x2300, 0x23FF),  # misc technical: ⌘
    (0x2460, 0x24FF),  # circled digits
    (0x2500, 0x257F),  # box drawing: ─ └
    (0x25A0, 0x25FF),  # geometric: ▲ ▶ ▸ ●
    (0x2600, 0x27BF),  # misc symbols: ★ ✓
    (0x3000, 0x303F),  # CJK punctuation: 。、「」【】
    (0x3040, 0x30FF),  # kana, so a Japanese pack has glyphs to draw with
    (0xFB00, 0xFB06),  # Latin ligatures
    (0xFF00, 0xFFEF),  # fullwidth forms: （）：；！？
]


def hanzi(codec):
    """Every hanzi the given Chinese codec can encode."""
    out = set()
    for cp in range(0x3000, 0xA000):
        ch = chr(cp)
        try:
            ch.encode(codec)
        except Exception:
            continue
        out.add(ch)
    return out


def scan_text(paths, exts):
    """Characters the app can display today: translation packs and source literals."""
    out = set()
    for path in paths:
        for f in glob.glob(os.path.join(path, exts), recursive=True):
            try:
                with open(f, encoding="utf-8") as fh:
                    out |= {c for c in fh.read() if ord(c) > 0x7E}
            except (UnicodeDecodeError, OSError):
                continue
    return out


def charset(kind):
    chars = set()
    for a, b in RANGES:
        chars |= {chr(c) for c in range(a, b + 1)}
    if kind == "gb2312":
        chars |= hanzi("gb2312")
    elif kind == "gbk":
        chars |= hanzi("gbk")
    chars |= scan_text([os.path.join(ROOT, "i18n")], "*.json")
    chars |= scan_text([os.path.join(ROOT, "src")], "*.[ch]*")
    return chars


def ensure_master(name, cache):
    path = os.path.join(cache, name)
    if os.path.exists(path):
        return path
    url, member = MASTERS[name]
    os.makedirs(cache, exist_ok=True)
    archive = os.path.join(cache, os.path.basename(url))
    if not os.path.exists(archive):
        print("downloading %s" % url)
        urllib.request.urlretrieve(url, archive)
    print("extracting %s" % member)
    if archive.endswith(".7z"):
        # 7z is the only format Sarasa publishes; py7zr or the 7z CLI must be around.
        try:
            import py7zr
            with py7zr.SevenZipFile(archive) as z:
                z.extract(path=os.path.dirname(path), targets=[member.split("/")[-1]])
            got = os.path.join(os.path.dirname(path), member.split("/")[-1])
            os.replace(got, path)
        except ImportError:
            subprocess.run(["7z", "e", "-y", "-o" + os.path.dirname(path), archive, member], check=True)
            os.replace(os.path.join(os.path.dirname(path), os.path.basename(member)), path)
    else:
        import zipfile
        with zipfile.ZipFile(archive) as z:
            with z.open(member) as src, open(path, "wb") as dst:
                dst.write(src.read())
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", dest="kind", choices=("gb2312", "gbk", "full"), default="gb2312")
    ap.add_argument("--masters", help="directory with the full TTFs (default: .cache/fonts)")
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "fonts"))
    args = ap.parse_args()

    try:
        import fontTools  # noqa: F401
    except ImportError:
        sys.exit("fontTools is required: python -m pip install fonttools brotli")

    cache = args.masters or os.path.join(ROOT, ".cache", "fonts")
    os.makedirs(args.out, exist_ok=True)
    text = os.path.join(cache, "subset_chars.txt")
    os.makedirs(cache, exist_ok=True)
    chars = charset(args.kind)
    with open(text, "w", encoding="utf-8", newline="") as fh:
        fh.write("".join(sorted(chars)))
    print("%s set: %d characters" % (args.kind, len(chars)))

    total = 0
    for name in MASTERS:
        master = ensure_master(name, cache)
        out = os.path.join(args.out, name)
        cmd = [sys.executable, "-m", "fontTools.subset", master, "--output-file=" + out,
               "--layout-features=*", "--name-IDs=*", "--drop-tables+=DSIG", "--recalc-bounds"]
        if args.kind != "full":  # "full" keeps every glyph the master has
            cmd.append("--text-file=" + text)
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.exit("subset failed for %s:\n%s" % (name, r.stderr[-800:]))
        size = os.path.getsize(out)
        total += size
        print("  %-34s %6.1f MB" % (name, size / 1e6))
    print("fonts total: %.1f MB" % (total / 1e6))


if __name__ == "__main__":
    main()
