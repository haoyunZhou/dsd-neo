// SPDX-License-Identifier: GPL-3.0-or-later
#include <dsd-neo/protocol/tetra/tetra_acelp.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/runtime/udp_audio_hooks.h>
#include <sndfile.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#endif

/* EN 300 395-2 V1.3.1 Table 4 bit-position tables for TCH/FS — from osmo-tetra tch_reordering.c */

/* 50 Class-0 (most protected) bit positions, 1-indexed */
static const uint8_t class0_positions[] = {
    35, 36, 37, 38, 39, 40, 41, 42, 33, 47, 48,
    56, 61, 62, 63, 65, 66, 67, 68, 69, 70, 74,
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
#define TETRA_TCH_FRAME_SAMPLES  160  /* decoded PCM16 samples (20ms @ 8 kHz)         */

/* Convert decoded TYPE2 bits (432 bits) into two consecutive codec frames (2*216 bits)
 * This matches osmo-tetra's tetra_acelp_type2_to_codec behavior.
 */
void tetra_acelp_reorder(const uint8_t* in, uint8_t* out, int len) {
    (void)len; /* we expect at least 2*NUM_ACELP_BITS but function uses provided arrays */
    const uint8_t *in_cur = in;
    int bit, frame;

    /* Class0 */
    for (bit = 0; bit < NUM_ACELP_CLASS0_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame*NUM_ACELP_BITS + class0_positions[bit] - 1] = in_cur[2*bit + frame];
    }
    in_cur += 2*NUM_ACELP_CLASS0_BITS;

    /* Class1 */
    for (bit = 0; bit < NUM_ACELP_CLASS1_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame*NUM_ACELP_BITS + class1_positions[bit] - 1] = in_cur[2*bit + frame];
    }
    in_cur += 2*NUM_ACELP_CLASS1_BITS;

    /* Class2 */
    for (bit = 0; bit < NUM_ACELP_CLASS2_BITS; bit++) {
        for (frame = 0; frame < 2; frame++)
            out[frame*NUM_ACELP_BITS + class2_positions[bit] - 1] = in_cur[2*bit + frame];
    }
}

/* =========================================================================
 * Persistent external vocoder subprocess
 *
 * The TETRA ACELP speech codec is proprietary (ETSI ETS 300 395-2) and has
 * no freely-available open-source implementation.  dsd-neo supports an
 * external command-line vocoder via the TETRA_VOCODER_CMD environment
 * variable.  The subprocess must implement this synchronous protocol:
 *
 *   → stdin  : TETRA_TCH_FRAME_BITS raw bytes (one byte per coded bit, 0/1)
 *   ← stdout : TETRA_TCH_FRAME_SAMPLES × sizeof(int16_t) bytes of PCM16LE
 *              (8 kHz, mono, little-endian)
 *   repeat indefinitely until stdin is closed.
 *
 * tetra-rx from osmo-tetra, or any wrapper around the codec binary,
 * can be used as the external vocoder command.
 * See tools/tetra/README.md and tools/tetra/vocoder_stub.py for a
 * silence-producing stub suitable for integration testing.
 * ========================================================================= */

#ifdef _WIN32
/* ---- Windows implementation ---- */
static struct {
    HANDLE h_write;   /* parent writes bits  → child stdin  */
    HANDLE h_read;    /* parent reads  PCM   ← child stdout */
    HANDLE h_proc;
    HANDLE h_thread;
    int    open;
} s_voc;

