// Menus, and the dialogs that are not yet dialogs.
//
// The menu bar is real: it is built from the port's own AppendMenu calls, drawn
// into the frame, and a click on a top-level item drops its popup and posts
// WM_COMMAND.  Dialogs are the next piece of work; what is here answers their
// API well enough that the game runs past them rather than into them.

#include "win32_internal.h"

#include <algorithm>
#include <cstdio>

namespace shim {

namespace {

const int kItemPadding = 8;

// Which top-level item is open, if any, and where its popup sits.
HMENU g_openMenu = nullptr;
HWND  g_openOwner = nullptr;
RECT  g_openRect = {0, 0, 0, 0};

std::string itemLabel(const MenuItem & item) {
    // An ampersand is the accelerator marker and is not drawn.
    std::string out;
    for (size_t i = 0; i < item.text.size(); i++) {
        if (item.text[i] == '&' && i + 1 < item.text.size()) continue;
        out += item.text[i];
    }
    return out;
}

}   // namespace


extern "C" HMENU CreateMenu(void) {
    const uintptr_t h = state().allocate();
    state().menus[h] = Menu{};
    return (HMENU)h;
}

extern "C" HMENU CreatePopupMenu(void) { return CreateMenu(); }

extern "C" BOOL DestroyMenu(HMENU handle) {
    if (!menu(handle)) return FALSE;
    state().menus.erase((uintptr_t)handle);
    if (g_openMenu == handle) g_openMenu = nullptr;
    return TRUE;
}

extern "C" BOOL AppendMenuW(HMENU handle, UINT flags, UINT_PTR id,
                            LPCWSTR text) {
    Menu * m = menu(handle);
    if (!m) return FALSE;
    MenuItem item{};
    item.flags = flags;
    item.id = id;
    if (flags & MF_POPUP) {
        // With MF_POPUP the id is the submenu's handle, not a command.
        item.submenu = (HMENU)id;
        item.id = 0;
    }
    if (!(flags & MF_SEPARATOR) && text) item.text = toUtf8(text);
    m->items.push_back(std::move(item));
    return TRUE;
}

extern "C" BOOL InsertMenuW(HMENU handle, UINT pos, UINT flags, UINT_PTR id,
                            LPCWSTR text) {
    Menu * m = menu(handle);
    if (!m) return FALSE;
    if (!AppendMenuW(handle, flags, id, text)) return FALSE;
    if (flags & MF_BYPOSITION) {
        MenuItem item = m->items.back();
        m->items.pop_back();
        if (pos > m->items.size()) pos = (UINT)m->items.size();
        m->items.insert(m->items.begin() + pos, std::move(item));
    }
    return TRUE;
}

extern "C" BOOL ModifyMenuW(HMENU handle, UINT pos, UINT flags, UINT_PTR id,
                            LPCWSTR text) {
    Menu * m = menu(handle);
    if (!m) return FALSE;
    for (size_t i = 0; i < m->items.size(); i++) {
        const bool match = (flags & MF_BYPOSITION) ? (i == pos)
                                                   : (m->items[i].id == pos);
        if (!match) continue;
        m->items[i].flags = flags & ~MF_BYPOSITION;
        m->items[i].id = id;
        if (text) m->items[i].text = toUtf8(text);
        return TRUE;
    }
    return FALSE;
}

static bool removeItem(HMENU handle, UINT pos, UINT flags) {
    Menu * m = menu(handle);
    if (!m) return false;
    for (size_t i = 0; i < m->items.size(); i++) {
        const bool match = (flags & MF_BYPOSITION) ? (i == pos)
                                                   : (m->items[i].id == pos);
        if (!match) continue;
        m->items.erase(m->items.begin() + i);
        return true;
    }
    return false;
}

extern "C" BOOL DeleteMenu(HMENU h, UINT pos, UINT flags) {
    return removeItem(h, pos, flags);
}
extern "C" BOOL RemoveMenu(HMENU h, UINT pos, UINT flags) {
    return removeItem(h, pos, flags);
}

static MenuItem * findItem(HMENU handle, UINT id, UINT flags) {
    Menu * m = menu(handle);
    if (!m) return nullptr;
    for (size_t i = 0; i < m->items.size(); i++) {
        if ((flags & MF_BYPOSITION) ? (i == id) : (m->items[i].id == id))
            return &m->items[i];
        // Commands live in the submenus, so a by-command search descends.
        if (!(flags & MF_BYPOSITION) && m->items[i].submenu)
            if (MenuItem * found = findItem(m->items[i].submenu, id, flags))
                return found;
    }
    return nullptr;
}

extern "C" DWORD CheckMenuItem(HMENU handle, UINT id, UINT check) {
    MenuItem * item = findItem(handle, id, check & MF_BYPOSITION);
    if (!item) return (DWORD)-1;
    const DWORD was = item->flags & MF_CHECKED;
    if (check & MF_CHECKED) item->flags |= MF_CHECKED;
    else item->flags &= ~MF_CHECKED;
    return was;
}

extern "C" BOOL EnableMenuItem(HMENU handle, UINT id, UINT enable) {
    MenuItem * item = findItem(handle, id, enable & MF_BYPOSITION);
    if (!item) return FALSE;
    if (enable & (MF_GRAYED | MF_DISABLED)) item->flags |= MF_GRAYED;
    else item->flags &= ~MF_GRAYED;
    return TRUE;
}

extern "C" HMENU GetMenu(HWND hwnd) {
    Window * w = window(hwnd);
    return w ? w->menu : nullptr;
}

extern "C" BOOL SetMenu(HWND hwnd, HMENU handle) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    w->menu = handle;
    invalidate(*w, nullptr, true);
    return TRUE;
}

