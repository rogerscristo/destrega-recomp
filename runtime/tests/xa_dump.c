/* Decode one XA channel from a raw 2352-byte-sector stream into a WAV and
 * print sanity statistics (RMS, clipping ratio, lag-1 autocorrelation).
 *
 * Build: clang -std=c11 -O2 -Wall -Wextra -Iruntime runtime/xa.c \
 *          runtime/tests/xa_dump.c -o build/xa_dump
 * Usage: build/xa_dump disc/XADATA.BNS.raw <channel> <max_sectors> build/xa_ch0.wav
 */
#include "psx.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void put32(FILE *f, u32 v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, u32 v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s in.raw channel max_sectors out.wav\n", argv[0]); return 2; }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    int chan = atoi(argv[2]);
    int max_sec = atoi(argv[3]);
    FILE *out = fopen(argv[4], "wb");
    if (!out) { perror(argv[4]); return 1; }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, out);
    put32(out, 16); put16(out, 1); put16(out, 2); put32(out, 44100);
    put32(out, 44100 * 4); put16(out, 4); put16(out, 16);
    fwrite("data\0\0\0\0", 1, 8, out);

    static u8 sec[2352];
    static s16 pcm[2 * 10000];
    XaDecoder d;
    xa_reset(&d);
    long frames = 0, clipped = 0, used = 0, scanned = 0;
    double sum2 = 0, ac_num = 0, ac_den = 0;
    s16 prev_l = 0;
    int last_coding = -1;
    while (used < max_sec && fread(sec, 1, 2352, in) == 2352) {
        scanned++;
        const u8 *sh = sec + 16;
        if (sh[1] != chan || !(sh[2] & 0x04)) continue;
        if (sh[2] & 0x80) { /* EOF bit: treat as end of stream */ }
        last_coding = sh[3];
        int n = xa_decode_sector(&d, sec, pcm, 10000);
        for (int i = 0; i < n; i++) {
            s16 l = pcm[i * 2], r = pcm[i * 2 + 1];
            sum2 += (double)l * l + (double)r * r;
            if (l == 32767 || l == -32768 || r == 32767 || r == -32768) clipped++;
            ac_num += (double)l * prev_l;
            ac_den += (double)l * l;
            prev_l = l;
            put16(out, (u16)l);
            put16(out, (u16)r);
        }
        frames += n;
        used++;
    }
    long bytes = frames * 4;
    fseek(out, 4, SEEK_SET); put32(out, (u32)(36 + bytes));
    fseek(out, 40, SEEK_SET); put32(out, (u32)bytes);
    fclose(out);
    fclose(in);
    double rms = sqrt(sum2 / (2.0 * (frames ? frames : 1)));
    double ac = ac_den > 0 ? ac_num / ac_den : 0;
    printf("channel %d: %ld sectors (scanned %ld), coding=0x%02x, %ld frames (%.2fs)\n",
           chan, used, scanned, last_coding, frames, frames / 44100.0);
    printf("rms=%.1f clipped=%.4f%% lag1_autocorr=%.4f\n", rms, 100.0 * clipped / (frames ? frames : 1), ac);
    int ok = frames > 0 && rms > 200 && rms < 20000 && clipped < frames / 100 && ac > 0.5;
    printf("%s\n", ok ? "XA sanity OK" : "XA sanity FAILED");
    return ok ? 0 : 1;
}
