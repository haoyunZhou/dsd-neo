// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Unit tests for TETRA TCH timeslot audio gate (Phase 14).
 *
 * Tests tetra_acelp_slot_gate_passes(block_idx, state) which implements:
 *   1. NULL-state permissive pass-through
 *   2. Phase 12 floor-grant gate (tetra_tx_granted_valid)
 *   3. Phase 14 slot filter (tetra_vc_slot vs block_idx)
 *
 * This test only compiles tetra_tch_slot_gate.c — it does NOT need the
 * full ACELP pipeline or any audio/platform libraries.
 */

#include <dsd-neo/protocol/tetra/tetra_acelp.h>
#include <dsd-neo/core/state.h>

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

/* Allocate a zeroed state ready for audio tests. */
static dsd_state *
alloc_granted_state(uint8_t vc_slot)
{
    dsd_state *s = (dsd_state *)calloc(1, sizeof(dsd_state));
    s->tetra_tx_granted_valid = 1;
    s->tetra_vc_slot = vc_slot;
    return s;
}

/* -----------------------------------------------------------------------
 * Test 1: NULL state → permissive pass
 * ----------------------------------------------------------------------- */
static void
test_null_state_passes(void)
{
    CHECK(tetra_acelp_slot_gate_passes(1, NULL) == 1, "NULL state: block 1 must pass");
    CHECK(tetra_acelp_slot_gate_passes(2, NULL) == 1, "NULL state: block 2 must pass");
    fprintf(stderr, "  PASS test_null_state_passes\n");
}

/* -----------------------------------------------------------------------
 * Test 2: tx_granted_valid == 0 → suppress regardless of slot
 * ----------------------------------------------------------------------- */
static void
test_no_grant_suppresses_all(void)
{
    dsd_state *s = (dsd_state *)calloc(1, sizeof(dsd_state));
    s->tetra_tx_granted_valid = 0;
    s->tetra_vc_slot = 0; /* no filter */

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 0, "no grant: block 1 must be suppressed");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 0, "no grant: block 2 must be suppressed");

    free(s);
    fprintf(stderr, "  PASS test_no_grant_suppresses_all\n");
}

/* -----------------------------------------------------------------------
 * Test 3: vc_slot == 0 (no assignment) → pass both
 * ----------------------------------------------------------------------- */
static void
test_slot0_passes_both(void)
{
    dsd_state *s = alloc_granted_state(0);

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 1, "slot=0: block 1 must pass");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 1, "slot=0: block 2 must pass");

    free(s);
    fprintf(stderr, "  PASS test_slot0_passes_both\n");
}

/* -----------------------------------------------------------------------
 * Test 4: vc_slot == 1 → only block 1 passes
 * ----------------------------------------------------------------------- */
static void
test_slot1_filter(void)
{
    dsd_state *s = alloc_granted_state(1);

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 1, "slot=1: block 1 must pass");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 0, "slot=1: block 2 must be suppressed");

    free(s);
    fprintf(stderr, "  PASS test_slot1_filter\n");
}

/* -----------------------------------------------------------------------
 * Test 5: vc_slot == 2 → only block 2 passes
 * ----------------------------------------------------------------------- */
static void
test_slot2_filter(void)
{
    dsd_state *s = alloc_granted_state(2);

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 0, "slot=2: block 1 must be suppressed");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 1, "slot=2: block 2 must pass");

    free(s);
    fprintf(stderr, "  PASS test_slot2_filter\n");
}

/* -----------------------------------------------------------------------
 * Test 6: vc_slot == 3 (dual-slot) → pass both
 * ----------------------------------------------------------------------- */
static void
test_slot3_passes_both(void)
{
    dsd_state *s = alloc_granted_state(3);

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 1, "slot=3: block 1 must pass (dual)");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 1, "slot=3: block 2 must pass (dual)");

    free(s);
    fprintf(stderr, "  PASS test_slot3_passes_both\n");
}

/* -----------------------------------------------------------------------
 * Test 7: no grant overrides slot filter (slot 1 assigned, no grant)
 * ----------------------------------------------------------------------- */
static void
test_no_grant_overrides_slot(void)
{
    dsd_state *s = alloc_granted_state(1);
    s->tetra_tx_granted_valid = 0; /* revoke grant after alloc */

    CHECK(tetra_acelp_slot_gate_passes(1, s) == 0,
          "no grant overrides slot: block 1 must be suppressed even with slot=1");

    free(s);
    fprintf(stderr, "  PASS test_no_grant_overrides_slot\n");
}

/* -----------------------------------------------------------------------
 * Test 8: slot 1 granted then slot changes to 2 → block 1 now suppressed
 * ----------------------------------------------------------------------- */
static void
test_slot_change_updates_filter(void)
{
    dsd_state *s = alloc_granted_state(1);
    CHECK(tetra_acelp_slot_gate_passes(1, s) == 1, "before change: block 1 passes with slot=1");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 0, "before change: block 2 suppressed with slot=1");

    s->tetra_vc_slot = 2; /* new grant on slot 2 */
    CHECK(tetra_acelp_slot_gate_passes(1, s) == 0, "after change: block 1 suppressed with slot=2");
    CHECK(tetra_acelp_slot_gate_passes(2, s) == 1, "after change: block 2 passes with slot=2");

    free(s);
    fprintf(stderr, "  PASS test_slot_change_updates_filter\n");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
    fprintf(stderr, "=== TETRA TCH slot gate tests ===\n");

    test_null_state_passes();
    test_no_grant_suppresses_all();
    test_slot0_passes_both();
    test_slot1_filter();
    test_slot2_filter();
    test_slot3_passes_both();
    test_no_grant_overrides_slot();
    test_slot_change_updates_filter();

    fprintf(stderr, "=== %d failure(s) ===\n", g_failures);
    return g_failures != 0 ? 1 : 0;
}
