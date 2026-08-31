// Dialogs from a template, and the last of the host's requests.
//
// The port's dialogs arrive as DLGTEMPLATEs in memory - 51 of them, using only
// the standard control classes - so there is a fixed layout to walk rather than
// a resource compiler to replace.  Strings inside a template are UTF-16, which
// is not what WCHAR is here, so they are read as char16_t explicitly.
//
// A modal dialog runs its own message loop.  That is only survivable because
// Asyncify is on: the loop yields through PeekMessage like the game's own does.

#include "win32_internal.h"

#include <commdlg.h>

#include <emscripten.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <sys/stat.h>

namespace shim {

namespace {

// Dialog units to pixels.  Win16's system font is 8x16, and a template's units
// are quarters of the average character width and eighths of its height, so
// both axes double.  Approximate, and consistent, which is what matters for a
// layout that only has to be readable.
inline int scaleX(int units) { return units * 2; }
inline int scaleY(int units) { return units * 2; }

struct Cursor {
    const BYTE * p;
    const BYTE * end;

    bool ok() const { return p && p < end; }
    void align(size_t n) {
        const size_t offset = (size_t)p & (n - 1);
        if (offset) p += n - offset;
    }
    uint16_t u16() {
        if (p + 2 > end) { p = end; return 0; }
        const uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
        p += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t low = u16();
        return low | ((uint32_t)u16() << 16);
    }

    // A name in a template is either 0xffff followed by an ordinal, or a
    // NUL-terminated run of UTF-16.
    std::string name(uint16_t * ordinal) {
        if (ordinal) *ordinal = 0;
        if (p + 2 > end) return std::string();
        if (p[0] == 0xff && p[1] == 0xff) {
            p += 2;
            const uint16_t value = u16();
            if (ordinal) *ordinal = value;
            return std::string();
        }
        const char16_t * text = (const char16_t *)p;
        size_t length = 0;
        while (p + (length + 1) * 2 <= end && text[length]) length++;
        std::string out = utf16ToUtf8(text, (int)length);
        p += (length + 1) * 2;
        return out;
    }
};

// The ordinals a template uses instead of naming a standard class.
const wchar_t * classForOrdinal(uint16_t ordinal) {
    switch (ordinal) {
        case 0x0080: return L"BUTTON";
        case 0x0081: return L"EDIT";
        case 0x0082: return L"STATIC";
        case 0x0083: return L"LISTBOX";
        case 0x0084: return L"SCROLLBAR";
        case 0x0085: return L"COMBOBOX";
        default:     return L"STATIC";
    }
}

HWND buildDialog(const DLGTEMPLATE * tmpl, HWND parent, DLGPROC proc,
                 LPARAM init, bool modal) {
    if (!tmpl) return nullptr;

    // The template is followed by its menu, class and title, then one item per
    // control - each aligned to a four-byte boundary.
    Cursor c{(const BYTE *)tmpl, (const BYTE *)tmpl + 0x10000};
    const DWORD style = c.u32();
    c.u32();                                  // dwExtendedStyle
    const uint16_t items = c.u16();
    const short x = (short)c.u16();
    const short y = (short)c.u16();
    const short cx = (short)c.u16();
    const short cy = (short)c.u16();

    uint16_t ordinal = 0;
    c.name(&ordinal);                         // menu
    c.name(&ordinal);                         // class
    const std::string title = c.name(&ordinal);
    if (style & DS_SETFONT) {
        c.u16();                              // point size
        c.name(&ordinal);                     // face
    }

    const std::wstring wideTitle = fromUtf8(title.c_str());
    HWND dialog = CreateWindowExW(0, L"#32770", wideTitle.c_str(),
                                  (style | WS_VISIBLE) & ~WS_CHILD,
                                  scaleX(x), scaleY(y), scaleX(cx), scaleY(cy),
                                  parent, nullptr, nullptr, nullptr);
    if (!dialog) return nullptr;

    Window * w = window(dialog);
    if (!w) return nullptr;
    w->isDialog = true;
    w->dialogProc = proc;

    for (uint16_t i = 0; i < items && c.ok(); i++) {
        c.align(4);
        const DWORD itemStyle = c.u32();
        c.u32();                              // dwExtendedStyle
        const short ix = (short)c.u16();
        const short iy = (short)c.u16();
        const short icx = (short)c.u16();
        const short icy = (short)c.u16();
        const uint16_t id = c.u16();

        uint16_t classOrdinal = 0;
        const std::string className = c.name(&classOrdinal);
        uint16_t textOrdinal = 0;
        const std::string text = c.name(&textOrdinal);
        const uint16_t extra = c.u16();       // creation data
        if (extra) c.p += extra;

        const std::wstring wideClass = className.empty()
            ? std::wstring(classForOrdinal(classOrdinal))
            : fromUtf8(className.c_str());
        const std::wstring wideText = fromUtf8(text.c_str());

        HWND control = CreateWindowExW(0, wideClass.c_str(), wideText.c_str(),
                                       itemStyle | WS_CHILD | WS_VISIBLE,
                                       scaleX(ix), scaleY(iy),
                                       scaleX(icx), scaleY(icy),
                                       dialog, nullptr, nullptr, nullptr);
        if (Window * cw = window(control)) {
            cw->id = id;
            cw->controlClass = wideClass;
        }
    }

    // The dialog procedure gets to set up its controls before it is shown, and
    // its return value decides whether the default focus is applied - which
    // nothing here does yet.
    send(dialog, WM_INITDIALOG, 0, init);
    if (Window * again = window(dialog)) invalidate(*again, nullptr, true);

    if (!modal) return dialog;

    // Modal: pump until EndDialog.  Bounded by the window still existing, so a
    // dialog that is destroyed rather than ended does not spin for ever.
    for (;;) {
        Window * live = window(dialog);
        if (!live || live->dialogEnded) break;
        MSG msg;
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { state().quitting = true; break; }
            DispatchMessageW(&msg);
        } else {
            presentScreen();
            emscripten_sleep(16);
        }
    }

