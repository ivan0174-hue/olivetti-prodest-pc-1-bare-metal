// Prodest PC1 Emulator - BIOS integrato ad alto livello (HLE)
// Implementa i servizi BIOS IBM PC/XT richiamati dalle "trap" F1 nn della ROM (biosrom.asm).
#include "machine.h"
#include "font.h"

#define AL ((u8)cpu.r[AX])
#define AH ((u8)(cpu.r[AX] >> 8))
#define BL ((u8)cpu.r[BX])
#define BH ((u8)(cpu.r[BX] >> 8))
#define CL ((u8)cpu.r[CX])
#define CH ((u8)(cpu.r[CX] >> 8))
#define DL ((u8)cpu.r[DX])
#define DH ((u8)(cpu.r[DX] >> 8))
#define SET_AL(v) cpu.setR8(0, (u8)(v))
#define SET_AH(v) cpu.setR8(4, (u8)(v))
#define SET_BL(v) cpu.setR8(3, (u8)(v))
#define SET_BH(v) cpu.setR8(7, (u8)(v))
#define SET_CL(v) cpu.setR8(1, (u8)(v))
#define SET_CH(v) cpu.setR8(5, (u8)(v))
#define SET_DL(v) cpu.setR8(2, (u8)(v))
#define SET_DH(v) cpu.setR8(6, (u8)(v))

static const char *cpuNames[CPUM_COUNT] = { "NEC V40 8 MHz", "Intel 8088 4.77 MHz", "NEC V40 turbo" };

bool Machine::trap(u8 n)
{
	switch (n) {
	case 0x00: biosPost(); break;
	case 0x08: biosInt08(); break;
	case 0x09: SET_AL(biosInt09()); break;
	case 0x0E: biosInt0E(); break;
	case 0x10: biosInt10(); break;
	case 0x11: cpu.r[AX] = bda16(0x10); break;
	case 0x12: cpu.r[AX] = bda16(0x13); break;
	case 0x13: biosInt13(); break;
	case 0x14: SET_AH(0x80); break;                       // nessuna porta seriale
	case 0x15: biosInt15(); break;
	case 0x16: biosInt16(); break;
	case 0x17: SET_AH(0x01); break;                       // stampante: timeout
	case 0x18: biosInt18(); break;
	case 0x19: setCF(!biosInt19()); break;
	case 0x1A: biosInt1A(); break;
	default: return false;
	}
	return true;
}

// ================================================================== POST
void Machine::biosPost()
{
	memset(mem, 0, cfg.ramKB * 1024);

	// vettori di interrupt
	for (int i = 0; i < 0x20; i++) { mem[i * 4] = 0x53; mem[i * 4 + 1] = 0xFF; mem[i * 4 + 2] = 0x00; mem[i * 4 + 3] = 0xF0; }
	struct { u8 n; u16 off; } vec[] = {
		{ 0x02, 0xE2C3 }, { 0x05, 0xFF54 }, { 0x08, 0xFEA5 }, { 0x09, 0xE987 }, { 0x0A, 0xFF23 }, { 0x0B, 0xFF23 },
		{ 0x0C, 0xFF23 }, { 0x0D, 0xFF23 }, { 0x0E, 0xEF57 }, { 0x0F, 0xFF23 }, { 0x10, 0xF065 }, { 0x11, 0xF84D },
		{ 0x12, 0xF841 }, { 0x13, 0xEC59 }, { 0x14, 0xE739 }, { 0x15, 0xF859 }, { 0x16, 0xE82E }, { 0x17, 0xEFD2 },
		{ 0x18, 0xE3A0 }, { 0x19, 0xE6F2 }, { 0x1A, 0xFE6E }, { 0x1D, 0xF0A4 }, { 0x1E, 0xEFC7 }, { 0x1F, 0xA000 },
	};
	for (unsigned i = 0; i < sizeof vec / sizeof vec[0]; i++) {
		mem[vec[i].n * 4] = (u8)vec[i].off; mem[vec[i].n * 4 + 1] = vec[i].off >> 8;
	}

	// BIOS Data Area
	u16 equip = 0x0001 | 0x000C | 0x0040 | (cfg.video == VID_HERCULES ? 0x0030 : 0x0020);
	sbda16(0x10, equip);
	sbda16(0x13, (u16)cfg.ramKB);
	sbda16(0x1A, 0x1E); sbda16(0x1C, 0x1E);
	sbda16(0x80, 0x1E); sbda16(0x82, 0x3E);

	// PIC: IRQ0 timer, IRQ1 tastiera, IRQ6 floppy
	out8(0x20, 0x13); out8(0x21, 0x08); out8(0x21, 0x09); out8(0x21, 0xBC);
	// PIT: canale 0 a 18.2 Hz, canale 1 refresh, canale 2 beep
	out8(0x43, 0x36); out8(0x40, 0x00); out8(0x40, 0x00);
	out8(0x43, 0x54); out8(0x41, 18);
	out8(0x43, 0xB6); out8(0x42, 0x33); out8(0x42, 0x05);
	// tastiera abilitata
	out8(0x61, 0xC8); out8(0x61, 0x48);
	kbdHead = kbdTail = 0; kbdFull = false;
	// DMA: refresh sul canale 0 (ininfluente), canale 2 per il floppy
	out8(0x0D, 0); out8(0x0B, 0x58); out8(0x0A, 0x00);

	vSetMode(cfg.video == VID_HERCULES ? 7 : 3);

	char buf[128];
	vPrint("Olivetti Prodest PC1 - emulatore bare metal per Raspberry Pi\r\n");
	vPrint("BIOS integrato v1.0  -  F12: menu configurazione\r\n\r\n");
	// "CPU: xxx   RAM: nnn KB"
	pc_strncpy(buf, "CPU: ", sizeof buf);
	int l = pc_strlen(buf);
	pc_strncpy(buf + l, cpuNames[cfg.cpuModel], sizeof buf - l);
	l = pc_strlen(buf);
	pc_strncpy(buf + l, "   RAM: ", sizeof buf - l);
	l = pc_strlen(buf);
	int kb = cfg.ramKB; char num[8]; int nl = 0;
	do { num[nl++] = '0' + kb % 10; kb /= 10; } while (kb);
	while (nl) buf[l++] = num[--nl];
	buf[l] = 0;
	vPrint(buf);
	vPrint(" KB OK\r\nVideo: ");
	vPrint(cfg.video == VID_HERCULES ? "Hercules" : "CGA (Yamaha V6355)");
	vPrint("\r\n\r\n");
}

