// A small Win32 for Emscripten — enough of it to build the SimTower port.
//
// Only what the port actually asks for is here; the list was taken from the
// compiler rather than from memory, by building every translation unit against
// a header containing nothing but typedefs and collecting what it complained
// about.
//
// Two layout rules matter and are not negotiable:
//
//   * BITMAPINFOHEADER, RGBQUAD, PALETTEENTRY, LOGPALETTE and the dialog
//     templates are read straight out of the original executable's resources,
//     so their field order and sizes have to match Windows exactly.
//   * WCHAR is wchar_t, which is four bytes here rather than two.  That is fine
//     for the API surface, but any UTF-16 that comes out of a resource is
//     handled as char16_t explicitly at the point it is parsed - never by
//     casting it to WCHAR.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ------------------------------------------------------------------ scalars */

typedef int                  BOOL;
typedef unsigned char        BYTE;
typedef unsigned short       WORD;
/* long, not int, exactly as Win32 declares them.  Both are 32 bits under
   Emscripten so nothing about layout changes, but the port writes
   std::max(0L, rect.right) and that does not compile if LONG is int. */
typedef unsigned long        DWORD;
typedef long                 LONG;
typedef unsigned long        ULONG;
typedef unsigned int         UINT;
typedef int                  INT;
typedef short                SHORT;
typedef unsigned short       USHORT;
typedef char                 CHAR;
typedef wchar_t              WCHAR;
typedef float                FLOAT;
typedef int64_t              LONGLONG;
typedef uint64_t             ULONGLONG;

typedef intptr_t             INT_PTR;
typedef uintptr_t            UINT_PTR;
typedef intptr_t             LONG_PTR;
typedef uintptr_t            ULONG_PTR;

typedef BYTE *               PBYTE;
typedef BYTE *               LPBYTE;
typedef WORD *               PWORD;
typedef WORD *               LPWORD;
typedef DWORD *              PDWORD;
typedef DWORD *              LPDWORD;
typedef LONG *               PLONG;
typedef LONG *               LPLONG;
typedef int *                PINT;
typedef void *               PVOID;
typedef void *               LPVOID;
typedef const void *         LPCVOID;

typedef CHAR *               PSTR;
typedef CHAR *               LPSTR;
typedef const CHAR *         PCSTR;
typedef const CHAR *         LPCSTR;
typedef WCHAR *              PWSTR;
typedef WCHAR *              LPWSTR;
typedef const WCHAR *        PCWSTR;
typedef const WCHAR *        LPCWSTR;

#ifdef UNICODE
typedef WCHAR                TCHAR;
typedef LPWSTR               LPTSTR;
typedef LPCWSTR              LPCTSTR;
#else
typedef CHAR                 TCHAR;
typedef LPSTR                LPTSTR;
typedef LPCSTR               LPCTSTR;
#endif

typedef UINT_PTR             WPARAM;
typedef LONG_PTR             LPARAM;
typedef LONG_PTR             LRESULT;
typedef DWORD                COLORREF;
typedef LONG                 HRESULT;
typedef unsigned short       ATOM;

#define TRUE   1
#define FALSE  0
#ifndef NULL
#define NULL   0
#endif

#define MAX_PATH 260
#define CALLBACK
#define WINAPI
#define APIENTRY
#define WINAPIV
#define IN
#define OUT
#define OPTIONAL
#define CONST const

#define LOWORD(l)   ((WORD)(((ULONG_PTR)(l)) & 0xffff))
#define HIWORD(l)   ((WORD)((((ULONG_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w)   ((BYTE)(((ULONG_PTR)(w)) & 0xff))
#define HIBYTE(w)   ((BYTE)((((ULONG_PTR)(w)) >> 8) & 0xff))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | (((DWORD)((WORD)(b))) << 16)))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))
#define MAKELPARAM(l, h) ((LPARAM)MAKELONG(l, h))
#define MAKEWPARAM(l, h) ((WPARAM)MAKELONG(l, h))

#define RGB(r, g, b) \
    ((COLORREF)(((BYTE)(r)) | (((WORD)((BYTE)(g))) << 8) | (((DWORD)((BYTE)(b))) << 16)))
#define GetRValue(c) ((BYTE)((c) & 0xff))
#define GetGValue(c) ((BYTE)(((c) >> 8) & 0xff))
#define GetBValue(c) ((BYTE)(((c) >> 16) & 0xff))
#define PALETTEINDEX(i) ((COLORREF)(0x01000000 | (DWORD)(WORD)(i)))
#define PALETTERGB(r, g, b) (0x02000000 | RGB(r, g, b))

#define CLR_INVALID ((COLORREF)0xffffffff)
#define GDI_ERROR   ((DWORD)0xffffffff)


/* ------------------------------------------------------------------ handles */

#define DECLARE_HANDLE(name) typedef struct name##__ { int unused; } * name

typedef void *  HANDLE;
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HINSTANCE);
/* void *, as in the real SDK - which is what lets DeleteObject(hBrush)
   and SelectObject(dc, hFont) compile without a cast. */
typedef void * HGDIOBJ;
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
/* The real SDK aliases these, which is why casting one to the other
   compiles in Windows code. */
typedef HICON HCURSOR;
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HGLOBAL);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HFILE);

typedef HINSTANCE HMODULE;


/* -------------------------------------------------------------- geometry */

typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagPOINTS { SHORT x, y; } POINTS;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT * LPCRECT;


/* ----------------------------------------------------------------- bitmaps */

/* Exact Windows layout: these come out of the original resources verbatim. */
typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

typedef struct tagRGBTRIPLE {
    BYTE rgbtBlue, rgbtGreen, rgbtRed;
} RGBTRIPLE;