static int voc_open(const char *cmd) {
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE pipe_stdin_r,  pipe_stdin_w;
    HANDLE pipe_stdout_r, pipe_stdout_w;
    if (!CreatePipe(&pipe_stdin_r,  &pipe_stdin_w,  &sa, 0)) return 0;
    if (!CreatePipe(&pipe_stdout_r, &pipe_stdout_w, &sa, 0)) {
        CloseHandle(pipe_stdin_r); CloseHandle(pipe_stdin_w);
        return 0;
    }
    /* make the parent-side handles non-inheritable */
    SetHandleInformation(pipe_stdin_w,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pipe_stdout_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput  = pipe_stdin_r;
    si.hStdOutput = pipe_stdout_w;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    char cmd_buf[1024];
    snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd);
    if (!CreateProcessA(NULL, cmd_buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pipe_stdin_r);  CloseHandle(pipe_stdin_w);
        CloseHandle(pipe_stdout_r); CloseHandle(pipe_stdout_w);
        return 0;
    }
    /* close child-side handles in parent */
    CloseHandle(pipe_stdin_r);
    CloseHandle(pipe_stdout_w);

    s_voc.h_write  = pipe_stdin_w;
    s_voc.h_read   = pipe_stdout_r;
    s_voc.h_proc   = pi.hProcess;
    s_voc.h_thread = pi.hThread;
    s_voc.open     = 1;
    fprintf(stderr, "[TETRA] vocoder subprocess started\n");
    return 1;
}

static int voc_send_recv(const uint8_t *bits, int nbits, int16_t *pcm, int nsamples) {
    DWORD nwritten, nread;
    if (!WriteFile(s_voc.h_write, bits, (DWORD)nbits, &nwritten, NULL)
            || (int)nwritten != nbits)
        return 0;
    int pcm_bytes = nsamples * (int)sizeof(int16_t);
    DWORD got = 0;
    uint8_t *p = (uint8_t *)pcm;
    while ((int)got < pcm_bytes) {
        if (!ReadFile(s_voc.h_read, p + got, (DWORD)(pcm_bytes - (int)got), &nread, NULL)
                || nread == 0)
            break;
        got += nread;
    }
    return (int)(got / sizeof(int16_t));
}

void tetra_vocoder_close(void) {
    if (!s_voc.open) return;
    CloseHandle(s_voc.h_write);
    CloseHandle(s_voc.h_read);
    WaitForSingleObject(s_voc.h_proc, 2000);
    CloseHandle(s_voc.h_proc);
    CloseHandle(s_voc.h_thread);
    memset(&s_voc, 0, sizeof(s_voc));
}

#else
/* ---- POSIX implementation ---- */
static struct {
    int    fd_write;   /* parent writes bits  → child stdin  */
    int    fd_read;    /* parent reads  PCM   ← child stdout */
    pid_t  pid;
    int    open;
} s_voc;