// ================================================================== timer / tastiera / floppy IRQ
void Machine::biosInt08()
{
	u32 t = bda16(0x6C) | ((u32)bda16(0x6E) << 16);
	t++;
	if (t >= 0x1800B0) { t = 0; sbda8(0x70, 1); }
	sbda16(0x6C, (u16)t); sbda16(0x6E, (u16)(t >> 16));
	u8 m = bda8(0x40);
	if (m) {
		m--; sbda8(0x40, m);
		if (!m) sbda8(0x3F, bda8(0x3F) & 0xF0);
	}
}

void Machine::biosInt0E()
{
	sbda8(0x3E, bda8(0x3E) | 0x80);
	out8(0x20, 0x20);
}

// tabelle scancode XT -> ASCII (layout USA; KEYB del DOS gestisce le altre lingue)
static const char kNormal[0x5A] =
	"\0\x1b" "1234567890-=\x08\x09" "qwertyuiop[]\x0d\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 \0"
	"\0\0\0\0\0\0\0\0\0\0\0\0" "789-456+1230." "\0\0<\0\0";
static const char kShift[0x5A] =
	"\0\x1b" "!@#$%^&*()_+\x08\0" "QWERTYUIOP{}\x0d\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 \0"
	"\0\0\0\0\0\0\0\0\0\0\0\0" "789-456+1230." "\0\0>\0\0";

bool Machine::kbdBufPut(u16 w)
{
	u16 tail = bda16(0x1C);
	u16 next = tail + 2;
	if (next >= bda16(0x82)) next = bda16(0x80);
	if (next == bda16(0x1A)) return false;
	mem[0x400 + tail] = (u8)w; mem[0x401 + tail] = w >> 8;
	sbda16(0x1C, next);
	return true;
}

