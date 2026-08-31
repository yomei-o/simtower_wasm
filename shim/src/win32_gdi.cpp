// GDI, in software.
//
// The port's drawing is almost entirely SetDIBitsToDevice and StretchDIBits -
// it hands over a block of palettised pixels and a palette - plus lines,
// rectangles and text for the chrome.  That is a good fit for a rasteriser and
// needs nothing from WebGL.
//
// Text is the exception and does not get a font table here.  It goes to the
// browser, which already has fonts and correct metrics; see hostMeasureText and
// hostDrawText in win32_host.cpp.  A hand-rolled bitmap font would be both
// bigger and worse.

#include "win32_internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace shim {

State & state() {
    static State s;
    return s;
}

void Surface::resize(int w, int h) {
    width = w < 0 ? 0 : w;
    height = h < 0 ? 0 : h;
    pixels.assign((size_t)width * height, 0xff000000u);
}

GdiObject * object(HGDIOBJ h) {
    if (!h) return nullptr;
    auto & m = state().objects;
    auto it = m.find((uintptr_t)h);
    return it == m.end() ? nullptr : &it->second;
}

DeviceContext * dc(HDC h) {
    if (!h) return nullptr;
    auto & m = state().dcs;
    auto it = m.find((uintptr_t)h);
    return it == m.end() ? nullptr : &it->second;
}

Menu * menu(HMENU h) {
    if (!h) return nullptr;
    auto & m = state().menus;
    auto it = m.find((uintptr_t)h);
    return it == m.end() ? nullptr : &it->second;
}

HGDIOBJ createObject(GdiObject o) {
    uintptr_t h = state().allocate();
    state().objects[h] = std::move(o);
    return (HGDIOBJ)h;
}


/* ------------------------------------------------------------- primitives */

// The part of a rectangle that can actually be written, in the DC's own
// coordinates, with the clip and the surface bounds applied once.  Doing this
// per pixel - a call, two bounds tests and a switch each time - is what made
// the world blit slow enough for the picture to fall behind and tear.
bool clipSpan(const DeviceContext & d, RECT r, RECT & out) {
    if (!d.target) return false;
    if (d.clip.right > d.clip.left && d.clip.bottom > d.clip.top) {
        if (r.left < d.clip.left) r.left = d.clip.left;
        if (r.top < d.clip.top) r.top = d.clip.top;
        if (r.right > d.clip.right) r.right = d.clip.right;
        if (r.bottom > d.clip.bottom) r.bottom = d.clip.bottom;
    }
    // The surface's own bounds, which sit at the DC's origin.
    if (r.left < -d.origin.x) r.left = -d.origin.x;
    if (r.top < -d.origin.y) r.top = -d.origin.y;
    if (r.right > d.target->width - d.origin.x)
        r.right = d.target->width - d.origin.x;
    if (r.bottom > d.target->height - d.origin.y)
        r.bottom = d.target->height - d.origin.y;
    out = r;
    return r.right > r.left && r.bottom > r.top;
}

void blendPixel(DeviceContext & d, int x, int y, uint32_t colour) {
    if (!d.target) return;
    const int tx = x + d.origin.x;
    const int ty = y + d.origin.y;
    if (d.clipped(x, y)) return;
    if (!d.target->contains(tx, ty)) return;
    uint32_t * p = d.target->row(ty) + tx;
    // R2_NOT and R2_XORPEN are the only rops the port asks for beyond a plain
    // copy, and both are used for rubber-band feedback.
    switch (d.rop2) {
        case R2_NOT:    *p = (~*p) | 0xff000000u; break;
        case R2_XORPEN: *p = (*p ^ colour) | 0xff000000u; break;
        default:        *p = colour; break;
    }
}

void fillRect(DeviceContext & d, RECT r, uint32_t colour) {
    RECT span;
    if (!clipSpan(d, r, span)) return;
    if (d.rop2 != R2_COPYPEN) {
        for (int y = span.top; y < span.bottom; y++)
            for (int x = span.left; x < span.right; x++)
                blendPixel(d, x, y, colour);
        return;
    }
    const int width = span.right - span.left;
    for (int y = span.top; y < span.bottom; y++) {
        uint32_t * out = d.target->row(y + d.origin.y) + span.left + d.origin.x;
        for (int i = 0; i < width; i++) out[i] = colour;
    }
}

void drawLine(DeviceContext & d, int x0, int y0, int x1, int y1, uint32_t colour) {
    // Bresenham.  Width is ignored: every pen the port creates is one pixel
    // wide, and a thick pen would need a different algorithm anyway.
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        blendPixel(d, x0, y0, colour);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}


/* ------------------------------------------------------------ stock objects */