    INT_PTR result = 0;
    if (Window * live = window(dialog)) {
        result = live->dialogResult;
        DestroyWindow(dialog);
    }
    return (HWND)(uintptr_t)result;
}

}   // namespace

extern "C" HWND CreateDialogIndirectParamW(HINSTANCE, LPCDLGTEMPLATE tmpl,
                                           HWND parent, DLGPROC proc,
                                           LPARAM init) {
    return buildDialog(tmpl, parent, proc, init, false);
}

extern "C" INT_PTR DialogBoxIndirectParamW(HINSTANCE, LPCDLGTEMPLATE tmpl,
                                           HWND parent, DLGPROC proc,
                                           LPARAM init) {
    return (INT_PTR)(uintptr_t)buildDialog(tmpl, parent, proc, init, true);
}

extern "C" INT_PTR DialogBoxParamW(HINSTANCE inst, LPCWSTR name, HWND parent,
                                   DLGPROC proc, LPARAM init) {
    HRSRC res = FindResourceW(nullptr, name, RT_DIALOG);
    if (!res) return -1;
    const BYTE * bits = resourceAt((uintptr_t)res - 1, nullptr);
    if (!bits) return -1;
    return DialogBoxIndirectParamW(inst, (LPCDLGTEMPLATE)bits, parent, proc,
                                   init);
}


/* ------------------------------------------------- the rest of the host API */

extern "C" int GetDeviceCaps(HDC, int index) {
    switch (index) {
        case HORZRES:     return state().screen.width;
        case VERTRES:     return state().screen.height;
        case BITSPIXEL:   return 32;
        case PLANES:      return 1;
        case NUMCOLORS:   return -1;      // more than 256, as a true-colour device
        case SIZEPALETTE: return 0;
        case COLORRES:    return 24;
        case RASTERCAPS:
            // Everything the port might branch on except palette support: there
            // is no hardware palette here, and claiming one would send it down a
            // path that expects RealizePalette to do something.
            return RC_BITBLT | RC_BITMAP64 | RC_DI_BITMAP | RC_DIBTODEV |
                   RC_STRETCHDIB;
        default:          return 0;
    }
}

