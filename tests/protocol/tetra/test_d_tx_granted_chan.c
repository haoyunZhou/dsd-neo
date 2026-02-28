// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Unit tests for D-TX-GRANTED Assigned Channel IE parsing (Phase 12).
 *
 * Verifies that tetra_mle_dispatch() correctly fills the new VC channel
 * assignment fields in dsd_state when D-TX-GRANTED carries an
 * Assigned Channel IE of various types.
 *
 * Bit layout reference (offsets into the tetra_mle_dispatch input array):
 *
 *   [0-4]   MLE type  = 24 (C_PLANE_DATA)
 *   [5-8]   PD        =  3 (CMCE)
 *   [9-13]  CMCE type = 10 (D-TX-GRANTED)
 *   [14]    tx_perm
 *   [15-16] enc_mode
 *   [17]    reserv
 *   [18]    gp_present (Granted Party Identity IE present flag)
 *     ...   [19-21] addr_type (3b), [22-45] SSI (24b) if gp_present=1
 *   [N]     ac_present (Assigned Channel IE present flag)
 *   [N+1..N+2] ac_type  (2 bits: 01=new carrier, 10=same carrier)
 *   If ac_type=01: [N+3..N+14] carrier (12b), [N+15..N+16] slot (2b)
 *   If ac_type=10: [N+3..N+4]  slot (2b)
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/opts.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

/* Write @nbits bits from @val (MSB-first) into out[offset..]. */
static void pack_bits(uint8_t *out, uint32_t val, int offset, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--)
        out[offset++] = (uint8_t)((val >> i) & 1u);
}

static dsd_state *alloc_state(void) { return (dsd_state *)calloc(1, sizeof(dsd_state)); }
static dsd_opts  *alloc_opts(void)  { return (dsd_opts  *)calloc(1, sizeof(dsd_opts));  }

/* Common MLE + CMCE D-TX-GRANTED prefix (bits 0-17, no gp_present): */
static void pack_dtg_header(uint8_t *bits)
{
    pack_bits(bits, 24u, 0, 5);  /* MLE C_PLANE_DATA */
    pack_bits(bits,  3u, 5, 4);  /* PD = CMCE */
    pack_bits(bits, 10u, 9, 5);  /* CMCE type = D-TX-GRANTED */
    pack_bits(bits,  1u, 14, 1); /* tx_perm = 1 */
    pack_bits(bits,  0u, 15, 2); /* enc_mode = 0 */
    pack_bits(bits,  0u, 17, 1); /* reserv = 0 */
    /* bit 18 = gp_present — caller fills this */
}

/* -------------------------------------------------------------------------
 * test_no_assigned_channel
 *
 * D-TX-GRANTED without Assigned Channel IE — vc fields must remain zero.
 * ----------------------------------------------------------------------- */
static void test_no_assigned_channel(void)
{
    /* 20 bits: header + gp_present=0, ac_present=0 */
    const int NBITS = 20;
    uint8_t bits[20];
    memset(bits, 0, sizeof bits);

    pack_dtg_header(bits);          /* [0..17] */
    pack_bits(bits, 0u, 18, 1);    /* gp_present = 0 */
    /* bit 19 = ac_present = 0 (already 0 from memset) */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_tx_granted_valid == 1, "no-AC: tx_granted_valid should be 1");
    CHECK(state->tetra_vc_assignment_type == 0, "no-AC: vc_assignment_type should be 0");
    CHECK(state->tetra_vc_carrier == 0,         "no-AC: vc_carrier should be 0");
    CHECK(state->tetra_vc_slot    == 0,         "no-AC: vc_slot should be 0");
    CHECK(state->tetra_vc_freq_hz == 0L,        "no-AC: vc_freq_hz should be 0");

    fprintf(stderr, "[test_no_assigned_channel] ok\n");
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * test_assigned_channel_new_carrier
 *
 * D-TX-GRANTED with ac_type=1 (specific carrier):
 *   gp_present=0, ac_present=1, ac_type=1, carrier=100, slot=2
 *
 * With tetra_freq_band=0 (default, 390 MHz base):
 *   vc_freq_hz = 390000000 + 100*25000 = 392500000
 * ----------------------------------------------------------------------- */
