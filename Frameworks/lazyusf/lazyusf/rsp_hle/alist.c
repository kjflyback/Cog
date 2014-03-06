/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist.c                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../usf.h"

#include "alist_internal.h"
#include "arithmetics.h"
#include "audio.h"
#include "memory.h"
#include "plugin.h"

#include "../usf_internal.h"

struct ramp_t
{
    int64_t value;
    int64_t step;
    int64_t target;
};

/* local functions */
static void swap(int16_t **a, int16_t **b)
{
    int16_t* tmp = *b;
    *b = *a;
    *a = tmp;
}

static int16_t* sample(usf_state_t * state, unsigned pos)
{
    return (int16_t*)state->BufferSpace + (pos ^ S);
}

static void sample_mix(int16_t* dst, int16_t src, int16_t gain)
{
    *dst = clamp_s16(*dst + ((src * gain) >> 15));
}

static void alist_envmix_mix(size_t n, int16_t** dst, const int16_t* gains, int16_t src)
{
    size_t i;

    for(i = 0; i < n; ++i)
        sample_mix(dst[i], src, gains[i]);
}

static int16_t ramp_step(struct ramp_t* ramp)
{
    ramp->value += ramp->step;

    bool target_reached = (ramp->step <= 0)
        ? (ramp->value <= ramp->target)
        : (ramp->value >= ramp->target);

    if (target_reached)
    {
        ramp->value = ramp->target;
        ramp->step  = 0;
    }

    return (ramp->value >> 16);
}

/* global functions */
void alist_process(usf_state_t* state, const acmd_callback_t abi[], unsigned int abi_size)
{
    uint32_t w1, w2;
    unsigned int acmd;

    const uint32_t *alist = dram_u32(state, *dmem_u32(state, TASK_DATA_PTR));
    const uint32_t *const alist_end = alist + (*dmem_u32(state, TASK_DATA_SIZE) >> 2);

    while (alist != alist_end) {
        w1 = *(alist++);
        w2 = *(alist++);

        acmd = (w1 >> 24) & 0x7f;

        if (acmd < abi_size)
            (*abi[acmd])(state, w1, w2);
        else
            DebugMessage(state, M64MSG_WARNING, "Invalid ABI command %u", acmd);
    }
}

uint32_t alist_get_address(usf_state_t* state, uint32_t so, const uint32_t *segments, size_t n)
{
    uint8_t  segment = (so >> 24);
    uint32_t offset  = (so & 0xffffff);

    if (segment >= n) {
        DebugMessage(state, M64MSG_WARNING, "Invalid segment %u", segment);
        return offset;
    }

    return segments[segment] + offset;
}

void alist_set_address(usf_state_t* state, uint32_t so, uint32_t *segments, size_t n)
{
    uint8_t  segment = (so >> 24);
    uint32_t offset  = (so & 0xffffff);

    if (segment >= n) {
        DebugMessage(state, M64MSG_WARNING, "Invalid segment %u", segment);
        return;
    }

    segments[segment] = offset;
}

void alist_clear(usf_state_t* state, uint16_t dmem, uint16_t count)
{
    while(count != 0) {
        state->BufferSpace[(dmem++)^S8] = 0;
        --count;
    }
}

void alist_load(usf_state_t* state, uint16_t dmem, uint32_t address, uint16_t count)
{
    /* enforce DMA alignment constraints */
    dmem    &= ~3;
    address &= ~7;
    count = align(count, 8);
    memcpy(state->BufferSpace + dmem, state->N64MEM + address, count);
}

void alist_save(usf_state_t* state, uint16_t dmem, uint32_t address, uint16_t count)
{
    /* enforce DMA alignment constraints */
    dmem    &= ~3;
    address &= ~7;
    count = align(count, 8);
    memcpy(state->N64MEM + address, state->BufferSpace + dmem, count);
}

void alist_move(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    while (count != 0) {
        state->BufferSpace[(dmemo++)^S8] = state->BufferSpace[(dmemi++)^S8];
        --count;
    }
}

void alist_copy_every_other_sample(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    while (count != 0) {
        *(uint16_t*)(state->BufferSpace + (dmemo^S8)) = *(uint16_t*)(state->BufferSpace + (dmemi^S8));
        dmemo += 2;
        dmemi += 4;
        --count;
    }
}

