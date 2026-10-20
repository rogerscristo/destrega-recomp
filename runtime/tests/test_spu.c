/* Self-checking tests for runtime/spu.c and runtime/xa.c.
 * Build: clang -std=c11 -O2 -Wall -Wextra -Iruntime runtime/spu.c runtime/xa.c \
 *          runtime/tests/test_spu.c -o build/test_spu && build/test_spu
 */
#include "psx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_irq_count;
static int g_last_irq = -1;
void irq_raise(int bit) { g_irq_count++; g_last_irq = bit; }

static int failures;
#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

#define V(n, r) ((u32)(n) * 16 + (r))

static void dma_upload(u32 byte_addr, const void *data, u32 bytes) {
    spu_write16(0x1a6, (u16)(byte_addr >> 3));
    spu_dma_write((const u16 *)data, bytes / 2);
}

/* Fill an ADPCM block: shift/filter header, flags, 28 nibbles from pattern. */
static void make_block(u8 *b, u8 shift_filter, u8 flags, const u8 *nibbles) {
    b[0] = shift_filter;
    b[1] = flags;
    for (int i = 0; i < 14; i++) b[2 + i] = (u8)(nibbles[i * 2] | (nibbles[i * 2 + 1] << 4));
}

static double rms(const s16 *buf, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)buf[i] * buf[i];
    return sqrt(s / (n ? n : 1));
}

static void basic_setup(void) {
    spu_init();
    spu_write16(0x1ac, 0x0004);
    spu_write16(0x180, 0x3fff);
    spu_write16(0x182, 0x3fff);
    spu_write16(0x1aa, 0xc000); /* enable, unmute */
}

/* Sine-ish looped sample at 0x1000: block0 loop-start, block1 end+repeat. */
static void upload_loop_sample(void) {
    static const u8 pat[28] = {7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9};
    u8 blk[32];
    make_block(blk, 0x00, 0x04, pat);
    make_block(blk + 16, 0x00, 0x03, pat);
    dma_upload(0x1000, blk, 32);
}

static void setup_voice0(u16 adsr1, u16 adsr2) {
    spu_write16(V(0, 0x0), 0x3fff);
    spu_write16(V(0, 0x2), 0x3fff);
    spu_write16(V(0, 0x4), 0x1000);
    spu_write16(V(0, 0x6), 0x1000 >> 3);
    spu_write16(V(0, 0x8), adsr1);
    spu_write16(V(0, 0xa), adsr2);
}