static void test_assigned_channel_new_carrier(void)
{
    /* Layout:
     *  [0-17]  DTG header
     *  [18]    gp_present = 0
     *  [19]    ac_present = 1
     *  [20-21] ac_type    = 1  (specific carrier)
     *  [22-33] carrier    = 100
     *  [34-35] slot       = 2
     *  Total = 36 bits
     */
    const int NBITS = 36;
    uint8_t bits[36];
    memset(bits, 0, sizeof bits);

    pack_dtg_header(bits);
    pack_bits(bits,   0u, 18, 1);  /* gp_present = 0 */
    pack_bits(bits,   1u, 19, 1);  /* ac_present = 1 */
    pack_bits(bits,   1u, 20, 2);  /* ac_type = 1 (specific carrier) */
    pack_bits(bits, 100u, 22, 12); /* carrier = 100 */
    pack_bits(bits,   2u, 34, 2);  /* slot = 2 */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    /* tetra_freq_band = 0 (default), tetra_freq_offset = 0 → base = 390 MHz */

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_tx_granted_valid    == 1, "new-carrier: tx_granted_valid");
    CHECK(state->tetra_vc_assignment_type  == 1, "new-carrier: vc_assignment_type=1");
    CHECK(state->tetra_vc_carrier          == 100, "new-carrier: vc_carrier=100");
    CHECK(state->tetra_vc_slot             == 2,   "new-carrier: vc_slot=2");
    /* Expected: 390000000 + 100*25000 = 392500000 */
    CHECK(state->tetra_vc_freq_hz == 392500000L,   "new-carrier: vc_freq_hz=392500000");

    fprintf(stderr, "[test_assigned_channel_new_carrier] carrier=%u slot=%u freq_hz=%ld\n",
            state->tetra_vc_carrier, state->tetra_vc_slot, state->tetra_vc_freq_hz);
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * test_assigned_channel_new_carrier_band4
 *
 * Same as above but state has tetra_freq_band=4 (460 MHz base).
 *   vc_freq_hz = 460000000 + 50*25000 = 461250000
 * ----------------------------------------------------------------------- */
static void test_assigned_channel_new_carrier_band4(void)
{
    const int NBITS = 36;
    uint8_t bits[36];
    memset(bits, 0, sizeof bits);

    pack_dtg_header(bits);
    pack_bits(bits,  0u, 18, 1);
    pack_bits(bits,  1u, 19, 1);
    pack_bits(bits,  1u, 20, 2);  /* ac_type = 1 */
    pack_bits(bits, 50u, 22, 12); /* carrier = 50 */
    pack_bits(bits,  1u, 34, 2);  /* slot = 1 */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    state->tetra_freq_band   = 4;  /* 460 MHz base */
    state->tetra_freq_offset = 0;

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_vc_assignment_type == 1,       "band4: type=1");
    CHECK(state->tetra_vc_carrier         == 50,      "band4: carrier=50");
    CHECK(state->tetra_vc_slot            == 1,       "band4: slot=1");
    /* 460000000 + 50*25000 = 461250000 */
    CHECK(state->tetra_vc_freq_hz == 461250000L,       "band4: vc_freq_hz=461250000");

    fprintf(stderr, "[test_assigned_channel_new_carrier_band4] freq_hz=%ld\n",
            state->tetra_vc_freq_hz);
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * test_assigned_channel_same_carrier
 *
 * D-TX-GRANTED with ac_type=2 (same carrier, different timeslot).
 * Only tetra_vc_slot should be set; vc_carrier stays 0.
 * vc_freq_hz should equal state->tetra_dl_carrier_hz (= CC freq).
 * ----------------------------------------------------------------------- */
static void test_assigned_channel_same_carrier(void)
{
    /* Layout:
     *  [0-17]  DTG header
     *  [18]    gp_present = 0
     *  [19]    ac_present = 1
     *  [20-21] ac_type    = 2 (same carrier)
     *  [22-23] slot       = 3
     *  Total = 24 bits
     */
    const int NBITS = 24;
    uint8_t bits[24];
    memset(bits, 0, sizeof bits);

    pack_dtg_header(bits);
    pack_bits(bits, 0u, 18, 1); /* gp_present = 0 */
    pack_bits(bits, 1u, 19, 1); /* ac_present = 1 */
    pack_bits(bits, 2u, 20, 2); /* ac_type = 2 (same carrier) */
    pack_bits(bits, 3u, 22, 2); /* slot = 3 */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    state->tetra_dl_carrier_hz = 392500000L; /* CC freq set by SYSINFO */

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_vc_assignment_type == 2,      "same-carrier: type=2");
    CHECK(state->tetra_vc_carrier         == 0,      "same-carrier: carrier stays 0");
    CHECK(state->tetra_vc_slot            == 3,      "same-carrier: slot=3");
    /* same-carrier: vc_freq_hz mirrors the CC freq */
    CHECK(state->tetra_vc_freq_hz == 392500000L,     "same-carrier: vc_freq_hz=CC");

    fprintf(stderr, "[test_assigned_channel_same_carrier] slot=%u vc_freq_hz=%ld\n",
            state->tetra_vc_slot, state->tetra_vc_freq_hz);
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * test_ssi_and_assigned_channel
 *
 * D-TX-GRANTED with both Granted Party SSI and Assigned Channel IE.
 * Bit layout:
 *   [0-17]   DTG header
 *   [18]     gp_present = 1
 *   [19-21]  addr_type  = 1 (SSI)
 *   [22-45]  SSI = TEST_SSI (24 bits)
 *   [46]     ac_present = 1
 *   [47-48]  ac_type    = 1 (new carrier)
 *   [49-60]  carrier    = 200 (12 bits)
 *   [61-62]  slot       = 1
 *   Total = 63 bits
 * ----------------------------------------------------------------------- */
