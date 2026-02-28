// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA Phase 76 test suite.
 *
 * Covers functionality introduced in Phases 71-75:
 *
 * Phase 71 — cmce_d_sds_data truncates stale text
 *   1. Send truncated SDS-DATA and ensure tetra_sds_text_len = 0
 *
 * Phase 72 — D-SDS-SHORT-DATA data_types 2 and 3
 *   2. data_type=2 (64-bit user defined)
 *   3. data_type=3 (variable length user defined)
 *
 * Phase 73 — CMCE D-SDS-REPORT (type 22)
 *   4. Parses D-SDS-REPORT and sets delivery_ok / cause
 *
 * Phase 74 — MAC ACCESS-DEFINE exact state caching
 *   5. Parses MAC ACCESS-DEFINE and saves num_ra, frame_len_f, ts_ptr, min_pdu_pri
 *
 * Phase 75 — tetra_channel_info_fmt extension
 *   6. Handles TDMA timestamp (TN/FN/MN)
 *   7. Handles VC freq
 *   8. Handles SDS valid text
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/protocol/tetra/tetra_mac.h>
#include <dsd-neo/protocol/tetra/tetra_channel_info.h>
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

/* Wrap a CMCE body in MLE C-PLANE-DATA + PD=CMCE header (9 overhead bits) */
static void wrap_mle_cmce(const uint8_t *cmce_body, int cmce_nbits,
                           uint8_t *out, int *out_nbits)
{
    int total = 9 + cmce_nbits;
    memset(out, 0, (size_t)total);
    pack_bits(out, 24, 0, 5); /* MLE type = C-PLANE-DATA */
    pack_bits(out,  3, 5, 4); /* PD = CMCE */
    memcpy(out + 9, cmce_body, (size_t)cmce_nbits);
    *out_nbits = total;
}

/* =======================================================================
 * Phase 71: CMCE D-SDS-DATA truncated leaves no stale state
 * ======================================================================= */
