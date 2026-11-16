/* fps_overlay: "FPS nn" in the top left corner of an RGB565 frame (green from 30, yellow from 20, red below).
 * Call fps_draw() once per newly shown frame. */
#ifndef FPS_OVERLAY_H
#define FPS_OVERLAY_H
#include <stdint.h>
#include <time.h>
static double fps_now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static const uint8_t font5x7[][7] = {
	{ 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 }, { 14, 17, 1, 2, 4, 8, 31 }, { 31, 2, 4, 2, 1, 17, 14 },
	{ 2, 6, 10, 18, 31, 2, 2 }, { 31, 16, 30, 1, 1, 17, 14 }, { 6, 8, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
	{ 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 2, 12 },
	{ 31, 16, 16, 30, 16, 16, 16 }, { 30, 17, 17, 30, 16, 16, 16 }, { 15, 16, 16, 14, 1, 1, 30 },
};
static void fps_draw(uint16_t *px, int w, int h)
{
	static double t0; static int n, shown;
	double t = fps_now_s();
	if (t0 == 0) t0 = t;
	n++;
	if (t - t0 >= 0.5) { shown = (int)(n / (t - t0) + 0.5); n = 0; t0 = t; }
	int glyph[6], ng = 0;
	glyph[ng++] = 10; glyph[ng++] = 11; glyph[ng++] = 12; glyph[ng++] = -1;
	int v = shown > 999 ? 999 : shown;
	if (v >= 100) glyph[ng++] = v / 100;
	if (v >= 10) glyph[ng++] = (v / 10) % 10;
	if (ng < 6) glyph[ng++] = v % 10;
	int s = h / 96; if (s < 2) s = 2;
	int bw = (ng * 6 + 1) * s, bh = 9 * s;
	if (bw > w) bw = w;
	uint16_t col = shown >= 30 ? 0x07E0 : shown >= 20 ? 0xFFE0 : 0xF800;
	for (int y = 0; y < bh && y < h; y++) for (int x = 0; x < bw; x++) px[y * w + x] = 0;
	for (int g = 0; g < ng; g++) {
		if (glyph[g] < 0) continue;
		const uint8_t *rows = font5x7[glyph[g]];
		for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) {
			if (!(rows[r] & (16 >> c))) continue;
			int x0 = (1 + g * 6 + c) * s, y0 = (1 + r) * s;
			for (int yy = 0; yy < s; yy++) for (int xx = 0; xx < s; xx++)
				if (x0 + xx < w && y0 + yy < h) px[(y0 + yy) * w + x0 + xx] = col;
		}
	}
}
#endif