u8 Machine::biosInt09()
{
	u8 sc = in8(0x60);
	u8 p = in8(0x61);
	out8(0x61, p | 0x80); out8(0x61, p);
	u8 ret = 0;
	u8 f1 = bda8(0x17), f2 = bda8(0x18);
	bool rel = sc & 0x80;
	u8 k = sc & 0x7F;

	switch (k) {
	case 0x2A: if (rel) f1 &= ~0x02; else f1 |= 0x02; goto done;
	case 0x36: if (rel) f1 &= ~0x01; else f1 |= 0x01; goto done;
	case 0x1D: if (rel) f1 &= ~0x04; else f1 |= 0x04; goto done;
	case 0x38:
		if (rel) {
			f1 &= ~0x08;
			u8 alt = bda8(0x19);
			if (alt) { kbdBufPut(alt); sbda8(0x19, 0); }
		} else f1 |= 0x08;
		goto done;
	case 0x3A: if (rel) f2 &= ~0x40; else if (!(f2 & 0x40)) { f2 |= 0x40; f1 ^= 0x40; } goto done;
	case 0x45: if (rel) f2 &= ~0x20; else if (!(f2 & 0x20)) { f2 |= 0x20; f1 ^= 0x20; } goto done;
	case 0x46:
		if (!rel && (f1 & 0x04)) {             // Ctrl+Break
			sbda16(0x1A, bda16(0x80)); sbda16(0x1C, bda16(0x80));
			sbda8(0x71, 0x80);
			kbdBufPut(0x0000);
			ret = 1;
			goto done;
		}
		if (rel) f2 &= ~0x10; else if (!(f2 & 0x10)) { f2 |= 0x10; f1 ^= 0x10; }
		goto done;
	}
	if (rel || k == 0 || k > 0x58) goto done;
	{
		bool shift = f1 & 0x03, ctrl = f1 & 0x04, alt = f1 & 0x08;
		bool caps = f1 & 0x40, num = f1 & 0x20;
		u16 code = 0;
		if (ctrl && alt && k == 0x53) {          // Ctrl+Alt+Canc
			sbda8(0x17, f1); sbda8(0x18, f2);
			out8(0x20, 0x20);
			sbda16(0x72, 0x1234);
			cpu.s[CS] = 0xF000; cpu.ip = 0xE05B;
			return 0;
		}
		if (k >= 0x47 && k <= 0x53 && k != 0x4A && k != 0x4E) {
			// tastierino numerico
			bool digits = num != shift;
			if (alt && kNormal[k] >= '0' && kNormal[k] <= '9') {
				sbda8(0x19, (u8)(bda8(0x19) * 10 + (kNormal[k] - '0')));
				goto done;
			}
			if (k == 0x52 && !digits) f1 ^= 0x80;   // Ins
			if (ctrl) {
				static const u8 cx[13] = { 0x77, 0, 0x84, 0, 0x73, 0, 0x74, 0, 0x75, 0, 0x76, 0, 0 };
				code = cx[k - 0x47] << 8;
			} else if (digits) code = (k << 8) | (u8)kNormal[k];
			else code = k << 8;
		} else if (k >= 0x3B && k <= 0x44) {     // F1-F10
			int i = k - 0x3B;
			if (alt) code = (0x68 + i) << 8;
			else if (ctrl) code = (0x5E + i) << 8;
			else if (shift) code = (0x54 + i) << 8;
			else code = k << 8;
		} else if (alt) {
			if (k >= 0x02 && k <= 0x0D) code = (k + 0x76) << 8;
			else code = k << 8;
		} else if (ctrl) {
			char c = kNormal[k];
			if (c >= 'a' && c <= 'z') code = (k << 8) | (c - 'a' + 1);
			else switch (k) {
				case 0x1A: code = 0x1A1B; break;
				case 0x1B: code = 0x1B1D; break;
				case 0x2B: code = 0x2B1C; break;
				case 0x1C: code = 0x1C0A; break;
				case 0x0E: code = 0x0E7F; break;
				case 0x03: code = 0x0300; break;
				case 0x07: code = 0x071E; break;
				case 0x0C: code = 0x0C1F; break;
				case 0x37: code = 0x7200; break;
				default: code = 0;
			}
		} else {
			char c = shift ? kShift[k] : kNormal[k];
			if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c ^= 0x20;
			if (k == 0x0F && shift) code = 0x0F00;
			else if (c || k == 0x01) code = (k << 8) | (u8)c;
		}
		if (code || k == 0x03) kbdBufPut(code);
	}
done:
	sbda8(0x17, f1); sbda8(0x18, f2);
	out8(0x20, 0x20);
	return ret;
}

void Machine::biosInt16()
{
	u16 head = bda16(0x1A), tail = bda16(0x1C);
	switch (AH) {
	case 0x00: case 0x10:
		if (head == tail) { setCF(true); return; }
		cpu.r[AX] = mem[0x400 + head] | (mem[0x401 + head] << 8);
		head += 2; if (head >= bda16(0x82)) head = bda16(0x80);
		sbda16(0x1A, head);
		break;
	case 0x01: case 0x11:
		if (head == tail) cpu.setFlag(F_ZF, true);
		else { cpu.setFlag(F_ZF, false); cpu.r[AX] = mem[0x400 + head] | (mem[0x401 + head] << 8); }
		break;
	case 0x02: SET_AL(bda8(0x17)); break;
	case 0x12: SET_AL(bda8(0x17)); SET_AH(bda8(0x18)); break;
	case 0x05: SET_AL(kbdBufPut(cpu.r[CX]) ? 0 : 1); break;
	default: break;
	}
	setCF(false);
}