static void test_sds_data_truncation(void)
{
    printf("[test_sds_data_truncation]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* Prime state with fake previous message */
    st->tetra_sds_text_len = 5;
    strcpy(st->tetra_sds_text, "HELLO");

    /* Truncated CMCE type=23 (D-SDS-DATA), missing SDS-TL bits but enough to parse SSI */
    uint8_t cmce[40];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 23, 0, 5); /* D-SDS-DATA */
    pack_bits(cmce,  0, 5, 1); /* external = 0 */
    pack_bits(cmce, 111, 6, 24); /* calling_ssi */
    /* Cut it short: only 36 bits total, so it reaches log_only */

    uint8_t pdu[40 + 9]; int n;
    wrap_mle_cmce(cmce, 36, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    /* Since parsing truncated at log_only, text_len should be cleared */
    CHECK(st->tetra_sds_text_len == 0,     "Stale text_len cleared on truncated D-SDS-DATA");
    CHECK(st->tetra_sds_text[0] == '\0',   "Stale text buffer cleared on truncated D-SDS-DATA");
    CHECK(st->tetra_sds_src == 111,        "src_ssi still extracted before truncation");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 72: CMCE D-SDS-SHORT-DATA types 2 and 3
 * ======================================================================= */
static void test_sds_short_data_types(void)
{
    printf("[test_sds_short_data_types]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* 1) Type 2: 64-bit user defined -> bits 48-63 = 0xAA55 */
    uint8_t cmce2[100];
    memset(cmce2, 0, sizeof(cmce2));
    pack_bits(cmce2, 21, 0, 5); /* D-SDS-SHORT-DATA */
    pack_bits(cmce2,  0, 5, 1); /* external = 0 */
    pack_bits(cmce2, 222, 6, 24); /* calling_ssi */
    pack_bits(cmce2,  2, 30, 2); /* data_type = 2 */
    pack_bits(cmce2, 0, 32, 48); /* bits 0..47 */
    pack_bits(cmce2, 0xAA55, 32+48, 16); /* bits 48..63 = 0xAA55 */

    uint8_t pdu2[100 + 9]; int n2;
    wrap_mle_cmce(cmce2, 32 + 64, pdu2, &n2);
    tetra_mle_dispatch(pdu2, n2, 0, opt, st);
    
    CHECK(st->tetra_sds_short_valid == 1,       "Short data flag set (type 2)");
    CHECK(st->tetra_sds_short_data == 0xAA55,   "Short data (type 2) extracted bits 48-63");

    /* 2) Type 3: Variable length (10-bit len + data) -> len=12, data=0x888 */
    st->tetra_sds_short_valid = 0;
    st->tetra_sds_short_data = 0;

    uint8_t cmce3[100];
    memset(cmce3, 0, sizeof(cmce3));
    pack_bits(cmce3, 21, 0, 5); /* D-SDS-SHORT-DATA */
    pack_bits(cmce3,  0, 5, 1); /* external = 0 */
    pack_bits(cmce3, 333, 6, 24); /* calling_ssi */
    pack_bits(cmce3,  3, 30, 2); /* data_type = 3 */
    pack_bits(cmce3, 12, 32, 10); /* length = 12 */
    pack_bits(cmce3, 0x888, 42, 12); /* data = 0x888 */

    uint8_t pdu3[100 + 9]; int n3;
    wrap_mle_cmce(cmce3, 42 + 12, pdu3, &n3);
    tetra_mle_dispatch(pdu3, n3, 0, opt, st);
    
    CHECK(st->tetra_sds_short_valid == 1,       "Short data flag set (type 3)");
    CHECK(st->tetra_sds_short_data == 0x0888,   "Short data (type 3) extracted variable data");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 73: CMCE D-SDS-REPORT (22)
 * ======================================================================= */
static void test_sds_report(void)
{
    printf("[test_sds_report]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* Report: Delivered (ok) */
    uint8_t cmce[16];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 22, 0, 5); /* D-SDS-REPORT */
    pack_bits(cmce,  0, 5, 1); /* 0 = Delivered */

    uint8_t pdu[16 + 9]; int n;
    wrap_mle_cmce(cmce, 10, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_sds_report_valid == 1,       "SDS Report flag set");
    CHECK(st->tetra_sds_report_delivery_ok == 1, "Delivered");

    /* Report: Not delivered (ok=0), cause=7 */
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 22, 0, 5); /* D-SDS-REPORT */
    pack_bits(cmce,  1, 5, 1); /* 1 = Not delivered */
    pack_bits(cmce,  7, 6, 4); /* cause = 7 */

    wrap_mle_cmce(cmce, 12, pdu, &n);
    tetra_mle_dispatch(pdu, n, 0, opt, st);

    CHECK(st->tetra_sds_report_delivery_ok == 0, "Not delivered");
    CHECK(st->tetra_sds_report_cause == 7,       "Extracted cause");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 74: MAC ACCESS-DEFINE Fields
 * ======================================================================= */
static void test_mac_access_define(void)
{
    printf("[test_mac_access_define]\n");
    dsd_state *st  = alloc_state();
    dsd_opts  *opt = alloc_opts();

    /* MAC-BROADCAST (2) -> ACCESS-DEFINE (1) */
    uint8_t mac[50];
    memset(mac, 0, sizeof(mac));
    pack_bits(mac, 2, 0, 2); /* PDU = BROADCAST */
    pack_bits(mac, 1, 2, 2); /* BROADCAST type = ACCESS-DEFINE */
    pack_bits(mac, 1, 4, 1); /* common */
    pack_bits(mac, 5, 5, 4); /* imm = 5 */
    pack_bits(mac, 2, 9, 4); /* wait = 2 */
    pack_bits(mac, 9, 13, 4); /* num_ra = 9 */
    pack_bits(mac, 1, 17, 1); /* frame_len_f = 1 */
    pack_bits(mac, 4, 18, 4); /* ts_ptr = 4 */
    pack_bits(mac, 3, 22, 3); /* min_prio = 3 */

    tetra_mac_parse_schd(mac, 25, 0, opt, st);

    CHECK(st->tetra_access_imm == 5,         "Extracted immediate");
    CHECK(st->tetra_access_wait_time == 2,   "Extracted wait time");
    CHECK(st->tetra_access_num_ra == 9,      "Extracted num_ra");
    CHECK(st->tetra_access_frame_len_f == 1, "Extracted frame_len_f");
    CHECK(st->tetra_access_ts_ptr == 4,      "Extracted ts_ptr");
    CHECK(st->tetra_access_min_pdu_pri == 3, "Extracted min_pdu_pri");

    free(st); free(opt);
}

/* =======================================================================
 * Phase 75: channel_info format extensions
 * ======================================================================= */
static void test_channel_info_fmt(void)
{
    printf("[test_channel_info_fmt]\n");
    dsd_state *st = alloc_state();
    
    /* Base net setup */
    st->tetra_net_known = 1;
    st->tetra_mcc = 123;
    st->tetra_mnc = 45;
    st->tetra_colour = 6;
    st->tetra_enc_mode = 0; /* none */

    /* TDMA Phase 75 */
    st->tetra_tdma_valid = 1;
    st->tetra_tn = 1;
    st->tetra_fn = 2;
    st->tetra_mn = 3;

    /* VC freq Phase 75 */
    st->tetra_vc_freq_hz = 405125000;

    /* SDS Text Phase 75 */
    st->tetra_sds_src = 999;
    strcpy(st->tetra_sds_text, "TESTMSG");
    st->tetra_sds_text_len = 7;

    char buf[256];
    tetra_channel_info_fmt(st, buf, sizeof(buf));

    CHECK(strstr(buf, "TN:1 FN:2 MN:3") != NULL,      "Includes TDMA timestamp");
    CHECK(strstr(buf, "VC:405.125MHz") != NULL,       "Includes VC frequency");
    CHECK(strstr(buf, "SDS(999):\"TESTMSG\"") != NULL,"Includes SDS text and src");

    /* Clear text to ensure it's not printed when empty */
    st->tetra_sds_text_len = 0;
    st->tetra_sds_text[0] = '\0';
    tetra_channel_info_fmt(st, buf, sizeof(buf));
    CHECK(strstr(buf, "SDS") == NULL, "Omits SDS when empty");

    free(st);
}

int main(void)
{
    printf("=== TETRA Phase 76 Test Suite ===\n\n");

    test_sds_data_truncation();
    test_sds_short_data_types();
    test_sds_report();
    test_mac_access_define();
    test_channel_info_fmt();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
