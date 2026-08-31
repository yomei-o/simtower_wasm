// Windows, messages, and the frame around them.
//
// Everything lives in one screen surface: a window's DC draws into it at the
// window's own origin, so the port draws in client coordinates and never learns
// that there is no window manager here.

#include "win32_internal.h"

#include <emscripten.h>

#include <algorithm>
#include <cstring>

namespace shim {

namespace {

const int kBorder = 4;
const int kCaption = kCaptionBarHeight;

int captionHeight(const Window & w) {
    return (w.style & WS_CAPTION) == WS_CAPTION ? kCaption : 0;
}
int borderWidth(const Window & w) {
    if (w.style & WS_THICKFRAME) return kBorder;
    if (w.style & (WS_BORDER | WS_DLGFRAME)) return 1;
    return 0;
}

// A window's own scroll bars stand inside the frame and outside the client
// area, so the client is smaller by exactly this much - which is what the port
// measures when it installs their ranges.
const int kScrollThickness = 16;
int vScrollWidth(const Window & w) {
    return (w.style & WS_VSCROLL) ? kScrollThickness : 0;
}
int hScrollHeight(const Window & w) {
    return (w.style & WS_HSCROLL) ? kScrollThickness : 0;
}

}   // namespace

// Defined below, next to the rest of the frame drawing; needed by PeekMessage,
// which generates WM_PAINT and draws the chrome around it.
void drawFrame(Window & w);

Window * window(HWND h) {
    if (!h) return nullptr;
    auto & m = state().windows;
    auto it = m.find((uintptr_t)h);
    if (it == m.end() || it->second.destroyed) return nullptr;
    return &it->second;
}

int menuBarHeight(const Window & w) {
    return w.menu && menu(w.menu) && !menu(w.menu)->items.empty()
         ? kCaptionBarHeight : 0;
}

POINT clientOrigin(const Window & w) {
    const int b = borderWidth(w);
    return POINT{w.rect.left + b,
                 w.rect.top + b + captionHeight(w) + menuBarHeight(w)};
}

// A child window is positioned in its parent's client coordinates, and
// everything here works in screen coordinates, so the two are converted at the
// two points where Windows converts them: creation, and every move.
POINT parentClientOrigin(const Window & w) {
    if (!(w.style & WS_CHILD)) return POINT{0, 0};
    Window * p = window(w.parent);
    return p ? clientOrigin(*p) : POINT{0, 0};
}

RECT clientRect(const Window & w) {
    const int b = borderWidth(w);
    RECT r{0, 0,
           w.rect.right - w.rect.left - b * 2 - vScrollWidth(w),
           w.rect.bottom - w.rect.top - b * 2 - captionHeight(w)
               - menuBarHeight(w) - hScrollHeight(w)};
    if (r.right < 0) r.right = 0;
    if (r.bottom < 0) r.bottom = 0;
    return r;
}

// The two bars, in screen coordinates, for drawing and for hit testing.
RECT windowScrollRect(const Window & w, bool vertical) {
    const int b = borderWidth(w);
    const POINT origin = clientOrigin(w);
    const RECT client = clientRect(w);
    if (vertical)
        return RECT{origin.x + client.right, origin.y,
                    origin.x + client.right + vScrollWidth(w),
                    origin.y + client.bottom};
    // It stops where the vertical bar begins, so its right arrow is not hidden
    // under the corner square the two of them leave between each other.
    return RECT{origin.x, origin.y + client.bottom,
                (w.style & WS_VSCROLL) ? origin.x + client.right
                                       : w.rect.right - b,
                origin.y + client.bottom + hScrollHeight(w)};
}


/* -------------------------------------------------------------- classes */

extern "C" ATOM RegisterClassW(const WNDCLASSW * cls) {
    if (!cls || !cls->lpszClassName) return 0;
    WindowClass c{};
    c.style = cls->style;
    c.proc = cls->lpfnWndProc;
    c.background = cls->hbrBackground;
    c.cursor = cls->hCursor;
    c.icon = cls->hIcon;
    c.extraWindowBytes = cls->cbWndExtra;
    c.name = cls->lpszClassName;
    state().classes[c.name] = c;
    return 1;
}

extern "C" ATOM RegisterClassExW(const WNDCLASSEXW * cls) {
    if (!cls) return 0;
    WNDCLASSW plain{};
    plain.style = cls->style;
    plain.lpfnWndProc = cls->lpfnWndProc;
    plain.cbWndExtra = cls->cbWndExtra;
    plain.hInstance = cls->hInstance;
    plain.hIcon = cls->hIcon;
    plain.hCursor = cls->hCursor;
    plain.hbrBackground = cls->hbrBackground;
    plain.lpszClassName = cls->lpszClassName;
    return RegisterClassW(&plain);
}


/* ------------------------------------------------------------- z-order

   Everything is drawn into one surface with no clipping between windows, so
   which window is on top is decided entirely by which one is painted last.
   Without this the order was the order they were created in, and the game's
   command palette - created first and set topmost - was painted before the map
   window that then covered it.                                              */

static void zErase(HWND h) {
    auto & z = state().zorder;
    z.erase(std::remove(z.begin(), z.end(), (uintptr_t)h), z.end());
}

// A window and everything inside it, parent first: children are drawn over
// their parent and travel with it.
static void collectTree(HWND h, std::vector<uintptr_t> & out) {
    out.push_back((uintptr_t)h);
    if (Window * w = window(h))
        for (HWND child : w->children) collectTree(child, out);
}

// Where the ordinary band ends, which is where a non-topmost window goes when
// it is brought to the front.
static size_t bandEnd(bool topmost) {
    auto & z = state().zorder;
    if (topmost) return z.size();
    size_t end = 0;
    for (size_t i = 0; i < z.size(); i++) {
        Window * w = window((HWND)z[i]);
        if (w && w->topmost) break;
        end = i + 1;
    }
    return end;
}

static void zPlace(HWND h, HWND insertAfter) {
    Window * w = window(h);
    if (!w) return;
    std::vector<uintptr_t> tree;
    collectTree(h, tree);
    for (uintptr_t entry : tree) zErase((HWND)entry);

    auto & z = state().zorder;
    size_t at;
    if (insertAfter == HWND_BOTTOM) {
        at = 0;
    } else if (insertAfter == HWND_TOPMOST) {
        w->topmost = true;
        at = z.size();
    } else if (insertAfter == HWND_NOTOPMOST) {
        w->topmost = false;
        at = bandEnd(false);
    } else if (insertAfter == HWND_TOP) {
        at = bandEnd(w->topmost);
    } else {
        auto it = std::find(z.begin(), z.end(), (uintptr_t)insertAfter);
        at = it == z.end() ? bandEnd(w->topmost) : (size_t)(it - z.begin()) + 1;
    }
    z.insert(z.begin() + at, tree.begin(), tree.end());
}

static void zInsertOnCreate(HWND h, HWND parent) {
    Window * p = window(parent);
    if (!p) { zPlace(h, HWND_TOP); return; }
    if (Window * w = window(h)) w->topmost = p->topmost;
    // Directly above the parent and everything already inside it.
    std::vector<uintptr_t> tree;
    collectTree(parent, tree);
    auto & z = state().zorder;
    size_t at = 0;
    for (size_t i = 0; i < z.size(); i++)
        if (std::find(tree.begin(), tree.end(), z[i]) != tree.end()) at = i + 1;
    z.insert(z.begin() + at, (uintptr_t)h);
}


/* -------------------------------------------------------------- windows */

extern "C" HWND CreateWindowExW(DWORD exStyle, LPCWSTR cls, LPCWSTR name,
                                DWORD style, int x, int y, int w, int h,
                                HWND parent, HMENU menuHandle, HINSTANCE inst,
                                LPVOID param) {
    State & s = state();

    Window win{};
    win.className = cls ? cls : L"";
    win.text = name ? name : L"";
    win.style = style;
    win.exStyle = exStyle;
    win.parent = parent;
    win.menu = menuHandle;
    win.visible = (style & WS_VISIBLE) != 0;

    // CW_USEDEFAULT means "you decide", and the decision here is the whole
    // screen, since there is no desktop to place a window on.
    if (x == CW_USEDEFAULT) x = 0;
    if (y == CW_USEDEFAULT) y = 0;
    if (w == CW_USEDEFAULT || w <= 0) w = s.screen.width ? s.screen.width : 640;
    if (h == CW_USEDEFAULT || h <= 0) h = s.screen.height ? s.screen.height : 480;
    if (style & WS_CHILD) {
        // The caller gave parent-client coordinates.  Storing them as screen
        // coordinates is what put every dialog control in the top-left corner.
        if (Window * p = window(parent)) {
            const POINT o = clientOrigin(*p);
            x += o.x;
            y += o.y;
        }
    }
    win.rect = RECT{x, y, x + w, y + h};

    auto it = s.classes.find(win.className);
    if (it != s.classes.end()) {
        win.proc = it->second.proc;
        win.extra.assign((it->second.extraWindowBytes + sizeof(LONG_PTR) - 1) /
                         sizeof(LONG_PTR) + 4, 0);
    } else {
        // A built-in control class, or one that was never registered.  Both are
        // handled by the control window procedure rather than refused.
        win.controlClass = win.className;
        win.extra.assign(8, 0);
    }

    const uintptr_t handle = s.allocate();
    s.windows[handle] = std::move(win);
    HWND hwnd = (HWND)handle;

    if (Window * p = window(parent)) p->children.push_back(hwnd);
    zInsertOnCreate(hwnd, parent);
    if (!s.active) { s.active = hwnd; s.focus = hwnd; }

    CREATESTRUCTW create{};
    create.lpCreateParams = param;
    create.hInstance = inst;
    create.hMenu = menuHandle;
    create.hwndParent = parent;
    create.x = x; create.y = y; create.cx = w; create.cy = h;
    create.style = (LONG)style;
    create.lpszName = name;
    create.lpszClass = cls;
    create.dwExStyle = exStyle;

    if (send(hwnd, WM_CREATE, 0, (LPARAM)&create) == -1) {
        s.windows.erase(handle);
        return nullptr;
    }

    if (Window * created = window(hwnd))
        if (created->visible) invalidate(*created, nullptr, true);

    return hwnd;
}

extern "C" BOOL DestroyWindow(HWND hwnd) {
    Window * w = window(hwnd);
    if (!w) return FALSE;

    // Children first, as Windows does, so a child's WM_DESTROY still sees a
    // live parent.
    std::vector<HWND> children = w->children;
    for (HWND child : children) DestroyWindow(child);

    send(hwnd, WM_DESTROY, 0, 0);

    State & s = state();
    const RECT vacated = w->rect;
    const bool wasVisible = w->visible;
    zErase(hwnd);
    if (Window * again = window(hwnd)) again->destroyed = true;
    if (wasVisible) invalidateArea(vacated);
    if (s.active == hwnd) s.active = nullptr;
    if (s.focus == hwnd) s.focus = nullptr;
    if (s.capture == hwnd) s.capture = nullptr;
    if (Window * p = window(w->parent)) {
        auto & c = p->children;
        c.erase(std::remove(c.begin(), c.end(), hwnd), c.end());
        invalidate(*p, nullptr, true);
    }
    return TRUE;
}

extern "C" BOOL ShowWindow(HWND hwnd, int cmd) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    const bool was = w->visible;
    w->visible = (cmd != SW_HIDE);
    if (w->visible) {
        invalidate(*w, nullptr, true);
        for (HWND child : w->children)
            if (Window * c = window(child))
                if (c->visible) invalidate(*c, nullptr, true);
    } else {
        invalidateArea(w->rect);
    }
    return was;
}

