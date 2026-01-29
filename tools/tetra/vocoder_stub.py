#!/usr/bin/env python3
"""
Simple TETRA vocoder stub for testing TETRA_VOCODER_CMD integration.
Reads one-byte-per-bit input from stdin and writes 16-bit little-endian PCM silence
for each frame. This is only a functional stub for integration testing.
"""
import sys
import struct

SAMPLE_RATE = 8000
FRAME_SAMPLES = 160  # 20ms at 8kHz

def main():
    # Read all input bits (one byte per bit expected)
    data = sys.stdin.buffer.read()
    if not data:
        # no input: write a short silence WAV to stdout
        out = b"\x00" * (FRAME_SAMPLES * 2)
        sys.stdout.buffer.write(out)
        return

    # For each incoming bit byte, produce one 20ms PCM frame of silence
    # (This simulates a vocoder producing audio frames per codec frame.)
    outbuf = bytearray()
    for _b in data:
        outbuf.extend(b"\x00" * (FRAME_SAMPLES * 2))

    sys.stdout.buffer.write(outbuf)

if __name__ == '__main__':
    main()
