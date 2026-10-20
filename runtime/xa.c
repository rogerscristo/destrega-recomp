/* CD-ROM XA-ADPCM decoder (psx-spx "CDROM XA Audio ADPCM Compression").
 *
 * Decodes one raw 2352-byte Form-2 audio sector: 18 sound groups of 128
 * bytes, each with 8 (4-bit) or 4 (8-bit) sound units of 28 samples.
 * Resampling 37800Hz -> 44100Hz uses the hardware's 7-phase zigzag
 * interpolation tables (every 6 input samples produce 7 output samples).
 * 18900Hz streams are first doubled to 37800Hz by sample repetition and then
 * go through the same zigzag filter (the two-step behaviour psx-spx
 * describes as matching hardware).
 *
 * XaDecoder state usage: prev[ch][0..1] = ADPCM old/older, resample_hist[ch]
 * = 32-entry ring buffer of 37800Hz samples, resample_pos = ring write
 * position, resample_frac = "sixstep" countdown (1..6) stored as a double.
 */
#include "psx.h"
#include <string.h>

static const s32 zigzag[7][29] = {
    { 0, 0, 0, 0, 0, -2, 10, -34, 65, -84, 52, 9, -266, 1024, -2680, 9036, 26516, -6016, 3021, -1571, 848, -365, 107, 10, -16, 17, -8, 3, -1 },
    { 0, 0, 0, -2, 0, 3, -19, 60, -75, 162, -227, 306, -67, -615, 3229, 29883, -4532, 2488, -1471, 882, -424, 166, -27, 5, 6, -8, 3, -1, 0 },
    { 0, 0, -1, 3, -2, -5, 31, -74, 179, -402, 689, -926, 1272, -1446, 31033, -1446, 1272, -926, 689, -402, 179, -74, 31, -5, -2, 3, -1, 0, 0 },
    { 0, -1, 3, -8, 6, 5, -27, 166, -424, 882, -1471, 2488, -4532, 29883, 3229, -615, -67, 306, -227, 162, -75, 60, -19, 3, 0, -2, 0, 0, 0 },
    { -1, 3, -8, 17, -16, 10, 107, -365, 848, -1571, 3021, -6016, 26516, 9036, -2680, 1024, -266, 9, 52, -84, 65, -34, 10, -1, 0, 1, 0, 0, 0 },
    { 2, -8, 16, -35, 43, 26, -235, 635, -1352, 2810, -5882, 21472, 15367, -4681, 2062, -839, 347, -68, -23, 70, -35, 17, -5, 0, 0, 0, 0, 0, 0 },
    { -5, 17, -35, 70, -23, -68, 347, -839, 2062, -4681, 15367, 21472, -5882, 2810, -1352, 635, -235, 26, 43, -35, 16, -8, 2, 0, 0, 0, 0, 0, 0 },
};

static const s32 xa_pos[4] = {0, 60, 115, 98};
static const s32 xa_neg[4] = {0, 0, -52, -55};

static inline s32 clamp16(s32 v) {
    return v < -0x8000 ? -0x8000 : (v > 0x7fff ? 0x7fff : v);
}

void xa_reset(XaDecoder *d) {
    memset(d, 0, sizeof *d);
    d->resample_frac = 6.0;
}

/* Decode the 28 samples of one sound unit. */
static void decode_unit(const u8 *grp, int unit, bool eight, s32 *hist,
                        s16 *dst) {
    u8 hdr = grp[4 + unit];
    int shift = hdr & 0xf;
    if (shift > 12) shift = 9;
    int filt = (hdr >> 4) & 3;
    s32 f0 = xa_pos[filt], f1 = xa_neg[filt];
    s32 old = hist[0], older = hist[1];
    for (int j = 0; j < 28; j++) {
        s32 t;
        if (eight) {
            u8 b = grp[16 + j * 4 + unit];
            t = (s32)(s16)(u16)(b << 8) >> shift;
        } else {
            u8 b = grp[16 + j * 4 + (unit >> 1)];
            int nib = (b >> ((unit & 1) * 4)) & 0xf;
            t = (s32)(s16)(u16)(nib << 12) >> shift;
        }
        s32 s = clamp16(t + ((old * f0 + older * f1 + 32) >> 6));
        dst[j] = (s16)s;
        older = old;
        old = s;
    }
    hist[0] = old;
    hist[1] = older;
}

static s16 zz_interp(const s32 *ring, u32 p, const s32 *tab) {
    s32 sum = 0;
    for (int i = 1; i <= 29; i++) sum += ring[(p - (u32)i) & 31] * tab[i - 1];
    return (s16)clamp16(sum >> 15);
}

int xa_decode_sector(XaDecoder *d, const u8 *sector, s16 *out, int max_frames) {
    const u8 *sub = sector + 16;
    u8 ci = sub[3];
    bool stereo = (ci & 3) == 1;
    bool half = ((ci >> 2) & 3) == 1;
    bool eight = ((ci >> 4) & 3) == 1;
    const u8 *src = sector + 24;

    s16 ch[2][18 * 8 * 28];
    int n = 0; /* samples per channel */
    int units = eight ? 4 : 8;
    for (int g = 0; g < 18; g++) {
        const u8 *grp = src + g * 128;
        for (int u = 0; u < units; u++) {
            if (stereo) {
                int c = u & 1;
                decode_unit(grp, u, eight, d->prev[c], &ch[c][n + (u >> 1) * 28]);
            } else {
                decode_unit(grp, u, eight, d->prev[0], &ch[0][n + u * 28]);
            }
        }
        n += stereo ? (units / 2) * 28 : units * 28;
    }

    int sixstep = (int)d->resample_frac;
    if (sixstep < 1 || sixstep > 6) sixstep = 6;
    u32 p = d->resample_pos;
    int written = 0;
    int reps = half ? 2 : 1;
    for (int i = 0; i < n; i++) {
        for (int r = 0; r < reps; r++) {
            d->resample_hist[0][p & 31] = ch[0][i];
            d->resample_hist[1][p & 31] = stereo ? ch[1][i] : ch[0][i];
            p++;
            if (--sixstep == 0) {
                sixstep = 6;
                for (int t = 0; t < 7; t++) {
                    if (written < max_frames) {
                        out[written * 2] = zz_interp(d->resample_hist[0], p, zigzag[t]);
                        out[written * 2 + 1] = stereo
                            ? zz_interp(d->resample_hist[1], p, zigzag[t])
                            : out[written * 2];
                        written++;
                    }
                }
            }
        }
    }
    d->resample_pos = p & 31;
    d->resample_frac = sixstep;
    return written;
}
