// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DSD_NEO_PROTOCOL_TETRA_ACELP_H
#define DSD_NEO_PROTOCOL_TETRA_ACELP_H

#include <stdint.h>

// Reorder TETRA ACELP bits for vocoder input (see osmo-tetra tch_reordering.c)
void tetra_acelp_reorder(const uint8_t* in, uint8_t* out, int len);

// Placeholder for vocoder integration
void tetra_acelp_decode(const uint8_t* bits, int len);

#endif // DSD_NEO_PROTOCOL_TETRA_ACELP_H
