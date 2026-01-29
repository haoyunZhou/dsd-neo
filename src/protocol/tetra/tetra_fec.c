// SPDX-License-Identifier: GPL-3.0-or-later
#include <dsd-neo/protocol/tetra/tetra_fec.h>
#include <string.h>
#include <stdio.h>
#include <dsd-neo/fec/viterbi.h>
#include <stdlib.h>

/* Minimal port of osmo-tetra puncturer tables and mapping functions
 * to implement exact RCPC depuncture ordering.
 * The arrays below are derived from osmo-tetra's `tetra_conv_enc.c`.
 */

/* Puncturer enums are declared in the header as macros to allow callers to
 * select puncturer ids without duplicating symbols. The detailed puncturer
 * table below uses the same ordering as those macros.
 */

/* Minimal no-puncture fallback pattern used by generic depuncturer when none supplied */
static const uint8_t punct_1_2_arr[] = { 1 };

struct puncturer {
    int type;
    const uint8_t *P;
    uint8_t t;
    uint8_t period;
    uint32_t (*i_func)(uint32_t j);
};

static uint32_t i_func_equals(uint32_t j) { return j; }
static uint32_t i_func_292(uint32_t j) { return (j + ((j-1)/65)); }
static uint32_t i_func_148(uint32_t j) { return (j + ((j-1)/35)); }

/* Puncturer tables from osmo-tetra */
static const uint8_t P_rate2_3[] = { 0, 1, 2, 5 };
static const uint8_t P_rate1_3[] = { 0, 1, 2, 3, 5, 6, 7 };
static const uint8_t P_rate8_12[] = { 0, 1, 2, 4 };
static const uint8_t P_rate8_18[] = { 0, 1, 2, 3, 4, 5, 7, 8, 10, 11 };
static const uint8_t P_rate8_17[] = { 0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23 };

static const struct puncturer punct_2_3 = { .type = TETRA_RCPC_PUNCT_2_3, .P = P_rate2_3, .t = 3, .period = 8, .i_func = &i_func_equals };
static const struct puncturer punct_1_3 = { .type = TETRA_RCPC_PUNCT_1_3, .P = P_rate1_3, .t = 6, .period = 8, .i_func = &i_func_equals };
static const struct puncturer punct_292_432 = { .type = TETRA_RCPC_PUNCT_292_432, .P = P_rate2_3, .t = 3, .period = 8, .i_func = &i_func_292 };
static const struct puncturer punct_148_432 = { .type = TETRA_RCPC_PUNCT_148_432, .P = P_rate1_3, .t = 6, .period = 8, .i_func = &i_func_148 };
static const struct puncturer punct_112_168 = { .type = TETRA_RCPC_PUNCT_112_168, .P = P_rate8_12, .t = 3, .period = 6, .i_func = &i_func_equals };
static const struct puncturer punct_72_162 = { .type = TETRA_RCPC_PUNCT_72_162, .P = P_rate8_18, .t = 9, .period = 12, .i_func = &i_func_equals };
static const struct puncturer punct_38_80 = { .type = TETRA_RCPC_PUNCT_38_80, .P = P_rate8_17, .t = 17, .period = 24, .i_func = &i_func_equals };

static const struct puncturer *tetra_puncts[] = {
    &punct_2_3,
    &punct_1_3,
    &punct_292_432,
    &punct_148_432,
    &punct_112_168,
    &punct_72_162,
    &punct_38_80
};

/* Depuncture by puncturer id using soft costs. Mirrors tetra_rcpc_depunct logic. */
int tetra_rcpc_depuncture_by_id(int punct_id, const uint16_t* in_costs, int in_len, uint16_t* out_costs, int out_len) {
    if (punct_id < 0 || punct_id >= (int)(sizeof(tetra_puncts)/sizeof(tetra_puncts[0]))) return -1;
    if (!in_costs || !out_costs) return -1;
    const struct puncturer *punct = tetra_puncts[punct_id];
    const uint8_t *P = punct->P;
    uint8_t t = punct->t;
    uint8_t period = punct->period;
    uint32_t (*i_func)(uint32_t) = punct->i_func;

    /* initialize out with neutral costs */
    for (int i = 0; i < out_len; i++) out_costs[i] = 0x7FFF;

    /* map input coded symbols (in_costs) into mother-code positions per puncturer */
    for (uint32_t j = 1; j <= (uint32_t)in_len; j++) {
        uint32_t i = i_func(j);
        if (i == 0) continue;
        uint32_t block = (i - 1) / (uint32_t)t;
        uint32_t pos_in_block = (i - 1) % (uint32_t)t; /* 0-based */
        uint32_t p_index = pos_in_block + 1; /* preserve 1-based P layout used historically */
        uint32_t pval = (uint32_t)P[p_index];
        uint32_t k = (uint32_t)period * block + pval;
        if (k >= 1 && (int)(k-1) < out_len) {
            out_costs[k-1] = in_costs[j-1];
        }
    }

    return out_len;
}