void alist_repeat64(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint8_t count)
{
    uint16_t buffer[64];

    memcpy(buffer, state->BufferSpace + dmemi, 128);

    while(count != 0) {
        memcpy(state->BufferSpace + dmemo, buffer, 128);
        dmemo += 128;
        --count;
    }
}

void alist_copy_blocks(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint16_t block_size, uint8_t count)
{
    int block_left = count;

    do
    {
        int bytes_left = block_size;

        do
        {
            memcpy(state->BufferSpace + dmemo, state->BufferSpace + dmemi, 0x20);
            bytes_left -= 0x20;

            dmemi += 0x20;
            dmemo += 0x20;

        } while(bytes_left > 0);

        --block_left;
    } while(block_left > 0);
}

void alist_interleave(usf_state_t* state, uint16_t dmemo, uint16_t left, uint16_t right, uint16_t count)
{
    uint16_t       *dst  = (uint16_t*)(state->BufferSpace + dmemo);
    const uint16_t *srcL = (uint16_t*)(state->BufferSpace + left);
    const uint16_t *srcR = (uint16_t*)(state->BufferSpace + right);

    count >>= 2;

    while(count != 0) {
        uint16_t l1 = *(srcL++);
        uint16_t l2 = *(srcL++);
        uint16_t r1 = *(srcR++);
        uint16_t r2 = *(srcR++);

#if M64P_BIG_ENDIAN
        *(dst++) = l1;
        *(dst++) = r1;
        *(dst++) = l2;
        *(dst++) = r2;
#else
        *(dst++) = r2;
        *(dst++) = l2;
        *(dst++) = r1;
        *(dst++) = l1;
#endif
        --count;
    }
}


void alist_envmix_exp(
        usf_state_t* state,
        bool init,
        bool aux,
        uint16_t dmem_dl, uint16_t dmem_dr,
        uint16_t dmem_wl, uint16_t dmem_wr,
        uint16_t dmemi, uint16_t count,
        int16_t dry, int16_t wet,
        const int16_t *vol,
        const int16_t *target,
        const int32_t *rate,
        uint32_t address)
{
    size_t n = (aux) ? 4 : 2;

    const int16_t* const in = (int16_t*)(state->BufferSpace + dmemi);
    int16_t* const dl = (int16_t*)(state->BufferSpace + dmem_dl);
    int16_t* const dr = (int16_t*)(state->BufferSpace + dmem_dr);
    int16_t* const wl = (int16_t*)(state->BufferSpace + dmem_wl);
    int16_t* const wr = (int16_t*)(state->BufferSpace + dmem_wr);

    struct ramp_t ramps[2];
    int32_t exp_seq[2];
    int32_t exp_rates[2];

    uint32_t ptr = 0;
    int x, y;
    short save_buffer[40];

    if (init) {
        ramps[0].value  = (vol[0] << 16);
        ramps[1].value  = (vol[1] << 16);
        ramps[0].target = (target[0] << 16);
        ramps[1].target = (target[1] << 16);
        exp_rates[0]    = rate[0];
        exp_rates[1]    = rate[1];
        exp_seq[0]      = (vol[0] * rate[0]);
        exp_seq[1]      = (vol[1] * rate[1]);
    } else {
        memcpy((uint8_t *)save_buffer, (state->N64MEM + address), 80);
        wet             = *(int16_t *)(save_buffer +  0); /* 0-1 */
        dry             = *(int16_t *)(save_buffer +  2); /* 2-3 */
        ramps[0].target = *(int32_t *)(save_buffer +  4); /* 4-5 */
        ramps[1].target = *(int32_t *)(save_buffer +  6); /* 6-7 */
        exp_rates[0]    = *(int32_t *)(save_buffer +  8); /* 8-9 (save_buffer is a 16bit pointer) */
        exp_rates[1]    = *(int32_t *)(save_buffer + 10); /* 10-11 */
        exp_seq[0]      = *(int32_t *)(save_buffer + 12); /* 12-13 */
        exp_seq[1]      = *(int32_t *)(save_buffer + 14); /* 14-15 */
        ramps[0].value  = *(int32_t *)(save_buffer + 16); /* 12-13 */
        ramps[1].value  = *(int32_t *)(save_buffer + 18); /* 14-15 */
    }

    /* init which ensure ramp.step != 0 iff ramp.value == ramp.target */
    ramps[0].step = ramps[0].target - ramps[0].value;
    ramps[1].step = ramps[1].target - ramps[1].value;

    for (y = 0; y < count; y += 16) {

        if (ramps[0].step != 0)
        {
            exp_seq[0] = ((int64_t)exp_seq[0]*(int64_t)exp_rates[0]) >> 16;
            ramps[0].step = (exp_seq[0] - ramps[0].value) >> 3;
        }

        if (ramps[1].step != 0)
        {
            exp_seq[1] = ((int64_t)exp_seq[1]*(int64_t)exp_rates[1]) >> 16;
            ramps[1].step = (exp_seq[1] - ramps[1].value) >> 3;
        }

        for (x = 0; x < 8; ++x) {
            int16_t  gains[4];
            int16_t* buffers[4];
            int16_t l_vol = ramp_step(&ramps[0]);
            int16_t r_vol = ramp_step(&ramps[1]);

            buffers[0] = dl + (ptr^S);
            buffers[1] = dr + (ptr^S);
            buffers[2] = wl + (ptr^S);
            buffers[3] = wr + (ptr^S);

            gains[0] = clamp_s16((l_vol * dry + 0x4000) >> 15);
            gains[1] = clamp_s16((r_vol * dry + 0x4000) >> 15);
            gains[2] = clamp_s16((l_vol * wet + 0x4000) >> 15);
            gains[3] = clamp_s16((r_vol * wet + 0x4000) >> 15);

            alist_envmix_mix(n, buffers, gains, in[ptr^S]);
            ++ptr;
        }
    }

    *(int16_t *)(save_buffer +  0) = wet;               /* 0-1 */
    *(int16_t *)(save_buffer +  2) = dry;               /* 2-3 */
    *(int32_t *)(save_buffer +  4) = ramps[0].target;   /* 4-5 */
    *(int32_t *)(save_buffer +  6) = ramps[1].target;   /* 6-7 */
    *(int32_t *)(save_buffer +  8) = exp_rates[0];      /* 8-9 (save_buffer is a 16bit pointer) */
    *(int32_t *)(save_buffer + 10) = exp_rates[1];      /* 10-11 */
    *(int32_t *)(save_buffer + 12) = exp_seq[0];        /* 12-13 */
    *(int32_t *)(save_buffer + 14) = exp_seq[1];        /* 14-15 */
    *(int32_t *)(save_buffer + 16) = ramps[0].value;    /* 12-13 */
    *(int32_t *)(save_buffer + 18) = ramps[1].value;    /* 14-15 */
    memcpy(state->N64MEM + address, (uint8_t *)save_buffer, 80);
}

