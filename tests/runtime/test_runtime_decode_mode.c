// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/decode_mode.h>
#include <stdio.h>
#include <string.h>
#include "dsd-neo/core/opts_fwd.h"
#include "dsd-neo/core/safe_api.h"
#include "dsd-neo/core/state_fwd.h"
#include "dsd-neo/runtime/config.h"

static int
test_auto_profile_differences(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    opts.frame_dstar = 0;
    opts.frame_dmr = 0;
    opts.pulse_digi_out_channels = 7;
    state.rf_mod = 2;

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_AUTO, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive AUTO apply failed\n");
        return 1;
    }
    if (opts.frame_dstar != 0 || opts.frame_dmr != 0 || opts.pulse_digi_out_channels != 7 || state.rf_mod != 2) {
        DSD_FPRINTF(stderr, "interactive AUTO should preserve existing mode flags/channels\n");
        return 1;
    }
    if (strcmp(opts.output_name, "AUTO") != 0) {
        DSD_FPRINTF(stderr, "interactive AUTO output_name mismatch: %s\n", opts.output_name);
        return 1;
    }

    DSD_MEMSET(&opts, 0, sizeof opts);
    DSD_MEMSET(&state, 0, sizeof state);
    opts.frame_dstar = 0;
    opts.frame_dmr = 0;
    opts.frame_ysf = 0;
    opts.frame_provoice = 0;
    opts.pulse_digi_out_channels = 1;
    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_AUTO, DSD_DECODE_PRESET_PROFILE_CLI, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "cli AUTO apply failed\n");
        return 1;
    }
    if (!(opts.frame_dstar == 1 && opts.frame_x2tdma == 1 && opts.frame_p25p1 == 1 && opts.frame_p25p2 == 1
          && opts.frame_nxdn48 == 1 && opts.frame_nxdn96 == 1 && opts.frame_dmr == 1 && opts.frame_dpmr == 1
          && opts.frame_provoice == 1 && opts.frame_ysf == 1 && opts.frame_m17 == 1)) {
        DSD_FPRINTF(stderr, "cli AUTO should enable all digital frame flags\n");
        return 1;
    }
    if (opts.pulse_digi_out_channels != 2 || opts.dmr_stereo != 1 || opts.dmr_mono != 0) {
        DSD_FPRINTF(stderr, "cli AUTO audio settings mismatch channels=%d stereo=%d mono=%d\n",
                    opts.pulse_digi_out_channels, opts.dmr_stereo, opts.dmr_mono);
        return 1;
    }

    return 0;
}

static int
test_p25p2_prefers_qpsk(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_P25P2, DSD_DECODE_PRESET_PROFILE_CLI, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "cli P25P2 apply failed\n");
        return 1;
    }
    if (!(opts.frame_p25p2 == 1 && opts.frame_p25p1 == 0 && opts.frame_x2tdma == 0)) {
        DSD_FPRINTF(stderr, "cli P25P2 frame flags mismatch\n");
        return 1;
    }
    if (!(opts.mod_c4fm == 0 && opts.mod_qpsk == 1 && opts.mod_gfsk == 0 && state.rf_mod == 1)) {
        DSD_FPRINTF(stderr, "cli P25P2 should select QPSK demod (mod=%d/%d/%d rf_mod=%d)\n", opts.mod_c4fm,
                    opts.mod_qpsk, opts.mod_gfsk, state.rf_mod);
        return 1;
    }
    if (!(state.samplesPerSymbol == 8 && state.symbolCenter == 3)) {
        DSD_FPRINTF(stderr, "cli P25P2 symbol timing mismatch sps=%d center=%d\n", state.samplesPerSymbol,
                    state.symbolCenter);
        return 1;
    }
    return 0;
}

static int
test_dmr_prefers_gfsk(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_DMR, DSD_DECODE_PRESET_PROFILE_CLI, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "cli DMR apply failed\n");
        return 1;
    }
    if (!(opts.frame_dmr == 1 && opts.frame_dstar == 0 && opts.frame_p25p1 == 0 && opts.frame_p25p2 == 0)) {
        DSD_FPRINTF(stderr, "cli DMR frame flags mismatch\n");
        return 1;
    }
    if (!(opts.mod_c4fm == 0 && opts.mod_qpsk == 0 && opts.mod_gfsk == 1 && state.rf_mod == 2)) {
        DSD_FPRINTF(stderr, "cli DMR should select GFSK demod (mod=%d/%d/%d rf_mod=%d)\n", opts.mod_c4fm, opts.mod_qpsk,
                    opts.mod_gfsk, state.rf_mod);
        return 1;
    }
    return 0;
}

