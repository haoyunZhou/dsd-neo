// SPDX-License-Identifier: GPL-3.0-or-later
#include <dsd-neo/protocol/tetra/tetra_acelp.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/runtime/udp_audio_hooks.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/runtime/decode_clock.h>
#include <sndfile.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* EN 300 395-2 V1.3.1 Table 4 bit-position tables for TCH/FS — from osmo-tetra tch_reordering.c */

/* 51 Class-0 (unprotected) bit positions, 1-indexed */
static const uint8_t class0_positions[] = {
    35, 36, 37, 38, 39, 40, 41, 42, 43, 47, 48,
    56, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 74,
    75, 83, 88, 89, 90, 91, 92, 93, 94, 95, 96,
    97, 101, 102, 110, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 128, 129, 137
};

/* 56 Class-1 bit positions, 1-indexed */
static const uint8_t class1_positions[] = {
    58, 85, 112,
    54, 81, 108, 135,
    50, 77,
    104, 131,
    45, 72, 99, 126,
    55, 82, 109, 136,
    5, 13, 34,
    8, 16, 17, 22, 23, 24, 25, 26,
    6, 14, 7, 15,
    60, 87, 114,
    46,
    73, 100, 127,
    44, 71, 98, 125,
    33, 49,
    76, 103, 130,
    59, 86, 113,
    57, 84, 111
};

/* 30 Class-2 (least protected) bit positions, 1-indexed */
static const uint8_t class2_positions[] = {
    18, 19, 20, 21,
    31, 32,
    53, 80, 107, 134,
    1, 2, 3, 4,
    9, 10, 11, 12,
    27, 28, 29, 30,
    52, 79, 106, 133,
    51, 78, 105, 132
};
/* Compute counts from arrays to avoid mismatches between defines and initializers */
enum {
    NUM_ACELP_CLASS0_BITS = sizeof(class0_positions) / sizeof(class0_positions[0]),
    NUM_ACELP_CLASS1_BITS = sizeof(class1_positions) / sizeof(class1_positions[0]),
    NUM_ACELP_CLASS2_BITS = sizeof(class2_positions) / sizeof(class2_positions[0])
};
#define NUM_ACELP_BITS (NUM_ACELP_CLASS0_BITS + NUM_ACELP_CLASS1_BITS + NUM_ACELP_CLASS2_BITS)

/* TETRA TCH/FS speech frame parameters (ETSI EN 300 395-2) */
#define TETRA_TCH_FRAME_BITS     137  /* encoded bits in one ACELP codec frame       */
#define TETRA_TCH_FRAME_SAMPLES  240  /* decoded PCM16 samples (30 ms @ 8 kHz)        */
static int s_voc_builtin = 0;

#ifndef DSD_HAVE_TETRA_ACELP_BUILTIN
#define DSD_HAVE_TETRA_ACELP_BUILTIN 0
#endif

#if DSD_HAVE_TETRA_ACELP_BUILTIN
extern void tetra_acelp_builtin_reset(void);
extern int tetra_acelp_builtin_decode(const uint8_t *bits, int nbits, int16_t *pcm, int nsamples);
#endif

/* Convert decoded TYPE2 bits (at least 274 bits) into two consecutive codec frames.
 * Each frame occupies TETRA_TCH_FRAME_BITS (137) bytes in the output buffer.
 * The output buffer must be at least 2 * TETRA_TCH_FRAME_BITS = 274 bytes.
 *
 * The class-reordered TYPE2 layout is (ETSI EN 300 395-2 Table 4):
 *   [class0_frame0, class0_frame1, class0_frame0, class0_frame1, ...]  × 51 pairs
 *   [class1 interleaved]  × 56 pairs
 *   [class2 interleaved]  × 30 pairs
 * Total: 2 × (51+56+30) = 274 input bits consumed.
 */