extern "C" BOOL GetRasterizerCaps(LPRASTERIZER_STATUS status, UINT size) {
    if (!status || size < sizeof(RASTERIZER_STATUS)) return FALSE;
    status->nSize = sizeof(RASTERIZER_STATUS);
    status->wFlags = TT_AVAILABLE | TT_ENABLED;
    status->nLanguageID = 0;
    return TRUE;
}

extern "C" BOOL AnimatePalette(HPALETTE palette, UINT start, UINT count,
                               const PALETTEENTRY * entries) {
    // Windows would recolour the screen in place through the hardware palette.
    // Here the entries are simply stored: every DIB carries its own colour table
    // and is converted on blit, so the next frame picks the change up anyway.
    return SetPaletteEntries(palette, start, count, entries) == count;
}

extern "C" BOOL UpdateColors(HDC) { return TRUE; }

extern "C" BOOL AdjustWindowRectEx(LPRECT r, DWORD style, BOOL menu, DWORD) {
    if (!r) return FALSE;
    const int border = (style & WS_THICKFRAME) ? 4
                     : (style & (WS_BORDER | WS_DLGFRAME)) ? 1 : 0;
    r->left -= border;
    r->right += border;
    r->top -= border + (((style & WS_CAPTION) == WS_CAPTION) ? kCaptionBarHeight : 0)
                     + (menu ? kCaptionBarHeight : 0);
    r->bottom += border;
    return TRUE;
}

extern "C" BOOL AdjustWindowRect(LPRECT r, DWORD style, BOOL menu) {
    return AdjustWindowRectEx(r, style, menu, 0);
}

extern "C" BOOL MapWindowPoints(HWND from, HWND to, LPPOINT points,
                                UINT count) {
    if (!points) return FALSE;
    POINT origin{0, 0};
    if (Window * w = window(from)) origin = clientOrigin(*w);
    POINT target{0, 0};
    if (Window * w = window(to)) target = clientOrigin(*w);
    for (UINT i = 0; i < count; i++) {
        points[i].x += origin.x - target.x;
        points[i].y += origin.y - target.y;
    }
    return TRUE;
}

extern "C" HMODULE LoadLibraryW(LPCWSTR) {
    // Nothing can be loaded, and saying so is the honest answer: the port checks
    // for optional entry points and does without them when they are missing.
    return nullptr;
}
extern "C" HMODULE LoadLibraryA(LPCSTR) { return nullptr; }
extern "C" BOOL FreeLibrary(HMODULE) { return TRUE; }
extern "C" void * GetProcAddress(HMODULE, LPCSTR) { return nullptr; }

extern "C" DWORD GetModuleFileNameW(HMODULE, LPWSTR buf, DWORD len) {
    const wchar_t path[] = L"/SIMTOWER.EXE";
    if (!buf || len == 0) return 0;
    DWORD n = 0;
    while (path[n] && n + 1 < len) { buf[n] = path[n]; n++; }
    buf[n] = 0;
    return n;
}

extern "C" DWORD GetModuleFileNameA(HMODULE, LPSTR buf, DWORD len) {
    const char path[] = "/SIMTOWER.EXE";
    if (!buf || len == 0) return 0;
    DWORD n = 0;
    while (path[n] && n + 1 < len) { buf[n] = path[n]; n++; }
    buf[n] = 0;
    return n;
}

extern "C" UINT GetWindowsDirectoryW(LPWSTR buf, UINT len) {
    const wchar_t path[] = L"/";
    if (!buf || len == 0) return 0;
    buf[0] = path[0];
    buf[1] = 0;
    return 1;
}

extern "C" UINT GetSystemDirectoryW(LPWSTR buf, UINT len) {
    return GetWindowsDirectoryW(buf, len);
}

