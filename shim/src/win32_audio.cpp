// waveOut, over Web Audio.
//
// The port hands over a WAVEFORMATEX and a block of PCM and then watches the
// header for WHDR_DONE, which is how it knows a channel is free again.  So
// there are two halves here: get the samples to the browser, and put the flag
// back at the right moment - not immediately, which would have the port reset
// and close the device while the sound was still playing.
//
// It opens one device per channel and there are two of them, so a single-device
// shim could only ever play one sound at a time.  There are as many here as it
// asks for.
//
// Where there is no AudioContext at all - the command-line harness, or a
// browser that has not been given a gesture yet - playing silently is the
// answer: the flags still come back on time, so the game's own sound
// accounting is the same either way.

#include "win32_internal.h"

#include <emscripten.h>

#include <cstdio>
#include <map>
#include <vector>

namespace shim {

namespace {

// EM_JS rather than EM_ASM: these take several arguments, and a comma splits an
// EM_ASM macro argument.
EM_JS(int, jsWaveStart, (uintptr_t data, int bytes, int channels, int rate,
                         int bits, int loops), {
    try {
        var audio = Module.simtowerAudio;
        if (!audio) {
            var Context = (typeof AudioContext !== 'undefined') ? AudioContext
                        : (typeof webkitAudioContext !== 'undefined')
                              ? webkitAudioContext : null;
            if (!Context) return 0;
            audio = Module.simtowerAudio = { ctx: new Context(), next: 1,
                                             playing: {} };
        }
        var ctx = audio.ctx;
        // A page has to be interacted with before it may make a sound; by the
        // time the game plays one it has been clicked, so this succeeds.
        if (ctx.state === 'suspended') ctx.resume();

        if (channels < 1) channels = 1;
        var bytesPerSample = (bits === 8) ? 1 : 2;
        var frames = Math.floor(bytes / (bytesPerSample * channels));
        if (frames <= 0) return 0;

        var buffer = ctx.createBuffer(channels, frames, rate);
        for (var channel = 0; channel < channels; channel++) {
            var out = buffer.getChannelData(channel);
            if (bits === 8) {
                // Eight-bit PCM is unsigned and rests at 0x80.
                for (var i = 0; i < frames; i++)
                    out[i] = (HEAPU8[data + i * channels + channel] - 128) / 128;
            } else {
                var base = data >> 1;
                for (var i = 0; i < frames; i++)
                    out[i] = HEAP16[base + i * channels + channel] / 32768;
            }
        }

        var source = ctx.createBufferSource();
        source.buffer = buffer;
        if (loops > 1) {
            source.loop = true;
            source.start(0, 0, buffer.duration * loops);
        } else {
            source.start();
        }
        var id = audio.next++;
        audio.playing[id] = source;
        source.onended = function() { delete audio.playing[id]; };
        return id;
    } catch (e) {
        return 0;
    }
});

EM_JS(void, jsWaveStop, (int id), {
    var audio = Module.simtowerAudio;
    if (!audio || !id) return;
    var source = audio.playing[id];
    if (!source) return;
    try { source.stop(); } catch (e) {}
    delete audio.playing[id];
});

struct Device {
    bool open = false;
    WAVEFORMATEX format{};
    DWORD_PTR callback = 0;
    DWORD_PTR instance = 0;
    DWORD flags = 0;
    DWORD volume = 0xffffffffu;