void alist_envmix_lin(
        usf_state_t* state,
        bool init,
        uint16_t dmem_dl, uint16_t dmem_dr,
        uint16_t dmem_wl, uint16_t dmem_wr,
        uint16_t dmemi, uint16_t count,
        int16_t dry, int16_t wet,
        const int16_t *vol,
        const int16_t *target,
        const int32_t *rate,
        uint32_t address)
{
    size_t k;
    struct ramp_t ramps[2];
    int16_t save_buffer[40];

    const int16_t * const in = (int16_t*)(state->BufferSpace + dmemi);
    int16_t* const dl = (int16_t*)(state->BufferSpace + dmem_dl);
    int16_t* const dr = (int16_t*)(state->BufferSpace + dmem_dr);
    int16_t* const wl = (int16_t*)(state->BufferSpace + dmem_wl);
    int16_t* const wr = (int16_t*)(state->BufferSpace + dmem_wr);

    if (init) {
        ramps[0].step   = rate[0] / 8;
        ramps[0].value  = (vol[0] << 16);
        ramps[0].target = (target[0] << 16);
        ramps[1].step   = rate[1] / 8;
        ramps[1].value  = (vol[1] << 16);
        ramps[1].target = (target[1] << 16);
    }
    else {
        memcpy((uint8_t *)save_buffer, state->N64MEM + address, 80);
        wet             = *(int16_t *)(save_buffer +  0); /* 0-1 */
        dry             = *(int16_t *)(save_buffer +  2); /* 2-3 */
        ramps[0].target = *(int16_t *)(save_buffer +  4) << 16; /* 4-5 */
        ramps[1].target = *(int16_t *)(save_buffer +  6) << 16; /* 6-7 */
        ramps[0].step   = *(int32_t *)(save_buffer +  8); /* 8-9 (save_buffer is a 16bit pointer) */
        ramps[1].step   = *(int32_t *)(save_buffer + 10); /* 10-11 */
        ramps[0].value  = *(int32_t *)(save_buffer + 16); /* 16-17 */
        ramps[1].value  = *(int32_t *)(save_buffer + 18); /* 16-17 */
    }

    count >>= 1;
    for(k = 0; k < count; ++k) {
        int16_t  gains[4];
        int16_t* buffers[4];
        int16_t l_vol = ramp_step(&ramps[0]);
        int16_t r_vol = ramp_step(&ramps[1]);

        buffers[0] = dl + (k^S);
        buffers[1] = dr + (k^S);
        buffers[2] = wl + (k^S);
        buffers[3] = wr + (k^S);

        gains[0] = clamp_s16((l_vol * dry + 0x4000) >> 15);
        gains[1] = clamp_s16((r_vol * dry + 0x4000) >> 15);
        gains[2] = clamp_s16((l_vol * wet + 0x4000) >> 15);
        gains[3] = clamp_s16((r_vol * wet + 0x4000) >> 15);

        alist_envmix_mix(4, buffers, gains, in[k^S]);
    }

    *(int16_t *)(save_buffer +  0) = wet;            /* 0-1 */
    *(int16_t *)(save_buffer +  2) = dry;            /* 2-3 */
    *(int16_t *)(save_buffer +  4) = ramps[0].target >> 16; /* 4-5 */
    *(int16_t *)(save_buffer +  6) = ramps[1].target >> 16; /* 6-7 */
    *(int32_t *)(save_buffer +  8) = ramps[0].step;  /* 8-9 (save_buffer is a 16bit pointer) */
    *(int32_t *)(save_buffer + 10) = ramps[1].step;  /* 10-11 */
    *(int32_t *)(save_buffer + 16) = ramps[0].value; /* 16-17 */
    *(int32_t *)(save_buffer + 18) = ramps[1].value; /* 18-19 */
    memcpy(state->N64MEM + address, (uint8_t *)save_buffer, 80);
}

