// Prodest PC1 Emulator - video: CGA (compatibile Yamaha V6355 del PC1) e Hercules
#include "machine.h"
#include "font.h"

static const u32 cgaRgb[16] = {
	0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
	0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

// colori dell'interfaccia (indici 16-31)
static const u32 uiRgb[16] = {
	0x0B1A3A, 0x2A62C9, 0xF2F2F2, 0xFFD34E, 0x8A93A8, 0xB0283A, 0x000000, 0x5FD068,
	0x16295A, 0xFFFFFF, 0x3D4B6B, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
};

void Machine::palette(int monitor, u32 rgb[32])
{
	for (int i = 0; i < 16; i++) {
		u32 c = cgaRgb[i];
		if (monitor != MON_COLOR) {
			int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
			int l = (r * 30 + g * 59 + b * 11) / 100;
			if (i != 0 && l < 40) l = 40;            // anche il blu scuro resta visibile
			int tr = 255, tg = 255, tb = 255;
			if (monitor == MON_GREEN) { tr = 60; tg = 255; tb = 90; }
			else if (monitor == MON_AMBER) { tr = 255; tg = 176; tb = 0; }
			c = ((tr * l / 255) << 16) | ((tg * l / 255) << 8) | (tb * l / 255);
		}
		rgb[i] = c;
		rgb[16 + i] = uiRgb[i];
	}
}

u8 Machine::videoStatus()
{
	u64 t = (u64)frameCycle * 262;
	u32 line = (u32)(t / cyclesPerFrame);
	u32 xf = (u32)(t % cyclesPerFrame);
	bool hblank = xf > cyclesPerFrame * 3 / 4;
	if (cfg.video == VID_HERCULES) {
		u8 v = 0x70;
		if (line < 245) v |= 0x80;           // bit 7 = 0 durante il ritorno verticale
		if (hblank) v |= 0x01;
		if ((frameCycle >> 3) & 1) v |= 0x08;
		return v;
	}
	u8 v = 0xF0;
	if (line >= 200 || hblank) v |= 0x01;
	if (line >= 224 && line < 240) v |= 0x08;
	return v;
}

void Machine::render(u8 *fb, int pitch)
{
	if (cfg.video == VID_HERCULES) renderHerc(fb, pitch);
	else renderCga(fb, pitch);
}

void Machine::renderCga(u8 *fb, int pitch)
{
	const u8 *vram = mem + 0xB8000;
	bool graphics = cgaMode & 0x02;
	bool hires = cgaMode & 0x10;
	u8 border = graphics && hires ? 0 : (cgaColor & 0x0F);
	if (!(cgaMode & 0x08)) border = 0;
	for (int y = 0; y < SCREEN_H; y++) memset(fb + y * pitch, border, SCREEN_W);
	if (!(cgaMode & 0x08)) return;
	const int X0 = 40;
	u32 start = ((crtc.reg[12] << 8) | crtc.reg[13]) * 2;

	if (graphics) {
		u8 pal[4];
		if (hires) { pal[0] = 0; pal[1] = cgaColor & 0x0F; }
		else {
			u8 in = (cgaColor & 0x10) ? 8 : 0;
			pal[0] = cgaColor & 0x0F;
			if (cgaMode & 0x04) { pal[1] = 3 | in; pal[2] = 4 | in; pal[3] = 7 | in; }
			else if (cgaColor & 0x20) { pal[1] = 3 | in; pal[2] = 5 | in; pal[3] = 7 | in; }
			else { pal[1] = 2 | in; pal[2] = 4 | in; pal[3] = 6 | in; }
		}
		for (int y = 0; y < 200; y++) {
			u32 lo = (start + (y & 1) * 0x2000 + (y >> 1) * 80);
			u8 *d0 = fb + (y * 2) * pitch + X0;
			u8 *d1 = d0 + pitch;
			for (int x = 0; x < 80; x++) {
				u8 b = vram[(lo + x) & 0x3FFF];
				if (hires) {
					for (int k = 7; k >= 0; k--) { u8 c = pal[(b >> k) & 1]; *d0++ = c; *d1++ = c; }
				} else {
					for (int k = 6; k >= 0; k -= 2) {
						u8 c = pal[(b >> k) & 3];
						d0[0] = d0[1] = c; d1[0] = d1[1] = c;
						d0 += 2; d1 += 2;
					}
				}
			}
		}
		return;
	}

	int cols = (cgaMode & 0x01) ? 80 : 40;
	int rows = crtc.reg[6]; if (rows == 0 || rows > 50) rows = 25;
	int ch = (crtc.reg[9] & 0x1F) + 1; if (ch > 16) ch = 8;
	int cw = cols == 80 ? 8 : 16;
	bool blinkEn = cgaMode & 0x20;
	bool blinkPhase = (frameCount >> 4) & 1;
	u32 cursor = ((crtc.reg[14] << 8) | crtc.reg[15]) * 2;
	int cs = crtc.reg[10] & 0x1F, ce = crtc.reg[11] & 0x1F;
	bool curOn = (crtc.reg[10] & 0x60) != 0x20 && ((frameCount >> 3) & 1);
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			u32 a = start + (r * cols + c) * 2;
			u8 chr = vram[a & 0x3FFF], at = vram[(a + 1) & 0x3FFF];
			u8 fg = at & 0x0F, bg = blinkEn ? ((at >> 4) & 7) : (at >> 4);
			if (blinkEn && (at & 0x80) && blinkPhase) fg = bg;
			bool isCur = curOn && a == cursor;
			for (int l = 0; l < ch; l++) {
				int py = (r * ch + l) * 2;
				if (py >= SCREEN_H) break;
				u8 bits = l < 8 ? font8x8[chr * 8 + l] : 0;
				if (isCur && ((cs <= ce && l >= cs && l <= ce) || (cs > ce && (l >= cs || l <= ce)))) bits = 0xFF;
				u8 *d0 = fb + py * pitch + X0 + c * cw;
				u8 *d1 = d0 + pitch;
				for (int k = 7; k >= 0; k--) {
					u8 col = (bits >> k) & 1 ? fg : bg;
					if (cw == 16) { *d0++ = col; *d0++ = col; *d1++ = col; *d1++ = col; }
					else { *d0++ = col; *d1++ = col; }
				}
			}
		}
	}
}

