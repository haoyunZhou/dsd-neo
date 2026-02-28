// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Unit tests for the TETRA trunking state machine (Phase 13).
 *
 * Uses dsd_trunk_tuning_hooks_set() to install counting stubs that track
 * whether tune_to_freq / return_to_cc were actually called.
 *
 * Link requirements:
 *   tetra_trunk_sm.c, dsd-neo_runtime, dsd-neo_core
 */

#include <dsd-neo/protocol/tetra/tetra_trunk_sm.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Test infrastructure
 * ----------------------------------------------------------------------- */
static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

/* -----------------------------------------------------------------------
 * Mock hooks
 * ----------------------------------------------------------------------- */
static int g_tune_calls    = 0;
static long g_tuned_freq   = 0;
static int g_release_calls = 0;

static void
mock_tune_to_freq(dsd_opts *opts, dsd_state *state, long int freq, int ted_sps)
{
    (void)state; (void)ted_sps;
    g_tune_calls++;
    g_tuned_freq = freq;
    if (opts) opts->trunk_is_tuned = 1;
}

static void
mock_return_to_cc(dsd_opts *opts, dsd_state *state)
{
    (void)state;
    g_release_calls++;
    if (opts) opts->trunk_is_tuned = 0;
}

static void
install_hooks(void)
{
    dsd_trunk_tuning_hooks h = {0};
    h.tune_to_freq = mock_tune_to_freq;
    h.return_to_cc = mock_return_to_cc;
    dsd_trunk_tuning_hooks_set(h);
}

static void
reset_counters(void)
{
    g_tune_calls    = 0;
    g_tuned_freq    = 0;
    g_release_calls = 0;
}

/* Build a minimal opts/state pair ready for trunking (heap-allocated). */
static void
make_pair(dsd_opts **opts_out, dsd_state **state_out)
{
    dsd_opts  *opts  = (dsd_opts  *)calloc(1, sizeof(dsd_opts));
    dsd_state *state = (dsd_state *)calloc(1, sizeof(dsd_state));
    opts->trunk_enable   = 1;
    opts->trunk_hangtime = 5.0f; /* 5 second hangtime */
    state->trunk_cc_freq = 380000000L; /* 380 MHz CC */
    state->tetra_dl_carrier_hz = state->trunk_cc_freq;
    state->samplesPerSymbol = 10;
    *opts_out  = opts;
    *state_out = state;
}

/* -----------------------------------------------------------------------
 * Test 1: on_grant with trunking enabled → tune hook called
 * ----------------------------------------------------------------------- */
static void
test_grant_tunes(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);

    CHECK(g_tune_calls == 1,   "tune hook must be called once on grant");
    CHECK(g_tuned_freq == 390000000L, "tuned to correct VC frequency");
    CHECK(tetra_sm_get_state() == TETRA_SM_TUNED, "SM must be TUNED");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_grant_tunes\n");
}

/* -----------------------------------------------------------------------
 * Test 2: on_grant with trunk_enable=0 → no tuning
 * ----------------------------------------------------------------------- */
static void
test_grant_disabled(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    opts->trunk_enable = 0;
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);

    CHECK(g_tune_calls == 0, "tune hook must NOT be called when trunking disabled");
    CHECK(tetra_sm_get_state() == TETRA_SM_IDLE, "SM must remain IDLE");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_grant_disabled\n");
}

/* -----------------------------------------------------------------------
 * Test 3: duplicate grant same freq → no second tune, hangtime refreshed
 * ----------------------------------------------------------------------- */
static void
test_double_grant_same_freq(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);
    int first_calls = g_tune_calls;
    tetra_sm_on_grant(opts, state, 390000000L, 1);

    CHECK(first_calls == 1, "first grant must tune");
    CHECK(g_tune_calls == 1, "duplicate grant (same freq) must NOT re-tune");
    CHECK(tetra_sm_get_state() == TETRA_SM_TUNED, "SM must remain TUNED");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_double_grant_same_freq\n");
}

/* -----------------------------------------------------------------------
 * Test 4: on_release after grant → return_to_cc hook called, SM on ON_CC
 * ----------------------------------------------------------------------- */
static void
test_release_returns_to_cc(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);
    reset_counters();
    tetra_sm_on_release(opts, state);

    CHECK(g_release_calls == 1,  "return_to_cc hook must be called on release");
    CHECK(g_tune_calls    == 0,  "no tune during release");
    CHECK(tetra_sm_get_state() == TETRA_SM_ON_CC, "SM must be ON_CC after release");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_release_returns_to_cc\n");
}

/* -----------------------------------------------------------------------
 * Test 5: trunk_cc_freq=0 → grant blocked even if trunk_enable=1
 * ----------------------------------------------------------------------- */
static void
test_no_cc_freq_blocks_grant(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    state->trunk_cc_freq = 0L; /* no CC known */
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);

    CHECK(g_tune_calls == 0, "grant must be blocked when CC freq unknown");
    CHECK(tetra_sm_get_state() == TETRA_SM_IDLE, "SM must remain IDLE");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_no_cc_freq_blocks_grant\n");
}

/* -----------------------------------------------------------------------
 * Test 6: on_cc_sync transitions IDLE→ON_CC
 * ----------------------------------------------------------------------- */
static void
test_cc_sync_transitions_state(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    reset_counters();
    tetra_sm_init();

    CHECK(tetra_sm_get_state() == TETRA_SM_IDLE, "SM starts IDLE");
    tetra_sm_on_cc_sync(opts, state);
    CHECK(tetra_sm_get_state() == TETRA_SM_ON_CC, "SM transitions to ON_CC after cc_sync");
    /* subsequent cc_sync while ON_CC must keep ON_CC state */
    tetra_sm_on_cc_sync(opts, state);
    CHECK(tetra_sm_get_state() == TETRA_SM_ON_CC, "SM stays ON_CC on repeated cc_sync");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_cc_sync_transitions_state\n");
}

/* -----------------------------------------------------------------------
 * Test 7: tick while TUNED with expired hangtime → release
 *
 * Use hangtime=0.0f so that any real elapsed time triggers the release.
 * ----------------------------------------------------------------------- */
static void
test_hangtime_expires(void)
{
    dsd_opts *opts; dsd_state *state;
    make_pair(&opts, &state);
    opts->trunk_hangtime = 0.0f; /* expire immediately */
    reset_counters();
    tetra_sm_init();

    tetra_sm_on_grant(opts, state, 390000000L, 1);
    CHECK(tetra_sm_get_state() == TETRA_SM_TUNED, "SM must be TUNED before tick");

    reset_counters();
    tetra_sm_tick(opts, state); /* hangtime=0 → should release */

    CHECK(g_release_calls == 1, "return_to_cc must be called when hangtime expires");
    CHECK(tetra_sm_get_state() == TETRA_SM_ON_CC, "SM must be ON_CC after hangtime");
    free(opts); free(state);
    fprintf(stderr, "  PASS test_hangtime_expires\n");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    install_hooks();

    fprintf(stderr, "=== TETRA SM tests ===\n");
    test_grant_tunes();
    test_grant_disabled();
    test_double_grant_same_freq();
    test_release_returns_to_cc();
    test_no_cc_freq_blocks_grant();
    test_cc_sync_transitions_state();
    test_hangtime_expires();
    fprintf(stderr, "=== %d failure(s) ===\n", g_failures);

    /* Reset hooks so other tests are not affected */
    dsd_trunk_tuning_hooks_set((dsd_trunk_tuning_hooks){0});

    return g_failures != 0 ? 1 : 0;
}
