#ifndef GHL_TRANSLATE_H
#define GHL_TRANSLATE_H
#include <stddef.h>
#include <stdint.h>

/* PS3 GHL native input report length (no report ID). */
#define GHL_PS3_REPORT_SIZE 27u
#define GHL_PS4_REPORT_ID 0x01u

/* Returns 0 for a recognized PS4 six-fret report, -1 for insufficient
   length, -2 for a mismatched report ID, -3 for null pointers.
   On failure the output buffer is NOT touched. Thread safe, no allocation.
   Mapping is based on PlasticBand documentation, not hardware validated. */
int ghl_ps4_to_ps3(const uint8_t *input, size_t input_length,
                   uint8_t output[GHL_PS3_REPORT_SIZE]);

#endif
