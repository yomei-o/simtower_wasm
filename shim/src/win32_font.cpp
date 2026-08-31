// Text, from the baked bitmap font.
//
// The alternative was to hand strings to the browser and read back the pixels,
// which gets real fonts for free but gives a different picture on every machine
// - and this port is worth being able to compare against itself, natively and
// in WebAssembly.  The sizes are bounded anyway: the game's font cache holds ten
// heights, clamped to a minimum of nine pixels.
//
// See tools/make_font.py for how the data is produced.

#include "win32_internal.h"
#include "win32_font_data.h"

#include <cstring>

namespace shim {

namespace {

const font::Face & faceFor(int size) {
    // Nearest available, not next-largest: a 15-pixel request looks better at
    // 14 or 16 than it does stretched.
    const font::Face * best = &font::kFaces[0];
    int bestDistance = 1 << 30;
    for (int i = 0; i < font::kFaceCount; i++) {
        const int distance = font::kFaces[i].size > size
                           ? font::kFaces[i].size - size
                           : size - font::kFaces[i].size;
        if (distance < bestDistance) { bestDistance = distance; best = &font::kFaces[i]; }
    }
    return *best;
}

const font::Face & faceFor(DeviceContext & d) {
    GdiObject * f = object(d.font);
    return faceFor(f && f->fontHeight ? f->fontHeight : 12);
}

// The font holds one glyph per printable ASCII code.  Anything outside that -
// and the port does hand over the odd Latin-1 byte - draws as a space rather
// than as a wrong glyph or a crash.
const font::Glyph * glyphFor(const font::Face & face, unsigned char c) {
    if (c < font::kFirstChar || c > font::kLastChar) return nullptr;
    return &face.glyphs[c - font::kFirstChar];
}

// UTF-8 in, one code point out, advancing the cursor.  Only the ASCII range has
// glyphs, so anything wider collapses to a single unrepresentable character.
unsigned char nextChar(const char *& p, const char * end) {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    int extra = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : 3;
    while (extra-- > 0 && p < end && ((unsigned char)*p & 0xc0) == 0x80) p++;
    return 0;
}

}   // namespace


SIZE measureText(DeviceContext & d, const char * utf8, int count) {
    const font::Face & face = faceFor(d);
    SIZE size{0, face.ascent + face.descent};
    if (!utf8) return size;

    const char * p = utf8;
    const char * end = count < 0 ? utf8 + strlen(utf8) : utf8 + count;
    while (p < end) {
        const unsigned char c = nextChar(p, end);
        const font::Glyph * g = glyphFor(face, c);
        size.cx += g ? g->advance : face.size / 2;
    }
    return size;
}

void drawText(DeviceContext & d, int x, int y, const char * utf8, int count) {
    if (!utf8 || !d.target) return;

    const font::Face & face = faceFor(d);
    const char * begin = utf8;
    const char * end = count < 0 ? utf8 + strlen(utf8) : utf8 + count;

    const SIZE extent = measureText(d, utf8, count);

    // With TA_UPDATECP the position passed in is ignored entirely and the DC's
    // current position is used - the port sets that alignment on the info bar,
    // the palettes and the dialogs, then draws every string as TextOut(dc, 0,
    // 0, ...) after a MoveToEx.  Reading the pen only after drawing, and never
    // before it, put all of that text in the top-left corner: the funds, the
    // population and the date were being drawn, just not where they belong.
    const int startX = (d.textAlign & TA_UPDATECP) ? (int)d.current.x : x;
    const int startY = (d.textAlign & TA_UPDATECP) ? (int)d.current.y : y;

    // Windows places text by the alignment set on the DC, and the port sets
    // TA_RIGHT and TA_BASELINE in places where getting this wrong would put a
    // number in the wrong column.
    int penX = startX;
    if ((d.textAlign & TA_CENTER) == TA_CENTER) penX = startX - extent.cx / 2;
    else if (d.textAlign & TA_RIGHT)            penX = startX - extent.cx;

    int lineTop = startY;
    if (d.textAlign & TA_BASELINE)      lineTop = startY - face.ascent;
    else if (d.textAlign & TA_BOTTOM)   lineTop = startY - extent.cy;

    if (d.bkMode == OPAQUE)
        fillRect(d, RECT{penX, lineTop, penX + extent.cx, lineTop + extent.cy},
                 fromColorref(d.bkColour));

    const uint32_t ink = fromColorref(d.textColour);
    const char * p = begin;
    while (p < end) {
        const unsigned char c = nextChar(p, end);
        const font::Glyph * g = glyphFor(face, c);
        if (!g) { penX += face.size / 2; continue; }

        if (g->width > 0) {
            const int stride = (g->width + 7) / 8;
            const uint8_t * rows = font::kBits + g->offset;
            for (int row = 0; row < g->height; row++) {
                const uint8_t * bits = rows + (size_t)stride * row;
                for (int col = 0; col < g->width; col++)
                    if (bits[col / 8] & (0x80 >> (col % 8)))
                        blendPixel(d, penX + col, lineTop + g->top + row, ink);
            }
        }
        penX += g->advance;
    }

    // TA_UPDATECP asks for the pen to be left where the text ended, which is
    // how the port draws a run of numbers without recomputing each position.
    if (d.textAlign & TA_UPDATECP) {
        d.current.x = penX;
        d.current.y = startY;
    }
}

}   // namespace shim