void tetra_acelp_reorder(const uint8_t* in, uint8_t* out, int len) {
    if (!in || !out || len < 2 * NUM_ACELP_BITS)
        return;
    const uint8_t *in_cur = in;
    int bit, frame;

    /* Class0 */
    for (bit = 0; bit < NUM_ACELP_CLASS0_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame * TETRA_TCH_FRAME_BITS + class0_positions[bit] - 1] = in_cur[2*bit + frame];
    }
    in_cur += 2*NUM_ACELP_CLASS0_BITS;

    /* Class1 */
    for (bit = 0; bit < NUM_ACELP_CLASS1_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame * TETRA_TCH_FRAME_BITS + class1_positions[bit] - 1] = in_cur[2*bit + frame];
    }
    in_cur += 2*NUM_ACELP_CLASS1_BITS;

    /* Class2 */
    for (bit = 0; bit < NUM_ACELP_CLASS2_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame * TETRA_TCH_FRAME_BITS + class2_positions[bit] - 1] = in_cur[2*bit + frame];
    }
}

/* Half-slot speech (frame stealing): one frame, same class order as table 6 of
 * EN 300 395-2 (51 class 0, 56 class 1, 30 class 2), no A/B interleave. */
static void tetra_acelp_reorder_half(const uint8_t* in, uint8_t* out) {
    const uint8_t *cur = in;
    for (int bit = 0; bit < NUM_ACELP_CLASS0_BITS; bit++)
        out[class0_positions[bit] - 1] = cur[bit];
    cur += NUM_ACELP_CLASS0_BITS;
    for (int bit = 0; bit < NUM_ACELP_CLASS1_BITS; bit++)
        out[class1_positions[bit] - 1] = cur[bit];
    cur += NUM_ACELP_CLASS1_BITS;
    for (int bit = 0; bit < NUM_ACELP_CLASS2_BITS; bit++)
        out[class2_positions[bit] - 1] = cur[bit];
}

/* Speech uses only the ETSI ACELP decoder linked into this process. */
static int voc_ensure_open(void) {
    if (s_voc_builtin)
        return 1;
#if DSD_HAVE_TETRA_ACELP_BUILTIN
    s_voc_builtin = 1;
    fprintf(stderr, "[TETRA] ACELP decoder linked in-process\n");
    return 1;
#else
    return 0;
#endif
}

void tetra_vocoder_close(void) {
    if (!s_voc_builtin)
        return;
#if DSD_HAVE_TETRA_ACELP_BUILTIN
    tetra_acelp_builtin_reset();
#endif
    s_voc_builtin = 0;
}

static const char *
tetra_audio_backend_name(const dsd_opts *opts) {
    if (!opts)
        return "unknown";

    switch (opts->audio_out_type) {
        case 0: return "pulse";
        case 1: return "file/stdout";
        case 8: return "udp";
        default: return "other";
    }
}

static int
tetra_has_live_audio_route(const dsd_opts *opts) {
    if (!opts)
        return 0;
    if (opts->slot1_on != 1)
        return 0;

    if (opts->audio_out_type == 0)
        return opts->audio_out_stream != NULL;
    if (opts->audio_out_type == 1)
        return opts->audio_out_fd >= 0;
    if (opts->audio_out_type == 8)
        return 1;
    return 0;
}

static int
tetra_has_any_audio_sink(const dsd_opts *opts) {
    if (!opts)
        return 0;
    if (tetra_has_live_audio_route(opts))
        return 1;
    if (opts->wav_out_f != NULL && (opts->dmr_stereo_wav == 1 || opts->static_wav_file == 1))
        return 1;
    return 0;
}

/* tetra_acelp_decode() kept for backward ABI compat; no longer does anything.
 * Callers should use tetra_acelp_process_tch() instead.
 */
void tetra_acelp_decode(const uint8_t* bits, int len) {
    (void)bits; (void)len;
}

