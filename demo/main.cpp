// A window, the game's own graphics, and a menu.
//
// This is not the game: the port's own host is a separate, larger job.  It
// exists to put the pipeline on screen end to end - resource pack, palettised
// DIB, GDI, canvas - and to be the thing that fails visibly when one of those
// is wrong.  Everything it draws comes out of the player's own SIMTOWER.EXE.

#include "win32_internal.h"

#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace shim;

namespace {

// Filled in at startup from the pack: every BITMAP resource, largest first, so
// the page opens on something recognisable rather than on a 16x16 icon.
struct Sprite {
    HBITMAP bitmap = nullptr;
    int id = 0;
    int width = 0;
    int height = 0;
};

std::vector<Sprite> g_sprites;
size_t g_page = 0;
HWND g_frame = nullptr;

const int kScreenWidth = 800;
const int kScreenHeight = 600;

void collectSprites() {
    for (size_t i = 0; i < resourceCount(); i++) {
        if (std::string(resourceType(i)) != "BITMAP") continue;
        const int id = resourceId(i);
        HBITMAP bitmap = LoadBitmapW(nullptr, MAKEINTRESOURCEW(id));
        if (!bitmap) continue;
        GdiObject * o = object(bitmap);
        if (!o || !o->surface) continue;
        Sprite s;
        s.bitmap = bitmap;
        s.id = id;
        s.width = o->surface->width;
        s.height = o->surface->height;
        g_sprites.push_back(s);
    }
    // Widest first: the tower's own artwork is wide, the icons are not.
    std::sort(g_sprites.begin(), g_sprites.end(),
              [](const Sprite & a, const Sprite & b) {
                  return a.width * a.height > b.width * b.height;
              });
}

void drawSprite(HDC hdc, const Sprite & s, int x, int y) {
    HDC memory = CreateCompatibleDC(hdc);
    SelectObject(memory, s.bitmap);
    BitBlt(hdc, x, y, s.width, s.height, memory, 0, 0, SRCCOPY);
    DeleteDC(memory);
}

void paint(HWND hwnd, HDC hdc) {
    RECT client;
    GetClientRect(hwnd, &client);

    HBRUSH background = CreateSolidBrush(RGB(0, 64, 96));
    FillRect(hdc, &client, background);
    DeleteObject(background);

    // A bright frame around the client area.  Without it, a canvas that is
    // present but has not been drawn into looks exactly like a canvas that is
    // not there, and that ambiguity has already cost a round trip.
    HPEN edge = CreatePen(PS_SOLID, 1, RGB(255, 176, 0));
    HGDIOBJ wasPen = SelectObject(hdc, edge);
    MoveToEx(hdc, 1, 1, nullptr);
    LineTo(hdc, client.right - 2, 1);
    LineTo(hdc, client.right - 2, client.bottom - 2);
    LineTo(hdc, 1, client.bottom - 2);
    LineTo(hdc, 1, 1);
    SelectObject(hdc, wasPen);
    DeleteObject(edge);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    SelectObject(hdc, CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0,
                                  0, L"Arial"));

    char line[200];
    if (g_sprites.empty()) {
        const char * waiting =
            "Choose your SIMTOWER.EXE above.  Nothing is uploaded - the file is "
            "read in this browser.";
        TextOutA(hdc, 8, 8, waiting, (int)strlen(waiting));
        return;
    }
    snprintf(line, sizeof(line),
             "%d bitmaps out of the executable.  Click, or press space, for the "
             "next page.", (int)g_sprites.size());
    TextOutA(hdc, 8, 8, line, (int)strlen(line));

    // A page of the pack's own artwork, laid out left to right.
    int x = 8, y = 32, rowHeight = 0;
    size_t shown = 0;
    for (size_t i = g_page; i < g_sprites.size(); i++) {
        const Sprite & s = g_sprites[i];
        if (s.width > client.right - 16) continue;
        if (x + s.width > client.right - 8) {
            x = 8;
            y += rowHeight + 20;
            rowHeight = 0;
        }
        if (y + s.height > client.bottom - 8) break;

        drawSprite(hdc, s, x, y);

        SetTextColor(hdc, RGB(180, 210, 230));
        snprintf(line, sizeof(line), "%d  %dx%d", s.id, s.width, s.height);
        TextOutA(hdc, x, y + s.height + 2, line, (int)strlen(line));

        x += s.width + 12;
        if (s.height > rowHeight) rowHeight = s.height;
        shown++;
    }

    if (shown == 0 && !g_sprites.empty()) g_page = 0;
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            // The menu bar gets first refusal; anything it does not want pages
            // the artwork on.
            if (Window * win = window(hwnd))
                if (menuBarClick(*win, POINT{(int)(short)LOWORD(l),
                                             (int)(short)HIWORD(l)}))
                    return 0;
            g_page += 8;
            if (g_page >= g_sprites.size()) g_page = 0;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_SPACE || w == VK_RIGHT) g_page += 8;
            else if (w == VK_LEFT) g_page = g_page >= 8 ? g_page - 8 : 0;
            else if (w == VK_HOME) g_page = 0;
            else break;
            if (g_page >= g_sprites.size()) g_page = 0;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_COMMAND:
            if (w == 101) g_page = 0;
            if (w == 199) PostQuitMessage(0);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

void frame() {
    dispatchPending();
    paintPending();
    presentScreen();

    // The first few frames are announced, because "the page shows text and no
    // picture" has several causes and this separates them: no line at all means
    // the loop never ran, lines with no picture means the presentation did.
    static int announced = 0;
    if (announced < 3) {
        announced++;
        printf("frame %d presented, screen %dx%d\n", announced,
               state().screen.width, state().screen.height);
    }
}

}   // namespace

// Called from the page once the player has chosen their SIMTOWER.EXE.  Nothing
// copyrighted is served with the page; the file never leaves the browser.
extern "C" EMSCRIPTEN_KEEPALIVE
int simtowerLoadExecutable(const uint8_t * data, int size) {
    if (!loadResourcesFromExecutable(data, (size_t)size)) return 0;
    collectSprites();
    printf("%d bitmaps decoded\n", (int)g_sprites.size());
    if (g_frame) InvalidateRect(g_frame, nullptr, TRUE);
    return (int)g_sprites.size();
}

int main() {
    State & s = state();
    s.screen.resize(kScreenWidth, kScreenHeight);
    hostInit();
    printf("main: screen %dx%d\n", s.screen.width, s.screen.height);

    WNDCLASSW cls{};
    cls.lpfnWndProc = frameProc;
    cls.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    cls.lpszClassName = L"SimTowerWasmDemo";
    RegisterClassW(&cls);

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, 101, L"&First page");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, 199, L"E&xit");

    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");

    g_frame = CreateWindowExW(0, L"SimTowerWasmDemo",
                              L"SimTower resources - WebAssembly",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              0, 0, kScreenWidth, kScreenHeight,
                              nullptr, bar, nullptr, nullptr);
    if (!g_frame) return 1;
    ShowWindow(g_frame, SW_SHOW);

    // The frame belongs to the browser.  A loop of our own here would lock the
    // tab, which is the failure that made the OpenSkyscraper port unreloadable.
    emscripten_set_main_loop(frame, 0, 0);
    return 0;
}
