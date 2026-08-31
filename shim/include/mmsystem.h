// The waveOut subset the port uses.
#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef UINT MMRESULT;
DECLARE_HANDLE(HWAVEOUT);
typedef HWAVEOUT * LPHWAVEOUT;
typedef UINT_PTR DWORD_PTR;

#define MMSYSERR_NOERROR     0
#define MMSYSERR_ERROR       1
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_ALLOCATED   4
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_NOMEM       7
#define WAVERR_STILLPLAYING  33

#define WAVE_FORMAT_PCM   1
#define WAVE_MAPPER       ((UINT)-1)
#define CALLBACK_NULL     0x00000000
#define CALLBACK_FUNCTION 0x00030000
#define WHDR_DONE         0x00000001
#define WHDR_PREPARED     0x00000002
#define WHDR_BEGINLOOP    0x00000004
#define WHDR_ENDLOOP      0x00000008
#define WHDR_INQUEUE      0x00000010

#define WOM_OPEN  0x03BB
#define WOM_CLOSE 0x03BC
#define WOM_DONE  0x03BD

typedef struct tWAVEFORMATEX {
    WORD  wFormatTag, nChannels;
    DWORD nSamplesPerSec, nAvgBytesPerSec;
    WORD  nBlockAlign, wBitsPerSample, cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX * LPCWAVEFORMATEX;

typedef struct wavehdr_tag {
    LPSTR     lpData;
    DWORD     dwBufferLength;
    DWORD     dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD     dwFlags;
    DWORD     dwLoops;
    struct wavehdr_tag * lpNext;
    DWORD_PTR reserved;
} WAVEHDR, *LPWAVEHDR;

MMRESULT waveOutOpen(HWAVEOUT * out, UINT deviceId, LPCWAVEFORMATEX format,
                     DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
MMRESULT waveOutClose(HWAVEOUT out);
MMRESULT waveOutPrepareHeader(HWAVEOUT out, LPWAVEHDR header, UINT size);
MMRESULT waveOutUnprepareHeader(HWAVEOUT out, LPWAVEHDR header, UINT size);
MMRESULT waveOutWrite(HWAVEOUT out, LPWAVEHDR header, UINT size);
MMRESULT waveOutReset(HWAVEOUT out);
MMRESULT waveOutPause(HWAVEOUT out);
MMRESULT waveOutRestart(HWAVEOUT out);
MMRESULT waveOutGetVolume(HWAVEOUT out, DWORD * volume);
MMRESULT waveOutSetVolume(HWAVEOUT out, DWORD volume);
UINT     waveOutGetNumDevs(void);

#ifdef __cplusplus
}
#endif
