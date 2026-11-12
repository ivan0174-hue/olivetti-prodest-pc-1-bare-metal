// Prodest PC1 Emulator - macchina completa (scheda madre XT-compatibile del PC1)
#pragma once
#include "platform.h"
#include "cpu.h"

// ------------------------------------------------------------------ configurazione
enum CpuModel  { CPUM_V40_8MHZ = 0, CPUM_8088_477 = 1, CPUM_V40_TURBO = 2, CPUM_COUNT };
enum VideoCard { VID_CGA = 0, VID_HERCULES = 1, VID_COUNT };
enum Monitor   { MON_COLOR = 0, MON_GREEN = 1, MON_AMBER = 2, MON_MONO = 3, MON_COUNT };

struct Config {
	int  cpuModel;
	int  ramKB;            // 128, 256, 512, 640
	int  video;
	int  monitor;
	int  volume;           // 0 off, 1 basso, 2 alto
	bool builtinBios;      // true = BIOS integrato, false = file ROM
	char romFile[64];
	char floppyA[64];
	char floppyB[64];
	char tapeFile[64];
	void setDefaults();
};

// ------------------------------------------------------------------ dimensioni
static const int SCREEN_W = 720;   // framebuffer indicizzato 8 bit
static const int SCREEN_H = 400;
static const u32 DISK_MAX = 1474560 + 512;
static const u32 TAPE_MAX = 8 * 1024 * 1024;
static const int AUDIO_RATE = 48000;   // 48 kHz: va bene sia per PWM sia per HDMI
static const int AUDIO_RING = 8192;

struct Disk {
	u8   data[DISK_MAX];
	u32  size;
	int  cyls, heads, spt;
	bool present, dirty, writeProtect;
	bool insert(u32 bytes);     // imposta la geometria in base alla dimensione
	void eject() { present = false; dirty = false; size = 0; }
	int  lba(int c, int h, int sct) const { return (c * heads + h) * spt + (sct - 1); }
};

struct Tape {
	u8   data[TAPE_MAX];       // PCM 8 bit unsigned mono a 'rate' Hz
	u32  len;
	u32  rate;
	u64  pos;                  // posizione in campioni << 16
	bool present, dirty, motor;
	bool loadWav(const u8 *wav, u32 n);
	u32  saveWav(u8 *out, u32 max) const;
	void blank() { len = 0; rate = 44100; pos = 0; present = true; dirty = false; }
};

// ------------------------------------------------------------------ periferiche
struct Pic {
	u8 irr, isr, imr, vecBase, icwStep;
	bool needIcw4, readIsr, autoEoi;
	void reset() { irr = isr = 0; imr = 0xFF; vecBase = 8; icwStep = 0; needIcw4 = false; readIsr = false; autoEoi = false; }
};

struct PitChan {
	u32  reload;               // 1..65536
	s32  count;
	u8   mode, rw, ctrl;
	bool writeHi, readHi, latched, gate, out, running, bcd;
	u16  latch;
	u8   wlo;
};

struct Dma {
	u16 baseAddr[4], baseCount[4], curAddr[4], curCount[4];
	u8  page[4], mode[4];
	u8  mask, status;
	bool flip;
	void reset();
};

struct Fdc {
	u8   dor, msr, st0;
	u8   cmd[9]; int cmdLen, cmdPos;
	u8   res[7]; int resLen, resPos;
	int  cyl[4];
	int  pendingSis;           // interrupt da riportare con SENSE INTERRUPT
	u8   sisSt0[4]; int sisDrive[4];
	int  irqDelay;
	void reset();
};

struct Crtc { u8 idx; u8 reg[18]; };

// ------------------------------------------------------------------ macchina
class Machine : public CpuBus
{
public:
	Machine();
	void powerOn(const Config &c, const u8 *rom, u32 romSize); // reset completo
	void warmReset();                                            // Ctrl+Alt+Canc
	void applyCpuModel(int model);
	void runFrame();                                             // 1/60 di secondo
	void render(u8 *fb, int pitch);                              // 720x400 indicizzato
	void keyEvent(u8 xtScancode, bool down);                     // set 1 XT, 0xE0 escluso
	int  audioRead(s16 *out, int n);                             // campioni mono
	static void palette(int monitor, u32 rgb[32]);               // RGB 0xRRGGBB

