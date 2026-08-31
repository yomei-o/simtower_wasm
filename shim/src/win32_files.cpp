// Saving and loading a tower.
//
// The port asks for a path with GetOpenFileName - for both Open and Save As,
// which is what the Win16 original did - and then writes the file with an
// ordinary ofstream.  There is no common dialog in a browser and no file system
// worth the name, so both halves are supplied here: a chooser drawn out of the
// same control classes as every other dialog, over an IndexedDB-backed
// directory that survives a reload.
//
// The port never learns any of this.  It is handed `/saves/NAME.TDT` and writes
// to it; the sync back to IndexedDB happens a moment later, from the message
// pump, because the write has not happened yet when the dialog returns.

#include "win32_internal.h"

#include <commdlg.h>

#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace shim {

namespace {

const char * kSaveDirectory = "/saves";

bool g_mounted = false;
bool g_needsSync = false;
double g_syncAt = 0;

// EM_JS rather than EM_ASM: a comma inside the JavaScript would split an
// EM_ASM macro argument, and these have several.
EM_JS(void, jsMountSaves, (), {
    try {
        if (!FS.analyzePath('/saves').exists) FS.mkdir('/saves');
        // IDBFS asserts rather than throws where there is no IndexedDB - the
        // command-line harness, a private window with storage refused - and an
        // assert aborts the whole runtime.  Ask first; without it the
        // directory is still there, it just does not outlive the session.
        if (typeof indexedDB === 'undefined') {
            Module.simtowerSavesReady = 2;
            return;
        }
        FS.mount(IDBFS, {}, '/saves');
        Module.simtowerSavesReady = 0;
        FS.syncfs(true, function(err) {
            // Ready either way: a first run has nothing to restore, and a
            // browser that refuses IndexedDB still gets a working session.
            if (err) Module.simtowerSavesError = String(err);
            Module.simtowerSavesReady = err ? 2 : 1;
        });
    } catch (e) {
        Module.simtowerSavesError = String(e);
        Module.simtowerSavesReady = 2;
    }
});

EM_JS(int, jsSavesReady, (), {
    return Module.simtowerSavesReady || 0;
});

EM_JS(void, jsSyncSaves, (), {
    try {
        FS.syncfs(false, function() {});
    } catch (e) {}
});

std::vector<std::string> savedTowers() {
    std::vector<std::string> names;
    DIR * directory = opendir(kSaveDirectory);
    if (!directory) return names;
    while (struct dirent * entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        // Only towers, whatever case the name was given in.
        if (name.size() < 5) continue;
        std::string tail = name.substr(name.size() - 4);
        for (char & c : tail) c = (char)toupper((unsigned char)c);
        if (tail != ".TDT") continue;
        names.push_back(name);
    }
    closedir(directory);
    std::sort(names.begin(), names.end());
    return names;
}


/* ------------------------------------------------------------ the chooser */

struct ChooserContext {
    std::vector<std::string> names;
    std::string chosen;
    bool saving = false;
};

ChooserContext * g_chooser = nullptr;

INT_PTR CALLBACK chooserProc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message != WM_COMMAND || !g_chooser) return FALSE;
    const int id = LOWORD(wp);
    const int code = HIWORD(wp);

    if (id == 100 && code == LBN_SELCHANGE) {
        // Picking from the list fills the name in, so Save As over an existing
        // tower is one click and a confirmation.
        const int at = (int)SendDlgItemMessageW(dialog, 100, LB_GETCURSEL, 0, 0);
        if (at >= 0 && at < (int)g_chooser->names.size()) {
            const std::wstring wide = fromUtf8(g_chooser->names[at].c_str());
            SetDlgItemTextW(dialog, 101, wide.c_str());
        }
        return TRUE;
    }
    if (id == 100 && code == LBN_DBLCLK) {
        const int at = (int)SendDlgItemMessageW(dialog, 100, LB_GETCURSEL, 0, 0);
        if (at >= 0 && at < (int)g_chooser->names.size()) {
            g_chooser->chosen = g_chooser->names[at];
            EndDialog(dialog, IDOK);
        }
        return TRUE;
    }
    if (id == IDOK) {
        wchar_t typed[128] = {};
        GetDlgItemTextW(dialog, 101, typed, 128);
        g_chooser->chosen = toUtf8(typed);
        if (g_chooser->chosen.empty()) return TRUE;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (id == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Returns the chosen bare name, or empty if the player cancelled.
std::string runChooser(HWND owner, const std::wstring & title, bool saving,
                       const std::string & suggested) {
    ChooserContext context;
    context.names = savedTowers();
    context.saving = saving;

    const int width = 300;
    const int height = 220;
    State & s = state();
    const int x = std::max(0, (s.screen.width - width) / 2);
    const int y = std::max(0, (s.screen.height - height) / 3);

    HWND dialog = CreateWindowExW(
        0, L"#32770", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_DLGFRAME | WS_VISIBLE,
        x, y, width, height, owner, nullptr, nullptr, nullptr);
    Window * w = window(dialog);
    if (!w) return std::string();
    w->isDialog = true;
    w->dialogProc = chooserProc;

    const int client = width - 2;
    CreateWindowExW(0, L"STATIC",
                    saving ? L"Save the tower as:" : L"Open which tower?",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    10, 8, client - 20, 16, dialog, nullptr, nullptr, nullptr);

    HWND list = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY,
                                10, 28, client - 20, 96,
                                dialog, nullptr, nullptr, nullptr);
    if (Window * c = window(list)) {
        c->id = 100;
        for (const std::string & name : context.names)
            c->listItems.push_back(fromUtf8(name.c_str()));
    }

    HWND edit = CreateWindowExW(0, L"EDIT",
                                fromUtf8(suggested.c_str()).c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT,
                                10, 132, client - 20, 22,
                                dialog, nullptr, nullptr, nullptr);
    if (Window * c = window(edit)) {
        c->id = 101;
        c->caret = (int)c->text.size();
        c->enabled = saving;          // Open takes its answer from the list
    }

    HWND ok = CreateWindowExW(0, L"BUTTON", saving ? L"Save" : L"Open",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                  BS_DEFPUSHBUTTON,
                              client - 160, 164, 68, 24,
                              dialog, nullptr, nullptr, nullptr);
    if (Window * c = window(ok)) c->id = IDOK;
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                      BS_PUSHBUTTON,
                                  client - 84, 164, 68, 24,
                                  dialog, nullptr, nullptr, nullptr);
    if (Window * c = window(cancel)) c->id = IDCANCEL;

    g_chooser = &context;
    invalidate(*w, nullptr, true);
    dialogSetInitialFocus(dialog);
    const INT_PTR answer = runModalLoop(dialog);
    g_chooser = nullptr;

    if (answer != IDOK) return std::string();
    return context.chosen;
}