static HGDIOBJ stockObject(int index) {
    // Created once and never deleted; DeleteObject refuses them, as Windows
    // does, so the port cannot free the desktop out from under itself.
    static HGDIOBJ cache[DEFAULT_GUI_FONT + 1] = {};
    if (index < 0 || index > DEFAULT_GUI_FONT) return nullptr;
    if (cache[index]) return cache[index];

    GdiObject o{};
    o.stock = true;
    switch (index) {
        case WHITE_BRUSH:  o.kind = ObjectKind::Brush; o.colour = RGB(255,255,255); break;
        case LTGRAY_BRUSH: o.kind = ObjectKind::Brush; o.colour = RGB(192,192,192); break;
        case GRAY_BRUSH:   o.kind = ObjectKind::Brush; o.colour = RGB(128,128,128); break;
        case DKGRAY_BRUSH: o.kind = ObjectKind::Brush; o.colour = RGB(64,64,64);    break;
        case BLACK_BRUSH:  o.kind = ObjectKind::Brush; o.colour = RGB(0,0,0);       break;
        case NULL_BRUSH:   o.kind = ObjectKind::Brush; o.hollow = true;             break;
        case WHITE_PEN:    o.kind = ObjectKind::Pen;   o.colour = RGB(255,255,255); break;
        case BLACK_PEN:    o.kind = ObjectKind::Pen;   o.colour = RGB(0,0,0);       break;
        case NULL_PEN:     o.kind = ObjectKind::Pen;   o.penStyle = PS_NULL;        break;
        default:
            o.kind = ObjectKind::Font;
            o.fontHeight = (index == OEM_FIXED_FONT || index == ANSI_FIXED_FONT ||
                            index == SYSTEM_FIXED_FONT) ? 13 : 12;
            o.fontFace = (index == OEM_FIXED_FONT || index == ANSI_FIXED_FONT ||
                          index == SYSTEM_FIXED_FONT) ? "monospace" : "sans-serif";
            break;
    }
    cache[index] = createObject(std::move(o));
    return cache[index];
}

extern "C" HGDIOBJ GetStockObject(int index) { return stockObject(index); }


/* ------------------------------------------------------- object lifecycle */

extern "C" HBRUSH CreateSolidBrush(COLORREF colour) {
    GdiObject o{};
    o.kind = ObjectKind::Brush;
    o.colour = colour;
    return (HBRUSH)createObject(std::move(o));
}

extern "C" HBRUSH CreatePatternBrush(HBITMAP bitmap) {
    // Approximated by the bitmap's top-left pixel.  The port uses a pattern
    // brush only to tile a background, where a flat fill of the same colour is
    // indistinguishable at this size.
    GdiObject o{};
    o.kind = ObjectKind::Brush;
    o.colour = RGB(192, 192, 192);
    if (GdiObject * b = object(bitmap))
        if (b->surface && !b->surface->pixels.empty()) {
            uint32_t p = b->surface->pixels[0];
            o.colour = RGB(p & 0xff, (p >> 8) & 0xff, (p >> 16) & 0xff);
        }
    return (HBRUSH)createObject(std::move(o));
}

extern "C" HPEN CreatePen(int style, int width, COLORREF colour) {
    GdiObject o{};
    o.kind = ObjectKind::Pen;
    o.penStyle = style;
    o.penWidth = width < 1 ? 1 : width;
    o.colour = colour;
    return (HPEN)createObject(std::move(o));
}

static HFONT makeFont(int height, int weight, DWORD italic, DWORD underline,
                      const std::string & face) {
    GdiObject o{};
    o.kind = ObjectKind::Font;
    // A negative height is a character height rather than a cell height; both
    // reach the browser as a pixel size, so only the magnitude matters.
    o.fontHeight = height < 0 ? -height : height;
    if (o.fontHeight == 0) o.fontHeight = 12;
    o.fontWeight = weight ? weight : FW_NORMAL;
    o.fontItalic = italic != 0;
    o.fontUnderline = underline != 0;
    o.fontFace = face.empty() ? "sans-serif" : face;
    return (HFONT)createObject(std::move(o));
}

extern "C" HFONT CreateFontW(int height, int, int, int, int weight, DWORD italic,
                             DWORD underline, DWORD, DWORD, DWORD, DWORD, DWORD,
                             DWORD, LPCWSTR face) {
    return makeFont(height, weight, italic, underline, toUtf8(face));
}

extern "C" HFONT CreateFontA(int height, int, int, int, int weight, DWORD italic,
                             DWORD underline, DWORD, DWORD, DWORD, DWORD, DWORD,
                             DWORD, LPCSTR face) {
    return makeFont(height, weight, italic, underline, face ? face : "");
}

extern "C" HFONT CreateFontIndirectW(const LOGFONTW * lf) {
    if (!lf) return nullptr;
    return makeFont(lf->lfHeight, lf->lfWeight, lf->lfItalic, lf->lfUnderline,
                    toUtf8(lf->lfFaceName, LF_FACESIZE));
}

extern "C" HFONT CreateFontIndirectA(const LOGFONTA * lf) {
    if (!lf) return nullptr;
    std::string face(lf->lfFaceName, strnlen(lf->lfFaceName, LF_FACESIZE));
    return makeFont(lf->lfHeight, lf->lfWeight, lf->lfItalic, lf->lfUnderline, face);
}

extern "C" BOOL DeleteObject(HGDIOBJ obj) {
    GdiObject * o = object(obj);
    if (!o || o->stock) return FALSE;
    state().objects.erase((uintptr_t)obj);
    return TRUE;
}

extern "C" HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj) {
    DeviceContext * d = dc(hdc);
    GdiObject * o = object(obj);
    if (!d || !o) return nullptr;
    HGDIOBJ previous = nullptr;
    switch (o->kind) {
        case ObjectKind::Pen:    previous = d->pen;    d->pen = obj;    break;
        case ObjectKind::Brush:  previous = d->brush;  d->brush = obj;  break;
        case ObjectKind::Font:   previous = d->font;   d->font = obj;   break;
        case ObjectKind::Bitmap:
            previous = d->bitmap;
            d->bitmap = obj;
            // Selecting a bitmap into a memory DC is what makes it the target.
            if (!d->owner && o->surface) d->target = o->surface;
            break;
        default: return nullptr;
    }
    return previous;
}

