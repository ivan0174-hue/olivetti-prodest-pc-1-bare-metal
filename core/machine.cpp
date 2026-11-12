// Prodest PC1 Emulator - scheda madre: memoria, I/O, PIC 8259, PIT 8253, PPI 8255,
// tastiera XT, DMA 8237, altoparlante, cassetta, temporizzazione
#include "machine.h"
#include "biosrom.h"
#include "font.h"

static const u32 PIT_HZ = 1193182;
static const u32 PIT_PER_FRAME = PIT_HZ / 60;   // 19886

// ------------------------------------------------------------------ Config
void Config::setDefaults()
{
	cpuModel = CPUM_V40_8MHZ;
	ramKB = 512;
	video = VID_CGA;
	monitor = MON_COLOR;
	volume = 1;
	builtinBios = true;
	romFile[0] = floppyA[0] = floppyB[0] = tapeFile[0] = 0;
}

// ------------------------------------------------------------------ Disk
bool Disk::insert(u32 bytes)
{
	static const struct { u32 size; int c, h, s; } geo[] = {
		{ 163840, 40, 1, 8 }, { 184320, 40, 1, 9 }, { 327680, 40, 2, 8 }, { 368640, 40, 2, 9 },
		{ 655360, 80, 2, 8 }, { 737280, 80, 2, 9 }, { 1228800, 80, 2, 15 }, { 1474560, 80, 2, 18 },
	};
	for (unsigned i = 0; i < sizeof geo / sizeof geo[0]; i++) {
		if (bytes == geo[i].size) {
			cyls = geo[i].c; heads = geo[i].h; spt = geo[i].s;
			size = bytes; present = true; dirty = false;
			return true;
		}
	}
	// immagine di dimensione insolita: prova a leggere il BPB
	if (bytes >= 512 && bytes <= DISK_MAX) {
		int bps = data[11] | (data[12] << 8);
		int s = data[24] | (data[25] << 8);
		int h = data[26] | (data[27] << 8);
		if (bps == 512 && s >= 8 && s <= 18 && h >= 1 && h <= 2) {
			spt = s; heads = h; cyls = (bytes / 512 + s * h - 1) / (s * h);
			size = bytes; present = true; dirty = false;
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------ Tape (WAV)
static u32 rd32le(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u16 rd16le(const u8 *p) { return p[0] | (p[1] << 8); }
static void wr32le(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16le(u8 *p, u16 v) { p[0] = v; p[1] = v >> 8; }

bool Tape::loadWav(const u8 *w, u32 n)
{
	if (n < 44 || w[0] != 'R' || w[1] != 'I' || w[2] != 'F' || w[3] != 'F') return false;
	u32 p = 12;
	int ch = 1, bits = 8; u32 sr = 44100;
	bool fmt = false;
	while (p + 8 <= n) {
		u32 sz = rd32le(w + p + 4);
		if (w[p] == 'f' && w[p + 1] == 'm' && w[p + 2] == 't') {
			if (rd16le(w + p + 8) != 1) return false;      // solo PCM
			ch = rd16le(w + p + 10);
			sr = rd32le(w + p + 12);
			bits = rd16le(w + p + 22);
			fmt = true;
		} else if (w[p] == 'd' && w[p + 1] == 'a' && w[p + 2] == 't' && w[p + 3] == 'a' && fmt) {
			const u8 *d = w + p + 8;
			if (sz > n - p - 8) sz = n - p - 8;
			int frame = ch * (bits / 8);
			if (frame <= 0 || (bits != 8 && bits != 16)) return false;
			u32 frames = sz / frame;
			if (frames > TAPE_MAX) frames = TAPE_MAX;
			for (u32 i = 0; i < frames; i++) {
				const u8 *s = d + i * frame;
				data[i] = bits == 8 ? s[0] : (u8)((s16)rd16le(s) / 256 + 128);
			}
			len = frames; rate = sr; pos = 0;
			present = true; dirty = false;
			return true;
		}
		p += 8 + sz + (sz & 1);
	}
	return false;
}

u32 Tape::saveWav(u8 *o, u32 max) const
{
	if (max < 44 + len) return 0;
	memcpy(o, "RIFF", 4); wr32le(o + 4, 36 + len);
	memcpy(o + 8, "WAVEfmt ", 8); wr32le(o + 16, 16);
	wr16le(o + 20, 1); wr16le(o + 22, 1); wr32le(o + 24, rate); wr32le(o + 28, rate);
	wr16le(o + 32, 1); wr16le(o + 34, 8);
	memcpy(o + 36, "data", 4); wr32le(o + 40, len);
	memcpy(o + 44, data, len);
	return 44 + len;
}

// ------------------------------------------------------------------ Machine
Machine::Machine()
{
	cfg.setDefaults();
	cpu.bus = this;
	cpu.mem = mem;
	cpu.wmap = wmap;
	for (int i = 0; i < 2; i++) disk[i].eject();
	tape.present = false; tape.len = 0; tape.rate = 44100; tape.pos = 0; tape.motor = false; tape.dirty = false;
	audioW = audioR = 0;
	frameCount = 0;
	romLoaded = false;
	sampleAcc = sampleTicks = sampleFrac = 0;
}

void Machine::applyCpuModel(int model)
{
	cfg.cpuModel = model;
	switch (model) {
	case CPUM_8088_477: cpu.type = CPU_8088; cyclesPerFrame = 4772727 / 60; break;
	case CPUM_V40_TURBO: cpu.type = CPU_V20; cyclesPerFrame = 32000000 / 60; break;
	default: cpu.type = CPU_V20; cyclesPerFrame = 8000000 / 60; break;
	}
}

void Machine::powerOn(const Config &c, const u8 *rom, u32 romSize)
{
	cfg = c;
	if (cfg.ramKB < 64) cfg.ramKB = 64;
	if (cfg.ramKB > 640) cfg.ramKB = 640;
	applyCpuModel(cfg.cpuModel);

	// mappa memoria
	memset(mem, 0xFF, sizeof mem);
	memset(mem, 0, cfg.ramKB * 1024);
	memset(wmap, 0, sizeof wmap);
	for (u32 p = 0; p < (u32)cfg.ramKB / 4; p++) wmap[p] = 1;
	if (cfg.video == VID_HERCULES) { memset(mem + 0xB0000, 0, 0x10000); for (int p = 0xB0; p < 0xC0; p++) wmap[p] = 1; }
	else { memset(mem + 0xB8000, 0, 0x8000); for (int p = 0xB8; p < 0xC0; p++) wmap[p] = 1; }

	romLoaded = false;
	if (!cfg.builtinBios && rom && romSize >= 2048 && romSize <= 0x40000) {
		memcpy(mem + 0x100000 - romSize, rom, romSize);
		romLoaded = true;
	} else {
		cfg.builtinBios = true;
		memcpy(mem + 0xF0000 + BIOSROM_ORG, biosrom_bin, sizeof biosrom_bin);
		memcpy(mem + 0xFFA6E, font8x8, 128 * 8);          // F000:FA6E caratteri 0-127
		memcpy(mem + 0xF0000 + 0xA000, font8x8 + 1024, 1024);  // INT 1Fh: caratteri 128-255
	}
	cpu.hleTraps = cfg.builtinBios;

	// periferiche
	pic.reset();
	for (int i = 0; i < 3; i++) {
		PitChan &p = pit[i];
		p.reload = 65536; p.count = 65536; p.mode = 3; p.rw = 3; p.ctrl = 0;
		p.writeHi = p.readHi = p.latched = false; p.gate = (i != 2); p.out = true; p.running = false;
		p.bcd = false; p.latch = 0; p.wlo = 0;
	}
	dma.reset();
	fdc.reset();
	ppiB = 0x08; kbdData = 0; kbdFull = false; kbdHead = kbdTail = 0; kbdTimer = 0; kbdClockLowTicks = 0;
	crtc.idx = 0; memset(crtc.reg, 0, sizeof crtc.reg);
	cgaMode = 0; cgaColor = 0; hercMode = 0; hercConfig = 0;
	frameCycle = 0; pitAcc = 0;
	tape.motor = false;

	cpu.reset();
	cpu.irqLine = false;
}

void Machine::warmReset()
{
	mem[0x472] = 0x34; mem[0x473] = 0x12;
	pic.reset();
	fdc.reset();
	cpu.reset();
}

// ------------------------------------------------------------------ esecuzione
void Machine::runFrame()
{
	frameCycle = 0;
	while (frameCycle < cyclesPerFrame) {
		int c;
		if (cpu.halted && !(cpu.irqLine && (cpu.getFlag(F_IF)))) {
			// CPU ferma: avanza fino al prossimo evento del timer
			c = 200;
		} else {
			c = cpu.step();
		}
		frameCycle += c;
		pitAcc += (u32)c * PIT_PER_FRAME;
		if (pitAcc >= cyclesPerFrame) {
			int ticks = pitAcc / cyclesPerFrame;
			pitAcc -= ticks * cyclesPerFrame;
			advance(ticks);
		}
	}
	frameCount++;
}

void Machine::advance(int ticks)
{
	pitTick(0, ticks);
	int refresh = pitTick(1, ticks);
	if (refresh && !(dma.mask & 1)) {
		// refresh DRAM: il canale 1 del PIT richiede cicli DMA sul canale 0
		u32 c = dma.curCount[0];
		if ((u32)refresh > c) {
			dma.status |= 1;
			u32 period = (u32)dma.baseCount[0] + 1;
			refresh -= c + 1;
			if (dma.mode[0] & 0x10) dma.curCount[0] = (u16)(dma.baseCount[0] - (refresh % period));
			else { dma.curCount[0] = 0xFFFF; dma.mask |= 1; }
		} else dma.curCount[0] = (u16)(c - refresh);
	}
	pitTick(2, ticks);
	speakerTick(ticks);
	if (tape.present) tapeTick(ticks);
	if (fdc.irqDelay > 0) {
		fdc.irqDelay -= ticks;
		if (fdc.irqDelay <= 0) { fdc.irqDelay = 0; if (fdc.dor & 0x08) picRaise(6); }
	}
	kbdTimer += ticks;
	if (kbdTimer >= 1200) {         // ~1 ms
		int t = kbdTimer; kbdTimer = 0;
		if (!(ppiB & 0x40)) kbdClockLowTicks += t;
		kbdService();
	}
}

// ------------------------------------------------------------------ I/O
u8 Machine::in8(u16 port)
{
	port &= 0x3FF;
	if (port < 0x10) return dmaRead(port);
	if (port == 0x20 || port == 0x21) return picRead(port);
	if (port >= 0x40 && port <= 0x43) return pitRead(port);
	switch (port) {
	case 0x60: return kbdData;
	case 0x61: return ppiB;
	case 0x62: {
		u8 sw = switches();
		u8 v = (ppiB & 0x08) ? (sw >> 4) : (sw & 0x0F);
		if (pit[2].out) v |= 0x20;
		if (tape.present && !cfg.builtinBios && tape.motor) {
			u32 i = (u32)(tape.pos >> 16);
			if (i < tape.len && tape.data[i] >= 128) v |= 0x10;
		}
		return v;
	}
	case 0x63: return 0x99;
	case 0x81: return dma.page[2];
	case 0x82: return dma.page[3];
	case 0x83: return dma.page[1];
	case 0x87: return dma.page[0];
	}
	if (port >= 0x3F0 && port <= 0x3F7) return fdcRead(port);
	if (cfg.video == VID_CGA && port >= 0x3D0 && port <= 0x3DF) {
		if ((port & 0x0F) <= 7 && (port & 1)) return crtc.idx >= 12 && crtc.idx < 18 ? crtc.reg[crtc.idx] : 0xFF;
		if (port == 0x3DA) return videoStatus();
		return 0xFF;
	}
	if (cfg.video == VID_HERCULES && port >= 0x3B0 && port <= 0x3BF) {
		if (port <= 0x3B7 && (port & 1)) return crtc.idx >= 12 && crtc.idx < 18 ? crtc.reg[crtc.idx] : 0xFF;
		if (port == 0x3BA) return videoStatus();
		return 0xFF;
	}
	return 0xFF;
}

void Machine::out8(u16 port, u8 v)
{
	port &= 0x3FF;
	if (port < 0x10) { dmaWrite(port, v); return; }
	if (port == 0x20 || port == 0x21) { picWrite(port, v); return; }
	if (port >= 0x40 && port <= 0x43) { pitWrite(port, v); return; }
	switch (port) {
	case 0x61: {
		u8 old = ppiB;
		ppiB = v;
		if (v & 0x80) { kbdFull = false; kbdData = 0; }
		if ((v ^ old) & 0x01) pitSetGate(2, v & 1);
		if ((v & 0x40) && !(old & 0x40)) {
			// clock tastiera rilasciato dopo un reset: la tastiera risponde AAh
			if (kbdClockLowTicks > 8000) { kbdHead = kbdTail = 0; kbdPush(0xAA); }
			kbdClockLowTicks = 0;
		}
		if (!cfg.builtinBios) tape.motor = !(v & 0x08);
		return;
	}
	case 0x81: dma.page[2] = v & 0x0F; return;
	case 0x82: dma.page[3] = v & 0x0F; return;
	case 0x83: dma.page[1] = v & 0x0F; return;
	case 0x87: dma.page[0] = v & 0x0F; return;
	}
	if (port >= 0x3F0 && port <= 0x3F7) { fdcWrite(port, v); return; }
	if (cfg.video == VID_CGA && port >= 0x3D0 && port <= 0x3DF) {
		if (port <= 0x3D7) { if (port & 1) { if (crtc.idx < 18) crtc.reg[crtc.idx] = v; } else crtc.idx = v & 0x1F; }
		else if (port == 0x3D8) cgaMode = v;
		else if (port == 0x3D9) cgaColor = v;
		return;
	}
	if (cfg.video == VID_HERCULES && port >= 0x3B0 && port <= 0x3BF) {
		if (port <= 0x3B7) { if (port & 1) { if (crtc.idx < 18) crtc.reg[crtc.idx] = v; } else crtc.idx = v & 0x1F; }
		else if (port == 0x3B8) hercMode = v;
		else if (port == 0x3BF) hercConfig = v;
		return;
	}
}

// ------------------------------------------------------------------ PIC 8259
void Machine::picUpdate()
{
	u8 req = pic.irr & ~pic.imr;
	bool pend = false;
	for (int i = 0; i < 8; i++) {
		if (pic.isr & (1 << i)) break;      // priorita' piu' alta gia' in servizio
		if (req & (1 << i)) { pend = true; break; }
	}
	cpu.irqLine = pend;
}

void Machine::picRaise(int irq)
{
	pic.irr |= 1 << irq;
	picUpdate();
}

int Machine::irqAck()
{
	u8 req = pic.irr & ~pic.imr;
	for (int i = 0; i < 8; i++) {
		if (pic.isr & (1 << i)) break;
		if (req & (1 << i)) {
			pic.irr &= ~(1 << i);
			if (!pic.autoEoi) pic.isr |= 1 << i;
			picUpdate();
			return pic.vecBase + i;
		}
	}
	picUpdate();
	return -1;
}

void Machine::picWrite(int port, u8 v)
{
	if (port == 0x20) {
		if (v & 0x10) {                     // ICW1
			pic.icwStep = 2; pic.needIcw4 = v & 1;
			pic.imr = 0; pic.isr = 0; pic.irr = 0; pic.readIsr = false;
		} else if (v & 0x08) {              // OCW3
			if (v & 0x02) pic.readIsr = v & 1;
		} else {                            // OCW2
			int cmd = v >> 5;
			if (cmd == 1) {                 // EOI non specifico
				for (int i = 0; i < 8; i++) if (pic.isr & (1 << i)) { pic.isr &= ~(1 << i); break; }
			} else if (cmd == 3) pic.isr &= ~(1 << (v & 7));
		}
	} else {
		if (pic.icwStep == 2) { pic.vecBase = v & 0xF8; pic.icwStep = pic.needIcw4 ? 4 : 0; }
		else if (pic.icwStep == 3) { pic.icwStep = pic.needIcw4 ? 4 : 0; }
		else if (pic.icwStep == 4) { pic.autoEoi = (v & 2) != 0; pic.icwStep = 0; }
		else pic.imr = v;
	}
	picUpdate();
}

u8 Machine::picRead(int port)
{
	if (port == 0x21) return pic.imr;
	return pic.readIsr ? pic.isr : pic.irr;
}

// ------------------------------------------------------------------ PIT 8253
void Machine::pitSetGate(int ch, bool g)
{
	PitChan &p = pit[ch];
	if (g && !p.gate) {
		// fronte di salita: riavvia il conteggio (modi 1,2,3,5)
		if (p.mode != 0 && p.mode != 4) { p.count = p.reload; if (p.mode == 1 || p.mode == 5) p.out = p.mode == 5; else p.out = true; }
	}
	if (!g && (p.mode == 2 || p.mode == 3)) p.out = true;
	p.gate = g;
}

void Machine::pitWrite(int port, u8 v)
{
	if (port == 0x43) {
		int ch = v >> 6;
		if (ch == 3) return;
		PitChan &p = pit[ch];
		int rw = (v >> 4) & 3;
		if (rw == 0) { p.latched = true; p.latch = pitValue(ch); p.readHi = false; return; }
		p.rw = rw;
		p.mode = (v >> 1) & 7;
		if (p.mode > 5) p.mode -= 4;
		p.bcd = v & 1;
		p.writeHi = p.readHi = false;
		p.running = false;
		p.out = p.mode != 0;
		return;
	}
	int ch = port - 0x40;
	PitChan &p = pit[ch];
	u32 val;
	if (p.rw == 1) val = v;
	else if (p.rw == 2) val = v << 8;
	else {
		if (!p.writeHi) { p.wlo = v; p.writeHi = true; if (p.mode == 0) { p.running = false; p.out = false; } return; }
		p.writeHi = false;
		val = p.wlo | (v << 8);
	}
	if (p.bcd) { // conversione BCD semplificata
		val = (val & 0xF) + ((val >> 4) & 0xF) * 10 + ((val >> 8) & 0xF) * 100 + ((val >> 12) & 0xF) * 1000;
		if (!val) val = 10000;
	}
	p.reload = val ? val : 65536;
	p.count = p.reload;
	p.running = true;
	p.out = p.mode != 0;
	if (p.mode == 1 || p.mode == 5) p.out = true;
}

u16 Machine::pitValue(int ch)
{
	PitChan &p = pit[ch];
	s32 c = p.count;
	if (p.mode == 3) {
		s32 half = (s32)p.reload / 2;
		c = c > half ? (c - half) * 2 : c * 2;
	}
	return (u16)c;
}

u8 Machine::pitRead(int port)
{
	if (port == 0x43) return 0xFF;
	PitChan &p = pit[port - 0x40];
	u16 v = p.latched ? p.latch : pitValue(port - 0x40);
	u8 r;
	if (p.rw == 1) { r = (u8)v; p.latched = false; }
	else if (p.rw == 2) { r = v >> 8; p.latched = false; }
	else {
		if (!p.readHi) { r = (u8)v; p.readHi = true; }
		else { r = v >> 8; p.readHi = false; p.latched = false; }
	}
	return r;
}

int Machine::pitTick(int ch, int ticks)
{
	PitChan &p = pit[ch];
	if (!p.running) return 0;
	if (!p.gate && p.mode != 1 && p.mode != 5) return 0;
	int edge = 0;
	switch (p.mode) {
	case 0: case 1: case 4: case 5:
		p.count -= ticks;
		if (p.count <= 0) {
			if (!p.out || p.mode == 4 || p.mode == 5) edge = 1;
			p.out = true;
			while (p.count <= 0) p.count += 65536;
		} else if (p.mode == 1) p.out = false;
		break;
	case 2:
		p.count -= ticks;
		while (p.count <= 0) { p.count += p.reload; edge++; }
		p.out = true;
		break;
	case 3:
		p.count -= ticks;
		while (p.count <= 0) { p.count += p.reload; edge++; }
		p.out = (u32)p.count > p.reload / 2;
		break;
	}
	if (edge && ch == 0) picRaise(0);
	return edge;
}

// ------------------------------------------------------------------ PPI / tastiera
u8 Machine::switches() const
{
	u8 sw = 0x01 | 0x0C;                         // floppy presenti, RAM piena
	sw |= cfg.video == VID_HERCULES ? 0x30 : 0x20; // monocromatico / CGA 80 colonne
	sw |= 0x40;                                  // 2 unita' floppy
	return sw;
}

void Machine::kbdPush(u8 code)
{
	int n = (kbdHead + 1) & 63;
	if (n == kbdTail) return;
	kbdQueue[kbdHead] = code;
	kbdHead = n;
}

void Machine::kbdService()
{
	if (kbdFull || (ppiB & 0x80) || !(ppiB & 0x40)) return;
	if (kbdHead == kbdTail) return;
	kbdData = kbdQueue[kbdTail];
	kbdTail = (kbdTail + 1) & 63;
	kbdFull = true;
	picRaise(1);
}

void Machine::keyEvent(u8 code, bool down)
{
	kbdPush(down ? code : (code | 0x80));
}

// ------------------------------------------------------------------ DMA 8237
void Dma::reset()
{
	for (int i = 0; i < 4; i++) { baseAddr[i] = baseCount[i] = curAddr[i] = curCount[i] = 0; page[i] = 0; mode[i] = 0; }
	mask = 0x0F; status = 0; flip = false;
}

void Machine::dmaWrite(int port, u8 v)
{
	if (port < 8) {
		int ch = port >> 1;
		u16 *base = (port & 1) ? &dma.baseCount[ch] : &dma.baseAddr[ch];
		u16 *cur = (port & 1) ? &dma.curCount[ch] : &dma.curAddr[ch];
		if (!dma.flip) *base = (*base & 0xFF00) | v;
		else *base = (*base & 0x00FF) | (v << 8);
		*cur = *base;
		dma.flip = !dma.flip;
		return;
	}
	switch (port) {
	case 0x0A: if (v & 4) dma.mask |= 1 << (v & 3); else dma.mask &= ~(1 << (v & 3)); break;
	case 0x0B: dma.mode[v & 3] = v; break;
	case 0x0C: dma.flip = false; break;
	case 0x0D: dma.reset(); break;
	case 0x0E: dma.mask = 0; break;
	case 0x0F: dma.mask = v & 0x0F; break;
	}
}

u8 Machine::dmaRead(int port)
{
	if (port < 8) {
		int ch = port >> 1;
		u16 v = (port & 1) ? dma.curCount[ch] : dma.curAddr[ch];
		u8 r = dma.flip ? v >> 8 : (u8)v;
		dma.flip = !dma.flip;
		return r;
	}
	if (port == 0x08) { u8 s = dma.status; dma.status &= 0xF0; return s; }
	return 0xFF;
}

// trasferisce fino a len byte; ritorna i byte trasferiti (si ferma al terminal count)
int Machine::dmaTransfer(int ch, u8 *buf, int len, bool toMemory)
{
	if (dma.mask & (1 << ch)) return 0;
	int n = 0;
	bool dec = dma.mode[ch] & 0x20;
	int type = (dma.mode[ch] >> 2) & 3;   // 1 = scrittura in memoria, 2 = lettura dalla memoria
	while (n < len) {
		u32 a = ((u32)dma.page[ch] << 16) | dma.curAddr[ch];
		if (toMemory) { if (type == 1) cpu.wr8(a, buf[n]); }
		else buf[n] = type == 2 ? cpu.rd8(a) : 0;
		n++;
		dma.curAddr[ch] += dec ? -1 : 1;
		if (dma.curCount[ch]-- == 0) {
			dma.status |= 1 << ch;
			if (dma.mode[ch] & 0x10) { dma.curAddr[ch] = dma.baseAddr[ch]; dma.curCount[ch] = dma.baseCount[ch]; }
			else dma.mask |= 1 << ch;
			break;
		}
	}
	return n;
}

// ------------------------------------------------------------------ audio
void Machine::speakerTick(int ticks)
{
	static const int amp[3] = { 0, 2500, 7000 };
	bool on = (ppiB & 0x02) && pit[2].out;
	int level = on ? amp[cfg.volume < 0 ? 0 : cfg.volume > 2 ? 2 : cfg.volume] : 0;
	while (ticks > 0) {
		int need = (int)((PIT_HZ - sampleFrac + AUDIO_RATE - 1) / AUDIO_RATE);
		if (need < 1) need = 1;
		int t = need < ticks ? need : ticks;
		sampleAcc += level * t;
		sampleTicks += t;
		sampleFrac += t * AUDIO_RATE;
		ticks -= t;
		if (sampleFrac >= PIT_HZ) {
			sampleFrac -= PIT_HZ;
			s32 x = sampleTicks ? (s32)(sampleAcc / sampleTicks) : 0;
			// filtro passa-alto per eliminare la componente continua
			static s32 px = 0, py = 0;
			s32 y = x - px + (py * 252) / 256;
			px = x; py = y;
			if (y > 32767) y = 32767;
			if (y < -32768) y = -32768;
			u32 nw = (audioW + 1) % AUDIO_RING;
			if (nw != audioR) { audioRing[audioW] = (s16)y; audioW = nw; }
			sampleAcc = 0; sampleTicks = 0;
		}
	}
}

int Machine::audioRead(s16 *out, int n)
{
	static s16 last = 0;
	int got = 0;
	while (got < n && audioR != audioW) {
		last = audioRing[audioR];
		out[got++] = last;
		audioR = (audioR + 1) % AUDIO_RING;
	}
	int real = got;
	while (got < n) {
		last = (s16)(last * 31 / 32);
		out[got++] = last;
	}
	return real;
}

// ------------------------------------------------------------------ cassetta (livello hardware)
void Machine::tapeTick(int ticks)
{
	if (!tape.motor || cfg.builtinBios) return;
	tape.pos += ((u64)ticks * tape.rate << 16) / PIT_HZ;
	if ((tape.pos >> 16) > tape.len) tape.pos = (u64)tape.len << 16;
}