extern "C" HMENU GetSubMenu(HMENU handle, int pos) {
    Menu * m = menu(handle);
    if (!m || pos < 0 || pos >= (int)m->items.size()) return nullptr;
    return m->items[pos].submenu;
}

extern "C" int GetMenuItemCount(HMENU handle) {
    Menu * m = menu(handle);
    return m ? (int)m->items.size() : -1;
}

extern "C" UINT GetMenuItemID(HMENU handle, int pos) {
    Menu * m = menu(handle);
    if (!m || pos < 0 || pos >= (int)m->items.size()) return (UINT)-1;
    return (UINT)m->items[pos].id;
}

extern "C" BOOL DrawMenuBar(HWND hwnd) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    invalidate(*w, nullptr, true);
    return TRUE;
}

extern "C" HACCEL CreateAcceleratorTableW(LPACCEL, int) {
    return (HACCEL)(void *)state().allocate();
}
extern "C" BOOL DestroyAcceleratorTable(HACCEL) { return TRUE; }
extern "C" int TranslateAcceleratorW(HWND, HACCEL, LPMSG) { return 0; }


/* ------------------------------------------------------------ menu drawing */

void drawMenuBar(DeviceContext & d, Window & w) {
    Menu * m = menu(w.menu);
    if (!m) return;

    d.font = GetStockObject(SYSTEM_FONT);
    d.bkMode = TRANSPARENT;
    d.textAlign = TA_LEFT | TA_TOP;

    int x = kItemPadding;
    for (MenuItem & item : m->items) {
        const std::string label = itemLabel(item);
        const SIZE extent = measureText(d, label.c_str(), (int)label.size());

        // Recorded so a click can be resolved without laying the bar out again.
        item.bounds = RECT{x - kItemPadding / 2, 0,
                           x + extent.cx + kItemPadding / 2, kCaptionBarHeight};

        const bool open = item.submenu && item.submenu == g_openMenu;
        if (open) {
            fillRect(d, item.bounds, fromColorref(GetSysColor(COLOR_HIGHLIGHT)));
            d.textColour = GetSysColor(COLOR_HIGHLIGHTTEXT);
        } else {
            d.textColour = (item.flags & MF_GRAYED)
                         ? GetSysColor(COLOR_GRAYTEXT)
                         : GetSysColor(COLOR_MENUTEXT);
        }
        drawText(d, x, 4, label.c_str(), (int)label.size());
        x += extent.cx + kItemPadding * 2;
    }

    if (g_openMenu && g_openOwner == nullptr) return;
    if (g_openMenu) drawMenuPopup(d, w);
}