void alist_envmix_nead(
        usf_state_t* state,
        bool swap_wet_LR,
        uint16_t dmem_dl,
        uint16_t dmem_dr,
        uint16_t dmem_wl,
        uint16_t dmem_wr,
        uint16_t dmemi,
        unsigned count,
        uint16_t *env_values,
        uint16_t *env_steps,
        const int16_t *xors)
{
    /* make sure count is a multiple of 8 */
    count = align(count, 8);

    int16_t *in = (int16_t*)(state->BufferSpace + dmemi);
    int16_t *dl = (int16_t*)(state->BufferSpace + dmem_dl);
    int16_t *dr = (int16_t*)(state->BufferSpace + dmem_dr);
    int16_t *wl = (int16_t*)(state->BufferSpace + dmem_wl);
    int16_t *wr = (int16_t*)(state->BufferSpace + dmem_wr);

    if (swap_wet_LR)
        swap(&wl, &wr);

    while (count != 0) {
        size_t i;
        for(i = 0; i < 8; ++i) {
            int16_t l  = (((int32_t)in[i^S] * (uint32_t)env_values[0]) >> 16) ^ xors[0];
            int16_t r  = (((int32_t)in[i^S] * (uint32_t)env_values[1]) >> 16) ^ xors[1];
            int16_t l2 = (((int32_t)l * (uint32_t)env_values[2]) >> 16) ^ xors[2];
            int16_t r2 = (((int32_t)r * (uint32_t)env_values[2]) >> 16) ^ xors[3];

            dl[i^S] = clamp_s16(dl[i^S] + l);
            dr[i^S] = clamp_s16(dr[i^S] + r);
            wl[i^S] = clamp_s16(wl[i^S] + l2);
            wr[i^S] = clamp_s16(wr[i^S] + r2);
        }

        env_values[0] += env_steps[0];
        env_values[1] += env_steps[1];
        env_values[2] += env_steps[2];

        dl += 8;
        dr += 8;
        wl += 8;
        wr += 8;
        in += 8;
        count -= 8;
    }
}