extern "C" BOOL UpdateWindow(HWND hwnd) {
    Window * w = window(hwnd);
    if (!w || !w->needsPaint) return TRUE;
    // UpdateWindow means "paint it now", not "paint it soon", and the port
    // relies on that to get a frame out between simulation steps.
    send(hwnd, WM_PAINT, 0, 0);
    w->needsPaint = false;
    return TRUE;
}

extern "C" BOOL IsWindow(HWND hwnd) { return window(hwnd) != nullptr; }
extern "C" BOOL IsWindowVisible(HWND hwnd) {
    Window * w = window(hwnd);
    return w && w->visible;
}
extern "C" BOOL IsIconic(HWND) { return FALSE; }

extern "C" BOOL EnableWindow(HWND hwnd, BOOL enable) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    const bool was = !w->enabled;
    w->enabled = enable != 0;
    send(hwnd, WM_ENABLE, (WPARAM)enable, 0);
    return was;
}

// Moving a window moves what is inside it.  Children hold screen rectangles
// here, so the delta has to be pushed down the tree; without it a resized
// parent leaves its controls behind at their old absolute position.
static void offsetDescendants(Window & w, int dx, int dy) {
    if (!dx && !dy) return;
    for (HWND child : w.children) {
        Window * c = window(child);
        if (!c) continue;
        OffsetRect(&c->rect, dx, dy);
        offsetDescendants(*c, dx, dy);
    }
}