// The dropped popup, drawn over the client area.  Its own coordinates are the
// menu bar's, which is what the caller's DC is already set up for.
void drawMenuPopup(DeviceContext & d, Window & w) {
    Menu * m = menu(g_openMenu);
    if (!m || m->items.empty()) return;

    const int lineHeight = kCaptionBarHeight;
    int widest = 0;
    for (MenuItem & item : m->items) {
        const std::string label = itemLabel(item);
        const SIZE extent = measureText(d, label.c_str(), (int)label.size());
        widest = std::max<int>(widest, extent.cx);
    }
    const int width = widest + kItemPadding * 6;
    const int height = (int)m->items.size() * lineHeight + 4;

    g_openRect = RECT{g_openRect.left, kCaptionBarHeight,
                      g_openRect.left + width, kCaptionBarHeight + height};

    fillRect(d, g_openRect, fromColorref(GetSysColor(COLOR_MENU)));
    drawLine(d, g_openRect.left, g_openRect.top, g_openRect.right - 1,
             g_openRect.top, fromColorref(RGB(0, 0, 0)));
    drawLine(d, g_openRect.left, g_openRect.bottom - 1, g_openRect.right - 1,
             g_openRect.bottom - 1, fromColorref(RGB(0, 0, 0)));
    drawLine(d, g_openRect.left, g_openRect.top, g_openRect.left,
             g_openRect.bottom - 1, fromColorref(RGB(0, 0, 0)));
    drawLine(d, g_openRect.right - 1, g_openRect.top, g_openRect.right - 1,
             g_openRect.bottom - 1, fromColorref(RGB(0, 0, 0)));

    int y = g_openRect.top + 2;
    for (MenuItem & item : m->items) {
        item.bounds = RECT{g_openRect.left, y, g_openRect.right, y + lineHeight};
        if (item.flags & MF_SEPARATOR) {
            drawLine(d, g_openRect.left + 2, y + lineHeight / 2,
                     g_openRect.right - 3, y + lineHeight / 2,
                     fromColorref(GetSysColor(COLOR_BTNSHADOW)));
        } else {
            d.textColour = (item.flags & MF_GRAYED)
                         ? GetSysColor(COLOR_GRAYTEXT)
                         : GetSysColor(COLOR_MENUTEXT);
            const std::string label = itemLabel(item);
            drawText(d, g_openRect.left + kItemPadding * 3, y + 3,
                     label.c_str(), (int)label.size());
            if (item.flags & MF_CHECKED)
                drawText(d, g_openRect.left + 4, y + 3, "*", 1);
        }
        y += lineHeight;
    }
}

// Returns true when the click belonged to the menu, so the window's own handler
// does not also see it.
bool menuBarClick(Window & w, POINT client) {
    Menu * bar = menu(w.menu);
    if (!bar) return false;

    // The bar sits above the client area, so a click in it arrives with a
    // negative y once it has been converted into client coordinates.
    const int barTop = -kCaptionBarHeight;

    if (client.y >= barTop && client.y < 0) {
        const int x = client.x;
        for (MenuItem & item : bar->items) {
            if (x < item.bounds.left || x >= item.bounds.right) continue;
            if (!item.submenu) {
                if (item.id) post((HWND)nullptr, WM_COMMAND, item.id, 0);
                return true;
            }
            g_openMenu = (g_openMenu == item.submenu) ? nullptr : item.submenu;
            g_openOwner = nullptr;
            g_openRect.left = item.bounds.left;
            invalidate(w, nullptr, true);
            return true;
        }
        g_openMenu = nullptr;
        invalidate(w, nullptr, true);
        return true;
    }

    if (!g_openMenu) return false;

    // A click in the dropped popup, whose coordinates were recorded relative to
    // the menu bar.
    Menu * open = menu(g_openMenu);
    const POINT inBar{client.x, client.y - barTop};
    if (open) {
        for (MenuItem & item : open->items) {
            if (inBar.x < item.bounds.left || inBar.x >= item.bounds.right) continue;
            if (inBar.y < item.bounds.top || inBar.y >= item.bounds.bottom) continue;
            g_openMenu = nullptr;
            invalidate(w, nullptr, true);
            if (!(item.flags & (MF_SEPARATOR | MF_GRAYED)) && item.id) {
                HWND owner = nullptr;
                for (auto & entry : state().windows)
                    if (&entry.second == &w) owner = (HWND)entry.first;
                post(owner, WM_COMMAND, item.id, 0);
            }
            return true;
        }
    }

    // Anywhere else dismisses it.
    g_openMenu = nullptr;
    invalidate(w, nullptr, true);
    return true;
}