// ================================================================== dischi
void Machine::biosInt13()
{
	u8 fn = AH, drv = DL;
	u8 status = 0;
	if (drv >= 0x80) {                              // nessun disco fisso
		if (fn == 0x08) { SET_DL(0); }
		if (fn == 0x15) { SET_AH(0); setCF(false); return; }
		SET_AH(0x01); setCF(true); return;
	}
	Disk *d = drv < 2 ? &disk[drv] : 0;
	sbda8(0x3F, (bda8(0x3F) & 0xF0) | (drv < 2 ? 1 << drv : 0));
	sbda8(0x40, 37);
	switch (fn) {
	case 0x00: status = 0; break;
	case 0x01: SET_AH(bda8(0x41)); setCF(bda8(0x41) != 0); return;
	case 0x02: case 0x03: case 0x04: {
		int count = AL;
		int c = CH | ((CL & 0xC0) << 2), s = CL & 0x3F, h = DH;
		u32 buf = ((u32)cpu.s[ES] << 4) + cpu.r[BX];
		int done = 0;
		if (!d) { status = 0x01; break; }
		if (!d->present) { status = 0x80; break; }
		if (fn == 0x03 && d->writeProtect) { status = 0x03; break; }
		for (; done < count; done++) {
			if (s < 1 || s > d->spt || h >= d->heads || c >= d->cyls) { status = 0x04; break; }
			u32 off = (u32)d->lba(c, h, s) * 512;
			if (fn == 0x02) for (int i = 0; i < 512; i++) cpu.wr8(buf + i, d->data[off + i]);
			else if (fn == 0x03) { for (int i = 0; i < 512; i++) d->data[off + i] = cpu.rd8(buf + i); d->dirty = true; }
			buf += 512;
			if (++s > d->spt) { s = 1; if (++h >= d->heads) { h = 0; c++; } }
		}
		SET_AL(done);
	} break;
	case 0x05: {
		if (!d) { status = 0x01; break; }
		if (!d->present) { status = 0x80; break; }
		if (d->writeProtect) { status = 0x03; break; }
		int c = CH, h = DH;
		if (c >= d->cyls || h >= d->heads) { status = 0x04; break; }
		memset(d->data + (u32)d->lba(c, h, 1) * 512, 0xF6, d->spt * 512);
		d->dirty = true;
	} break;
	case 0x08: {
		if (!d) { status = 0x01; break; }
		int cyls = 80, spt = 9, heads = 2; u8 type = 3;
		if (d->present) {
			cyls = d->cyls; spt = d->spt; heads = d->heads;
			type = cyls <= 40 ? 1 : spt >= 18 ? 4 : spt == 15 ? 2 : 3;
		}
		SET_BL(type);
		SET_CH((cyls - 1) & 0xFF);
		SET_CL(spt | (((cyls - 1) >> 2) & 0xC0));
		SET_DH(heads - 1);
		SET_DL(2);
		cpu.s[ES] = 0xF000; cpu.r[DI] = 0xEFC7;
		cpu.r[AX] = 0;
		sbda8(0x41, 0);
		setCF(false);
		return;
	}
	case 0x15:
		if (!d) { SET_AH(0); setCF(false); return; }
		SET_AH(0x01); setCF(false); return;      // floppy senza linea di cambio disco
	case 0x16: status = 0x06; break;
	case 0x17: case 0x18: status = 0; break;
	default: status = 0x01; break;
	}
	sbda8(0x41, status);
	SET_AH(status);
	setCF(status != 0);
}

// ================================================================== orologio
void Machine::biosInt1A()
{
	switch (AH) {
	case 0x00:
		cpu.r[CX] = bda16(0x6E); cpu.r[DX] = bda16(0x6C);
		SET_AL(bda8(0x70)); sbda8(0x70, 0);
		setCF(false);
		break;
	case 0x01:
		sbda16(0x6E, cpu.r[CX]); sbda16(0x6C, cpu.r[DX]); sbda8(0x70, 0);
		setCF(false);
		break;
	default:
		setCF(true);   // il PC1 non ha orologio in tempo reale
		break;
	}
}

void Machine::biosInt18()
{
	vPrint("\r\nROM BASIC non presente: avviare GW-BASIC da disco.\r\nPremere un tasto per riprovare l'avvio...\r\n");
}

bool Machine::biosInt19()
{
	Disk &d = disk[0];
	if (d.present && d.size >= 512) {
		for (int i = 0; i < 512; i++) mem[0x7C00 + i] = d.data[i];
		return true;
	}
	vPrint("Nessun disco di avvio nell'unita' A:\r\n");
	vPrint("Premere F12 per inserire un floppy, poi un tasto qualsiasi...\r\n");
	return false;
}

// ================================================================== cassetta (INT 15h)
// Formato IBM PC: bit 1 = periodo 1 ms, bit 0 = periodo 0,5 ms, MSB per primo,
// leader di bit a 1, bit di sincronismo 0, byte 16h, blocchi da 256 byte + CRC CCITT.
static u16 crcBit(u16 crc, int bit)
{
	int msb = (crc >> 15) & 1;
	crc <<= 1;
	if (msb ^ bit) crc ^= 0x1021;
	return crc;
}