extern "C" int GetObjectW(HGDIOBJ obj, int size, LPVOID buf) {
    GdiObject * o = object(obj);
    if (!o || !buf) return 0;
    if (o->kind == ObjectKind::Bitmap && o->surface && size >= (int)sizeof(BITMAP)) {
        BITMAP b{};
        b.bmWidth = o->surface->width;
        b.bmHeight = o->surface->height;
        b.bmWidthBytes = o->surface->width * 4;
        b.bmPlanes = 1;
        b.bmBitsPixel = 32;
        b.bmBits = o->surface->pixels.data();
        memcpy(buf, &b, sizeof(b));
        return sizeof(b);
    }
    if (o->kind == ObjectKind::Font && size >= (int)sizeof(LOGFONTW)) {
        LOGFONTW lf{};
        lf.lfHeight = o->fontHeight;
        lf.lfWeight = o->fontWeight;
        lf.lfItalic = o->fontItalic;
        std::wstring face = fromUtf8(o->fontFace.c_str());
        for (size_t i = 0; i < face.size() && i < LF_FACESIZE - 1; i++)
            lf.lfFaceName[i] = face[i];
        memcpy(buf, &lf, sizeof(lf));
        return sizeof(lf);
    }
    return 0;
}


/* ---------------------------------------------------------------- dcs */

extern "C" HDC CreateCompatibleDC(HDC) {
    DeviceContext d{};
    d.pen = stockObject(BLACK_PEN);
    d.brush = stockObject(WHITE_BRUSH);
    d.font = stockObject(SYSTEM_FONT);
    uintptr_t h = state().allocate();
    state().dcs[h] = d;
    return (HDC)h;
}

extern "C" HBITMAP CreateCompatibleBitmap(HDC, int w, int h) {
    GdiObject o{};
    o.kind = ObjectKind::Bitmap;
    o.surface = std::make_shared<Surface>();
    o.surface->resize(w, h);
    return (HBITMAP)createObject(std::move(o));
}

extern "C" BOOL DeleteDC(HDC hdc) {
    if (!dc(hdc)) return FALSE;
    state().dcs.erase((uintptr_t)hdc);
    return TRUE;
}

extern "C" int SaveDC(HDC hdc) {
    DeviceContext * d = dc(hdc);
    if (!d) return 0;
    DeviceContext copy = *d;
    copy.saved.clear();          // do not nest the whole stack in every frame
    d->saved.push_back(copy);
    return (int)d->saved.size();
}

extern "C" BOOL RestoreDC(HDC hdc, int stateIndex) {
    DeviceContext * d = dc(hdc);
    if (!d || d->saved.empty()) return FALSE;
    size_t want = stateIndex < 0 ? d->saved.size() - 1 : (size_t)stateIndex - 1;
    if (want >= d->saved.size()) want = d->saved.size() - 1;
    std::vector<DeviceContext> stack = std::move(d->saved);
    DeviceContext restored = stack[want];
    stack.resize(want);
    *d = restored;
    d->saved = std::move(stack);
    return TRUE;
}


/* -------------------------------------------------------------- dc modes */

extern "C" COLORREF SetTextColor(HDC hdc, COLORREF c) {
    DeviceContext * d = dc(hdc); if (!d) return CLR_INVALID;
    COLORREF old = d->textColour; d->textColour = c; return old;
}
extern "C" COLORREF GetTextColor(HDC hdc) {
    DeviceContext * d = dc(hdc); return d ? d->textColour : CLR_INVALID;
}
extern "C" COLORREF SetBkColor(HDC hdc, COLORREF c) {
    DeviceContext * d = dc(hdc); if (!d) return CLR_INVALID;
    COLORREF old = d->bkColour; d->bkColour = c; return old;
}
extern "C" COLORREF GetBkColor(HDC hdc) {
    DeviceContext * d = dc(hdc); return d ? d->bkColour : CLR_INVALID;
}
extern "C" int SetBkMode(HDC hdc, int mode) {
    DeviceContext * d = dc(hdc); if (!d) return 0;
    int old = d->bkMode; d->bkMode = mode; return old;
}
extern "C" int GetBkMode(HDC hdc) {
    DeviceContext * d = dc(hdc); return d ? d->bkMode : 0;
}
extern "C" UINT SetTextAlign(HDC hdc, UINT align) {
    DeviceContext * d = dc(hdc); if (!d) return GDI_ERROR;
    UINT old = d->textAlign; d->textAlign = align; return old;
}
extern "C" UINT GetTextAlign(HDC hdc) {
    DeviceContext * d = dc(hdc); return d ? d->textAlign : GDI_ERROR;
}
extern "C" int SetROP2(HDC hdc, int mode) {
    DeviceContext * d = dc(hdc); if (!d) return 0;
    int old = d->rop2; d->rop2 = mode; return old;
}
extern "C" int SetStretchBltMode(HDC hdc, int mode) {
    DeviceContext * d = dc(hdc); if (!d) return 0;
    int old = d->stretchMode; d->stretchMode = mode; return old;
}
extern "C" int IntersectClipRect(HDC hdc, int l, int t, int r, int b) {
    DeviceContext * d = dc(hdc); if (!d) return 0;
    if (d->clip.right <= d->clip.left || d->clip.bottom <= d->clip.top) {
        d->clip = RECT{l, t, r, b};
    } else {
        d->clip.left = std::max(d->clip.left, (LONG)l);
        d->clip.top = std::max(d->clip.top, (LONG)t);
        d->clip.right = std::min(d->clip.right, (LONG)r);
        d->clip.bottom = std::min(d->clip.bottom, (LONG)b);
    }
    return 1;
}
extern "C" int SelectClipRgn(HDC hdc, HRGN) {
    DeviceContext * d = dc(hdc); if (!d) return 0;
    d->clip = RECT{0, 0, 0, 0};      // only ever used to clear the clip
    return 1;
}
extern "C" HRGN CreateRectRgn(int, int, int, int) {
    GdiObject o{}; o.kind = ObjectKind::Region;
    return (HRGN)createObject(std::move(o));
}


