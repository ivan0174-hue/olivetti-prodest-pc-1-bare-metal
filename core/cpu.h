// Prodest PC1 Emulator - CPU Intel 8088 / NEC V20-V40 (set istruzioni 80186 reale)
#pragma once
#include "platform.h"

enum { AX, CX, DX, BX, SP, BP, SI, DI };
enum { ES, CS, SS, DS };

enum {
	F_CF = 0x0001, F_PF = 0x0004, F_AF = 0x0010, F_ZF = 0x0040, F_SF = 0x0080,
	F_TF = 0x0100, F_IF = 0x0200, F_DF = 0x0400, F_OF = 0x0800
};

enum CpuType { CPU_8088 = 0, CPU_V20 = 1 };

// Interfaccia verso il resto della macchina
class CpuBus
{
public:
	virtual ~CpuBus() {}
	virtual u8   in8(u16 port) = 0;
	virtual void out8(u16 port, u8 v) = 0;
	virtual int  irqAck() = 0;            // ritorna il vettore da servire, -1 se nessuno
	virtual bool trap(u8 n) { (void)n; return false; } // trap BIOS integrato (opcode F1 nn in ROM)
};

class CPU
{
public:
	CPU();
	void reset();
	int  step();                 // esegue una istruzione, ritorna i cicli consumati
	void interrupt(u8 n);        // genera un interrupt software/hardware

	// Stato visibile
	u16 r[8];
	u16 s[4];
	u16 ip;
	u16 flags;

	int     type;                // CpuType
	CpuBus *bus;
	u8     *mem;                 // 1 MB lineare
	const u8 *wmap;              // pagine da 4K scrivibili (1) o ROM/non mappate (0)
	bool    halted;
	volatile bool irqLine;       // il PIC ha una richiesta pendente
	bool    hleTraps;            // abilita trap F1 nn in ROM (BIOS integrato)
	bool    repChunking;         // spezza i REP lunghi (per far servire gli interrupt)

	// accesso memoria pubblico (usato dal BIOS integrato)
	PRODEST_INLINE u8 rd8(u32 a) { return mem[a & 0xFFFFF]; }
	PRODEST_INLINE void wr8(u32 a, u8 v) { a &= 0xFFFFF; if (wmap[a >> 12]) mem[a] = v; }
	PRODEST_INLINE u8 rdS8(u16 seg, u16 off) { return rd8(((u32)seg << 4) + off); }
	PRODEST_INLINE u16 rdS16(u16 seg, u16 off) { return rdS8(seg, off) | (rdS8(seg, (u16)(off + 1)) << 8); }
	PRODEST_INLINE void wrS8(u16 seg, u16 off, u8 v) { wr8(((u32)seg << 4) + off, v); }
	PRODEST_INLINE void wrS16(u16 seg, u16 off, u16 v) { wrS8(seg, off, (u8)v); wrS8(seg, (u16)(off + 1), v >> 8); }

	void push(u16 v) { r[SP] -= 2; wrS16(s[SS], r[SP], v); }
	u16  pop() { u16 v = rdS16(s[SS], r[SP]); r[SP] += 2; return v; }

	u8 getR8(int i) const { return i < 4 ? (u8)r[i] : (u8)(r[i - 4] >> 8); }
	void setR8(int i, u8 v) { if (i < 4) r[i] = (r[i] & 0xFF00) | v; else r[i - 4] = (r[i - 4] & 0x00FF) | (v << 8); }

	void setFlag(u16 f, bool on) { if (on) flags |= f; else flags &= ~f; }
	bool getFlag(u16 f) const { return (flags & f) != 0; }
	u16  packedFlags() const { return (flags & 0x0FD5) | 0xF002; }

private:
	// decodifica
	u8   fetch8() { u8 v = rdS8(s[CS], ip); ip++; return v; }
	u16  fetch16() { u16 v = fetch8(); v |= fetch8() << 8; return v; }
	void decodeModRM();
	u8   rm8();
	u16  rm16();
	void setRm8(u8 v);
	void setRm16(u16 v);

	// ALU
	u8   alu8(int op, u8 a, u8 b);
	u16  alu16(int op, u16 a, u16 b);
	void szp8(u8 v);
	void szp16(u16 v);
	u8   inc8(u8 v); u8 dec8(u8 v);
	u16  inc16(u16 v); u16 dec16(u16 v);
	u8   shift8(int op, u8 v, int cnt);
	u16  shift16(int op, u16 v, int cnt);
	bool grp3_8(int op);
	bool grp3_16(int op);
	void stringOp(u8 op, int rep);
	bool cond(int c);

	int  exec(u8 op);

	// stato decodifica corrente
	int  segOvr;          // -1 nessuno
	int  modrm, mod_, reg_, rm_;
	u16  eaOff; u16 eaSeg;
	int  extraCycles;
	int  inhibitIrq;
	bool divError;
	u16  instStartIP;
	int  prefixCount;
	int  curRep;
};
