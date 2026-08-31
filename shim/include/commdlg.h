// GetOpenFileName / GetSaveFileName, which reach the browser as a file picker
// and a download rather than as a modal dialog.
#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

#define OFN_READONLY          0x00000001
#define OFN_OVERWRITEPROMPT   0x00000002
#define OFN_HIDEREADONLY      0x00000004
#define OFN_PATHMUSTEXIST     0x00000800
#define OFN_FILEMUSTEXIST     0x00001000
#define OFN_EXPLORER          0x00080000

typedef struct tagOFNW {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR   lpstrFilter;
    LPWSTR    lpstrCustomFilter;
    DWORD     nMaxCustFilter, nFilterIndex;
    LPWSTR    lpstrFile;
    DWORD     nMaxFile;
    LPWSTR    lpstrFileTitle;
    DWORD     nMaxFileTitle;
    LPCWSTR   lpstrInitialDir, lpstrTitle;
    DWORD     Flags;
    WORD      nFileOffset, nFileExtension;
    LPCWSTR   lpstrDefExt;
    LPARAM    lCustData;
    void *    lpfnHook;
    LPCWSTR   lpTemplateName;
} OPENFILENAMEW, *LPOPENFILENAMEW;

BOOL GetOpenFileNameW(LPOPENFILENAMEW ofn);
BOOL GetSaveFileNameW(LPOPENFILENAMEW ofn);
DWORD CommDlgExtendedError(void);

#ifdef UNICODE
typedef OPENFILENAMEW OPENFILENAME;
typedef LPOPENFILENAMEW LPOPENFILENAME;
#define GetOpenFileName GetOpenFileNameW
#define GetSaveFileName GetSaveFileNameW
#endif

#ifdef __cplusplus
}
#endif