/* -------------------------------------------------------------- drawing */

extern "C" BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT old) {
    DeviceContext * d = dc(hdc); if (!d) return FALSE;
    if (old) *old = d->current;
    d->current.x = x; d->current.y = y;
    return TRUE;
}

extern "C" BOOL LineTo(HDC hdc, int x, int y) {
    DeviceContext * d = dc(hdc); if (!d) return FALSE;
    GdiObject * p = object(d->pen);
    if (p && p->penStyle != PS_NULL)
        drawLine(*d, d->current.x, d->current.y, x, y, fromColorref(p->colour));
    d->current.x = x; d->current.y = y;
    return TRUE;
}

extern "C" int FillRect(HDC hdc, const RECT * r, HBRUSH brush) {
    DeviceContext * d = dc(hdc);
    GdiObject * b = object(brush);
    if (!d || !r) return 0;
    if (b && b->hollow) return 1;
    // A null brush handle means the window background, which is grey here.
    fillRect(*d, *r, fromColorref(b ? b->colour : RGB(192, 192, 192)));
    return 1;
}

extern "C" int FrameRect(HDC hdc, const RECT * r, HBRUSH brush) {
    DeviceContext * d = dc(hdc);
    GdiObject * b = object(brush);
    if (!d || !r) return 0;
    uint32_t c = fromColorref(b ? b->colour : RGB(0, 0, 0));
    drawLine(*d, r->left, r->top, r->right - 1, r->top, c);
    drawLine(*d, r->right - 1, r->top, r->right - 1, r->bottom - 1, c);
    drawLine(*d, r->right - 1, r->bottom - 1, r->left, r->bottom - 1, c);
    drawLine(*d, r->left, r->bottom - 1, r->left, r->top, c);
    return 1;
}

extern "C" BOOL InvertRect(HDC hdc, const RECT * r) {
    DeviceContext * d = dc(hdc);
    if (!d || !r || !d->target) return FALSE;
    int saved = d->rop2;
    d->rop2 = R2_NOT;
    fillRect(*d, *r, 0);
    d->rop2 = saved;
    return TRUE;
}

extern "C" BOOL DrawFocusRect(HDC hdc, const RECT * r) {
    DeviceContext * d = dc(hdc);
    if (!d || !r) return FALSE;
    // Dotted, as Windows draws it, so a focused control reads as focused.
    for (int x = r->left; x < r->right; x += 2) {
        blendPixel(*d, x, r->top, 0xff000000u);
        blendPixel(*d, x, r->bottom - 1, 0xff000000u);
    }
    for (int y = r->top; y < r->bottom; y += 2) {
        blendPixel(*d, r->left, y, 0xff000000u);
        blendPixel(*d, r->right - 1, y, 0xff000000u);
    }
    return TRUE;
}

extern "C" BOOL Rectangle(HDC hdc, int l, int t, int r, int b) {
    DeviceContext * d = dc(hdc); if (!d) return FALSE;
    GdiObject * br = object(d->brush);
    if (br && !br->hollow)
        fillRect(*d, RECT{l, t, r, b}, fromColorref(br->colour));
    GdiObject * p = object(d->pen);
    if (p && p->penStyle != PS_NULL) {
        uint32_t c = fromColorref(p->colour);
        drawLine(*d, l, t, r - 1, t, c);
        drawLine(*d, r - 1, t, r - 1, b - 1, c);
        drawLine(*d, r - 1, b - 1, l, b - 1, c);
        drawLine(*d, l, b - 1, l, t, c);
    }
    return TRUE;
}

extern "C" BOOL Ellipse(HDC hdc, int l, int t, int r, int b) {
    // Midpoint ellipse, filled with the brush and outlined with the pen.
    DeviceContext * d = dc(hdc); if (!d) return FALSE;
    const double cx = (l + r - 1) / 2.0, cy = (t + b - 1) / 2.0;
    const double rx = (r - l) / 2.0, ry = (b - t) / 2.0;
    if (rx <= 0 || ry <= 0) return FALSE;
    GdiObject * br = object(d->brush);
    GdiObject * p = object(d->pen);
    for (int y = t; y < b; y++) {
        for (int x = l; x < r; x++) {
            const double nx = (x - cx) / rx, ny = (y - cy) / ry;
            const double v = nx * nx + ny * ny;
            if (v <= 1.0) {
                const bool edge = v > 0.75;
                if (edge && p && p->penStyle != PS_NULL)
                    blendPixel(*d, x, y, fromColorref(p->colour));
                else if (br && !br->hollow)
                    blendPixel(*d, x, y, fromColorref(br->colour));
            }
        }
    }
    return TRUE;
}

extern "C" BOOL Polyline(HDC hdc, const POINT * points, int count) {
    DeviceContext * d = dc(hdc);
    if (!d || !points || count < 2) return FALSE;
    GdiObject * p = object(d->pen);
    if (!p || p->penStyle == PS_NULL) return TRUE;
    uint32_t c = fromColorref(p->colour);
    for (int i = 1; i < count; i++)
        drawLine(*d, points[i-1].x, points[i-1].y, points[i].x, points[i].y, c);
    return TRUE;
}