extern "C" BOOL MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    Window * win = window(hwnd);
    if (!win) return FALSE;
    const int clientX = x, clientY = y;
    const POINT origin = parentClientOrigin(*win);
    x += origin.x;
    y += origin.y;
    const RECT before = win->rect;
    win->rect = RECT{x, y, x + w, y + h};
    offsetDescendants(*win, x - before.left, y - before.top);
    if (win->visible && !EqualRect(&before, &win->rect)) invalidateArea(before);
    send(hwnd, WM_MOVE, 0, MAKELPARAM(clientX, clientY));
    const RECT client = clientRect(*win);
    send(hwnd, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom));
    if (repaint) invalidate(*win, nullptr, true);
    return TRUE;
}

extern "C" BOOL SetWindowPos(HWND hwnd, HWND insertAfter, int x, int y,
                             int w, int h, UINT flags) {
    Window * win = window(hwnd);
    if (!win) return FALSE;
    // x/y arrive in the same coordinates MoveWindow takes, so the current
    // position has to be converted back out of screen space to stand in for
    // them when SWP_NOMOVE says to keep it.
    const POINT origin = parentClientOrigin(*win);
    int nx = x, ny = y;
    if (flags & SWP_NOMOVE) {
        nx = win->rect.left - origin.x;
        ny = win->rect.top - origin.y;
    }
    int nw = w, nh = h;
    if (flags & SWP_NOSIZE) {
        nw = win->rect.right - win->rect.left;
        nh = win->rect.bottom - win->rect.top;
    }
    if (!(flags & SWP_NOZORDER)) zPlace(hwnd, insertAfter);
    if (flags & SWP_SHOWWINDOW) win->visible = true;
    if (flags & SWP_HIDEWINDOW) { win->visible = false; invalidateArea(win->rect); }
    return MoveWindow(hwnd, nx, ny, nw, nh, !(flags & SWP_NOREDRAW));
}

extern "C" BOOL GetClientRect(HWND hwnd, LPRECT r) {
    Window * w = window(hwnd);
    if (!r) return FALSE;
    if (!w) { *r = RECT{0, 0, 0, 0}; return FALSE; }
    *r = clientRect(*w);
    return TRUE;
}

extern "C" BOOL GetWindowRect(HWND hwnd, LPRECT r) {
    Window * w = window(hwnd);
    if (!r) return FALSE;
    if (!w) { *r = RECT{0, 0, 0, 0}; return FALSE; }
    *r = w->rect;
    return TRUE;
}

extern "C" BOOL ScreenToClient(HWND hwnd, LPPOINT p) {
    Window * w = window(hwnd);
    if (!w || !p) return FALSE;
    const POINT o = clientOrigin(*w);
    p->x -= o.x; p->y -= o.y;
    return TRUE;
}

extern "C" BOOL ClientToScreen(HWND hwnd, LPPOINT p) {
    Window * w = window(hwnd);
    if (!w || !p) return FALSE;
    const POINT o = clientOrigin(*w);
    p->x += o.x; p->y += o.y;
    return TRUE;
}

extern "C" BOOL SetWindowTextW(HWND hwnd, LPCWSTR text) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    w->text = text ? text : L"";
    invalidate(*w, nullptr, true);
    return TRUE;
}

extern "C" int GetWindowTextW(HWND hwnd, LPWSTR buf, int len) {
    Window * w = window(hwnd);
    if (!w || !buf || len <= 0) return 0;
    int n = 0;
    for (; n < (int)w->text.size() && n + 1 < len; n++) buf[n] = w->text[n];
    buf[n] = 0;
    return n;
}

extern "C" int GetWindowTextLengthW(HWND hwnd) {
    Window * w = window(hwnd);
    return w ? (int)w->text.size() : 0;
}

extern "C" HWND SetFocus(HWND hwnd) {
    State & s = state();
    HWND was = s.focus;
    if (was == hwnd) return was;
    if (was) send(was, WM_KILLFOCUS, (WPARAM)hwnd, 0);
    s.focus = hwnd;
    if (hwnd) send(hwnd, WM_SETFOCUS, (WPARAM)was, 0);
    return was;
}
extern "C" HWND GetFocus(void) { return state().focus; }

extern "C" HWND SetActiveWindow(HWND hwnd) {
    State & s = state();
    HWND was = s.active;
    s.active = hwnd;
    if (hwnd) send(hwnd, WM_ACTIVATE, 1, 0);
    return was;
}
extern "C" HWND GetActiveWindow(void) { return state().active; }
extern "C" HWND GetDesktopWindow(void) {
    // The port asks the desktop how big the screen is - to size the splash, and
    // to centre every dialog.  Returning null made both of those measure zero,
    // which is how the startup chooser ended up at (-130,-59).
    static HWND desktop = nullptr;
    State & s = state();
    if (!window(desktop)) {
        Window w{};
        w.className = L"#32769";
        w.style = 0;                  // no frame and no caption: it is the screen
        w.visible = false;            // so it is never painted or hit-tested
        const uintptr_t handle = s.allocate();
        s.windows[handle] = std::move(w);
        desktop = (HWND)handle;
    }
    // Answered from the screen rather than remembered, so a resize is followed.
    window(desktop)->rect = RECT{0, 0, s.screen.width, s.screen.height};
    return desktop;
}
extern "C" HWND GetTopWindow(HWND) { return nullptr; }