    // The buffer in flight, if any.
    LPWAVEHDR header = nullptr;
    int source = 0;                    // the Web Audio node, 0 when silent
    double endsAt = 0;                 // ms on the same clock as hostNow()
};

std::map<uintptr_t, Device> g_devices;

Device * deviceFor(HWAVEOUT handle) {
    auto it = g_devices.find((uintptr_t)handle);
    return it == g_devices.end() || !it->second.open ? nullptr : &it->second;
}

// The port passes either a window to post to or a function to call; both are
// answered so its buffer accounting stays balanced.
void notify(Device & device, HWAVEOUT handle, UINT message, DWORD_PTR param) {
    if (!device.callback) return;
    if ((device.flags & CALLBACK_FUNCTION) == CALLBACK_FUNCTION) {
        using Callback = void (*)(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
        ((Callback)device.callback)(handle, message, device.instance, param, 0);
    } else {
        post((HWND)device.callback, message, (WPARAM)handle, (LPARAM)param);
    }
}

// How long a block of PCM lasts, in milliseconds.
double durationOf(const WAVEFORMATEX & format, DWORD bytes, DWORD loops) {
    DWORD rate = format.nAvgBytesPerSec;
    if (!rate) {
        const WORD align = format.nBlockAlign
                         ? format.nBlockAlign
                         : (WORD)((format.wBitsPerSample / 8) * format.nChannels);
        rate = format.nSamplesPerSec * (align ? align : 1);
    }
    if (!rate) return 0;
    const double passes = loops > 1 ? (double)loops : 1.0;
    return (double)bytes * 1000.0 / (double)rate * passes;
}

void finish(Device & device, HWAVEOUT handle) {
    if (!device.header) return;
    LPWAVEHDR header = device.header;
    device.header = nullptr;
    device.source = 0;
    header->dwFlags &= ~WHDR_INQUEUE;
    header->dwFlags |= WHDR_DONE;
    notify(device, handle, WOM_DONE, (DWORD_PTR)header);
}

}   // namespace


// Called from the message pump, which the port turns constantly.  A header is
// only handed back when its sound has actually finished: returning it straight
// away, as this used to, means the next pump resets and closes the device
// underneath a sound that has barely started.
void audioTick() {
    if (g_devices.empty()) return;
    const double now = hostNow();
    for (auto & entry : g_devices) {
        Device & device = entry.second;
        if (!device.open || !device.header) continue;
        if (now < device.endsAt) continue;
        finish(device, (HWAVEOUT)entry.first);
    }
}


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
    Device device{};
    device.open = true;
    if (format) device.format = *format;
    device.callback = callback;
    device.instance = instance;
    device.flags = flags;

    const uintptr_t handle = state().allocate();
    g_devices[handle] = device;
    if (out) *out = (HWAVEOUT)handle;
    notify(g_devices[handle], (HWAVEOUT)handle, WOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutClose(HWAVEOUT out) {
    Device * device = deviceFor(out);
    if (!device) return MMSYSERR_INVALHANDLE;
    if (device->source) jsWaveStop(device->source);
    notify(*device, out, WOM_CLOSE, 0);
    g_devices.erase((uintptr_t)out);
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutPrepareHeader(HWAVEOUT out, LPWAVEHDR header, UINT) {
    if (!deviceFor(out)) return MMSYSERR_INVALHANDLE;
    if (!header) return MMSYSERR_ERROR;
    header->dwFlags |= WHDR_PREPARED;
    header->dwFlags &= ~WHDR_DONE;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutUnprepareHeader(HWAVEOUT out, LPWAVEHDR header,
                                           UINT) {
    if (!deviceFor(out)) return MMSYSERR_INVALHANDLE;
    if (!header) return MMSYSERR_ERROR;
    if (header->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
    header->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutWrite(HWAVEOUT out, LPWAVEHDR header, UINT) {
    Device * device = deviceFor(out);
    if (!device) return MMSYSERR_INVALHANDLE;
    if (!header || !header->lpData) return MMSYSERR_ERROR;

    const DWORD loops = (header->dwFlags & WHDR_BEGINLOOP) ? header->dwLoops : 1;
    device->header = header;
    device->endsAt = hostNow() +
        durationOf(device->format, header->dwBufferLength, loops);
    header->dwFlags &= ~WHDR_DONE;
    header->dwFlags |= WHDR_INQUEUE;

    device->source = jsWaveStart(
        (uintptr_t)header->lpData, (int)header->dwBufferLength,
        device->format.nChannels ? device->format.nChannels : 1,
        device->format.nSamplesPerSec ? device->format.nSamplesPerSec : 11025,
        device->format.wBitsPerSample ? device->format.wBitsPerSample : 8,
        (int)loops);

    // Nothing was played - no audio context, or a format that could not be
    // built - so the header comes back on the next pump rather than never.
    if (!device->source) device->endsAt = hostNow();
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutReset(HWAVEOUT out) {
    Device * device = deviceFor(out);
    if (!device) return MMSYSERR_INVALHANDLE;
    if (device->source) jsWaveStop(device->source);
    finish(*device, out);
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutPause(HWAVEOUT out) {
    return deviceFor(out) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
}

extern "C" MMRESULT waveOutRestart(HWAVEOUT out) {
    return deviceFor(out) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
}

extern "C" MMRESULT waveOutGetVolume(HWAVEOUT out, DWORD * volume) {
    Device * device = deviceFor(out);
    if (volume) *volume = device ? device->volume : 0xffffffffu;
    return MMSYSERR_NOERROR;
}

extern "C" MMRESULT waveOutSetVolume(HWAVEOUT out, DWORD volume) {
    if (Device * device = deviceFor(out)) device->volume = volume;
    return MMSYSERR_NOERROR;
}

}   // namespace shim
