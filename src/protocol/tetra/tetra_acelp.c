// SPDX-License-Identifier: GPL-3.0-or-later
#include <dsd-neo/protocol/tetra/tetra_acelp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

/* Ported from osmo-tetra src/lower_mac/tch_reordering.c -- reordering tables */

/* Reordering positions (1-based indices) */

static const uint8_t class0_positions[] = {
    35,36,37,38,39,40,41,42,33,47,48,56,61,62,63,65,66,67,68,69,70,74,75,83,88,89,90,91,92,93,94,95,96,97,101,102,110,115,116,117,118,119,120,121,122,123,124,128,129,137
};

static const uint8_t class1_positions[] = {
    58,85,112,54,81,108,135,51,78,105,132,55,82,109,136,5,13,34,8,16,17,22,23,24,25,26,6,14,7,15,60,87,114,46,73,100,127,44,71,98,125,33,49,76,103,130,59,86,113,57,84,111,29,30,31,32,20,21
};

static const uint8_t class2_positions[] = {
    18,19,20,21,31,32,53,80,107,134,1,2,3,4,9,10,11,12,27,28,29,30,39,40,41,42,43,44,45,46
};
/* Compute counts from arrays to avoid mismatches between defines and initializers */
enum {
    NUM_ACELP_CLASS0_BITS = sizeof(class0_positions) / sizeof(class0_positions[0]),
    NUM_ACELP_CLASS1_BITS = sizeof(class1_positions) / sizeof(class1_positions[0]),
    NUM_ACELP_CLASS2_BITS = sizeof(class2_positions) / sizeof(class2_positions[0])
};
#define NUM_ACELP_BITS (NUM_ACELP_CLASS0_BITS + NUM_ACELP_CLASS1_BITS + NUM_ACELP_CLASS2_BITS)

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

    fprintf(stderr, "[TETRA] ACELP reorder done\n");
}

/* Runtime hook: if env var TETRA_VOCODER_CMD is set, run that command and pipe raw bits
 * to its stdin; otherwise print a placeholder message. The external tool must accept
 * raw one-bit-per-byte input and produce audio on stdout, or handle its own output.
 */
void tetra_acelp_decode(const uint8_t* bits, int len) {
    const char* cmd = getenv("TETRA_VOCODER_CMD");
    if (!cmd) {
        fprintf(stderr, "[TETRA] ACELP decode: no TETRA_VOCODER_CMD set; skipping decode (len=%d)\n", len);
        return;
    }

#ifndef _WIN32
    int pipe_stdin[2];
    pid_t pid;
    if (pipe(pipe_stdin) < 0) {
        perror("pipe");
        return;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        return;
    }
    if (pid == 0) {
        /* child: replace stdin with pipe read end and exec command */
        dup2(pipe_stdin[0], STDIN_FILENO);
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        execlp("sh", "sh", "-c", cmd, (char*)NULL);
        perror("execlp");
        _exit(127);
    }

    /* parent: write bits as bytes to child's stdin */
    close(pipe_stdin[0]);
    for (int i = 0; i < len; i++) {
        uint8_t b = bits[i] & 1;
        if (write(pipe_stdin[1], &b, 1) != 1) {
            break;
        }
    }
    close(pipe_stdin[1]);
    waitpid(pid, NULL, 0);
    fprintf(stderr, "[TETRA] ACELP decode: external cmd executed\n");
#else
    (void)bits; (void)len;
    fprintf(stderr, "[TETRA] ACELP decode: TETRA_VOCODER_CMD not supported on Windows build; set up external vocoder integration for Windows separately.\n");
#endif
}