extern "C" HWND GetParent(HWND hwnd) {
    Window * w = window(hwnd);
    return w ? w->parent : nullptr;
}

extern "C" HWND SetCapture(HWND hwnd) {
    State & s = state();
    HWND was = s.capture;
    s.capture = hwnd;
    return was;
}
extern "C" BOOL ReleaseCapture(void) { state().capture = nullptr; return TRUE; }
extern "C" HWND GetCapture(void) { return state().capture; }

extern "C" LONG_PTR GetWindowLongPtrW(HWND hwnd, int index) {
    Window * w = window(hwnd);
    if (!w) return 0;
    switch (index) {
        case GWL_STYLE:    return (LONG_PTR)w->style;
        case GWL_EXSTYLE:  return (LONG_PTR)w->exStyle;
        case GWL_WNDPROC:  return (LONG_PTR)w->proc;
        case GWL_ID:       return w->id;
        default: break;
    }
    // Anything positive is an offset into the window's extra bytes, which is
    // also where a dialog keeps DWLP_MSGRESULT and DWLP_USER.
    if (index >= 0 && (size_t)(index / (int)sizeof(LONG_PTR)) < w->extra.size())
        return w->extra[index / sizeof(LONG_PTR)];
    return 0;
}

extern "C" LONG_PTR SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value) {
    Window * w = window(hwnd);
    if (!w) return 0;
    LONG_PTR was = GetWindowLongPtrW(hwnd, index);
    switch (index) {
        case GWL_STYLE:   w->style = (DWORD)value; return was;
        case GWL_EXSTYLE: w->exStyle = (DWORD)value; return was;
        case GWL_WNDPROC: w->proc = (WNDPROC)value; return was;
        case GWL_ID:      w->id = (int)value; return was;
        default: break;
    }
    if (index >= 0 && (size_t)(index / (int)sizeof(LONG_PTR)) < w->extra.size())
        w->extra[index / sizeof(LONG_PTR)] = value;
    return was;
}

extern "C" LONG_PTR GetClassLongPtrW(HWND hwnd, int index) {
    Window * w = window(hwnd);
    if (!w) return 0;
    auto it = state().classes.find(w->className);
    if (it == state().classes.end()) return 0;
    switch (index) {
        case GCL_HICON:        return (LONG_PTR)it->second.icon;
        case GCL_HCURSOR:      return (LONG_PTR)it->second.cursor;
        case GCL_HBRBACKGROUND: return (LONG_PTR)it->second.background;
        default: return 0;
    }
}

extern "C" LONG_PTR SetClassLongPtrW(HWND hwnd, int index, LONG_PTR value) {
    Window * w = window(hwnd);
    if (!w) return 0;
    auto it = state().classes.find(w->className);
    if (it == state().classes.end()) return 0;
    LONG_PTR was = GetClassLongPtrW(hwnd, index);
    switch (index) {
        case GCL_HICON:         it->second.icon = (HICON)value; break;
        case GCL_HCURSOR:       it->second.cursor = (HCURSOR)value; break;
        case GCL_HBRBACKGROUND: it->second.background = (HBRUSH)value; break;
        default: break;
    }
    return was;
}

extern "C" BOOL SetPropW(HWND hwnd, LPCWSTR name, HANDLE data) {
    Window * w = window(hwnd);
    if (!w || !name) return FALSE;
    w->props[name] = data;
    return TRUE;
}
extern "C" HANDLE GetPropW(HWND hwnd, LPCWSTR name) {
    Window * w = window(hwnd);
    if (!w || !name) return nullptr;
    auto it = w->props.find(name);
    return it == w->props.end() ? nullptr : it->second;
}
extern "C" HANDLE RemovePropW(HWND hwnd, LPCWSTR name) {
    Window * w = window(hwnd);
    if (!w || !name) return nullptr;
    auto it = w->props.find(name);
    if (it == w->props.end()) return nullptr;
    HANDLE data = it->second;
    w->props.erase(it);
    return data;
}

extern "C" BOOL EnumChildWindows(HWND hwnd, WNDENUMPROC proc, LPARAM param) {
    Window * w = window(hwnd);
    if (!w || !proc) return FALSE;
    std::vector<HWND> children = w->children;
    for (HWND child : children)
        if (!proc(child, param)) return FALSE;
    return TRUE;
}


/* ------------------------------------------------------------- hit testing */

// The frame, the caption and the menu bar are not the client area, and Windows
// does not deliver a click on them as a client click.  The port's own window
// procedure reads WM_LBUTTONDOWN as an attempt to build something, so a click
// on the File menu arriving there - at a negative y, no less - was a click on
// the sky.  It answered "Cannot place item there", which is exactly right and
// exactly not what was asked.
// Where along a scroll bar a position sits, and back again.  One place for the
// arithmetic so the thumb a drag produces is the thumb that gets drawn.
static int scrollPositionAt(const Window & w, bool vertical, int along) {
    const SCROLLINFO & info = w.scroll[vertical ? SB_VERT : SB_HORZ];
    const RECT bar = windowScrollRect(w, vertical);
    const int length = vertical ? bar.bottom - bar.top : bar.right - bar.left;
    const int arrow = std::min(16, length / 2);
    const int span = std::max(1, info.nMax - info.nMin + 1);
    const int page = std::max(1, (int)info.nPage);
    const int track = std::max(1, length - arrow * 2);
    const int size = std::max(8, std::min(track, track * page / span));
    const int room = std::max(1, track - size);
    const int reach = std::max(1, span - page);
    const int offset = std::max(0, std::min(room, along - arrow));
    return info.nMin + offset * reach / room;
}

static int scrollThumbOffset(const Window & w, bool vertical) {
    const SCROLLINFO & info = w.scroll[vertical ? SB_VERT : SB_HORZ];
    const RECT bar = windowScrollRect(w, vertical);
    const int length = vertical ? bar.bottom - bar.top : bar.right - bar.left;
    const int arrow = std::min(16, length / 2);
    const int span = std::max(1, info.nMax - info.nMin + 1);
    const int page = std::max(1, (int)info.nPage);
    const int track = std::max(1, length - arrow * 2);
    const int size = std::max(8, std::min(track, track * page / span));
    const int room = std::max(0, track - size);
    const int reach = std::max(1, span - page);
    const int position = std::min(reach, std::max(0, info.nPos - info.nMin));
    return arrow + room * position / reach;
}

