// The Win32 resource API, over whatever win32_ne.cpp parsed.
//
// This is the part that turned out to be almost free.  The port's own resource
// script is a single RCDATA blob and a generated index, so none of Windows'
// resource compiler is needed - and going straight to the executable's own NE
// table means the page can ask the player for their SIMTOWER.EXE and nothing
// copyrighted is committed or served.  windres has no Emscripten equivalent and
// would have been the expensive thing to replace.

#include "win32_internal.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace shim {

namespace {

// MAKEINTRESOURCE values are small integers cast to a pointer, so anything
// above the id ceiling is a real string - which these resources never use.
uint16_t typeNumber(LPCWSTR type) {
    const uintptr_t v = (uintptr_t)type;
    return v <= 0xffff ? (uint16_t)v : 0;
}

}   // namespace

extern "C" HRSRC FindResourceW(HMODULE, LPCWSTR name, LPCWSTR type) {
    const uint16_t wanted = typeNumber(type);
    const uintptr_t id = (uintptr_t)name;
    if (!wanted || id > 0xffff) return nullptr;
    const size_t index = findResourceIndex(wanted, (uint16_t)id);
    return index ? (HRSRC)(uintptr_t)index : nullptr;
}

extern "C" HRSRC FindResourceA(HMODULE mod, LPCSTR name, LPCSTR type) {
    return FindResourceW(mod, (LPCWSTR)(uintptr_t)name, (LPCWSTR)(uintptr_t)type);
}

extern "C" HGLOBAL LoadResource(HMODULE, HRSRC res) {
    // Nothing to load: the image is already in memory, so the handle carries
    // through and LockResource resolves it.
    return (HGLOBAL)res;
}

extern "C" LPVOID LockResource(HGLOBAL res) {
    const uintptr_t v = (uintptr_t)res;
    if (!v) return nullptr;
    return (LPVOID)resourceAt(v - 1, nullptr);
}

extern "C" BOOL FreeResource(HGLOBAL) { return TRUE; }

extern "C" DWORD SizeofResource(HMODULE, HRSRC res) {
    const uintptr_t v = (uintptr_t)res;
    if (!v) return 0;
    size_t size = 0;
    if (!resourceAt(v - 1, &size)) return 0;
    return (DWORD)size;
}

extern "C" int LoadStringW(HINSTANCE, UINT id, LPWSTR buf, int len) {
    if (!buf || len <= 0) return 0;
    buf[0] = 0;

    // Win16 packs strings sixteen to a block, the block numbered id/16 + 1 and
    // the string at id%16 inside it, each one a length byte then its bytes.
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id / 16 + 1), RT_STRING);
    if (!res) return 0;
    size_t size = 0;
    const BYTE * block = resourceAt((uintptr_t)res - 1, &size);
    if (!block) return 0;

    const UINT wanted = id % 16;
    const BYTE * p = block;
    const BYTE * end = block + size;
    for (UINT i = 0; i < wanted && p < end; i++)
        p += 1 + *p;
    if (p >= end) return 0;

    const int count = *p++;
    // Win16 string resources are single-byte, so this widens rather than
    // decodes.
    int n = 0;
    for (; n < count && n + 1 < len && p + n < end; n++)
        buf[n] = (WCHAR)(unsigned char)p[n];
    buf[n] = 0;
    return n;
}

extern "C" HBITMAP LoadBitmapW(HINSTANCE, LPCWSTR name) {
    HRSRC res = FindResourceW(nullptr, name, RT_BITMAP);
    if (!res) return nullptr;
    size_t size = 0;
    const BYTE * bits = resourceAt((uintptr_t)res - 1, &size);
    if (!bits || size < sizeof(BITMAPINFOHEADER)) return nullptr;

    // A BITMAP resource is a DIB with no file header: the info header, then the
    // colour table, then the pixels.
    const BITMAPINFO * info = (const BITMAPINFO *)bits;
    const DWORD headerSize = info->bmiHeader.biSize;
    DWORD colours = info->bmiHeader.biClrUsed;
    if (colours == 0 && info->bmiHeader.biBitCount <= 8)
        colours = 1u << info->bmiHeader.biBitCount;
    const BYTE * pixels = bits + headerSize + colours * sizeof(RGBQUAD);
    if (pixels > bits + size) return nullptr;

    return CreateDIBitmap(nullptr, &info->bmiHeader, 1, pixels, info,
                          DIB_RGB_COLORS);
}

extern "C" HANDLE LoadImageW(HINSTANCE inst, LPCWSTR name, UINT type,
                             int, int, UINT) {
    if (type == IMAGE_BITMAP) return (HANDLE)LoadBitmapW(inst, name);
    return nullptr;
}

extern "C" HCURSOR LoadCursorW(HINSTANCE, LPCWSTR name) {
    // Cursors are the browser's business: the canvas gets a CSS cursor rather
    // than a drawn one, so the handle only has to be distinct and comparable.
    // The mapping to CSS lives in win32_host.cpp.
    static std::map<uintptr_t, HCURSOR> cache;
    const uintptr_t key = (uintptr_t)name;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    HCURSOR h = (HCURSOR)(void *)state().allocate();
    cache[key] = h;
    return h;
}

extern "C" HICON LoadIconW(HINSTANCE, LPCWSTR) {
    return (HICON)(void *)state().allocate();
}

extern "C" HICON CreateIconFromResourceEx(PBYTE, DWORD, BOOL, DWORD, int, int,
                                          UINT) {
    return (HICON)(void *)state().allocate();
}

}   // namespace shim
