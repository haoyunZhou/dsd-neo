// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA Phase 42 test suite.
 *
 * Covers functionality introduced in Phases 39-41:
 *
 * Phase 39 — FEC decode quality counters:
 *   1.  tetra_decode_ok and tetra_decode_errors zero-initialize correctly
 *   2.  Fields are writable (counters can be incremented by callers)
 *
 * Phase 40 — D-SDS-DATA (CMCE type 23) parser:
 *   3.  8-bit encoded SDS: src_ssi, sds_text, sds_text_len populated
 *   4.  Short PDU (< 36 bits) handled without crash
 *   5.  ext_flag=1 PDU handled without crash, src_ssi stays 0
 *   6.  7-bit text encoding decoded correctly
 *
 * Phase 41 — TETRA SB sync type IDs:
 *   7.  DSD_SYNC_IS_TETRA returns 1 for SB_POS
 *   8.  DSD_SYNC_IS_TETRA returns 1 for SB_NEG
 *   9.  DSD_SYNC_IS_TETRA still returns 1 for NDB_POS and NDB_NEG
 *  10.  DSD_SYNC_IS_TETRA returns 0 for non-TETRA sync type
 *  11.  tetra_sb1_dibuf and tetra_sb1_valid zero-initialize correctly
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
            g_fail++; \
        } \
    } while (0)

static void pack_bits(uint8_t *out, uint32_t val, int offset, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--)
        out[offset++] = (uint8_t)((val >> i) & 1u);
}

static dsd_state *alloc_state(void) { return (dsd_state *)calloc(1, sizeof(dsd_state)); }
static dsd_opts  *alloc_opts(void)  { return (dsd_opts  *)calloc(1, sizeof(dsd_opts));  }

/* -----------------------------------------------------------------------
 * Helper: build an MLE C-PLANE / CMCE wrapper around a raw CMCE PDU body.
 *   out[0..4]  = mle_type=24, out[5..8] = pd=3, out[9..9+cmce_len-1] = cmce_body
 * ----------------------------------------------------------------------- */
static void wrap_mle_cmce(const uint8_t *cmce_body, int cmce_nbits,
                           uint8_t *out, int *out_nbits)
{
    memset(out, 0, (size_t)(9 + cmce_nbits));
    pack_bits(out, 24, 0, 5);   /* mle_type = TETRA_MLE_C_PLANE_DATA */
    pack_bits(out,  3, 5, 4);   /* pd       = TETRA_MLE_PD_CMCE       */
    memcpy(out + 9, cmce_body, (size_t)cmce_nbits);
    *out_nbits = 9 + cmce_nbits;
}

/* -----------------------------------------------------------------------
 * Phase 39 tests: FEC decode quality counter fields
 * ----------------------------------------------------------------------- */