bool nonClientMouse(HWND hwnd, UINT message, POINT screen) {
    State & s = state();

    // A thumb that has been taken hold of keeps the mouse until it is released.
    if (s.scrollDrag) {
        Window * dragged = window(s.scrollDrag);
        if (!dragged) { s.scrollDrag = nullptr; return true; }
        const bool vertical = s.scrollDragVertical;
        const RECT bar = windowScrollRect(*dragged, vertical);
        const int along = (vertical ? screen.y - bar.top : screen.x - bar.left)
                        - s.scrollDragGrab;
        const int position = scrollPositionAt(*dragged, vertical, along);
        if (message == WM_MOUSEMOVE) {
            send(s.scrollDrag, vertical ? WM_VSCROLL : WM_HSCROLL,
                 MAKEWPARAM(SB_THUMBTRACK, position), 0);
            return true;
        }
        if (message == WM_LBUTTONUP) {
            HWND target = s.scrollDrag;
            s.scrollDrag = nullptr;
            send(target, vertical ? WM_VSCROLL : WM_HSCROLL,
                 MAKEWPARAM(SB_THUMBPOSITION, position), 0);
            send(target, vertical ? WM_VSCROLL : WM_HSCROLL,
                 MAKEWPARAM(SB_ENDSCROLL, 0), 0);
            return true;
        }
        return true;
    }

    const bool press = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK;

    // An open popup takes the next click wherever it lands: on one of its own
    // items, or anywhere else, which dismisses it.
    if (menuIsOpen() && press) {
        Window * bar = window(menuOpenWindow());
        if (bar) {
            const POINT origin = clientOrigin(*bar);
            menuBarClick(*bar, POINT{screen.x - origin.x, screen.y - origin.y});
            return true;
        }
    }

    Window * w = window(hwnd);
    if (!w) return false;
    const RECT client = clientRect(*w);
    const POINT origin = clientOrigin(*w);
    const POINT p{screen.x - origin.x, screen.y - origin.y};
    if (PtInRect(&client, p)) return false;

    // The window's own scroll bars are part of the frame too.
    if (press) {
        for (int axis = 0; axis < 2; axis++) {
            const bool vertical = axis == 0;
            if (!(w->style & (vertical ? WS_VSCROLL : WS_HSCROLL))) continue;
            const RECT bar = windowScrollRect(*w, vertical);
            if (!PtInRect(&bar, screen)) continue;
            const SCROLLINFO & info = w->scroll[vertical ? SB_VERT : SB_HORZ];
            const RECT local{0, 0, bar.right - bar.left, bar.bottom - bar.top};
            const POINT at{screen.x - bar.left, screen.y - bar.top};
            const int code = scrollBarHit(local, at, info, vertical);
            if (code < 0) return true;
            if (code == SB_THUMBTRACK) {
                // Taking hold of the thumb, remembering where on it, so it does
                // not jump to put its middle under the pointer.
                state().scrollDrag = hwnd;
                state().scrollDragVertical = vertical;
                state().scrollDragGrab =
                    (vertical ? at.y : at.x) - scrollThumbOffset(*w, vertical);
                return true;
            }
            send(hwnd, vertical ? WM_VSCROLL : WM_HSCROLL,
                 MAKEWPARAM(code, info.nPos), 0);
            return true;
        }
        menuBarClick(*w, p);
    }
    return true;                   // the frame keeps it either way
}

HWND windowAtPoint(POINT screen) {
    // Top down: the first window the point lands in owns it, which is the
    // paint order read backwards.
    State & s = state();
    for (size_t i = s.zorder.size(); i-- > 0; ) {
        Window * w = window((HWND)s.zorder[i]);
        if (!w || !w->visible) continue;
        if (screen.x < w->rect.left || screen.x >= w->rect.right) continue;
        if (screen.y < w->rect.top || screen.y >= w->rect.bottom) continue;
        // Static text is transparent to the mouse, so a click on a label
        // belongs to the dialog underneath it rather than to the label.
        if (controlHitTransparent(*w)) continue;
        return (HWND)s.zorder[i];
    }
    return nullptr;
}


/* ------------------------------------------------------------------ paint */

// There is no clipping between windows here - one surface, painted in order -
// so a window repainting itself paints straight over anything standing on top
// of it.  Whatever it will cover has to be queued to paint again after it.
static void invalidateAbove(const Window & w) {
    State & s = state();
    uintptr_t self = 0;
    for (auto & entry : s.windows)
        if (&entry.second == &w) { self = entry.first; break; }
    if (!self) return;

    auto at = std::find(s.zorder.begin(), s.zorder.end(), self);
    if (at == s.zorder.end()) return;
    for (auto it = at + 1; it != s.zorder.end(); ++it) {
        Window * above = window((HWND)*it);
        if (!above || !above->visible) continue;
        RECT overlap;
        if (!IntersectRect(&overlap, &w.rect, &above->rect)) continue;
        // Marked directly rather than through invalidate(), because each of
        // these would otherwise walk the rest of the order again for itself.
        above->invalid = clientRect(*above);
        above->needsPaint = true;
        above->eraseOnPaint = true;
    }
}

void invalidate(Window & w, const RECT * r, bool erase) {
    const RECT client = clientRect(w);
    const RECT area = r ? *r : client;
    if (w.needsPaint) {
        RECT merged;
        UnionRect(&merged, &w.invalid, &area);
        w.invalid = merged;
    } else {
        w.invalid = area;
    }
    w.needsPaint = true;
    w.eraseOnPaint = w.eraseOnPaint || erase;
    invalidateAbove(w);
}

extern "C" BOOL InvalidateRect(HWND hwnd, const RECT * r, BOOL erase) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    invalidate(*w, r, erase != 0);
    return TRUE;
}

extern "C" BOOL ValidateRect(HWND hwnd, const RECT *) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    w->needsPaint = false;
    return TRUE;
}

