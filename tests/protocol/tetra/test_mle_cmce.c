// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA MLE dispatch + CMCE D-SETUP / D-RELEASE call-state tests.
 *
 * Verifies that tetra_mle_dispatch() correctly updates dsd_state when it
 * processes MLE C-PLANE-DATA PDUs carrying CMCE D-SETUP, D-RELEASE, and
 * D-CONNECT messages.
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/core/state.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Write @nbits bits from @val (MSB-first) into out[offset..]. */
static void pack_bits(uint8_t *out, uint32_t val, int offset, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--)
        out[offset++] = (uint8_t)((val >> i) & 1u);
}

static dsd_state *alloc_state(void) { return (dsd_state *)calloc(1, sizeof(dsd_state)); }
static dsd_opts  *alloc_opts(void)  { return (dsd_opts  *)calloc(1, sizeof(dsd_opts));  }

/* -------------------------------------------------------------------------
 * Test 1: MLE C-PLANE-DATA + CMCE D-SETUP with calling party SSI.
 *
 * TM-SDU bit layout (49 bits total):
 *   [0-4]   MLE type   = 24 (11000b, TETRA_MLE_C_PLANE_DATA)
 *   [5-8]   PD         =  3 (0011b,  TETRA_MLE_PD_CMCE)
 *   [9-13]  CMCE type  =  6 (00110b, TETRA_CMCE_D_SETUP)
 *   [14]    call_id    = 0
 *   [15]    call_timeout = 0
 *   [16-18] call_type  =  2 (010b, acknowledged call)
 *   [19]    duplex     = 1
 *   [20]    notif_ind  = 0
 *   [21]    com_type   = 0
 *   [22]    slots      = 0
 *   [23]    calling_party_present = 1
 *   [24]    calling_party_type    = 0 (SSI)
 *   [25-48] calling_party_ssi     = TEST_CALLING_SSI (24 bits)
 * Expected: tetra_call_active=1, tetra_call_type=2, tetra_calling_ssi=TEST_SSI
 * ------------------------------------------------------------------------- */
