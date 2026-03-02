// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA Phase 85 test suite.
 *
 * Covers functionality introduced in Phases 81-84:
 *
 * Phase 81: tetra_bits.h shared header (bits_to_uint unification)
 * Phase 82: D-SDS-LONG-DATA (PDU type 20) parser
 * Phase 83: D-FACILITY / D-SDS-ACK / D-SDS-SHORT-REPORT parsers
 * Phase 84: channel_info_fmt extensions (release cause, access, mm_addr)
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/protocol/tetra/tetra_bits.h>
#include <dsd-neo/protocol/tetra/tetra_channel_info.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/opts.h>

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
    int total = 9 + cmce_nbits;
    memset(out, 0, (size_t)total);
    pack_bits(out, 24, 0, 5); /* MLE type = C-PLANE-DATA */
    pack_bits(out,  3, 5, 4); /* PD = CMCE */
    memcpy(out + 9, cmce_body, (size_t)cmce_nbits);
    *out_nbits = total;
}

/* -----------------------------------------------------------------------
 * Phase 81: tetra_bits_to_uint shared utility
 * ----------------------------------------------------------------------- */
static void test_tetra_bits_to_uint(void)
{
    printf("[Phase 81] tetra_bits_to_uint shared header\n");

    /* Pack 0xAB = 10101011 into bit array */
    uint8_t bits[32];
    memset(bits, 0, sizeof(bits));
    pack_bits(bits, 0xAB, 0, 8);

    uint32_t v = tetra_bits_to_uint(bits, 0, 8);
    CHECK(v == 0xAB, "tetra_bits_to_uint full byte 0xAB");

    uint32_t hi = tetra_bits_to_uint(bits, 0, 4);
    CHECK(hi == 0x0A, "tetra_bits_to_uint high nibble 0x0A");

    uint32_t lo = tetra_bits_to_uint(bits, 4, 4);
    CHECK(lo == 0x0B, "tetra_bits_to_uint low nibble 0x0B");

    /* 24-bit value */
    pack_bits(bits, 0x123456, 0, 24);
    v = tetra_bits_to_uint(bits, 0, 24);
    CHECK(v == 0x123456u, "tetra_bits_to_uint 24-bit value");
}

/* -----------------------------------------------------------------------
 * Phase 82: D-SDS-LONG-DATA (PDU type 20)
 * ----------------------------------------------------------------------- */
static void test_d_sds_long_data(void)
{
    printf("[Phase 82] D-SDS-LONG-DATA parser\n");

    dsd_state *st = alloc_state();
    dsd_opts  *op = alloc_opts();

    /* Build CMCE PDU type 20 with ext_flag=0, SSI, msg_ref, len_ind, bpc=8, 3 chars "Hi!" */
    uint8_t cmce[256];
    memset(cmce, 0, sizeof(cmce));

    int off = 0;
    pack_bits(cmce, 20, off, 5); off += 5;   /* pdu_type = 20 */
    pack_bits(cmce,  0, off, 1); off += 1;   /* ext_flag = 0 (SSI follows) */
    pack_bits(cmce, 99, off, 24); off += 24; /* calling_ssi = 99 */
    pack_bits(cmce,  5, off, 4); off += 4;   /* msg_ref = 5 */
    pack_bits(cmce, 24, off, 10); off += 10; /* length_indicator: 24 bits of SDS data */
    pack_bits(cmce,  8, off, 8); off += 8;   /* bpc = 8 */
    pack_bits(cmce,  3, off, 8); off += 8;   /* num_chars = 3 */
    pack_bits(cmce, 'H', off, 8); off += 8;
    pack_bits(cmce, 'i', off, 8); off += 8;
    pack_bits(cmce, '!', off, 8); off += 8;

    uint8_t mle[512];
    int mle_nbits = 0;
    wrap_mle_cmce(cmce, off, mle, &mle_nbits);
    tetra_mle_dispatch(mle, mle_nbits, 1, op, st);

    CHECK(st->tetra_sds_long_valid == 1, "D-SDS-LONG-DATA valid flag set");
    CHECK(st->tetra_sds_src == 99, "D-SDS-LONG-DATA src_ssi=99");
    CHECK(st->tetra_sds_long_text_len == 3, "D-SDS-LONG-DATA text_len=3");
    CHECK(strcmp(st->tetra_sds_long_text, "Hi!") == 0, "D-SDS-LONG-DATA text='Hi!'");
    CHECK(st->tetra_sds_long_text_unicode == 0, "D-SDS-LONG-DATA not unicode");

    free(st);
    free(op);
}

static void test_d_sds_long_data_truncated(void)
{
    printf("[Phase 82] D-SDS-LONG-DATA truncated\n");

    dsd_state *st = alloc_state();
    dsd_opts  *op = alloc_opts();

    /* Only 20 bits — too short */
    uint8_t cmce[32];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 20, 0, 5); /* pdu_type = 20 */

    uint8_t mle[64];
    int mle_nbits = 0;
    wrap_mle_cmce(cmce, 20, mle, &mle_nbits);
    tetra_mle_dispatch(mle, mle_nbits, 1, op, st);

    CHECK(st->tetra_sds_long_valid == 0, "truncated D-SDS-LONG-DATA not marked valid");

    free(st);
    free(op);
}

/* -----------------------------------------------------------------------
 * Phase 83: D-FACILITY (PDU type 15)
 * ----------------------------------------------------------------------- */