extern "C" BOOL Polygon(HDC hdc, const POINT * points, int count) {
    if (!Polyline(hdc, points, count)) return FALSE;
    DeviceContext * d = dc(hdc);
    GdiObject * p = object(d->pen);
    if (d && p && count >= 2 && p->penStyle != PS_NULL)
        drawLine(*d, points[count-1].x, points[count-1].y,
                 points[0].x, points[0].y, fromColorref(p->colour));
    return TRUE;
}

extern "C" BOOL PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) {
    DeviceContext * d = dc(hdc); if (!d) return FALSE;
    RECT r{x, y, x + w, y + h};
    if (rop == BLACKNESS) { fillRect(*d, r, pack(0,0,0)); return TRUE; }
    if (rop == WHITENESS) { fillRect(*d, r, pack(255,255,255)); return TRUE; }
    if (rop == DSTINVERT) {
        int saved = d->rop2; d->rop2 = R2_NOT;
        fillRect(*d, r, 0); d->rop2 = saved; return TRUE;
    }
    GdiObject * b = object(d->brush);
    if (b && !b->hollow) fillRect(*d, r, fromColorref(b->colour));
    return TRUE;
}

extern "C" COLORREF SetPixel(HDC hdc, int x, int y, COLORREF colour) {
    DeviceContext * d = dc(hdc); if (!d) return CLR_INVALID;
    blendPixel(*d, x, y, fromColorref(colour));
    return colour;
}

extern "C" COLORREF GetPixel(HDC hdc, int x, int y) {
    DeviceContext * d = dc(hdc);
    if (!d || !d->target) return CLR_INVALID;
    const int tx = x + d->origin.x, ty = y + d->origin.y;
    if (!d->target->contains(tx, ty)) return CLR_INVALID;
    uint32_t p = *(d->target->row(ty) + tx);
    return RGB(p & 0xff, (p >> 8) & 0xff, (p >> 16) & 0xff);
}


/* -------------------------------------------------------------- palettes */

extern "C" HPALETTE CreatePalette(const LOGPALETTE * lp) {
    if (!lp) return nullptr;
    GdiObject o{};
    o.kind = ObjectKind::Palette;
    o.palette.assign(lp->palPalEntry, lp->palPalEntry + lp->palNumEntries);
    return (HPALETTE)createObject(std::move(o));
}

extern "C" HPALETTE SelectPalette(HDC hdc, HPALETTE palette, BOOL) {
    DeviceContext * d = dc(hdc); if (!d) return nullptr;
    HPALETTE old = d->palette; d->palette = palette; return old;
}

extern "C" UINT RealizePalette(HDC) {
    // There is no hardware palette to realise into: a DIB carries its own
    // colour table and is converted on blit, so nothing changes.
    return 0;
}

extern "C" UINT GetPaletteEntries(HPALETTE palette, UINT start, UINT count,
                                  LPPALETTEENTRY entries) {
    GdiObject * o = object(palette);
    if (!o) return 0;
    if (!entries) return (UINT)o->palette.size();
    UINT n = 0;
    for (UINT i = start; i < o->palette.size() && n < count; i++, n++)
        entries[n] = o->palette[i];
    return n;
}

extern "C" UINT SetPaletteEntries(HPALETTE palette, UINT start, UINT count,
                                  const PALETTEENTRY * entries) {
    GdiObject * o = object(palette);
    if (!o || !entries) return 0;
    if (o->palette.size() < start + count) o->palette.resize(start + count);
    for (UINT i = 0; i < count; i++) o->palette[start + i] = entries[i];
    return count;
}

extern "C" BOOL ResizePalette(HPALETTE palette, UINT count) {
    GdiObject * o = object(palette);
    if (!o) return FALSE;
    o->palette.resize(count);
    return TRUE;
}

extern "C" UINT GetNearestPaletteIndex(HPALETTE palette, COLORREF colour) {
    GdiObject * o = object(palette);
    if (!o || o->palette.empty()) return 0;
    const int r = GetRValue(colour), g = GetGValue(colour), b = GetBValue(colour);
    UINT best = 0;
    long bestDistance = 0x7fffffff;
    for (size_t i = 0; i < o->palette.size(); i++) {
        const long dr = r - o->palette[i].peRed;
        const long dg = g - o->palette[i].peGreen;
        const long db = b - o->palette[i].peBlue;
        const long distance = dr*dr + dg*dg + db*db;
        if (distance < bestDistance) { bestDistance = distance; best = (UINT)i; }
    }
    return best;
}

extern "C" COLORREF GetNearestColor(HDC, COLORREF colour) { return colour; }

extern "C" UINT GetSystemPaletteEntries(HDC, UINT, UINT count,
                                        LPPALETTEENTRY entries) {
    if (entries) memset(entries, 0, count * sizeof(PALETTEENTRY));
    return 0;
}


/* ------------------------------------------------------------ dib blitting */

namespace {

// A palettised or true-colour DIB, resolved to a function that yields a pixel.
struct DibReader {
    const BITMAPINFOHEADER * header = nullptr;
    const BYTE * bits = nullptr;
    const RGBQUAD * colours = nullptr;
    int width = 0, height = 0, bitCount = 0;
    size_t stride = 0;
    bool topDown = false;
    bool valid = false;

