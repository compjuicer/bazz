#!/usr/bin/env python3
"""Turn one embedded bitmap strike of a TTF (EBDT/EBLC) into a pixel-exact outline font: every pixel a square.

usage: strike2ttf.py src.ttf ppem dst.ttf "Family Name"
The result renders pixel-exact at font size ascent+descent of the strike (printed at the end).
"""
import sys
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

src, ppem, dst, family = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
PX = 64
font = TTFont(src)
idx = next(i for i, s in enumerate(font["EBLC"].strikes) if s.bitmapSizeTable.ppemY == ppem)
hori = font["EBLC"].strikes[idx].bitmapSizeTable.hori
ascent, descent = hori.ascender, -hori.descender
glyphs = font["EBDT"].strikeData[idx]
cmap = font.getBestCmap()

order, outlines, metrics, chars = [".notdef"], {}, {}, {}
empty = TTGlyphPen(None)
outlines[".notdef"] = empty.glyph()
metrics[".notdef"] = (ppem // 2 * PX, 0)
for code, name in sorted(cmap.items()):
    if code < 32 or code > 0x24F:
        continue
    if name not in glyphs:  # blank glyphs (space) have no bitmap: advance from the outline metrics
        if code == 32 or code == 0xA0:
            order.append(name) if name not in outlines else None
            outlines[name] = TTGlyphPen(None).glyph()
            metrics[name] = (round(font["hmtx"][name][0] * ppem / font["head"].unitsPerEm) * PX, 0)
            chars[code] = name
        continue
    g = glyphs[name]
    m = g.metrics
    pen = TTGlyphPen(None)
    for row in range(m.height):
        bits = g.getRow(row, bitDepth=1, metrics=m)
        y = m.BearingY - row - 1
        for x in range(m.width):
            if bits[x >> 3] & (0x80 >> (x & 7)):
                X0, Y0 = (m.BearingX + x) * PX, y * PX
                pen.moveTo((X0, Y0))
                pen.lineTo((X0, Y0 + PX))
                pen.lineTo((X0 + PX, Y0 + PX))
                pen.lineTo((X0 + PX, Y0))
                pen.closePath()
    if name not in outlines:
        order.append(name)
        outlines[name] = pen.glyph()
        metrics[name] = (m.Advance * PX, 0)
    chars[code] = name

fb = FontBuilder((ascent + descent) * PX, isTTF=True)
fb.setupGlyphOrder(order)
fb.setupCharacterMap(chars)
fb.setupGlyf(outlines)
fb.setupHorizontalMetrics(metrics)
fb.setupHorizontalHeader(ascent=ascent * PX, descent=-descent * PX)
fb.setupNameTable({"familyName": family, "styleName": "Regular"})
fb.setupOS2(sTypoAscender=ascent * PX, sTypoDescender=-descent * PX, usWinAscent=ascent * PX, usWinDescent=descent * PX)
fb.setupPost()
fb.save(dst)
print(f"{len(order) - 1} glyphs, ascent {ascent} descent {descent}: render at size {ascent + descent} -> {dst}")
