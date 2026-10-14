/* In-process ETSI EN 300 395-2 decoder. The C-CODE sources are not in this
 * repository; CMake compiles this file only when a local tree is configured. */
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#    define TETRA_ACELP_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#    define TETRA_ACELP_EXPORT __attribute__((visibility("default")))
#else
#    define TETRA_ACELP_EXPORT
#endif

#include "SOURCE.H"

typedef char tetra_etsi_word32_must_be_32_bits[(sizeof(Word32) == 4) ? 1 : -1];

enum { TETRA_ACELP_BITS = 137, TETRA_ACELP_SAMPLES = 240 };

static int s_decoder_ready = 0;

TETRA_ACELP_EXPORT void
tetra_acelp_builtin_reset(void)
{
    s_decoder_ready = 0;
}

TETRA_ACELP_EXPORT int
tetra_acelp_builtin_decode(const uint8_t *bits, int nbits, int16_t *pcm, int nsamples, int bfi)
{
    Word16 serial[TETRA_ACELP_BITS + 1];
    Word16 parameters[24];
    Word16 samples[TETRA_ACELP_SAMPLES];
    int i;

    if (!bits || !pcm || nbits != TETRA_ACELP_BITS || nsamples < TETRA_ACELP_SAMPLES)
        return 0;
    if (sizeof(Word16) != sizeof(int16_t))
        return 0;
    if (!s_decoder_ready) {
        Init_Decod_Tetra();
        s_decoder_ready = 1;
    }

    serial[0] = (Word16)(bfi ? 1 : 0); /* ETSI bad-frame indicator */
    for (i = 0; i < TETRA_ACELP_BITS; i++) {
        if (bits[i] > 1)
            return 0;
        serial[i + 1] = (Word16)bits[i];
    }
    Bits2prm_Tetra(serial, parameters);
    Decod_Tetra(parameters, samples);
    Post_Process(samples, (Word16)TETRA_ACELP_SAMPLES);
    memcpy(pcm, samples, sizeof(samples));
    return TETRA_ACELP_SAMPLES;
}
