// First cut: just enough for the compiler to tell us what is actually needed.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD;
typedef unsigned long DWORD; typedef long LONG; typedef unsigned int UINT;
typedef wchar_t WCHAR; typedef char CHAR;
typedef void * PVOID; typedef void * LPVOID; typedef const void * LPCVOID;
typedef char * LPSTR; typedef const char * LPCSTR;
typedef WCHAR * LPWSTR; typedef const WCHAR * LPCWSTR;
typedef intptr_t LPARAM; typedef uintptr_t WPARAM; typedef intptr_t LRESULT;
typedef void * HANDLE; typedef HANDLE HWND, HDC, HINSTANCE, HBITMAP, HBRUSH,
        HPEN, HFONT, HPALETTE, HMENU, HCURSOR, HICON, HGDIOBJ, HRSRC, HGLOBAL,
        HMODULE, HRGN, HACCEL, HWAVEOUT;
