// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA MLE (Mobile Link Entity) dispatch + CMCE call-control parser.
 * ETSI EN 300 392-2 §21.6 (D-MLE) and Chapter 14 (CMCE).
 *
 * The TM-SDU delivered by MAC-RESOURCE (after the address field) is an
 * MLE PDU on signaling channels.  For call-control messages the MLE PDU
 * wraps a CMCE (or MM) PDU using the C-PLANE-DATA bearer (type 24).
 *
 * Layout of a C-PLANE-DATA MLE PDU:
 *   Bits  0-4  : MLE PDU type  = 24 (11000b, TETRA_MLE_C_PLANE_DATA)
 *   Bits  5-8  : Protocol discriminator (4 bits, TETRA_MLE_PD_*)
 *   Bits  9+   : CMCE / MM PDU
 *
 * CMCE D-SETUP fixed PDU layout (bits relative to start of CMCE PDU):
 *   Bits  0-4  : PDU type  = 6 (TETRA_CMCE_D_SETUP)
 *   Bit     5  : Call identification (TI)
 *   Bit     6  : Call timeout timer toggle
 *   Bits  7-9  : Call type  (TETRA_CMCE_CALL_TYPE_*)
 *   Bit    10  : Simplex / duplex flag
 *   Bit    11  : Notification indicator
 *   Bit    12  : Com-type (0=signalling, 1=user data)
 *   Bit    13  : Slots / circuit flag
 *   Bit    14  : Calling party present (optional IE presence flag)
 *   Bit    15  : (if present) Calling party type (0=SSI, 1=USSI/SNA)
 *   Bits 16-39 : (if present, type=SSI) Calling party SSI (24 bits)
 */
#ifndef DSD_NEO_PROTOCOL_TETRA_MLE_H
#define DSD_NEO_PROTOCOL_TETRA_MLE_H

#include <stdint.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * MLE PDU type constants  (5-bit field, ETSI EN 300 392-2 Table 21.77)
 * ----------------------------------------------------------------------- */
#define TETRA_MLE_D_NWRK_BROADCAST   0   /* D-NWRK-BROADCAST (network info) */
#define TETRA_MLE_D_NWRK_BCAST_EXT   1   /* D-NWRK-BROADCAST-EXTENSION      */
#define TETRA_MLE_D_RESTORE_ACK       2   /* D-RESTORE-ACK                   */
#define TETRA_MLE_D_RESTORE_RESPONSE  3   /* D-RESTORE-RESPONSE              */
#define TETRA_MLE_C_PLANE_DATA       24   /* Carry CMCE / MM PDU (11000b)    */

/* -----------------------------------------------------------------------
 * MLE Protocol Discriminator (4-bit, inside C-PLANE-DATA PDU)
 * (ETSI EN 300 392-2 Table 21.2)
 * ----------------------------------------------------------------------- */
#define TETRA_MLE_PD_CMCE             3   /* Call Management Control Entity  */
#define TETRA_MLE_PD_MM               5   /* Mobility Management             */
#define TETRA_MLE_PD_SNDCP            8   /* Sub-Network Dependent Conv.Prot.*/

/* -----------------------------------------------------------------------
 * CMCE downlink PDU type constants (5-bit, ETSI EN 300 392-2 Table 14.31)
 * ----------------------------------------------------------------------- */
#define TETRA_CMCE_D_ALERT            0
#define TETRA_CMCE_D_CALL_PROCEEDING  1
#define TETRA_CMCE_D_CONNECT          2   /* Call connected → call_active = 1 */
#define TETRA_CMCE_D_CONNECT_ACK      3
#define TETRA_CMCE_D_DISCONNECT       4   /* Call ending   → call_active = 0 */
#define TETRA_CMCE_D_RELEASE          5   /* Call released → call_active = 0 */
#define TETRA_CMCE_D_SETUP            6   /* Incoming call setup              */
#define TETRA_CMCE_D_STATUS           7
#define TETRA_CMCE_D_TX_CEASED        8   /* PTT released  → tx_granted_valid=0 */
#define TETRA_CMCE_D_TX_CONTINUE      9
#define TETRA_CMCE_D_TX_GRANTED      10   /* PTT grant     → tx_granted_ssi     */
#define TETRA_CMCE_D_TX_INTERRUPT    11
#define TETRA_CMCE_D_TX_WAIT         12
#define TETRA_CMCE_D_TX_TIMED_OUT    13
#define TETRA_CMCE_D_INFO            14
#define TETRA_CMCE_D_FACILITY        15   /* Supplementary svc   §14.7.1.18 */
#define TETRA_CMCE_D_SDS_ACK         17   /* SDS acknowledgement §14.7.1.16 */
#define TETRA_CMCE_D_SDS_SHORT_REPORT 18  /* SDS short report    §14.7.1.20a*/
#define TETRA_CMCE_D_SDS_LONG_DATA   20   /* Long SDS data       §14.7.1.19a*/
#define TETRA_CMCE_D_SDS_SHORT_DATA  21  /* Short data service  §14.7.1.17 */
#define TETRA_CMCE_D_SDS_REPORT      22  /* SDS delivery report §14.7.1.20 */
#define TETRA_CMCE_D_SDS_DATA        23  /* Short data message  §14.7.1.19 */