#pragma pack(push, 1)
typedef struct tagBITMAPFILEHEADER {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1, bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth, biHeight;
    WORD  biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG  biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPCOREHEADER {
    DWORD bcSize;
    WORD  bcWidth, bcHeight, bcPlanes, bcBitCount;
} BITMAPCOREHEADER, *LPBITMAPCOREHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

typedef struct tagBITMAP {
    LONG   bmType, bmWidth, bmHeight, bmWidthBytes;
    WORD   bmPlanes, bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

#define BI_RGB        0
#define BI_RLE8       1
#define BI_RLE4       2
#define BI_BITFIELDS  3

#define DIB_RGB_COLORS  0
#define DIB_PAL_COLORS  1

#define BLACKONWHITE  1
#define WHITEONBLACK  2
#define COLORONCOLOR  3
#define HALFTONE      4

#define SRCCOPY       0x00CC0020u
#define SRCPAINT      0x00EE0086u
#define SRCAND        0x008800C6u
#define SRCINVERT     0x00660046u
#define SRCERASE      0x00440328u
#define NOTSRCCOPY    0x00330008u
#define DSTINVERT     0x00550009u
#define BLACKNESS     0x00000042u
#define WHITENESS     0x00FF0062u


/* ---------------------------------------------------------------- palettes */

typedef struct tagPALETTEENTRY {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagLOGPALETTE {
    WORD         palVersion, palNumEntries;
    PALETTEENTRY palPalEntry[1];
} LOGPALETTE, *PLOGPALETTE, *LPLOGPALETTE;

#define PC_RESERVED   0x01
#define PC_EXPLICIT   0x02
#define PC_NOCOLLAPSE 0x04


/* ------------------------------------------------------------------- fonts */

#define LF_FACESIZE     32
#define LF_FULLFACESIZE 64

typedef struct tagLOGFONTA {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
    BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;

typedef struct tagLOGFONTW {
    LONG  lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE  lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
    BYTE  lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *LPLOGFONTW;

typedef struct tagTEXTMETRICA {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    LONG tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
    LONG tmDigitizedAspectX, tmDigitizedAspectY;
    CHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *LPTEXTMETRICA;

typedef struct tagTEXTMETRICW {
    LONG  tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    LONG  tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
    LONG  tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE  tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW, *LPTEXTMETRICW;

#ifdef UNICODE
typedef LOGFONTW    LOGFONT;
typedef PLOGFONTW   PLOGFONT;
typedef LPLOGFONTW  LPLOGFONT;
typedef TEXTMETRICW TEXTMETRIC;
typedef LPTEXTMETRICW LPTEXTMETRIC;
#else
typedef LOGFONTA    LOGFONT;
typedef PLOGFONTA   PLOGFONT;
typedef LPLOGFONTA  LPLOGFONT;
typedef TEXTMETRICA TEXTMETRIC;
typedef LPTEXTMETRICA LPTEXTMETRIC;
#endif

#define FW_DONTCARE   0
#define FW_THIN       100
#define FW_NORMAL     400
#define FW_REGULAR    400
#define FW_MEDIUM     500
#define FW_SEMIBOLD   600
#define FW_BOLD       700
#define FW_HEAVY      900

#define ANSI_CHARSET        0
#define DEFAULT_CHARSET     1
#define SYMBOL_CHARSET      2
#define OEM_CHARSET         255

#define OUT_DEFAULT_PRECIS  0
#define OUT_TT_PRECIS       4
#define OUT_TT_ONLY_PRECIS  7

#define CLIP_DEFAULT_PRECIS 0

#define DEFAULT_QUALITY     0
#define DRAFT_QUALITY       1
#define PROOF_QUALITY       2
#define ANTIALIASED_QUALITY 4

#define DEFAULT_PITCH       0
#define FIXED_PITCH         1
#define VARIABLE_PITCH      2

#define FF_DONTCARE   (0 << 4)
#define FF_ROMAN      (1 << 4)
#define FF_SWISS      (2 << 4)
#define FF_MODERN     (3 << 4)


/* ------------------------------------------------------------- text output */

#define TRANSPARENT   1
#define OPAQUE        2

#define TA_NOUPDATECP 0
#define TA_UPDATECP   1
#define TA_LEFT       0
#define TA_RIGHT      2
#define TA_CENTER     6
#define TA_TOP        0
#define TA_BOTTOM     8
#define TA_BASELINE   24

#define DT_LEFT       0x00000000
#define DT_CENTER     0x00000001
#define DT_RIGHT      0x00000002
#define DT_TOP        0x00000000
#define DT_VCENTER    0x00000004
#define DT_BOTTOM     0x00000008
#define DT_WORDBREAK  0x00000010
#define DT_SINGLELINE 0x00000020
#define DT_NOCLIP     0x00000100
#define DT_CALCRECT   0x00000400


/* ----------------------------------------------------------- stock objects */

#define WHITE_BRUSH         0
#define LTGRAY_BRUSH        1
#define GRAY_BRUSH          2
#define DKGRAY_BRUSH        3
#define BLACK_BRUSH         4
#define NULL_BRUSH          5
#define HOLLOW_BRUSH        NULL_BRUSH
#define WHITE_PEN           6
#define BLACK_PEN           7
#define NULL_PEN            8
#define OEM_FIXED_FONT      10
#define ANSI_FIXED_FONT     11
#define ANSI_VAR_FONT       12
#define SYSTEM_FONT         13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE     15
#define SYSTEM_FIXED_FONT   16
#define DEFAULT_GUI_FONT    17

#define PS_SOLID       0
#define PS_DASH        1
#define PS_DOT         2
#define PS_DASHDOT     3
#define PS_DASHDOTDOT  4
#define PS_NULL        5
#define PS_INSIDEFRAME 6

#define BS_SOLID       0
#define BS_NULL        1
#define BS_HOLLOW      BS_NULL
#define BS_HATCHED     2
#define BS_PATTERN     3

#define R2_COPYPEN     13
#define R2_XORPEN      7
#define R2_NOT         6


/* --------------------------------------------------------- system colours */

#define COLOR_SCROLLBAR        0
#define COLOR_BACKGROUND       1
#define COLOR_ACTIVECAPTION    2
#define COLOR_INACTIVECAPTION  3
#define COLOR_MENU             4
#define COLOR_WINDOW           5
#define COLOR_WINDOWFRAME      6
#define COLOR_MENUTEXT         7
#define COLOR_WINDOWTEXT       8
#define COLOR_CAPTIONTEXT      9
#define COLOR_ACTIVEBORDER     10
#define COLOR_INACTIVEBORDER   11
#define COLOR_APPWORKSPACE     12
#define COLOR_HIGHLIGHT        13
#define COLOR_HIGHLIGHTTEXT    14
#define COLOR_BTNFACE          15
#define COLOR_BTNSHADOW        16
#define COLOR_GRAYTEXT         17
#define COLOR_BTNTEXT          18
#define COLOR_3DFACE           COLOR_BTNFACE
#define COLOR_3DSHADOW         COLOR_BTNSHADOW
#define COLOR_3DHILIGHT        20

#define SM_CXSCREEN        0
#define SM_CYSCREEN        1
#define SM_CXVSCROLL       2
#define SM_CYHSCROLL       3
#define SM_CYCAPTION       4
#define SM_CXBORDER        5
#define SM_CYBORDER        6
#define SM_CXFRAME         32
#define SM_CYFRAME         33
#define SM_CYMENU          15
#define SM_CXFULLSCREEN    16
#define SM_CYFULLSCREEN    17


/* -------------------------------------------------------------- resources */

#define MAKEINTRESOURCEA(i) ((LPSTR)(ULONG_PTR)((WORD)(i)))
#define MAKEINTRESOURCEW(i) ((LPWSTR)(ULONG_PTR)((WORD)(i)))
#ifdef UNICODE
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#else
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif

#define RT_CURSOR       MAKEINTRESOURCE(1)
#define RT_BITMAP       MAKEINTRESOURCE(2)
#define RT_ICON         MAKEINTRESOURCE(3)
#define RT_MENU         MAKEINTRESOURCE(4)
#define RT_DIALOG       MAKEINTRESOURCE(5)
#define RT_STRING       MAKEINTRESOURCE(6)
#define RT_ACCELERATOR  MAKEINTRESOURCE(9)
#define RT_RCDATA       MAKEINTRESOURCE(10)
#define RT_GROUP_CURSOR MAKEINTRESOURCE(12)
#define RT_GROUP_ICON   MAKEINTRESOURCE(14)

#define LR_DEFAULTCOLOR     0x0000
#define LR_MONOCHROME       0x0001
#define LR_LOADFROMFILE     0x0010
#define LR_LOADTRANSPARENT  0x0020
#define LR_DEFAULTSIZE      0x0040
#define LR_CREATEDIBSECTION 0x2000
#define LR_SHARED           0x8000

#define IMAGE_BITMAP  0
#define IMAGE_ICON    1
#define IMAGE_CURSOR  2


/* ------------------------------------------------------------------- menus */

#define MF_INSERT        0x00000000
#define MF_CHANGE        0x00000080
#define MF_APPEND        0x00000100
#define MF_DELETE        0x00000200
#define MF_REMOVE        0x00001000
#define MF_BYCOMMAND     0x00000000
#define MF_BYPOSITION    0x00000400
#define MF_SEPARATOR     0x00000800
#define MF_ENABLED       0x00000000
#define MF_GRAYED        0x00000001
#define MF_DISABLED      0x00000002
#define MF_UNCHECKED     0x00000000
#define MF_CHECKED       0x00000008
#define MF_USECHECKBITMAPS 0x00000200
#define MF_STRING        0x00000000
#define MF_BITMAP        0x00000004
#define MF_OWNERDRAW     0x00000100
#define MF_POPUP         0x00000010
#define MF_MENUBARBREAK  0x00000020
#define MF_MENUBREAK     0x00000040
#define MF_UNHILITE      0x00000000
#define MF_HILITE        0x00000080
#define MF_HELP          0x00004000
#define MF_RIGHTJUSTIFY  0x00004000
#define MF_SYSMENU       0x00002000

#define TPM_LEFTALIGN    0x0000
#define TPM_RETURNCMD    0x0100


/* -------------------------------------------------------------- messages */

#define WM_NULL            0x0000
#define WM_CREATE          0x0001
#define WM_DESTROY         0x0002
#define WM_MOVE            0x0003
#define WM_SIZE            0x0005
#define WM_ACTIVATE        0x0006
#define WM_SETFOCUS        0x0007
#define WM_KILLFOCUS       0x0008
#define WM_ENABLE          0x000A
#define WM_SETREDRAW       0x000B
#define WM_SETTEXT         0x000C
#define WM_GETTEXT         0x000D
#define WM_PAINT           0x000F
#define WM_CLOSE           0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT            0x0012
#define WM_ERASEBKGND      0x0014
#define WM_SYSCOLORCHANGE  0x0015
#define WM_ACTIVATEAPP     0x001C
#define WM_SETCURSOR       0x0020
#define WM_MOUSEACTIVATE   0x0021
#define WM_GETMINMAXINFO   0x0024
#define WM_PALETTECHANGED  0x0311
#define WM_QUERYNEWPALETTE 0x030F
#define WM_SETFONT         0x0030
#define WM_GETFONT         0x0031
#define WM_NOTIFY          0x004E
#define WM_NCPAINT         0x0085
#define WM_NCACTIVATE      0x0086
#define WM_NCHITTEST       0x0084
#define WM_NCLBUTTONDOWN   0x00A1
#define WM_KEYFIRST        0x0100
#define WM_KEYDOWN         0x0100
#define WM_KEYUP           0x0101
#define WM_CHAR            0x0102
#define WM_DEADCHAR        0x0103
#define WM_SYSKEYDOWN      0x0104
#define WM_SYSKEYUP        0x0105
#define WM_SYSCHAR         0x0106
#define WM_KEYLAST         0x0108
#define WM_INITDIALOG      0x0110
#define WM_COMMAND         0x0111
#define WM_SYSCOMMAND      0x0112
#define WM_TIMER           0x0113
#define WM_HSCROLL         0x0114
#define WM_VSCROLL         0x0115
#define WM_INITMENU        0x0116
#define WM_INITMENUPOPUP   0x0117
#define WM_MENUSELECT      0x011F
#define WM_MENUCHAR        0x0120
#define WM_ENTERIDLE       0x0121
#define WM_CTLCOLORMSGBOX  0x0132
#define WM_CTLCOLOREDIT    0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN     0x0135
#define WM_CTLCOLORDLG     0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC  0x0138
#define WM_MOUSEFIRST      0x0200
#define WM_MOUSEMOVE       0x0200
#define WM_LBUTTONDOWN     0x0201
#define WM_LBUTTONUP       0x0202
#define WM_LBUTTONDBLCLK   0x0203
#define WM_RBUTTONDOWN     0x0204
#define WM_RBUTTONUP       0x0205
#define WM_RBUTTONDBLCLK   0x0206
#define WM_MBUTTONDOWN     0x0207
#define WM_MBUTTONUP       0x0208
#define WM_MOUSEWHEEL      0x020A
#define WM_MOUSELAST       0x020A
#define WM_USER            0x0400

#define SC_MINIMIZE  0xF020
#define SC_MAXIMIZE  0xF030
#define SC_CLOSE     0xF060

#define MK_LBUTTON  0x0001
#define MK_RBUTTON  0x0002
#define MK_SHIFT    0x0004
#define MK_CONTROL  0x0008
#define MK_MBUTTON  0x0010


/* --------------------------------------------------------- window classes */

#define CS_VREDRAW      0x0001
#define CS_HREDRAW      0x0002
#define CS_DBLCLKS      0x0008
#define CS_OWNDC        0x0020
#define CS_CLASSDC      0x0040
#define CS_PARENTDC     0x0080
#define CS_SAVEBITS     0x0800

#define WS_OVERLAPPED   0x00000000
#define WS_POPUP        0x80000000
#define WS_CHILD        0x40000000
#define WS_MINIMIZE     0x20000000
#define WS_VISIBLE      0x10000000
#define WS_DISABLED     0x08000000
#define WS_CLIPSIBLINGS 0x04000000
#define WS_CLIPCHILDREN 0x02000000
#define WS_MAXIMIZE     0x01000000
#define WS_CAPTION      0x00C00000
#define WS_BORDER       0x00800000
#define WS_DLGFRAME     0x00400000
#define WS_VSCROLL      0x00200000
#define WS_HSCROLL      0x00100000
#define WS_SYSMENU      0x00080000
#define WS_THICKFRAME   0x00040000
#define WS_GROUP        0x00020000
#define WS_TABSTOP      0x00010000
#define WS_MINIMIZEBOX  0x00020000
#define WS_MAXIMIZEBOX  0x00010000
#define WS_OVERLAPPEDWINDOW \
    (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | \
     WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_CHILDWINDOW  WS_CHILD

#define WS_EX_DLGMODALFRAME 0x00000001
#define WS_EX_CLIENTEDGE    0x00000200
#define WS_EX_STATICEDGE    0x00020000

#define CW_USEDEFAULT ((int)0x80000000)

#define SW_HIDE            0
#define SW_SHOWNORMAL      1
#define SW_NORMAL          1
#define SW_SHOWMINIMIZED   2
#define SW_SHOWMAXIMIZED   3
#define SW_MAXIMIZE        3
#define SW_SHOWNOACTIVATE  4
#define SW_SHOW            5
#define SW_MINIMIZE        6
#define SW_RESTORE         9

#define GWL_STYLE     (-16)
#define GWL_EXSTYLE   (-20)
#define GWL_WNDPROC   (-4)
#define GWL_HINSTANCE (-6)
#define GWL_ID        (-12)
#define GWL_USERDATA  (-21)
#define GWLP_WNDPROC   GWL_WNDPROC
#define GWLP_HINSTANCE GWL_HINSTANCE
#define GWLP_ID        GWL_ID
#define GWLP_USERDATA  GWL_USERDATA
#define DWLP_MSGRESULT 0
#define DWLP_USER      8

#define GCL_HICON     (-14)
#define GCL_HCURSOR   (-12)
#define GCL_HBRBACKGROUND (-10)
#define GCLP_HICON    GCL_HICON
#define GCLP_HCURSOR  GCL_HCURSOR
#define GCLP_HBRBACKGROUND GCL_HBRBACKGROUND

#define SWP_NOSIZE      0x0001
#define SWP_NOMOVE      0x0002
#define SWP_NOZORDER    0x0004
#define SWP_NOREDRAW    0x0008
#define SWP_NOACTIVATE  0x0010
#define SWP_SHOWWINDOW  0x0040
#define SWP_HIDEWINDOW  0x0080
#define SWP_FRAMECHANGED 0x0020

/* Hit-test results, returned from WM_NCHITTEST. */
#define HTERROR      (-2)
#define HTNOWHERE    0
#define HTCLIENT     1
#define HTCAPTION    2
#define HTSYSMENU    3
#define HTMENU       5
#define HTMINBUTTON  8
#define HTMAXBUTTON  9

#define HWND_DESKTOP ((HWND)0)
#define HWND_TOP     ((HWND)0)
#define HWND_BOTTOM  ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)

#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
#define PM_NOYIELD  0x0002

#define SB_HORZ         0
#define SB_VERT         1
#define SB_CTL          2
#define SB_LINEUP       0
#define SB_LINELEFT     0
#define SB_LINEDOWN     1
#define SB_LINERIGHT    1
#define SB_PAGEUP       2
#define SB_PAGELEFT     2
#define SB_PAGEDOWN     3
#define SB_PAGERIGHT    3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK   5
#define SB_TOP          6
#define SB_BOTTOM       7
#define SB_ENDSCROLL    8

#define SIF_RANGE 0x0001
#define SIF_PAGE  0x0002
#define SIF_POS   0x0004
#define SIF_ALL   (SIF_RANGE | SIF_PAGE | SIF_POS | 0x0010)

typedef struct tagSCROLLINFO {
    UINT cbSize, fMask;
    int  nMin, nMax;
    UINT nPage;
    int  nPos, nTrackPos;
} SCROLLINFO, *PSCROLLINFO, *LPSCROLLINFO;


/* ------------------------------------------------------------ dialogs etc */

/* Exact Windows layout; templates are parsed straight out of the resources. */
#pragma pack(push, 2)
typedef struct tagDLGTEMPLATE {
    DWORD style, dwExtendedStyle;
    WORD  cdit;
    short x, y, cx, cy;
} DLGTEMPLATE, *LPDLGTEMPLATE;
typedef const DLGTEMPLATE * LPCDLGTEMPLATE;

typedef struct tagDLGITEMTEMPLATE {
    DWORD style, dwExtendedStyle;
    short x, y, cx, cy;
    WORD  id;
} DLGITEMTEMPLATE, *LPDLGITEMTEMPLATE;
#pragma pack(pop)

#define DS_ABSALIGN   0x0001
#define DS_SYSMODAL   0x0002
#define DS_LOCALEDIT  0x0020
#define DS_SETFONT    0x0040
#define DS_MODALFRAME 0x0080
#define DS_NOIDLEMSG  0x0100
#define DS_SETFOREGROUND 0x0200
#define DS_CENTER     0x0800

#define IDOK      1
#define IDCANCEL  2
#define IDABORT   3
#define IDRETRY   4
#define IDIGNORE  5
#define IDYES     6
#define IDNO      7
#define IDCLOSE   8
#define IDHELP    9

#define MB_OK               0x00000000
#define MB_OKCANCEL         0x00000001
#define MB_ABORTRETRYIGNORE 0x00000002
#define MB_YESNOCANCEL      0x00000003
#define MB_YESNO            0x00000004
#define MB_RETRYCANCEL      0x00000005
#define MB_ICONHAND         0x00000010
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONERROR        MB_ICONHAND
#define MB_ICONQUESTION     0x00000020
#define MB_ICONEXCLAMATION  0x00000030
#define MB_ICONWARNING      MB_ICONEXCLAMATION
#define MB_ICONASTERISK     0x00000040
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_DEFBUTTON1       0x00000000
#define MB_DEFBUTTON2       0x00000100
#define MB_APPLMODAL        0x00000000
#define MB_SYSTEMMODAL      0x00001000
#define MB_TASKMODAL        0x00002000
#define MB_SETFOREGROUND    0x00010000

#define MB_ERR_INVALID_CHARS 0x00000008

#define IDC_ARROW    MAKEINTRESOURCE(32512)
#define IDC_IBEAM    MAKEINTRESOURCE(32513)
#define IDC_WAIT     MAKEINTRESOURCE(32514)
#define IDC_CROSS    MAKEINTRESOURCE(32515)
#define IDC_SIZEALL  MAKEINTRESOURCE(32646)
#define IDC_NO       MAKEINTRESOURCE(32648)
#define IDC_HAND     MAKEINTRESOURCE(32649)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)

#define CP_ACP       0
#define CP_UTF8      65001

/* Control classes and their messages, as far as the port uses them. */
#define BM_GETCHECK   0x00F0
#define BM_SETCHECK   0x00F1
#define BST_UNCHECKED 0
#define BST_CHECKED   1

#define EM_GETSEL     0x00B0
#define EM_SETSEL     0x00B1
#define EM_LIMITTEXT  0x00C5

#define LB_ADDSTRING  0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_GETCURSEL  0x0188
#define LB_SETCURSEL  0x0186
#define LB_GETCOUNT   0x018B
#define LB_GETTEXT    0x0189
#define LB_ERR        (-1)

#define CB_ADDSTRING  0x0143
#define CB_RESETCONTENT 0x014B
#define CB_GETCURSEL  0x0147
#define CB_SETCURSEL  0x014E
#define CB_ERR        (-1)

#define LBN_SELCHANGE 1
#define LBN_DBLCLK    2
#define CBN_SELCHANGE 1
#define BN_CLICKED    0
#define EN_CHANGE     0x0300

#define LB_GETTEXTLEN 0x018A
#define CB_GETLBTEXT  0x0148
#define CB_GETCOUNT   0x0146
#define CB_DELETESTRING 0x0144

#define BM_GETSTATE   0x00F2
#define BM_SETSTATE   0x00F3
#define BM_CLICK      0x00F5
#define BST_PUSHED    0x0004
#define BST_FOCUS     0x0008

/* The control styles the game's own dialogs use.  Counted across all 51 of
   them: 77 buttons, all of them plain or default pushbuttons; 56 statics, all
   of them text; 6 drop-down lists; 2 list boxes; 2 single-line edits; and one
   vertical scrollbar.  There are no check boxes, radio buttons, group boxes or
   owner-drawn controls anywhere in the game, so there are none here. */
#define BS_PUSHBUTTON    0x00000000L
#define BS_DEFPUSHBUTTON 0x00000001L
#define BS_TYPEMASK      0x0000000FL

#define SS_LEFT          0x00000000L
#define SS_CENTER        0x00000001L
#define SS_RIGHT         0x00000002L
#define SS_SIMPLE        0x0000000BL
#define SS_LEFTNOWORDWRAP 0x0000000CL
#define SS_TYPEMASK      0x0000001FL
#define SS_NOPREFIX      0x00000080L

#define ES_LEFT          0x00000000L
#define ES_CENTER        0x00000001L
#define ES_RIGHT         0x00000002L
#define ES_MULTILINE     0x00000004L
#define ES_AUTOHSCROLL   0x00000080L
#define ES_READONLY      0x00000800L

#define LBS_NOTIFY       0x00000001L
#define LBS_SORT         0x00000002L
#define LBS_HASSTRINGS   0x00000040L

#define CBS_SIMPLE       0x00000001L
#define CBS_DROPDOWN     0x00000002L
#define CBS_DROPDOWNLIST 0x00000003L
#define CBS_TYPEMASK     0x00000003L

#define SBS_HORZ         0x00000000L
#define SBS_VERT         0x00000001L

#define WM_GETTEXTLENGTH 0x000E
#define WM_GETDLGCODE    0x0087
#define WM_NEXTDLGCTL    0x0028
#define DLGC_WANTARROWS  0x0001
#define DLGC_WANTTAB     0x0002
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_BUTTON      0x2000
#define DLGC_DEFPUSHBUTTON 0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_STATIC      0x0100
#define DLGC_HASSETSEL   0x0008

#define HTTRANSPARENT    (-1)

#define MAKELRESULT(low, high) ((LRESULT)MAKELONG(low, high))


/* ------------------------------------------------------------- structures */

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG, *LPMSG;

typedef struct tagPAINTSTRUCT {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore, fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *PPAINTSTRUCT, *LPPAINTSTRUCT;

typedef LRESULT (CALLBACK * WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (CALLBACK * DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (CALLBACK * WNDENUMPROC)(HWND, LPARAM);
typedef void (CALLBACK * TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

typedef struct tagWNDCLASSW {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCWSTR   lpszMenuName, lpszClassName;
} WNDCLASSW, *PWNDCLASSW, *LPWNDCLASSW;

typedef struct tagWNDCLASSA {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName, lpszClassName;
} WNDCLASSA, *PWNDCLASSA, *LPWNDCLASSA;

typedef struct tagWNDCLASSEXW {
    UINT      cbSize, style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCWSTR   lpszMenuName, lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXW, *PWNDCLASSEXW, *LPWNDCLASSEXW;

#ifdef UNICODE
typedef WNDCLASSW   WNDCLASS;
typedef LPWNDCLASSW LPWNDCLASS;
typedef WNDCLASSEXW WNDCLASSEX;
#else
typedef WNDCLASSA   WNDCLASS;
typedef LPWNDCLASSA LPWNDCLASS;
#endif

typedef struct tagACCEL {
    BYTE fVirt;
    WORD key, cmd;
} ACCEL, *LPACCEL;

#define FVIRTKEY  1
#define FSHIFT    0x04
#define FCONTROL  0x08
#define FALT      0x10

typedef struct tagMINMAXINFO {
    POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO, *LPMINMAXINFO;

typedef struct tagCREATESTRUCTW {
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    int       cy, cx, y, x;
    LONG      style;
    LPCWSTR   lpszName, lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCTW, *LPCREATESTRUCTW;

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;


/* --------------------------------------------------- virtual key codes */

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK    0x08
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_MENU    0x12
#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_PRIOR   0x21
#define VK_NEXT    0x22
#define VK_END     0x23
#define VK_HOME    0x24
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_DELETE  0x2E
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F3      0x72
#define VK_F4      0x73
#define VK_F5      0x74
#define VK_F6      0x75
#define VK_F7      0x76
#define VK_F8      0x77
#define VK_F9      0x78
#define VK_F10     0x79
#define VK_F11     0x7A
#define VK_F12     0x7B


/* -------------------------------------------------------------- functions */

/* module and process */
HMODULE GetModuleHandleW(LPCWSTR name);
HMODULE GetModuleHandleA(LPCSTR name);
DWORD   GetTickCount(void);
void    Sleep(DWORD ms);
DWORD   GetLastError(void);
void    SetLastError(DWORD err);
void    ExitProcess(UINT code);
void    GetLocalTime(LPSYSTEMTIME t);
void    GetSystemTime(LPSYSTEMTIME t);

/* resources */
HRSRC   FindResourceW(HMODULE mod, LPCWSTR name, LPCWSTR type);
HRSRC   FindResourceA(HMODULE mod, LPCSTR name, LPCSTR type);
HGLOBAL LoadResource(HMODULE mod, HRSRC res);
LPVOID  LockResource(HGLOBAL res);
BOOL    FreeResource(HGLOBAL res);
DWORD   SizeofResource(HMODULE mod, HRSRC res);
int     LoadStringW(HINSTANCE inst, UINT id, LPWSTR buf, int len);
HBITMAP LoadBitmapW(HINSTANCE inst, LPCWSTR name);
HANDLE  LoadImageW(HINSTANCE inst, LPCWSTR name, UINT type, int cx, int cy, UINT flags);
HCURSOR LoadCursorW(HINSTANCE inst, LPCWSTR name);
HICON   LoadIconW(HINSTANCE inst, LPCWSTR name);

/* string conversion */
int MultiByteToWideChar(UINT page, DWORD flags, LPCSTR src, int srcLen,
                        LPWSTR dst, int dstLen);
int WideCharToMultiByte(UINT page, DWORD flags, LPCWSTR src, int srcLen,
                        LPSTR dst, int dstLen, LPCSTR defaultChar,
                        BOOL * usedDefault);

/* profile (ini) */
UINT GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT def, LPCWSTR file);
DWORD GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR def,
                               LPWSTR buf, DWORD len, LPCWSTR file);
BOOL WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value,
                                LPCWSTR file);

/* windows */
ATOM    RegisterClassW(const WNDCLASSW * cls);
ATOM    RegisterClassExW(const WNDCLASSEXW * cls);
HWND    CreateWindowExW(DWORD exStyle, LPCWSTR cls, LPCWSTR name, DWORD style,
                        int x, int y, int w, int h, HWND parent, HMENU menu,
                        HINSTANCE inst, LPVOID param);
BOOL    DestroyWindow(HWND wnd);
BOOL    ShowWindow(HWND wnd, int cmd);
BOOL    UpdateWindow(HWND wnd);
BOOL    IsWindow(HWND wnd);
BOOL    IsWindowVisible(HWND wnd);
BOOL    EnableWindow(HWND wnd, BOOL enable);
BOOL    MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint);
BOOL    SetWindowPos(HWND wnd, HWND after, int x, int y, int w, int h, UINT flags);
BOOL    GetClientRect(HWND wnd, LPRECT r);
BOOL    GetWindowRect(HWND wnd, LPRECT r);
BOOL    ScreenToClient(HWND wnd, LPPOINT p);
BOOL    ClientToScreen(HWND wnd, LPPOINT p);
BOOL    SetWindowTextW(HWND wnd, LPCWSTR text);
int     GetWindowTextW(HWND wnd, LPWSTR buf, int len);
HWND    SetFocus(HWND wnd);
HWND    GetFocus(void);
HWND    SetActiveWindow(HWND wnd);
HWND    GetActiveWindow(void);
HWND    GetDesktopWindow(void);
HWND    GetParent(HWND wnd);
HWND    SetCapture(HWND wnd);
BOOL    ReleaseCapture(void);
HWND    GetCapture(void);
LONG_PTR GetWindowLongPtrW(HWND wnd, int index);
LONG_PTR SetWindowLongPtrW(HWND wnd, int index, LONG_PTR value);
LONG_PTR GetClassLongPtrW(HWND wnd, int index);
LONG_PTR SetClassLongPtrW(HWND wnd, int index, LONG_PTR value);
BOOL    SetPropW(HWND wnd, LPCWSTR name, HANDLE data);
HANDLE  GetPropW(HWND wnd, LPCWSTR name);
HANDLE  RemovePropW(HWND wnd, LPCWSTR name);
int     GetSystemMetrics(int index);
DWORD   GetSysColor(int index);
HBRUSH  GetSysColorBrush(int index);

/* painting */
HDC     GetDC(HWND wnd);
HDC     GetWindowDC(HWND wnd);
int     ReleaseDC(HWND wnd, HDC dc);
HDC     BeginPaint(HWND wnd, LPPAINTSTRUCT ps);
BOOL    EndPaint(HWND wnd, const PAINTSTRUCT * ps);
BOOL    InvalidateRect(HWND wnd, const RECT * r, BOOL erase);
BOOL    SetRect(LPRECT r, int l, int t, int right, int bottom);
BOOL    SetRectEmpty(LPRECT r);
BOOL    CopyRect(LPRECT dst, const RECT * src);
BOOL    InflateRect(LPRECT r, int dx, int dy);
BOOL    OffsetRect(LPRECT r, int dx, int dy);
BOOL    IsRectEmpty(const RECT * r);
BOOL    PtInRect(const RECT * r, POINT p);
BOOL    IntersectRect(LPRECT dst, const RECT * a, const RECT * b);
BOOL    UnionRect(LPRECT dst, const RECT * a, const RECT * b);
BOOL    EqualRect(const RECT * a, const RECT * b);
int     IntersectClipRect(HDC dc, int l, int t, int r, int b);
int     SelectClipRgn(HDC dc, HRGN rgn);
HRGN    CreateRectRgn(int l, int t, int r, int b);
BOOL    ValidateRect(HWND wnd, const RECT * r);
int     FillRect(HDC dc, const RECT * r, HBRUSH brush);
int     FrameRect(HDC dc, const RECT * r, HBRUSH brush);
BOOL    InvertRect(HDC dc, const RECT * r);
BOOL    DrawFocusRect(HDC dc, const RECT * r);

/* messages */
BOOL    PeekMessageW(LPMSG msg, HWND wnd, UINT first, UINT last, UINT remove);
BOOL    GetMessageW(LPMSG msg, HWND wnd, UINT first, UINT last);
BOOL    TranslateMessage(const MSG * msg);
LRESULT DispatchMessageW(const MSG * msg);
LRESULT DefWindowProcW(HWND wnd, UINT msg, WPARAM w, LPARAM l);
LRESULT SendMessageW(HWND wnd, UINT msg, WPARAM w, LPARAM l);
BOOL    PostMessageW(HWND wnd, UINT msg, WPARAM w, LPARAM l);
void    PostQuitMessage(int code);
LRESULT SendDlgItemMessageW(HWND dlg, int id, UINT msg, WPARAM w, LPARAM l);
UINT_PTR SetTimer(HWND wnd, UINT_PTR id, UINT elapse, TIMERPROC proc);
BOOL    KillTimer(HWND wnd, UINT_PTR id);
BOOL    MessageBeep(UINT type);
int     MessageBoxA(HWND wnd, LPCSTR text, LPCSTR caption, UINT type);
int     MessageBoxW(HWND wnd, LPCWSTR text, LPCWSTR caption, UINT type);

/* dialogs */
INT_PTR DialogBoxParamW(HINSTANCE inst, LPCWSTR tmpl, HWND parent,
                        DLGPROC proc, LPARAM init);
INT_PTR DialogBoxIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATE tmpl, HWND parent,
                                DLGPROC proc, LPARAM init);
HWND    CreateDialogIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATE tmpl,
                                   HWND parent, DLGPROC proc, LPARAM init);
BOOL    EndDialog(HWND dlg, INT_PTR result);
HWND    GetDlgItem(HWND dlg, int id);
int     GetDlgCtrlID(HWND wnd);
UINT    GetDlgItemInt(HWND dlg, int id, BOOL * ok, BOOL sign);
BOOL    SetDlgItemInt(HWND dlg, int id, UINT value, BOOL sign);
BOOL    SetDlgItemTextW(HWND dlg, int id, LPCWSTR text);
UINT    GetDlgItemTextW(HWND dlg, int id, LPWSTR buf, int len);
BOOL    CheckDlgButton(HWND dlg, int id, UINT check);
UINT    IsDlgButtonChecked(HWND dlg, int id);

/* menus */
HMENU   CreateMenu(void);
HMENU   CreatePopupMenu(void);
BOOL    DestroyMenu(HMENU menu);
BOOL    AppendMenuW(HMENU menu, UINT flags, UINT_PTR id, LPCWSTR item);
BOOL    InsertMenuW(HMENU menu, UINT pos, UINT flags, UINT_PTR id, LPCWSTR item);
BOOL    ModifyMenuW(HMENU menu, UINT pos, UINT flags, UINT_PTR id, LPCWSTR item);
BOOL    DeleteMenu(HMENU menu, UINT pos, UINT flags);
BOOL    RemoveMenu(HMENU menu, UINT pos, UINT flags);
DWORD   CheckMenuItem(HMENU menu, UINT id, UINT check);
BOOL    EnableMenuItem(HMENU menu, UINT id, UINT enable);
HMENU   GetMenu(HWND wnd);
BOOL    SetMenu(HWND wnd, HMENU menu);
HMENU   GetSubMenu(HMENU menu, int pos);
int     GetMenuItemCount(HMENU menu);
UINT    GetMenuItemID(HMENU menu, int pos);
BOOL    DrawMenuBar(HWND wnd);
BOOL    TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                       HWND wnd, const RECT * r);
HACCEL  CreateAcceleratorTableW(LPACCEL accel, int count);
BOOL    DestroyAcceleratorTable(HACCEL accel);
int     TranslateAcceleratorW(HWND wnd, HACCEL accel, LPMSG msg);

/* cursors */
HCURSOR SetCursor(HCURSOR cursor);
BOOL    GetCursorPos(LPPOINT p);
BOOL    SetCursorPos(int x, int y);
int     ShowCursor(BOOL show);

/* scrolling */
BOOL    SetScrollInfo(HWND wnd, int bar, const SCROLLINFO * info, BOOL redraw);
BOOL    GetScrollInfo(HWND wnd, int bar, LPSCROLLINFO info);
int     SetScrollPos(HWND wnd, int bar, int pos, BOOL redraw);
int     GetScrollPos(HWND wnd, int bar);
BOOL    ShowScrollBar(HWND wnd, int bar, BOOL show);

/* gdi objects */
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);
BOOL    DeleteObject(HGDIOBJ obj);
HGDIOBJ GetStockObject(int index);
HBRUSH  CreateSolidBrush(COLORREF colour);
HBRUSH  CreatePatternBrush(HBITMAP bitmap);
HPEN    CreatePen(int style, int width, COLORREF colour);
HFONT   CreateFontW(int height, int width, int escapement, int orientation,
                    int weight, DWORD italic, DWORD underline, DWORD strikeout,
                    DWORD charset, DWORD outPrecision, DWORD clipPrecision,
                    DWORD quality, DWORD pitchAndFamily, LPCWSTR face);
HFONT   CreateFontA(int height, int width, int escapement, int orientation,
                    int weight, DWORD italic, DWORD underline, DWORD strikeout,
                    DWORD charset, DWORD outPrecision, DWORD clipPrecision,
                    DWORD quality, DWORD pitchAndFamily, LPCSTR face);
HFONT   CreateFontIndirectW(const LOGFONTW * lf);
HFONT   CreateFontIndirectA(const LOGFONTA * lf);
typedef int (CALLBACK * FONTENUMPROCA)(const LOGFONTA *, const TEXTMETRICA *,
                                      DWORD, LPARAM);
int     EnumFontsA(HDC dc, LPCSTR face, FONTENUMPROCA proc, LPARAM param);
HICON   CreateIconFromResourceEx(PBYTE bits, DWORD size, BOOL icon, DWORD ver,
                                 int cx, int cy, UINT flags);
int     SaveDC(HDC dc);
BOOL    RestoreDC(HDC dc, int state);
HDC     CreateCompatibleDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
BOOL    DeleteDC(HDC dc);
int     GetObjectW(HGDIOBJ obj, int size, LPVOID buf);

/* gdi drawing */
COLORREF SetTextColor(HDC dc, COLORREF colour);
COLORREF GetTextColor(HDC dc);
COLORREF SetBkColor(HDC dc, COLORREF colour);
COLORREF GetBkColor(HDC dc);
int     SetBkMode(HDC dc, int mode);
int     GetBkMode(HDC dc);
UINT    SetTextAlign(HDC dc, UINT align);
UINT    GetTextAlign(HDC dc);
int     SetROP2(HDC dc, int mode);
int     SetStretchBltMode(HDC dc, int mode);
BOOL    MoveToEx(HDC dc, int x, int y, LPPOINT old);
BOOL    LineTo(HDC dc, int x, int y);
BOOL    Rectangle(HDC dc, int l, int t, int r, int b);
BOOL    Ellipse(HDC dc, int l, int t, int r, int b);
BOOL    Polygon(HDC dc, const POINT * points, int count);
BOOL    Polyline(HDC dc, const POINT * points, int count);
BOOL    PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop);
COLORREF SetPixel(HDC dc, int x, int y, COLORREF colour);
COLORREF GetPixel(HDC dc, int x, int y);
BOOL    TextOutW(HDC dc, int x, int y, LPCWSTR text, int count);
BOOL    TextOutA(HDC dc, int x, int y, LPCSTR text, int count);
int     DrawTextW(HDC dc, LPCWSTR text, int count, LPRECT r, UINT format);
int     DrawTextA(HDC dc, LPCSTR text, int count, LPRECT r, UINT format);
BOOL    GetTextExtentPoint32A(HDC dc, LPCSTR text, int count, LPSIZE size);
BOOL    GetTextMetricsA(HDC dc, LPTEXTMETRICA tm);
BOOL    GetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size);
BOOL    GetTextMetricsW(HDC dc, LPTEXTMETRICW tm);

/* blitting */
BOOL    BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
               DWORD rop);
BOOL    StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
                   int sw, int sh, DWORD rop);
int     SetDIBitsToDevice(HDC dc, int x, int y, DWORD w, DWORD h, int sx, int sy,
                          UINT startScan, UINT scanLines, LPCVOID bits,
                          const BITMAPINFO * info, UINT colourUse);
int     StretchDIBits(HDC dc, int x, int y, int w, int h, int sx, int sy,
                      int sw, int sh, LPCVOID bits, const BITMAPINFO * info,
                      UINT colourUse, DWORD rop);
HBITMAP CreateDIBitmap(HDC dc, const BITMAPINFOHEADER * header, DWORD init,
                       LPCVOID bits, const BITMAPINFO * info, UINT use);
int     GetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, LPVOID bits,
                  LPBITMAPINFO info, UINT use);

/* palettes */
HPALETTE CreatePalette(const LOGPALETTE * palette);
HPALETTE SelectPalette(HDC dc, HPALETTE palette, BOOL forceBackground);
UINT     RealizePalette(HDC dc);
UINT     GetNearestPaletteIndex(HPALETTE palette, COLORREF colour);
COLORREF GetNearestColor(HDC dc, COLORREF colour);
UINT     GetPaletteEntries(HPALETTE palette, UINT start, UINT count,
                           LPPALETTEENTRY entries);
UINT     SetPaletteEntries(HPALETTE palette, UINT start, UINT count,
                           const PALETTEENTRY * entries);
BOOL     ResizePalette(HPALETTE palette, UINT count);
UINT     GetSystemPaletteEntries(HDC dc, UINT start, UINT count,
                                 LPPALETTEENTRY entries);


/* ------------------------------------------------- the host's own requests */

/* Device capabilities.  The port asks for the colour depth and whether the
   display can do palettes, and decides how to draw from the answers. */
#define DRIVERVERSION 0
#define TECHNOLOGY    2
#define HORZSIZE      4
#define VERTSIZE      6
#define HORZRES       8
#define VERTRES       10
#define BITSPIXEL     12
#define PLANES        14
#define NUMBRUSHES    16
#define NUMPENS       18
#define NUMCOLORS     24
#define RASTERCAPS    38
#define SIZEPALETTE   104
#define NUMRESERVED   106
#define COLORRES      108

#define RC_BITBLT     1
#define RC_BITMAP64   8
#define RC_DI_BITMAP  128
#define RC_PALETTE    256
#define RC_DIBTODEV   512
#define RC_STRETCHDIB 8192

int GetDeviceCaps(HDC dc, int index);

typedef struct _RASTERIZER_STATUS {
    short nSize, wFlags, nLanguageID;
} RASTERIZER_STATUS, *LPRASTERIZER_STATUS;
#define TT_AVAILABLE 0x0001
#define TT_ENABLED   0x0002
BOOL GetRasterizerCaps(LPRASTERIZER_STATUS status, UINT size);

BOOL AnimatePalette(HPALETTE palette, UINT start, UINT count,
                    const PALETTEENTRY * entries);
BOOL UpdateColors(HDC dc);

/* Window metrics the host uses to work out its own frame. */
#define SM_CXDLGFRAME  7
#define SM_CYDLGFRAME  8
#define SM_SWAPBUTTON  23
BOOL AdjustWindowRectEx(LPRECT r, DWORD style, BOOL menu, DWORD exStyle);
BOOL AdjustWindowRect(LPRECT r, DWORD style, BOOL menu);

#define WA_INACTIVE    0
#define WA_ACTIVE      1
#define WA_CLICKACTIVE 2

#define WM_NCDESTROY 0x0082
#define WM_APP       0x8000

#define DT_NOPREFIX     0x00000800
#define DT_END_ELLIPSIS 0x00008000

#define IDC_SIZENS   MAKEINTRESOURCE(32645)
#define IDC_SIZEWE   MAKEINTRESOURCE(32644)

/* Modules.  There is nothing to load here, but the port checks for optional
   entry points before using them, so the calls have to answer. */
HMODULE LoadLibraryW(LPCWSTR name);
HMODULE LoadLibraryA(LPCSTR name);
BOOL    FreeLibrary(HMODULE module);
void *  GetProcAddress(HMODULE module, LPCSTR name);
DWORD   GetModuleFileNameW(HMODULE module, LPWSTR buf, DWORD len);
DWORD   GetModuleFileNameA(HMODULE module, LPSTR buf, DWORD len);

/* Files and directories, for the save-game paths. */
#define INVALID_FILE_ATTRIBUTES     ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY     0x00000001
#define FILE_ATTRIBUTE_DIRECTORY    0x00000010
#define FILE_ATTRIBUTE_ARCHIVE      0x00000020
#define FILE_ATTRIBUTE_NORMAL       0x00000080
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetFileAttributesA(LPCSTR path);
UINT  GetWindowsDirectoryW(LPWSTR buf, UINT len);
UINT  GetSystemDirectoryW(LPWSTR buf, UINT len);
UINT  GetTempPathW(DWORD len, LPWSTR buf);
DWORD GetProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR buf,
                        DWORD len);
UINT  GetProfileIntA(LPCSTR section, LPCSTR key, INT def);

/* Memory.  Answered from what the runtime actually has rather than invented:
   the port only looks at it to decide how much it may cache. */
typedef struct _MEMORYSTATUSEX {
    DWORD     dwLength;
    DWORD     dwMemoryLoad;
    ULONGLONG ullTotalPhys, ullAvailPhys;
    ULONGLONG ullTotalPageFile, ullAvailPageFile;
    ULONGLONG ullTotalVirtual, ullAvailVirtual;
    ULONGLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status);

/* Odds and ends. */
int  MulDiv(int number, int numerator, int denominator);
int  lstrcmpiW(LPCWSTR a, LPCWSTR b);
int  lstrcmpiA(LPCSTR a, LPCSTR b);
int  lstrlenW(LPCWSTR s);
LPWSTR lstrcpyW(LPWSTR dst, LPCWSTR src);

#define HELP_CONTEXT  0x0001
#define HELP_QUIT     0x0002
#define HELP_INDEX    0x0003
#define HELP_CONTENTS 0x0003
#define HELP_HELPONHELP 0x0004
#define HELP_KEY      0x0101
BOOL WinHelpW(HWND wnd, LPCWSTR help, UINT command, ULONG_PTR data);


/* Implemented in the shim; declared here so the host can reach them. */
LRESULT CallWindowProcW(WNDPROC proc, HWND wnd, UINT msg, WPARAM w, LPARAM l);
BOOL    EnumChildWindows(HWND wnd, WNDENUMPROC proc, LPARAM param);
HWND    GetTopWindow(HWND wnd);
int     GetWindowTextLengthW(HWND wnd);
BOOL    IsIconic(HWND wnd);
BOOL    IsDialogMessageW(HWND dlg, LPMSG msg);
BOOL    SetScrollRange(HWND wnd, int bar, int min, int max, BOOL redraw);
BOOL    DestroyCursor(HCURSOR cursor);
BOOL    DestroyIcon(HICON icon);
BOOL    ClipCursor(const RECT * r);
SHORT   GetAsyncKeyState(int key);
SHORT   GetKeyState(int key);
void    FatalAppExitA(UINT action, LPCSTR text);
BOOL    MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count);
BOOL    WriteProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value);