    bool init(const BITMAPINFO * info, LPCVOID data) {
        if (!info || !data) return false;
        header = &info->bmiHeader;
        if (header->biCompression != BI_RGB) return false;   // no RLE in these
        width = header->biWidth;
        height = header->biHeight < 0 ? -header->biHeight : header->biHeight;
        topDown = header->biHeight < 0;
        bitCount = header->biBitCount;
        if (width <= 0 || height <= 0) return false;
        stride = (((size_t)width * bitCount + 31) / 32) * 4;
        colours = info->bmiColors;
        bits = (const BYTE *)data;
        valid = true;
        return true;
    }

    // The row a scanline lives on, so a blit can walk it rather than work it
    // out again for every pixel.
    const BYTE * rowAt(int y) const {
        return bits + stride * (topDown ? y : (height - 1 - y));
    }

    uint32_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0xff000000u;
        const BYTE * row = bits + stride * (topDown ? y : (height - 1 - y));
        switch (bitCount) {
            case 8: {
                const RGBQUAD & c = colours[row[x]];
                return pack(c.rgbRed, c.rgbGreen, c.rgbBlue);
            }
            case 4: {
                const BYTE v = (x & 1) ? (row[x / 2] & 0x0f) : (row[x / 2] >> 4);
                const RGBQUAD & c = colours[v];
                return pack(c.rgbRed, c.rgbGreen, c.rgbBlue);
            }
            case 1: {
                const BYTE v = (row[x / 8] >> (7 - (x & 7))) & 1;
                const RGBQUAD & c = colours[v];
                return pack(c.rgbRed, c.rgbGreen, c.rgbBlue);
            }
            case 24: return pack(row[x*3 + 2], row[x*3 + 1], row[x*3 + 0]);
            case 32: return pack(row[x*4 + 2], row[x*4 + 1], row[x*4 + 0]);
            default: return 0xff000000u;
        }
    }
};

}   // namespace

extern "C" int SetDIBitsToDevice(HDC hdc, int x, int y, DWORD w, DWORD h,
                                 int sx, int sy, UINT startScan, UINT scanLines,
                                 LPCVOID bits, const BITMAPINFO * info,
                                 UINT /*colourUse*/) {
    DeviceContext * d = dc(hdc);
    DibReader dib;
    if (!d || !dib.init(info, bits)) return 0;
    (void)startScan; (void)scanLines;

    // The source y that Windows means here is measured from the bottom of the
    // DIB, so it is flipped into the reader's top-down space.
    const int srcTop = dib.height - (int)sy - (int)h;

    RECT span;
    if (!clipSpan(*d, RECT{x, y, x + (int)w, y + (int)h}, span)) return (int)h;

    if (d->rop2 != R2_COPYPEN) {
        for (int ty = span.top; ty < span.bottom; ty++)
            for (int tx = span.left; tx < span.right; tx++)
                blendPixel(*d, tx, ty,
                           dib.at(sx + tx - x, srcTop + ty - y));
        return (int)h;
    }

    // The whole world arrives through here every frame - 572x483 of it - so
    // the inner loop does one thing per pixel and everything else is hoisted.
    for (int ty = span.top; ty < span.bottom; ty++) {
        const int srcY = srcTop + ty - y;
        uint32_t * out = d->target->row(ty + d->origin.y)
                       + span.left + d->origin.x;
        const int count = span.right - span.left;
        const int srcX = sx + span.left - x;
        if (srcY < 0 || srcY >= dib.height) {
            for (int i = 0; i < count; i++) out[i] = 0xff000000u;
            continue;
        }
        const BYTE * row = dib.rowAt(srcY);
        switch (dib.bitCount) {
            case 32: {
                // 0x00RRGGBB in memory as B,G,R,0; the surface wants R,G,B,A.
                const BYTE * p = row + (size_t)srcX * 4;
                for (int i = 0; i < count; i++, p += 4)
                    out[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                             ((uint32_t)p[1] << 8) | p[2];
                break;
            }
            case 8: {
                const BYTE * p = row + srcX;
                for (int i = 0; i < count; i++) {
                    const RGBQUAD & c = dib.colours[p[i]];
                    out[i] = pack(c.rgbRed, c.rgbGreen, c.rgbBlue);
                }
                break;
            }
            case 24: {
                const BYTE * p = row + (size_t)srcX * 3;
                for (int i = 0; i < count; i++, p += 3)
                    out[i] = pack(p[2], p[1], p[0]);
                break;
            }
            default:
                for (int i = 0; i < count; i++)
                    out[i] = dib.at(srcX + i, srcY);
                break;
        }
    }
    return (int)h;
}

extern "C" int StretchDIBits(HDC hdc, int x, int y, int w, int h,
                             int sx, int sy, int sw, int sh, LPCVOID bits,
                             const BITMAPINFO * info, UINT /*colourUse*/,
                             DWORD rop) {
    DeviceContext * d = dc(hdc);
    DibReader dib;
    if (!d || !dib.init(info, bits)) return 0;
    if (w == 0 || h == 0 || sw == 0 || sh == 0) return 0;
    if (rop != SRCCOPY) return 0;      // nothing else is asked for

    // 16.16 fixed point stepping: nearest neighbour, which is what
    // COLORONCOLOR means and what keeps the original pixel art crisp.
    const int32_t stepX = (int32_t)(((int64_t)sw << 16) / w);
    const int32_t stepY = (int32_t)(((int64_t)sh << 16) / h);
    const int destW = w < 0 ? -w : w;
    const int destH = h < 0 ? -h : h;
    const int srcTop = dib.height - sy - (sh > 0 ? sh : 0);

    int32_t v = 0;
    for (int row = 0; row < destH; row++, v += stepY) {
        int32_t u = 0;
        const int srcY = srcTop + (v >> 16);
        for (int col = 0; col < destW; col++, u += stepX)
            blendPixel(*d, x + col, y + row, dib.at(sx + (u >> 16), srcY));
    }
    return destH;
}

extern "C" HBITMAP CreateDIBitmap(HDC, const BITMAPINFOHEADER * header,
                                  DWORD init, LPCVOID bits,
                                  const BITMAPINFO * info, UINT) {
    if (!header) return nullptr;
    GdiObject o{};
    o.kind = ObjectKind::Bitmap;
    o.surface = std::make_shared<Surface>();
    const int w = header->biWidth;
    const int h = header->biHeight < 0 ? -header->biHeight : header->biHeight;
    o.surface->resize(w, h);

    DibReader dib;
    if (init && bits && dib.init(info, bits))
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                *(o.surface->row(y) + x) = dib.at(x, y);

    return (HBITMAP)createObject(std::move(o));
}

extern "C" int GetDIBits(HDC, HBITMAP bitmap, UINT, UINT lines, LPVOID bits,
                         LPBITMAPINFO info, UINT) {
    GdiObject * o = object(bitmap);
    if (!o || !o->surface) return 0;
    if (info && !bits) {
        // The first call asks only for the dimensions.
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = o->surface->width;
        info->bmiHeader.biHeight = o->surface->height;
        info->bmiHeader.biPlanes = 1;
        info->bmiHeader.biBitCount = 32;
        info->bmiHeader.biCompression = BI_RGB;
        return o->surface->height;
    }
    if (!bits) return 0;
    const size_t stride = (size_t)o->surface->width * 4;
    BYTE * out = (BYTE *)bits;
    const int n = lines ? (int)lines : o->surface->height;
    for (int y = 0; y < n && y < o->surface->height; y++)
        memcpy(out + stride * y,
               o->surface->row(o->surface->height - 1 - y), stride);
    return n;
}

extern "C" BOOL BitBlt(HDC dstDc, int x, int y, int w, int h, HDC srcDc,
                       int sx, int sy, DWORD rop) {
    DeviceContext * dst = dc(dstDc);
    DeviceContext * src = dc(srcDc);
    if (!dst || !src || !dst->target || !src->target) return FALSE;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const int tx = sx + col + src->origin.x;
            const int ty = sy + row + src->origin.y;
            uint32_t c = src->target->contains(tx, ty)
                       ? *(src->target->row(ty) + tx) : 0xff000000u;
            if (rop == NOTSRCCOPY) c = (~c) | 0xff000000u;
            blendPixel(*dst, x + col, y + row, c);
        }
    }
    return TRUE;
}

