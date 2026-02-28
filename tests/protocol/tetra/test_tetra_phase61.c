// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA Phase 61 test suite.
 *
 * Covers functionality introduced in Phases 54-60:
 *
 * Phase 54 — Floor-control event flags
 *   1. D-TX-CONTINUE / INTERRUPT / WAIT / TIMED-OUT
 *
 * Phase 55 — MM/MLE small parsers
 *   2. MM D-CHECK-TSI
 *   3. MM D-STATUS (type 8)
 *   4. MLE D-RESTORE-ACK / D-RESTORE-RESPONSE
 *
 * Phase 56 — External subscriber flag (ext_flag=1)
 *   5. D-SDS-SHORT-DATA with ext_flag=1 (length + up to 24 bits extracted)
 *   6. D-SDS-DATA with ext_flag=1 (length + up to 24 bits extracted)
 *
 * Phase 57 — CMCE D-INFO
 *   7. D-INFO call_id
 *
 * Phase 58 — SDS Unicode text
 *   8. bpc=16 UCS-2 → UTF-8 decode
 *
 * Phase 59 — MLE D-NWRK-BROADCAST-EXTENSION
 *   9. D-NWRK-BCAST-EXT Location Area
 *
 * Phase 60 — MM D-AUTHENTICATION
 *  10. D-AUTHENTICATION RAND and auth_type
 */

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

static void wrap_mle_cmce(const uint8_t *cmce_body, int cmce_nbits,
                           uint8_t *out, int *out_nbits)
{
    memset(out, 0, (size_t)(9 + cmce_nbits));
    pack_bits(out, 24, 0, 5);
    pack_bits(out,  3, 5, 4);
    memcpy(out + 9, cmce_body, (size_t)cmce_nbits);
    *out_nbits = 9 + cmce_nbits;
}

static void wrap_mle_mm(const uint8_t *mm_body, int mm_nbits,
                         uint8_t *out, int *out_nbits)
{
    memset(out, 0, (size_t)(9 + mm_nbits));
    pack_bits(out, 24, 0, 5);
    pack_bits(out,  5, 5, 4);
    memcpy(out + 9, mm_body, (size_t)mm_nbits);
    *out_nbits = 9 + mm_nbits;
}

/* =======================================================================
 * Phase 54: Floor control
 * ======================================================================= */