static void test_dma(void) {
    spu_init();
    u16 data[256], back[256];
    for (int i = 0; i < 256; i++) data[i] = (u16)(i * 0x1234 + 7);
    spu_write16(0x1aa, 0x0020); /* DMA write mode */
    CHECK((spu_read16(0x1ae) & 0x3f) == 0x20, "STAT mode mirror %04x", spu_read16(0x1ae));
    CHECK(spu_read16(0x1ae) & 0x100, "STAT DMA write request");
    dma_upload(0x2000, data, sizeof data);
    spu_write16(0x1aa, 0x0030);
    CHECK(spu_read16(0x1ae) & 0x200, "STAT DMA read request");
    spu_write16(0x1a6, 0x2000 >> 3);
    CHECK(spu_read16(0x1a6) == 0x400, "TSA readback");
    spu_dma_read(back, 256);
    CHECK(memcmp(data, back, sizeof data) == 0, "DMA readback mismatch");

    /* Manual FIFO writes go to the transfer address. */
    spu_write16(0x1a6, 0x3000 >> 3);
    spu_write16(0x1a8, 0xbeef);
    spu_write16(0x1a8, 0xcafe);
    spu_write16(0x1a6, 0x3000 >> 3);
    spu_dma_read(back, 2);
    CHECK(back[0] == 0xbeef && back[1] == 0xcafe, "FIFO write %04x %04x", back[0], back[1]);

    /* Transfer wraps at 512KB. */
    spu_write16(0x1a6, 0xffff);
    u16 w[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    spu_dma_write(w, 8);
    spu_write16(0x1a6, 0);
    spu_dma_read(back, 4);
    CHECK(back[0] == 5 && back[3] == 8, "wrap %d %d", back[0], back[3]);
}

static void test_regs(void) {
    spu_init();
    spu_write16(0x1a0, 0x1111);
    spu_write16(0x1bc, 0x2222);
    spu_write16(0x1c4, 0x6d80);
    spu_write16(0x1fe, 0x8000);
    spu_write16(0x260, 0x3333);
    spu_write16(0x184, 0x4000);
    spu_write16(0x1b0, 0x7fff);
    spu_write16(V(5, 0x4), 0x0800);
    spu_write16(V(5, 0x6), 0x0200);
    spu_write16(V(5, 0x8), 0x80ff);
    spu_write16(V(5, 0xa), 0x1fc0);
    CHECK(spu_read16(0x1a0) == 0x1111, "0x1a0");
    CHECK(spu_read16(0x1bc) == 0x2222, "0x1bc");
    CHECK(spu_read16(0x1c4) == 0x6d80, "0x1c4");
    CHECK(spu_read16(0x1fe) == 0x8000, "0x1fe");
    CHECK(spu_read16(0x260) == 0x3333, "0x260");
    CHECK(spu_read16(0x184) == 0x4000, "0x184");
    CHECK(spu_read16(0x1b0) == 0x7fff, "0x1b0");
    CHECK(spu_read16(V(5, 0x4)) == 0x0800, "pitch");
    CHECK(spu_read16(V(5, 0x6)) == 0x0200, "start");
    CHECK(spu_read16(V(5, 0x8)) == 0x80ff, "adsr1");
    CHECK(spu_read16(V(5, 0xa)) == 0x1fc0, "adsr2");
    spu_write16(0x1aa, 0xc0bf);
    CHECK(spu_read16(0x1aa) == 0xc0bf, "SPUCNT readback");
    CHECK((spu_read16(0x1ae) & 0x3f) == 0x3f, "STAT low bits");
    CHECK(!(spu_read16(0x1ae) & 0x400), "STAT busy must be 0");
    spu_write16(0x180, 0x2000);
    CHECK(spu_read16(0x1b8) == 0x4000, "current main volume %04x", spu_read16(0x1b8));
    spu_write16(0x188, 0x00ff);
    CHECK(spu_read16(0x188) == 0x00ff, "KON readback");
}

static void test_voice_play(void) {
    basic_setup();
    upload_loop_sample();
    /* Attack fastest linear, decay fastest, sustain level max, sustain hold,
     * release linear shift 0x10. */
    setup_voice0(0x000f, (0x7f << 6) | 0x10);
    CHECK(spu_read16(0x19c) == 0, "ENDX initially 0");
    spu_write16(0x188, 0x0001);
    static s16 out[4096 * 2];
    u16 env_max = 0;
    for (int i = 0; i < 16; i++) {
        spu_render(out, 1);
        if (spu_read16(V(0, 0xc)) > env_max) env_max = spu_read16(V(0, 0xc));
    }
    /* attack (rate 0: +0x3800/tick) hits 7fff, then one fast exponential
     * decay step (-0x4000*lvl) lands at 3fff before sustain level 7fff is
     * "reached" (decreasing target check happens after the step). */
    CHECK(env_max == 0x7fff, "ADSR attack should reach 7fff, max %04x", env_max);
    spu_render(out, 4096);
    double r = rms(out, 4096 * 2);
    printf("  voice0 loop rms=%.1f envx=%04x endx=%04x\n", r, spu_read16(V(0, 0xc)), spu_read16(0x19c));
    CHECK(r > 1000, "voice output too quiet: %.1f", r);
    CHECK(spu_read16(V(0, 0xc)) == 0x3fff, "sustain level after fast decay %04x", spu_read16(V(0, 0xc)));
    CHECK(spu_read16(0x19c) & 1, "ENDX bit0 set after end flag");
    CHECK(spu_read16(V(0, 0xe)) == (0x1000 >> 3), "repeat addr from loop-start flag: %04x", spu_read16(V(0, 0xe)));

    /* KON clears ENDX */
    spu_write16(0x188, 0x0001);
    CHECK(!(spu_read16(0x19c) & 1), "KON clears ENDX");
    spu_render(out, 1024);

    /* KOFF -> release decays to 0 */
    spu_write16(0x18c, 0x0001);
    s16 prev = (s16)spu_read16(V(0, 0xc));
    spu_render(out, 64);
    s16 now = (s16)spu_read16(V(0, 0xc));
    CHECK(now < prev, "release should decrease envx (%d -> %d)", prev, now);
    for (int i = 0; i < 64 && spu_read16(V(0, 0xc)); i++) spu_render(out, 1024);
    CHECK(spu_read16(V(0, 0xc)) == 0, "release should reach 0");
    spu_render(out, 256);
    CHECK(rms(out, 512) == 0, "silent after release");
}

static void test_adsr_timing(void) {
    /* Linear attack with shift 11 step 0 (+7 per tick): 0x7fff/7 ~= 4682 ticks. */
    basic_setup();
    upload_loop_sample();
    setup_voice0((u16)(((11 << 2) | 0) << 8) | 0x000f, (0x7f << 6));
    spu_write16(0x188, 1);
    s16 out[2];
    int ticks = 0;
    while (spu_read16(V(0, 0xc)) != 0x7fff && ticks < 100000) { spu_render(out, 1); ticks++; }
    printf("  linear attack (rate 0x2c) ticks=%d\n", ticks);
    CHECK(ticks >= 4680 && ticks <= 4684, "attack ticks %d", ticks);

    /* Exponential decrease (sustain) to near zero. */
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (u16)(0xc000 | (0x20 << 6)));
    spu_write16(0x188, 1);
    s16 buf[2 * 2048];
    spu_render(buf, 2048);
    u16 e = spu_read16(V(0, 0xc));
    CHECK(e < 0x7fff && e > 0, "exp sustain decrease envx=%04x", e);
}