// SimTower's own rule, applied before the port sees the name: a DOS basename
// and the extension it expects.
std::string tidyName(std::string name) {
    std::string out;
    for (char c : name) {
        if (c == '\\' || c == '/' || c == ':') continue;
        out += c;
    }
    // Strip any extension the player typed, then put the right one back.
    const size_t dot = out.find_last_of('.');
    if (dot != std::string::npos) out.resize(dot);
    if (out.size() > 8) out.resize(8);
    if (out.empty()) out = "TOWER";
    for (char & c : out) c = (char)toupper((unsigned char)c);
    return out + ".TDT";
}

}   // namespace


void mountSaves() {
    if (g_mounted) return;
    g_mounted = true;
    jsMountSaves();
    // The port measures the DOS basename from the last backslash, and there is
    // no backslash in a POSIX path - so "/saves/TOWER.TDT" measures twelve
    // characters and is refused as "not a valid filename".  It is handed a
    // bare name instead, and the process works in the saves directory.
    chdir(kSaveDirectory);
    // The restore is asynchronous and startup has to wait for it, or the first
    // Open sees an empty directory that is about to fill up.
    for (int spin = 0; spin < 200 && jsSavesReady() == 0; spin++)
        emscripten_sleep(20);
}

// Called from the message pump.  The port writes the file after the dialog has
// returned, so the sync has to be a moment later rather than immediately.
void syncSaves() {
    if (!g_needsSync) return;
    if (hostNow() < g_syncAt) return;
    g_needsSync = false;
    jsSyncSaves();
}


extern "C" BOOL GetOpenFileNameW(LPOPENFILENAMEW ofn) {
    if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile) return FALSE;
    mountSaves();

    // The port uses this call for Save As as well, which is what the original
    // did; its title is the only thing that says which.
    const std::wstring title = ofn->lpstrTitle ? ofn->lpstrTitle : L"";
    const bool saving = title.find(L"Save") != std::wstring::npos ||
                        (ofn->Flags & OFN_OVERWRITEPROMPT) != 0;

    std::string suggested = toUtf8(ofn->lpstrFile);
    const size_t slash = suggested.find_last_of("\\/");
    if (slash != std::string::npos) suggested = suggested.substr(slash + 1);
    if (suggested.empty() && saving) suggested = "TOWER.TDT";

    const std::string chosen =
        runChooser(ofn->hwndOwner, title.empty()
                       ? (saving ? L"Save Tower" : L"Open Tower")
                       : title,
                   saving, suggested);
    if (chosen.empty()) return FALSE;

    const std::string name = saving ? tidyName(chosen) : chosen;
    // A bare name, resolved against the saves directory the process sits in.
    const std::string path = name;

    if (!saving) {
        struct stat info;
        if (stat(path.c_str(), &info) != 0) return FALSE;
    } else {
        // The write lands shortly after this returns; carry it to IndexedDB
        // once it has.
        g_needsSync = true;
        g_syncAt = hostNow() + 1500.0;
    }

    const std::wstring widePath = fromUtf8(path.c_str());
    if (widePath.size() + 1 > ofn->nMaxFile) return FALSE;
    memcpy(ofn->lpstrFile, widePath.c_str(),
           (widePath.size() + 1) * sizeof(wchar_t));
    ofn->nFileOffset = 0;
    ofn->nFileExtension = (WORD)(widePath.size() - 4);
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle) {
        const std::wstring wideName = fromUtf8(name.c_str());
        if (wideName.size() + 1 <= ofn->nMaxFileTitle)
            memcpy(ofn->lpstrFileTitle, wideName.c_str(),
                   (wideName.size() + 1) * sizeof(wchar_t));
    }
    return TRUE;
}

extern "C" BOOL GetSaveFileNameW(LPOPENFILENAMEW ofn) {
    return GetOpenFileNameW(ofn);
}

extern "C" DWORD CommDlgExtendedError(void) { return 0; }

}   // namespace shim
