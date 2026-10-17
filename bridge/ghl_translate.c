/* Pure PS4(1430:07BB) to PS3(12BA:074B) GHL report conversion.
 * Reference (not hardware-verified here):
 *   TheNathannator/PlasticBand/Docs/Instruments/6-Fret Guitar/{PS4.md,PS3 and Wii U.md}
 *   TheNathannator/PlasticBand/Docs/Base Reports/{PS4.md,PS3.md}
 *   ghlre/GHLtarUtility/PS3Guitar.cs
 * This is an OFFLINE component; it does not claim a USB device or patch a game.
 */
#include "ghl_translate.h"
#include <string.h>

int ghl_ps4_to_ps3(const uint8_t *in, size_t length, uint8_t out[GHL_PS3_REPORT_SIZE])
{
    uint8_t tmp[GHL_PS3_REPORT_SIZE];
    unsigned tilt10;
    unsigned hat;
    if (!in || !out) return -3;
    if (length < 8u) return -1;
    if (in[0] != GHL_PS4_REPORT_ID) return -2;
    memset(tmp, 0, sizeof(tmp));

    /* PS4 PS-style report:
       byte5 bits 4..7: white1, black1, black2, black3.
       byte6 bits 0..1: white2, white3. */
    tmp[0] = (uint8_t)(((in[5] & 0x10u) >> 4) |  /* white1 -> square */
                       ((in[5] & 0x20u) >> 4) |  /* black1 -> cross */
                       ((in[5] & 0x40u) >> 4) |  /* black2 -> circle */
                       ((in[5] & 0x80u) >> 4) |  /* black3 -> triangle */
                       ((in[6] & 0x01u) << 4) |  /* white2 -> L1 */
                       ((in[6] & 0x02u) << 4));  /* white3 -> R1 */

    /* PS4: byte6 bits 5..7 options/L3/R3; byte7 bit0 PS button.
       PS3: byte1 bits 1/2/0 = start/L3/select; bit4 PS button. */
    tmp[1] = (uint8_t)(((in[6] & 0x20u) >> 4) | /* pause -> start */
                       ((in[6] & 0x40u) >> 4) | /* GHTV -> L3 */
                       ((in[6] & 0x80u) >> 7) | /* Hero -> select */
                       ((in[7] & 0x01u) << 4)); /* PS -> PS */

    /* Hat is a four-bit ordinal, not direction-bitmask.
       Neutral PS4=8; neutral PS3 GHL=15. */
    hat=(unsigned)(in[5] & 0x0fu);
    tmp[2] = (uint8_t)(hat <= 7u ? hat : 0x0fu);

    /* Stick neutral values and axes. */
    tmp[3]=0x80;            /* left X neutral */
    tmp[4]=in[2];           /* left Y = strum (0,0x80,0xFF) */
    tmp[5]=0x80;            /* right X neutral */
    tmp[6]=in[4];           /* right Y = whammy (0x80..0xFF) */

    /* PS3 uses a 10-bit accelerometer X, nominally 0x0200 center.
       PS4 exposes tilt as 8-bit at byte3 (0x80 center).
       Scale 0..255 to 0..1020. TILT IS A PROVISIONAL MAPPING until
       EBOOT pad handler and real 1430:07BB reports are validated. */
    tilt10=(unsigned)in[3]*4u;
    tmp[19]=(uint8_t)(tilt10 & 0xffu);
    tmp[20]=(uint8_t)(tilt10 >> 8);

    memcpy(out,tmp,sizeof(tmp));
    return 0;
}