static void test_oneshot(void) {
    basic_setup();
    static const u8 pat[28] = {7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9,7,7,9,9};
    u8 blk[16];
    make_block(blk, 0x00, 0x01, pat); /* end + mute */
    dma_upload(0x1000, blk, 16);
    setup_voice0(0x000f, (0x7f << 6));
    spu_write16(0x188, 1);
    s16 out[2 * 100];
    spu_render(out, 100);
    CHECK(spu_read16(0x19c) & 1, "one-shot sets ENDX");
    CHECK(spu_read16(V(0, 0xc)) == 0, "one-shot end -> envx 0");
    spu_render(out, 100);
    CHECK(rms(out, 200) == 0, "one-shot silent afterwards");
}

static void test_irq(void) {
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (0x7f << 6));
    g_irq_count = 0;
    spu_write16(0x1a4, (0x1010) >> 3);    /* second block */
    spu_write16(0x1aa, 0xc040);           /* IRQ enable */
    spu_write16(0x188, 1);
    s16 out[2 * 100];
    spu_render(out, 20);
    CHECK(g_irq_count == 0, "no IRQ before reaching block");
    spu_render(out, 40);
    CHECK(g_irq_count == 1 && g_last_irq == IRQ_SPU, "voice IRQ count=%d", g_irq_count);
    CHECK(spu_read16(0x1ae) & 0x40, "STAT IRQ flag");
    spu_write16(0x1aa, 0xc000);
    CHECK(!(spu_read16(0x1ae) & 0x40), "IRQ ack clears flag");

    /* Transfer IRQ */
    g_irq_count = 0;
    spu_write16(0x1a4, 0x4000 >> 3);
    spu_write16(0x1aa, 0xc040);
    u16 d[16] = {0};
    spu_write16(0x1a6, 0x3ff0 >> 3);
    spu_dma_write(d, 16);
    CHECK(g_irq_count == 1, "transfer IRQ count=%d", g_irq_count);
    spu_write16(0x1aa, 0xc000);
}