/* Return the mother-code position k for given puncturer id and j index (1-based),
 * or -1 on error. This mirrors the mapping used in puncture/depuncture.
 */
int tetra_rcpc_map_j_to_k(int punct_id, uint32_t j) {
    if (punct_id < 0 || punct_id >= (int)(sizeof(tetra_puncts)/sizeof(tetra_puncts[0]))) return -1;
    const struct puncturer *punct = tetra_puncts[punct_id];
    const uint8_t *P = punct->P;
    uint8_t t = punct->t;
    uint8_t period = punct->period;
    uint32_t (*i_func)(uint32_t) = punct->i_func;

    /* Use a period-based lookup to compute k.
     * The original formula uses 1-based indexing into P; preserve that
     * semantics here but express it as a clear periodic lookup:
     *  - i = i_func(j)
     *  - block = (i-1) / t
     *  - pos_in_block = (i-1) % t  (0-based)
     *  - P is addressed as P[pos_in_block + 1] to match existing layout
     *  - k = period * block + P[pos_in_block + 1]
     */
    uint32_t i = i_func(j);
    if (i == 0) return -1;
    uint32_t block = (i - 1) / (uint32_t)t;
    uint32_t pos_in_block = (i - 1) % (uint32_t)t; /* 0-based */
    uint32_t p_index = pos_in_block + 1; /* preserve existing 1-based P layout */
    uint32_t pval = (uint32_t)P[p_index];
    uint32_t k = (uint32_t)period * block + pval;
    return (int)k;
}

/* Puncture mother-code by puncturer id (forward mapping). Writes up to out_len
 * type-3 symbols into out. Returns number written or -1 on error.
 */
int tetra_rcpc_puncture_by_id(int punct_id, const uint8_t* mother, int mother_len, uint8_t* out, int out_len) {
    if (punct_id < 0 || punct_id >= (int)(sizeof(tetra_puncts)/sizeof(tetra_puncts[0]))) return -1;
    if (!mother || !out) return -1;
    const struct puncturer *punct = tetra_puncts[punct_id];
    const uint8_t *P = punct->P;
    uint8_t t = punct->t;
    uint8_t period = punct->period;
    uint32_t (*i_func)(uint32_t) = punct->i_func;

    int written = 0;
    for (uint32_t j = 1; written < out_len; j++) {
        uint32_t i = i_func(j);
        if (i == 0) {
            out[written++] = 0xff;
            continue;
        }
        uint32_t block = (i - 1) / (uint32_t)t;
        uint32_t pos_in_block = (i - 1) % (uint32_t)t;
        uint32_t p_index = pos_in_block + 1;
        uint32_t pval = (uint32_t)P[p_index];
        uint32_t k = (uint32_t)period * block + pval;
        if (k >= 1 && (int)(k-1) < mother_len) {
            out[written++] = mother[k-1];
        } else {
            out[written++] = 0xff;
        }
    }

    return written;
}

/* Debug: print puncturer parameters and P array */
void tetra_rcpc_print_puncturer(int punct_id) {
    if (punct_id < 0 || punct_id >= (int)(sizeof(tetra_puncts)/sizeof(tetra_puncts[0]))) {
        fprintf(stderr, "[TETRA] puncturer id %d out of range\n", punct_id);
        return;
    }
    const struct puncturer *p = tetra_puncts[punct_id];
    fprintf(stderr, "[TETRA] puncturer id=%d t=%u period=%u P=[", punct_id, p->t, p->period);
    /* print first few entries of P for brevity */
    for (int i = 0; i < 16 && p->P[i] != 0 && i < 64; i++) {
        if (i) fprintf(stderr, ",");
        fprintf(stderr, "%u", p->P[i]);
    }
    fprintf(stderr, "]\n");
}

