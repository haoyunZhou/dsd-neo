// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA MM (Mobility Management) PDU dispatcher.
 * ETSI EN 300 392-2, Chapter 16.
 *
 * MM PDUs are carried inside MLE C-PLANE-DATA frames with
 * Protocol Discriminator (PD) = 5 (TETRA_MLE_PD_MM).
 *
 * The 5-bit PDU type field at bits 0-4 of the MM PDU selects the message.
 * This module decodes the messages most relevant to scanner/monitor use:
 *
 *  D-DISABLE (type 3)                      → tetra_ms_enabled=0
 *  D-ENABLE  (type 4)                      → tetra_ms_enabled=1
 *  D-LOCATION-UPDATING-ACCEPT (type 5)     → tetra_mm_la, tetra_mm_la_valid
 *  D-LOCATION-UPDATING-COMMAND (type 6)    → log only
 *  D-LOCATION-UPDATING-REJECT  (type 7)    → log only
 *  D-SUBSCRIBER-CLASS-ASSIGN (type 10)     → tetra_subscr_class
 *  D-ATTACH-DETACH-GROUP-IDENTITY (type 14) → tetra_mm_group_ssi
 *  D-MM-STATUS (type 15)                   → tetra_mm_status_code
 *
 * All other types are logged unconditionally by name.
 */
#ifndef DSD_NEO_PROTOCOL_TETRA_MM_H
#define DSD_NEO_PROTOCOL_TETRA_MM_H

#include <stdint.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * MM PDU type constants — downlink  (5-bit, ETSI EN 300 392-2 Table 16.11)
 * ----------------------------------------------------------------------- */
#define TETRA_MM_D_OTAR                        0
#define TETRA_MM_D_AUTHENTICATION              1
#define TETRA_MM_D_CHECK_TSI                   2
#define TETRA_MM_D_DISABLE                     3
#define TETRA_MM_D_ENABLE                      4
#define TETRA_MM_D_LOCATION_UPDATING_ACCEPT    5
#define TETRA_MM_D_LOCATION_UPDATING_COMMAND   6
#define TETRA_MM_D_LOCATION_UPDATING_REJECT    7
#define TETRA_MM_D_STATUS                      8
#define TETRA_MM_D_TEMPORARY_ADDRESS           9
#define TETRA_MM_D_SUBSCRIBER_CLASS_ASSIGN    10
#define TETRA_MM_D_PARAMETER_CHANGE           11
#define TETRA_MM_D_ITSI_DETACH_ACK            12
#define TETRA_MM_D_LOCATION_UPDATING_DEMAND   13
#define TETRA_MM_D_ATTACH_DETACH_GROUP        14
#define TETRA_MM_D_MM_STATUS                  15

/* -----------------------------------------------------------------------
 * Carrier frequency computation helper.
 *
 * tetra_carrier_to_dl_hz()
 *
 * Convert the SYSINFO `main_carrier` (12-bit, 0–4095) + `freq_band` (4-bit)
 * + `freq_offset` (2-bit) into an absolute DL frequency in Hz.
 *
 * TETRA DL frequency formula (ETSI EN 300 392-2 Annex A):
 *   DL (Hz) = band_base_hz[freq_band] + main_carrier × 25000
 *             + freq_offset × 6250
 *
 * Returns 0 when the freq_band is not in the built-in table.
 * ----------------------------------------------------------------------- */
long tetra_carrier_to_dl_hz(uint32_t main_carrier,
                             uint32_t freq_band,
                             uint32_t freq_offset);

/* -----------------------------------------------------------------------
 * tetra_mm_dispatch()
 *
 * Parse a TETRA MM PDU (bit array, one byte per bit, MSB-first) and update
 * dsd_state with any extracted mobility-management information.
 *
 * @bits   – one byte per bit, value 0 or 1, MSB-first, MM PDU at bit 0
 * @nbits  – number of valid bits
 * @cc     – Colour Code (for log prefix)
 * @opts   – dsd_opts verbosity flags
 * @state  – dsd_state to update; may be NULL
 * ----------------------------------------------------------------------- */
void tetra_mm_dispatch(const uint8_t *bits, int nbits,
                       int cc, const dsd_opts *opts, dsd_state *state);

/* -----------------------------------------------------------------------
 * tetra_mm_status_name()  (Phase 34)
 *
 * Return an ETSI-labelled string for the 8-bit MM status code.
 * Codes 0-7 follow ETSI EN 300 392-2 Table 16.34; others return "?"
 * ----------------------------------------------------------------------- */
static inline const char *
tetra_mm_status_name(uint8_t code)
{
    switch (code) {
    case  0: return "normal";
    case  1: return "roaming not allowed";
    case  2: return "unknown TETRA network";
    case  3: return "unknown group";
    case  4: return "unknown individual";
    case  5: return "DGNA occupied";
    case  6: return "no resource";
    case  7: return "not supported";
    case  8: return "not subscribed";
    case  9: return "not available";
    case 10: return "auth failure";
    case 11: return "auth required";
    case 12: return "LA not allowed";
    case 13: return "network congestion";
    case 14: return "migration not allowed";
    case 15: return "service disabled";
    default: return "?";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_PROTOCOL_TETRA_MM_H */