static void test_noise_and_pmon(void) {
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (0x7f << 6));
    spu_write16(0x1aa, 0xc000 | (0xf << 10) | (3 << 8));
    spu_write16(0x194, 1);
    spu_write16(0x188, 1);
    static s16 out[2 * 2048];
    spu_render(out, 2048);
    double r = rms(out, 4096);
    printf("  noise rms=%.1f\n", r);
    CHECK(r > 1000, "noise voice silent");

    /* Pitch modulation: voice1 modulated by voice0 still plays. */
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (0x7f << 6));
    for (int r2 = 0; r2 < 0x10; r2 += 2) spu_write16(V(1, r2), spu_read16(V(0, r2)));
    spu_write16(V(0, 0x0), 0);
    spu_write16(V(0, 0x2), 0);
    spu_write16(0x190, 0x0002);
    spu_write16(0x188, 3);
    spu_render(out, 2048);
    r = rms(out, 4096);
    CHECK(r > 500, "pmon voice silent %.1f", r);
}

static void test_reverb(void) {
    static const u16 hall[32] = {
        0x01A5,0x0139,0x6000,0x5000,0x4C00,0xB800,0xBC00,0xC000,
        0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
        0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD,
        0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A,0x8000,0x8000};
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (0x7f << 6) | 0x08);
    spu_write16(0x1a2, (u16)((0x80000 - 0xADE0) >> 3));
    for (int i = 0; i < 32; i++) spu_write16(0x1c0 + i * 2, hall[i]);
    spu_write16(0x184, 0x4000);
    spu_write16(0x186, 0x4000);
    spu_write16(0x198, 1);
    spu_write16(0x1aa, 0xc080);
    spu_write16(0x188, 1);
    static s16 out[2 * 8192];
    spu_render(out, 4096);
    spu_write16(0x18c, 1); /* release quickly */
    spu_render(out, 8192);
    /* After the dry voice is gone, reverb tail should remain audible. */
    CHECK(spu_read16(V(0, 0xc)) == 0, "voice released");
    double tail = rms(out + 2 * 6000, 2 * 2000);
    printf("  reverb tail rms=%.1f\n", tail);
    CHECK(tail > 10, "reverb tail missing (%.2f)", tail);
    /* reverb buffer was written */
    u16 rb[64];
    spu_write16(0x1a6, (u16)((0x80000 - 0xADE0) >> 3));
    int nz = 0;
    for (int k = 0; k < 0xADE0 / 128; k++) { spu_dma_read(rb, 64); for (int i = 0; i < 64; i++) nz += rb[i] != 0; }
    CHECK(nz > 100, "reverb work area untouched (%d)", nz);

    /* Same without EON: no tail. */
    basic_setup();
    upload_loop_sample();
    setup_voice0(0x000f, (0x7f << 6) | 0x08);
    spu_write16(0x1a2, (u16)((0x80000 - 0xADE0) >> 3));
    for (int i = 0; i < 32; i++) spu_write16(0x1c0 + i * 2, hall[i]);
    spu_write16(0x184, 0x4000);
    spu_write16(0x186, 0x4000);
    spu_write16(0x1aa, 0xc080);
    spu_write16(0x188, 1);
    spu_render(out, 4096);
    spu_write16(0x18c, 1);
    spu_render(out, 8192);
    CHECK(rms(out + 2 * 6000, 2 * 2000) == 0, "no-EON should have no tail");
}

static void test_cd_audio(void) {
    basic_setup();
    static s16 in[2 * 1000];
    for (int i = 0; i < 1000; i++) { in[i * 2] = 10000; in[i * 2 + 1] = -10000; }
    spu_push_cd_audio(in, 1000);
    spu_write16(0x1b0, 0x4000);
    spu_write16(0x1b2, 0x4000);
    spu_write16(0x180, 0x3fff); /* main vol 0x7ffe */
    spu_write16(0x182, 0x3fff);
    spu_write16(0x1aa, 0xc001);
    static s16 out[2 * 1200];
    spu_render(out, 1200);
    CHECK(out[0] > 4900 && out[0] < 5100 && out[1] < -4900 && out[1] > -5100,
          "cd audio mix %d %d", out[0], out[1]);
    CHECK(out[2 * 999] != 0 && out[2 * 1000] == 0 && out[2 * 1199] == 0, "cd underflow silence");
    /* disabled -> silent, but still consumed */
    spu_push_cd_audio(in, 100);
    spu_write16(0x1aa, 0xc000);
    spu_render(out, 100);
    CHECK(rms(out, 200) == 0, "cd disabled silent");
    spu_write16(0x1aa, 0xc001);
    spu_render(out, 10);
    CHECK(out[0] == 0, "cd buffer consumed while disabled");
}