/* -------------------------------------------------------------------------
 * tetra_acelp_process_tch()
 *
 * Full TCH/FS voice pipeline for the 274 speech bits decoded
 * from both NDB blocks:
 *
 *   type-2 bits → class reorder → 2 × ACELP codec frames
 *               → in-process ETSI ACELP decoder
 *               → PCM16 routing (PA / UDP / raw FD / WAV)
 *
 * The reorder produces two 137-bit ACELP frames from all 274
 * type-2 bits (102 class-0 + 112 class-1 + 60 class-2, interleaved
 * for two frames).  Each frame is sent to the vocoder independently.
 *
 * @type2_bits : decoded sensitivity-ordered bits (at least 274 required)
 * @type2_len  : number of valid bits
 * @block_idx  : 0 for combined TCH/FS (informational)
 * @opts/state : standard dsd-neo context
 * ------------------------------------------------------------------------- */
static void tetra_acelp_process_impl(const uint8_t *type2_bits, int type2_len,
                                     int block_idx, int half,
                                     dsd_opts *opts, dsd_state *state);

void tetra_acelp_process_tch(const uint8_t *type2_bits, int type2_len,
                              int block_idx,
                              dsd_opts *opts, dsd_state *state)
{
    tetra_acelp_process_impl(type2_bits, type2_len, block_idx, 0, opts, state);
}

void tetra_acelp_process_tch_half(const uint8_t *type2_bits, int type2_len,
                                  dsd_opts *opts, dsd_state *state)
{
    tetra_acelp_process_impl(type2_bits, type2_len, 0, 1, opts, state);
}

