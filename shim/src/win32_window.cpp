// Windows, messages, and the frame around them.
//
// Everything lives in one screen surface: a window's DC draws into it at the
// window's own origin, so the port draws in client coordinates and never learns
// that there is no window manager here.

#include "win32_internal.h"

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

}   // namespace

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

RECT clientRect(const Window & w) {
    const int b = borderWidth(w);
    RECT r{0, 0,
           w.rect.right - w.rect.left - b * 2,
           w.rect.bottom - w.rect.top - b * 2 - captionHeight(w) - menuBarHeight(w)};
    if (r.right < 0) r.right = 0;
    if (r.bottom < 0) r.bottom = 0;
    return r;
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
    if (Window * again = window(hwnd)) again->destroyed = true;
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
    if (w->visible) invalidate(*w, nullptr, true);
    else if (Window * p = window(w->parent)) invalidate(*p, nullptr, true);
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

extern "C" BOOL MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    Window * win = window(hwnd);
    if (!win) return FALSE;
    win->rect = RECT{x, y, x + w, y + h};
    send(hwnd, WM_MOVE, 0, MAKELPARAM(x, y));
    const RECT client = clientRect(*win);
    send(hwnd, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom));
    if (repaint) invalidate(*win, nullptr, true);
    return TRUE;
}

extern "C" BOOL SetWindowPos(HWND hwnd, HWND, int x, int y, int w, int h,
                             UINT flags) {
    Window * win = window(hwnd);
    if (!win) return FALSE;
    RECT r = win->rect;
    if (!(flags & SWP_NOMOVE)) { r.right += x - r.left; r.bottom += y - r.top;
                                 r.left = x; r.top = y; }
    if (!(flags & SWP_NOSIZE)) { r.right = r.left + w; r.bottom = r.top + h; }
    if (flags & SWP_SHOWWINDOW) win->visible = true;
    if (flags & SWP_HIDEWINDOW) win->visible = false;
    return MoveWindow(hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top,
                      !(flags & SWP_NOREDRAW));
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
extern "C" HWND GetDesktopWindow(void) { return nullptr; }
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

HWND windowAtPoint(POINT screen) {
    // Topmost first, and children before their parent, which is the order the
    // windows were created in reversed.
    State & s = state();
    HWND best = nullptr;
    for (auto & entry : s.windows) {
        Window & w = entry.second;
        if (w.destroyed || !w.visible) continue;
        if (screen.x < w.rect.left || screen.x >= w.rect.right) continue;
        if (screen.y < w.rect.top || screen.y >= w.rect.bottom) continue;
        best = (HWND)entry.first;      // later handles are newer, so they win
    }
    return best;
}


/* ------------------------------------------------------------------ paint */

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
static void drawFrame(Window & w) {
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
}

void paintPending() {
    State & s = state();

    // A copy of the handles, because a WM_PAINT can create or destroy windows.
    std::vector<HWND> order;
    order.reserve(s.windows.size());
    for (auto & entry : s.windows) order.push_back((HWND)entry.first);

    for (HWND hwnd : order) {
        Window * w = window(hwnd);
        if (!w || !w->visible || !w->needsPaint) continue;
        drawFrame(*w);
        send(hwnd, WM_PAINT, 0, 0);
        if (Window * again = window(hwnd)) again->needsPaint = false;
    }
}

void presentScreen() { hostPresent(state().screen); }


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

extern "C" BOOL PeekMessageW(LPMSG msg, HWND filter, UINT first, UINT last,
                             UINT remove) {
    if (!msg) return FALSE;
    fireTimers();

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
extern "C" SHORT GetAsyncKeyState(int) { return 0; }

}   // namespace shim