static int
test_dmr_preserves_manual_c4fm_lock(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    opts.mod_cli_lock = 1;
    opts.mod_c4fm = 1;
    opts.mod_qpsk = 0;
    opts.mod_gfsk = 0;
    state.rf_mod = 0;

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_DMR, DSD_DECODE_PRESET_PROFILE_CLI, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "cli DMR apply failed\n");
        return 1;
    }
    if (!(opts.frame_dmr == 1 && opts.mod_cli_lock == 1 && opts.mod_c4fm == 1 && opts.mod_qpsk == 0
          && opts.mod_gfsk == 0 && state.rf_mod == 0)) {
        DSD_FPRINTF(stderr, "cli DMR should preserve manual C4FM lock (mod=%d/%d/%d lock=%d rf_mod=%d)\n",
                    opts.mod_c4fm, opts.mod_qpsk, opts.mod_gfsk, opts.mod_cli_lock, state.rf_mod);
        return 1;
    }
    return 0;
}

static int
test_interactive_x2_and_ysf_behavior(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    opts.mod_c4fm = 0;
    opts.mod_qpsk = 1;
    opts.mod_gfsk = 1;
    state.rf_mod = 2;

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_X2TDMA, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive X2 apply failed\n");
        return 1;
    }
    if (!(opts.frame_x2tdma == 1 && opts.frame_dstar == 0 && opts.frame_dmr == 0)) {
        DSD_FPRINTF(stderr, "interactive X2 frame flags mismatch\n");
        return 1;
    }
    if (!(opts.mod_c4fm == 1 && opts.mod_qpsk == 0 && opts.mod_gfsk == 0 && state.rf_mod == 0)) {
        DSD_FPRINTF(stderr, "interactive X2 should reset demod mode to C4FM (mod=%d/%d/%d rf_mod=%d)\n", opts.mod_c4fm,
                    opts.mod_qpsk, opts.mod_gfsk, state.rf_mod);
        return 1;
    }
    if (opts.pulse_digi_out_channels != 1) {
        DSD_FPRINTF(stderr, "interactive X2 should use 1 output channel, got %d\n", opts.pulse_digi_out_channels);
        return 1;
    }

    DSD_MEMSET(&opts, 0, sizeof opts);
    DSD_MEMSET(&state, 0, sizeof state);
    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_YSF, DSD_DECODE_PRESET_PROFILE_CONFIG, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "config YSF apply failed\n");
        return 1;
    }
    if (!(opts.pulse_digi_out_channels == 2 && opts.dmr_stereo == 1 && opts.dmr_mono == 0)) {
        DSD_FPRINTF(stderr, "config YSF audio settings mismatch channels=%d stereo=%d mono=%d\n",
                    opts.pulse_digi_out_channels, opts.dmr_stereo, opts.dmr_mono);
        return 1;
    }

    DSD_MEMSET(&opts, 0, sizeof opts);
    DSD_MEMSET(&state, 0, sizeof state);
    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_YSF, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive YSF apply failed\n");
        return 1;
    }
    if (!(opts.pulse_digi_out_channels == 1 && opts.dmr_stereo == 0 && state.dmr_stereo == 0 && opts.dmr_mono == 0)) {
        DSD_FPRINTF(stderr, "interactive YSF audio settings mismatch channels=%d stereo=%d state_stereo=%d mono=%d\n",
                    opts.pulse_digi_out_channels, opts.dmr_stereo, state.dmr_stereo, opts.dmr_mono);
        return 1;
    }
    return 0;
}

