// The built-in control classes, and what makes a dialog usable.
//
// A dialog template names classes nobody registers - BUTTON, STATIC, EDIT,
// LISTBOX, COMBOBOX, SCROLLBAR - and Windows answers for them.  Without that
// the templates still build a tree of windows, and every one of them is a grey
// rectangle that cannot be clicked: which is exactly where this port stopped,
// at a startup chooser whose three buttons were three blank blocks.
//
// The scope here was counted rather than assumed.  Across the game's 51
// dialogs there are 77 buttons - every one a plain or default pushbutton - 56
// statics, all text, 6 drop-down lists, 2 list boxes, 2 single-line edits and
// one vertical scrollbar.  No check boxes, radio buttons, group boxes or
// owner-draw anywhere, so none are written here.
//
// The look is Windows 3.1's, because the game's own art was drawn to sit next
// to it: a black outline, a white top-left highlight and a grey bottom-right
// shadow, and the whole thing inverted while a button is held down.

#include "win32_internal.h"

#include <algorithm>
#include <cstring>

namespace shim {

namespace {

const int kListItemPadding = 2;
const int kComboArrowWidth = 16;
const int kScrollArrowSize = 16;

HWND handleOf(const Window & w) {
    for (auto & entry : state().windows)
        if (&entry.second == &w) return (HWND)entry.first;
    return nullptr;
}

int textHeight(HDC hdc) {
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    return tm.tmHeight > 0 ? tm.tmHeight : 12;
}

void frameRect(HDC hdc, RECT r, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    RECT top{r.left, r.top, r.right, r.top + 1};
    RECT bottom{r.left, r.bottom - 1, r.right, r.bottom};
    RECT left{r.left, r.top, r.left + 1, r.bottom};
    RECT right{r.right - 1, r.top, r.right, r.bottom};
    FillRect(hdc, &top, brush);
    FillRect(hdc, &bottom, brush);
    FillRect(hdc, &left, brush);
    FillRect(hdc, &right, brush);
    DeleteObject(brush);
}

// The two-tone edge every Windows 3.1 control is built out of.  `raised` puts
// the light on the top left, `sunken` puts it on the bottom right, and that one
// difference is the whole difference between a button and a text box.
void edge(HDC hdc, RECT r, bool raised) {
    const COLORREF light = raised ? GetSysColor(COLOR_3DHILIGHT)
                                  : GetSysColor(COLOR_BTNSHADOW);
    const COLORREF dark  = raised ? GetSysColor(COLOR_BTNSHADOW)
                                  : GetSysColor(COLOR_3DHILIGHT);
    HBRUSH lightBrush = CreateSolidBrush(light);
    HBRUSH darkBrush = CreateSolidBrush(dark);
    RECT top{r.left, r.top, r.right - 1, r.top + 1};
    RECT left{r.left, r.top, r.left + 1, r.bottom - 1};
    RECT bottom{r.left, r.bottom - 1, r.right, r.bottom};
    RECT right{r.right - 1, r.top, r.right, r.bottom};
    FillRect(hdc, &top, lightBrush);
    FillRect(hdc, &left, lightBrush);
    FillRect(hdc, &bottom, darkBrush);
    FillRect(hdc, &right, darkBrush);
    DeleteObject(lightBrush);
    DeleteObject(darkBrush);
}

// Ask the parent how this control should be coloured, exactly as Windows does.
// The port answers these: it selects its own font into the DC and hands back
// NULL_BRUSH so its own dialog art shows through instead of a grey fill.
HBRUSH askParent(Window & w, HWND hwnd, HDC hdc, UINT message, COLORREF back) {
    SetBkColor(hdc, back);
    SetBkMode(hdc, OPAQUE);
    SetTextColor(hdc, GetSysColor(w.enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
    if (w.font) SelectObject(hdc, w.font);
    if (!w.parent) return nullptr;
    return (HBRUSH)send(w.parent, message, (WPARAM)hdc, (LPARAM)hwnd);
}

void fillBackground(HDC hdc, const RECT & r, HBRUSH brush, COLORREF fallback) {
    if (brush) {
        RECT copy = r;
        FillRect(hdc, &copy, brush);
        return;
    }
    HBRUSH solid = CreateSolidBrush(fallback);
    RECT copy = r;
    FillRect(hdc, &copy, solid);
    DeleteObject(solid);
}

void notifyParent(Window & w, HWND hwnd, int code) {
    if (!w.parent) return;
    send(w.parent, WM_COMMAND, MAKEWPARAM(w.id, code), (LPARAM)hwnd);
}


/* ---------------------------------------------------------------- button */

void paintButton(Window & w, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc) return;
    const RECT client = clientRect(w);
    HBRUSH brush = askParent(w, hwnd, hdc, WM_CTLCOLORBTN,
                             GetSysColor(COLOR_BTNFACE));

    RECT face = client;
    if ((w.style & BS_TYPEMASK) == BS_DEFPUSHBUTTON) {
        // The default button wears a second outline, which is how Windows 3.1
        // says "this is what Enter does".
        frameRect(hdc, face, RGB(0, 0, 0));
        InflateRect(&face, -1, -1);
    }
    fillBackground(hdc, face, brush, GetSysColor(COLOR_BTNFACE));
    frameRect(hdc, face, RGB(0, 0, 0));
    RECT inner = face;
    InflateRect(&inner, -1, -1);
    edge(hdc, inner, !w.pressed);

    RECT text = inner;
    InflateRect(&text, -2, -2);
    if (w.pressed) OffsetRect(&text, 1, 1);
    if (!w.enabled) SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, w.text.c_str(), (int)w.text.size(), &text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (state().focus == hwnd && !w.text.empty()) {
        RECT focus = inner;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(hdc, &focus);
    }
    EndPaint(hwnd, &ps);
}

void clickButton(Window & w, HWND hwnd) {
    notifyParent(w, hwnd, BN_CLICKED);
}

LRESULT buttonProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            paintButton(w, hwnd);
            return 0;
        case WM_GETDLGCODE:
            return (w.style & BS_TYPEMASK) == BS_DEFPUSHBUTTON
                 ? (DLGC_BUTTON | DLGC_DEFPUSHBUTTON)
                 : (DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON);
        case WM_LBUTTONDOWN:
            if (!w.enabled) return 0;
            SetFocus(hwnd);
            SetCapture(hwnd);
            w.pressed = true;
            invalidate(w, nullptr, true);
            return 0;
        case WM_MOUSEMOVE: {
            if (!w.pressed || state().capture != hwnd) return 0;
            // The press only stands while the pointer is still on the button,
            // so sliding off and letting go cancels it - as it does in Windows.
            const RECT client = clientRect(w);
            POINT p{(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
            const bool inside = PtInRect(&client, p) != 0;
            if (inside != w.pressed) {
                w.pressed = inside;
                invalidate(w, nullptr, true);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (state().capture != hwnd) return 0;
            ReleaseCapture();
            const bool was = w.pressed;
            w.pressed = false;
            invalidate(w, nullptr, true);
            if (was) clickButton(w, hwnd);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_SPACE && w.enabled) {
                w.pressed = true;
                invalidate(w, nullptr, true);
            }
            return 0;
        case WM_KEYUP:
            if (wp == VK_SPACE && w.pressed) {
                w.pressed = false;
                invalidate(w, nullptr, true);
                clickButton(w, hwnd);
            }
            return 0;
        case BM_CLICK:
            clickButton(w, hwnd);
            return 0;
        case BM_GETSTATE:
            return (w.pressed ? BST_PUSHED : 0) |
                   (state().focus == hwnd ? BST_FOCUS : 0) |
                   (w.checked ? BST_CHECKED : BST_UNCHECKED);
        case BM_SETSTATE:
            w.pressed = wp != 0;
            invalidate(w, nullptr, true);
            return 0;
        case BM_GETCHECK:
            return w.checked ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK:
            w.checked = wp != BST_UNCHECKED;
            invalidate(w, nullptr, true);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}


/* ---------------------------------------------------------------- static */

LRESULT staticProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;
            RECT client = clientRect(w);
            HBRUSH brush = askParent(w, hwnd, hdc, WM_CTLCOLORSTATIC,
                                     GetSysColor(COLOR_BTNFACE));
            fillBackground(hdc, client, brush, GetSysColor(COLOR_BTNFACE));
            UINT format = DT_WORDBREAK;
            switch (w.style & SS_TYPEMASK) {
                case SS_CENTER: format |= DT_CENTER; break;
                case SS_RIGHT:  format |= DT_RIGHT; break;
                case SS_SIMPLE:
                case SS_LEFTNOWORDWRAP:
                    format = DT_LEFT | DT_SINGLELINE | DT_VCENTER;
                    break;
                default:        format |= DT_LEFT; break;
            }
            if (w.style & SS_NOPREFIX) format |= DT_NOPREFIX;
            SetBkMode(hdc, brush ? TRANSPARENT : OPAQUE);
            DrawTextW(hdc, w.text.c_str(), (int)w.text.size(), &client, format);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_STATIC;
        case WM_NCHITTEST:
            // Static text does not take clicks; they belong to whatever is
            // underneath it, which on a dialog is the dialog.
            return HTTRANSPARENT;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}


/* ------------------------------------------------------------------ edit */

LRESULT editProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;
            RECT client = clientRect(w);
            HBRUSH brush = askParent(w, hwnd, hdc, WM_CTLCOLOREDIT,
                                     GetSysColor(COLOR_WINDOW));
            fillBackground(hdc, client, brush, GetSysColor(COLOR_WINDOW));
            edge(hdc, client, false);
            RECT text = client;
            InflateRect(&text, -3, -2);
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, w.text.c_str(), (int)w.text.size(), &text,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            if (state().focus == hwnd) {
                // The caret, drawn rather than blinked: a blinking one would
                // need a repaint every half second for no gain in a game that
                // asks for a name and nothing else.
                SIZE extent{};
                const int upto = std::min<int>(w.caret, (int)w.text.size());
                GetTextExtentPoint32W(hdc, w.text.c_str(), upto, &extent);
                const int height = textHeight(hdc);
                RECT caret{text.left + extent.cx,
                           (client.top + client.bottom - height) / 2,
                           text.left + extent.cx + 1,
                           (client.top + client.bottom + height) / 2};
                HBRUSH ink = CreateSolidBrush(GetSysColor(COLOR_WINDOWTEXT));
                FillRect(hdc, &caret, ink);
                DeleteObject(ink);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_HASSETSEL;
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            invalidate(w, nullptr, true);
            return 0;
        case WM_CHAR: {
            if (w.style & ES_READONLY) return 0;
            const wchar_t ch = (wchar_t)wp;
            if (ch == L'\b') {
                if (w.caret > 0) {
                    w.text.erase(w.caret - 1, 1);
                    w.caret--;
                }
            } else if (ch >= 32) {
                w.caret = std::min<int>(w.caret, (int)w.text.size());
                w.text.insert(w.text.begin() + w.caret, ch);
                w.caret++;
            } else {
                return 0;
            }
            invalidate(w, nullptr, true);
            notifyParent(w, hwnd, EN_CHANGE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_LEFT && w.caret > 0) w.caret--;
            else if (wp == VK_RIGHT && w.caret < (int)w.text.size()) w.caret++;
            else if (wp == VK_HOME) w.caret = 0;
            else if (wp == VK_END) w.caret = (int)w.text.size();
            else if (wp == VK_DELETE && w.caret < (int)w.text.size()) {
                w.text.erase(w.caret, 1);
                notifyParent(w, hwnd, EN_CHANGE);
            } else return 0;
            invalidate(w, nullptr, true);
            return 0;
        case EM_SETSEL:
            // Only the caret is modelled, so a selection is where it ends.
            w.caret = std::min<int>((int)lp < 0 ? (int)w.text.size() : (int)lp,
                                    (int)w.text.size());
            invalidate(w, nullptr, true);
            return 1;
        case EM_GETSEL:
            return MAKELRESULT(w.caret, w.caret);
        case EM_LIMITTEXT:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}


/* --------------------------------------------------------------- list box */

int listRowHeight(Window & w, HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    if (w.font) SelectObject(hdc, w.font);
    const int height = textHeight(hdc) + kListItemPadding;
    ReleaseDC(hwnd, hdc);
    return height;
}

void listEnsureVisible(Window & w, HWND hwnd) {
    if (w.listSelection < 0) return;
    const RECT client = clientRect(w);
    const int row = listRowHeight(w, hwnd);
    const int rows = std::max<int>(1, (int)(client.bottom - client.top - 4) / row);
    if (w.listSelection < w.listTop) w.listTop = w.listSelection;
    else if (w.listSelection >= w.listTop + rows)
        w.listTop = w.listSelection - rows + 1;
}

void paintList(Window & w, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc) return;
    RECT client = clientRect(w);
    HBRUSH brush = askParent(w, hwnd, hdc, WM_CTLCOLORLISTBOX,
                             GetSysColor(COLOR_WINDOW));
    fillBackground(hdc, client, brush, GetSysColor(COLOR_WINDOW));
    frameRect(hdc, client, RGB(0, 0, 0));

    const int row = textHeight(hdc) + kListItemPadding;
    RECT item{client.left + 1, client.top + 1, client.right - 1,
              client.top + 1 + row};
    for (int i = w.listTop; i < (int)w.listItems.size(); i++) {
        if (item.top >= client.bottom - 1) break;
        const bool selected = i == w.listSelection;
        if (selected) {
            HBRUSH highlight = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            RECT copy = item;
            copy.bottom = std::min<LONG>(copy.bottom, client.bottom - 1);
            FillRect(hdc, &copy, highlight);
            DeleteObject(highlight);
            SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        }
        SetBkMode(hdc, TRANSPARENT);
        RECT text = item;
        text.left += 2;
        DrawTextW(hdc, w.listItems[i].c_str(), (int)w.listItems[i].size(),
                  &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        OffsetRect(&item, 0, row);
    }
    EndPaint(hwnd, &ps);
}

// Which item a click landed on, or -1.
int listItemAt(Window & w, HWND hwnd, POINT client) {
    const RECT r = clientRect(w);
    if (!PtInRect(&r, client)) return -1;
    const int row = listRowHeight(w, hwnd);
    const int index = w.listTop + (client.y - r.top - 1) / row;
    return index >= 0 && index < (int)w.listItems.size() ? index : -1;
}

LRESULT listProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            paintList(w, hwnd);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(hwnd);
            POINT p{(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
            const int index = listItemAt(w, hwnd, p);
            if (index < 0) return 0;
            w.listSelection = index;
            invalidate(w, nullptr, true);
            if (w.style & LBS_NOTIFY)
                notifyParent(w, hwnd, msg == WM_LBUTTONDBLCLK ? LBN_DBLCLK
                                                              : LBN_SELCHANGE);
            return 0;
        }
        case WM_KEYDOWN: {
            const int count = (int)w.listItems.size();
            int next = w.listSelection;
            if (wp == VK_UP) next--;
            else if (wp == VK_DOWN) next++;
            else if (wp == VK_HOME) next = 0;
            else if (wp == VK_END) next = count - 1;
            else return 0;
            next = std::max(0, std::min(next, count - 1));
            if (next == w.listSelection) return 0;
            w.listSelection = next;
            listEnsureVisible(w, hwnd);
            invalidate(w, nullptr, true);
            if (w.style & LBS_NOTIFY) notifyParent(w, hwnd, LBN_SELCHANGE);
            return 0;
        }
        case LB_ADDSTRING: {
            std::wstring item = lp ? (const wchar_t *)lp : L"";
            if (w.style & LBS_SORT) {
                auto at = std::lower_bound(w.listItems.begin(),
                                           w.listItems.end(), item);
                const int index = (int)(at - w.listItems.begin());
                w.listItems.insert(at, item);
                invalidate(w, nullptr, true);
                return index;
            }
            w.listItems.push_back(item);
            invalidate(w, nullptr, true);
            return (int)w.listItems.size() - 1;
        }
        case LB_INSERTSTRING: {
            const int index = (int)wp > (int)w.listItems.size() || (int)wp < 0
                            ? (int)w.listItems.size() : (int)wp;
            w.listItems.insert(w.listItems.begin() + index,
                               lp ? (const wchar_t *)lp : L"");
            invalidate(w, nullptr, true);
            return index;
        }
        case LB_DELETESTRING:
            if ((int)wp < 0 || (int)wp >= (int)w.listItems.size()) return LB_ERR;
            w.listItems.erase(w.listItems.begin() + (int)wp);
            if (w.listSelection >= (int)w.listItems.size())
                w.listSelection = (int)w.listItems.size() - 1;
            invalidate(w, nullptr, true);
            return (int)w.listItems.size();
        case LB_RESETCONTENT:
            w.listItems.clear();
            w.listSelection = -1;
            w.listTop = 0;
            invalidate(w, nullptr, true);
            return 0;
        case LB_GETCOUNT:
            return (int)w.listItems.size();
        case LB_GETCURSEL:
            return w.listSelection < 0 ? LB_ERR : w.listSelection;
        case LB_SETCURSEL:
            if ((int)wp < -1 || (int)wp >= (int)w.listItems.size()) {
                w.listSelection = -1;
                return LB_ERR;
            }
            w.listSelection = (int)wp;
            listEnsureVisible(w, hwnd);
            invalidate(w, nullptr, true);
            return w.listSelection;
        case LB_GETTEXT: {
            if ((int)wp < 0 || (int)wp >= (int)w.listItems.size()) return LB_ERR;
            const std::wstring & item = w.listItems[(int)wp];
            if (lp) {
                wchar_t * out = (wchar_t *)lp;
                memcpy(out, item.c_str(), (item.size() + 1) * sizeof(wchar_t));
            }
            return (int)item.size();
        }
        case LB_GETTEXTLEN:
            if ((int)wp < 0 || (int)wp >= (int)w.listItems.size()) return LB_ERR;
            return (int)w.listItems[(int)wp].size();
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}


/* -------------------------------------------------------------- combo box

   Only CBS_DROPDOWNLIST is here, because that is the only kind the game uses.
   Dropping it creates a real list box under it and takes the mouse, which is
   what Windows does too; the alternative - drawing a list outside the control's
   own rectangle - would put pixels somewhere no hit test would ever look.   */

void closeDropList(Window & w) {
    if (!w.dropList) return;
    HWND list = w.dropList;
    w.dropList = nullptr;
    if (Window * l = window(list)) l->dropOwner = nullptr;
    if (state().capture == list) ReleaseCapture();
    DestroyWindow(list);
}

void openDropList(Window & w, HWND hwnd) {
    if (w.dropList) { closeDropList(w); return; }
    const int row = listRowHeight(w, hwnd);
    const int visible = std::max(1, std::min((int)w.listItems.size(), 8));
    const int height = visible * row + 2;

    // Positioned in the parent's client coordinates, which is where a child
    // window is placed, and directly under the closed control.
    const POINT origin = w.parent && window(w.parent)
                       ? clientOrigin(*window(w.parent)) : POINT{0, 0};
    const int x = w.rect.left - origin.x;
    const int y = w.rect.bottom - origin.y;
    HWND list = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | LBS_NOTIFY,
                                x, y, w.rect.right - w.rect.left, height,
                                w.parent, nullptr, nullptr, nullptr);
    Window * l = window(list);
    if (!l) return;
    l->listItems = w.listItems;
    l->listSelection = w.listSelection;
    l->font = w.font;
    l->dropOwner = hwnd;
    l->id = w.id;
    listEnsureVisible(*l, list);
    w.dropList = list;
    SetCapture(list);
    invalidate(*l, nullptr, true);
}

LRESULT comboProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;
            RECT client = clientRect(w);
            HBRUSH brush = askParent(w, hwnd, hdc, WM_CTLCOLORLISTBOX,
                                     GetSysColor(COLOR_WINDOW));
            fillBackground(hdc, client, brush, GetSysColor(COLOR_WINDOW));
            frameRect(hdc, client, RGB(0, 0, 0));

            RECT arrow{client.right - kComboArrowWidth - 1, client.top + 1,
                       client.right - 1, client.bottom - 1};
            HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            FillRect(hdc, &arrow, face);
            DeleteObject(face);
            edge(hdc, arrow, w.dropList == nullptr);
            // A triangle, drawn as shortening rows: three lines of code instead
            // of a polygon fill, and it reads the same at this size.
            HBRUSH ink = CreateSolidBrush(GetSysColor(COLOR_BTNTEXT));
            const int cx = (arrow.left + arrow.right) / 2;
            const int cy = (arrow.top + arrow.bottom) / 2 - 1;
            for (int i = 0; i < 4; i++) {
                RECT line{cx - 3 + i, cy + i, cx + 4 - i, cy + i + 1};
                FillRect(hdc, &line, ink);
            }
            DeleteObject(ink);

            if (w.listSelection >= 0 &&
                w.listSelection < (int)w.listItems.size()) {
                RECT text{client.left + 3, client.top + 1,
                          arrow.left - 2, client.bottom - 1};
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                SetBkMode(hdc, TRANSPARENT);
                const std::wstring & item = w.listItems[w.listSelection];
                DrawTextW(hdc, item.c_str(), (int)item.size(), &text,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            openDropList(w, hwnd);
            return 0;
        case WM_KEYDOWN: {
            const int count = (int)w.listItems.size();
            int next = w.listSelection;
            if (wp == VK_UP) next--;
            else if (wp == VK_DOWN) next++;
            else return 0;
            next = std::max(0, std::min(next, count - 1));
            if (next == w.listSelection) return 0;
            w.listSelection = next;
            invalidate(w, nullptr, true);
            notifyParent(w, hwnd, CBN_SELCHANGE);
            return 0;
        }
        case WM_DESTROY:
            closeDropList(w);
            return 0;
        case CB_ADDSTRING:
            w.listItems.push_back(lp ? (const wchar_t *)lp : L"");
            invalidate(w, nullptr, true);
            return (int)w.listItems.size() - 1;
        case CB_DELETESTRING:
            if ((int)wp < 0 || (int)wp >= (int)w.listItems.size()) return CB_ERR;
            w.listItems.erase(w.listItems.begin() + (int)wp);
            if (w.listSelection >= (int)w.listItems.size())
                w.listSelection = (int)w.listItems.size() - 1;
            invalidate(w, nullptr, true);
            return (int)w.listItems.size();
        case CB_RESETCONTENT:
            w.listItems.clear();
            w.listSelection = -1;
            closeDropList(w);
            invalidate(w, nullptr, true);
            return 0;
        case CB_GETCOUNT:
            return (int)w.listItems.size();
        case CB_GETCURSEL:
            return w.listSelection < 0 ? CB_ERR : w.listSelection;
        case CB_SETCURSEL:
            if ((int)wp < -1 || (int)wp >= (int)w.listItems.size()) {
                w.listSelection = -1;
                invalidate(w, nullptr, true);
                return CB_ERR;
            }
            w.listSelection = (int)wp;
            invalidate(w, nullptr, true);
            return w.listSelection;
        case CB_GETLBTEXT: {
            if ((int)wp < 0 || (int)wp >= (int)w.listItems.size()) return CB_ERR;
            const std::wstring & item = w.listItems[(int)wp];
            if (lp) memcpy((wchar_t *)lp, item.c_str(),
                           (item.size() + 1) * sizeof(wchar_t));
            return (int)item.size();
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// A dropped list reports back to the combo box that owns it rather than to the
// dialog, so the dialog only ever hears CBN_SELCHANGE from the control it knows.
bool routeDropListClick(Window & list, HWND hwnd, UINT msg, LPARAM lp) {
    if (!list.dropOwner) return false;
    Window * combo = window(list.dropOwner);
    if (!combo) return false;
    POINT p{(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
    const RECT client = clientRect(list);
    if (msg == WM_LBUTTONDOWN && !PtInRect(&client, p)) {
        // A click outside a dropped list closes it, and goes no further.
        HWND owner = list.dropOwner;
        closeDropList(*combo);
        (void)owner;
        return true;
    }
    if (msg != WM_LBUTTONUP) return false;
    const int index = listItemAt(list, hwnd, p);
    HWND ownerHandle = list.dropOwner;
    closeDropList(*combo);
    if (index >= 0) {
        Window * again = window(ownerHandle);
        if (again) {
            again->listSelection = index;
            invalidate(*again, nullptr, true);
            notifyParent(*again, ownerHandle, CBN_SELCHANGE);
        }
    }
    return true;
}


/* ------------------------------------------------------------- scroll bar */

LRESULT scrollProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}   // namespace

// One scroll bar, drawn one way.  A window's own WS_VSCROLL bar lives in the
// frame and a SCROLLBAR control is a window of its own, but they are the same
// picture and the same arithmetic, so they are the same code.
void paintScrollBar(DeviceContext & d, RECT r, const SCROLLINFO & info,
                    bool vertical) {
    const int length = vertical ? r.bottom - r.top : r.right - r.left;
    if (length <= 0) return;
    const int arrow = std::min(kScrollArrowSize, length / 2);

    // The shaft is a half-tone of white on grey, which is what Windows 3.1
    // drew and what makes the thumb read as solid against it.
    const uint32_t pale = fromColorref(GetSysColor(COLOR_3DHILIGHT));
    const uint32_t grey = fromColorref(GetSysColor(COLOR_SCROLLBAR));
    for (int y = r.top; y < r.bottom; y++)
        for (int x = r.left; x < r.right; x++)
            fillRect(d, RECT{x, y, x + 1, y + 1}, ((x + y) & 1) ? pale : grey);

    RECT first = r, last = r;
    if (vertical) { first.bottom = first.top + arrow; last.top = last.bottom - arrow; }
    else          { first.right = first.left + arrow; last.left = last.right - arrow; }

    const uint32_t face = fromColorref(GetSysColor(COLOR_BTNFACE));
    const uint32_t light = fromColorref(GetSysColor(COLOR_3DHILIGHT));
    const uint32_t shadow = fromColorref(GetSysColor(COLOR_BTNSHADOW));
    const uint32_t ink = fromColorref(GetSysColor(COLOR_BTNTEXT));

    auto raised = [&](RECT box) {
        fillRect(d, box, face);
        fillRect(d, RECT{box.left, box.top, box.right - 1, box.top + 1}, light);
        fillRect(d, RECT{box.left, box.top, box.left + 1, box.bottom - 1}, light);
        fillRect(d, RECT{box.left, box.bottom - 1, box.right, box.bottom}, shadow);
        fillRect(d, RECT{box.right - 1, box.top, box.right, box.bottom}, shadow);
    };
    // A triangle as shortening rows, pointing whichever way it is asked to.
    auto arrowhead = [&](RECT box, int dx, int dy) {
        const int cx = (int)(box.left + box.right) / 2;
        const int cy = (int)(box.top + box.bottom) / 2;
        for (int i = 0; i < 4; i++) {
            if (dy) {
                const int y = dy > 0 ? cy - 2 + i : cy + 2 - i;
                fillRect(d, RECT{cx - 3 + i, y, cx + 4 - i, y + 1}, ink);
            } else {
                const int x = dx > 0 ? cx - 2 + i : cx + 2 - i;
                fillRect(d, RECT{x, cy - 3 + i, x + 1, cy + 4 - i}, ink);
            }
        }
    };
    raised(first);
    raised(last);
    arrowhead(first, vertical ? 0 : -1, vertical ? -1 : 0);
    arrowhead(last,  vertical ? 0 :  1, vertical ?  1 : 0);

    // The thumb says how much of the whole is showing, and where in it.
    const int span = std::max(1, info.nMax - info.nMin + 1);
    const int page = std::max(1, (int)info.nPage);
    const int track = std::max(1, length - arrow * 2);
    const int size = std::max(8, std::min(track, track * page / span));
    const int room = std::max(0, track - size);
    const int reach = std::max(1, span - page);
    const int position = std::min(reach, std::max(0, info.nPos - info.nMin));
    const int offset = room * position / reach;
    RECT box = r;
    if (vertical) { box.top = first.bottom + offset; box.bottom = box.top + size; }
    else          { box.left = first.right + offset; box.right = box.left + size; }
    raised(box);
}

// Which part of a scroll bar a point is in, as the SB_ code a WM_VSCROLL or
// WM_HSCROLL carries.
int scrollBarHit(RECT r, POINT p, const SCROLLINFO & info, bool vertical) {
    const int length = vertical ? r.bottom - r.top : r.right - r.left;
    if (length <= 0) return -1;
    const int arrow = std::min(kScrollArrowSize, length / 2);
    const int along = vertical ? p.y - r.top : p.x - r.left;
    if (along < 0 || along >= length) return -1;
    if (along < arrow) return vertical ? SB_LINEUP : SB_LINELEFT;
    if (along >= length - arrow) return vertical ? SB_LINEDOWN : SB_LINERIGHT;

    const int span = std::max(1, info.nMax - info.nMin + 1);
    const int page = std::max(1, (int)info.nPage);
    const int track = std::max(1, length - arrow * 2);
    const int size = std::max(8, std::min(track, track * page / span));
    const int room = std::max(0, track - size);
    const int reach = std::max(1, span - page);
    const int position = std::min(reach, std::max(0, info.nPos - info.nMin));
    const int offset = room * position / reach;
    const int inTrack = along - arrow;
    if (inTrack < offset) return vertical ? SB_PAGEUP : SB_PAGELEFT;
    if (inTrack >= offset + size) return vertical ? SB_PAGEDOWN : SB_PAGERIGHT;
    return SB_THUMBTRACK;
}

namespace {

LRESULT scrollProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const bool vertical = (w.style & SBS_VERT) != 0;
    SCROLLINFO & info = w.scroll[vertical ? SB_VERT : SB_HORZ];
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;
            askParent(w, hwnd, hdc, WM_CTLCOLORSCROLLBAR,
                      GetSysColor(COLOR_SCROLLBAR));
            if (DeviceContext * d = dc(hdc))
                paintScrollBar(*d, clientRect(w), info, vertical);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p{(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
            const int code = scrollBarHit(clientRect(w), p, info, vertical);
            if (code < 0) return 0;
            if (w.parent)
                send(w.parent, vertical ? WM_VSCROLL : WM_HSCROLL,
                     MAKEWPARAM(code, info.nPos), (LPARAM)hwnd);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}


/* ------------------------------------------------------------- the dialog

   A dialog's own class is one of these too: nobody registers #32770 either.  */

LRESULT dialogClassProc(Window & w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            // The dialog procedure decides its own background, and the port's
            // hands back NULL_BRUSH so its painted art is not erased first.
            HBRUSH brush = (HBRUSH)send(hwnd, WM_CTLCOLORDLG, wp, (LPARAM)hwnd);
            GdiObject * o = object(brush);
            if (!brush) return 0;                    // let the class brush do it
            if (o && o->hollow) return 1;            // asked for nothing at all
            RECT client = clientRect(w);
            FillRect((HDC)wp, &client, brush);
            return 1;
        }
        case WM_CLOSE:
            // A dialog closes by ending, not by being destroyed under its own
            // modal loop.
            EndDialog(hwnd, IDCANCEL);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

}   // namespace


bool controlHitTransparent(const Window & w) {
    return !w.proc && w.controlClass == L"STATIC";
}

LRESULT controlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window * w = window(hwnd);
    if (!w) return 0;

    // What every class answers the same way.
    switch (msg) {
        case WM_SETFONT:
            w->font = (HFONT)wp;
            if (lp) invalidate(*w, nullptr, true);
            return 0;
        case WM_GETFONT:
            return (LRESULT)w->font;
        case WM_SETTEXT:
            w->text = lp ? (const wchar_t *)lp : L"";
            w->caret = (int)w->text.size();
            invalidate(*w, nullptr, true);
            return TRUE;
        case WM_GETTEXT: {
            if (!lp || (int)wp <= 0) return 0;
            wchar_t * out = (wchar_t *)lp;
            int n = 0;
            for (; n < (int)w->text.size() && n + 1 < (int)wp; n++)
                out[n] = w->text[n];
            out[n] = 0;
            return n;
        }
        case WM_GETTEXTLENGTH:
            return (LRESULT)w->text.size();
        case WM_ENABLE:
            w->enabled = wp != 0;
            invalidate(*w, nullptr, true);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            invalidate(*w, nullptr, true);
            break;                                   // and on to the class
        default:
            break;
    }

    // A dropped combo list has the mouse, and it answers for its owner.
    if (w->dropOwner &&
        (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)) {
        if (routeDropListClick(*w, hwnd, msg, lp)) return 0;
    }

    const std::wstring & cls = w->controlClass;
    if (cls == L"BUTTON")    return buttonProc(*w, hwnd, msg, wp, lp);
    if (cls == L"STATIC")    return staticProc(*w, hwnd, msg, wp, lp);
    if (cls == L"EDIT")      return editProc(*w, hwnd, msg, wp, lp);
    if (cls == L"LISTBOX")   return listProc(*w, hwnd, msg, wp, lp);
    if (cls == L"COMBOBOX")  return comboProc(*w, hwnd, msg, wp, lp);
    if (cls == L"SCROLLBAR") return scrollProc(*w, hwnd, msg, wp, lp);
    if (cls == L"#32770")    return dialogClassProc(*w, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}


/* ------------------------------------------------------- dialog navigation */

namespace {

// The controls a dialog can move focus between, in the order the template put
// them in, which is the order Tab follows.
std::vector<HWND> tabStops(HWND dialog) {
    std::vector<HWND> stops;
    Window * d = window(dialog);
    if (!d) return stops;
    for (HWND child : d->children) {
        Window * c = window(child);
        if (!c || !c->visible || !c->enabled) continue;
        if (!(c->style & WS_TABSTOP)) continue;
        stops.push_back(child);
    }
    return stops;
}

HWND defaultButton(HWND dialog) {
    Window * d = window(dialog);
    if (!d) return nullptr;
    for (HWND child : d->children) {
        Window * c = window(child);
        if (!c || c->controlClass != L"BUTTON") continue;
        if ((c->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON) return child;
    }
    return nullptr;
}

}   // namespace

void dialogSetInitialFocus(HWND dialog) {
    const std::vector<HWND> stops = tabStops(dialog);
    if (!stops.empty()) SetFocus(stops.front());
}

extern "C" BOOL IsDialogMessageW(HWND dialog, LPMSG msg) {
    Window * d = window(dialog);
    if (!d || !msg) return FALSE;
    if (msg->message != WM_KEYDOWN && msg->message != WM_CHAR &&
        msg->message != WM_KEYUP)
        return FALSE;

    // Only keys aimed at this dialog or something inside it.
    bool inside = msg->hwnd == dialog;
    for (HWND child = msg->hwnd; !inside && child; ) {
        Window * c = window(child);
        if (!c) break;
        child = c->parent;
        if (child == dialog) inside = true;
    }
    if (!inside) return FALSE;

    // A control that wants the key gets it: an edit needs its arrows, a list
    // needs its up and down.
    const LRESULT code = msg->hwnd ? send(msg->hwnd, WM_GETDLGCODE, 0, 0) : 0;

    if (msg->message == WM_KEYDOWN) {
        switch (msg->wParam) {
            case VK_TAB: {
                const std::vector<HWND> stops = tabStops(dialog);
                if (stops.empty()) return TRUE;
                const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                auto at = std::find(stops.begin(), stops.end(), GetFocus());
                size_t index = at == stops.end() ? 0
                             : (size_t)(at - stops.begin());
                index = back ? (index + stops.size() - 1) % stops.size()
                             : (index + 1) % stops.size();
                SetFocus(stops[index]);
                return TRUE;
            }
            case VK_RETURN: {
                if (code & DLGC_WANTALLKEYS) return FALSE;
                HWND button = GetFocus();
                Window * f = window(button);
                if (!f || f->controlClass != L"BUTTON") button = defaultButton(dialog);
                Window * b = window(button);
                if (b) send(dialog, WM_COMMAND, MAKEWPARAM(b->id, BN_CLICKED),
                            (LPARAM)button);
                return TRUE;
            }
            case VK_ESCAPE:
                send(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
                return TRUE;
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
                if (code & DLGC_WANTARROWS) return FALSE;
                break;
            default:
                break;
        }
    }
    if (msg->message == WM_CHAR && (msg->wParam == VK_TAB ||
                                    msg->wParam == VK_RETURN ||
                                    msg->wParam == VK_ESCAPE))
        return TRUE;             // swallowed with the key that produced it

    // Anything else is delivered as usual, but through this dialog's own path.
    TranslateMessage(msg);
    DispatchMessageW(msg);
    return TRUE;
}

}   // namespace shim
