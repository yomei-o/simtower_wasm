// The odds and ends: strings, rectangles, time, settings, errors.

#include "win32_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <emscripten.h>

namespace shim {

/* ------------------------------------------------------------- conversion */

// WCHAR is four bytes here, so these are UTF-32 to UTF-8 and back.  Resource
// strings are a separate matter: those really are UTF-16 and go through
// utf16ToUtf8 at the point they are parsed.
static void appendUtf8(std::string & out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xc0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += (char)(0xe0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    } else {
        out += (char)(0xf0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3f));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    }
}

std::string toUtf8(const wchar_t * s, int count) {
    std::string out;
    if (!s) return out;
    // A negative count means null-terminated; a positive one is a maximum, and
    // a null inside it still ends the string - which is how LOGFONT's fixed
    // face name array arrives.
    for (int i = 0; count < 0 || i < count; i++) {
        if (!s[i]) break;
        appendUtf8(out, (uint32_t)s[i]);
    }
    return out;
}

std::wstring fromUtf8(const char * s, int count) {
    std::wstring out;
    if (!s) return out;
    const char * p = s;
    const char * end = count < 0 ? s + strlen(s) : s + count;
    while (p < end) {
        unsigned char c = (unsigned char)*p++;
        uint32_t cp = c;
        int extra = 0;
        if ((c & 0xe0) == 0xc0)      { cp = c & 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
        while (extra-- > 0 && p < end)
            cp = (cp << 6) | ((unsigned char)*p++ & 0x3f);
        out += (wchar_t)cp;
    }
    return out;
}

std::string utf16ToUtf8(const char16_t * s, int count) {
    std::string out;
    if (!s) return out;
    for (int i = 0; count < 0 || i < count; i++) {
        uint32_t cp = s[i];
        if (!cp) break;
        // Surrogate pair, if the resource happens to carry one.
        if (cp >= 0xd800 && cp <= 0xdbff && (count < 0 || i + 1 < count)) {
            const uint32_t low = s[i + 1];
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                i++;
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

extern "C" int MultiByteToWideChar(UINT, DWORD, LPCSTR src, int srcLen,
                                   LPWSTR dst, int dstLen) {
    if (!src) return 0;
    std::wstring w = fromUtf8(src, srcLen < 0 ? -1 : srcLen);
    // A zero destination length is a request for the size that would be needed,
    // including the terminator when the source was null-terminated.
    const int needed = (int)w.size() + (srcLen < 0 ? 1 : 0);
    if (dstLen == 0 || !dst) return needed;
    const int n = std::min(needed, dstLen);
    for (int i = 0; i < n; i++) dst[i] = i < (int)w.size() ? w[i] : 0;
    return n;
}

extern "C" int WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int srcLen,
                                   LPSTR dst, int dstLen, LPCSTR, BOOL *) {
    if (!src) return 0;
    std::string s = toUtf8(src, srcLen < 0 ? -1 : srcLen);
    const int needed = (int)s.size() + (srcLen < 0 ? 1 : 0);
    if (dstLen == 0 || !dst) return needed;
    const int n = std::min(needed, dstLen);
    for (int i = 0; i < n; i++) dst[i] = i < (int)s.size() ? s[i] : 0;
    return n;
}


/* -------------------------------------------------------------- rectangles */

extern "C" BOOL SetRect(LPRECT r, int l, int t, int right, int bottom) {
    if (!r) return FALSE;
    r->left = l; r->top = t; r->right = right; r->bottom = bottom;
    return TRUE;
}
extern "C" BOOL SetRectEmpty(LPRECT r) {
    if (!r) return FALSE;
    r->left = r->top = r->right = r->bottom = 0;
    return TRUE;
}
extern "C" BOOL CopyRect(LPRECT dst, const RECT * src) {
    if (!dst || !src) return FALSE;
    *dst = *src;
    return TRUE;
}
extern "C" BOOL InflateRect(LPRECT r, int dx, int dy) {
    if (!r) return FALSE;
    r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy;
    return TRUE;
}
extern "C" BOOL OffsetRect(LPRECT r, int dx, int dy) {
    if (!r) return FALSE;
    r->left += dx; r->right += dx; r->top += dy; r->bottom += dy;
    return TRUE;
}
extern "C" BOOL IsRectEmpty(const RECT * r) {
    if (!r) return TRUE;
    return r->right <= r->left || r->bottom <= r->top;
}
extern "C" BOOL PtInRect(const RECT * r, POINT p) {
    if (!r) return FALSE;
    return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}
extern "C" BOOL IntersectRect(LPRECT dst, const RECT * a, const RECT * b) {
    if (!dst || !a || !b) return FALSE;
    dst->left = std::max(a->left, b->left);
    dst->top = std::max(a->top, b->top);
    dst->right = std::min(a->right, b->right);
    dst->bottom = std::min(a->bottom, b->bottom);
    if (IsRectEmpty(dst)) { SetRectEmpty(dst); return FALSE; }
    return TRUE;
}
extern "C" BOOL UnionRect(LPRECT dst, const RECT * a, const RECT * b) {
    if (!dst || !a || !b) return FALSE;
    if (IsRectEmpty(a)) { *dst = *b; return !IsRectEmpty(dst); }
    if (IsRectEmpty(b)) { *dst = *a; return !IsRectEmpty(dst); }
    dst->left = std::min(a->left, b->left);
    dst->top = std::min(a->top, b->top);
    dst->right = std::max(a->right, b->right);
    dst->bottom = std::max(a->bottom, b->bottom);
    return TRUE;
}
extern "C" BOOL EqualRect(const RECT * a, const RECT * b) {
    if (!a || !b) return FALSE;
    return a->left == b->left && a->top == b->top &&
           a->right == b->right && a->bottom == b->bottom;
}


/* -------------------------------------------------------------------- time */

double hostNow() { return emscripten_get_now(); }

extern "C" DWORD GetTickCount(void) {
    // Since the page loaded, which is the only origin available and is what the
    // port's pacing measures against anyway.
    return (DWORD)emscripten_get_now();
}

extern "C" void Sleep(DWORD ms) {
    // Not honoured, deliberately.  There is one thread and it belongs to the
    // browser: spinning here would freeze the tab, which is exactly the failure
    // that made SDL_Delay unusable in the OpenSkyscraper port.  The frame is
    // driven by emscripten_set_main_loop instead.
    (void)ms;
}

static void fillSystemTime(LPSYSTEMTIME t, bool local) {
    if (!t) return;
    const time_t now = time(nullptr);
    struct tm parts{};
    if (local) localtime_r(&now, &parts); else gmtime_r(&now, &parts);
    t->wYear = (WORD)(parts.tm_year + 1900);
    t->wMonth = (WORD)(parts.tm_mon + 1);
    t->wDayOfWeek = (WORD)parts.tm_wday;
    t->wDay = (WORD)parts.tm_mday;
    t->wHour = (WORD)parts.tm_hour;
    t->wMinute = (WORD)parts.tm_min;
    t->wSecond = (WORD)parts.tm_sec;
    t->wMilliseconds = (WORD)((uint64_t)emscripten_get_now() % 1000);
}

extern "C" void GetLocalTime(LPSYSTEMTIME t) { fillSystemTime(t, true); }
extern "C" void GetSystemTime(LPSYSTEMTIME t) { fillSystemTime(t, false); }


/* ----------------------------------------------------------- errors, misc */

extern "C" DWORD GetLastError(void) { return state().lastError; }
extern "C" void SetLastError(DWORD err) { state().lastError = err; }

extern "C" void ExitProcess(UINT code) {
    state().quitting = true;
    state().exitCode = (int)code;
    emscripten_cancel_main_loop();
}

extern "C" HMODULE GetModuleHandleW(LPCWSTR) { return (HMODULE)0x1; }
extern "C" HMODULE GetModuleHandleA(LPCSTR)  { return (HMODULE)0x1; }

extern "C" int GetSystemMetrics(int index) {
    switch (index) {
        case SM_CXSCREEN:     return state().screen.width;
        case SM_CYSCREEN:     return state().screen.height;
        case SM_CXFULLSCREEN: return state().screen.width;
        case SM_CYFULLSCREEN: return state().screen.height;
        case SM_CXVSCROLL:    return 16;
        case SM_CYHSCROLL:    return 16;
        case SM_CYCAPTION:    return 19;
        case SM_CXBORDER:     return 1;
        case SM_CYBORDER:     return 1;
        case SM_CXFRAME:      return 4;
        case SM_CYFRAME:      return 4;
        case SM_CYMENU:       return 19;
        default:              return 0;
    }
}

extern "C" DWORD GetSysColor(int index) {
    // The Windows 3.1 defaults, which is the scheme the game's own chrome was
    // drawn to sit inside.
    switch (index) {
        case COLOR_SCROLLBAR:       return RGB(192, 192, 192);
        case COLOR_BACKGROUND:      return RGB(0, 128, 128);
        case COLOR_ACTIVECAPTION:   return RGB(0, 0, 128);
        case COLOR_INACTIVECAPTION: return RGB(128, 128, 128);
        case COLOR_MENU:            return RGB(192, 192, 192);
        case COLOR_WINDOW:          return RGB(255, 255, 255);
        case COLOR_WINDOWFRAME:     return RGB(0, 0, 0);
        case COLOR_MENUTEXT:        return RGB(0, 0, 0);
        case COLOR_WINDOWTEXT:      return RGB(0, 0, 0);
        case COLOR_CAPTIONTEXT:     return RGB(255, 255, 255);
        case COLOR_HIGHLIGHT:       return RGB(0, 0, 128);
        case COLOR_HIGHLIGHTTEXT:   return RGB(255, 255, 255);
        case COLOR_BTNFACE:         return RGB(192, 192, 192);
        case COLOR_BTNSHADOW:       return RGB(128, 128, 128);
        case COLOR_GRAYTEXT:        return RGB(128, 128, 128);
        case COLOR_BTNTEXT:         return RGB(0, 0, 0);
        case COLOR_3DHILIGHT:       return RGB(255, 255, 255);
        default:                    return RGB(192, 192, 192);
    }
}

extern "C" HBRUSH GetSysColorBrush(int index) {
    return CreateSolidBrush(GetSysColor(index));
}

extern "C" BOOL MessageBeep(UINT) { return TRUE; }


/* ------------------------------------------------------------- settings */

// The port reads a handful of values out of an ini file.  There is no ini file
// in a browser, so the defaults it asks for are what it gets - which is the
// same answer a fresh install would give.
extern "C" UINT GetPrivateProfileIntW(LPCWSTR, LPCWSTR, INT def, LPCWSTR) {
    return (UINT)def;
}

extern "C" DWORD GetPrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR def,
                                          LPWSTR buf, DWORD len, LPCWSTR) {
    if (!buf || len == 0) return 0;
    DWORD n = 0;
    if (def) while (def[n] && n + 1 < len) { buf[n] = def[n]; n++; }
    buf[n] = 0;
    return n;
}

extern "C" BOOL WritePrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR) {
    return TRUE;
}

}   // namespace shim
