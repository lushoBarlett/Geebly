#pragma once

#include "SDL2/SDL_audio.h"

// SPU_NATIVE_SAMPLERATE in Hertz
#define SPU_NATIVE_SAMPLERATE 2000000 // 2 MHz
#define SPU_DEVICE_SAMPLERATE 48000
#define SPU_BUFFER_SIZE 32
#define SPU_RESAMPLER_FILL_SIZE ((size_t)(SPU_BUFFER_SIZE * ((double)SPU_NATIVE_SAMPLERATE / (double)SPU_DEVICE_SAMPLERATE)))

#define SPU_BEGIN 0xff10
#define SPU_END 0xff3f

#define SPUNR_SWPC 0x0
#define SPUNR_LENC 0x1
#define SPUNR_ENVC 0x2
#define SPUNR_FREQ 0x3
#define SPUNR_CTRL 0x4

#define CTRL_RESTR 0x80
#define CTRL_LENCT 0x40
#define CTRL_FREQH 0x7

#define ENVC_STVOL 0xf0
#define ENVC_DIRCT 0x8
#define ENVC_ENVSN 0x7

#define LENC_WDUTY 0xc0
#define LENC_LENCT 0x3f

#define TEST_REG(r, m) (nr[r] & m)
#define RESET_REG(r, m) nr[r] &= (~m)

#include <cstdint>

namespace gameboy {
    namespace spu {
        namespace detail {
            template <typename T> inline int sign(T val) {
                return (T(0) < val) - (val < T(0));
            }
        }
    }
}