extern "C" BOOL TrackPopupMenu(HMENU handle, UINT, int x, int, int, HWND hwnd,
                               const RECT *) {
    Window * w = window(hwnd);
    if (!w) return FALSE;
    g_openMenu = handle;
    g_openOwner = hwnd;
    g_openRect.left = x - w->rect.left;
    invalidate(*w, nullptr, true);
    return TRUE;
}


/* ------------------------------------------------- dialogs, for the moment */

extern "C" int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type) {
    // Reported rather than drawn, until there is a dialog manager to draw it
    // in.  Answering the default button is what lets the game continue instead
    // of waiting for a click that can never arrive.
    fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "",
            text ? text : "");
    if (type & MB_YESNO) return IDYES;
    if (type & MB_OKCANCEL) return IDOK;
    if (type & MB_RETRYCANCEL) return IDRETRY;
    return IDOK;
}

extern "C" int MessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    const std::string t = toUtf8(text);
    const std::string c = toUtf8(caption);
    return MessageBoxA(hwnd, t.c_str(), c.c_str(), type);
}

extern "C" void FatalAppExitA(UINT, LPCSTR text) {
    fprintf(stderr, "[FatalAppExit] %s\n", text ? text : "");
    ExitProcess(1);
}

extern "C" HWND GetDlgItem(HWND dlg, int id) {
    Window * w = window(dlg);
    if (!w) return nullptr;
    for (HWND child : w->children) {
        Window * c = window(child);
        if (c && c->id == id) return child;
        if (HWND deeper = GetDlgItem(child, id)) return deeper;
    }
    return nullptr;
}

extern "C" int GetDlgCtrlID(HWND hwnd) {
    Window * w = window(hwnd);
    return w ? w->id : 0;
}

extern "C" BOOL EndDialog(HWND dlg, INT_PTR result) {
    Window * w = window(dlg);
    if (!w) return FALSE;
    w->dialogEnded = true;
    w->dialogResult = result;
    w->visible = false;
    return TRUE;
}

extern "C" BOOL SetDlgItemTextW(HWND dlg, int id, LPCWSTR text) {
    HWND item = GetDlgItem(dlg, id);
    return item ? SetWindowTextW(item, text) : FALSE;
}

extern "C" UINT GetDlgItemTextW(HWND dlg, int id, LPWSTR buf, int len) {
    HWND item = GetDlgItem(dlg, id);
    return item ? (UINT)GetWindowTextW(item, buf, len) : 0;
}

extern "C" BOOL SetDlgItemInt(HWND dlg, int id, UINT value, BOOL sign) {
    wchar_t buf[32];
    swprintf(buf, 32, sign ? L"%d" : L"%u", value);
    return SetDlgItemTextW(dlg, id, buf);
}

extern "C" UINT GetDlgItemInt(HWND dlg, int id, BOOL * ok, BOOL) {
    wchar_t buf[32] = {};
    GetDlgItemTextW(dlg, id, buf, 32);
    const long v = wcstol(buf, nullptr, 10);
    if (ok) *ok = TRUE;
    return (UINT)v;
}

extern "C" BOOL CheckDlgButton(HWND dlg, int id, UINT check) {
    HWND item = GetDlgItem(dlg, id);
    Window * w = window(item);
    if (!w) return FALSE;
    w->checked = check != 0;
    return TRUE;
}

extern "C" UINT IsDlgButtonChecked(HWND dlg, int id) {
    Window * w = window(GetDlgItem(dlg, id));
    return w && w->checked ? BST_CHECKED : BST_UNCHECKED;
}

extern "C" BOOL IsDialogMessageW(HWND, LPMSG) { return FALSE; }

}   // namespace shim
