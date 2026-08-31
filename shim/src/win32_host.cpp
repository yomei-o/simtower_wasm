// The browser side: the canvas, the input, and the frame.
//
// One screen surface holds everything drawn, and it reaches the canvas as an
// ImageData once per frame.  There is no WebGL here on purpose - the port's
// drawing is DIB blits and lines, so a 2D context is all it ever needed.
//
// Sleep and a blocking message loop are both unusable in a browser, so the
// frame is driven by emscripten_set_main_loop and the port's own loop is
// pumped from it.  That is the same trap that froze the OpenSkyscraper port:
// spinning on the main thread locks the tab, and even the reload button.

#include "win32_internal.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstring>

namespace shim {

namespace {

bool g_initialised = false;

// Where the canvas last was told the screen is, so the element is only resized
// when it actually changes.
int g_presentedWidth = 0;
int g_presentedHeight = 0;

// EM_JS rather than EM_ASM: a comma inside the JavaScript splits an EM_ASM
// macro argument, and both of these need one.
EM_JS(void, jsResizeCanvas, (int w, int h), {
    var canvas = document.getElementById('canvas');
    if (!canvas) return;
    canvas.width = w;
    canvas.height = h;
    // The element's box matches the backbuffer exactly.  Any other ratio both
    // resamples the picture and misplaces every click, because pointer
    // coordinates are derived from the element rect.
    canvas.style.width = w + 'px';
    canvas.style.height = h + 'px';
});

EM_JS(void, jsPresent, (uintptr_t pixels, int w, int h), {
    var canvas = document.getElementById('canvas');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    if (!ctx) return;
    var count = w * h * 4;
    var image = ctx.createImageData(w, h);
    image.data.set(HEAPU8.subarray(pixels, pixels + count));
    ctx.putImageData(image, 0, 0);
});

EM_JS(void, jsSetCursor, (const char * css), {
    var canvas = document.getElementById('canvas');
    if (canvas) canvas.style.cursor = UTF8ToString(css);
});

void resizeCanvas(int w, int h) {
    if (w == g_presentedWidth && h == g_presentedHeight) return;
    g_presentedWidth = w;
    g_presentedHeight = h;
    jsResizeCanvas(w, h);
}

int buttonFromDom(unsigned short button) {
    switch (button) {
        case 0:  return WM_LBUTTONDOWN;
        case 1:  return WM_MBUTTONDOWN;
        case 2:  return WM_RBUTTONDOWN;
        default: return 0;
    }
}

WPARAM mouseKeys(int shift, int ctrl, bool leftDown, bool rightDown) {
    WPARAM keys = 0;
    if (leftDown) keys |= MK_LBUTTON;
    if (rightDown) keys |= MK_RBUTTON;
    if (shift) keys |= MK_SHIFT;
    if (ctrl) keys |= MK_CONTROL;
    return keys;
}

bool g_leftDown = false;
bool g_rightDown = false;

EM_BOOL onMouse(int type, const EmscriptenMouseEvent * e, void *) {
    State & s = state();
    s.mouse.x = e->targetX;
    s.mouse.y = e->targetY;

    // Capture wins, then the window under the pointer.  That is what makes a
    // drag keep going after the pointer leaves the window it started in.
    HWND target = s.capture ? s.capture : windowAtPoint(s.mouse);
    if (!target) return EM_TRUE;

    Window * w = window(target);
    if (!w) return EM_TRUE;
    const POINT origin = clientOrigin(*w);
    const LPARAM pos = MAKELPARAM(s.mouse.x - origin.x, s.mouse.y - origin.y);

    switch (type) {
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            post(target, WM_MOUSEMOVE,
                 mouseKeys(e->shiftKey, e->ctrlKey, g_leftDown, g_rightDown), pos);
            break;
        case EMSCRIPTEN_EVENT_MOUSEDOWN: {
            const int msg = buttonFromDom(e->button);
            if (!msg) break;
            if (msg == WM_LBUTTONDOWN) g_leftDown = true;
            if (msg == WM_RBUTTONDOWN) g_rightDown = true;
            s.keys[msg == WM_LBUTTONDOWN ? VK_LBUTTON
                  : msg == WM_RBUTTONDOWN ? VK_RBUTTON : VK_MBUTTON] = 0x80;
            post(target, msg,
                 mouseKeys(e->shiftKey, e->ctrlKey, g_leftDown, g_rightDown), pos);
            break;
        }
        case EMSCRIPTEN_EVENT_MOUSEUP: {
            const int down = buttonFromDom(e->button);
            if (!down) break;
            if (down == WM_LBUTTONDOWN) g_leftDown = false;
            if (down == WM_RBUTTONDOWN) g_rightDown = false;
            s.keys[down == WM_LBUTTONDOWN ? VK_LBUTTON
                  : down == WM_RBUTTONDOWN ? VK_RBUTTON : VK_MBUTTON] = 0;
            post(target, down + 1,     // ...DOWN + 1 is the matching ...UP
                 mouseKeys(e->shiftKey, e->ctrlKey, g_leftDown, g_rightDown), pos);
            break;
        }
        case EMSCRIPTEN_EVENT_DBLCLICK:
            post(target, WM_LBUTTONDBLCLK,
                 mouseKeys(e->shiftKey, e->ctrlKey, g_leftDown, g_rightDown), pos);
            break;
        default: break;
    }
    return EM_TRUE;
}

EM_BOOL onWheel(int, const EmscriptenWheelEvent * e, void *) {
    State & s = state();
    HWND target = s.capture ? s.capture : windowAtPoint(s.mouse);
    if (!target) return EM_TRUE;
    const short delta = e->deltaY > 0 ? -120 : 120;
    post(target, WM_MOUSEWHEEL, MAKEWPARAM(0, delta),
         MAKELPARAM(s.mouse.x, s.mouse.y));
    return EM_TRUE;
}

// The browser's key names, mapped to the virtual key codes the port switches on.
int virtualKey(const char * code, const char * key) {
    if (!strcmp(code, "ArrowUp"))    return VK_UP;
    if (!strcmp(code, "ArrowDown"))  return VK_DOWN;
    if (!strcmp(code, "ArrowLeft"))  return VK_LEFT;
    if (!strcmp(code, "ArrowRight")) return VK_RIGHT;
    if (!strcmp(code, "Escape"))     return VK_ESCAPE;
    if (!strcmp(code, "Enter"))      return VK_RETURN;
    if (!strcmp(code, "Space"))      return VK_SPACE;
    if (!strcmp(code, "Tab"))        return VK_TAB;
    if (!strcmp(code, "Backspace"))  return VK_BACK;
    if (!strcmp(code, "Delete"))     return VK_DELETE;
    if (!strcmp(code, "Home"))       return VK_HOME;
    if (!strcmp(code, "End"))        return VK_END;
    if (!strcmp(code, "PageUp"))     return VK_PRIOR;
    if (!strcmp(code, "PageDown"))   return VK_NEXT;
    if (!strncmp(code, "Digit", 5))  return code[5];
    if (!strncmp(code, "Key", 3))    return code[3];
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        const int n = atoi(code + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (key && key[0] && !key[1]) return (unsigned char)key[0];
    return 0;
}

EM_BOOL onKey(int type, const EmscriptenKeyboardEvent * e, void *) {
    State & s = state();
    HWND target = s.focus ? s.focus : s.active;
    if (!target) return EM_FALSE;

    const int vk = virtualKey(e->code, e->key);
    if (!vk) return EM_FALSE;

    s.keys[VK_SHIFT] = e->shiftKey ? 0x80 : 0;
    s.keys[VK_CONTROL] = e->ctrlKey ? 0x80 : 0;

    if (type == EMSCRIPTEN_EVENT_KEYDOWN) {
        s.keys[vk & 0xff] = 0x80;
        post(target, WM_KEYDOWN, (WPARAM)vk, 1);
        // A printable key also produces the character message the port reads
        // for typing; TranslateMessage is where Windows would do this.
        if (e->key[0] && !e->key[1])
            post(target, WM_CHAR, (WPARAM)(unsigned char)e->key[0], 1);
    } else if (type == EMSCRIPTEN_EVENT_KEYUP) {
        s.keys[vk & 0xff] = 0;
        post(target, WM_KEYUP, (WPARAM)vk, 1);
    }
    // Swallowed so the page does not scroll under the game.
    return EM_TRUE;
}


}   // namespace


/* ----------------------------------------------------- driving it headlessly

   The command-line harness has no DOM, so its canvas stub never fires an event
   and nothing can be clicked.  These feed the *same* handlers the browser's
   listeners call - a synthesised EmscriptenMouseEvent through onMouse - rather
   than posting messages directly, because a second input path would be a
   second thing to get wrong and the browser's would stop being the one under
   test.                                                                     */

extern "C" EMSCRIPTEN_KEEPALIVE
void simtowerInjectMouse(int type, int x, int y, int button) {
    EmscriptenMouseEvent e{};
    e.targetX = x;
    e.targetY = y;
    e.clientX = x;
    e.clientY = y;
    e.button = (unsigned short)button;
    onMouse(type, &e, nullptr);
}

extern "C" EMSCRIPTEN_KEEPALIVE
void simtowerInjectKey(int type, int vk, int character) {
    EmscriptenKeyboardEvent e{};
    // onKey resolves the virtual key from `code`/`key`, so a caller that knows
    // the code passes it straight through as a one-character `key`.
    if (character > 0 && character < 128) {
        e.key[0] = (char)character;
        e.key[1] = 0;
    }
    if (vk) {
        // A synthetic code that virtualKey() maps back to exactly this vk.
        switch (vk) {
            case VK_RETURN: strcpy(e.code, "Enter"); break;
            case VK_ESCAPE: strcpy(e.code, "Escape"); break;
            case VK_TAB:    strcpy(e.code, "Tab"); break;
            case VK_SPACE:  strcpy(e.code, "Space"); break;
            case VK_BACK:   strcpy(e.code, "Backspace"); break;
            case VK_UP:     strcpy(e.code, "ArrowUp"); break;
            case VK_DOWN:   strcpy(e.code, "ArrowDown"); break;
            case VK_LEFT:   strcpy(e.code, "ArrowLeft"); break;
            case VK_RIGHT:  strcpy(e.code, "ArrowRight"); break;
            default:
                if (vk >= '0' && vk <= '9') sprintf(e.code, "Digit%c", vk);
                else if (vk >= 'A' && vk <= 'Z') sprintf(e.code, "Key%c", vk);
                break;
        }
    }
    onKey(type, &e, nullptr);
}

// The window tree, for a harness that has to say what it is looking at rather
// than guess from a picture.
extern "C" EMSCRIPTEN_KEEPALIVE
void simtowerDumpWindows(void) {
    State & s = state();
    printf("windows: %d  active=%p focus=%p capture=%p\n",
           (int)s.windows.size(), (void *)s.active, (void *)s.focus,
           (void *)s.capture);
    for (auto & entry : s.windows) {
        Window & w = entry.second;
        if (w.destroyed) continue;
        printf("  %p %-10s cls=%-12s id=%-5d %s%s (%d,%d)-(%d,%d) style=%08x \"%s\"\n",
               (void *)entry.first,
               w.isDialog ? "[dialog]" : (w.controlClass.empty() ? "" : "[control]"),
               toUtf8(w.className.c_str()).c_str(), w.id,
               w.visible ? "vis" : "hid", w.enabled ? "" : "/dis",
               (int)w.rect.left, (int)w.rect.top,
               (int)w.rect.right, (int)w.rect.bottom,
               (unsigned)w.style,
               toUtf8(w.text.c_str()).c_str());
    }
    fflush(stdout);
}

void hostInit() {
    if (g_initialised) return;
    g_initialised = true;

    // Listened for on the document, not the canvas.  A canvas cannot take
    // focus unless it is given a tabindex, and even then the mousedown handler
    // preventing default stops the click from focusing it - so keys aimed at
    // the canvas never arrive.  This is the exact trap the OpenSkyscraper port
    // fell into.
    emscripten_set_mousemove_callback("#canvas", nullptr, 0, onMouse);
    emscripten_set_mousedown_callback("#canvas", nullptr, 0, onMouse);
    emscripten_set_mouseup_callback("#canvas", nullptr, 0, onMouse);
    emscripten_set_dblclick_callback("#canvas", nullptr, 0, onMouse);
    emscripten_set_wheel_callback("#canvas", nullptr, 0, onWheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, 0, onKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, 0, onKey);
}

void hostPresent(const Surface & s) {
    if (s.width <= 0 || s.height <= 0) return;
    resizeCanvas(s.width, s.height);
    // The surface is already in the byte order ImageData wants, so this is one
    // copy out of the heap and one putImageData - no per-pixel work in JS.
    jsPresent((uintptr_t)s.pixels.data(), s.width, s.height);
}

void hostSetCursor(const char * css) { jsSetCursor(css); }

}   // namespace shim
