// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA MLE PDU dispatcher and CMCE call-control decoder.
 * ETSI EN 300 392-2 §21.6 (D-MLE) and Chapter 14 (CMCE).
 *
 * Phase 9: decode the TM-SDU carried by MAC-RESOURCE to extract CMCE
 * call-setup / release events and update dsd_state accordingly.
 */

#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/protocol/tetra/tetra_mm.h>
#include <dsd-neo/protocol/tetra/tetra_trunk_sm.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dsd-neo/protocol/tetra/tetra_bits.h>

/* Phase 81: bits_to_uint unified — see tetra_bits.h */
#define mle_bits_to_uint  tetra_bits_to_uint

/* -----------------------------------------------------------------------
 * CMCE D-SETUP parser
 *
 * Fixed PDU layout (bits relative to start of CMCE PDU):
 *   Bits  0-4  : pdu_type = 6  (already consumed by caller)
 *   Bit     5  : call_id (TI)
 *   Bit     6  : call_timeout
 *   Bits  7-9  : call_type (3 bits)
 *   Bit    10  : simplex_duplex
 *   Bit    11  : notification_ind
 *   Bit    12  : com_type
 *   Bit    13  : slots
 *   Bit    14  : calling_party_present (1 = calling IE follows)
 *   Bit    15  : calling_party_type    (0 = SSI, 1 = USSI/SNA)
 *   Bits 16-39 : calling_party_ssi     (24 bits, if type == SSI)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_setup(const uint8_t *bits, int nbits,
                                int cc, dsd_state *state)
{
    /* Need at least fixed 14 bits (after the 5-bit type that the caller
     * already consumed — we start at bit 5 of the original CMCE PDU,
     * but the caller passes the full CMCE PDU starting at bit 0 here). */
    if (nbits < 14) {
        fprintf(stderr, "[TETRA CMCE D-SETUP] CC=%d (too short: %d bits)\n",
                cc, nbits);
        return;
    }

    int off = 5; /* skip 5-bit pdu_type that the dispatcher already read */

    uint32_t call_id      = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t call_timeout = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t call_type    = mle_bits_to_uint(bits, off, 3); off += 3;
    uint32_t duplex       = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t notif        = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t com_type     = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t slots        = mle_bits_to_uint(bits, off, 1); off += 1;
    /* off == 14 here */

    const char *call_type_str[] = {
        "group", "unaack-group", "acknowledged", "SDS",
        "PSTN", "ISDN", "SDM", "rsvd"
    };

    fprintf(stderr, "[TETRA CMCE D-SETUP] CC=%d  call_id=%u  call_type=%s(%u)"
                    "  %s  notif=%u  com_type=%u  slots=%u  timeout=%u",
            cc,
            call_id,
            call_type < 8 ? call_type_str[call_type] : "?", call_type,
            duplex ? "duplex" : "simplex",
            notif, com_type, slots, call_timeout);

    /* Optional calling-party IE */
    uint32_t calling_ssi = 0;
    if (off < nbits) {
        uint32_t cp_present = mle_bits_to_uint(bits, off, 1); off += 1;
        if (cp_present && off + 1 <= nbits) {
            uint32_t cp_type = mle_bits_to_uint(bits, off, 1); off += 1;
            if (cp_type == 0 && off + 24 <= nbits) {
                /* SSI */
                calling_ssi = mle_bits_to_uint(bits, off, 24); off += 24;
                fprintf(stderr, "  calling_SSI=%u", calling_ssi);
            }
        }
    }

    /* Optional called-party IE (ETSI EN 300 392-2 §14.7.1.2, element 8).
     * Presence flag(1b) + type(2b: 0=SSI,1=GSSI,2=USSI,3=SNA) + SSI(24b). */
    uint32_t called_ssi  = 0;
    uint32_t called_type = 0;
    if (off < nbits) {
        uint32_t cld_present = mle_bits_to_uint(bits, off, 1); off += 1;
        if (cld_present && off + 2 <= nbits) {
            called_type = mle_bits_to_uint(bits, off, 2); off += 2;
            if (off + 24 <= nbits) {
                called_ssi = mle_bits_to_uint(bits, off, 24); off += 24;
                fprintf(stderr, "  called_SSI=%u(type=%u)", called_ssi, called_type);
            }
        }
    }

    fprintf(stderr, "\n");

    /* Update state */
    if (state) {
        state->tetra_call_type    = (uint8_t)(call_type & 0x07u);
        state->tetra_call_active  = 1;
        state->tetra_call_id      = (uint8_t)(call_id & 1u);
        state->tetra_call_timeout = (uint8_t)(call_timeout & 1u);
        state->tetra_call_slots   = (uint8_t)(slots & 1u);
        
        /* Phase 78: CMCE D-SETUP dropped variables */
        state->tetra_cmce_duplex      = (uint8_t)(duplex & 1u);
        state->tetra_cmce_notif       = (uint8_t)(notif & 1u);
        state->tetra_cmce_com_type    = (uint8_t)(com_type & 1u);
        state->tetra_cmce_called_type = (uint8_t)(called_type & 0x03u);
        state->tetra_call_timeout = (uint8_t)(call_timeout & 1u);
        state->tetra_call_slots   = (uint8_t)(slots & 1u);
        if (calling_ssi != 0)
            state->tetra_calling_ssi = calling_ssi;
        if (called_ssi != 0)
            state->tetra_gssi = called_ssi;
        /* Populate shared UI fields so watchdog_event_current() can display
         * active TETRA calls alongside DMR/P25/EDACS channels. */
        state->lasttg  = (int)(state->tetra_gssi ? state->tetra_gssi
                                                  : called_ssi);
        state->lastsrc = (int)(calling_ssi ? calling_ssi
                                           : state->tetra_calling_ssi);
        snprintf(state->active_channel[0], sizeof(state->active_channel[0]),
                 "TETRA TG:%u SRC:%u type=%u",
                 (unsigned)state->tetra_gssi,
                 (unsigned)state->tetra_calling_ssi,
                 (unsigned)call_type);
        state->last_active_time = time(NULL);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-RELEASE / D-DISCONNECT parser  (clear call state)
 *
 * D-RELEASE fixed layout (after 5-bit type):
 *   Bit  5 : disconnect_cause_type (0=standard, 1=non-standard)
 *   Bits 6-9 : disconnect_cause (4 bits, standard cause code)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_release(const uint8_t *bits, int nbits,
                                  int cc, int is_disconnect,
                                  dsd_opts *opts, dsd_state *state)
{
    uint32_t cause_type = 0, cause = 0;
    if (nbits >= 10) {
        cause_type = mle_bits_to_uint(bits, 5, 1);
        cause      = mle_bits_to_uint(bits, 6, 4);
    }
    fprintf(stderr, "[TETRA CMCE %s] CC=%d  cause_type=%u  cause=%u\n",
            is_disconnect ? "D-DISCONNECT" : "D-RELEASE",
            cc, cause_type, cause);

    if (state) {
        state->tetra_call_active = 0;
        state->tetra_cmce_release_cause_type = (uint8_t)(cause_type & 1u);
        state->tetra_cmce_release_cause      = (uint8_t)(cause & 0x0Fu);
        tetra_sm_on_release(opts, state);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-CONNECT parser  (call is now connected)
 *
 * Fixed layout (ETSI EN 300 392-2 §14.7.1.7):
 *   Bits 0-4  : pdu_type = 2
 *   Bit     5 : call_id (TI)
 *   Bits 6-8  : call_type (3 bits)
 *   Bit     9 : simplex_duplex
 *   Bits 10-11: encryption_mode (2 bits)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_connect(const uint8_t *bits, int nbits,
                                  int cc, dsd_state *state)
{
    uint32_t call_type = 0, enc_mode = 0;
    if (nbits >= 12) {
        call_type = mle_bits_to_uint(bits, 6, 3);
        enc_mode  = mle_bits_to_uint(bits, 10, 2);
    }
    fprintf(stderr, "[TETRA CMCE D-CONNECT] CC=%d  call_type=%u  enc=%u\n",
            cc, call_type, enc_mode);
    if (state) {
        state->tetra_call_active       = 1;
        state->tetra_connect_enc_mode  = (uint8_t)(enc_mode & 0x03u);
        state->tetra_connect_call_type = (uint8_t)(call_type & 0x07u);
        state->tetra_connect_valid     = 1;
        state->tetra_enc_mode          = (uint8_t)(enc_mode & 0x03u);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-TX-GRANTED (PDU type 10)  — ETSI EN 300 392-2 §14.7.3.5
 *
 * Fixed bits (relative to start of CMCE PDU):
 *   Bits 0-4  : pdu_type = 10             (consumed by dispatcher)
 *   Bit     5 : Transmission permission   (1 bit)
 *   Bits 6-7  : Encryption mode           (2 bits)
 *   Bit     8 : Reservation requirement   (1 bit)
 * Optional IEs (each prefixed by 1-bit presence flag):
 *   Bit     9 : Granted party identity present
 *   Bits 10-12: Address type (3 bits, if present)
 *   Bits 13-36: SSI          (24 bits, if addr_type == 1 = SSI)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_tx_granted(const uint8_t *bits, int nbits,
                                     int cc, dsd_opts *opts, dsd_state *state)
{
    if (nbits < 9) {
        fprintf(stderr, "[TETRA CMCE D-TX-GRANTED] CC=%d (too short: %d bits)\n",
                cc, nbits);
        return;
    }

    int off = 5; /* skip 5-bit PDU type */
    uint32_t tx_perm   = mle_bits_to_uint(bits, off, 1); off += 1;
    uint32_t enc_mode  = mle_bits_to_uint(bits, off, 2); off += 2;
    uint32_t reserv    = mle_bits_to_uint(bits, off, 1); off += 1;
    /* off == 9 */

    uint32_t granted_ssi = 0;
    uint8_t  got_ssi     = 0;

    if (off < nbits) {
        uint32_t gp_present = mle_bits_to_uint(bits, off, 1); off += 1;
        if (gp_present && off + 3 <= nbits) {
            uint32_t addr_type = mle_bits_to_uint(bits, off, 3); off += 3;
            if (addr_type == 1 /* SSI */ && off + 24 <= nbits) {
                granted_ssi = mle_bits_to_uint(bits, off, 24); off += 24;
                got_ssi = 1;
            }
        }
    }

    /* Phase 12: Assigned Channel IE (§14.7.3.5 Element 9)
     * Presence flag + 2-bit assignment type + optional carrier + timeslot. */
    uint32_t ac_type    = 0;
    uint32_t ac_carrier = 0;
    uint32_t ac_slot    = 0;
    uint8_t  got_ac     = 0;

    if (off < nbits) {
        uint32_t ac_present = mle_bits_to_uint(bits, off, 1); off += 1;
        if (ac_present && off + 2 <= nbits) {
            ac_type = mle_bits_to_uint(bits, off, 2); off += 2;
            got_ac  = 1;
            if (ac_type == 1 /* specific carrier */ && off + 14 <= nbits) {
                ac_carrier = mle_bits_to_uint(bits, off, 12); off += 12;
                ac_slot    = mle_bits_to_uint(bits, off,  2); off +=  2;
            } else if ((ac_type == 2 || ac_type == 3) && off + 2 <= nbits) {
                ac_slot = mle_bits_to_uint(bits, off, 2); off += 2;
            }
        }
    }

    fprintf(stderr, "[TETRA CMCE D-TX-GRANTED] CC=%d  perm=%u  enc=%u  reserv=%u",
            cc, tx_perm, enc_mode, reserv);
    if (got_ssi)
        fprintf(stderr, "  granted_SSI=%u", granted_ssi);
    if (got_ac)
        fprintf(stderr, "  ac_type=%u  ac_carrier=%u  ac_slot=%u",
                ac_type, ac_carrier, ac_slot);
    fprintf(stderr, "\n");

    if (state) {
        state->tetra_tx_granted_valid = 1;
        state->tetra_enc_mode = (uint8_t)(enc_mode & 0x03u);
        
        /* Phase 78: CMCE D-TX-GRANTED dropped variables */
        state->tetra_cmce_tx_granted_perm   = (uint8_t)(tx_perm & 1u);
        state->tetra_cmce_tx_granted_reserv = (uint8_t)(reserv & 1u);

        if (got_ssi) {
            state->tetra_tx_granted_ssi = granted_ssi;
            /* Phase 45: propagate floor grant to shared UI fields so the
             * active_channel display reflects the current speaker. */
            state->lastsrc = (int)granted_ssi;
            snprintf(state->active_channel[0], sizeof(state->active_channel[0]),
                     "TETRA TG:%u SRC:%u (TX-GRANTED)",
                     (unsigned)state->lasttg,
                     (unsigned)granted_ssi);
            state->last_active_time = time(NULL);
        }
        if (got_ac) {
            state->tetra_vc_assignment_type = (uint8_t)ac_type;
            state->tetra_vc_carrier         = (uint16_t)ac_carrier;
            state->tetra_vc_slot            = (uint8_t)ac_slot;
            /* Resolve VC frequency for new-carrier assignments. */
            if (ac_type == 1 && ac_carrier > 0) {
                long vc_hz = tetra_carrier_to_dl_hz(ac_carrier,
                                                    state->tetra_freq_band,
                                                    state->tetra_freq_offset);
                state->tetra_vc_freq_hz = vc_hz;
            } else {
                /* Same-carrier: VC is on the CC frequency. */
                state->tetra_vc_freq_hz = state->tetra_dl_carrier_hz;
            }
            tetra_sm_on_grant(opts, state,
                              state->tetra_vc_freq_hz,
                              state->tetra_vc_slot);
        }
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-TX-CEASED (PDU type 8)  — floor released.
 *
 * Fixed bits:
 *   Bits 0-4 : pdu_type = 8             (consumed by dispatcher)
 *   Bit    5 : Transmission permission   (1 bit)
 *   Bit    6 : Cipher info               (1 bit)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_tx_ceased(const uint8_t *bits, int nbits,
                                    int cc, dsd_opts *opts, dsd_state *state)
{
    uint8_t tx_perm    = 0;
    uint8_t cipher_res = 0;
    if (nbits >= 7) {
        tx_perm    = (uint8_t)mle_bits_to_uint(bits, 5, 1);
        cipher_res = (uint8_t)mle_bits_to_uint(bits, 6, 1);
    }
    fprintf(stderr, "[TETRA CMCE D-TX-CEASED] CC=%d  tx_perm=%u  cipher_res=%u  (floor released)\n",
            cc, tx_perm, cipher_res);
    if (state) {
        state->tetra_tx_ceased_tx_perm     = tx_perm;
        state->tetra_tx_ceased_cipher_info = cipher_res;
        state->tetra_tx_granted_valid      = 0;
        tetra_sm_on_release(opts, state);
    }
}

/* -----------------------------------------------------------------------
 * MLE D-NWRK-BROADCAST (MLE type 0)  —  ETSI EN 300 392-2 §21.6.1
 *
 * Carried in MAC-BROADCAST bcast_type 3 (TETRA_MAC_BC_NWRK_BCAST).
 * Mandatory fields after the 5-bit MLE PDU type:
 *   Bits  5-18  : Location Area       (14 bits)
 *   Bits 19-34  : Subscriber class    (16 bits)
 *   Bit     35  : Registration        ( 1 bit )
 *   (further mandatory / optional IEs not parsed here)
 * ----------------------------------------------------------------------- */
static void parse_mle_d_nwrk_broadcast(const uint8_t *bits, int nbits,
                                        int cc, dsd_state *state)
{
    if (nbits < 36) {
        fprintf(stderr, "[TETRA MLE D-NWRK-BROADCAST] CC=%d (too short: %d bits)\n",
                cc, nbits);
        return;
    }

    uint32_t la          = mle_bits_to_uint(bits, 5, 14);
    uint32_t subscr_cls  = mle_bits_to_uint(bits, 19, 16);
    uint32_t registration = mle_bits_to_uint(bits, 35, 1);

    fprintf(stderr, "[TETRA MLE D-NWRK-BROADCAST] CC=%d  LA=%u"
                    "  subscr_class=0x%04X  reg=%u\n",
            cc, la, subscr_cls, registration);

    if (state) {
        state->tetra_la               = (uint16_t)la;
        state->tetra_subscr_class     = (uint16_t)subscr_cls;
        state->tetra_nwrk_bcast_known = 1;
        /* Phase 78: dropped MLE D-NWRK-BROADCAST variable */
        state->tetra_mle_registration = (uint8_t)(registration & 1u);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-ALERT (PDU type 0)  — MS/BS is alerting (ringing).
 * Sets call_active=1 so the decoder knows a call is in progress.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_alert(const uint8_t *bits, int nbits,
                                int cc, dsd_state *state)
{
    uint8_t call_id = 0;
    if (nbits >= 9) {
        call_id = (uint8_t)mle_bits_to_uint(bits, 5, 4);
        fprintf(stderr, "[TETRA CMCE D-ALERT] CC=%d  call_id=%u  (alerting)\n",
                cc, call_id);
    } else {
        fprintf(stderr, "[TETRA CMCE D-ALERT] CC=%d  (alerting)\n", cc);
    }
    if (state) {
        state->tetra_d_alert_call_id = call_id;
        state->tetra_d_alert_valid   = 1;
        state->tetra_call_active     = 1;
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-CALL-PROCEEDING (PDU type 1)  — call is being processed.
 * Sets call_active=1.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_call_proceeding(const uint8_t *bits, int nbits,
                                          int cc, dsd_state *state)
{
    uint8_t call_id = 0;
    if (nbits >= 9) {
        call_id = (uint8_t)mle_bits_to_uint(bits, 5, 4);
        fprintf(stderr, "[TETRA CMCE D-CALL-PROCEEDING] CC=%d  call_id=%u\n",
                cc, call_id);
    } else {
        fprintf(stderr, "[TETRA CMCE D-CALL-PROCEEDING] CC=%d\n", cc);
    }
    if (state) {
        state->tetra_d_call_proc_call_id = call_id;
        state->tetra_d_call_proc_valid   = 1;
        state->tetra_call_active         = 1;
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-STATUS (PDU type 7)  — SDS pre-coded status
 *                                ETSI EN 300 392-2 §14.7.3.7
 *
 * Fixed layout:
 *   Bits 0-4  : pdu_type = 7           (consumed by dispatcher)
 *   Bits 5-20 : pre-coded status        (16 bits)
 * Optional IEs start at bit 21.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_status(const uint8_t *bits, int nbits,
                                  int cc, dsd_state *state)
{
    if (nbits < 21) {
        fprintf(stderr, "[TETRA CMCE D-STATUS] CC=%d (too short: %d bits)\n",
                cc, nbits);
        return;
    }

    uint32_t pre_coded = mle_bits_to_uint(bits, 5, 16);

    /* Optional calling-party IE (presence flag @ bit 21) */
    uint32_t src_ssi = 0;
    int off = 21;
    if (off < nbits) {
        uint32_t cp_present = mle_bits_to_uint(bits, off, 1); off += 1;
        if (cp_present && off + 1 <= nbits) {
            uint32_t cp_type = mle_bits_to_uint(bits, off, 1); off += 1;
            if (cp_type == 0 && off + 24 <= nbits)
                src_ssi = mle_bits_to_uint(bits, off, 24);
        }
    }

    fprintf(stderr, "[TETRA CMCE D-STATUS] CC=%d  status=0x%04X(%u)",
            cc, pre_coded, pre_coded);
    if (src_ssi)
        fprintf(stderr, "  src_SSI=%u", src_ssi);
    fprintf(stderr, "\n");

    if (state) {
        state->tetra_sds_status = (uint16_t)(pre_coded & 0xFFFFu);
        if (src_ssi)
            state->tetra_sds_src = src_ssi;
        /* Phase 30: ring-buffer the last 4 status codes */
        state->tetra_sds_status_log[state->tetra_sds_status_log_head & 3u] =
            (uint16_t)(pre_coded & 0xFFFFu);
        state->tetra_sds_status_log_head =
            (uint8_t)((state->tetra_sds_status_log_head + 1u) & 3u);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-CONNECT-ACK (PDU type 3)  — MS acknowledges connection.
 * Sets call_active=1.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_connect_ack(const uint8_t *bits, int nbits,
                                      int cc, dsd_state *state)
{
    uint8_t call_id = 0;
    if (nbits >= 9) {
        call_id = (uint8_t)mle_bits_to_uint(bits, 5, 4);
        fprintf(stderr, "[TETRA CMCE D-CONNECT-ACK] CC=%d  call_id=%u  (connection acknowledged)\n",
                cc, call_id);
    } else {
        fprintf(stderr, "[TETRA CMCE D-CONNECT-ACK] CC=%d  (connection acknowledged)\n", cc);
    }
    if (state) {
        state->tetra_d_connect_ack_call_id = call_id;
        state->tetra_d_connect_ack_valid   = 1;
        state->tetra_call_active           = 1;
    }
}

/* -----------------------------------------------------------------------
 * CMCE floor-control event stubs (types 9, 11, 12, 13)
 *
 * D-TX-CONTINUE  (9)  — current speaker may continue
 * D-TX-INTERRUPT (11) — current speaker is interrupted
 * D-TX-WAIT      (12) — MS must wait before transmitting
 * D-TX-TIMED-OUT (13) — floor grant has expired
 *
 * These carry no state change beyond logging.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_tx_event(const uint8_t *bits, int nbits,
                                   int cc, uint32_t pdu_type,
                                   dsd_state *state)
{
    const char *name;
    switch (pdu_type) {
    case TETRA_CMCE_D_TX_CONTINUE:  name = "D-TX-CONTINUE";  break;
    case TETRA_CMCE_D_TX_INTERRUPT: name = "D-TX-INTERRUPT"; break;
    case TETRA_CMCE_D_TX_WAIT:      name = "D-TX-WAIT";      break;
    case TETRA_CMCE_D_TX_TIMED_OUT: name = "D-TX-TIMED-OUT"; break;
    default:                         name = "D-TX-?";         break;
    }

    /* ETSI EN 300 392-2 §14.7.1.23-27:
     *   Bits 0-4 : pdu_type
     *   Bit    5 : call_id (1 bit, Transaction Identifier)
     *   Bit    6 : notification indicator (1 bit)
     * D-TX-CONTINUE additionally has:
     *   Bits 7-8 : tx_demand_priority (2 bits)         */
    uint32_t call_id      = 0;
    uint32_t notification = 0;

    if (nbits >= 6)
        call_id = mle_bits_to_uint(bits, 5, 1);
    if (nbits >= 7)
        notification = mle_bits_to_uint(bits, 6, 1);

    fprintf(stderr, "[TETRA CMCE %s] CC=%d  call_id=%u  notif=%u  (%d bits)\n",
            name, cc, call_id, notification, nbits);

    if (state) {
        state->tetra_tx_event_call_id      = (uint8_t)call_id;
        state->tetra_tx_event_notification = (uint8_t)notification;

        switch (pdu_type) {
        case TETRA_CMCE_D_TX_CONTINUE:  state->tetra_tx_continue    = 1; break;
        case TETRA_CMCE_D_TX_INTERRUPT: state->tetra_tx_interrupted = 1; break;
        case TETRA_CMCE_D_TX_WAIT:      state->tetra_tx_wait        = 1; break;
        case TETRA_CMCE_D_TX_TIMED_OUT:
            state->tetra_tx_timed_out      = 1;
            state->tetra_tx_granted_valid  = 0;
            break;
        default: break;
        }
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-INFO stub (PDU type 14)  — call supplementary information.
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_info(const uint8_t *bits, int nbits, int cc,
                               dsd_state *state)
{
    /* Layout (ETSI EN 300 392-2 §14.7.1.15):
     *   Bits 0-4 : pdu_type = 14
     *   Bit    5 : call_id  (1 bit, Transaction Identifier)
     *   Bit    6 : call_timeout (1 bit)
     *   Bit    7 : notification indicator (1 bit, optional)
     *   further optional IEs follow */
    if (nbits < 6) {
        fprintf(stderr, "[TETRA CMCE D-INFO] CC=%d  (%d bits, too short)\n",
                cc, nbits);
        if (state) state->tetra_d_info_valid = 1;
        return;
    }

    uint32_t call_id = mle_bits_to_uint(bits, 5, 1);
    uint32_t call_timeout = 0;
    uint32_t notification = 0;

    if (nbits >= 7)
        call_timeout = mle_bits_to_uint(bits, 6, 1);
    if (nbits >= 8)
        notification = mle_bits_to_uint(bits, 7, 1);

    fprintf(stderr, "[TETRA CMCE D-INFO] CC=%d  call_id=%u  timeout=%u  notif=%u  (%d bits)\n",
            cc, call_id, call_timeout, notification, nbits);

    if (state) {
        state->tetra_d_info_call_id      = (uint8_t)call_id;
        state->tetra_d_info_call_timeout  = (uint8_t)call_timeout;
        state->tetra_d_info_notification  = (uint8_t)notification;
        state->tetra_d_info_valid         = 1;
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-SDS-SHORT-DATA (PDU type 21) — ETSI EN 300 392-2 §14.7.1.17
 *
 * Layout:
 *   Bits  0-4  : pdu_type = 21
 *   Bit     5  : ext_flag (0 = SSI follows)
 *   Bits  6-29 : calling_ssi (24 bits, if ext_flag == 0)
 *   Bits 30-31 : data_type_identifier (2 bits)
 *     data_type 0 : 16-bit pre-defined status (bits 32-47)
 *     data_type 1 : 32-bit user-defined data
 *     data_type 2 : 64-bit user-defined data
 *     data_type 3 : length indicator (10 bits) + variable data
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_short_data(const uint8_t *bits, int nbits,
                                         int cc, dsd_state *state)
{
    if (nbits < 32) {
        fprintf(stderr, "[TETRA CMCE D-SDS-SHORT-DATA] CC=%d (too short: %d bits)\n",
                cc, nbits);
        return;
    }

    int off = 5;
    uint32_t ext_flag = mle_bits_to_uint(bits, off, 1); off += 1;

    uint32_t src_ssi = 0;
    if (ext_flag == 0) {
        if (off + 24 > nbits) return;
        src_ssi = mle_bits_to_uint(bits, off, 24);
        off += 24;  /* off == 30 */
    } else {
        /* External subscriber number: 8-bit length (in bits) + data */
        if (off + 8 > nbits) return;
        int field_len = (int)mle_bits_to_uint(bits, off, 8); off += 8;
        /* Extract up to 24 bits of the external number as src_ssi */
        if (field_len > 0 && off + field_len <= nbits) {
            int grab = field_len > 24 ? 24 : field_len;
            src_ssi = mle_bits_to_uint(bits, off, grab);
        }
        off += field_len;
    }

    /* Data type identifier (2 bits) */
    if (off + 2 > nbits) {
        fprintf(stderr, "[TETRA CMCE D-SDS-SHORT-DATA] CC=%d src_SSI=%u (truncated)\n",
                cc, src_ssi);
        if (state && src_ssi) state->tetra_sds_src = src_ssi;
        return;
    }
    uint32_t data_type = mle_bits_to_uint(bits, off, 2); off += 2;

    uint16_t short_data = 0;
    if (data_type == 0 && off + 16 <= nbits) {
        /* 16-bit pre-defined status */
        short_data = (uint16_t)mle_bits_to_uint(bits, off, 16);
    } else if (data_type == 1 && off + 32 <= nbits) {
        /* 32-bit user-defined: take bottom 16 for status */
        short_data = (uint16_t)mle_bits_to_uint(bits, off + 16, 16);
    } else if (data_type == 2 && off + 64 <= nbits) {
        /* Phase 72: 64-bit user-defined: take bits 48-63 */
        short_data = (uint16_t)mle_bits_to_uint(bits, off + 48, 16);
    } else if (data_type == 3 && off + 10 <= nbits) {
        /* Phase 72: variable length: 10-bit length + data */
        uint32_t var_len = mle_bits_to_uint(bits, off, 10);
        int grab = (int)var_len > 16 ? 16 : (int)var_len;
        if (grab > 0 && off + 10 + grab <= nbits)
            short_data = (uint16_t)mle_bits_to_uint(bits, off + 10, grab);
    }

    fprintf(stderr, "[TETRA CMCE D-SDS-SHORT-DATA] CC=%d  src_SSI=%u  data_type=%u  data=0x%04X\n",
            cc, src_ssi, data_type, short_data);

    if (state) {
        if (src_ssi) state->tetra_sds_src = src_ssi;
        state->tetra_sds_short_data  = short_data;
        state->tetra_sds_short_src   = src_ssi;
        state->tetra_sds_short_valid = 1;
        state->tetra_cmce_sds_data_type = (uint8_t)(data_type & 0x03u);
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-SDS-DATA (PDU type 23)  — short data service message.
 * ETSI EN 300 392-2 §14.7.1.19
 *
 * Layout of CMCE PDU (relative to bit 0, including 5-bit pdu_type):
 *   Bits  0-4  : pdu_type = 23  (delivered to us as full PDU)
 *   Bit     5  : external_subscriber_number flag  (0 = SSI follows)
 *   Bits  6-29 : calling_ssi (24 bits, if flag == 0)
 *   Bits 30-33 : message_reference (4 bits)
 *   Bit    34  : store_fwd_control (1 bit)
 *   Bit    35  : validity_period_flag  → if 1, 16-bit period follows
 *   Bit    36+ : datetime_ind → if 1, 48-bit timestamp follows
 *   Then SDS-TL service PDU:
 *     bits_per_character (4 bits) — 7=7-bit ASCII, 8=8-bit ISO, 10=Unicode
 *     number_of_characters (8 bits)
 *     character data (num_chars * bits_per_char bits)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_data(const uint8_t *bits, int nbits,
                                   int cc, dsd_state *state)
{
    if (nbits < 36) {
        fprintf(stderr, "[TETRA CMCE D-SDS-DATA] CC=%d (too short: %d bits)\n",
                cc, nbits);
        if (state) {
            state->tetra_sds_text_len = 0;
            state->tetra_sds_text[0]  = '\0';
        }
        return;
    }

    int off = 5; /* skip 5-bit pdu_type */

    /* External subscriber flag */
    uint32_t ext_flag = mle_bits_to_uint(bits, off, 1); off += 1;

    uint32_t src_ssi = 0;
    if (ext_flag == 0) {
        if (off + 24 > nbits) return;
        src_ssi = mle_bits_to_uint(bits, off, 24);
        off += 24;  /* off == 30 */
    } else {
        /* External subscriber number: 8-bit length (in bits) + data */
        if (off + 8 > nbits) return;
        int field_len = (int)mle_bits_to_uint(bits, off, 8); off += 8;
        /* Extract up to 24 bits of the external number as src_ssi */
        if (field_len > 0 && off + field_len <= nbits) {
            int grab = field_len > 24 ? 24 : field_len;
            src_ssi = mle_bits_to_uint(bits, off, grab);
        }
        off += field_len;
    }

    /* SDS-TL delivery header */
    if (off + 6 > nbits) {
        fprintf(stderr, "[TETRA CMCE D-SDS-DATA] CC=%d src_SSI=%u (no SDS-TL header bits)\n",
                cc, src_ssi);
        if (state && src_ssi) state->tetra_sds_src = src_ssi;
        return;
    }
    /* message_reference (4 bits) + store_fwd (1) + validity_period_flag (1) */
    uint32_t msg_ref      = mle_bits_to_uint(bits, off, 4); off += 4;
    /* Phase 46: save message reference and CC to state immediately */
    if (state) {
        state->tetra_sds_msg_ref = (uint8_t)(msg_ref & 0x0Fu);
        state->tetra_sds_last_cc = (int8_t)cc;
    }
    off += 1; /* store_fwd_control */
    uint32_t vp_flag = mle_bits_to_uint(bits, off, 1); off += 1;
    if (vp_flag && off + 16 <= nbits) off += 16; /* validity period */

    /* datetime_ind */
    if (off + 1 > nbits) goto log_only;
    uint32_t dt_flag = mle_bits_to_uint(bits, off, 1); off += 1;
    if (dt_flag && off + 48 <= nbits) off += 48; /* datetime */

    /* SDS-TL service PDU: bits_per_char(8) + num_chars(8) + data */
    if (off + 16 > nbits) goto log_only;
    uint32_t bpc       = mle_bits_to_uint(bits, off, 8); off += 8;
    uint32_t num_chars = mle_bits_to_uint(bits, off, 8); off += 8;

    fprintf(stderr, "[TETRA CMCE D-SDS-DATA] CC=%d  src_SSI=%u  bpc=%u  num_chars=%u",
            cc, src_ssi, bpc, num_chars);

    /* Decode text for 7-bit, 8-bit, or Unicode (10/16-bit) encodings */
    char text[256];
    int  text_len = 0;
    uint8_t is_unicode = 0;
    if ((bpc == 7 || bpc == 8) && num_chars > 0 && off + (int)(num_chars * bpc) <= nbits) {
        uint32_t max_chars = num_chars < 255u ? num_chars : 255u;
        for (uint32_t ci = 0; ci < max_chars; ci++) {
            uint8_t ch = (uint8_t)mle_bits_to_uint(bits, off + (int)(ci * bpc), (int)bpc);
            text[text_len++] = (char)(ch & 0x7Fu); /* strip parity/high bit */
        }
        text[text_len] = '\0';
        fprintf(stderr, "  text=\"%s\"", text);
    } else if ((bpc == 10 || bpc == 16) && num_chars > 0 && off + (int)(num_chars * bpc) <= nbits) {
        /* Unicode (UCS-2 / UTF-16-like): decode each code unit to UTF-8.
         * Only handles BMP code points (U+0000..U+FFFF). */
        is_unicode = 1;
        uint32_t max_chars = num_chars < 127u ? num_chars : 127u;
        for (uint32_t ci = 0; ci < max_chars; ci++) {
            uint32_t cp = mle_bits_to_uint(bits, off + (int)(ci * bpc), (int)bpc);
            if (cp < 0x80u && text_len + 1 < 255) {
                text[text_len++] = (char)cp;
            } else if (cp < 0x800u && text_len + 2 < 255) {
                text[text_len++] = (char)(0xC0u | (cp >> 6));
                text[text_len++] = (char)(0x80u | (cp & 0x3Fu));
            } else if (text_len + 3 < 255) {
                text[text_len++] = (char)(0xE0u | (cp >> 12));
                text[text_len++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                text[text_len++] = (char)(0x80u | (cp & 0x3Fu));
            }
        }
        text[text_len] = '\0';
        fprintf(stderr, "  text(utf8)=\"%s\"", text);
    } else {
        text_len = 0;
        text[0]  = '\0';
    }
    fprintf(stderr, "\n");

    if (state) {
        if (src_ssi) state->tetra_sds_src = src_ssi;
        state->tetra_sds_text_len     = (uint16_t)(text_len & 0xFFFFu);
        state->tetra_sds_text_unicode = is_unicode;
        if (text_len > 0)
            memcpy(state->tetra_sds_text, text, (size_t)(text_len + 1));
        else
            state->tetra_sds_text[0] = '\0';
        
        /* Phase 78: CMCE D-SDS-DATA dropped variables */
        state->tetra_cmce_sds_bpc       = (uint8_t)(bpc & 0xFFu);
        state->tetra_cmce_sds_num_chars = (uint8_t)(num_chars & 0xFFu);
    }
    return;

log_only:
    fprintf(stderr, "[TETRA CMCE D-SDS-DATA] CC=%d  src_SSI=%u  (SDS-TL parse truncated)\n",
            cc, src_ssi);
    if (state) {
        if (src_ssi) state->tetra_sds_src = src_ssi;
        /* Phase 71: clear stale text so callers don't read old data */
        state->tetra_sds_text_len  = 0;
        state->tetra_sds_text[0]   = '\0';
    }
}

/* -----------------------------------------------------------------------
 * Phase 83: CMCE D-FACILITY (PDU type 15) — supplementary service.
 * ETSI EN 300 392-2 §14.7.1.18
 *
 * Layout:
 *   Bits 0-4  : pdu_type = 15
 *   Bits 5-8  : facility_ie (4-bit type indicator)
 *   Remaining : supplementary-service-specific IEs (variable)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_facility(const uint8_t *bits, int nbits,
                                   int cc, dsd_state *state)
{
    uint8_t fac_type = 0;
    if (nbits >= 9)
        fac_type = (uint8_t)mle_bits_to_uint(bits, 5, 4);

    fprintf(stderr, "[TETRA CMCE D-FACILITY] CC=%d  fac_type=%u  (%d bits)\n",
            cc, fac_type, nbits);
    if (state) {
        state->tetra_facility_type  = fac_type;
        state->tetra_facility_valid = 1;
    }
}

/* -----------------------------------------------------------------------
 * Phase 83: CMCE D-SDS-ACK (PDU type 17) — SDS acknowledgement.
 * ETSI EN 300 392-2 §14.7.1.16
 *
 * Layout:
 *   Bits 0-4  : pdu_type = 17
 *   Bits 5-8  : message_reference (4 bits)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_ack(const uint8_t *bits, int nbits,
                                  int cc, dsd_state *state)
{
    uint8_t msg_ref = 0;
    if (nbits >= 9)
        msg_ref = (uint8_t)mle_bits_to_uint(bits, 5, 4);

    fprintf(stderr, "[TETRA CMCE D-SDS-ACK] CC=%d  msg_ref=%u\n", cc, msg_ref);
    if (state) {
        state->tetra_sds_ack_msg_ref = msg_ref;
        state->tetra_sds_ack_valid   = 1;
    }
}

/* -----------------------------------------------------------------------
 * Phase 83: CMCE D-SDS-SHORT-REPORT (PDU type 18) — SDS short report.
 * ETSI EN 300 392-2 §14.7.1.20a
 *
 * Layout:
 *   Bits 0-4  : pdu_type = 18
 *   Bits 5-6  : report_result (2 bits: 0=success, 1=fail, 2=pending, 3=reserved)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_short_report(const uint8_t *bits, int nbits,
                                           int cc, dsd_state *state)
{
    uint8_t result = 0;
    if (nbits >= 7)
        result = (uint8_t)mle_bits_to_uint(bits, 5, 2);

    fprintf(stderr, "[TETRA CMCE D-SDS-SHORT-REPORT] CC=%d  result=%u\n",
            cc, result);
    if (state) {
        state->tetra_sds_short_report_result = result;
        state->tetra_sds_short_report_valid  = 1;
    }
}

/* -----------------------------------------------------------------------
 * Phase 82: CMCE D-SDS-LONG-DATA (PDU type 20) — long SDS data.
 * ETSI EN 300 392-2 §14.7.1.19a
 *
 * Layout (similar to D-SDS-DATA with 10-bit length indicator):
 *   Bits  0-4  : pdu_type = 20
 *   Bit     5  : external_subscriber_number flag
 *   Bits  6-29 : calling_ssi (24 bits, if flag == 0)
 *   Bits 30-33 : message_reference (4 bits)
 *   Bits 34-43 : length_indicator (10 bits, data length in bits)
 *   Then SDS-TL PDU:
 *     bits_per_character (8 bits)
 *     number_of_characters (8 bits)
 *     character data
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_long_data(const uint8_t *bits, int nbits,
                                        int cc, dsd_state *state)
{
    if (nbits < 44) {
        fprintf(stderr, "[TETRA CMCE D-SDS-LONG-DATA] CC=%d (too short: %d bits)\n",
                cc, nbits);
        if (state) {
            state->tetra_sds_long_text_len = 0;
            state->tetra_sds_long_text[0]  = '\0';
        }
        return;
    }

    int off = 5; /* skip pdu_type */

    /* External subscriber flag */
    uint32_t ext_flag = mle_bits_to_uint(bits, off, 1); off += 1;

    uint32_t src_ssi = 0;
    if (ext_flag == 0) {
        if (off + 24 > nbits) goto long_log_only;
        src_ssi = mle_bits_to_uint(bits, off, 24);
        off += 24;
    } else {
        if (off + 8 > nbits) goto long_log_only;
        int field_len = (int)mle_bits_to_uint(bits, off, 8); off += 8;
        if (field_len > 0 && off + field_len <= nbits) {
            int grab = field_len > 24 ? 24 : field_len;
            src_ssi = mle_bits_to_uint(bits, off, grab);
        }
        off += field_len;
    }

    /* message_reference (4 bits) */
    if (off + 4 > nbits) goto long_log_only;
    off += 4; /* msg_ref consumed but not saved separately */

    /* length_indicator (10 bits) */
    if (off + 10 > nbits) goto long_log_only;
    uint32_t data_len_bits = mle_bits_to_uint(bits, off, 10); off += 10;
    (void)data_len_bits;

    /* SDS-TL PDU: bpc (8 bits) + num_chars (8 bits) + data */
    if (off + 16 > nbits) goto long_log_only;
    uint32_t bpc       = mle_bits_to_uint(bits, off, 8); off += 8;
    uint32_t num_chars = mle_bits_to_uint(bits, off, 8); off += 8;

    fprintf(stderr, "[TETRA CMCE D-SDS-LONG-DATA] CC=%d  src_SSI=%u  bpc=%u  num_chars=%u",
            cc, src_ssi, bpc, num_chars);

    char text[256];
    int  text_len = 0;
    uint8_t is_unicode = 0;

    if ((bpc == 7 || bpc == 8) && num_chars > 0 && off + (int)(num_chars * bpc) <= nbits) {
        uint32_t max_chars = num_chars < 255u ? num_chars : 255u;
        for (uint32_t ci = 0; ci < max_chars; ci++) {
            uint8_t ch = (uint8_t)mle_bits_to_uint(bits, off + (int)(ci * bpc), (int)bpc);
            text[text_len++] = (char)(ch & 0x7Fu);
        }
        text[text_len] = '\0';
        fprintf(stderr, "  text=\"%s\"", text);
    } else if ((bpc == 10 || bpc == 16) && num_chars > 0 && off + (int)(num_chars * bpc) <= nbits) {
        is_unicode = 1;
        uint32_t max_chars = num_chars < 127u ? num_chars : 127u;
        for (uint32_t ci = 0; ci < max_chars; ci++) {
            uint32_t cp = mle_bits_to_uint(bits, off + (int)(ci * bpc), (int)bpc);
            if (cp < 0x80u && text_len + 1 < 255) {
                text[text_len++] = (char)cp;
            } else if (cp < 0x800u && text_len + 2 < 255) {
                text[text_len++] = (char)(0xC0u | (cp >> 6));
                text[text_len++] = (char)(0x80u | (cp & 0x3Fu));
            } else if (text_len + 3 < 255) {
                text[text_len++] = (char)(0xE0u | (cp >> 12));
                text[text_len++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                text[text_len++] = (char)(0x80u | (cp & 0x3Fu));
            }
        }
        text[text_len] = '\0';
        fprintf(stderr, "  text(utf8)=\"%s\"", text);
    } else {
        text_len = 0;
        text[0]  = '\0';
    }
    fprintf(stderr, "\n");

    if (state) {
        if (src_ssi) state->tetra_sds_src = src_ssi;
        state->tetra_sds_long_text_len     = (uint16_t)(text_len & 0xFFFFu);
        state->tetra_sds_long_text_unicode = is_unicode;
        if (text_len > 0)
            memcpy(state->tetra_sds_long_text, text, (size_t)(text_len + 1));
        else
            state->tetra_sds_long_text[0] = '\0';
        state->tetra_sds_long_valid = 1;
    }
    return;

long_log_only:
    fprintf(stderr, "[TETRA CMCE D-SDS-LONG-DATA] CC=%d  src_SSI=%u  (truncated)\n",
            cc, src_ssi);
    if (state) {
        if (src_ssi) state->tetra_sds_src = src_ssi;
        state->tetra_sds_long_text_len = 0;
        state->tetra_sds_long_text[0]  = '\0';
    }
}

/* -----------------------------------------------------------------------
 * CMCE D-SDS-REPORT (PDU type 22)  — SDS delivery report.
 * ETSI EN 300 392-2 §14.7.1.20
 *
 * Layout:
 *   Bits 0-4 : pdu_type = 22
 *   Bit    5 : delivery_status  (0=delivered, 1=not delivered)
 *   Bits 6-9  : failure_cause   (4 bits; only meaningful when status=1)
 * ----------------------------------------------------------------------- */
static void parse_cmce_d_sds_report(const uint8_t *bits, int nbits,
                                      int cc, dsd_state *state)
{
    uint8_t delivery_ok = 1;
    uint8_t cause       = 0;
    if (nbits >= 6) {
        delivery_ok = (uint8_t)(1u - (mle_bits_to_uint(bits, 5, 1) & 1u));
        if (!delivery_ok && nbits >= 10)
            cause = (uint8_t)mle_bits_to_uint(bits, 6, 4);
    }
    fprintf(stderr, "[TETRA CMCE D-SDS-REPORT] CC=%d  %s",
            cc, delivery_ok ? "delivered" : "NOT delivered");
    if (!delivery_ok)
        fprintf(stderr, "  cause=%u", cause);
    fprintf(stderr, "\n");
    if (state) {
        state->tetra_sds_report_delivery_ok = delivery_ok;
        state->tetra_sds_report_cause       = cause;
        state->tetra_sds_report_valid       = 1;
    }
}

/* -----------------------------------------------------------------------
 * CMCE PDU dispatcher  (bits = CMCE PDU starting at bit 0)
 * ----------------------------------------------------------------------- */
static void tetra_cmce_parse(const uint8_t *bits, int nbits,
                              int cc, const dsd_opts *opts, dsd_state *state)
{
    if (nbits < 5) {
        fprintf(stderr, "[TETRA CMCE] CC=%d (too short: %d bits)\n", cc, nbits);
        return;
    }

    uint32_t pdu_type = mle_bits_to_uint(bits, 0, 5);

    switch (pdu_type) {

    case TETRA_CMCE_D_ALERT:
        parse_cmce_d_alert(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_CALL_PROCEEDING:
        parse_cmce_d_call_proceeding(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SETUP:
        parse_cmce_d_setup(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_STATUS:
        parse_cmce_d_status(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_RELEASE:
        parse_cmce_d_release(bits, nbits, cc, 0, (dsd_opts *)(uintptr_t)opts, state);
        break;

    case TETRA_CMCE_D_DISCONNECT:
        parse_cmce_d_release(bits, nbits, cc, 1, (dsd_opts *)(uintptr_t)opts, state);
        break;

    case TETRA_CMCE_D_CONNECT:
        parse_cmce_d_connect(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_CONNECT_ACK:
        parse_cmce_d_connect_ack(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_TX_GRANTED:
        parse_cmce_d_tx_granted(bits, nbits, cc, (dsd_opts *)(uintptr_t)opts, state);
        break;

    case TETRA_CMCE_D_TX_CEASED:
        parse_cmce_d_tx_ceased(bits, nbits, cc, (dsd_opts *)(uintptr_t)opts, state);
        break;

    case TETRA_CMCE_D_TX_CONTINUE:  /* fall-through */
    case TETRA_CMCE_D_TX_INTERRUPT: /* fall-through */
    case TETRA_CMCE_D_TX_WAIT:      /* fall-through */
    case TETRA_CMCE_D_TX_TIMED_OUT:
        parse_cmce_d_tx_event(bits, nbits, cc, pdu_type, state);
        break;

    case TETRA_CMCE_D_INFO:
        parse_cmce_d_info(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_FACILITY:
        parse_cmce_d_facility(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_ACK:
        parse_cmce_d_sds_ack(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_SHORT_REPORT:
        parse_cmce_d_sds_short_report(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_LONG_DATA:
        parse_cmce_d_sds_long_data(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_SHORT_DATA:
        parse_cmce_d_sds_short_data(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_REPORT:
        parse_cmce_d_sds_report(bits, nbits, cc, state);
        break;

    case TETRA_CMCE_D_SDS_DATA:
        parse_cmce_d_sds_data(bits, nbits, cc, state);
        break;

    default:
        if (opts && opts->errorbars) {
            fprintf(stderr, "[TETRA CMCE] CC=%d  pdu_type=%u  (%d bits)\n",
                    cc, pdu_type, nbits);
        }
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public entry point: tetra_mle_dispatch()
 * ----------------------------------------------------------------------- */
void tetra_mle_dispatch(const uint8_t *bits, int nbits,
                        int cc, const dsd_opts *opts, dsd_state *state)
{
    if (!bits || nbits < 5) {
        /* Need at least 5-bit MLE type */
        return;
    }

    uint32_t mle_type = mle_bits_to_uint(bits, 0, 5);

    if (mle_type == TETRA_MLE_C_PLANE_DATA) {
        /* ----------------------------------------------------------------
         * C-PLANE-DATA: carries CMCE or MM PDU
         * ---------------------------------------------------------------- */
        if (nbits < 9) return; /* need 4-bit PD field as well */
        uint32_t pd = mle_bits_to_uint(bits, 5, 4);

        if (pd == TETRA_MLE_PD_CMCE) {
            /* CMCE PDU starts at bit 9 */
            tetra_cmce_parse(bits + 9, nbits - 9, cc, opts, state);
        } else if (pd == TETRA_MLE_PD_MM) {
            tetra_mm_dispatch(bits + 9, nbits - 9, cc, opts, state);
        } else if (pd == TETRA_MLE_PD_SNDCP) {
            /* ────────────────────────────────────────────────────────────
             * SNDCP — Sub-Network Dependent Convergence Protocol (PD=8)
             * ETSI EN 300 392-3 §11 / EN 300 392-2 Table 21.2
             *
             * Minimal parser: extract NSAPI (4 bits) and SNDCP PDU type
             * (4 bits) from the first octet, log, and save to state.
             * ──────────────────────────────────────────────────────────── */
            const uint8_t *sn = bits + 9;
            int sn_nbits = nbits - 9;
            if (sn_nbits >= 8) {
                uint32_t nsapi    = mle_bits_to_uint(sn, 0, 4);
                uint32_t sn_type  = mle_bits_to_uint(sn, 4, 4);
                fprintf(stderr,
                        "[TETRA SNDCP] CC=%d  NSAPI=%u  pdu_type=%u  (%d bits)\n",
                        cc, nsapi, sn_type, sn_nbits);
                if (state) {
                    state->tetra_sndcp_nsapi    = (uint8_t)nsapi;
                    state->tetra_sndcp_pdu_type = (uint8_t)sn_type;
                    state->tetra_sndcp_nbits    = (uint16_t)sn_nbits;
                    state->tetra_sndcp_valid    = 1;
                }
            } else {
                fprintf(stderr,
                        "[TETRA SNDCP] CC=%d  (%d bits, too short)\n",
                        cc, sn_nbits);
                if (state) state->tetra_sndcp_valid = 1;
            }
        } else {
            if (opts && opts->errorbars) {
                fprintf(stderr, "[TETRA MLE C-PLANE] CC=%d  PD=%u  (%d bits)\n",
                        cc, pd, nbits - 9);
            }
        }
    } else if (mle_type == TETRA_MLE_D_NWRK_BROADCAST) {
        /* Carried in MAC-BROADCAST bcast_type 3 (TETRA_MAC_BC_NWRK_BCAST). */
        parse_mle_d_nwrk_broadcast(bits, nbits, cc, state);
    } else if (mle_type == TETRA_MLE_D_NWRK_BCAST_EXT) {
        /* ----------------------------------------------------------------
         * D-NWRK-BROADCAST-EXTENSION  (MLE type 1)
         * ETSI EN 300 392-2 §21.6.2
         *
         * Bits  0-4 : MLE type = 1
         * Bits  5-18: Location Area (14 bits)
         * further optional IEs follow
         * ---------------------------------------------------------------- */
        if (nbits >= 19) {
            uint32_t la = mle_bits_to_uint(bits, 5, 14);
            fprintf(stderr, "[TETRA MLE D-NWRK-BROADCAST-EXT] CC=%d  LA=%u  (%d bits)\n",
                    cc, la, nbits);
            if (state) {
                state->tetra_nwrk_bcast_ext_la    = (uint16_t)la;
                state->tetra_nwrk_bcast_ext_known = 1;
            }
        } else {
            fprintf(stderr, "[TETRA MLE D-NWRK-BROADCAST-EXT] CC=%d  (%d bits, too short)\n",
                    cc, nbits);
            if (state) state->tetra_nwrk_bcast_ext_known = 1;
        }
    } else if (mle_type == TETRA_MLE_D_RESTORE_ACK) {
        if (nbits >= 19) {
            uint16_t la = (uint16_t)mle_bits_to_uint(bits, 5, 14);
            fprintf(stderr, "[TETRA MLE D-RESTORE-ACK] CC=%d  LA=%u  (%d bits)\n",
                    cc, la, nbits);
            if (state) {
                state->tetra_restore_ack          = 1;
                state->tetra_restore_ack_la       = la;
                state->tetra_restore_ack_la_valid = 1;
            }
        } else {
            fprintf(stderr, "[TETRA MLE D-RESTORE-ACK] CC=%d  (%d bits)\n",
                    cc, nbits);
            if (state) state->tetra_restore_ack = 1;
        }
    } else if (mle_type == TETRA_MLE_D_RESTORE_RESPONSE) {
        uint8_t result = 0;
        if (nbits >= 6) {
            result = (uint8_t)mle_bits_to_uint(bits, 5, 1);
            fprintf(stderr, "[TETRA MLE D-RESTORE-RESPONSE] CC=%d  result=%u  (%d bits)\n",
                    cc, result, nbits);
        } else {
            fprintf(stderr, "[TETRA MLE D-RESTORE-RESPONSE] CC=%d  (%d bits)\n",
                    cc, nbits);
        }
        if (state) {
            state->tetra_restore_response              = 1;
            state->tetra_restore_response_result       = result;
            state->tetra_restore_response_result_valid = (nbits >= 6) ? 1u : 0u;
        }
    } else {
        /* Unknown / future MLE types. */
        if (opts && opts->errorbars) {
            fprintf(stderr, "[TETRA MLE] CC=%d  mle_type=%u  (%d bits)\n",
                    cc, mle_type, nbits);
        }
    }
}