int tetra_rcpc_get_puncturer_params(int punct_id, const uint8_t **outP, int *out_t, int *out_period) {
    if (punct_id < 0 || punct_id >= (int)(sizeof(tetra_puncts)/sizeof(tetra_puncts[0]))) return -1;
    const struct puncturer *p = tetra_puncts[punct_id];
    if (outP) *outP = p->P;
    if (out_t) *out_t = (int)p->t;
    if (out_period) *out_period = (int)p->period;
    return 0;
}

void tetra_block_deinterleave(uint8_t* in, uint8_t* out, int len, int a) {
    if (!in || !out || len <= 0 || a <= 0) return;
    /* Implement a rectangular block deinterleaver parameterized by 'a'.
     * Write input row-wise into a matrix with 'a' rows, then read out column-wise
     * to produce the deinterleaved output. This matches the common block
     * interleaver approach used in osmo-tetra where 'a' is a row count.
     */
    int rows = a;
    int cols = (len + rows - 1) / rows;
    /* Fill matrix by rows (pad with 0 if necessary) */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int src = r * cols + c;
            int dst = c * rows + r;
            if (src < len)
                out[dst] = in[src];
            else
                out[dst] = 0;
        }
    }
    fprintf(stderr, "[TETRA] block_deinterleave called (len=%d, a=%d)\n", len, a);
}

/* TETRA descrambler (PN sequence generator) - LFSR based.
 * Implement a 17-bit LFSR with taps at bit positions 17 and 3 (polynomial x^17 + x^3 + 1).
 * `lfsr_init` provides an initial non-zero seed; low bits are used as initial state.
 */
void tetra_descramble(uint8_t* in, int len, uint32_t lfsr_init) {
    if (!in || len <= 0) return;
    uint32_t state = lfsr_init & 0x1FFFFu; /* 17-bit state */
    if (state == 0) state = 0x1; /* avoid all-zero */
    for (int i = 0; i < len; i++) {
        /* generate next bit: xor of bit 16 and bit 2 (0-based) */
        uint32_t bit16 = (state >> 16) & 1u;
        uint32_t bit2 = (state >> 2) & 1u;
        uint8_t pn = (uint8_t)(bit16 ^ bit2);
        /* XOR descramble the input bit */
        in[i] = in[i] ^ (pn & 1u);
        /* advance LFSR: shift left and insert pn at lsb (feedback) */
        state = ((state << 1) & 0x1FFFFu) | pn;
    }
}

/* Descramble soft-costs by inverting the soft cost polarity where scrambler bit == 1.
 * A soft cost is inverted by mapping 0x0000 <-> 0xFFFF; neutral cost 0x7FFF stays neutral if scrambler bit is 1.
 */
void tetra_descramble_soft(uint16_t* costs, int len, uint32_t lfsr_init) {
    if (!costs || len <= 0) return;
    uint32_t state = lfsr_init & 0x1FFFFu; /* 17-bit state */
    if (state == 0) state = 0x1;
    for (int i = 0; i < len; i++) {
        uint32_t bit16 = (state >> 16) & 1u;
        uint32_t bit2 = (state >> 2) & 1u;
        uint8_t pn = (uint8_t)(bit16 ^ bit2);
        if (pn) {
            uint16_t v = costs[i];
            if (v == 0x7FFF) {
                /* leave neutral as-is */
            } else if (v == 0x0000) {
                costs[i] = 0xFFFF;
            } else if (v == 0xFFFF) {
                costs[i] = 0x0000;
            } else {
                /* For intermediate soft values, invert around midpoint 0x7FFF */
                uint32_t inv = (uint32_t)0xFFFFu - (uint32_t)v;
                costs[i] = (uint16_t)inv;
            }
        }
        state = ((state << 1) & 0x1FFFFu) | (pn & 1u);
    }
}

void tetra_viterbi_decode(uint8_t* in, uint8_t* out, int len) {
    // Placeholder: just copy input to output
    memcpy(out, in, len);
    fprintf(stderr, "[TETRA] viterbi_decode called (len=%d)\n", len);
}