/* ------------------------------------------------------- ANSI/W aliasing */

#ifdef UNICODE
#define GetModuleHandle     GetModuleHandleW
#define FindResource        FindResourceW
#define LoadString          LoadStringW
#define LoadBitmap          LoadBitmapW
#define LoadImage           LoadImageW
#define LoadCursor          LoadCursorW
#define LoadIcon            LoadIconW
#define RegisterClass       RegisterClassW
#define RegisterClassEx     RegisterClassExW
#define CreateWindowExW_    CreateWindowExW
#define SetWindowText       SetWindowTextW
#define GetWindowText       GetWindowTextW
#define GetWindowLongPtr    GetWindowLongPtrW
#define SetWindowLongPtr    SetWindowLongPtrW
#define GetClassLongPtr     GetClassLongPtrW
#define SetClassLongPtr     SetClassLongPtrW
#define SetProp             SetPropW
#define GetProp             GetPropW
#define RemoveProp          RemovePropW
#define PeekMessage         PeekMessageW
#define GetMessage          GetMessageW
#define DispatchMessage     DispatchMessageW
#define DefWindowProc       DefWindowProcW
#define SendMessage         SendMessageW
#define PostMessage         PostMessageW
#define SendDlgItemMessage  SendDlgItemMessageW
#define MessageBox          MessageBoxW
#define DialogBoxParam      DialogBoxParamW
#define DialogBoxIndirectParam DialogBoxIndirectParamW
#define CreateDialogIndirectParam CreateDialogIndirectParamW
#define SetDlgItemText      SetDlgItemTextW
#define GetDlgItemText      GetDlgItemTextW
#define AppendMenu          AppendMenuW
#define InsertMenu          InsertMenuW
#define ModifyMenu          ModifyMenuW
#define CreateAcceleratorTable CreateAcceleratorTableW
#define TranslateAccelerator TranslateAcceleratorW
#define CreateFont          CreateFontW
#define CreateFontIndirect  CreateFontIndirectW
#define TextOut             TextOutW
#define DrawText            DrawTextW
#define GetTextExtentPoint32 GetTextExtentPoint32W
#define GetTextMetrics      GetTextMetricsW
#define GetObject           GetObjectW
#define GetPrivateProfileInt GetPrivateProfileIntW
#define GetPrivateProfileString GetPrivateProfileStringW
#define WritePrivateProfileString WritePrivateProfileStringW
#define LoadLibrary         LoadLibraryW
#define GetModuleFileName   GetModuleFileNameW
#define GetFileAttributes   GetFileAttributesW
#define GetWindowsDirectory GetWindowsDirectoryW
#define GetSystemDirectory  GetSystemDirectoryW
#define GetTempPath         GetTempPathW
#define lstrcmpi            lstrcmpiW
#define lstrlen             lstrlenW
#define lstrcpy             lstrcpyW
#define WinHelp             WinHelpW
#define CallWindowProc      CallWindowProcW
#define GetWindowTextLength GetWindowTextLengthW
#define IsDialogMessage     IsDialogMessageW
#define CreateWindow(c, n, s, x, y, w, h, p, m, i, l) \
    CreateWindowExW(0, c, n, s, x, y, w, h, p, m, i, l)
