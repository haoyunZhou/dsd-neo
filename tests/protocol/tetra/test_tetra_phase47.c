// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA Phase 47 test suite.
 *
 * Covers functionality introduced in Phases 43-46:
 *
 * Phase 43 — TDMA timestamps in BSCH:
 *   1.  TDMA fields (tetra_tn/fn/mn/tdma_valid) zero-initialise correctly
 *   2.  tetra_bsch_parse() with TN=1 (raw 0), FN=5, MN=12 populates fields
 *   3.  tetra_tn is 1-based (tn_raw + 1)
 *   4.  tetra_tdma_valid is set to 1 after a successful parse
 *   5.  Second BSCH with different TN/FN/MN overwrites previous values
 *
 * Phase 44 — MM D-LU-REJECT + D-TEMPORARY-ADDRESS parsers:
 *   6.  tetra_mm_lu_reject_* fields zero-initialise correctly
 *   7.  D-LU-REJECT with cause=4 sets tetra_mm_lu_reject_cause=4 and valid=1
 *   8.  D-LU-REJECT with fewer than 8 bits still sets valid=1 (cause=0)
 *   9.  tetra_mm_temp_ssi* fields zero-initialise correctly
 *  10.  D-TEMPORARY-ADDRESS with SSI=123456 sets temp_ssi and valid=1
 *  11.  D-TEMPORARY-ADDRESS with < 29 bits still sets valid=1 (ssi=0)
 *
 * Phase 45 — D-TX-GRANTED updates UI fields:
 *  12.  tetra_sds_msg_ref and tetra_sds_last_cc zero-initialise correctly
 *  13.  D-TX-GRANTED with SSI sets lastsrc to granted SSI
 *  14.  D-TX-GRANTED with SSI sets active_channel[0] to "TETRA TG:... SRC:...  (TX-GRANTED)"
 *  15.  D-TX-GRANTED with SSI sets last_active_time != 0
 *
 * Phase 46 — SDS message reference tracking:
 *  16.  D-SDS-DATA stores msg_ref in tetra_sds_msg_ref
 *  17.  D-SDS-DATA stores CC in tetra_sds_last_cc
 *  18.  Two SDS PDUs with different msg_ref values update sds_msg_ref correctly
 */

#include <dsd-neo/protocol/tetra/tetra_bsch.h>
#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/protocol/tetra/tetra_mm.h>
#include <dsd-neo/core/state.h>

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
 * BSCH helpers — build a 60-bit PDU with specified fields.
 *
 * BSCH bit layout (ETSI EN 300 392-2 §21.3.3):
 *   [0-3]   scrambling / reserved          (4 bits — leave 0)
 *   [4-9]   colour code                    (6 bits)
 *  [10-11]  TN timeslot (0-based, add 1)   (2 bits)
 *  [12-16]  FN frame number 0-17           (5 bits)
 *  [17-22]  MN multiframe number 0-59      (6 bits)
 *  [23-30]  reserved / HN partial          (8 bits — leave 0)
 *  [31-40]  MCC                            (10 bits)
 *  [41-54]  MNC                            (14 bits)
 *  [55-59]  reserved                       (5 bits — leave 0)
 * ----------------------------------------------------------------------- */
static void bsch_build(uint8_t bits[60],
                       uint8_t colour,
                       uint8_t tn_raw,   /* 0-based, function adds 1 */
                       uint8_t fn,
                       uint8_t mn,
                       uint16_t mcc,
                       uint16_t mnc)
{
    memset(bits, 0, 60);
    pack_bits(bits, colour,  4,  6);
    pack_bits(bits, tn_raw, 10,  2);
    pack_bits(bits, fn,     12,  5);
    pack_bits(bits, mn,     17,  6);
    pack_bits(bits, mcc,    31, 10);
    pack_bits(bits, mnc,    41, 14);
}

/* -----------------------------------------------------------------------
 * MLE / CMCE helper — wrap a raw CMCE PDU in an MLE C-PLANE-DATA frame.
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
 * Helper: build an MLE C-PLANE-DATA / MM wrapper.
 *   mle_type=24, pd=5 (MM), then mm_body
 * ----------------------------------------------------------------------- */