extern "C" UINT GetTempPathW(DWORD len, LPWSTR buf) {
    return GetWindowsDirectoryW(buf, len);
}

extern "C" DWORD GetProfileStringA(LPCSTR, LPCSTR, LPCSTR def, LPSTR buf,
                                   DWORD len) {
    if (!buf || len == 0) return 0;
    DWORD n = 0;
    if (def) while (def[n] && n + 1 < len) { buf[n] = def[n]; n++; }
    buf[n] = 0;
    return n;
}

extern "C" UINT GetProfileIntA(LPCSTR, LPCSTR, INT def) { return (UINT)def; }
extern "C" BOOL WriteProfileStringA(LPCSTR, LPCSTR, LPCSTR) { return TRUE; }

extern "C" DWORD GetFileAttributesW(LPCWSTR path) {
    const std::string narrow = toUtf8(path);
    return GetFileAttributesNarrow(narrow.c_str());
}

extern "C" DWORD GetFileAttributesA(LPCSTR path) {
    return GetFileAttributesNarrow(path);
}

extern "C" BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status) {
    if (!status) return FALSE;
    // What the runtime actually has, rather than a made-up number: the port
    // reads this to decide how much it may cache.
    const ULONGLONG total = (ULONGLONG)emscripten_run_script_int(
        "typeof HEAP8 !== 'undefined' ? HEAP8.length : 268435456");
    status->dwLength = sizeof(MEMORYSTATUSEX);
    status->dwMemoryLoad = 25;
    status->ullTotalPhys = total;
    status->ullAvailPhys = total / 2;
    status->ullTotalPageFile = total;
    status->ullAvailPageFile = total / 2;
    status->ullTotalVirtual = total;
    status->ullAvailVirtual = total / 2;
    status->ullAvailExtendedVirtual = 0;
    return TRUE;
}

extern "C" int MulDiv(int number, int numerator, int denominator) {
    if (denominator == 0) return -1;
    const long long product = (long long)number * numerator;
    // Rounded away from zero, as Windows does.
    const long long half = denominator / 2;
    return (int)((product + (product < 0 ? -half : half)) / denominator);
}

extern "C" int lstrcmpiW(LPCWSTR a, LPCWSTR b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    for (; *a && *b; a++, b++) {
        wchar_t x = *a, y = *b;
        if (x >= L'A' && x <= L'Z') x += 32;
        if (y >= L'A' && y <= L'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

extern "C" int lstrcmpiA(LPCSTR a, LPCSTR b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

extern "C" int lstrlenW(LPCWSTR s) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) n++;
    return n;
}

extern "C" LPWSTR lstrcpyW(LPWSTR dst, LPCWSTR src) {
    if (!dst) return nullptr;
    LPWSTR out = dst;
    if (src) while ((*dst++ = *src++)) {}
    else *dst = 0;
    return out;
}

extern "C" BOOL WinHelpW(HWND, LPCWSTR, UINT, ULONG_PTR) { return FALSE; }

extern "C" BOOL GetOpenFileNameW(LPOPENFILENAMEW ofn) {
    // There is no file dialog to show.  Refusing is what a cancelled dialog
    // looks like, which the port already handles.
    if (ofn && ofn->lpstrFile && ofn->nMaxFile) ofn->lpstrFile[0] = 0;
    return FALSE;
}

extern "C" BOOL GetSaveFileNameW(LPOPENFILENAMEW ofn) {
    return GetOpenFileNameW(ofn);
}

extern "C" DWORD CommDlgExtendedError(void) { return 0; }

}   // namespace shim

// Declared at global scope in windows.h, next to the narrow-path overloads, so
// it has to be defined there too rather than inside namespace shim.
DWORD GetFileAttributesNarrow(const char * path) {
    if (!path || !*path) return INVALID_FILE_ATTRIBUTES;
    struct stat info;
    if (stat(path, &info) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY
                                 : FILE_ATTRIBUTE_NORMAL;
}