	// CpuBus
	u8   in8(u16 port) override;
	void out8(u16 port, u8 v) override;
	int  irqAck() override;
	bool trap(u8 n) override;

	Config cfg;
	CPU    cpu;
	Disk   disk[2];
	Tape   tape;
	u32    frameCount;
	bool   romLoaded;

	u8 mem[0x100000];
	u8 wmap[256];

	// usati dal BIOS integrato
	void picRaise(int irq);
	void videoModeChanged() {}

private:
	// timing
	u32  cyclesPerFrame;
	u32  frameCycle;
	u32  pitAcc;
	void advance(int pitTicks);

	// PIC
	Pic  pic;
	void picUpdate();
	void picWrite(int port, u8 v);
	u8   picRead(int port);

	// PIT
	PitChan pit[3];
	void pitWrite(int port, u8 v);
	u8   pitRead(int port);
	int  pitTick(int ch, int ticks);
	u16  pitValue(int ch);
	void pitSetGate(int ch, bool g);

	// PPI / tastiera
	u8   ppiB, kbdData;
	bool kbdFull;
	u8   kbdQueue[64]; int kbdHead, kbdTail;
	int  kbdTimer, kbdClockLowTicks;
	void kbdPush(u8 code);
	void kbdService();
	u8   switches() const;

	// DMA
	Dma  dma;
	void dmaWrite(int port, u8 v);
	u8   dmaRead(int port);
public:
	int  dmaTransfer(int ch, u8 *buf, int len, bool toMemory);
private:

	// FDC
	Fdc  fdc;
	void fdcWrite(int port, u8 v);
	u8   fdcRead(int port);
	void fdcExecute();
	void fdcReadWrite(bool write);
	void fdcFormat();
	void fdcResult(int n, const u8 *r);
	void fdcRaiseIrq(int delay);

	// video
	Crtc  crtc;
	u8    cgaMode, cgaColor;
	u8    hercMode, hercConfig;
	u8    videoStatus();
	void  renderCga(u8 *fb, int pitch);
	void  renderHerc(u8 *fb, int pitch);

	// audio
	s16  audioRing[AUDIO_RING];
	volatile u32 audioW, audioR;
	u32  sampleAcc, sampleTicks, sampleFrac;
	void speakerTick(int ticks);

	// cassetta a basso livello (porta 62h bit 4)
	void tapeTick(int ticks);

	// BIOS integrato (hlebios.cpp)
	void biosPost();
	void biosInt08();
	u8   biosInt09();
	void biosInt0E();
	void biosInt10();
	void biosInt13();
	void biosInt15();
	void biosInt16();
	bool biosInt19();
	void biosInt1A();
	void biosInt18();
	// helper video
	void vSetMode(u8 mode);
	void vSetCursor(u8 page, u8 row, u8 col);
	void vUpdateCursorHw();
	void vScroll(bool up, u8 lines, u8 attr, u8 r1, u8 c1, u8 r2, u8 c2);
	void vWriteChar(u8 ch, u8 attr, u8 page, u8 row, u8 col, bool useAttr);
	void vTeletype(u8 ch, u8 color);
	void vPutPixel(u16 x, u16 y, u8 color);
	u8   vGetPixel(u16 x, u16 y);
	void vPrint(const char *s);
	void crtcWriteReg(u8 r, u8 v);
	bool kbdBufPut(u16 w);
	int  tapeReadBlock(u8 *buf, int n, int *status);
	void tapeWriteData(const u8 *buf, int n);

	u8   bda8(u16 o) { return mem[0x400 + o]; }
	u16  bda16(u16 o) { return mem[0x400 + o] | (mem[0x401 + o] << 8); }
	void sbda8(u16 o, u8 v) { mem[0x400 + o] = v; }
	void sbda16(u16 o, u16 v) { mem[0x400 + o] = (u8)v; mem[0x401 + o] = v >> 8; }
	void setCF(bool on) { cpu.setFlag(F_CF, on); }
	u16  vbase() const { return cfg.video == VID_HERCULES ? 0xB000 : 0xB800; }
};
