// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DSD_NEO_PROTOCOL_TETRA_ACELP_H
#define DSD_NEO_PROTOCOL_TETRA_ACELP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations (avoid pulling in full headers here) */
struct dsd_opts;
struct dsd_state;

/*
 * Reorder TETRA ACELP coded bits into the order expected by the ACELP
 * vocoder (ETSI EN 300 395-2 §6.2 / osmo-tetra tch_reordering.c).
 *
 * @in    : interleaved NDB payload bits (Viterbi-decoded)
 * @out   : vocoder-ordered frame (TETRA_TCH_FRAME_BITS bytes, one bit per byte)
 * @len   : number of valid input bits (clamped to TETRA_TCH_FRAME_BITS)
 */
void tetra_acelp_reorder(const uint8_t *in, uint8_t *out, int len);

/*
 * Combined audio gate: returns 1 if the floor-grant AND timeslot checks
 * both allow audio for the given block index.
 *
 * @block_idx : 1 or 2 (NDB sub-block / timeslot number)
 * @state     : decoder state; NULL is treated as "pass" for safety
 *
 * Gate rules:
 *   tetra_tx_granted_valid == 0  → always suppress (Phase 12 gate)
 *   tetra_vc_slot == 0           → no slot assigned, pass all
 *   tetra_vc_slot == 1           → pass only block 1
 *   tetra_vc_slot == 2           → pass only block 2
 *   tetra_vc_slot == 3           → dual-slot call, pass all
 */
int tetra_acelp_slot_gate_passes(int block_idx, const struct dsd_state *state);

/*
 * Legacy no-op – kept for ABI/test compatibility.
 * Call tetra_acelp_process_tch() instead for actual audio output.
 */
void tetra_acelp_decode(const uint8_t *bits, int len);

/*
 * Full TCH voice pipeline for one NDB block:
 *   decoded bits -> bit-reorder -> external vocoder (TETRA_VOCODER_CMD)
 *               -> PCM16 audio routing (PA / UDP / raw FD / WAV)
 *
 * TETRA_VOCODER_CMD protocol (synchronous, persistent subprocess):
 *   send : TETRA_TCH_FRAME_BITS bytes  (one byte per coded bit, values 0/1)
 *   recv : TETRA_TCH_FRAME_SAMPLES * 2 bytes  (PCM16LE @ 8 kHz)
 *   loop until pipe closed.
 *
 * @decoded   : Viterbi-decoded bits
 * @dec_len   : count of decoded bits
 * @block_idx : 1 or 2 (for diagnostic messages only)
 * @opts      : dsd-neo options
 * @state     : dsd-neo state
 */
void tetra_acelp_process_tch(const uint8_t *decoded, int dec_len,
                             int block_idx,
                             struct dsd_opts *opts, struct dsd_state *state);

/*
 * Close the persistent vocoder subprocess if it is running.
 * Call on protocol reset, channel change, or application exit.
 */
void tetra_vocoder_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_PROTOCOL_TETRA_ACELP_H */
