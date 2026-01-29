#include <dsd-neo/protocol/tetra/tetra_acelp.h>
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA protocol handler (scaffold)
 * Reference: osmo-tetra PHY/MAC
 */
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>



#include <stdio.h>
#include <stdlib.h>
#include <dsd-neo/protocol/tetra/tetra_fec.h>

void processTetraFrame(dsd_opts* opts, dsd_state* state) {
    // Example: process a TETRA burst after sync
    fprintf(stderr, "[TETRA] Frame processing invoked (slot=%d, synctype=%d)\n", state ? state->currentslot : -1, state ? state->synctype : -1);

    // Accurate dibit/soft-symbol extraction from demodulator
    int dibits = 216; // 216 dibits == 432 bits (TYPE2)
    uint8_t dbuf[216];
    float soft_symbols[216];
    memset(dbuf, 0, sizeof(dbuf));
    memset(soft_symbols, 0, sizeof(soft_symbols));

    // Mark soft symbol collection start
    soft_symbol_frame_begin(state);

    // Read dibits and soft symbols from demodulator
    for (int i = 0; i < dibits; i++) {
        dbuf[i] = (uint8_t)getDibitAndSoftSymbol(opts, state, &soft_symbols[i]);
    }

    // Convert dibits -> bit array and soft-costs for Viterbi
    int bits_len = dibits * 2; // 432
    uint8_t hard_bits[432];
    uint16_t soft_costs[432];
    for (int i = 0; i < dibits; i++) {
        // MSB then LSB
        hard_bits[i*2 + 0] = (dbuf[i] >> 1) & 1;
        hard_bits[i*2 + 1] = (dbuf[i] >> 0) & 1;
        soft_costs[i*2 + 0] = soft_symbol_to_viterbi_cost(soft_symbols[i], state, 0);
        soft_costs[i*2 + 1] = soft_symbol_to_viterbi_cost(soft_symbols[i], state, 1);
    }

    /* Select puncturer id early so interleaver selection can use it */
    int punct_id = TETRA_RCPC_PUNCT_2_3; // default
    if (bits_len == 432) {
        punct_id = TETRA_RCPC_PUNCT_292_432;
    }

    // Deinterleave (block). Working on hard bits and soft costs.
    uint8_t deint[432];
    memset(deint, 0, sizeof(deint));
    uint16_t soft_deint[432];
    for (int i = 0; i < 432; i++) soft_deint[i] = 0x7FFF;
    /* Use exact interleaver dimensions when available (from osmo-tetra port).
     * `tetra_get_interleaver_dims()` fills rows (a) and cols for the given bits_len.
     */
    int rows = 0, cols = 0;
    if (tetra_get_interleaver_dims(punct_id, bits_len, &rows, &cols) == 0) {
        /* K is the type-3/type-4 block length */
        int K = bits_len;
        tetra_block_deinterleave(hard_bits, deint, K, rows);
        /* Deinterleave soft-costs into matching ordering for depuncture */
        tetra_block_deinterleave_soft(soft_costs, soft_deint, bits_len, rows, cols);
    } else {
        /* Fallback to previous default (rows=11) for now */
        tetra_block_deinterleave(hard_bits, deint, bits_len, 11);
        tetra_block_deinterleave_soft(soft_costs, soft_deint, bits_len, 11, (bits_len+11-1)/11);
    }

    // Descramble hard bits
    tetra_descramble(deint, bits_len, 3);

    // For soft costs, apply descramble inversion where scrambler bit flips
    tetra_descramble_soft(soft_deint, bits_len, 3);

    /* Depuncture using a puncture pattern. We'll use a default no-puncture pattern
     * (all ones) for now; later this should be selected per-burst according to RCPC tables.
     */
    int depunc_len = bits_len * 2; // coded symbols capacity (estimate)
    uint16_t* depunc = malloc(sizeof(uint16_t) * depunc_len);
    if (!depunc) {
        fprintf(stderr, "[TETRA] depunc malloc failed\n");
        return;
    }
    /* Debug: print puncturer info when verbose */
    if (opts && opts->verbose > 1) {
        tetra_rcpc_print_puncturer(punct_id);
    }
    /* Use exact puncturer mapping by id (osmo-tetra derived) */
    tetra_rcpc_depuncture_by_id(punct_id, soft_deint, bits_len, depunc, depunc_len);

    // Viterbi decode (soft-costs)
    uint8_t decoded_bits[512];
    int decoded_len = tetra_viterbi_decode_soft(depunc, depunc_len, decoded_bits, sizeof(decoded_bits));

    // ACELP reorder: use decoded bits as input (truncate/pad as needed)
    uint8_t acelp_bits[512];
    memset(acelp_bits, 0, sizeof(acelp_bits));
    tetra_acelp_reorder(decoded_bits, acelp_bits, decoded_len > 0 ? decoded_len : bits_len);

    // Call vocoder integration
    tetra_acelp_decode(acelp_bits, bits_len);

    // Print first few bits for debug
    fprintf(stderr, "[TETRA] ACELP bits: ");
    for (int i = 0; i < 16; i++) fprintf(stderr, "%d", acelp_bits[i] & 1);
    fprintf(stderr, "...\n");
}
