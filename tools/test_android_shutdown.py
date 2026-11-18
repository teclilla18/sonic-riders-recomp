#!/usr/bin/env python3
"""Compile the actual Vulkan quit/prewarm helpers and SDL audio implementation with checked mocks.

Tests an in-flight compiler call and audio producer, cancellation, join/order, and no SDL calls
after close. No disc or generated game code is needed. --android produces an ARM64 PIE for adb.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from test_vk_legacy import function

ROOT = Path(__file__).resolve().parents[1]
SDL_MOCK = r'''#pragma once
#include <stdint.h>
typedef uint32_t SDL_AudioDeviceID;
typedef struct { int freq; uint16_t format; uint8_t channels; uint16_t samples; } SDL_AudioSpec;
typedef struct TestSDLThread SDL_Thread;
typedef struct TestSDLWindow SDL_Window;
#define SDL_INIT_AUDIO 0x10u
#define AUDIO_S16SYS 0x8010u
uint32_t SDL_WasInit(uint32_t);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int);
void SDL_CloseAudioDevice(SDL_AudioDeviceID);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID);
uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID);
int SDL_QueueAudio(SDL_AudioDeviceID, const void*, uint32_t);
const char* SDL_GetError(void);
void SDL_WaitThread(SDL_Thread*, int*);
void SDL_DestroyWindow(SDL_Window*);
void SDL_Quit(void);
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    ap.add_argument('--vulkan-include', required=True)
    ap.add_argument('--android', action='store_true')
    a = ap.parse_args()
    out = ROOT / 'build/tests/android-shutdown'
    include = out / 'include'
    include.mkdir(parents=True, exist_ok=True)
    for name in ('vulkan', 'vk_video'):
        p = Path(a.vulkan_include) / name
        if p.is_dir(): shutil.copytree(p, include / name, dirs_exist_ok=True)
    (include / 'SDL.h').write_text(SDL_MOCK)
    source = (ROOT / 'runtime/src/platform/android/render_vk.c').read_text()
    (include / 'shutdown_under_test.h').write_text(''.join(function(source, n) for n in
        ('pipe_drain', 'prewarm_thread', 'prewarm_stop', 'shutdown_note', 'renderer_shutdown')))
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-I'+str(include), '-I'+str(ROOT/'runtime/src'), '-I'+str(ROOT/'runtime/include')]
    if a.android: flags += ['--target=aarch64-linux-android30']
    elif os.name == 'nt': flags += ['-I'+str(ROOT/'runtime/include/compat')]
    else: flags += ['-pthread']
    audio = out / 'audio.o'
    subprocess.run([a.cc, *flags, '-c', str(ROOT/'runtime/src/platform/host/audio_sdl.c'), '-o', str(audio)], check=True)
    exe = out / ('test-android-shutdown' if a.android or os.name != 'nt' else 'test-android-shutdown.exe')
    subprocess.run([a.cc, *flags, str(ROOT/'tools/test_android_shutdown.c'), str(audio), '-o', str(exe)], check=True)
    assert 'while (!g_nfree || g_stopping)' in source and 'while (g_pending || g_stopping)' in source
    assert 'SDL_CreateThreadWithStackSize(prewarm_thread' in source and 'rt_thread_start(prewarm_thread' not in source
    if not a.android: subprocess.run([str(exe)], check=True, timeout=20)
    print('Shutdown regression executable:', exe)


if __name__ == '__main__': main()