static void test_decode_quality_fields_zero(void)
{
    printf("[test_decode_quality_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_decode_ok     == 0, "decode_ok zero after calloc");
    CHECK(st->tetra_decode_errors == 0, "decode_errors zero after calloc");

    /* counters must be directly writable */
    st->tetra_decode_ok     = 99;
    st->tetra_decode_errors = 3;
    CHECK(st->tetra_decode_ok     == 99, "decode_ok writable");
    CHECK(st->tetra_decode_errors ==  3, "decode_errors writable");

    free(st);
}

/* -----------------------------------------------------------------------
 * Phase 40 tests: D-SDS-DATA (CMCE type 23) parser
 * ----------------------------------------------------------------------- */

/* Test 3: 8-bit text "OK" from SSI 77777 */
static void test_d_sds_data_8bit_text(void)
{
    printf("[test_d_sds_data_8bit_text]\n");

    /*
     * CMCE D-SDS-DATA PDU (bits 0..64, relative to CMCE PDU start):
     *   [0-4]   pdu_type = 23
     *   [5]     ext_flag = 0
     *   [6-29]  calling_ssi = 77777 (24 bits)
     *   [30-33] msg_ref = 0 (4 bits)
     *   [34]    store_fwd = 0
     *   [35]    vp_flag = 0
     *   [36]    dt_flag = 0
     *   [37-40] bpc = 8
     *   [41-48] num_chars = 2
     *   [49-56] 'O' = 79
     *   [57-64] 'K' = 75
     * Total CMCE PDU: 65 bits
     */
    const int CMCE_NBITS = 65;
    uint8_t cmce[65];
    memset(cmce, 0, sizeof(cmce));

    pack_bits(cmce, 23u,    0,  5);   /* pdu_type = D-SDS-DATA */
    pack_bits(cmce,  0u,    5,  1);   /* ext_flag = 0           */
    pack_bits(cmce, 77777u, 6, 24);   /* calling_ssi = 77777    */
    pack_bits(cmce,  0u,   30,  4);   /* msg_ref                */
    pack_bits(cmce,  0u,   34,  1);   /* store_fwd              */
    pack_bits(cmce,  0u,   35,  1);   /* vp_flag = 0            */
    pack_bits(cmce,  0u,   36,  1);   /* dt_flag = 0            */
    pack_bits(cmce,  8u,   37,  4);   /* bits_per_char = 8      */
    pack_bits(cmce,  2u,   41,  8);   /* num_chars = 2          */
    pack_bits(cmce, (uint32_t)'O', 49, 8);  /* 'O' = 79        */
    pack_bits(cmce, (uint32_t)'K', 57, 8);  /* 'K' = 75        */

    uint8_t mle[9 + 65];
    int mle_nbits;
    wrap_mle_cmce(cmce, CMCE_NBITS, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_sds_src      == 77777u,   "D-SDS-DATA: src_ssi=77777");
    CHECK(st->tetra_sds_text_len == 2,        "D-SDS-DATA: sds_text_len=2");
    CHECK(st->tetra_sds_text[0]  == 'O',      "D-SDS-DATA: text[0]='O'");
    CHECK(st->tetra_sds_text[1]  == 'K',      "D-SDS-DATA: text[1]='K'");

    free(st); free(opt);
}

/* Test 4: too-short PDU (no crash, no side-effects) */
static void test_d_sds_data_too_short(void)
{
    printf("[test_d_sds_data_too_short]\n");

    uint8_t cmce[10];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 23u, 0, 5); /* pdu_type = 23, only 10 bits total */

    uint8_t mle[9 + 10];
    int mle_nbits;
    wrap_mle_cmce(cmce, 10, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* Must not crash */
    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_sds_src      == 0, "D-SDS-DATA short: src_ssi stays 0");
    CHECK(st->tetra_sds_text_len == 0, "D-SDS-DATA short: text_len stays 0");

    free(st); free(opt);
}

/* Test 5: ext_flag = 1 (external subscriber number) — no crash, src remains 0 */
static void test_d_sds_data_ext_flag(void)
{
    printf("[test_d_sds_data_ext_flag]\n");

    /* ext_flag=1, then 8-bit field_len=0, then end of bits (too short for SDS-TL) */
    const int CMCE_NBITS = 15;
    uint8_t cmce[15];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 23u, 0, 5);   /* pdu_type */
    pack_bits(cmce,  1u, 5, 1);   /* ext_flag = 1 */
    pack_bits(cmce,  0u, 6, 8);   /* field_len = 0 */

    uint8_t mle[9 + 15];
    int mle_nbits;
    wrap_mle_cmce(cmce, CMCE_NBITS, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    /* src_ssi should remain 0 (ext_flag path, no SSI extracted) */
    CHECK(st->tetra_sds_src == 0, "D-SDS-DATA ext_flag: src_ssi=0");

    free(st); free(opt);
}