/* D-SETUP call_type field values (3-bit, ETSI §14.7.3.2 Table 14.33) */
#define TETRA_CMCE_CALL_TYPE_GROUP        0   /* Basic group call        */
#define TETRA_CMCE_CALL_TYPE_UNAACK_GRP   1   /* Unacknowledged group    */
#define TETRA_CMCE_CALL_TYPE_ACKNOWLEDGED 2   /* Acknowledged call       */
#define TETRA_CMCE_CALL_TYPE_SDS          3   /* Short data service      */
#define TETRA_CMCE_CALL_TYPE_PSTN         4   /* PSTN/PABX private call  */
#define TETRA_CMCE_CALL_TYPE_ISDN         5   /* ISDN private call        */
#define TETRA_CMCE_CALL_TYPE_SDM          6   /* Semi-duplex migration   */

/* -----------------------------------------------------------------------
 * tetra_mle_dispatch()
 *
 * Parse the MLE TM-SDU that follows the address field in a MAC-RESOURCE
 * PDU and dispatch to CMCE / MM decoders as appropriate.
 *
 * Updates @state:
 *   MLE D-NWRK-BROADCAST → tetra_nwrk_bcast_known, tetra_la,
 *                           tetra_subscr_class
 *   MLE D-NWRK-BCAST-EXT / D-RESTORE-ACK / D-RESTORE-RESPONSE
 *                        → log only
 *   CMCE D-ALERT / D-CALL-PROCEEDING
 *                    → tetra_call_active=1
 *   CMCE D-SETUP     → tetra_calling_ssi, tetra_gssi, tetra_call_id,
 *                       tetra_call_type, tetra_call_active=1,
 *                       lasttg, lastsrc, active_channel[0], last_active_time
 *   CMCE D-CONNECT   → tetra_call_active=1
 *   CMCE D-STATUS    → tetra_sds_status, tetra_sds_src
 *   CMCE D-RELEASE /
 *   CMCE D-DISCONNECT → tetra_call_active=0
 *   CMCE D-TX-GRANTED → tetra_tx_granted_ssi, tetra_tx_granted_valid=1
 *   CMCE D-TX-CEASED  → tetra_tx_granted_valid=0
 *   CMCE D-TX-CONTINUE / D-TX-INTERRUPT / D-TX-WAIT / D-TX-TIMED-OUT
 *                    → log only (no state change)
 *   CMCE D-INFO      → log only
 *
 * @bits   – one byte per bit (value 0 or 1), MSB-first, TM-SDU payload
 * @nbits  – number of valid bits starting at @bits[0]
 * @cc     – Colour Code (for diagnostic log prefix)
 * @opts   – dsd_opts (errorbars / payload verbosity flags)
 * @state  – dsd_state to update; may be NULL
 * ----------------------------------------------------------------------- */
void tetra_mle_dispatch(const uint8_t *bits, int nbits,
                        int cc, const dsd_opts *opts, dsd_state *state);

/* -----------------------------------------------------------------------
 * tetra_enc_mode_name()  (Phase 33)
 *
 * Return a human-readable string for the 2-bit encryption mode field.
 * ----------------------------------------------------------------------- */
static inline const char *
tetra_enc_mode_name(uint8_t mode)
{
    switch (mode & 0x03u) {
    case 0: return "none";
    case 1: return "on";
    case 2: return "on+auth";
    default: return "rsvd";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_PROTOCOL_TETRA_MLE_H */