static int test_d_setup(void)
{
    const uint32_t TEST_CALLING_SSI = 0x7AB123u; /* 8040739 */
    const int NBITS = 49;

    uint8_t bits[49];
    memset(bits, 0, sizeof bits);

    /* MLE header */
    pack_bits(bits, 24u,              0, 5); /* MLE type = C_PLANE_DATA  */
    pack_bits(bits,  3u,              5, 4); /* PD      = CMCE           */
    /* CMCE D-SETUP */
    pack_bits(bits,  6u,              9, 5); /* CMCE type = D-SETUP      */
    pack_bits(bits,  0u,             14, 1); /* call_id  = 0             */
    pack_bits(bits,  0u,             15, 1); /* call_timeout = 0         */
    pack_bits(bits,  2u,             16, 3); /* call_type = 2 (ack)      */
    pack_bits(bits,  1u,             19, 1); /* duplex   = 1             */
    pack_bits(bits,  0u,             20, 1); /* notif    = 0             */
    pack_bits(bits,  0u,             21, 1); /* com_type = 0             */
    pack_bits(bits,  0u,             22, 1); /* slots    = 0             */
    pack_bits(bits,  1u,             23, 1); /* calling_party_present=1  */
    pack_bits(bits,  0u,             24, 1); /* calling_party_type=0(SSI)*/
    pack_bits(bits, TEST_CALLING_SSI, 25, 24); /* SSI (24 bits)          */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;

    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_setup): tetra_call_active not set\n");
        ok = 0;
    }
    if (state->tetra_call_type != 2) {
        fprintf(stderr, "FAIL(d_setup): tetra_call_type=%u expect=2\n",
                (unsigned)state->tetra_call_type);
        ok = 0;
    }
    if (state->tetra_calling_ssi != TEST_CALLING_SSI) {
        fprintf(stderr, "FAIL(d_setup): tetra_calling_ssi=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_calling_ssi,
                (unsigned)TEST_CALLING_SSI);
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 2: MLE C-PLANE-DATA + CMCE D-RELEASE clears call_active.
 *
 *   [0-4]   MLE type   = 24
 *   [5-8]   PD         =  3 (CMCE)
 *   [9-13]  CMCE type  =  5 (00101b, D-RELEASE)
 *   [14]    cause_type = 0 (standard)
 *   [15-18] cause      = 5 (0101b)
 * Expected: tetra_call_active goes from 1 → 0
 * ------------------------------------------------------------------------- */
static int test_d_release(void)
{
    const int NBITS = 19;

    uint8_t bits[19];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5); /* MLE C_PLANE_DATA                        */
    pack_bits(bits,  3u, 5, 4); /* PD = CMCE                               */
    pack_bits(bits,  5u, 9, 5); /* CMCE type = D-RELEASE (00101b)          */
    pack_bits(bits,  0u, 14, 1); /* cause_type = 0 (standard)              */
    pack_bits(bits,  5u, 15, 4); /* cause = 5                              */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    /* Pre-condition: simulate an active call */
    state->tetra_call_active = 1;

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (state->tetra_call_active != 0) {
        fprintf(stderr, "FAIL(d_release): tetra_call_active=%u expect=0\n",
                (unsigned)state->tetra_call_active);
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 3: MLE C-PLANE-DATA + CMCE D-CONNECT sets call_active.
 *
 *   [0-4]   MLE type   = 24
 *   [5-8]   PD         =  3 (CMCE)
 *   [9-13]  CMCE type  =  2 (00010b, D-CONNECT)
 * Expected: tetra_call_active = 1
 * ------------------------------------------------------------------------- */
static int test_d_connect(void)
{
    const int NBITS = 14;

    uint8_t bits[14];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5); /* MLE C_PLANE_DATA                        */
    pack_bits(bits,  3u, 5, 4); /* PD = CMCE                               */
    pack_bits(bits,  2u, 9, 5); /* CMCE type = D-CONNECT (00010b)          */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_connect): tetra_call_active not set\n");
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 4: D-SETUP without calling party IE — calling_ssi stays 0.
 *
 * Same as Test 1 but calling_party_present = 0.
 * Expected: tetra_call_active=1, tetra_call_type=1, tetra_calling_ssi=0
 * ------------------------------------------------------------------------- */
static int test_d_setup_no_calling_party(void)
{
    const int NBITS = 24;   /* up to and including calling_party_present=0 */

    uint8_t bits[24];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u,  0, 5); /* MLE C_PLANE_DATA                       */
    pack_bits(bits,  3u,  5, 4); /* PD = CMCE                              */
    pack_bits(bits,  6u,  9, 5); /* CMCE D-SETUP                           */
    /* call_id=0, call_timeout=0 */
    pack_bits(bits,  1u, 16, 3); /* call_type = 1 (unacknowledged group)   */
    /* duplex=0, notif=0, com_type=0, slots=0 */
    pack_bits(bits,  0u, 23, 1); /* calling_party_present = 0              */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_setup_no_cp): tetra_call_active not set\n");
        ok = 0;
    }
    if (state->tetra_call_type != 1) {
        fprintf(stderr, "FAIL(d_setup_no_cp): tetra_call_type=%u expect=1\n",
                (unsigned)state->tetra_call_type);
        ok = 0;
    }
    if (state->tetra_calling_ssi != 0) {
        fprintf(stderr, "FAIL(d_setup_no_cp): tetra_calling_ssi=%u expect=0\n",
                (unsigned)state->tetra_calling_ssi);
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 5: CMCE D-TX-GRANTED with granted party SSI.
 *
 * TM-SDU layout (46 bits):
 *   [0-4]   MLE type   = 24  (C_PLANE_DATA)
 *   [5-8]   PD         =  3  (CMCE)
 *   [9-13]  CMCE type  = 10  (D_TX_GRANTED, 01010b)
 *   [14]    tx_perm    =  1
 *   [15-16] enc_mode   =  0  (2 bits)
 *   [17]    reserv     =  0
 *   [18]    gp_present =  1
 *   [19-21] addr_type  =  1  (SSI)
 *   [22-45] granted_ssi = TEST_GRANTED_SSI  (24 bits)
 * Expected: tetra_tx_granted_valid=1, tetra_tx_granted_ssi=TEST_GRANTED_SSI
 * ------------------------------------------------------------------------- */
static int test_d_tx_granted(void)
{
    const uint32_t TEST_GRANTED_SSI = 0x4C1A77u;  /* 5053047 */
    const int NBITS = 46;

    uint8_t bits[46];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u,              0, 5); /* MLE C_PLANE_DATA              */
    pack_bits(bits,  3u,              5, 4); /* PD = CMCE                     */
    pack_bits(bits, 10u,              9, 5); /* CMCE type = D_TX_GRANTED(10)  */
    pack_bits(bits,  1u,             14, 1); /* tx_perm = 1                   */
    pack_bits(bits,  0u,             15, 2); /* enc_mode = 0                  */
    pack_bits(bits,  0u,             17, 1); /* reserv = 0                    */
    pack_bits(bits,  1u,             18, 1); /* gp_present = 1                */
    pack_bits(bits,  1u,             19, 3); /* addr_type = 1 (SSI)           */
    pack_bits(bits, TEST_GRANTED_SSI, 22, 24); /* granted SSI                 */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_tx_granted_valid) {
        fprintf(stderr, "FAIL(d_tx_granted): tetra_tx_granted_valid not set\n");
        ok = 0;
    }
    if (state->tetra_tx_granted_ssi != TEST_GRANTED_SSI) {
        fprintf(stderr, "FAIL(d_tx_granted): tetra_tx_granted_ssi=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_tx_granted_ssi,
                (unsigned)TEST_GRANTED_SSI);
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 6: CMCE D-TX-CEASED clears tetra_tx_granted_valid.
 *
 *   [0-4]   MLE type  = 24
 *   [5-8]   PD        =  3 (CMCE)
 *   [9-13]  CMCE type =  8 (D_TX_CEASED, 01000b)
 * Pre-condition: tetra_tx_granted_valid=1
 * Expected: tetra_tx_granted_valid=0
 * ------------------------------------------------------------------------- */
static int test_d_tx_ceased(void)
{
    const int NBITS = 14;

    uint8_t bits[14];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5); /* MLE C_PLANE_DATA                         */
    pack_bits(bits,  3u, 5, 4); /* PD = CMCE                                */
    pack_bits(bits,  8u, 9, 5); /* CMCE D_TX_CEASED (01000b = 8)            */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();
    state->tetra_tx_granted_valid = 1;  /* pre-condition: grant was active   */

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (state->tetra_tx_granted_valid != 0) {
        fprintf(stderr, "FAIL(d_tx_ceased): tetra_tx_granted_valid=%u expect=0\n",
                (unsigned)state->tetra_tx_granted_valid);
        ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 7: D-SETUP with calling party SSI + called party GSSI.
 *
 * MLE bit layout (76 bits):
 *   [0-4]   MLE type   = 24  (C_PLANE_DATA)
 *   [5-8]   PD         =  3  (CMCE)
 *   [9-13]  CMCE type  =  6  (D-SETUP)
 *   [14]    call_id    =  1  (→ tetra_call_id should be 1)
 *   [15]    call_timeout = 0
 *   [16-18] call_type  =  0  (group)
 *   [19-22] duplex/notif/com_type/slots = 0
 *   [23]    calling_party_present = 1
 *   [24]    calling_party_type    = 0 (SSI)
 *   [25-48] calling_party_ssi     = TEST_CALLING_SSI (24 bits)
 *   [49]    called_party_present  = 1
 *   [50-51] called_party_type     = 1 (GSSI/group)
 *   [52-75] called_party_ssi      = TEST_GSSI (24 bits)
 * Expected: tetra_gssi=TEST_GSSI, tetra_call_id=1, lasttg=TEST_GSSI,
 *           lastsrc=TEST_CALLING_SSI, active_channel[0] starts with "TETRA "
 * ------------------------------------------------------------------------- */
static int test_d_setup_with_gssi(void)
{
    const uint32_t TEST_CALLING_SSI = 0xAB1234u;
    const uint32_t TEST_GSSI        = 0x0D1234u;  /* talkgroup */
    const int NBITS = 76;

    uint8_t bits[76];
    memset(bits, 0, sizeof bits);

    /* MLE header */
    pack_bits(bits, 24u,             0, 5);   /* MLE C_PLANE_DATA          */
    pack_bits(bits,  3u,             5, 4);   /* PD = CMCE                 */
    /* CMCE D-SETUP */
    pack_bits(bits,  6u,             9, 5);   /* CMCE type = D-SETUP       */
    pack_bits(bits,  1u,            14, 1);   /* call_id = 1               */
    pack_bits(bits,  0u,            15, 1);   /* call_timeout = 0          */
    pack_bits(bits,  0u,            16, 3);   /* call_type = 0 (group)     */
    pack_bits(bits,  0u,            19, 1);   /* duplex = 0                */
    pack_bits(bits,  0u,            20, 1);   /* notif = 0                 */
    pack_bits(bits,  0u,            21, 1);   /* com_type = 0              */
    pack_bits(bits,  0u,            22, 1);   /* slots = 0                 */
    /* Calling party IE */
    pack_bits(bits,  1u,            23, 1);   /* calling_party_present = 1 */
    pack_bits(bits,  0u,            24, 1);   /* calling_party_type = SSI  */
    pack_bits(bits, TEST_CALLING_SSI, 25, 24); /* calling SSI (24 bits)    */
    /* Called party IE  (CMCE off=40 → MLE bit 9+40=49) */
    pack_bits(bits,  1u,            49, 1);   /* called_party_present = 1  */
    pack_bits(bits,  1u,            50, 2);   /* called_party_type = 1 (GSSI) */
    pack_bits(bits, TEST_GSSI,      52, 24);  /* called SSI = GSSI         */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_setup_gssi): tetra_call_active not set\n"); ok = 0;
    }
    if (state->tetra_call_type != 0) {
        fprintf(stderr, "FAIL(d_setup_gssi): tetra_call_type=%u expect=0\n",
                (unsigned)state->tetra_call_type); ok = 0;
    }
    if (state->tetra_call_id != 1) {
        fprintf(stderr, "FAIL(d_setup_gssi): tetra_call_id=%u expect=1\n",
                (unsigned)state->tetra_call_id); ok = 0;
    }
    if (state->tetra_calling_ssi != TEST_CALLING_SSI) {
        fprintf(stderr, "FAIL(d_setup_gssi): tetra_calling_ssi=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_calling_ssi, TEST_CALLING_SSI); ok = 0;
    }
    if (state->tetra_gssi != TEST_GSSI) {
        fprintf(stderr, "FAIL(d_setup_gssi): tetra_gssi=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_gssi, TEST_GSSI); ok = 0;
    }
    if (state->lasttg != (int)TEST_GSSI) {
        fprintf(stderr, "FAIL(d_setup_gssi): lasttg=%d expect=%u\n",
                state->lasttg, TEST_GSSI); ok = 0;
    }
    if (state->lastsrc != (int)TEST_CALLING_SSI) {
        fprintf(stderr, "FAIL(d_setup_gssi): lastsrc=%d expect=%u\n",
                state->lastsrc, TEST_CALLING_SSI); ok = 0;
    }
    if (state->active_channel[0][0] == '\0') {
        fprintf(stderr, "FAIL(d_setup_gssi): active_channel[0] is empty\n"); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 8: D-SETUP with NO calling party IE but WITH called party GSSI.
 *
 * MLE bit layout (51 bits):
 *   [0-4]   MLE type   = 24
 *   [5-8]   PD         =  3  (CMCE)
 *   [9-13]  CMCE type  =  6  (D-SETUP)
 *   [14]    call_id    =  0
 *   [15]    call_timeout = 0
 *   [16-18] call_type  =  0  (group)
 *   [19-22] duplex/notif/com_type/slots = 0
 *   [23]    calling_party_present = 0  (absent → off advances to CMCE bit 15)
 *   [24]    called_party_present  = 1  (CMCE bit 15 → MLE bit 9+15=24)
 *   [25-26] called_party_type     = 1  (GSSI)
 *   [27-50] called_party_ssi      = TEST_GSSI2 (24 bits)
 * Expected: tetra_gssi=TEST_GSSI2, lasttg=TEST_GSSI2, tetra_calling_ssi=0
 * ------------------------------------------------------------------------- */
static int test_d_setup_gssi_no_calling(void)
{
    const uint32_t TEST_GSSI2 = 0x050505u;
    const int NBITS = 51;

    uint8_t bits[51];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u,       0, 5);   /* MLE C_PLANE_DATA              */
    pack_bits(bits,  3u,       5, 4);   /* PD = CMCE                     */
    pack_bits(bits,  6u,       9, 5);   /* CMCE D-SETUP                  */
    pack_bits(bits,  0u,      14, 1);   /* call_id = 0                   */
    pack_bits(bits,  0u,      15, 1);   /* call_timeout = 0              */
    pack_bits(bits,  0u,      16, 3);   /* call_type = 0 (group)         */
    pack_bits(bits,  0u,      19, 4);   /* duplex/notif/com_type/slots=0 */
    /* off = 14 after fixed fields */
    pack_bits(bits,  0u,      23, 1);   /* calling_party_present = 0  [CMCE bit 14 = MLE bit 23] */
    /* off = 15 → called party IE at CMCE bit 15 = MLE bit 24 */
    pack_bits(bits,  1u,      24, 1);   /* called_party_present = 1      */
    pack_bits(bits,  1u,      25, 2);   /* called_party_type = 1 (GSSI)  */
    pack_bits(bits, TEST_GSSI2, 27, 24); /* called SSI                   */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(gssi_no_calling): tetra_call_active not set\n"); ok = 0;
    }
    if (state->tetra_calling_ssi != 0) {
        fprintf(stderr, "FAIL(gssi_no_calling): tetra_calling_ssi=%u expect=0\n",
                (unsigned)state->tetra_calling_ssi); ok = 0;
    }
    if (state->tetra_gssi != TEST_GSSI2) {
        fprintf(stderr, "FAIL(gssi_no_calling): tetra_gssi=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_gssi, TEST_GSSI2); ok = 0;
    }
    if (state->lasttg != (int)TEST_GSSI2) {
        fprintf(stderr, "FAIL(gssi_no_calling): lasttg=%d expect=%u\n",
                state->lasttg, TEST_GSSI2); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 9: D-ALERT sets tetra_call_active.
 *
 *   [0-4]  MLE type  = 24  (C_PLANE_DATA)
 *   [5-8]  PD        =  3  (CMCE)
 *   [9-13] CMCE type =  0  (D-ALERT)
 * Expected: tetra_call_active=1
 * ------------------------------------------------------------------------- */
static int test_d_alert(void)
{
    const int NBITS = 14;

    uint8_t bits[14];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5);  /* MLE C_PLANE_DATA                       */
    pack_bits(bits,  3u, 5, 4);  /* PD = CMCE                              */
    pack_bits(bits,  0u, 9, 5);  /* CMCE type = D-ALERT (00000b = 0)       */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_alert): tetra_call_active not set\n"); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 10: D-CALL-PROCEEDING sets tetra_call_active.
 *
 *   [0-4]  MLE type  = 24
 *   [5-8]  PD        =  3  (CMCE)
 *   [9-13] CMCE type =  1  (D-CALL-PROCEEDING)
 * Expected: tetra_call_active=1
 * ------------------------------------------------------------------------- */
static int test_d_call_proceeding(void)
{
    const int NBITS = 14;

    uint8_t bits[14];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u, 0, 5);  /* MLE C_PLANE_DATA                       */
    pack_bits(bits,  3u, 5, 4);  /* PD = CMCE                              */
    pack_bits(bits,  1u, 9, 5);  /* CMCE type = D-CALL-PROCEEDING (00001b) */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (!state->tetra_call_active) {
        fprintf(stderr, "FAIL(d_call_proc): tetra_call_active not set\n"); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 11: D-STATUS without calling party — pre-coded status stored.
 *
 * CMCE PDU bit layout (relative to CMCE start, CMCE PDU passed at MLE bit 9):
 *   CMCE[0-4]  = PDU type = 7  (D-STATUS)
 *   CMCE[5-20] = pre_coded_status = TEST_STATUS (16 bits)
 * No calling party IE (buffer ends at CMCE bit 21, i.e. MLE bit 30).
 *
 * Expected: tetra_sds_status=TEST_STATUS, tetra_sds_src=0
 * ------------------------------------------------------------------------- */
static int test_d_status(void)
{
    const uint32_t TEST_STATUS = 0x1A2Bu;   /* arbitrary 16-bit value */
    const int NBITS = 30;  /* MLE 5b + PD 4b + CMCE type 5b + status 16b  */

    uint8_t bits[30];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u,        0, 5);   /* MLE C_PLANE_DATA               */
    pack_bits(bits,  3u,        5, 4);   /* PD = CMCE                      */
    pack_bits(bits,  7u,        9, 5);   /* CMCE type = D-STATUS (00111b)  */
    /* pre-coded status at CMCE bits 5-20 = MLE bits 14-29 */
    pack_bits(bits, TEST_STATUS, 14, 16); /* pre-coded status               */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (state->tetra_sds_status != (uint16_t)TEST_STATUS) {
        fprintf(stderr, "FAIL(d_status): tetra_sds_status=0x%04X expect=0x%04X\n",
                (unsigned)state->tetra_sds_status, (unsigned)TEST_STATUS); ok = 0;
    }
    if (state->tetra_sds_src != 0) {
        fprintf(stderr, "FAIL(d_status): tetra_sds_src=%u expect=0\n",
                (unsigned)state->tetra_sds_src); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Test 12: D-STATUS with calling party SSI — src stored.
 *
 * CMCE bit layout:
 *   CMCE[0-4]   = 7  (D-STATUS)
 *   CMCE[5-20]  = TEST_STATUS (16 bits)
 *   CMCE[21]    = calling_party_present = 1
 *   CMCE[22]    = calling_party_type    = 0 (SSI)
 *   CMCE[23-46] = TEST_SRC_SSI (24 bits)
 * Total CMCE bits = 47, MLE total = 9 + 47 = 56 bits.
 *
 * Expected: tetra_sds_status=TEST_STATUS, tetra_sds_src=TEST_SRC_SSI
 * ------------------------------------------------------------------------- */
static int test_d_status_with_src(void)
{
    const uint32_t TEST_STATUS  = 0xFF01u;
    const uint32_t TEST_SRC_SSI = 0xC0FFEEu;
    const int NBITS = 56;

    uint8_t bits[56];
    memset(bits, 0, sizeof bits);

    pack_bits(bits, 24u,          0, 5);   /* MLE C_PLANE_DATA              */
    pack_bits(bits,  3u,          5, 4);   /* PD = CMCE                     */
    pack_bits(bits,  7u,          9, 5);   /* CMCE type = D-STATUS          */
    /* CMCE bits 5-20 = MLE bits 14-29 */
    pack_bits(bits, TEST_STATUS,  14, 16); /* pre-coded status              */
    /* CMCE bit 21 = MLE bit 30 */
    pack_bits(bits,  1u,          30, 1);  /* calling_party_present = 1     */
    /* CMCE bit 22 = MLE bit 31 */
    pack_bits(bits,  0u,          31, 1);  /* calling_party_type = 0 (SSI)  */
    /* CMCE bits 23-46 = MLE bits 32-55 */
    pack_bits(bits, TEST_SRC_SSI, 32, 24); /* src SSI                       */

    dsd_state *state = alloc_state();
    dsd_opts  *opts  = alloc_opts();

    tetra_mle_dispatch(bits, NBITS, 0, opts, state);

    int ok = 1;
    if (state->tetra_sds_status != (uint16_t)TEST_STATUS) {
        fprintf(stderr, "FAIL(d_status_src): tetra_sds_status=0x%04X expect=0x%04X\n",
                (unsigned)state->tetra_sds_status, (unsigned)TEST_STATUS); ok = 0;
    }
    if (state->tetra_sds_src != TEST_SRC_SSI) {
        fprintf(stderr, "FAIL(d_status_src): tetra_sds_src=0x%06X expect=0x%06X\n",
                (unsigned)state->tetra_sds_src, TEST_SRC_SSI); ok = 0;
    }

    free(state);
    free(opts);
    return ok;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */
int main(void)
{
    int passed = 0, total = 0;

#define RUN(fn)                                         \
    do {                                                \
        total++;                                        \
        if (fn()) {                                     \
            fprintf(stderr, "PASS: " #fn "\n");         \
            passed++;                                   \
        } else {                                        \
            fprintf(stderr, "FAIL: " #fn "\n");         \
        }                                               \
    } while (0)

    RUN(test_d_setup);
    RUN(test_d_release);
    RUN(test_d_connect);
    RUN(test_d_setup_no_calling_party);
    RUN(test_d_tx_granted);
    RUN(test_d_tx_ceased);
    RUN(test_d_setup_with_gssi);
    RUN(test_d_setup_gssi_no_calling);
    RUN(test_d_alert);
    RUN(test_d_call_proceeding);
    RUN(test_d_status);
    RUN(test_d_status_with_src);

#undef RUN

    fprintf(stderr, "\n%d / %d passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