void Machine::renderHerc(u8 *fb, int pitch)
{
	for (int y = 0; y < SCREEN_H; y++) memset(fb + y * pitch, 0, SCREEN_W);
	if (!(hercMode & 0x08)) return;
	const u8 *vram = mem + 0xB0000;
	if ((hercMode & 0x02) && (hercConfig & 0x01)) {
		u32 page = (hercMode & 0x80) && (hercConfig & 0x02) ? 0x8000 : 0;
		const int Y0 = (SCREEN_H - 348) / 2;
		for (int y = 0; y < 348; y++) {
			const u8 *src = vram + page + 0x2000 * (y & 3) + 90 * (y >> 2);
			u8 *d = fb + (y + Y0) * pitch;
			for (int x = 0; x < 90; x++) {
				u8 b = src[x];
				for (int k = 7; k >= 0; k--) *d++ = (b >> k) & 1 ? 15 : 0;
			}
		}
		return;
	}
	const int Y0 = (SCREEN_H - 350) / 2;
	u32 start = ((crtc.reg[12] << 8) | crtc.reg[13]) * 2;
	u32 cursor = ((crtc.reg[14] << 8) | crtc.reg[15]) * 2;
	int cs = crtc.reg[10] & 0x1F, ce = crtc.reg[11] & 0x1F;
	bool curOn = (crtc.reg[10] & 0x60) != 0x20 && ((frameCount >> 3) & 1);
	bool blinkEn = hercMode & 0x20;
	bool blinkPhase = (frameCount >> 4) & 1;
	for (int r = 0; r < 25; r++) {
		for (int c = 0; c < 80; c++) {
			u32 a = (start + (r * 80 + c) * 2) & 0x7FFF;
			u8 chr = vram[a], at = vram[a + 1];
			u8 fg, bg = 0;
			bool under = (at & 0x07) == 1;
			if ((at & 0x77) == 0x70) { fg = 0; bg = 7; under = false; }
			else if ((at & 0x77) == 0) { fg = 0; }
			else fg = (at & 0x08) ? 15 : 7;
			if (blinkEn && (at & 0x80) && blinkPhase) fg = bg;
			if (!blinkEn && (at & 0x80) && bg) bg = 15;
			bool isCur = curOn && a == (cursor & 0x7FFF);
			for (int l = 0; l < 14; l++) {
				u8 bits = font8x14[chr * 14 + l];
				if (under && l == 13) bits = 0xFF;
				if (isCur && l >= cs && l <= ce) bits = 0xFF;
				u8 *d = fb + (Y0 + r * 14 + l) * pitch + c * 9;
				for (int k = 7; k >= 0; k--) *d++ = (bits >> k) & 1 ? fg : bg;
				*d = (chr >= 0xC0 && chr <= 0xDF && (bits & 1)) ? fg : bg;
			}
		}
	}
}