static HDC makeWindowDc(Window & w) {
    DeviceContext d{};
    d.target = std::shared_ptr<Surface>(&state().screen, [](Surface *) {});
    d.origin = clientOrigin(w);
    d.pen = GetStockObject(BLACK_PEN);
    d.brush = GetStockObject(WHITE_BRUSH);
    d.font = GetStockObject(SYSTEM_FONT);
    const RECT client = clientRect(w);
    d.clip = client;
    const uintptr_t h = state().allocate();
    state().dcs[h] = d;
    return (HDC)h;
}

extern "C" HDC GetDC(HWND hwnd) {
    Window * w = window(hwnd);
    if (!w) {
        // A null window means the screen, which the port asks for when it wants
        // metrics rather than to draw.
        DeviceContext d{};
        d.target = std::shared_ptr<Surface>(&state().screen, [](Surface *) {});
        d.pen = GetStockObject(BLACK_PEN);
        d.brush = GetStockObject(WHITE_BRUSH);
        d.font = GetStockObject(SYSTEM_FONT);
        const uintptr_t h = state().allocate();
        state().dcs[h] = d;
        return (HDC)h;
    }
    return makeWindowDc(*w);
}

extern "C" HDC GetWindowDC(HWND hwnd) {
    Window * w = window(hwnd);
    if (!w) return GetDC(nullptr);
    HDC hdc = makeWindowDc(*w);
    if (DeviceContext * d = dc(hdc)) {
        // The whole window, frame included.
        d->origin = POINT{w->rect.left, w->rect.top};
        d->clip = RECT{0, 0, w->rect.right - w->rect.left,
                       w->rect.bottom - w->rect.top};
    }
    return hdc;
}

extern "C" int ReleaseDC(HWND, HDC hdc) {
    if (!dc(hdc)) return 0;
    state().dcs.erase((uintptr_t)hdc);
    return 1;
}

extern "C" HDC BeginPaint(HWND hwnd, LPPAINTSTRUCT ps) {
    Window * w = window(hwnd);
    if (!w || !ps) return nullptr;
    memset(ps, 0, sizeof(*ps));
    HDC hdc = makeWindowDc(*w);
    ps->hdc = hdc;
    ps->rcPaint = w->invalid;
    ps->fErase = w->eraseOnPaint;

    if (w->eraseOnPaint) {
        // WM_ERASEBKGND first, and the class brush if nothing claims it, which
        // is what stops a window from painting over whatever was there before.
        if (!send(hwnd, WM_ERASEBKGND, (WPARAM)hdc, 0)) {
            HBRUSH brush = nullptr;
            auto it = state().classes.find(w->className);
            if (it != state().classes.end()) brush = it->second.background;
            const RECT client = clientRect(*w);
            FillRect(hdc, &client, brush);
        }
    }

    w->needsPaint = false;
    w->eraseOnPaint = false;
    return hdc;
}

extern "C" BOOL EndPaint(HWND, const PAINTSTRUCT * ps) {
    if (!ps) return FALSE;
    if (dc(ps->hdc)) state().dcs.erase((uintptr_t)ps->hdc);
    return TRUE;
}


/* ------------------------------------------------------------- the frame */

// Windows 3.1 chrome, drawn plainly: the game's own art was designed to sit
// inside it, so leaving it out would look wrong rather than clean.
void drawFrame(Window & w) {
    if (!w.visible) return;
    State & s = state();

    DeviceContext d{};
    d.target = std::shared_ptr<Surface>(&s.screen, [](Surface *) {});
    d.origin = POINT{w.rect.left, w.rect.top};
    d.font = GetStockObject(SYSTEM_FONT);
    d.bkMode = TRANSPARENT;

    const int width = w.rect.right - w.rect.left;
    const int height = w.rect.bottom - w.rect.top;
    const int b = borderWidth(w);

    if (b > 0) {
        fillRect(d, RECT{0, 0, width, b}, fromColorref(RGB(192, 192, 192)));
        fillRect(d, RECT{0, height - b, width, height}, fromColorref(RGB(192,192,192)));
        fillRect(d, RECT{0, 0, b, height}, fromColorref(RGB(192, 192, 192)));
        fillRect(d, RECT{width - b, 0, width, height}, fromColorref(RGB(192,192,192)));
        drawLine(d, 0, 0, width - 1, 0, fromColorref(RGB(0, 0, 0)));
        drawLine(d, 0, 0, 0, height - 1, fromColorref(RGB(0, 0, 0)));
        drawLine(d, width - 1, 0, width - 1, height - 1, fromColorref(RGB(0,0,0)));
        drawLine(d, 0, height - 1, width - 1, height - 1, fromColorref(RGB(0,0,0)));
    }

    const int caption = captionHeight(w);
    if (caption > 0) {
        const bool active = ((HWND)0 == nullptr) && (s.active == nullptr ? false : true);
        const COLORREF bar = GetSysColor(active ? COLOR_ACTIVECAPTION
                                               : COLOR_ACTIVECAPTION);
        fillRect(d, RECT{b, b, width - b, b + caption}, fromColorref(bar));
        d.textColour = GetSysColor(COLOR_CAPTIONTEXT);
        const std::string title = toUtf8(w.text.c_str());
        d.textAlign = TA_LEFT | TA_TOP;
        drawText(d, b + 4, b + 3, title.c_str(), (int)title.size());
    }

    const int bar = menuBarHeight(w);
    if (bar > 0) {
        DeviceContext md = d;
        md.origin = POINT{w.rect.left + b, w.rect.top + b + caption};
        fillRect(md, RECT{0, 0, width - b * 2, bar},
                 fromColorref(GetSysColor(COLOR_MENU)));
        drawMenuBar(md, w);
    }

    // The window's own scroll bars.  The port installs their ranges at startup
    // and answers WM_VSCROLL/WM_HSCROLL; until they were drawn there was
    // nothing to answer with, and a strip of bare window where they belong.
    DeviceContext sd = d;
    sd.origin = POINT{0, 0};
    if (w.style & WS_VSCROLL)
        paintScrollBar(sd, windowScrollRect(w, true), w.scroll[SB_VERT], true);
    if (w.style & WS_HSCROLL)
        paintScrollBar(sd, windowScrollRect(w, false), w.scroll[SB_HORZ], false);
    if ((w.style & WS_VSCROLL) && (w.style & WS_HSCROLL)) {
        // The square where the two meet belongs to neither.
        const RECT vertical = windowScrollRect(w, true);
        const RECT horizontal = windowScrollRect(w, false);
        fillRect(sd, RECT{vertical.left, horizontal.top,
                          vertical.right, horizontal.bottom},
                 fromColorref(GetSysColor(COLOR_BTNFACE)));
    }
}