// legge il prossimo bit dal nastro: 0, 1 oppure -1 a fine nastro.
// 'pos' resta sempre su un fronte di salita: il bit e' la distanza dal fronte successivo.
static int tapeBit(Tape &t)
{
	u32 start = (u32)(t.pos >> 16);
	u32 p = start + 1;
	bool low = false;
	while (p < t.len) {
		u8 v = t.data[p];
		if (v < 118) low = true;
		else if (v > 138 && low) break;
		p++;
	}
	if (p >= t.len) { t.pos = (u64)t.len << 16; return -1; }
	t.pos = (u64)p << 16;
	u32 us = (u32)((u64)(p - start) * 1000000 / t.rate);
	return us > 750 ? 1 : 0;
}

static int tapeByte(Tape &t, u16 *crc)
{
	int v = 0;
	for (int i = 0; i < 8; i++) {
		int b = tapeBit(t);
		if (b < 0) return -1;
		v = (v << 1) | b;
		if (crc) *crc = crcBit(*crc, b);
	}
	return v;
}

int Machine::tapeReadBlock(u8 *dst, int n, int *status)
{
	*status = 0;
	if (!tape.present || tape.len == 0) { *status = 4; return 0; }
	for (;;) {
		// leader + sincronismo
		int ones = 0;
		for (;;) {
			int b = tapeBit(tape);
			if (b < 0) { *status = 4; return 0; }
			if (b) ones++;
			else if (ones >= 64) break;
			else ones = 0;
		}
		int sync = tapeByte(tape, 0);
		if (sync < 0) { *status = 4; return 0; }
		if (sync == 0x16) break;
	}
	int got = 0;
	while (got < n) {
		u16 crc = 0xFFFF;
		u8 blk[256];
		for (int i = 0; i < 256; i++) {
			int v = tapeByte(tape, &crc);
			if (v < 0) { *status = 2; return got; }
			blk[i] = (u8)v;
		}
		if (tapeByte(tape, &crc) < 0 || tapeByte(tape, &crc) < 0) { *status = 2; return got; }
		int take = n - got < 256 ? n - got : 256;
		memcpy(dst + got, blk, take);
		got += take;
		if (crc != 0x1D0F) { *status = 1; return got; }
	}
	return got;
}

static void tapeEmit(Tape &t, int bit)
{
	u32 half = bit ? t.rate / 2000 : t.rate / 4000;
	if (half < 1) half = 1;
	u32 p = (u32)(t.pos >> 16);
	for (int ph = 0; ph < 2; ph++) {
		for (u32 i = 0; i < half && p < TAPE_MAX; i++) t.data[p++] = ph == 0 ? 0xD0 : 0x30;
	}
	if (p > t.len) t.len = p;
	t.pos = (u64)p << 16;
}

static void tapeEmitByte(Tape &t, u8 v, u16 *crc)
{
	for (int i = 7; i >= 0; i--) {
		int b = (v >> i) & 1;
		tapeEmit(t, b);
		if (crc) *crc = crcBit(*crc, b);
	}
}

void Machine::tapeWriteData(const u8 *src, int n)
{
	if (!tape.present) tape.blank();
	if (tape.len == 0) tape.rate = 44100;
	for (int i = 0; i < 256; i++) tapeEmitByte(tape, 0xFF, 0);
	tapeEmit(tape, 0);
	tapeEmitByte(tape, 0x16, 0);
	for (int off = 0; off < n || off == 0; off += 256) {
		u16 crc = 0xFFFF;
		for (int i = 0; i < 256; i++) tapeEmitByte(tape, off + i < n ? src[off + i] : 0, &crc);
		crc = ~crc;
		tapeEmitByte(tape, crc >> 8, 0);
		tapeEmitByte(tape, (u8)crc, 0);
		if (n == 0) break;
	}
	for (int i = 0; i < 4; i++) tapeEmitByte(tape, 0xFF, 0);
	tape.dirty = true;
}

void Machine::biosInt15()
{
	switch (AH) {
	case 0x00: tape.motor = true; SET_AH(0); setCF(false); break;
	case 0x01: tape.motor = false; SET_AH(0); setCF(false); break;
	case 0x02: {
		static u8 tmp[65536];
		int n = cpu.r[CX], st;
		int got = tapeReadBlock(tmp, n, &st);
#ifdef TAPE_DEBUG
		fprintf(stderr, "tape read n=%d got=%d st=%d pos=%u len=%u\n", n, got, st, (u32)(tape.pos>>16), tape.len);
#endif
		u32 base = ((u32)cpu.s[ES] << 4);
		for (int i = 0; i < got; i++) cpu.wr8(base + (u16)(cpu.r[BX] + i), tmp[i]);
		cpu.r[DX] = got;
		cpu.r[BX] += got;
		SET_AH(st);
		setCF(st != 0);
	} break;
	case 0x03: {
		static u8 tmp[65536];
		int n = cpu.r[CX];
		for (int i = 0; i < n; i++) tmp[i] = cpu.rdS8(cpu.s[ES], (u16)(cpu.r[BX] + i));
		tapeWriteData(tmp, n);
		cpu.r[BX] += n; cpu.r[CX] = 0;
		SET_AH(0); setCF(false);
	} break;
	default:
		SET_AH(0x86); setCF(true);
		break;
	}
}

