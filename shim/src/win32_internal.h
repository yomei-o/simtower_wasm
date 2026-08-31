// The object model behind the shim.
//
// Everything Windows hands out as a handle is an index into one table here, so
// a stale handle is a lookup that fails rather than a wild pointer.  Handles
// start at 0x1000 and never repeat: the port compares them, stores them and
// occasionally passes one back after the object is gone, and a recycled handle
// would silently address the wrong object.
#pragma once

#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shim {

/* --------------------------------------------------------------- surfaces */

// Every drawing target is 32-bit BGRA in memory, top row first, which is the
// order canvas ImageData wants.  Windows DIBs are bottom-up; the conversion
// happens on the way in, once, rather than on every access.
struct Surface {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    void resize(int w, int h);
    bool contains(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }
    uint32_t * row(int y) { return pixels.data() + (size_t)y * width; }
    const uint32_t * row(int y) const { return pixels.data() + (size_t)y * width; }
};

inline uint32_t pack(BYTE r, BYTE g, BYTE b) {
    return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}
inline uint32_t fromColorref(COLORREF c) {
    return pack(GetRValue(c), GetGValue(c), GetBValue(c));
}


/* ------------------------------------------------------------ gdi objects */

enum class ObjectKind { Pen, Brush, Font, Bitmap, Palette, Region };

struct GdiObject {
    ObjectKind kind;
    bool stock = false;

    // Pen and brush.
    COLORREF colour = 0;
    int penStyle = PS_SOLID;
    int penWidth = 1;
    bool hollow = false;

    // Font.  There is no font engine here; a face is a request for a size and
    // a weight, which the built-in bitmap font approximates.
    int fontHeight = 0;
    int fontWeight = FW_NORMAL;
    bool fontItalic = false;
    bool fontUnderline = false;
    std::string fontFace;

    // Bitmap.
    std::shared_ptr<Surface> surface;

    // Palette.
    std::vector<PALETTEENTRY> palette;
};


/* ------------------------------------------------------- device contexts */

struct DeviceContext {
    HWND owner = nullptr;              // null for a memory DC
    std::shared_ptr<Surface> target;   // where drawing lands

    HGDIOBJ pen = nullptr;
    HGDIOBJ brush = nullptr;
    HGDIOBJ font = nullptr;
    HGDIOBJ bitmap = nullptr;          // memory DCs only
    HPALETTE palette = nullptr;

    COLORREF textColour = RGB(0, 0, 0);
    COLORREF bkColour = RGB(255, 255, 255);
    int bkMode = OPAQUE;
    UINT textAlign = TA_LEFT | TA_TOP;
    int stretchMode = COLORONCOLOR;
    int rop2 = R2_COPYPEN;

    POINT current = {0, 0};            // MoveToEx/LineTo position
    RECT clip = {0, 0, 0, 0};          // empty means the whole target

    // Where this DC's origin sits in its target, so a child window can draw in
    // its own coordinates into the one screen surface.
    POINT origin = {0, 0};

    std::vector<DeviceContext> saved;  // SaveDC / RestoreDC

    bool clipped(int x, int y) const {
        if (clip.right <= clip.left || clip.bottom <= clip.top) return false;
        return x < clip.left || y < clip.top || x >= clip.right || y >= clip.bottom;
    }
};


/* ------------------------------------------------------------------ menus */

struct MenuItem {
    UINT flags = 0;
    UINT_PTR id = 0;
    std::string text;
    HMENU submenu = nullptr;
    // Filled in when the bar is laid out, so a click can be resolved.
    RECT bounds = {0, 0, 0, 0};
};

struct Menu {
    std::vector<MenuItem> items;
};


/* ---------------------------------------------------------------- windows */

struct WindowClass {
    UINT style = 0;
    WNDPROC proc = nullptr;
    HBRUSH background = nullptr;
    HCURSOR cursor = nullptr;
    HICON icon = nullptr;
    int extraWindowBytes = 0;
    std::wstring name;
};

struct Window {
    std::wstring className;
    std::wstring text;
    WNDPROC proc = nullptr;
    DWORD style = 0;
    DWORD exStyle = 0;
    RECT rect = {0, 0, 0, 0};          // screen coordinates, whole window
    HWND parent = nullptr;
    HMENU menu = nullptr;
    int id = 0;
    bool visible = false;
    bool enabled = true;
    bool destroyed = false;

    std::vector<LONG_PTR> extra;       // cbWndExtra and the dialog's DWLP_*
    std::map<std::wstring, HANDLE> props;
    std::vector<HWND> children;