void alist_mix(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint16_t count, int16_t gain)
{
    int16_t       *dst = (int16_t*)(state->BufferSpace + dmemo);
    const int16_t *src = (int16_t*)(state->BufferSpace + dmemi);

    count >>= 1;

    while(count != 0) {
        sample_mix(dst, *src, gain);

        ++dst;
        ++src;
        --count;
    }
}

void alist_multQ44(usf_state_t* state, uint16_t dmem, uint16_t count, int8_t gain)
{
    int16_t *dst = (int16_t*)(state->BufferSpace + dmem);

    count >>= 1;

    while(count != 0) {
        *dst = clamp_s16(*dst * gain >> 4);

        ++dst;
        --count;
    }
}

void alist_add(usf_state_t* state, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    int16_t       *dst = (int16_t*)(state->BufferSpace + dmemo);
    const int16_t *src = (int16_t*)(state->BufferSpace + dmemi);

    count >>= 1;

    while(count != 0) {
        *dst = clamp_s16(*dst + *src);

        ++dst;
        ++src;
        --count;
    }
}

static void alist_resample_reset(usf_state_t* state, uint16_t pos, uint32_t* pitch_accu)
{
    unsigned k;

    for(k = 0; k < 4; ++k)
        *sample(state, pos + k) = 0;

    *pitch_accu = 0;
}

static void alist_resample_load(usf_state_t* state, uint32_t address, uint16_t pos, uint32_t* pitch_accu)
{
    *sample(state, pos + 0) = *dram_u16(state, address + 0);
    *sample(state, pos + 1) = *dram_u16(state, address + 2);
    *sample(state, pos + 2) = *dram_u16(state, address + 4);
    *sample(state, pos + 3) = *dram_u16(state, address + 6);

    *pitch_accu = *dram_u16(state, address + 8);
}

static void alist_resample_save(usf_state_t* state, uint32_t address, uint16_t pos, uint32_t pitch_accu)
{
    *dram_u16(state, address + 0) = *sample(state, pos + 0);
    *dram_u16(state, address + 2) = *sample(state, pos + 1);
    *dram_u16(state, address + 4) = *sample(state, pos + 2);
    *dram_u16(state, address + 6) = *sample(state, pos + 3);

    *dram_u16(state, address + 8) = pitch_accu;
}

void alist_resample(
        usf_state_t* state,
        bool init,
        bool flag2,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint32_t pitch,     /* Q16.16 */
        uint32_t address)
{
    uint32_t pitch_accu;

    uint16_t ipos = dmemi >> 1;
    uint16_t opos = dmemo >> 1;
    count >>= 1;
    ipos -= 4;

    if (flag2)
        DebugMessage(state, M64MSG_WARNING, "alist_resample: flag2 is not implemented");

    if (init)
        alist_resample_reset(state, ipos, &pitch_accu);
    else
        alist_resample_load(state, address, ipos, &pitch_accu);

    while (count != 0) {
        const int16_t* lut = RESAMPLE_LUT + ((pitch_accu & 0xfc00) >> 8);

        *sample(state, opos++) = clamp_s16(
                ((*sample(state, ipos    ) * lut[0]) >> 15) +
                ((*sample(state, ipos + 1) * lut[1]) >> 15) +
                ((*sample(state, ipos + 2) * lut[2]) >> 15) +
                ((*sample(state, ipos + 3) * lut[3]) >> 15));

        pitch_accu += pitch;
        ipos += (pitch_accu >> 16);
        pitch_accu &= 0xffff;
        --count;
    }

    alist_resample_save(state, address, ipos, pitch_accu);
}

void alist_resample_zoh(
        usf_state_t* state,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint32_t pitch,
        uint32_t pitch_accu)
{
    uint16_t ipos = dmemi >> 1;
    uint16_t opos = dmemo >> 1;
    count >>= 1;

    while(count != 0) {

        *sample(state, opos++) = *sample(state, ipos);

        pitch_accu += pitch;
        ipos += (pitch_accu >> 16);
        pitch_accu &= 0xffff;
        --count;
    }
}

typedef unsigned int (*adpcm_predict_frame_t)(usf_state_t* state, int16_t* dst, uint16_t dmemi, unsigned char scale);

static unsigned int adpcm_predict_frame_4bits(usf_state_t* state, int16_t* dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    unsigned int rshift = (scale < 12) ? 12 - scale : 0;

    for(i = 0; i < 8; ++i) {
        uint8_t byte = state->BufferSpace[(dmemi++)^S8];

        *(dst++) = adpcm_predict_sample(byte, 0xf0,  8, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x0f, 12, rshift);
    }

    return 8;
}