extern "C" BOOL StretchBlt(HDC dstDc, int x, int y, int w, int h, HDC srcDc,
                           int sx, int sy, int sw, int sh, DWORD) {
    DeviceContext * dst = dc(dstDc);
    DeviceContext * src = dc(srcDc);
    if (!dst || !src || !dst->target || !src->target) return FALSE;
    if (w == 0 || h == 0 || sw == 0 || sh == 0) return FALSE;
    const int32_t stepX = (int32_t)(((int64_t)sw << 16) / w);
    const int32_t stepY = (int32_t)(((int64_t)sh << 16) / h);
    int32_t v = 0;
    for (int row = 0; row < h; row++, v += stepY) {
        int32_t u = 0;
        for (int col = 0; col < w; col++, u += stepX) {
            const int tx = sx + (u >> 16) + src->origin.x;
            const int ty = sy + (v >> 16) + src->origin.y;
            const uint32_t c = src->target->contains(tx, ty)
                             ? *(src->target->row(ty) + tx) : 0xff000000u;
            blendPixel(*dst, x + col, y + row, c);
        }
    }
    return TRUE;
}


/* ------------------------------------------------------------------- text */

extern "C" BOOL TextOutW(HDC hdc, int x, int y, LPCWSTR text, int count) {
    DeviceContext * d = dc(hdc);
    if (!d || !text) return FALSE;
    std::string s = toUtf8(text, count);
    drawText(*d, x, y, s.c_str(), (int)s.size());
    return TRUE;
}

extern "C" BOOL TextOutA(HDC hdc, int x, int y, LPCSTR text, int count) {
    DeviceContext * d = dc(hdc);
    if (!d || !text) return FALSE;
    drawText(*d, x, y, text, count);
    return TRUE;
}

extern "C" BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR text, int count,
                                      LPSIZE size) {
    DeviceContext * d = dc(hdc);
    if (!d || !size) return FALSE;
    std::string s = toUtf8(text, count);
    *size = measureText(*d, s.c_str(), (int)s.size());
    return TRUE;
}

extern "C" BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR text, int count,
                                      LPSIZE size) {
    DeviceContext * d = dc(hdc);
    if (!d || !size) return FALSE;
    *size = measureText(*d, text, count);
    return TRUE;
}

static void fillTextMetrics(DeviceContext & d, LONG & height, LONG & ascent,
                            LONG & descent, LONG & aveWidth, LONG & maxWidth) {
    GdiObject * f = object(d.font);
    const int size = f && f->fontHeight ? f->fontHeight : 12;
    height = size;
    ascent = (size * 4) / 5;
    descent = size - ascent;
    SIZE s = measureText(d, "n", 1);
    aveWidth = s.cx ? s.cx : size / 2;
    maxWidth = aveWidth * 2;
}