static void test_ssi_and_assigned_channel(void)
{
    const uint32_t TEST_SSI = 0xABCDEFu;
    const int NBITS = 63;
    uint8_t bits[63];
    memset(bits, 0, sizeof bits);

    pack_dtg_header(bits);
    pack_bits(bits,    1u,  18, 1);  /* gp_present = 1 */
    pack_bits(bits,    1u,  19, 3);  /* addr_type = 1 (SSI) */
    pack_bits(bits, TEST_SSI, 22, 24); /* SSI */
    pack_bits(bits,    1u,  46, 1);  /* ac_present = 1 */
    pack_bits(bits,    1u,  47, 2);  /* ac_type = 1 (new carrier) */
    pack_bits(bits,  200u,  49, 12); /* carrier = 200 */
    pack_bits(bits,    1u,  61, 2);  /* slot = 1 */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    state->tetra_freq_band   = 0; /* 390 MHz base */
    state->tetra_freq_offset = 0;

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_tx_granted_ssi     == TEST_SSI, "ssi+ac: SSI set");
    CHECK(state->tetra_vc_assignment_type == 1,        "ssi+ac: type=1");
    CHECK(state->tetra_vc_carrier         == 200,      "ssi+ac: carrier=200");
    CHECK(state->tetra_vc_slot            == 1,        "ssi+ac: slot=1");
    /* 390000000 + 200*25000 = 395000000 */
    CHECK(state->tetra_vc_freq_hz == 395000000L,       "ssi+ac: vc_freq_hz=395000000");

    fprintf(stderr, "[test_ssi_and_assigned_channel] SSI=0x%06X carrier=%u slot=%u freq=%ld\n",
            state->tetra_tx_granted_ssi, state->tetra_vc_carrier,
            state->tetra_vc_slot, state->tetra_vc_freq_hz);
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * test_tx_ceased_clears_fields
 *
 * D-TX-CEASED must clear tetra_tx_granted_valid, which stops ACELP audio.
 * The vc assignment fields may retain their last values (not required to clear).
 * ----------------------------------------------------------------------- */
static void test_tx_ceased_clears_fields(void)
{
    /* Layout of D-TX-CEASED MLE PDU:
     *   [0-4]   MLE C_PLANE_DATA = 24
     *   [5-8]   PD = 3 (CMCE)
     *   [9-13]  CMCE type = 8 (D-TX-CEASED)
     *   [14]    tx_perm = 0
     *   [15]    cipher_info = 0
     *   Total = 16 bits
     */
    const int NBITS = 16;
    uint8_t bits[16];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5);
    pack_bits(bits,  3u, 5, 4);
    pack_bits(bits,  8u, 9, 5); /* D-TX-CEASED */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    state->tetra_tx_granted_valid = 1; /* pre-set: was in a call */

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    CHECK(state->tetra_tx_granted_valid == 0, "D-TX-CEASED: tx_granted_valid cleared");

    fprintf(stderr, "[test_tx_ceased_clears_fields] ok\n");
    free(state); free(opts);
}

/* -------------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    test_no_assigned_channel();
    test_assigned_channel_new_carrier();
    test_assigned_channel_new_carrier_band4();
    test_assigned_channel_same_carrier();
    test_ssi_and_assigned_channel();
    test_tx_ceased_clears_fields();

    if (g_failures == 0) {
        printf("PASS  test_d_tx_granted_chan: all checks passed\n");
        return 0;
    }
    printf("FAIL  test_d_tx_granted_chan: %d check(s) failed\n", g_failures);
    return 1;
}