// ================================================================== video (INT 10h)
void Machine::crtcWriteReg(u8 r, u8 v)
{
	u16 port = cfg.video == VID_HERCULES ? 0x3B4 : 0x3D4;
	out8(port, r); out8(port + 1, v);
}

void Machine::vSetMode(u8 mode)
{
	bool clear = !(mode & 0x80);
	mode &= 0x7F;
	if (cfg.video == VID_HERCULES) mode = 7;
	else {
		if (mode == 7) mode = 3;
		if (mode > 6) return;              // modi EGA/VGA non disponibili
	}
	static const u8 tblIdx[8] = { 0, 0, 1, 1, 2, 2, 2, 3 };
	const u8 *tbl = mem + 0xFF0A4;
	for (int r = 0; r < 16; r++) crtcWriteReg(r, tbl[tblIdx[mode] * 16 + r]);
	u16 regen[8] = { 0x800, 0x800, 0x1000, 0x1000, 0x4000, 0x4000, 0x4000, 0x1000 };
	u8 cols[8] = { 40, 40, 80, 80, 40, 40, 80, 80 };
	u8 msv = tbl[64 + 8 + 8 + mode];
	if (cfg.video == VID_HERCULES) {
		out8(0x3BF, 0x00);
		out8(0x3B8, 0x28);
		if (clear) for (u32 i = 0; i < 0x8000; i += 2) { mem[0xB0000 + i] = ' '; mem[0xB0001 + i] = 0x07; }
	} else {
		out8(0x3D8, msv);
		out8(0x3D9, mode == 6 ? 0x3F : 0x30);
		if (clear) {
			if (mode <= 3) for (u32 i = 0; i < 0x4000; i += 2) { mem[0xB8000 + i] = ' '; mem[0xB8001 + i] = 0x07; }
			else memset(mem + 0xB8000, 0, 0x4000);
		}
	}
	sbda8(0x49, mode);
	sbda16(0x4A, cols[mode]);
	sbda16(0x4C, regen[mode]);
	sbda16(0x4E, 0);
	for (int i = 0; i < 16; i++) sbda8(0x50 + i, 0);
	sbda16(0x60, mode == 7 ? 0x0B0C : 0x0607);
	sbda8(0x62, 0);
	sbda16(0x63, cfg.video == VID_HERCULES ? 0x3B4 : 0x3D4);
	sbda8(0x65, msv);
	sbda8(0x66, mode == 6 ? 0x3F : 0x30);
	vUpdateCursorHw();
}

void Machine::vUpdateCursorHw()
{
	u8 page = bda8(0x62);
	u16 pos = bda16(0x50 + page * 2);
	u16 cols = bda16(0x4A);
	u16 a = bda16(0x4E) / 2 + (pos >> 8) * cols + (pos & 0xFF);
	crtcWriteReg(14, a >> 8);
	crtcWriteReg(15, (u8)a);
}

void Machine::vSetCursor(u8 page, u8 row, u8 col)
{
	if (page > 7) return;
	sbda16(0x50 + page * 2, (row << 8) | col);
	if (page == bda8(0x62)) vUpdateCursorHw();
}

static bool isGfx(u8 mode) { return mode >= 4 && mode <= 6; }

static const u8 *glyph(const u8 *mem, u8 ch)
{
	if (ch < 128) return mem + 0xFFA6E + ch * 8;
	u32 v = (mem[0x7C] | (mem[0x7D] << 8)) + ((u32)(mem[0x7E] | (mem[0x7F] << 8)) << 4);
	return mem + ((v + (ch - 128) * 8) & 0xFFFFF);
}

void Machine::vWriteChar(u8 ch, u8 attr, u8 page, u8 row, u8 col, bool useAttr)
{
	u8 mode = bda8(0x49);
	u16 cols = bda16(0x4A);
	if (!isGfx(mode)) {
		u32 a = ((u32)vbase() << 4) + page * bda16(0x4C) + (row * cols + col) * 2;
		cpu.wr8(a, ch);
		if (useAttr) cpu.wr8(a + 1, attr);
		return;
	}
	if (row >= 25 || col >= cols) return;
	const u8 *g = glyph(mem, ch);
	bool xr = attr & 0x80;
	for (int l = 0; l < 8; l++) {
		int y = row * 8 + l;
		u32 base = 0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80;
		if (mode == 6) {
			u8 v = (attr & 1) ? g[l] : 0;
			u32 a = base + col;
			cpu.wr8(a, xr ? cpu.rd8(a) ^ v : v);
		} else {
			u16 v = 0;
			for (int b = 0; b < 8; b++) if (g[l] & (0x80 >> b)) v |= (attr & 3) << (14 - b * 2);
			u32 a = base + col * 2;
			u8 hi = v >> 8, lo = (u8)v;
			cpu.wr8(a, xr ? cpu.rd8(a) ^ hi : hi);
			cpu.wr8(a + 1, xr ? cpu.rd8(a + 1) ^ lo : lo);
		}
	}
}