static int voc_open(const char *cmd) {
    int to_child[2], from_child[2];
    if (pipe(to_child) != 0) return 0;
    if (pipe(from_child) != 0) {
        close(to_child[0]); close(to_child[1]);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        return 0;
    }
    if (pid == 0) {
        /* child */
        dup2(to_child[0],   STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        execlp("sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    /* parent */
    close(to_child[0]);
    close(from_child[1]);
    s_voc.fd_write = to_child[1];
    s_voc.fd_read  = from_child[0];
    s_voc.pid      = pid;
    s_voc.open     = 1;
    fprintf(stderr, "[TETRA] vocoder subprocess started\n");
    return 1;
}

static int voc_send_recv(const uint8_t *bits, int nbits, int16_t *pcm, int nsamples) {
    if (write(s_voc.fd_write, bits, (size_t)nbits) != nbits) return 0;
    int pcm_bytes = nsamples * (int)sizeof(int16_t);
    int total = 0;
    uint8_t *p = (uint8_t *)pcm;
    while (total < pcm_bytes) {
        ssize_t r = read(s_voc.fd_read, p + total, (size_t)(pcm_bytes - total));
        if (r <= 0) break;
        total += (int)r;
    }
    return total / (int)sizeof(int16_t);
}

void tetra_vocoder_close(void) {
    if (!s_voc.open) return;
    close(s_voc.fd_write);
    close(s_voc.fd_read);
    waitpid(s_voc.pid, NULL, 0);
    memset(&s_voc, 0, sizeof(s_voc));
}
#endif /* _WIN32 / POSIX */

/* Ensure the vocoder subprocess is running.  Returns 1 if open. */
static int voc_ensure_open(void) {
    if (s_voc.open) return 1;
    const char *cmd = getenv("TETRA_VOCODER_CMD");
    if (!cmd || !cmd[0]) return 0;
    return voc_open(cmd);
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
 * Full TCH voice pipeline for one NDB block:
 *   decoded bits → bit-reorder (ETSI EN 300 395-2 Table 4)
 *               → external vocoder subprocess (TETRA_VOCODER_CMD)
 *               → PCM16 routing (PulseAudio / UDP / raw FD / WAV)
 *
 * @decoded   : Viterbi-decoded type-1 bits (at least TETRA_TCH_FRAME_BITS)
 * @dec_len   : number of valid bits in decoded (clamped to TETRA_TCH_FRAME_BITS)
 * @block_idx : 1 or 2 (logging only)
 * @opts/state: standard dsd-neo context
 * ------------------------------------------------------------------------- */
void tetra_acelp_process_tch(const uint8_t *decoded, int dec_len,
                              int block_idx,
                              dsd_opts *opts, dsd_state *state)
{
    /* Phase 12 + Phase 14: combined floor-grant and timeslot gate.
     * tetra_acelp_slot_gate_passes() checks tetra_tx_granted_valid and
     * tetra_vc_slot so that audio is produced only for the granted slot. */
    if (!tetra_acelp_slot_gate_passes(block_idx, state))
        return;

    /* —— Step 1: bit reorder —— */
    uint8_t acelp_frame[TETRA_TCH_FRAME_BITS];
    memset(acelp_frame, 0, sizeof(acelp_frame));
    tetra_acelp_reorder(decoded, acelp_frame, dec_len);

    /* —— Step 2: send to vocoder, receive PCM16 —— */
    if (!voc_ensure_open()) {
        static int warned = 0;
        if (!warned) {
            fprintf(stderr,
                "[TETRA] TCH B%d: no vocoder. "
                "Set TETRA_VOCODER_CMD to enable audio output.\n",
                block_idx);
            warned = 1;
        }
        return;
    }

    int16_t pcm[TETRA_TCH_FRAME_SAMPLES];
    memset(pcm, 0, sizeof(pcm));
    int nsamples = voc_send_recv(acelp_frame, TETRA_TCH_FRAME_BITS,
                                 pcm, TETRA_TCH_FRAME_SAMPLES);
    if (nsamples <= 0) {
        /* vocoder pipe died; reset state so it will be re-opened next time */
        fprintf(stderr, "[TETRA] vocoder pipe error; resetting\n");
        tetra_vocoder_close();
        return;
    }

    /* —— Step 3: audio routing (same pattern as M17 / Codec2) —— */
    if (opts->slot1_on == 1) {
        /* PulseAudio / PortAudio */
        if (opts->audio_out_type == 0 && opts->audio_out_stream)
            dsd_audio_write(opts->audio_out_stream, pcm, (size_t)nsamples);

        /* UDP */
        if (opts->audio_out_type == 8)
            dsd_udp_audio_hook_blast(opts, state,
                                     (size_t)nsamples * sizeof(int16_t), pcm);

        /* raw file descriptor */
        if (opts->audio_out_type == 1 && opts->audio_out_fd >= 0)
            dsd_write(opts->audio_out_fd, pcm,
                      (size_t)nsamples * sizeof(int16_t));
    }

    /* per-call WAV */
    if (opts->wav_out_f != NULL && opts->dmr_stereo_wav == 1)
        sf_write_short(opts->wav_out_f, pcm, nsamples);

    /* static WAV (interleaved stereo) */
    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        short ss[TETRA_TCH_FRAME_SAMPLES * 2];
        for (int i = 0; i < nsamples && i < TETRA_TCH_FRAME_SAMPLES; i++) {
            ss[i * 2 + 0] = pcm[i];
            ss[i * 2 + 1] = pcm[i];
        }
        sf_write_short(opts->wav_out_f, ss, nsamples * 2);
    }
}