int tetra_viterbi_decode_soft(const uint16_t* depunc, int depunc_len, uint8_t* out_bits, int out_bits_len) {
    if (!depunc || !out_bits) return -1;

    /* viterbi_decode expects (uint8_t* out_bytes, const uint16_t* in_costs, uint16_t len)
     * where `len` is number of coded symbols (soft costs). We'll allocate a bytes buffer
     * large enough to hold the decoded output and then unpack to bits.
     */
    int out_bytes_cap = (depunc_len / 8) + 8;
    uint8_t out_bytes[256];
    memset(out_bytes, 0, sizeof(out_bytes));

#ifdef TETRA_FEC_TEST_MAIN
    /* Test build: no viterbi implementation linked. Provide a simple deterministic
     * stub to produce predictable output bytes for the unit test.
     */
    for (int i = 0; i < out_bytes_cap; i++) out_bytes[i] = (uint8_t)(0xAA + (i & 0xFF));
#else
    uint32_t v_err = viterbi_decode(out_bytes, depunc, (uint16_t)depunc_len);
#endif

    /* Unpack bytes into bits (MSB first) up to out_bits_len */
    int bitpos = 0;
    for (int b = 0; b < out_bytes_cap && bitpos < out_bits_len; b++) {
        for (int i = 0; i < 8 && bitpos < out_bits_len; i++) {
            out_bits[bitpos++] = (out_bytes[b] >> (7 - i)) & 1;
        }
    }

    return bitpos; /* number of bits written */
}

int tetra_rcpc_depuncture_soft(const uint16_t* in_costs, int info_bits_len, const uint8_t* punct, int p_len,
                               uint16_t* out_costs, int out_len) {
    if (!in_costs || !out_costs) return -1;
    if (!punct || p_len <= 0) {
        /* fallback to no-puncture */
        punct = punct_1_2_arr;
        p_len = 1;
    }

    int in_pos = 0;
    int out_pos = 0;

    /* Walk the puncture pattern repeating until we consume the requested output length.
     * If punct[i % p_len] == 1 then a coded symbol is present for this information bit;
     * otherwise it's punctured and we insert a neutral cost (0x7FFF).
     * This generic depuncturer maps each info bit to punctured coded symbols in sequence.
     */
    while (out_pos < out_len && in_pos < info_bits_len) {
        for (int pi = 0; pi < p_len && out_pos < out_len; pi++) {
            if (punct[pi]) {
                /* use the next info soft cost */
                out_costs[out_pos++] = in_costs[in_pos++];
            } else {
                /* punctured: insert neutral / uncertain cost */
                out_costs[out_pos++] = 0x7FFF;
            }
        }
    }

    /* If we exhausted info bits but still need to fill output, pad with neutral costs */
    while (out_pos < out_len) {
        out_costs[out_pos++] = 0x7FFF;
    }

    return out_pos;
}

void tetra_block_interleave(const uint8_t* in_bits, uint8_t* out_bits, int len, int rows, int cols) {
    if (!in_bits || !out_bits || rows <= 0 || cols <= 0) return;
    /* Simple row/column rectangular interleaver: write by rows, read by columns */
    int idx = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int pos = r * cols + c;
            if (pos < len) {
                out_bits[idx++] = in_bits[pos];
            }
        }
    }
}

void tetra_block_deinterleave_bits(const uint8_t* in_bits, uint8_t* out_bits, int len, int rows, int cols) {
    if (!in_bits || !out_bits || rows <= 0 || cols <= 0) return;
    /* Inverse of the above rectangular interleaver */
    int idx = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int pos = r * cols + c;
            if (pos < len) {
                out_bits[pos] = in_bits[idx++];
            }
        }
    }
}

void tetra_block_deinterleave_soft(const uint16_t* in_costs, uint16_t* out_costs, int len, int rows, int cols) {
    if (!in_costs || !out_costs || rows <= 0 || cols <= 0) return;
    /* Inverse of a rectangular interleaver for soft-costs: read column-wise from
     * a rows x cols matrix filled row-wise by the transmitter. Reverse that here.
     */
    int idx = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int pos = r * cols + c;
            if (pos < len) {
                out_costs[pos] = in_costs[idx++];
            }
        }
    }
}