#define DialogBox(inst, tmpl, parent, proc) \
    DialogBoxParamW(inst, tmpl, parent, proc, 0)
#endif


#ifdef __cplusplus
}   /* extern "C" */
#endif

#ifdef __cplusplus
/* Narrow paths into the wide entry points.
 *
 * std::filesystem::path::value_type is wchar_t on Windows and char here, so the
 * port's own calls - written correctly for Windows - hand a narrow path to a W
 * function.  Absorbed here rather than patched upstream: the difference is the
 * platform's, not the port's, and a patch would have to be carried for ever.
 */
extern "C++" {

inline UINT GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT def,
                                  const char * file) {
    (void)section; (void)key; (void)file;
    return (UINT)def;
}

inline DWORD GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR def,
                                      LPWSTR buf, DWORD len, const char * file) {
    (void)file;
    return GetPrivateProfileStringW(section, key, def, buf, len, (LPCWSTR)nullptr);
}

inline BOOL WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key,
                                       LPCWSTR value, const char * file) {
    (void)section; (void)key; (void)value; (void)file;
    return TRUE;
}

DWORD GetFileAttributesNarrow(const char * path);
inline DWORD GetFileAttributesW(const char * path) {
    return GetFileAttributesNarrow(path);
}

inline BOOL WinHelpW(HWND wnd, const char * help, UINT command,
                     ULONG_PTR data) {
    (void)wnd; (void)help; (void)command; (void)data;
    return FALSE;      /* there is no help viewer in a browser */
}

}   /* extern "C++" */
#endif