    SCROLLINFO scroll[2] = {};         // SB_HORZ, SB_VERT

    // Dialog state, for windows created from a template.
    bool isDialog = false;
    DLGPROC dialogProc = nullptr;
    bool dialogEnded = false;
    INT_PTR dialogResult = 0;

    // Controls: what the built-in classes need to answer their messages.
    std::wstring controlClass;         // BUTTON, STATIC, EDIT, LISTBOX, ...
    bool checked = false;
    std::vector<std::wstring> listItems;
    int listSelection = -1;

    RECT invalid = {0, 0, 0, 0};       // pending InvalidateRect, client coords
    bool needsPaint = false;
    bool eraseOnPaint = false;
};


/* ------------------------------------------------------------------- state */

struct QueuedMessage {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
};

struct Timer {
    HWND hwnd;
    UINT_PTR id;
    UINT interval;
    TIMERPROC proc;
    double next;                       // ms, on the same clock as GetTickCount
};

// One process, one of these.
struct State {
    std::map<uintptr_t, GdiObject> objects;
    std::map<uintptr_t, DeviceContext> dcs;
    std::map<uintptr_t, Window> windows;
    std::map<uintptr_t, Menu> menus;
    std::map<std::wstring, WindowClass> classes;

    Surface screen;                    // what the canvas shows
    std::vector<QueuedMessage> queue;
    std::vector<Timer> timers;

    HWND active = nullptr;
    HWND focus = nullptr;
    HWND capture = nullptr;
    HCURSOR cursor = nullptr;
    POINT mouse = {0, 0};
    bool quitting = false;
    int exitCode = 0;
    DWORD lastError = 0;

    // The resource pack, embedded at build time.
    const BYTE * pack = nullptr;
    size_t packSize = 0;

    uintptr_t nextHandle = 0x1000;

    uintptr_t allocate() { return nextHandle += 0x10; }
};

State & state();

/* Handle conversion.  Deliberately not a cast: a bad handle has to be a failed
   lookup, because the port does hand back handles to objects it has freed. */
GdiObject *    object(HGDIOBJ h);
DeviceContext * dc(HDC h);
Window *       window(HWND h);
Menu *         menu(HMENU h);

HGDIOBJ createObject(GdiObject o);

/* Drawing, shared between the GDI entry points. */
void  fillRect(DeviceContext & d, RECT r, uint32_t colour);
void  blendPixel(DeviceContext & d, int x, int y, uint32_t colour);
void  drawLine(DeviceContext & d, int x0, int y0, int x1, int y1, uint32_t colour);
SIZE  measureText(DeviceContext & d, const char * utf8, int count);
void  drawText(DeviceContext & d, int x, int y, const char * utf8, int count);

/* Text conversion.  The port is a UNICODE build, so most strings arrive as
   wchar_t; the shim works in UTF-8 internally because that is what the browser
   and the built-in font want. */
std::string  toUtf8(const wchar_t * s, int count = -1);
std::wstring fromUtf8(const char * s, int count = -1);
std::string  utf16ToUtf8(const char16_t * s, int count = -1);

/* Windows. */
void  invalidate(Window & w, const RECT * r, bool erase);
void  paintPending();
void  presentScreen();
RECT  clientRect(const Window & w);
POINT clientOrigin(const Window & w);
HWND  windowAtPoint(POINT screen);
void  dispatchPending();
void  post(HWND h, UINT msg, WPARAM w, LPARAM l);
LRESULT send(HWND h, UINT msg, WPARAM w, LPARAM l);

/* Menus.  One row height serves the caption, the menu bar and a popup line,
   which is what Windows 3.1 did and what keeps the three lined up. */
constexpr int kCaptionBarHeight = 19;
int   menuBarHeight(const Window & w);
void  drawMenuBar(DeviceContext & d, Window & w);
void  drawMenuPopup(DeviceContext & d, Window & w);
bool  menuBarClick(Window & w, POINT client);

/* The host also sets the canvas cursor. */
void  hostSetCursor(const char * css);

/* Resources, in win32_ne.cpp: parsed out of the player's own executable rather
   than out of a pack, so nothing copyrighted has to be committed or served. */
bool         loadResourcesFromExecutable(const BYTE * data, size_t size);
size_t       findResourceIndex(uint16_t type, uint16_t id);
size_t       resourceCount();
const BYTE * resourceAt(size_t index, size_t * size);
const char * resourceType(size_t index);
int          resourceId(size_t index);

/* The host, in win32_host.cpp. */
void  hostInit();
void  hostPresent(const Surface & s);
double hostNow();

}   // namespace shim
