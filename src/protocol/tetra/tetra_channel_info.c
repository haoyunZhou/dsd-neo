// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA channel-info summary formatter.  Phase 32.
 */

#include <dsd-neo/protocol/tetra/tetra_channel_info.h>
#include <dsd-neo/protocol/tetra/tetra_mle.h>
#include <dsd-neo/core/state.h>

#include <stdio.h>
#include <string.h>

void
tetra_channel_info_fmt(const dsd_state *state, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;

    if (!state) {
        snprintf(buf, len, "?");
        return;
    }

    /* Build the string in pieces and append */
    char tmp[256];
    int  off = 0;

    /* Network identity */
    if (state->tetra_net_known)
        off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                        "MCC:%u MNC:%u CC:%u",
                        (unsigned)state->tetra_mcc,
                        (unsigned)state->tetra_mnc,
                        (unsigned)state->tetra_colour);
    else
        off += snprintf(tmp + off, sizeof(tmp) - (size_t)off, "MCC:? MNC:? CC:?");

    /* DL carrier */
    if (state->tetra_dl_carrier_hz > 0)
        off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                        " DL:%.3fMHz",
                        (double)state->tetra_dl_carrier_hz / 1.0e6);

    /* Talkgroup and source */
    if (state->tetra_call_active) {
        if (state->tetra_gssi)
            off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                            " TG:%u", (unsigned)state->tetra_gssi);
        if (state->tetra_calling_ssi)
            off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                            " SRC:%u", (unsigned)state->tetra_calling_ssi);
    }

    /* Encryption mode */
    off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                    " enc:%s", tetra_enc_mode_name(state->tetra_enc_mode));

    /* Phase 75: TDMA timestamp */
    if (state->tetra_tdma_valid)
        off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                        " TN:%u FN:%u MN:%u",
                        (unsigned)state->tetra_tn,
                        (unsigned)state->tetra_fn,
                        (unsigned)state->tetra_mn);

    /* Phase 75: Voice channel frequency */
    if (state->tetra_vc_freq_hz > 0)
        off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                        " VC:%.3fMHz",
                        (double)state->tetra_vc_freq_hz / 1.0e6);

    /* Phase 75: Last SDS text */
    if (state->tetra_sds_text_len > 0 && state->tetra_sds_text[0] != '\0') {
        if (state->tetra_sds_src)
            off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                            " SDS(%u):\"%s\"",
                            (unsigned)state->tetra_sds_src,
                            state->tetra_sds_text);
        else
            off += snprintf(tmp + off, sizeof(tmp) - (size_t)off,
                            " SDS:\"%s\"", state->tetra_sds_text);
    }

    snprintf(buf, len, "%s", tmp);
}