/* Test 6: 7-bit encoding for text "Hi!" */
static void test_d_sds_data_7bit_text(void)
{
    printf("[test_d_sds_data_7bit_text]\n");

    /*
     * CMCE D-SDS-DATA, 7-bit text "Hi!" from SSI=55555:
     *   [0-4]   pdu_type = 23
     *   [5]     ext_flag = 0
     *   [6-29]  calling_ssi = 55555
     *   [30-33] msg_ref = 0
     *   [34]    store_fwd = 0
     *   [35]    vp_flag = 0
     *   [36]    dt_flag = 0
     *   [37-40] bpc = 7
     *   [41-48] num_chars = 3
     *   [49-55] 'H' = 72 (7 bits MSB first: 1001000)
     *   [56-62] 'i' = 105 (7 bits: 1101001)
     *   [63-69] '!' = 33  (7 bits: 0100001)
     * Total: 70 bits
     */
    const int CMCE_NBITS = 70;
    uint8_t cmce[70];
    memset(cmce, 0, sizeof(cmce));

    pack_bits(cmce, 23u,    0,  5);
    pack_bits(cmce,  0u,    5,  1);
    pack_bits(cmce, 55555u, 6, 24);
    pack_bits(cmce,  0u,   30,  4);
    pack_bits(cmce,  0u,   34,  1);
    pack_bits(cmce,  0u,   35,  1);
    pack_bits(cmce,  0u,   36,  1);
    pack_bits(cmce,  7u,   37,  4);   /* bpc = 7 */
    pack_bits(cmce,  3u,   41,  8);   /* num_chars = 3 */
    pack_bits(cmce, (uint32_t)'H', 49, 7);
    pack_bits(cmce, (uint32_t)'i', 56, 7);
    pack_bits(cmce, (uint32_t)'!', 63, 7);

    uint8_t mle[9 + 70];
    int mle_nbits;
    wrap_mle_cmce(cmce, CMCE_NBITS, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_sds_src       == 55555u,    "D-SDS-DATA 7bit: src_ssi=55555");
    CHECK(st->tetra_sds_text_len  == 3,         "D-SDS-DATA 7bit: text_len=3");
    CHECK(st->tetra_sds_text[0]   == 'H',       "D-SDS-DATA 7bit: text[0]='H'");
    CHECK(st->tetra_sds_text[1]   == 'i',       "D-SDS-DATA 7bit: text[1]='i'");
    CHECK(st->tetra_sds_text[2]   == '!',       "D-SDS-DATA 7bit: text[2]='!'");

    free(st); free(opt);
}

/* -----------------------------------------------------------------------
 * Phase 41 tests: TETRA SB sync type IDs
 * ----------------------------------------------------------------------- */
static void test_synctype_tetra_sb(void)
{
    printf("[test_synctype_tetra_sb]\n");

    CHECK(DSD_SYNC_IS_TETRA(DSD_SYNC_TETRA_SB_POS)  == 1, "SB_POS is TETRA");
    CHECK(DSD_SYNC_IS_TETRA(DSD_SYNC_TETRA_SB_NEG)  == 1, "SB_NEG is TETRA");
    CHECK(DSD_SYNC_IS_TETRA(DSD_SYNC_TETRA_NDB_POS) == 1, "NDB_POS still TETRA");
    CHECK(DSD_SYNC_IS_TETRA(DSD_SYNC_TETRA_NDB_NEG) == 1, "NDB_NEG still TETRA");
    CHECK(DSD_SYNC_IS_TETRA(0)                      == 0, "type 0 not TETRA");
    CHECK(DSD_SYNC_IS_TETRA(5)                      == 0, "type 5 not TETRA");
}

/* SB1 capture fields zero-initialize correctly */
static void test_sb1_fields_zero(void)
{
    printf("[test_sb1_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_sb1_valid == 0, "tetra_sb1_valid zero after calloc");
    /* Verify at least first and last bytes of the 60-byte buffer are zero */
    CHECK(st->tetra_sb1_dibuf[0]  == 0, "sb1_dibuf[0] zero");
    CHECK(st->tetra_sb1_dibuf[59] == 0, "sb1_dibuf[59] zero");

    free(st);
}

/* SB and NDB sync IDs are distinct */
static void test_synctype_sb_ndb_distinct(void)
{
    printf("[test_synctype_sb_ndb_distinct]\n");

    CHECK(DSD_SYNC_TETRA_SB_POS  != DSD_SYNC_TETRA_NDB_POS, "SB_POS != NDB_POS");
    CHECK(DSD_SYNC_TETRA_SB_NEG  != DSD_SYNC_TETRA_NDB_NEG, "SB_NEG != NDB_NEG");
    CHECK(DSD_SYNC_TETRA_SB_POS  != DSD_SYNC_TETRA_SB_NEG,  "SB_POS != SB_NEG");
    CHECK(DSD_SYNC_TETRA_NDB_POS != DSD_SYNC_TETRA_NDB_NEG, "NDB_POS != NDB_NEG");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    printf("[TETRA Phase 42 tests: Phases 39-41]\n");

    /* Phase 39: FEC decode quality counters */
    test_decode_quality_fields_zero();

    /* Phase 40: D-SDS-DATA parser */
    test_d_sds_data_8bit_text();
    test_d_sds_data_too_short();
    test_d_sds_data_ext_flag();
    test_d_sds_data_7bit_text();

    /* Phase 41: TETRA SB sync type IDs */
    test_synctype_tetra_sb();
    test_sb1_fields_zero();
    test_synctype_sb_ndb_distinct();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