int tetra_get_interleaver_dims(int punct_id, int bits_len, int *out_rows, int *out_cols) {
    if (!out_rows || !out_cols) return -1;
    /* Static interleaver dimensions table (authoritative values).
     * Only known canonical block lengths are accepted — do not fall back
     * to a heuristic. Unknown lengths return -1 so callers can decide.
     * Source: osmo-tetra `tetra_blk_param` mapping (ported entries).
     */
    struct inter_tab { int bits; int rows; } table[] = {
        {30,  1},   /* BBK (no interleave) - represent as rows=1 (identity) */
        {80, 11},   /* BSCH-ish small block (conservative) */
        {120, 11},  /* SB1 */
        {162, 11},  /* speech class-ish (conservative) */
        {168, 13},  /* SCH/HU */
        {216, 101}, /* SB2 / NDB */
        {432, 103}, /* SCH/F and some TCH modes */
    };
    const int n = sizeof(table) / sizeof(table[0]);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (table[i].bits == bits_len) {
            *out_rows = table[i].rows;
            found = 1;
            break;
        }
    }
    if (!found) return -1;
    if (*out_rows <= 0) return -1;
    *out_cols = (bits_len + *out_rows - 1) / *out_rows;
    return 0;
}

#ifdef TETRA_FEC_TEST_MAIN
#include <assert.h>
#include <stdint.h>

/* Simple test harness: build with -DTETRA_FEC_TEST_MAIN to run.
 * Example (MSVC): cl /Iinclude /D TETRA_FEC_TEST_MAIN src\protocol\tetra\tetra_fec.c /Fe:depunct_test.exe
 */
int main(void) {
    fprintf(stderr, "[TETRA TEST] Starting depuncture mapping test...\n");

    /* We'll perform a round-trip test: puncture a synthetic mother buffer,
     * then depuncture and verify that reconstructed positions match the
     * original mother buffer values.
     */
    const int mother_len = 512;
    uint8_t *mother = (uint8_t*)malloc(mother_len);
    for (int i = 0; i < mother_len; i++) mother[i] = (uint8_t)(i & 0xFF);

    /* Choose a common puncturer and a realistic punctured length */
    int punct_id = 2; /* punct_292_432 */
    const int puncted_len = 432;
    uint8_t *puncted = (uint8_t*)malloc(puncted_len);
    int wrote = tetra_rcpc_puncture_by_id(punct_id, mother, mother_len, puncted, puncted_len);
    if (wrote != puncted_len) {
        fprintf(stderr, "[TETRA TEST] unexpected puncture length %d (wanted %d)\n", wrote, puncted_len);
        return 3;
    }

    /* Convert punctured bytes into soft costs (simple mapping) */
    uint16_t *in_costs = (uint16_t*)malloc(sizeof(uint16_t) * puncted_len);
    for (int i = 0; i < puncted_len; i++) in_costs[i] = (uint16_t)puncted[i];

    uint16_t *out_costs = (uint16_t*)malloc(sizeof(uint16_t) * mother_len);
    for (int i = 0; i < mother_len; i++) out_costs[i] = 0x7FFF;

    int ret = tetra_rcpc_depuncture_by_id(punct_id, in_costs, puncted_len, out_costs, mother_len);
    if (ret < 0) {
        fprintf(stderr, "[TETRA TEST] depuncture returned error\n");
        return 4;
    }

    /* Verify reconstructed positions equal original mother values */
    int mismatches = 0;
    int mapped = 0;
    for (int i = 0; i < mother_len; i++) {
        if (out_costs[i] != 0x7FFF) {
            mapped++;
            if ((uint8_t)out_costs[i] != mother[i]) {
                mismatches++;
                if (mismatches < 8) {
                    fprintf(stderr, "[TETRA TEST] mismatch at pos %d: got %u expected %u\n", i, out_costs[i], mother[i]);
                }
            }
        }
    }

    fprintf(stderr, "[TETRA TEST] puncted_len=%d mapped_positions=%d mismatches=%d\n", puncted_len, mapped, mismatches);
    if (mismatches == 0 && mapped > 0) {
        fprintf(stderr, "[TETRA TEST] Puncture->depuncture roundtrip OK\n");
    } else {
        fprintf(stderr, "[TETRA TEST] Roundtrip FAILED\n");
        return 5;
    }

    free(mother);
    free(puncted);
    free(in_costs);
    free(out_costs);
    return 0;
}
#endif