void invalidateArea(const RECT & area) {
    State & s = state();
    if (area.right <= area.left || area.bottom <= area.top) return;

    // The ground first.  Windows 3.1's desktop is what was under everything,
    // and with one shared surface there is nothing else left to reveal.
    DeviceContext d{};
    d.target = std::shared_ptr<Surface>(&s.screen, [](Surface *) {});
    fillRect(d, area, fromColorref(GetSysColor(COLOR_BACKGROUND)));

    // Then everything still standing that overlapped it.
    for (auto & entry : s.windows) {
        Window & w = entry.second;
        if (w.destroyed || !w.visible) continue;
        RECT overlap;
        if (!IntersectRect(&overlap, &area, &w.rect)) continue;
        invalidate(w, nullptr, true);
    }
}

void paintPending() {
    State & s = state();

    // A copy of the order, because a WM_PAINT can create or destroy windows.
    std::vector<HWND> order;
    order.reserve(s.zorder.size());
    for (uintptr_t handle : s.zorder) order.push_back((HWND)handle);

    for (HWND hwnd : order) {
        Window * w = window(hwnd);
        if (!w || !w->visible || !w->needsPaint) continue;
        drawFrame(*w);
        send(hwnd, WM_PAINT, 0, 0);
        if (Window * again = window(hwnd)) again->needsPaint = false;
    }
}

void presentScreen() {
    // An open menu is drawn here rather than with the frame, so that it lands
    // on top of whatever was painted after the frame was.
    drawMenuOverlay();
    hostPresent(state().screen);
}


/* ---------------------------------------------------------------- messages */

void post(HWND h, UINT msg, WPARAM w, LPARAM l) {
    QueuedMessage q{h, msg, w, l, GetTickCount(), state().mouse};
    state().queue.push_back(q);
}

LRESULT send(HWND h, UINT msg, WPARAM w, LPARAM l) {
    Window * win = window(h);
    if (!win) return 0;
    // Dialogs answer through their own procedure first; anything it does not
    // claim falls through to the default handling.
    if (win->isDialog && win->dialogProc) {
        const INT_PTR handled = win->dialogProc(h, msg, w, l);
        if (handled) return (LRESULT)handled;
    }
    if (win->proc) return win->proc(h, msg, w, l);
    // A class nobody registered is one of the built-in controls - BUTTON,
    // STATIC, EDIT and the rest - and win32_control.cpp answers for it.
    if (!win->controlClass.empty()) return controlProc(h, msg, w, l);
    return DefWindowProcW(h, msg, w, l);
}

extern "C" LRESULT SendMessageW(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    return send(hwnd, msg, w, l);
}

extern "C" BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    post(hwnd, msg, w, l);
    return TRUE;
}

extern "C" void PostQuitMessage(int code) {
    state().quitting = true;
    state().exitCode = code;
    post(nullptr, WM_QUIT, (WPARAM)code, 0);
}

extern "C" LRESULT SendDlgItemMessageW(HWND dlg, int id, UINT msg, WPARAM w,
                                       LPARAM l) {
    HWND item = GetDlgItem(dlg, id);
    return item ? send(item, msg, w, l) : 0;
}

static void fireTimers() {
    State & s = state();
    const double now = hostNow();
    // A copy, because a timer procedure may add or kill timers.
    std::vector<Timer> due;
    for (Timer & t : s.timers)
        if (now >= t.next) { due.push_back(t); t.next = now + t.interval; }
    for (Timer & t : due) {
        if (t.proc) t.proc(t.hwnd, WM_TIMER, t.id, (DWORD)now);
        else post(t.hwnd, WM_TIMER, (WPARAM)t.id, 0);
    }
}

// Published, and the thread handed back, at the one moment nothing is half
// drawn.  Rate-limited rather than every call: the port polls constantly, and
// yielding on each poll would leave no time to simulate in.
static void publishAndYield() {
#ifdef __EMSCRIPTEN__
    static double lastPublish = 0;
    const double now = hostNow();
    if (now - lastPublish < 16.0) return;
    lastPublish = now;
    presentScreen();
    emscripten_sleep(0);
#endif
}

extern "C" BOOL PeekMessageW(LPMSG msg, HWND filter, UINT first, UINT last,
                             UINT remove) {
    if (!msg) return FALSE;
    fireTimers();
    publishAndYield();

    State & s = state();
    for (size_t i = 0; i < s.queue.size(); i++) {
        const QueuedMessage & q = s.queue[i];
        if (filter && q.hwnd != filter) continue;
        if (first || last) {
            if (q.message < first || q.message > last) continue;
        }
        msg->hwnd = q.hwnd;
        msg->message = q.message;
        msg->wParam = q.wParam;
        msg->lParam = q.lParam;
        msg->time = q.time;
        msg->pt = q.pt;
        if (remove & PM_REMOVE) s.queue.erase(s.queue.begin() + i);
        return TRUE;
    }

    // Nothing queued, so a window with an invalid region gets its WM_PAINT
    // generated now - which is what Windows does, and what lets the port's own
    // DispatchMessage deliver it rather than the shim painting behind its back.
    const bool paintAllowed = (first == 0 && last == 0) ||
                              (first <= WM_PAINT && WM_PAINT <= last);
    if (paintAllowed) {
        for (uintptr_t handle : s.zorder) {
            Window * found = window((HWND)handle);
            if (!found || !found->visible || !found->needsPaint) continue;
            if (filter && (HWND)handle != filter) continue;
            Window & w = *found;
            drawFrame(w);
            msg->hwnd = (HWND)handle;
            msg->message = WM_PAINT;
            msg->wParam = 0;
            msg->lParam = 0;
            msg->time = GetTickCount();
            msg->pt = s.mouse;
            // Left invalid until BeginPaint clears it, so a peek without
            // PM_REMOVE reports the same message again, as Windows does.
            return TRUE;
        }
    }
    return FALSE;
}