static void test_d_facility(void)
{
    printf("[Phase 83] D-FACILITY parser\n");

    dsd_state *st = alloc_state();
    dsd_opts  *op = alloc_opts();

    uint8_t cmce[16];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 15, 0, 5); /* pdu_type = 15 */
    pack_bits(cmce,  7, 5, 4); /* fac_type = 7 */

    uint8_t mle[32];
    int mle_nbits = 0;
    wrap_mle_cmce(cmce, 9, mle, &mle_nbits);
    tetra_mle_dispatch(mle, mle_nbits, 1, op, st);

    CHECK(st->tetra_facility_valid == 1, "D-FACILITY valid flag set");
    CHECK(st->tetra_facility_type == 7, "D-FACILITY fac_type=7");

    free(st);
    free(op);
}

/* -----------------------------------------------------------------------
 * Phase 83: D-SDS-ACK (PDU type 17)
 * ----------------------------------------------------------------------- */
static void test_d_sds_ack(void)
{
    printf("[Phase 83] D-SDS-ACK parser\n");

    dsd_state *st = alloc_state();
    dsd_opts  *op = alloc_opts();

    uint8_t cmce[16];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 17, 0, 5); /* pdu_type = 17 */
    pack_bits(cmce, 12, 5, 4); /* msg_ref = 12 */

    uint8_t mle[32];
    int mle_nbits = 0;
    wrap_mle_cmce(cmce, 9, mle, &mle_nbits);
    tetra_mle_dispatch(mle, mle_nbits, 1, op, st);

    CHECK(st->tetra_sds_ack_valid == 1, "D-SDS-ACK valid flag set");
    CHECK(st->tetra_sds_ack_msg_ref == 12, "D-SDS-ACK msg_ref=12");

    free(st);
    free(op);
}

/* -----------------------------------------------------------------------
 * Phase 83: D-SDS-SHORT-REPORT (PDU type 18)
 * ----------------------------------------------------------------------- */
static void test_d_sds_short_report(void)
{
    printf("[Phase 83] D-SDS-SHORT-REPORT parser\n");

    dsd_state *st = alloc_state();
    dsd_opts  *op = alloc_opts();

    uint8_t cmce[16];
    memset(cmce, 0, sizeof(cmce));
    pack_bits(cmce, 18, 0, 5); /* pdu_type = 18 */
    pack_bits(cmce,  2, 5, 2); /* result = 2 (pending) */

    uint8_t mle[32];
    int mle_nbits = 0;
    wrap_mle_cmce(cmce, 7, mle, &mle_nbits);
    tetra_mle_dispatch(mle, mle_nbits, 1, op, st);

    CHECK(st->tetra_sds_short_report_valid == 1, "D-SDS-SHORT-REPORT valid");
    CHECK(st->tetra_sds_short_report_result == 2, "D-SDS-SHORT-REPORT result=2");

    free(st);
    free(op);
}

/* -----------------------------------------------------------------------
 * Phase 84: channel_info_fmt extensions
 * ----------------------------------------------------------------------- */
static void test_channel_info_release_cause(void)
{
    printf("[Phase 84] channel_info release cause\n");

    dsd_state *st = alloc_state();
    st->tetra_net_known = 1;
    st->tetra_mcc = 1;
    st->tetra_mnc = 1;
    st->tetra_colour = 1;
    st->tetra_cmce_release_cause_type = 1; /* flag that release cause is present */
    st->tetra_cmce_release_cause = 2;      /* pre-emption */

    char buf[512];
    tetra_channel_info_fmt(st, buf, sizeof(buf));
    CHECK(strstr(buf, "rel:pre-emption") != NULL, "channel_info shows release cause");

    free(st);
}

static void test_channel_info_access_common(void)
{
    printf("[Phase 84] channel_info access common flag\n");

    dsd_state *st = alloc_state();
    st->tetra_net_known = 1;
    st->tetra_mcc = 1;
    st->tetra_mnc = 1;
    st->tetra_colour = 1;
    st->tetra_access_common_flag = 1;

    char buf[512];
    tetra_channel_info_fmt(st, buf, sizeof(buf));
    CHECK(strstr(buf, "acc:common") != NULL, "channel_info shows access common");

    free(st);
}

static void test_channel_info_mm_addr_type(void)
{
    printf("[Phase 84] channel_info MM addr type\n");

    dsd_state *st = alloc_state();
    st->tetra_net_known = 1;
    st->tetra_mcc = 1;
    st->tetra_mnc = 1;
    st->tetra_colour = 1;
    st->tetra_mm_addr_type = 3; /* SMI */

    char buf[512];
    tetra_channel_info_fmt(st, buf, sizeof(buf));
    CHECK(strstr(buf, "mm_addr:SMI") != NULL, "channel_info shows mm_addr:SMI");

    free(st);
}

/* -----------------------------------------------------------------------
 * CMCE constant sanity checks
 * ----------------------------------------------------------------------- */
static void test_cmce_constants(void)
{
    printf("[Phase 83] CMCE constant definitions\n");

    CHECK(TETRA_CMCE_D_FACILITY == 15, "D-FACILITY type = 15");
    CHECK(TETRA_CMCE_D_SDS_ACK == 17, "D-SDS-ACK type = 17");
    CHECK(TETRA_CMCE_D_SDS_SHORT_REPORT == 18, "D-SDS-SHORT-REPORT type = 18");
    CHECK(TETRA_CMCE_D_SDS_LONG_DATA == 20, "D-SDS-LONG-DATA type = 20");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    printf("=== TETRA Phase 85 Tests ===\n\n");

    /* Phase 81 */
    test_tetra_bits_to_uint();

    /* Phase 82 */
    test_d_sds_long_data();    test_d_sds_long_data_truncated();

    /* Phase 83 */
    test_cmce_constants();
    test_d_facility();
    test_d_sds_ack();
    test_d_sds_short_report();

    /* Phase 84 */
    test_channel_info_release_cause();
    test_channel_info_access_common();
    test_channel_info_mm_addr_type();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
