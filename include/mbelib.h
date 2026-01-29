// Minimal stub header for mbe-neo to allow building without the real library.
// Provides the bare typedefs and function prototypes referenced by the project.
#ifndef MBELIB_STUB_H
#define MBELIB_STUB_H

typedef struct mbe_parameters {
    int initialized;
} mbe_parms;

/* Initialize parameter structures (real implementation copies/clears history). */
void mbe_initMbeParms(mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced);

/* Version string (optional) */
const char* mbe_versionString(void);

/* Decoding helpers (return codes are typically int). */
int mbe_decodeAmbe2400Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);
int mbe_decodeAmbe2450Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);
int mbe_decodeImbe4400Parms(char* imbe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);

/* Processing functions: produce PCM output from parameter sets or frames. */
void mbe_processAmbe2400Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                              mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
void mbe_processAmbe2400Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                             mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);

void mbe_processAmbe3600x2400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                    char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
void mbe_processAmbe3600x2400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                   char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);

void mbe_processAmbe2450Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                              mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
void mbe_processAmbe2450Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                             mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);

void mbe_processImbe4400Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_d[88],
                              mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
void mbe_processImbe4400Data(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_d[88],
                             mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);

void mbe_processImbe7200x4400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[8][23],
                                    char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
void mbe_processImbe7200x4400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[8][23],
                                   char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);

/* Misc helpers */
void mbe_demodulateAmbe3600x2400Data(char ambe_fr[4][24]);
void mbe_demodulateAmbe3600x2450Data(char ambe_fr[4][24]);
void mbe_demodulateImbe7200x4400Data(char imbe_fr[8][23]);

#endif /* MBELIB_STUB_H */
/* Minimal fake mbelib.h for local testing only */
#ifndef MBELIB_H
#define MBELIB_H

/* Provide minimal declarations used by the build. */

#endif
