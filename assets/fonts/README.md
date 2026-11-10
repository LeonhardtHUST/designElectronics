# Bundled fonts

Both files here are **subsets** of the upstream releases, rebuilt by
`tools/subset_fonts.py` to keep the download small. Nothing about their design,
outlines or hinting changed — they simply carry fewer glyphs.

| File | Upstream | Full size | Here |
|---|---|---|---|
| `SarasaGothicSC-Regular.ttf` | [Sarasa Gothic](https://github.com/be5invis/Sarasa-Gothic) 1.0.43, hinted TTF | 24.0 MB | 4.0 MB |
| `JetBrainsMapleMono-Regular.ttf` | [Fusion JetBrains Maple Mono](https://github.com/SpaceTimee/Fusion-JetBrainsMapleMono) 1.2304.79, `XX-XX-NL-HT` | 16.7 MB | 4.8 MB |

## What is kept

Latin, Greek, punctuation, arrows, maths, box drawing, geometric shapes, kana,
CJK punctuation and fullwidth forms, plus **GB2312** (6 763 hanzi). On top of
that, every character appearing in `i18n/*.json` or the C++ sources is forced
in, so a string added later cannot silently lose its glyph.

## What happens to everything else

`src/gfx.cpp` builds a fallback chain: the other bundled face first, then system
fonts (Microsoft YaHei and SimSun on Windows, PingFang and Arial Unicode on
macOS, Noto CJK on Linux). A traditional character, a rare name or emoji is
therefore drawn by the system font on desktop. The web and mobile builds have no
system fonts to fall back on, so there a missing glyph shows as a box — build
those with `--set gbk` if that matters.

## Regenerating

```bash
python3 tools/subset_fonts.py                     # GB2312 (what ships here)
python3 tools/subset_fonts.py --set gbk           # + traditional Chinese, 21 003 hanzi
python3 tools/subset_fonts.py --set full          # only drop what the app cannot show
```

The script downloads the masters into `.cache/fonts` on first use (they are the
40 MB this removes) and needs `fonttools`: `python3 -m pip install fonttools`.

## Licence

Both fonts are under the SIL Open Font License 1.1; the licence texts sit next
to them. Sarasa declares `Source` as its Reserved Font Name (the Adobe portion);
neither the Sarasa nor the JetBrains Maple Mono name is reserved, so a subset may
keep its name.
