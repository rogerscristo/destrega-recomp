/* Self-checking MDEC tests. Build:
 *   clang -std=c11 -O2 -Wall -Wextra -Iruntime runtime/mdec.c runtime/tests/test_mdec.c -o build/tests/test_mdec -lm
 */
#include "mdec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQ(a, b) do { u32 _a = (u32)(a), _b = (u32)(b); \
    CHECK(_a == _b, "%s = 0x%08x, expected 0x%08x", #a, _a, _b); } while (0)

#define ST_OUT_EMPTY (1u << 31)
#define ST_BUSY      (1u << 29)
#define ST_IN_REQ    (1u << 28)
#define ST_OUT_REQ   (1u << 27)

/* depth: 0=4bit 1=8bit 2=24bit 3=15bit */
static u32 cmd_decode(int depth, int is_signed, int bit15, u32 nwords)
{
    return (1u << 29) | ((u32)depth << 27) | ((u32)is_signed << 26) | ((u32)bit15 << 25) | (nwords & 0xFFFF);
}

static void set_flat_quant(u8 qy0, u8 qc0)
{
    mdec_write_cmd((2u << 29) | 1);
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 16; i++) {
            u32 q = 1;
            u32 w = (q << 8) | (q << 16) | (q << 24) | (i == 0 ? (t ? qc0 : qy0) : q);
            mdec_write_cmd(w);
        }
}

/* Halfword stream builder */
static u16 g_hw[4096];
static int g_nhw;
static void hw(u16 v) { g_hw[g_nhw++] = v; }
static void dc_only_block(int dc) { hw((u16)((1u << 10) | ((u32)dc & 0x3FF))); hw(0xFE00); }
static void send_decode(int depth, int is_signed, int bit15)
{
    if (g_nhw & 1) hw(0xFE00);
    mdec_write_cmd(cmd_decode(depth, is_signed, bit15, (u32)g_nhw / 2));
    for (int i = 0; i < g_nhw; i += 2)
        mdec_write_cmd((u32)g_hw[i] | ((u32)g_hw[i + 1] << 16));
    g_nhw = 0;
}

static u32 g_out[4096];
static int drain(void)
{
    int n = 0;
    while (!(mdec_read_status() & ST_OUT_EMPTY) && n < 4096) g_out[n++] = mdec_read_data();
    return n;
}
static u8 out_byte(int i) { return (u8)(g_out[i >> 2] >> ((i & 3) * 8)); }

static void test_idle_status(void)
{
    mdec_init();
    mdec_write_ctrl(0x80000000u);
    u32 st = mdec_read_status();
    EQ(st & 0xFFFF, 0xFFFF);
    CHECK(st & ST_OUT_EMPTY, "out empty after reset");
    CHECK(!(st & ST_BUSY), "not busy after reset");
    EQ((st >> 16) & 7, 4);
    CHECK(!(st & ST_IN_REQ), "no in-req when disabled");
    mdec_write_ctrl(0x60000000u);
    st = mdec_read_status();
    CHECK(st & ST_IN_REQ, "in-req when enabled");
    CHECK(!(st & ST_OUT_REQ), "no out-req without data");
}

static void test_busy_and_tables(void)
{
    mdec_init();
    mdec_write_ctrl(0x60000000u);
    /* scale table command: 32 params */
    mdec_write_cmd(3u << 29);
    u32 st = mdec_read_status();
    CHECK(st & ST_BUSY, "busy while receiving scale table");
    EQ(st & 0xFFFF, 31);
    for (int i = 0; i < 31; i++) mdec_write_cmd(0);
    EQ(mdec_read_status() & 0xFFFF, 0);
    mdec_write_cmd(0);
    st = mdec_read_status();
    CHECK(!(st & ST_BUSY), "idle after scale table");
    EQ(st & 0xFFFF, 0xFFFF);
    /* luma-only quant: 16 params */
    mdec_write_cmd(2u << 29);
    EQ(mdec_read_status() & 0xFFFF, 15);
    for (int i = 0; i < 16; i++) mdec_write_cmd(0x01010101);
    CHECK(!(mdec_read_status() & ST_BUSY), "idle after luma quant");
    /* NOP command reflects low bits, no params expected */
    mdec_write_cmd(0x00001234);
    st = mdec_read_status();
    CHECK(!(st & ST_BUSY), "nop not busy");
    EQ(st & 0xFFFF, 0x1234);
    /* status bits 26-23 reflect command bits 28-25 */
    mdec_write_cmd(cmd_decode(3, 1, 1, 2));
    st = mdec_read_status();
    EQ((st >> 23) & 0xF, 0xF);
    CHECK(st & ST_BUSY, "busy during decode params");
    mdec_write_cmd(0xFE00FE00);
    mdec_write_cmd(0xFE00FE00); /* only padding: no output */
    st = mdec_read_status();
    CHECK(!(st & ST_BUSY), "not busy after padding-only decode");
    CHECK(st & ST_OUT_EMPTY, "no output from padding");
}

