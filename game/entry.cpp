// The game's own WinMain, started from a browser.
//
// The port's dispatcher is a Windows loop: PeekMessage, and simulate when there
// is nothing to dispatch.  Under Emscripten a loop like that never yields and
// the tab freezes, so the choice is to restructure it or to make it yield.
//
// It yields.  Asyncify lets PeekMessage suspend and resume, which keeps the
// port's dispatch order exactly as recovered - upstream is explicit that the
// recovered dispatcher stays unchanged - and needs no patch carried against a
// repository that cannot be forked into this one.  The cost is a larger, slower
// wasm, which is the right thing to spend here.

#include "win32_internal.h"

#include <emscripten.h>

#include <cstdio>
#include <vector>

// C++ linkage, not extern "C": native_main.cpp defines it as an ordinary C++
// function, so a C declaration here looks for a symbol that does not exist.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine,
                   int show);

namespace {

// The executable arrives from the page, so startup waits for it.  Waiting is
// only possible at all because Asyncify is on.
std::vector<uint8_t> g_executable;
bool g_ready = false;

const int kScreenWidth = 800;
const int kScreenHeight = 600;

}   // namespace

extern "C" EMSCRIPTEN_KEEPALIVE
int simtowerLoadExecutable(const uint8_t * data, int size) {
    if (size <= 0) return 0;
    g_executable.assign(data, data + size);
    if (!shim::loadResourcesFromExecutable(g_executable.data(),
                                           g_executable.size()))
        return 0;
    g_ready = true;
    return (int)shim::resourceCount();
}

int main() {
    shim::State & s = shim::state();

    // A screen before the game asks for one: the port creates its windows
    // against these metrics, and CW_USEDEFAULT resolves against them too.
    s.screen.resize(kScreenWidth, kScreenHeight);
    shim::hostInit();
    shim::presentScreen();

    printf("waiting for SIMTOWER.EXE\n");
    while (!g_ready)
        emscripten_sleep(100);

    printf("starting WinMain\n");
    char commandLine[] = "";
    const int code = WinMain((HINSTANCE)0x1, nullptr, commandLine, SW_SHOW);
    printf("WinMain returned %d\n", code);

    // Whatever it left on screen stays there rather than going blank.
    shim::presentScreen();
    return code;
}
