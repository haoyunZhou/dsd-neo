Vocoder stub and testing

Quick test for `TETRA_VOCODER_CMD` integration

- Make the stub executable (Linux/macOS):

```bash
chmod +x tools/tetra/vocoder_stub.py
```

- Example: run dsd-neo with the stub as the vocoder (POSIX shells):

```bash
export TETRA_VOCODER_CMD="$PWD/tools/tetra/vocoder_stub.py"
./build/<your_dsd_binary> [args]
```

- On Windows (PowerShell), set environment variable then run your dsd binary:

```powershell
$env:TETRA_VOCODER_CMD = "$PWD\tools\tetra\vocoder_stub.py"
# run your binary from the same session
```

Notes:
- The stub writes raw 16-bit little-endian PCM silence frames to stdout.
- Replace `TETRA_VOCODER_CMD` with the path to a real command-line vocoder when available.
- Real vocoder must accept raw one-byte-per-bit on stdin (adjust glue if needed).