/* default scale table restored with init; DC 400*q1 -> 50 after IDCT */
static void test_flat_24bpp(void)
{
    mdec_init();
    mdec_write_ctrl(0x60000000u);
    set_flat_quant(1, 1);
    dc_only_block(0); dc_only_block(0);              /* Cr Cb */
    for (int i = 0; i < 4; i++) dc_only_block(400);  /* Y1..Y4 */
    send_decode(2, 0, 0);
    u32 st = mdec_read_status();
    CHECK(!(st & ST_BUSY), "decode done");
    CHECK(st & ST_OUT_REQ, "out-req with data");
    int n = drain();
    EQ(n, 192);
    int bad = 0;
    for (int i = 0; i < 768; i++) if (out_byte(i) != 0xB2) bad++;
    EQ(bad, 0);
    CHECK(mdec_read_status() & ST_OUT_EMPTY, "empty after drain");
    CHECK(!(mdec_read_status() & ST_OUT_REQ), "no out-req after drain");
}

static void test_layout_and_color(void)
{
    mdec_init();
    set_flat_quant(1, 1);
    /* Cr positive -> red up, green down; Y blocks distinct per quadrant */
    dc_only_block(160); dc_only_block(0);
    dc_only_block(-400); dc_only_block(0); dc_only_block(200); dc_only_block(400);
    send_decode(2, 1, 0);
    int n = drain();
    EQ(n, 192);
    int yv[4] = { -50, 0, 25, 50 };
    /* Cr DC 160 -> 20 after idct; R=+28, G=-14 */
    for (int q = 0; q < 4; q++) {
        int x = (q & 1) * 8 + 3, y = (q >> 1) * 8 + 5;
        int p = (x + y * 16) * 3;
        int r = (s8)out_byte(p), g = (s8)out_byte(p + 1), b = (s8)out_byte(p + 2);
        int er = yv[q] + 28, eg = yv[q] - 14, eb = yv[q];
        if (er > 127) er = 127;
        if (er < -128) er = -128;
        CHECK(abs(r - er) <= 1 && abs(g - eg) <= 1 && abs(b - eb) <= 1,
              "quadrant %d: got %d,%d,%d expected %d,%d,%d", q, r, g, b, er, eg, eb);
    }
}

static void test_15bpp_and_chunks(void)
{
    mdec_init();
    set_flat_quant(1, 1);
    for (int m = 0; m < 2; m++) {
        dc_only_block(0); dc_only_block(0);
        for (int i = 0; i < 4; i++) dc_only_block(400);
    }
    send_decode(3, 0, 1);
    /* read in macroblock-sized chunks */
    int n = 0, bad = 0;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 0x80; i++) {
            u32 w = mdec_read_data();
            u32 px = 0xB2 >> 3;
            u32 e = (px | (px << 5) | (px << 10) | 0x8000u) * 0x10001u;
            if (w != e) bad++;
            n++;
        }
        if (c == 0) CHECK(!(mdec_read_status() & ST_OUT_EMPTY), "second MB pending");
    }
    EQ(n, 0x100);
    EQ(bad, 0);
    CHECK(mdec_read_status() & ST_OUT_EMPTY, "empty after 2 MBs");
    /* bit15 clear */
    dc_only_block(0); dc_only_block(0);
    for (int i = 0; i < 4; i++) dc_only_block(400);
    send_decode(3, 0, 0);
    n = drain();
    EQ(n, 0x80);
    EQ(g_out[0] & 0x80008000u, 0);
}

static void test_mono(void)
{
    mdec_init();
    set_flat_quant(1, 1);
    dc_only_block(400); dc_only_block(-400);
    send_decode(1, 0, 0);
    int n = drain();
    EQ(n, 32);
    EQ(g_out[0], 0xB2B2B2B2);
    EQ(g_out[16], (u32)(0x80 - 50) * 0x01010101u);
    dc_only_block(400);
    send_decode(0, 0, 0);
    n = drain();
    EQ(n, 8);
    EQ(g_out[0], 0xBBBBBBBB);
}

/* AC coefficient vs float reference IDCT (8bpp signed mono, q_scale=8, qt=1) */
static void test_ac_idct(void)
{
    mdec_init();
    set_flat_quant(1, 1);
    /* DC 80 (q_scale=8), AC seq1 (pos 1, horiz freq 1) level 100,
       AC run 1 -> seq3 (pos 16 -> row 2, col 0) level -60 */
    hw((u16)((8u << 10) | 80u));
    hw((u16)(0u << 10 | 100u));
    hw((u16)((1u << 10) | ((u32)-60 & 0x3FF)));
    hw(0xFE00);
    send_decode(1, 1, 0);
    int n = drain();
    EQ(n, 16);
    double F[64] = {0};
    F[0] = 80; F[1] = 100; F[16] = -60;
    int maxerr = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            double s = 0;
            for (int v = 0; v < 8; v++)
                for (int u = 0; u < 8; u++) {
                    double cu = u ? 1 : M_SQRT1_2, cv = v ? 1 : M_SQRT1_2;
                    s += cu * cv * F[v * 8 + u] * cos((2 * x + 1) * u * M_PI / 16) * cos((2 * y + 1) * v * M_PI / 16);
                }
            s /= 4;
            int got = (s8)out_byte(y * 8 + x);
            int e = abs(got - (int)lround(s));
            if (e > maxerr) maxerr = e;
        }
    CHECK(maxerr <= 1, "AC idct max error %d", maxerr);
}

int main(void)
{
    test_idle_status();
    test_busy_and_tables();
    test_flat_24bpp();
    test_layout_and_color();
    test_15bpp_and_chunks();
    test_mono();
    test_ac_idct();
    printf("mdec: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