void Machine::vScroll(bool up, u8 n, u8 attr, u8 r1, u8 c1, u8 r2, u8 c2)
{
	u8 mode = bda8(0x49);
	u16 cols = bda16(0x4A);
	if (c2 >= cols) c2 = cols - 1;
	if (r2 > 24) r2 = 24;
	if (r1 > r2 || c1 > c2) return;
	int h = r2 - r1 + 1;
	if (n == 0 || n > h) n = h;
	if (!isGfx(mode)) {
		u32 base = ((u32)vbase() << 4) + bda16(0x4E);
		for (int i = 0; i < h; i++) {
			int dr = up ? r1 + i : r2 - i;
			int sr = up ? dr + n : dr - n;
			for (int c = c1; c <= c2; c++) {
				u32 d = base + (dr * cols + c) * 2;
				if (i < h - n) { u32 s = base + (sr * cols + c) * 2; cpu.wr8(d, cpu.rd8(s)); cpu.wr8(d + 1, cpu.rd8(s + 1)); }
				else { cpu.wr8(d, ' '); cpu.wr8(d + 1, attr); }
			}
		}
		return;
	}
	int bpc = mode == 6 ? 1 : 2;
	u8 fill = mode == 6 ? ((attr & 1) ? 0xFF : 0) : (u8)((attr & 3) * 0x55);
	for (int i = 0; i < h; i++) {
		int dr = up ? r1 + i : r2 - i;
		int sr = up ? dr + n : dr - n;
		for (int l = 0; l < 8; l++) {
			int dy = dr * 8 + l, sy = sr * 8 + l;
			u32 da = 0xB8000 + (dy & 1) * 0x2000 + (dy >> 1) * 80 + c1 * bpc;
			u32 sa = 0xB8000 + (sy & 1) * 0x2000 + (sy >> 1) * 80 + c1 * bpc;
			for (int x = 0; x < (c2 - c1 + 1) * bpc; x++)
				cpu.wr8(da + x, i < h - n ? cpu.rd8(sa + x) : fill);
		}
	}
}

void Machine::vTeletype(u8 ch, u8 color)
{
	u8 page = isGfx(bda8(0x49)) ? 0 : bda8(0x62);
	u16 pos = bda16(0x50 + page * 2);
	u8 row = pos >> 8, col = pos & 0xFF;
	u16 cols = bda16(0x4A);
	switch (ch) {
	case 7: return;
	case 8: if (col) col--; break;
	case 10: row++; break;
	case 13: col = 0; break;
	default:
		vWriteChar(ch, color, page, row, col, false);
		if (++col >= cols) { col = 0; row++; }
		break;
	}
	if (row > 24) {
		u8 attr = 0;
		if (!isGfx(bda8(0x49))) attr = cpu.rd8(((u32)vbase() << 4) + bda16(0x4E) + (24 * cols + col) * 2 + 1);
		vScroll(true, 1, attr, 0, 0, 24, cols - 1);
		row = 24;
	}
	vSetCursor(page, row, col);
}

void Machine::vPrint(const char *s)
{
	while (*s) vTeletype((u8)*s++, 7);
}

void Machine::vPutPixel(u16 x, u16 y, u8 color)
{
	u8 mode = bda8(0x49);
	if (!isGfx(mode) || y >= 200) return;
	u32 base = 0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80;
	if (mode == 6) {
		if (x >= 640) return;
		u32 a = base + x / 8; u8 m = 0x80 >> (x & 7);
		u8 v = cpu.rd8(a);
		if (color & 0x80) { if (color & 1) v ^= m; }
		else v = (color & 1) ? (v | m) : (v & ~m);
		cpu.wr8(a, v);
	} else {
		if (x >= 320) return;
		u32 a = base + x / 4; int sh = (3 - (x & 3)) * 2;
		u8 v = cpu.rd8(a);
		if (color & 0x80) v ^= (color & 3) << sh;
		else v = (v & ~(3 << sh)) | ((color & 3) << sh);
		cpu.wr8(a, v);
	}
}

u8 Machine::vGetPixel(u16 x, u16 y)
{
	u8 mode = bda8(0x49);
	if (!isGfx(mode) || y >= 200) return 0;
	u32 base = 0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80;
	if (mode == 6) return (cpu.rd8(base + x / 8) >> (7 - (x & 7))) & 1;
	return (cpu.rd8(base + x / 4) >> ((3 - (x & 3)) * 2)) & 3;
}