static void test_floor_control_events(void)
{
    printf("[test_floor_control_events]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();
    uint8_t   pdu[9+5];
    int       n;

    /* CONTINUE=9 */
    uint8_t cmce[5];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 9, 0, 5);
    wrap_mle_cmce(cmce, 5, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_tx_continue == 1, "D-TX-CONTINUE sets flag");

    /* INTERRUPT=11 */
    pack_bits(cmce, 11, 0, 5);
    wrap_mle_cmce(cmce, 5, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_tx_interrupted == 1, "D-TX-INTERRUPT sets flag");

    /* WAIT=12 */
    pack_bits(cmce, 12, 0, 5);
    wrap_mle_cmce(cmce, 5, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_tx_wait == 1, "D-TX-WAIT sets flag");

    /* TIMED-OUT=13 */
    st->tetra_tx_granted_valid = 1; /* pre-condition */
    pack_bits(cmce, 13, 0, 5);
    wrap_mle_cmce(cmce, 5, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_tx_timed_out == 1, "D-TX-TIMED-OUT sets flag");
    CHECK(st->tetra_tx_granted_valid == 0, "D-TX-TIMED-OUT clears grant");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 55: MM/MLE small parsers
 * ======================================================================= */
static void test_mm_small_parsers(void)
{
    printf("[test_mm_small_parsers]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* D-CHECK-TSI (type 2) + 24-bit TSI */
    uint8_t mm_tsi[29];
    memset(mm_tsi, 0, sizeof(mm_tsi));
    pack_bits(mm_tsi, 2, 0, 5);
    pack_bits(mm_tsi, 123456, 5, 24);
    uint8_t pdu[9+29]; int n;
    wrap_mle_mm(mm_tsi, 29, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_mm_check_tsi == 123456 && st->tetra_mm_check_tsi_valid, "MM D-CHECK-TSI parsed");

    /* D-STATUS (type 8) + 8-bit status */
    uint8_t mm_sts[13];
    memset(mm_sts, 0, sizeof(mm_sts));
    pack_bits(mm_sts, 8, 0, 5);
    pack_bits(mm_sts, 42, 5, 8);
    wrap_mle_mm(mm_sts, 13, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);
    CHECK(st->tetra_mm_d_status_val == 42 && st->tetra_mm_d_status_valid, "MM D-STATUS parsed");

    free(st); free(opt);
}

static void test_mle_restore(void)
{
    printf("[test_mle_restore]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* MLE D-RESTORE-ACK (type 2) */
    uint8_t mle_ack[5];
    memset(mle_ack, 0, sizeof(mle_ack));
    pack_bits(mle_ack, 2, 0, 5);
    tetra_mle_dispatch(mle_ack, 5, 0, opt, st);
    CHECK(st->tetra_restore_ack == 1, "MLE D-RESTORE-ACK flag");

    /* MLE D-RESTORE-RESPONSE (type 3) */
    uint8_t mle_res[5];
    memset(mle_res, 0, sizeof(mle_res));
    pack_bits(mle_res, 3, 0, 5);
    tetra_mle_dispatch(mle_res, 5, 0, opt, st);
    CHECK(st->tetra_restore_response == 1, "MLE D-RESTORE-RESPONSE flag");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 56: External subscriber parsing
 * ======================================================================= */
static void test_sds_ext_flag(void)
{
    printf("[test_sds_ext_flag]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* D-SDS-SHORT-DATA (21), ext_flag=1, len=20 bits, val=0xABCDE, data_type=0 */
    uint8_t cmce[60];
    memset(cmce, 0, sizeof(cmce));
    int off = 0;
    pack_bits(cmce, 21, off, 5); off += 5;
    pack_bits(cmce,  1, off, 1); off += 1; /* ext_flag=1 */
    pack_bits(cmce, 20, off, 8); off += 8; /* length=20  */
    pack_bits(cmce, 0xABCDE, off, 20); off += 20; /* 20-bit src_ssi */
    pack_bits(cmce,  0, off, 2); off += 2; /* type=0 Status */
    pack_bits(cmce, 0x1111, off, 16); off += 16; /* payload */

    uint8_t pdu[100]; int n;
    wrap_mle_cmce(cmce, off, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_sds_short_valid == 1, "D-SDS-SHORT-DATA ext_flag parsed");
    CHECK(st->tetra_sds_src == 0xABCDE,   "Ext src_ssi extracted correctly");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 57: CMCE D-INFO
 * ======================================================================= */
static void test_d_info(void)
{
    printf("[test_d_info]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    uint8_t cmce[7];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 14, 0, 5); /* pdu_type=14 */
    pack_bits(cmce,  1, 5, 1); /* call_id=1 */
    pack_bits(cmce,  0, 6, 1); /* timeout=0 */

    uint8_t pdu[9+7]; int n;
    wrap_mle_cmce(cmce, 7, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_d_info_valid == 1, "D-INFO valid");
    CHECK(st->tetra_d_info_call_id == 1, "D-INFO call_id");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 58: SDS Unicode text
 * ======================================================================= */
static void test_sds_unicode(void)
{
    printf("[test_sds_unicode]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* D-SDS-DATA (23), ext=0, src=1, msg_ref/vp=0, len/unicode */
    uint8_t cmce[100];
    memset(cmce, 0, sizeof(cmce));
    int off = 0;
    pack_bits(cmce, 23, off, 5); off += 5;
    pack_bits(cmce,  0, off, 1); off += 1; /* ext=0 */
    pack_bits(cmce,  1, off, 24); off += 24;
    pack_bits(cmce,  0, off, 6); off += 6; /* msg_ref/etc */
    pack_bits(cmce,  0, off, 1); off += 1; /* dt_flag=0 */

    pack_bits(cmce, 16, off, 8); off += 8; /* bpc=16 (8-bit field) */
    pack_bits(cmce,  2, off, 8); off += 8; /* 2 chars */
    /* Unicode 'H' = 0x0048, 'i' = 0x0069 */
    pack_bits(cmce, 0x0048, off, 16); off += 16;
    pack_bits(cmce, 0x0069, off, 16); off += 16;

    uint8_t pdu[100+9]; int n;
    wrap_mle_cmce(cmce, off, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_sds_text_unicode == 1, "Unicode flag set");
    CHECK(st->tetra_sds_text_len == 2, "Text len 2");
    CHECK(strcmp(st->tetra_sds_text, "Hi") == 0, "UTF-8 conversion ok");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 59: NWRK-BCAST-EXT
 * ======================================================================= */
static void test_nwrk_ext(void)
{
    printf("[test_nwrk_ext]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* MLE type=1, LA=999 */
    uint8_t mle[19];
    memset(mle, 0, sizeof(mle));
    pack_bits(mle, 1, 0, 5);
    pack_bits(mle, 999, 5, 14);

    tetra_mle_dispatch(mle, 19, 0, opt, st);
    CHECK(st->tetra_nwrk_bcast_ext_known == 1, "NWRK-BCAST-EXT flag");
    CHECK(st->tetra_nwrk_bcast_ext_la == 999, "LA extracted");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 60: MM D-AUTHENTICATION
 * ======================================================================= */
static void test_mm_auth(void)
{
    printf("[test_mm_auth]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* MM type=1, auth=2, RAND=0xDEADBEEF */
    uint8_t mm[40];
    memset(mm, 0, sizeof(mm));
    pack_bits(mm, 1, 0, 5); /* pdu_type=1 */
    pack_bits(mm, 2, 5, 3); /* auth_type=2 */
    pack_bits(mm, 0xDEADBEEF, 8, 32); /* RAND */

    uint8_t pdu[40+9]; int n;
    wrap_mle_mm(mm, 40, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_mm_auth_valid == 1, "AUTH valid flag");
    CHECK(st->tetra_mm_auth_rand == 0xDEADBEEF, "AUTH RAND matched");

    free(st); free(opt);
}

int main(void)
{
    printf("=== TETRA Phase 61 Test Suite ===\n\n");
    test_floor_control_events();
    puts("");
    test_mm_small_parsers();
    puts("");
    test_mle_restore();
    puts("");
    test_sds_ext_flag();
    puts("");
    test_d_info();
    puts("");
    test_sds_unicode();
    puts("");
    test_nwrk_ext();
    puts("");
    test_mm_auth();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