static void tetra_acelp_process_impl(const uint8_t *type2_bits, int type2_len,
                                     int block_idx, int half,
                                     dsd_opts *opts, dsd_state *state)
{
    static int warned_gate = 0;
    static int warned_open_failed = 0;
    static int warned_no_sink = 0;
    static int warned_control_channel = 0;
    static double last_live_audio_time = 0.0;
    static dsd_audio_stream *last_live_audio_stream = NULL;

    if (!type2_bits || !opts) {
        fprintf(stderr, "[TETRA] cannot process TCH without bits and decoder options\n");
        return;
    }

    if (!tetra_acelp_channel_gate_passes(opts)) {
        if (!warned_control_channel) {
            fprintf(stderr, "[TETRA] waiting for a traffic-channel grant; control-channel bursts are not speech\n");
            warned_control_channel = 1;
        }
        return;
    }

    /* Phase 12 floor-grant gate. */
    if (!tetra_acelp_slot_gate_passes(block_idx ? block_idx : 1, state)) {
        if (!warned_gate) {
            fprintf(stderr,
                "[TETRA] audio suppressed by slot gate: grant_valid=%u vc_slot=%u block=%d\n",
                state ? (unsigned)state->tetra_tx_granted_valid : 0u,
                state ? (unsigned)state->tetra_vc_slot : 0u,
                block_idx ? block_idx : 1);
            warned_gate = 1;
        }
        return;
    }

    const int need_bits = half ? TETRA_TCH_FRAME_BITS : 2 * TETRA_TCH_FRAME_BITS;
    if (type2_len < need_bits) {
        fprintf(stderr,
            "[TETRA TCH] B%d: only %d type-2 bits (need %d), skipping\n",
            block_idx, type2_len, need_bits);
        return;
    }

    /* Reception activity is independent of codec availability. Do not let
     * generic no-carrier handling use a stale pre-call activity timestamp. */
    dsd_mark_vc_sync(state);

    /* —— Step 1: class reorder → 2 codec frames —— */
    uint8_t codec[2 * TETRA_TCH_FRAME_BITS];  /* 274 bytes */
    memset(codec, 0, sizeof(codec));
    if (half)
        tetra_acelp_reorder_half(type2_bits, codec);
    else
        tetra_acelp_reorder(type2_bits, codec, type2_len);

    /* —— Step 2: vocoder + audio routing for each frame —— */
    if (!voc_ensure_open()) {
        if (state) {
            state->tetra_vocoder_status = TETRA_VOCODER_STATUS_START_FAILED;
            state->tetra_vocoder_errors++;
        }
        if (!warned_open_failed) {
            fprintf(stderr,
                "[TETRA] audio disabled: the in-process ACELP decoder is not linked. "
                "Configure ETSI_TETRA_C_CODE and rebuild.\n");
            warned_open_failed = 1;
        }
        return;
    }

    if (state) state->tetra_vocoder_status = TETRA_VOCODER_STATUS_READY;

    if (!tetra_has_any_audio_sink(opts)) {
        if (!warned_no_sink) {
            fprintf(stderr,
                "[TETRA] vocoder is producing PCM but no audio sink is active: "
                "audio_out=%d backend=%s slot1_on=%d stream=%s fd=%d wav=%s static_wav=%d\n",
                opts ? opts->audio_out : -1,
                tetra_audio_backend_name(opts),
                opts ? opts->slot1_on : -1,
                (opts && opts->audio_out_stream) ? "yes" : "no",
                opts ? opts->audio_out_fd : -1,
                (opts && opts->wav_out_f) ? "yes" : "no",
                opts ? opts->static_wav_file : 0);
            warned_no_sink = 1;
        }
    }

    /* ACELP arrives in 60 ms radio bursts, while PortAudio consumes 20 ms
     * chunks. Prime the live ring after an idle gap so a small scheduling
     * delay does not turn the first speech burst into an underrun. Recorded
     * PCM remains untouched. */
    if (opts->audio_out_type == 0 && opts->audio_out_stream &&
        opts->slot1_on == 1 && opts->pulse_digi_rate_out == 8000) {
        const double now = dsd_realtime_mono_s();
        if (last_live_audio_stream != opts->audio_out_stream ||
            last_live_audio_time <= 0.0 || now - last_live_audio_time > 0.25) {
            const int16_t prefill[640] = {0}; /* 80 ms at 8 kHz */
            dsd_audio_write(opts->audio_out_stream, prefill, 640);
        }
        last_live_audio_stream = opts->audio_out_stream;
        last_live_audio_time = now;
    }

    for (int f = 0; f < (half ? 1 : 2); f++) {
        const uint8_t *frame = codec + f * TETRA_TCH_FRAME_BITS;

        int16_t pcm[TETRA_TCH_FRAME_SAMPLES];
        memset(pcm, 0, sizeof(pcm));
        int nsamples = 0;
#if DSD_HAVE_TETRA_ACELP_BUILTIN
        nsamples = tetra_acelp_builtin_decode(frame, TETRA_TCH_FRAME_BITS,
                                              pcm, TETRA_TCH_FRAME_SAMPLES);
#endif
        if (nsamples != TETRA_TCH_FRAME_SAMPLES) {
            if (state) {
                state->tetra_vocoder_status = TETRA_VOCODER_STATUS_SHORT_OUTPUT;
                state->tetra_vocoder_errors++;
            }
            fprintf(stderr,
                    "[TETRA] vocoder returned a short PCM frame (%d/%d samples); resetting\n",
                    nsamples, TETRA_TCH_FRAME_SAMPLES);
            tetra_vocoder_close();
            return;
        }
        if (state) state->tetra_vocoder_frames++;

        /* —— Step 3: audio routing (same pattern as M17 / Codec2) —— */
        if (opts->slot1_on == 1) {
            if (opts->audio_out_type == 0 && opts->audio_out_stream)
                dsd_audio_write(opts->audio_out_stream, pcm, (size_t)nsamples);
            if (opts->audio_out_type == 8)
                dsd_udp_audio_hook_blast(opts, state,
                                         (size_t)nsamples * sizeof(int16_t), pcm);
            if (opts->audio_out_type == 1 && opts->audio_out_fd >= 0)
                dsd_write(opts->audio_out_fd, pcm,
                          (size_t)nsamples * sizeof(int16_t));
        }

        if (opts->wav_out_f != NULL && opts->dmr_stereo_wav == 1)
            sf_write_short(opts->wav_out_f, pcm, nsamples);

        if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
            short ss[TETRA_TCH_FRAME_SAMPLES * 2];
            for (int i = 0; i < nsamples && i < TETRA_TCH_FRAME_SAMPLES; i++) {
                ss[i * 2 + 0] = pcm[i];
                ss[i * 2 + 1] = pcm[i];
            }
            sf_write_short(opts->wav_out_f, ss, nsamples * 2);
        }
    }
}