extern "C" BOOL GetMessageW(LPMSG msg, HWND filter, UINT first, UINT last) {
    // There is no blocking here and there cannot be: the browser owns this
    // thread, so waiting for a message would wait forever.  A caller that loops
    // on GetMessage gets an empty queue reported as WM_NULL and keeps going,
    // and the frame that produces the next message comes from the main loop.
    if (!msg) return FALSE;
    if (PeekMessageW(msg, filter, first, last, PM_REMOVE))
        return msg->message != WM_QUIT;
    memset(msg, 0, sizeof(*msg));
    msg->message = WM_NULL;
    return !state().quitting;
}

extern "C" BOOL TranslateMessage(const MSG *) {
    // WM_CHAR is produced alongside WM_KEYDOWN in the host, where the browser
    // has already decided what character a key means.
    return FALSE;
}

extern "C" LRESULT DispatchMessageW(const MSG * msg) {
    if (!msg || msg->message == WM_NULL) return 0;
    if (!msg->hwnd) return 0;
    return send(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

void dispatchPending() {
    MSG msg;
    // Bounded: a message handler that posts a message must not be able to keep
    // the frame from ending.
    for (int i = 0; i < 512; i++) {
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) break;
        if (msg.message == WM_QUIT) { state().quitting = true; break; }
        DispatchMessageW(&msg);
    }
}

extern "C" UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT elapse,
                             TIMERPROC proc) {
    State & s = state();
    for (Timer & t : s.timers)
        if (t.hwnd == hwnd && t.id == id) {
            t.interval = elapse; t.proc = proc; t.next = hostNow() + elapse;
            return id;
        }
    s.timers.push_back(Timer{hwnd, id, elapse ? elapse : 1, proc,
                             hostNow() + (elapse ? elapse : 1)});
    return id;
}

extern "C" BOOL KillTimer(HWND hwnd, UINT_PTR id) {
    auto & v = state().timers;
    const size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Timer & t) {
        return t.hwnd == hwnd && t.id == id;
    }), v.end());
    return v.size() != before;
}


/* --------------------------------------------------------- default handling */

extern "C" LRESULT DefWindowProcW(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    Window * win = window(hwnd);
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_ERASEBKGND:
            return 0;      // BeginPaint has already done it
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            return 1;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_LBUTTONDOWN:
            // A click on the menu bar arrives here as a client click, because
            // the frame is not a separate window.
            if (win) {
                POINT p{(int)(short)LOWORD(l), (int)(short)HIWORD(l)};
                if (menuBarClick(*win, p)) return 0;
            }
            return 0;
        default:
            return 0;
    }
}

extern "C" LRESULT CallWindowProcW(WNDPROC proc, HWND hwnd, UINT msg,
                                   WPARAM w, LPARAM l) {
    if (!proc) return DefWindowProcW(hwnd, msg, w, l);
    return proc(hwnd, msg, w, l);
}


/* ------------------------------------------------------------- scrollbars */

extern "C" BOOL SetScrollInfo(HWND hwnd, int bar, const SCROLLINFO * info,
                              BOOL) {
    Window * w = window(hwnd);
    if (!w || !info || bar < 0 || bar > 1) return FALSE;
    SCROLLINFO & s = w->scroll[bar];
    if (info->fMask & SIF_RANGE) { s.nMin = info->nMin; s.nMax = info->nMax; }
    if (info->fMask & SIF_PAGE) s.nPage = info->nPage;
    if (info->fMask & SIF_POS) s.nPos = info->nPos;
    return TRUE;
}

extern "C" BOOL GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO info) {
    Window * w = window(hwnd);
    if (!w || !info || bar < 0 || bar > 1) return FALSE;
    const SCROLLINFO & s = w->scroll[bar];
    if (info->fMask & SIF_RANGE) { info->nMin = s.nMin; info->nMax = s.nMax; }
    if (info->fMask & SIF_PAGE) info->nPage = s.nPage;
    if (info->fMask & SIF_POS) info->nPos = s.nPos;
    info->nTrackPos = s.nPos;
    return TRUE;
}

extern "C" int SetScrollPos(HWND hwnd, int bar, int pos, BOOL) {
    Window * w = window(hwnd);
    if (!w || bar < 0 || bar > 1) return 0;
    const int was = w->scroll[bar].nPos;
    w->scroll[bar].nPos = pos;
    return was;
}

extern "C" int GetScrollPos(HWND hwnd, int bar) {
    Window * w = window(hwnd);
    return w && bar >= 0 && bar <= 1 ? w->scroll[bar].nPos : 0;
}

extern "C" BOOL SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL) {
    Window * w = window(hwnd);
    if (!w || bar < 0 || bar > 1) return FALSE;
    w->scroll[bar].nMin = min;
    w->scroll[bar].nMax = max;
    return TRUE;
}

extern "C" BOOL ShowScrollBar(HWND, int, BOOL) { return TRUE; }


/* ------------------------------------------------------------------ cursor */

extern "C" HCURSOR SetCursor(HCURSOR cursor) {
    State & s = state();
    HCURSOR was = s.cursor;
    s.cursor = cursor;
    return was;
}
extern "C" BOOL GetCursorPos(LPPOINT p) {
    if (!p) return FALSE;
    *p = state().mouse;
    return TRUE;
}
extern "C" BOOL SetCursorPos(int, int) { return FALSE; }
extern "C" int  ShowCursor(BOOL) { return 0; }
extern "C" BOOL DestroyCursor(HCURSOR) { return TRUE; }
extern "C" BOOL DestroyIcon(HICON) { return TRUE; }
extern "C" BOOL ClipCursor(const RECT *) { return TRUE; }
extern "C" SHORT GetAsyncKeyState(int key) {
    if (key < 0 || key > 255) return 0;
    return state().keys[key] ? (SHORT)0x8000 : 0;
}

extern "C" SHORT GetKeyState(int key) { return GetAsyncKeyState(key); }

}   // namespace shim