void Machine::biosInt10()
{
	u8 mode = bda8(0x49);
	u16 cols = bda16(0x4A);
	switch (AH) {
	case 0x00: vSetMode(AL); break;
	case 0x01:
		sbda16(0x60, cpu.r[CX]);
		crtcWriteReg(10, CH); crtcWriteReg(11, CL);
		break;
	case 0x02: vSetCursor(BH, DH, DL); break;
	case 0x03: {
		u8 p = BH > 7 ? 0 : BH;
		cpu.r[DX] = bda16(0x50 + p * 2);
		cpu.r[CX] = bda16(0x60);
	} break;
	case 0x04: SET_AH(0); break;                         // penna ottica assente
	case 0x05:
		if (!isGfx(mode) && AL < 8) {
			sbda8(0x62, AL);
			sbda16(0x4E, AL * bda16(0x4C));
			u16 st = bda16(0x4E) / 2;
			crtcWriteReg(12, st >> 8); crtcWriteReg(13, (u8)st);
			vUpdateCursorHw();
		}
		break;
	case 0x06: vScroll(true, AL, BH, CH, CL, DH, DL); break;
	case 0x07: vScroll(false, AL, BH, CH, CL, DH, DL); break;
	case 0x08: {
		u8 p = BH > 7 ? 0 : BH;
		u16 pos = bda16(0x50 + p * 2);
		if (!isGfx(mode)) {
			u32 a = ((u32)vbase() << 4) + p * bda16(0x4C) + ((pos >> 8) * cols + (pos & 0xFF)) * 2;
			cpu.r[AX] = cpu.rd8(a) | (cpu.rd8(a + 1) << 8);
		} else {
			// riconosce il carattere confrontando con il font
			int row = pos >> 8, col = pos & 0xFF;
			u8 pat[8];
			for (int l = 0; l < 8; l++) {
				int y = row * 8 + l; u8 v = 0;
				for (int b = 0; b < 8; b++) if (vGetPixel((mode == 6 ? col * 8 : col * 8) + b, y)) v |= 0x80 >> b;
				pat[l] = v;
			}
			cpu.r[AX] = 0;
			for (int c = 0; c < 256; c++) {
				const u8 *g = glyph(mem, (u8)c);
				bool m = true;
				for (int l = 0; l < 8 && m; l++) m = g[l] == pat[l];
				if (m) { SET_AL(c); break; }
			}
		}
	} break;
	case 0x09: case 0x0A: {
		u8 p = BH > 7 ? 0 : BH;
		u16 pos = bda16(0x50 + p * 2);
		int row = pos >> 8, col = pos & 0xFF;
		for (int i = 0; i < cpu.r[CX]; i++) {
			vWriteChar(AL, BL, p, row, col, AH == 0x09 || isGfx(mode));
			if (++col >= cols) { col = 0; if (++row > 24) break; }
		}
	} break;
	case 0x0B: {
		u8 c = bda8(0x66);
		if (BH == 0) c = (c & 0xE0) | (BL & 0x1F);
		else c = (c & 0xDF) | ((BL & 1) << 5);
		sbda8(0x66, c);
		if (cfg.video == VID_CGA) out8(0x3D9, c);
	} break;
	case 0x0C: vPutPixel(cpu.r[CX], cpu.r[DX], AL); break;
	case 0x0D: SET_AL(vGetPixel(cpu.r[CX], cpu.r[DX])); break;
	case 0x0E: vTeletype(AL, BL); break;
	case 0x0F: SET_AL(mode); SET_AH(cols); SET_BH(bda8(0x62)); break;
	case 0x13: {
		u8 p = BH > 7 ? 0 : BH;
		u16 save = bda16(0x50 + p * 2);
		vSetCursor(p, DH, DL);
		u16 off = cpu.r[BP];
		for (int i = 0; i < cpu.r[CX]; i++) {
			u8 c = cpu.rdS8(cpu.s[ES], off++);
			u8 a = BL;
			if (AL & 2) a = cpu.rdS8(cpu.s[ES], off++);
			if (c == 7 || c == 8 || c == 10 || c == 13) { vTeletype(c, a); continue; }
			u16 pos = bda16(0x50 + p * 2);
			vWriteChar(c, a, p, pos >> 8, pos & 0xFF, true);
			u8 row = pos >> 8, col = (pos & 0xFF) + 1;
			if (col >= cols) { col = 0; row++; }
			if (row > 24) { vScroll(true, 1, 7, 0, 0, 24, cols - 1); row = 24; }
			vSetCursor(p, row, col);
		}
		if (!(AL & 1)) vSetCursor(p, save >> 8, save & 0xFF);
	} break;
	default: break;                                      // funzioni EGA/VGA: non presenti
	}
}