/* ---- XA ---------------------------------------------------------------- */

static void make_xa_sector(u8 *s, u8 coding, u8 hdr, u8 nibble_byte) {
    memset(s, 0, 2352);
    s[16 + 2] = 0x64;
    s[16 + 3] = coding;
    s[20 + 2] = 0x64;
    s[20 + 3] = coding;
    for (int g = 0; g < 18; g++) {
        u8 *grp = s + 24 + g * 128;
        for (int i = 0; i < 16; i++) grp[i] = hdr;
        for (int i = 16; i < 128; i++) grp[i] = nibble_byte;
    }
}

static void test_xa(void) {
    static u8 sec[2352];
    static s16 out[2 * 10000];
    XaDecoder d;

    xa_reset(&d);
    make_xa_sector(sec, 0x00, 0x00, 0x00);
    int n = xa_decode_sector(&d, sec, out, 10000);
    CHECK(n == 4704, "mono 4bit 37800: %d frames", n);

    xa_reset(&d);
    make_xa_sector(sec, 0x01, 0x00, 0x00);
    n = xa_decode_sector(&d, sec, out, 10000);
    CHECK(n == 2352, "stereo 4bit 37800: %d frames", n);

    xa_reset(&d);
    make_xa_sector(sec, 0x04, 0x00, 0x00);
    n = xa_decode_sector(&d, sec, out, 10000);
    CHECK(n == 9408, "mono 4bit 18900: %d frames", n);

    xa_reset(&d);
    make_xa_sector(sec, 0x10, 0x00, 0x00);
    n = xa_decode_sector(&d, sec, out, 10000);
    CHECK(n == 2352, "mono 8bit 37800: %d frames", n);

    /* Two sectors in a row: sixstep carries over (4032 % 6 == 0 though);
     * check accumulated output for 18 sectors. */
    xa_reset(&d);
    make_xa_sector(sec, 0x00, 0x00, 0x00);
    int total = 0;
    for (int i = 0; i < 18; i++) total += xa_decode_sector(&d, sec, out, 10000);
    CHECK(total == 4704 * 18, "accumulated %d", total);

    /* DC: stereo, left nibbles = 3 (shift 0 -> 0x3000), right nibbles = 0xD (-3 -> -0x3000). */
    xa_reset(&d);
    make_xa_sector(sec, 0x01, 0x00, 0xd3);
    n = xa_decode_sector(&d, sec, out, 10000);
    int mid = n / 2;
    printf("  xa DC L=%d R=%d (expected ~ +/-0x3000*0.906)\n", out[mid * 2], out[mid * 2 + 1]);
    CHECK(out[mid * 2] > 10500 && out[mid * 2] < 12500, "XA DC left %d", out[mid * 2]);
    CHECK(out[mid * 2 + 1] < -10500 && out[mid * 2 + 1] > -12500, "XA DC right %d", out[mid * 2 + 1]);

    /* max_frames limit */
    xa_reset(&d);
    n = xa_decode_sector(&d, sec, out, 100);
    CHECK(n == 100, "max_frames clamp %d", n);

    /* Filter 1 with shift: decoded value accumulates (history retained). */
    xa_reset(&d);
    make_xa_sector(sec, 0x00, 0x18 /* filter1 shift8 */, 0x11);
    xa_decode_sector(&d, sec, out, 10000);
    CHECK(d.prev[0][0] != 0, "XA history kept");
}

int main(void) {
    test_dma();
    test_regs();
    test_voice_play();
    test_adsr_timing();
    test_oneshot();
    test_irq();
    test_noise_and_pmon();
    test_reverb();
    test_cd_audio();
    test_xa();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all SPU/XA tests passed\n");
    return 0;
}