static int
test_tetra_preset(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};
    dsdneoUserDecodeMode mode = DSDCFG_MODE_UNSET;

    opts.frame_dmr = 1;
    opts.frame_tetra = 0;
    opts.mod_gfsk = 1;
    state.rf_mod = 2;

    if (dsd_decode_mode_from_cli_preset('T', &mode) != 0 || mode != DSDCFG_MODE_TETRA) {
        DSD_FPRINTF(stderr, "cli TETRA preset mapping failed\n");
        return 1;
    }

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_TETRA, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive TETRA apply failed\n");
        return 1;
    }
    if (!(opts.frame_tetra == 1 && opts.frame_dmr == 0 && opts.frame_p25p1 == 0 && opts.frame_m17 == 0)) {
        DSD_FPRINTF(stderr, "interactive TETRA frame flags mismatch tetra=%d dmr=%d p25=%d m17=%d\n",
                    opts.frame_tetra, opts.frame_dmr, opts.frame_p25p1, opts.frame_m17);
        return 1;
    }
    if (!(opts.mod_qpsk == 1 && opts.mod_c4fm == 0 && opts.mod_gfsk == 0 && state.rf_mod == 1)) {
        DSD_FPRINTF(stderr, "interactive TETRA demod mismatch mod=%d/%d/%d rf_mod=%d\n",
                    opts.mod_c4fm, opts.mod_qpsk, opts.mod_gfsk, state.rf_mod);
        return 1;
    }
    if (!(state.samplesPerSymbol == 3 && state.symbolCenter == 1)) {
        DSD_FPRINTF(stderr, "interactive TETRA symbol timing mismatch sps=%d center=%d\n",
                    state.samplesPerSymbol, state.symbolCenter);
        return 1;
    }
    if (dsd_infer_decode_mode_preset(&opts) != DSDCFG_MODE_TETRA) {
        DSD_FPRINTF(stderr, "TETRA mode inference failed\n");
        return 1;
    }
    return 0;
}

static int
test_dpmr_clears_tetra_state(void) {
    static dsd_opts opts = {0};
    static dsd_state state = {0};

    DSD_MEMSET(&opts, 0, sizeof opts);
    DSD_MEMSET(&state, 0, sizeof state);

    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_TETRA, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive TETRA apply failed before dPMR transition\n");
        return 1;
    }
    if (dsd_apply_decode_mode_preset(DSDCFG_MODE_DPMR, DSD_DECODE_PRESET_PROFILE_INTERACTIVE, &opts, &state) != 0) {
        DSD_FPRINTF(stderr, "interactive dPMR apply failed after TETRA transition\n");
        return 1;
    }
    if (!(opts.frame_dpmr == 1 && opts.frame_tetra == 0 && opts.frame_dmr == 0 && opts.frame_p25p2 == 0)) {
        DSD_FPRINTF(stderr, "dPMR after TETRA frame flags mismatch dpmr=%d tetra=%d dmr=%d p25p2=%d\n",
                    opts.frame_dpmr, opts.frame_tetra, opts.frame_dmr, opts.frame_p25p2);
        return 1;
    }
    if (!(opts.mod_c4fm == 1 && opts.mod_qpsk == 0 && opts.mod_gfsk == 0 && state.rf_mod == 0)) {
        DSD_FPRINTF(stderr, "dPMR after TETRA demod mismatch mod=%d/%d/%d rf_mod=%d\n",
                    opts.mod_c4fm, opts.mod_qpsk, opts.mod_gfsk, state.rf_mod);
        return 1;
    }
    if (!(state.samplesPerSymbol == 20 && state.symbolCenter == 9)) {
        DSD_FPRINTF(stderr, "dPMR after TETRA symbol timing mismatch sps=%d center=%d\n",
                    state.samplesPerSymbol, state.symbolCenter);
        return 1;
    }
    if (dsd_infer_decode_mode_preset(&opts) != DSDCFG_MODE_DPMR) {
        DSD_FPRINTF(stderr, "dPMR mode inference failed after TETRA transition\n");
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    rc |= test_auto_profile_differences();
    rc |= test_p25p2_prefers_qpsk();
    rc |= test_dmr_prefers_gfsk();
    rc |= test_dmr_preserves_manual_c4fm_lock();
    rc |= test_interactive_x2_and_ysf_behavior();
    rc |= test_tetra_preset();
    rc |= test_dpmr_clears_tetra_state();
    return rc;
}