static unsigned int adpcm_predict_frame_2bits(usf_state_t* state, int16_t* dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    unsigned int rshift = (scale < 14) ? 14 - scale : 0;

    for(i = 0; i < 4; ++i) {
        uint8_t byte = state->BufferSpace[(dmemi++)^S8];

        *(dst++) = adpcm_predict_sample(byte, 0xc0,  8, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x30, 10, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x0c, 12, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x03, 14, rshift);
    }

    return 4;
}

void alist_adpcm(
        usf_state_t* state,
        bool init,
        bool loop,
        bool two_bit_per_sample,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        const int16_t* codebook,
        uint32_t loop_address,
        uint32_t last_frame_address)
{
    assert((count & 0x1f) == 0);

    int16_t last_frame[16];
    size_t i;

    if (init)
        memset(last_frame, 0, 16*sizeof(last_frame[0]));
    else
        dram_load_u16(state, (uint16_t*)last_frame, (loop) ? loop_address : last_frame_address, 16);

    for(i = 0; i < 16; ++i, dmemo += 2)
        *(int16_t*)(state->BufferSpace + (dmemo ^ S16)) = last_frame[i];

    adpcm_predict_frame_t predict_frame = (two_bit_per_sample)
        ? adpcm_predict_frame_2bits
        : adpcm_predict_frame_4bits;

    while (count != 0) {
        int16_t frame[16];
        uint8_t code = state->BufferSpace[(dmemi++)^S8];
        unsigned char scale = (code & 0xf0) >> 4;
        const int16_t* const cb_entry = codebook + ((code & 0xf) << 4);

        dmemi += predict_frame(state, frame, dmemi, scale);

        adpcm_compute_residuals(last_frame    , frame    , cb_entry, last_frame + 14, 8);
        adpcm_compute_residuals(last_frame + 8, frame + 8, cb_entry, last_frame + 6 , 8);

        for(i = 0; i < 16; ++i, dmemo += 2)
            *(int16_t*)(state->BufferSpace + (dmemo ^ S16)) = last_frame[i];

        count -= 32;
    }

    dram_store_u16(state, (uint16_t*)last_frame, last_frame_address, 16);
}