static void wrap_mle_mm(const uint8_t *mm_body, int mm_nbits,
                         uint8_t *out, int *out_nbits)
{
    memset(out, 0, (size_t)(9 + mm_nbits));
    pack_bits(out, 24, 0, 5);   /* mle_type = TETRA_MLE_C_PLANE_DATA */
    pack_bits(out,  5, 5, 4);   /* pd       = TETRA_MLE_PD_MM (5)     */
    memcpy(out + 9, mm_body, (size_t)mm_nbits);
    *out_nbits = 9 + mm_nbits;
}

/* =======================================================================
 * Phase 43: TDMA timestamps in BSCH
 * ======================================================================= */

static void test_tdma_fields_zero(void)
{
    printf("[test_tdma_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_tdma_valid == 0, "tdma_valid zero after calloc");
    CHECK(st->tetra_tn         == 0, "tetra_tn zero after calloc");
    CHECK(st->tetra_fn         == 0, "tetra_fn zero after calloc");
    CHECK(st->tetra_mn         == 0, "tetra_mn zero after calloc");

    free(st);
}

static void test_bsch_tdma_parse(void)
{
    printf("[test_bsch_tdma_parse]\n");

    /* TN raw=0 → stored as 1; FN=5; MN=12 */
    uint8_t bits[60];
    bsch_build(bits, /*colour=*/0x15, /*tn_raw=*/0, /*fn=*/5, /*mn=*/12,
               /*mcc=*/234, /*mnc=*/30);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    int result = tetra_bsch_parse(bits, 60, opt, st);

    CHECK(result           == 1,  "bsch_parse returns 1 on success");
    CHECK(st->tetra_tdma_valid == 1,  "tdma_valid = 1 after bsch_parse");
    CHECK(st->tetra_tn     == 1,  "tetra_tn = tn_raw+1 = 1");
    CHECK(st->tetra_fn     == 5,  "tetra_fn = 5");
    CHECK(st->tetra_mn     == 12, "tetra_mn = 12");

    free(st); free(opt);
}

static void test_bsch_tdma_tn_onebased(void)
{
    printf("[test_bsch_tdma_tn_onebased]\n");

    uint8_t bits[60];
    /* tn_raw = 3 → stored TN = 4 */
    bsch_build(bits, 0x01, 3, 17, 59, 310, 100);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_bsch_parse(bits, 60, opt, st);

    CHECK(st->tetra_tn == 4,  "tetra_tn = tn_raw(3)+1 = 4");
    CHECK(st->tetra_fn == 17, "tetra_fn = 17 (max frame)");
    CHECK(st->tetra_mn == 59, "tetra_mn = 59 (max multiframe)");

    free(st); free(opt);
}

static void test_bsch_tdma_overwrite(void)
{
    printf("[test_bsch_tdma_overwrite]\n");

    uint8_t bits[60];

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* First BSCH: TN=1, FN=0, MN=0 */
    bsch_build(bits, 0x01, 0, 0, 0, 100, 1);
    tetra_bsch_parse(bits, 60, opt, st);
    CHECK(st->tetra_tn == 1, "first BSCH: tn=1");

    /* Second BSCH: TN=3, FN=15, MN=30 */
    bsch_build(bits, 0x01, 2, 15, 30, 100, 1);
    tetra_bsch_parse(bits, 60, opt, st);
    CHECK(st->tetra_tn == 3,  "second BSCH overwrites tn=3");
    CHECK(st->tetra_fn == 15, "second BSCH overwrites fn=15");
    CHECK(st->tetra_mn == 30, "second BSCH overwrites mn=30");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 44: MM D-LU-REJECT + D-TEMPORARY-ADDRESS
 * ======================================================================= */

static void test_mm_lu_reject_fields_zero(void)
{
    printf("[test_mm_lu_reject_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_mm_lu_reject_valid == 0, "lu_reject_valid zero after calloc");
    CHECK(st->tetra_mm_lu_reject_cause == 0, "lu_reject_cause zero after calloc");

    free(st);
}

static void test_mm_d_lu_reject_cause(void)
{
    printf("[test_mm_d_lu_reject_cause]\n");

    /*
     * MM D-LU-REJECT PDU:
     *   [0-4]  pdu_type = 7  (TETRA_MM_D_LOCATION_UPDATING_REJECT)
     *   [5-7]  cause = 4     (3 bits)
     * Total: 8 bits
     */
    uint8_t mm[8];
    memset(mm, 0, sizeof(mm));
    pack_bits(mm, 7u, 0, 5);   /* pdu_type = 7 */
    pack_bits(mm, 4u, 5, 3);   /* cause = 4    */

    uint8_t mle[9 + 8];
    int mle_nbits;
    wrap_mle_mm(mm, 8, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 7, opt, st);

    CHECK(st->tetra_mm_lu_reject_valid == 1, "D-LU-REJECT: valid=1");
    CHECK(st->tetra_mm_lu_reject_cause == 4, "D-LU-REJECT: cause=4");

    free(st); free(opt);
}

static void test_mm_d_lu_reject_short(void)
{
    printf("[test_mm_d_lu_reject_short]\n");

    /* Only 5 bits — pdu_type only, no cause field */
    uint8_t mm[5];
    memset(mm, 0, sizeof(mm));
    pack_bits(mm, 7u, 0, 5);

    uint8_t mle[9 + 5];
    int mle_nbits;
    wrap_mle_mm(mm, 5, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* Must not crash */
    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_mm_lu_reject_valid == 1, "D-LU-REJECT short: still sets valid=1");
    CHECK(st->tetra_mm_lu_reject_cause == 0, "D-LU-REJECT short: cause defaults to 0");

    free(st); free(opt);
}

static void test_mm_temp_ssi_fields_zero(void)
{
    printf("[test_mm_temp_ssi_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_mm_temp_ssi_valid == 0, "temp_ssi_valid zero after calloc");
    CHECK(st->tetra_mm_temp_ssi       == 0, "temp_ssi zero after calloc");

    free(st);
}

static void test_mm_d_temporary_address_ssi(void)
{
    printf("[test_mm_d_temporary_address_ssi]\n");

    /*
     * MM D-TEMPORARY-ADDRESS PDU:
     *   [0-4]   pdu_type = 9
     *   [5-28]  temporary SSI = 123456 (24 bits)
     * Total: 29 bits
     */
    uint8_t mm[29];
    memset(mm, 0, sizeof(mm));
    pack_bits(mm, 9u,      0,  5);  /* pdu_type = 9 */
    pack_bits(mm, 123456u, 5, 24);  /* temp_ssi     */

    uint8_t mle[9 + 29];
    int mle_nbits;
    wrap_mle_mm(mm, 29, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_mm_temp_ssi_valid == 1,      "D-TEMP-ADDR: valid=1");
    CHECK(st->tetra_mm_temp_ssi       == 123456u, "D-TEMP-ADDR: ssi=123456");

    free(st); free(opt);
}

static void test_mm_d_temporary_address_short(void)
{
    printf("[test_mm_d_temporary_address_short]\n");

    /* Only pdu_type, no SSI field */
    uint8_t mm[5];
    memset(mm, 0, sizeof(mm));
    pack_bits(mm, 9u, 0, 5);

    uint8_t mle[9 + 5];
    int mle_nbits;
    wrap_mle_mm(mm, 5, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->tetra_mm_temp_ssi_valid == 1, "D-TEMP-ADDR short: still sets valid=1");
    CHECK(st->tetra_mm_temp_ssi       == 0, "D-TEMP-ADDR short: ssi=0");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 45: D-TX-GRANTED updates UI fields
 * ======================================================================= */

static void test_tx_granted_ui_fields_zero(void)
{
    printf("[test_tx_granted_ui_fields_zero]\n");
    dsd_state *st = alloc_state();

    CHECK(st->tetra_sds_msg_ref  == 0,  "sds_msg_ref zero after calloc");
    CHECK(st->tetra_sds_last_cc  == 0,  "sds_last_cc zero after calloc");

    free(st);
}

/*
 * Build a D-TX-GRANTED CMCE PDU with granted SSI present.
 *
 * CMCE D-TX-GRANTED layout (PDU type=10):
 *   [0-4]  pdu_type = 10
 *   [5]    tx_perm  (1 bit)
 *   [6-7]  enc_mode (2 bits)
 *   [8]    reserv   (1 bit)
 *   [9]    gp_present = 1
 *   [10-12] addr_type = 1 (SSI)
 *   [13-36] granted_ssi (24 bits)
 *   [37]   ac_present = 0
 * Total: 38 bits
 */
static void build_d_tx_granted(uint8_t *cmce, uint32_t granted_ssi)
{
    memset(cmce, 0, 38);
    pack_bits(cmce, 10u,          0,  5);   /* pdu_type = 10 */
    pack_bits(cmce,  1u,          5,  1);   /* tx_perm = 1   */
    pack_bits(cmce,  0u,          6,  2);   /* enc_mode = 0  */
    pack_bits(cmce,  0u,          8,  1);   /* reserv = 0    */
    pack_bits(cmce,  1u,          9,  1);   /* gp_present = 1 */
    pack_bits(cmce,  1u,         10,  3);   /* addr_type = SSI */
    pack_bits(cmce, granted_ssi, 13, 24);   /* SSI           */
    pack_bits(cmce,  0u,         37,  1);   /* ac_present = 0 */
}

static void test_tx_granted_lastsrc_set(void)
{
    printf("[test_tx_granted_lastsrc_set]\n");

    uint8_t cmce[38];
    build_d_tx_granted(cmce, 88888u);

    uint8_t mle[9 + 38];
    int mle_nbits;
    wrap_mle_cmce(cmce, 38, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* Pre-set lasttg so active_channel can include it */
    st->lasttg = 44444;

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->lastsrc                 == 88888, "TX-GRANTED: lastsrc = 88888");
    CHECK(st->tetra_tx_granted_ssi    == 88888u, "TX-GRANTED: tx_granted_ssi = 88888");
    CHECK(st->tetra_tx_granted_valid  == 1,      "TX-GRANTED: valid = 1");

    free(st); free(opt);
}

static void test_tx_granted_active_channel_set(void)
{
    printf("[test_tx_granted_active_channel_set]\n");

    uint8_t cmce[38];
    build_d_tx_granted(cmce, 12345u);

    uint8_t mle[9 + 38];
    int mle_nbits;
    wrap_mle_cmce(cmce, 38, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();
    st->lasttg = 9999;

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    /* active_channel[0] must contain "TX-GRANTED" marker */
    int found_tg  = (strstr(st->active_channel[0], "9999") != NULL);
    int found_src = (strstr(st->active_channel[0], "12345") != NULL);
    int found_kw  = (strstr(st->active_channel[0], "TX-GRANTED") != NULL);

    CHECK(found_tg,  "TX-GRANTED: active_channel contains lasttg");
    CHECK(found_src, "TX-GRANTED: active_channel contains granted SSI");
    CHECK(found_kw,  "TX-GRANTED: active_channel contains TX-GRANTED keyword");

    free(st); free(opt);
}

static void test_tx_granted_last_active_time(void)
{
    printf("[test_tx_granted_last_active_time]\n");

    uint8_t cmce[38];
    build_d_tx_granted(cmce, 111u);

    uint8_t mle[9 + 38];
    int mle_nbits;
    wrap_mle_cmce(cmce, 38, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 0, opt, st);

    CHECK(st->last_active_time != 0, "TX-GRANTED: last_active_time updated");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 46: SDS message reference tracking
 * ======================================================================= */

/*
 * Build a minimal D-SDS-DATA CMCE PDU with an explicit msg_ref.
 *
 *   [0-4]   pdu_type = 23
 *   [5]     ext_flag = 0
 *   [6-29]  calling_ssi (24 bits)
 *   [30-33] msg_ref (4 bits)
 *   [34]    store_fwd = 0
 *   [35]    vp_flag = 0
 *   [36]    dt_flag = 0
 *   [37-44] bpc = 8
 *   [45-52] num_chars = 1
 *   [53-60] 'A' = 65
 * Total: 61 bits
 */
static void build_d_sds_data(uint8_t *cmce, uint32_t src_ssi, uint8_t msg_ref)
{
    memset(cmce, 0, 61);
    pack_bits(cmce, 23u,     0,  5);    /* pdu_type = D-SDS-DATA */
    pack_bits(cmce,  0u,     5,  1);    /* ext_flag = 0          */
    pack_bits(cmce, src_ssi, 6, 24);    /* calling_ssi           */
    pack_bits(cmce, msg_ref,30,  4);    /* msg_ref               */
    pack_bits(cmce,  0u,    34,  1);    /* store_fwd             */
    pack_bits(cmce,  0u,    35,  1);    /* vp_flag               */
    pack_bits(cmce,  0u,    36,  1);    /* dt_flag               */
    pack_bits(cmce,  8u,    37,  8);    /* bpc = 8               */
    pack_bits(cmce,  1u,    45,  8);    /* num_chars = 1         */
    pack_bits(cmce, (uint32_t)'A', 53, 8);  /* 'A'              */
}

static void test_sds_msg_ref_stored(void)
{
    printf("[test_sds_msg_ref_stored]\n");

    uint8_t cmce[61];
    build_d_sds_data(cmce, 22222u, 7u);

    uint8_t mle[9 + 61];
    int mle_nbits;
    wrap_mle_cmce(cmce, 61, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 3, opt, st);

    CHECK(st->tetra_sds_msg_ref  == 7,  "SDS msg_ref = 7");
    CHECK(st->tetra_sds_last_cc  == 3,  "SDS last_cc = 3");

    free(st); free(opt);
}

static void test_sds_last_cc_stored(void)
{
    printf("[test_sds_last_cc_stored]\n");

    uint8_t cmce[61];
    build_d_sds_data(cmce, 33333u, 2u);

    uint8_t mle[9 + 61];
    int mle_nbits;
    wrap_mle_cmce(cmce, 61, mle, &mle_nbits);

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    tetra_mle_dispatch(mle, mle_nbits, 11, opt, st);

    CHECK(st->tetra_sds_last_cc == 11, "SDS last_cc = 11");

    free(st); free(opt);
}

static void test_sds_msg_ref_overwrite(void)
{
    printf("[test_sds_msg_ref_overwrite]\n");

    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* First SDS with msg_ref=5 */
    uint8_t cmce1[61];
    build_d_sds_data(cmce1, 10001u, 5u);
    uint8_t mle1[9 + 61]; int n1;
    wrap_mle_cmce(cmce1, 61, mle1, &n1);
    tetra_mle_dispatch(mle1, n1, 0, opt, st);
    CHECK(st->tetra_sds_msg_ref == 5, "first SDS: msg_ref=5");

    /* Second SDS with msg_ref=12 */
    uint8_t cmce2[61];
    build_d_sds_data(cmce2, 10002u, 12u);
    uint8_t mle2[9 + 61]; int n2;
    wrap_mle_cmce(cmce2, 61, mle2, &n2);
    tetra_mle_dispatch(mle2, n2, 0, opt, st);
    /* msg_ref is 4-bit so 12 stores as 12 */
    CHECK(st->tetra_sds_msg_ref == 12, "second SDS: msg_ref overwritten to 12");

    free(st); free(opt);
}

/* =======================================================================
 * main
 * ======================================================================= */
int main(void)
{
    printf("=== TETRA Phase 47 Test Suite ===\n\n");

    /* Phase 43 */
    printf("--- Phase 43: TDMA timestamps in BSCH ---\n");
    test_tdma_fields_zero();
    test_bsch_tdma_parse();
    test_bsch_tdma_tn_onebased();
    test_bsch_tdma_overwrite();

    /* Phase 44 */
    printf("\n--- Phase 44: MM D-LU-REJECT + D-TEMPORARY-ADDRESS ---\n");
    test_mm_lu_reject_fields_zero();
    test_mm_d_lu_reject_cause();
    test_mm_d_lu_reject_short();
    test_mm_temp_ssi_fields_zero();
    test_mm_d_temporary_address_ssi();
    test_mm_d_temporary_address_short();

    /* Phase 45 */
    printf("\n--- Phase 45: D-TX-GRANTED UI fields ---\n");
    test_tx_granted_ui_fields_zero();
    test_tx_granted_lastsrc_set();
    test_tx_granted_active_channel_set();
    test_tx_granted_last_active_time();

    /* Phase 46 */
    printf("\n--- Phase 46: SDS message reference tracking ---\n");
    test_sds_msg_ref_stored();
    test_sds_last_cc_stored();
    test_sds_msg_ref_overwrite();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