extern "C" BOOL GetTextMetricsW(HDC hdc, LPTEXTMETRICW tm) {
    DeviceContext * d = dc(hdc);
    if (!d || !tm) return FALSE;
    memset(tm, 0, sizeof(*tm));
    fillTextMetrics(*d, tm->tmHeight, tm->tmAscent, tm->tmDescent,
                    tm->tmAveCharWidth, tm->tmMaxCharWidth);
    tm->tmWeight = FW_NORMAL;
    tm->tmFirstChar = 32;
    tm->tmLastChar = 255;
    return TRUE;
}

extern "C" BOOL GetTextMetricsA(HDC hdc, LPTEXTMETRICA tm) {
    DeviceContext * d = dc(hdc);
    if (!d || !tm) return FALSE;
    memset(tm, 0, sizeof(*tm));
    fillTextMetrics(*d, tm->tmHeight, tm->tmAscent, tm->tmDescent,
                    tm->tmAveCharWidth, tm->tmMaxCharWidth);
    tm->tmWeight = FW_NORMAL;
    tm->tmFirstChar = 32;
    tm->tmLastChar = (CHAR)255;
    return TRUE;
}

// The lines a run of text becomes: at every newline it carries, and - unless it
// asked to stay on one line - at the last space that still fits the width.
// Without this a dialog's paragraph of explanation was drawn as one line
// running out of its own window, which is most of what a dialog says.
static std::vector<std::string> layOutLines(DeviceContext & d,
                                            const std::string & s, int width,
                                            UINT format) {
    std::vector<std::string> lines;
    if (format & DT_SINGLELINE) { lines.push_back(s); return lines; }

    size_t start = 0;
    while (start <= s.size()) {
        size_t newline = s.find('\n', start);
        std::string paragraph = s.substr(start, newline == std::string::npos
                                              ? std::string::npos
                                              : newline - start);
        if (!paragraph.empty() && paragraph.back() == '\r') paragraph.pop_back();

        if (!(format & DT_WORDBREAK) || width <= 0) {
            lines.push_back(paragraph);
        } else {
            size_t at = 0;
            while (at < paragraph.size()) {
                size_t fits = paragraph.size() - at;
                size_t breakAt = std::string::npos;
                for (size_t n = 1; n <= paragraph.size() - at; n++) {
                    const SIZE extent =
                        measureText(d, paragraph.c_str() + at, (int)n);
                    if (extent.cx > width) { fits = n - 1; break; }
                    if (paragraph[at + n - 1] == ' ') breakAt = n;
                }
                if (fits >= paragraph.size() - at) {
                    lines.push_back(paragraph.substr(at));
                    break;
                }
                // Back up to the last space that still fitted; a single word
                // too long for the width is cut rather than lost.
                size_t take = (breakAt != std::string::npos && breakAt <= fits)
                            ? breakAt : std::max<size_t>(1, fits);
                std::string line = paragraph.substr(at, take);
                while (!line.empty() && line.back() == ' ') line.pop_back();
                lines.push_back(line);
                at += take;
                while (at < paragraph.size() && paragraph[at] == ' ') at++;
            }
            if (paragraph.empty()) lines.push_back(std::string());
        }

        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    return lines;
}

static int drawTextCommon(DeviceContext & d, const std::string & s, LPRECT r,
                          UINT format) {
    const std::vector<std::string> lines =
        layOutLines(d, s, r->right - r->left, format);
    const int lineHeight = measureText(d, "", 0).cy;
    const int height = lineHeight * (int)lines.size();

    int widest = 0;
    for (const std::string & line : lines)
        widest = std::max<int>(widest,
                               measureText(d, line.c_str(), (int)line.size()).cx);

    if (format & DT_CALCRECT) {
        r->right = r->left + widest;
        r->bottom = r->top + height;
        return height;
    }

    int y = r->top;
    if (format & DT_VCENTER) y = r->top + ((r->bottom - r->top) - height) / 2;
    else if (format & DT_BOTTOM) y = r->bottom - height;

    const UINT savedAlign = d.textAlign;
    d.textAlign = TA_LEFT | TA_TOP;
    for (const std::string & line : lines) {
        const int lineWidth = measureText(d, line.c_str(), (int)line.size()).cx;
        int x = r->left;
        if (format & DT_CENTER)
            x = r->left + ((r->right - r->left) - lineWidth) / 2;
        else if (format & DT_RIGHT)
            x = r->right - lineWidth;
        drawText(d, x, y, line.c_str(), (int)line.size());
        y += lineHeight;
    }
    d.textAlign = savedAlign;
    return height;
}

extern "C" int DrawTextW(HDC hdc, LPCWSTR text, int count, LPRECT r, UINT format) {
    DeviceContext * d = dc(hdc);
    if (!d || !text || !r) return 0;
    return drawTextCommon(*d, toUtf8(text, count), r, format);
}

extern "C" int DrawTextA(HDC hdc, LPCSTR text, int count, LPRECT r, UINT format) {
    DeviceContext * d = dc(hdc);
    if (!d || !text || !r) return 0;
    std::string s = count < 0 ? std::string(text) : std::string(text, count);
    return drawTextCommon(*d, s, r, format);
}

extern "C" int EnumFontsA(HDC, LPCSTR, FONTENUMPROCA proc, LPARAM param) {
    // The port enumerates fonts to find Arial.  Offering exactly one face that
    // the browser will honour is both true and enough.
    if (!proc) return 0;
    LOGFONTA lf{};
    TEXTMETRICA tm{};
    lf.lfHeight = 12;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    memcpy(lf.lfFaceName, "Arial", 6);
    tm.tmHeight = 12;
    tm.tmAveCharWidth = 6;
    return proc(&lf, &tm, 0, param);
}

}   // namespace shim