void alist_filter(usf_state_t* state, uint16_t dmem, uint16_t count, uint32_t address, const uint32_t* lut_address)
{
    int x;
    int16_t outbuff[0x3c0];
    int16_t *outp = outbuff;

    int16_t* const lutt6 = (int16_t*)(state->N64MEM + lut_address[0]);
    int16_t* const lutt5 = (int16_t*)(state->N64MEM + lut_address[1]);

    int16_t* in1 = (int16_t*)(state->N64MEM + address);
    int16_t* in2 = (int16_t*)(state->BufferSpace + dmem);


    for (x = 0; x < 8; ++x) {
        int32_t v = (lutt5[x] + lutt6[x]) >> 1;
        lutt5[x] = lutt6[x] = v;
    }

    for (x = 0; x < count; x += 16) {
        int32_t v[8];

        v[1] =  in1[0] * lutt6[6];
        v[1] += in1[3] * lutt6[7];
        v[1] += in1[2] * lutt6[4];
        v[1] += in1[5] * lutt6[5];
        v[1] += in1[4] * lutt6[2];
        v[1] += in1[7] * lutt6[3];
        v[1] += in1[6] * lutt6[0];
        v[1] += in2[1] * lutt6[1]; /* 1 */

        v[0] =  in1[3] * lutt6[6];
        v[0] += in1[2] * lutt6[7];
        v[0] += in1[5] * lutt6[4];
        v[0] += in1[4] * lutt6[5];
        v[0] += in1[7] * lutt6[2];
        v[0] += in1[6] * lutt6[3];
        v[0] += in2[1] * lutt6[0];
        v[0] += in2[0] * lutt6[1];

        v[3] =  in1[2] * lutt6[6];
        v[3] += in1[5] * lutt6[7];
        v[3] += in1[4] * lutt6[4];
        v[3] += in1[7] * lutt6[5];
        v[3] += in1[6] * lutt6[2];
        v[3] += in2[1] * lutt6[3];
        v[3] += in2[0] * lutt6[0];
        v[3] += in2[3] * lutt6[1];

        v[2] =  in1[5] * lutt6[6];
        v[2] += in1[4] * lutt6[7];
        v[2] += in1[7] * lutt6[4];
        v[2] += in1[6] * lutt6[5];
        v[2] += in2[1] * lutt6[2];
        v[2] += in2[0] * lutt6[3];
        v[2] += in2[3] * lutt6[0];
        v[2] += in2[2] * lutt6[1];

        v[5] =  in1[4] * lutt6[6];
        v[5] += in1[7] * lutt6[7];
        v[5] += in1[6] * lutt6[4];
        v[5] += in2[1] * lutt6[5];
        v[5] += in2[0] * lutt6[2];
        v[5] += in2[3] * lutt6[3];
        v[5] += in2[2] * lutt6[0];
        v[5] += in2[5] * lutt6[1];

        v[4] =  in1[7] * lutt6[6];
        v[4] += in1[6] * lutt6[7];
        v[4] += in2[1] * lutt6[4];
        v[4] += in2[0] * lutt6[5];
        v[4] += in2[3] * lutt6[2];
        v[4] += in2[2] * lutt6[3];
        v[4] += in2[5] * lutt6[0];
        v[4] += in2[4] * lutt6[1];

        v[7] =  in1[6] * lutt6[6];
        v[7] += in2[1] * lutt6[7];
        v[7] += in2[0] * lutt6[4];
        v[7] += in2[3] * lutt6[5];
        v[7] += in2[2] * lutt6[2];
        v[7] += in2[5] * lutt6[3];
        v[7] += in2[4] * lutt6[0];
        v[7] += in2[7] * lutt6[1];

        v[6] =  in2[1] * lutt6[6];
        v[6] += in2[0] * lutt6[7];
        v[6] += in2[3] * lutt6[4];
        v[6] += in2[2] * lutt6[5];
        v[6] += in2[5] * lutt6[2];
        v[6] += in2[4] * lutt6[3];
        v[6] += in2[7] * lutt6[0];
        v[6] += in2[6] * lutt6[1];

        outp[1] = ((v[1] + 0x4000) >> 15);
        outp[0] = ((v[0] + 0x4000) >> 15);
        outp[3] = ((v[3] + 0x4000) >> 15);
        outp[2] = ((v[2] + 0x4000) >> 15);
        outp[5] = ((v[5] + 0x4000) >> 15);
        outp[4] = ((v[4] + 0x4000) >> 15);
        outp[7] = ((v[7] + 0x4000) >> 15);
        outp[6] = ((v[6] + 0x4000) >> 15);
        in1 = in2;
        in2 += 8;
        outp += 8;
    }

    memcpy(state->N64MEM + address, in2 - 8, 16);
    memcpy(state->BufferSpace + dmem, outbuff, count);
}

void alist_polef(
        usf_state_t* state,
        bool init,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint16_t gain,
        int16_t* table,
        uint32_t address)
{
    int16_t *dst = (int16_t*)(state->BufferSpace + dmemo);

    const int16_t* const h1 = table;
          int16_t* const h2 = table + 8;

    unsigned i;
    int16_t l1, l2;
    int16_t h2_before[8];

    count = align(count, 16);

    if (init) {
        l1 = 0;
        l2 = 0;
    }
    else {
        l1 = *dram_u16(state, address + 4);
        l2 = *dram_u16(state, address + 6);
    }

    for(i = 0; i < 8; ++i) {
        h2_before[i] = h2[i];
        h2[i] = (((int32_t)h2[i] * gain) >> 14);
    }

    do
    {
        int16_t frame[8];

        for(i = 0; i < 8; ++i, dmemi += 2) {
            frame[i] = *(int16_t*)(state->BufferSpace + (dmemi ^ S16));
        }

        for(i = 0; i < 8; ++i) {
            int32_t accu = frame[i] * gain;
            accu += h1[i]*l1 + h2_before[i]*l2 + rdot(i, h2, frame);
            dst[i^S] = clamp_s16(accu >> 14);
        }

        l1 = dst[6^S];
        l2 = dst[7^S];

        dst += 8;
        count -= 16;
    } while (count != 0);

    dram_store_u16(state, (uint16_t*)(dst - 4), address, 4);
}
