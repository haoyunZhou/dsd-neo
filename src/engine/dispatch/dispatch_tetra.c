// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * TETRA protocol dispatch integration
 */
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/tetra/tetra.h>

int dsd_dispatch_matches_tetra(int synctype) {
    return DSD_SYNC_IS_TETRA(synctype);
}

void dsd_dispatch_handle_tetra(dsd_opts* opts, dsd_state* state) {
    processTetraFrame(opts, state);
}
