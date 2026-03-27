// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA TCH timeslot audio gate.
 * Phase 14: combined floor-grant + timeslot filter for TCH audio output.
 *
 * Kept as a separate translation unit so unit tests can link it without
 * pulling in the full ACELP pipeline (which depends on platform audio,
 * libsndfile, and the vocoder subprocess).
 */

#include <dsd-neo/protocol/tetra/tetra_acelp.h>
#include <dsd-neo/core/state.h>

/*
 * tetra_acelp_slot_gate_passes()
 *
 * Returns 1 if the TETRA audio gate allows audio for the given block
 * index, 0 if it should be suppressed.
 *
 * Gate order (early-exit):
 *   1. NULL state            → pass (defensive; caller should always provide state)
 *   2. tetra_tx_granted_valid == 0  → pass (direct-tuned / no-control-plane voice)
 *   3. tetra_vc_slot == 0 or 3      → pass (no slot assigned, or dual-slot call)
 *   4. tetra_vc_slot == 1           → pass only block 1 (slot 1)
 *   5. tetra_vc_slot == 2           → pass only block 2 (slot 2)
 *
 * Rationale:
 *   D-TX-GRANTED is only available when the control-plane decode path is
 *   active. When users tune directly to a TETRA VC, or when signalling is
 *   missing/corrupt but the TCH/FS bursts are decodable, suppressing audio
 *   until a grant arrives causes valid voice to remain permanently muted.
 *   We therefore only apply slot filtering once a valid grant exists.
 */
int
tetra_acelp_slot_gate_passes(int block_idx, const dsd_state *state)
{
    if (!state)
        return 1; /* no context: permissive */

    /* No grant decoded yet: allow direct-tuned / signalling-free voice. */
    if (state->tetra_tx_granted_valid == 0)
        return 1;

    /* Grant is valid: apply slot filter. */
    uint8_t vs = state->tetra_vc_slot;
    if (vs == 1 && block_idx != 1)
        return 0; /* slot 1 only – skip block 2 */
    if (vs == 2 && block_idx != 2)
        return 0; /* slot 2 only – skip block 1 */
    /* vs == 0 (unassigned) or vs == 3 (dual): pass both blocks */
    return 1;
}
