// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA format-helper unit tests.  Phases 31-34.
 *
 * Tests:
 *  1.  tetra_enc_mode_name(0) == "none"
 *  2.  tetra_enc_mode_name(1) == "on"
 *  3.  tetra_enc_mode_name(2) == "on+auth"
 *  4.  tetra_enc_mode_name(3) == "rsvd"
 *  5.  tetra_mm_status_name(0) == "normal"
 *  6.  tetra_mm_status_name(1) == "roaming not allowed"
 *  7.  tetra_mm_status_name(15) == "service disabled"
 *  8.  tetra_bsch_fmt_net(NULL) writes "MCC:? MNC:? CC:?"
 *  9.  tetra_bsch_fmt_net with net_known=0 writes "MCC:? MNC:? CC:?"
 * 10.  tetra_bsch_fmt_net with known values formats correctly
 * 11.  tetra_channel_info_fmt(NULL) writes "?"
 * 12.  tetra_channel_info_fmt with net_known=0, call inactive formats "MCC:? ..."
 * 13.  tetra_channel_info_fmt includes enc mode
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/protocol/tetra/tetra_mm.h>
#include <dsd-neo/protocol/tetra/tetra_bsch_fmt.h>
#include <dsd-neo/protocol/tetra/tetra_channel_info.h>
#include <dsd-neo/core/state.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s\n", msg); \
            g_fail++; \
        } \
    } while (0)

#define CHECK_STR(got, exp, msg) \
    do { \
        if (strcmp((got), (exp)) == 0) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s  (got \"%s\", expected \"%s\")\n", msg, (got), (exp)); \
            g_fail++; \
        } \
    } while (0)

static dsd_state *alloc_state(void) { return (dsd_state *)calloc(1, sizeof(dsd_state)); }

/* ----------------------------------------------------------------------- */
int main(void)
{
    printf("[TETRA fmt helpers]\n");

    /* --- tetra_enc_mode_name --- */
    CHECK_STR(tetra_enc_mode_name(0), "none",    "enc_mode 0 == none");
    CHECK_STR(tetra_enc_mode_name(1), "on",      "enc_mode 1 == on");
    CHECK_STR(tetra_enc_mode_name(2), "on+auth", "enc_mode 2 == on+auth");
    CHECK_STR(tetra_enc_mode_name(3), "rsvd",    "enc_mode 3 == rsvd");

    /* --- tetra_mm_status_name --- */
    CHECK_STR(tetra_mm_status_name(0),  "normal",              "mm_status 0 == normal");
    CHECK_STR(tetra_mm_status_name(1),  "roaming not allowed", "mm_status 1 == roaming not allowed");
    CHECK_STR(tetra_mm_status_name(15), "service disabled",    "mm_status 15 == service disabled");

    /* --- tetra_bsch_fmt_net --- */
    {
        char buf[64];
        tetra_bsch_fmt_net(NULL, buf, sizeof(buf));
        CHECK_STR(buf, "MCC:? MNC:? CC:?", "bsch_fmt_net(NULL) == '?'");

        dsd_state *st = alloc_state();
        tetra_bsch_fmt_net(st, buf, sizeof(buf));
        CHECK_STR(buf, "MCC:? MNC:? CC:?", "bsch_fmt_net net_unknown == '?'");

        st->tetra_net_known = 1;
        st->tetra_mcc       = 234;
        st->tetra_mnc       = 30;
        st->tetra_colour    = 42;
        tetra_bsch_fmt_net(st, buf, sizeof(buf));
        CHECK_STR(buf, "MCC:234 MNC:30 CC:42", "bsch_fmt_net known values");
        free(st);
    }

    /* --- tetra_channel_info_fmt --- */
    {
        char buf[256];
        tetra_channel_info_fmt(NULL, buf, sizeof(buf));
        CHECK_STR(buf, "?", "channel_info_fmt(NULL) == '?'");

        dsd_state *st = alloc_state();
        tetra_channel_info_fmt(st, buf, sizeof(buf));
        /* Should contain "MCC:?" and "enc:none" */
        CHECK(strstr(buf, "MCC:?") != NULL,    "channel_info_fmt includes MCC:?");
        CHECK(strstr(buf, "enc:none") != NULL,  "channel_info_fmt includes enc:none");

        /* With enc_mode=1 */
        st->tetra_enc_mode = 1;
        tetra_channel_info_fmt(st, buf, sizeof(buf));
        CHECK(strstr(buf, "enc:on") != NULL, "channel_info_fmt enc:on");

        free(st);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
