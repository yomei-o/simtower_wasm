// waveOut, silently.
//
// Sound is not what is being proved yet, but the port checks that audio
// initialised and gives up if it did not, so this has to succeed rather than
// fail politely.  Buffers are marked done as soon as they are written and the
// callback is invoked, because the port recycles them on that signal - a header
// that never comes back would stall the sound queue and, with it, whatever the
// game does after a sound.
//
// Replacing this with Web Audio is a self-contained job: the PCM the port
// submits is already in the header.

#include "win32_internal.h"

#include <cstdio>

namespace shim {

namespace {

struct WaveDevice {
    bool open = false;
    WAVEFORMATEX format{};
    DWORD_PTR callback = 0;
    DWORD_PTR instance = 0;
    DWORD flags = 0;
    DWORD volume = 0xffffffffu;
};

WaveDevice g_device;
HWAVEOUT g_handle = nullptr;

// The port passes either a window to post to or a function to call; both are
// answered so its buffer accounting stays balanced.
void notify(UINT message, DWORD_PTR param) {
    if (!g_device.callback) return;
    if ((g_device.flags & CALLBACK_FUNCTION) == CALLBACK_FUNCTION) {
        using Callback = void (*)(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
        ((Callback)g_device.callback)(g_handle, message, g_device.instance,
                                     param, 0);
    } else {
        post((HWND)g_device.callback, message, (WPARAM)g_handle, (LPARAM)param);
    }
}

}   // namespace

extern "C" UINT waveOutGetNumDevs(void) { return 1; }

extern "C" MMRESULT waveOutGetDevCapsW(UINT_PTR, LPWAVEOUTCAPSW caps,
                                       UINT size) {
    if (!caps || size < sizeof(WAVEOUTCAPSW)) return MMSYSERR_ERROR;
    WAVEOUTCAPSW filled{};
    filled.wMid = 1;
    filled.wPid = 1;
    filled.vDriverVersion = 0x0100;
    const wchar_t name[] = L"WebAssembly";
    for (size_t i = 0; i < sizeof(name) / sizeof(name[0]) && i < MAXPNAMELEN; i++)
        filled.szPname[i] = name[i];
    filled.dwFormats = WAVE_FORMAT_4S16;
    filled.wChannels = 2;
    filled.dwSupport = WAVECAPS_VOLUME;
    *caps = filled;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutOpen(HWAVEOUT * out, UINT, LPCWAVEFORMATEX format,
                                DWORD_PTR callback, DWORD_PTR instance,
                                DWORD flags) {
    if (g_device.open) return MMSYSERR_ALLOCATED;
    g_device = WaveDevice{};
    g_device.open = true;
    if (format) g_device.format = *format;
    g_device.callback = callback;
    g_device.instance = instance;
    g_device.flags = flags;
    g_handle = (HWAVEOUT)(void *)state().allocate();
    if (out) *out = g_handle;
    notify(WOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutClose(HWAVEOUT out) {
    if (out != g_handle || !g_device.open) return MMSYSERR_INVALHANDLE;
    notify(WOM_CLOSE, 0);
    g_device.open = false;
    g_handle = nullptr;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutPrepareHeader(HWAVEOUT out, LPWAVEHDR header, UINT) {
    if (out != g_handle) return MMSYSERR_INVALHANDLE;
    if (!header) return MMSYSERR_ERROR;
    header->dwFlags |= WHDR_PREPARED;
    header->dwFlags &= ~WHDR_DONE;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutUnprepareHeader(HWAVEOUT out, LPWAVEHDR header,
                                           UINT) {
    if (out != g_handle) return MMSYSERR_INVALHANDLE;
    if (!header) return MMSYSERR_ERROR;
    if (header->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
    header->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutWrite(HWAVEOUT out, LPWAVEHDR header, UINT) {
    if (out != g_handle) return MMSYSERR_INVALHANDLE;
    if (!header) return MMSYSERR_ERROR;
    // Straight to done: nothing is played, and the port needs the header back
    // before it will submit the next one.
    header->dwFlags &= ~WHDR_INQUEUE;
    header->dwFlags |= WHDR_DONE;
    notify(WOM_DONE, (DWORD_PTR)header);
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutReset(HWAVEOUT out) {
    return out == g_handle ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
}
extern "C" MMRESULT waveOutPause(HWAVEOUT out) {
    return out == g_handle ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
}
extern "C" MMRESULT waveOutRestart(HWAVEOUT out) {
    return out == g_handle ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
}
extern "C" MMRESULT waveOutGetVolume(HWAVEOUT, DWORD * volume) {
    if (volume) *volume = g_device.volume;
    return MMSYSERR_NOERROR;
}
extern "C" MMRESULT waveOutSetVolume(HWAVEOUT, DWORD volume) {
    g_device.volume = volume;
    return MMSYSERR_NOERROR;
}

}   // namespace shim
