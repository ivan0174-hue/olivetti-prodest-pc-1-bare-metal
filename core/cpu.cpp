// Prodest PC1 Emulator - CPU Intel 8088 / NEC V20-V40
// Interprete completo delle istruzioni 8086/8088 (incluse le varianti non documentate
// principali) e delle estensioni 80186 presenti su NEC V20/V30/V40.
#include "cpu.h"

// Cicli base approssimati 8088 (forma registro). Gli accessi in memoria aggiungono extra.
static const u8 baseCycles[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	3, 3, 3, 3, 4, 4,14,12, 3, 3, 3, 3, 4, 4,14,12, // 0
	3, 3, 3, 3, 4, 4,14,12, 3, 3, 3, 3, 4, 4,14,12, // 1
	3, 3, 3, 3, 4, 4, 2, 4, 3, 3, 3, 3, 4, 4, 2, 4, // 2
	3, 3, 3, 3, 4, 4, 2, 8, 3, 3, 3, 3, 4, 4, 2, 8, // 3
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // 4
   15,15,15,15,15,15,15,15,12,12,12,12,12,12,12,12, // 5
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 6
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 7
	4, 4, 4, 4, 5, 5, 4, 4, 2, 2, 2, 2, 2, 2, 2,12, // 8
	3, 3, 3, 3, 3, 3, 3, 3, 2, 5,36, 4,14,12, 4, 4, // 9
   10,14,10,14,26,26,30,30, 4, 8,11,15,16,16,19,19, // A
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, // B
	4, 4,24,20,24,24, 4, 4, 4, 4,33,34,72,71, 4,44, // C
	2, 2, 8, 8,83,60, 4,11, 2, 2, 2, 2, 2, 2, 2, 2, // D
   17,18,17,18,14,14,14,14,23,15,15,15,12,12,12,12, // E
	2, 2, 9, 9, 2, 2, 5, 5, 2, 2, 2, 2, 2, 2, 3, 3, // F
};

static const u8 parityTable[256] = {
#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
	P6(1), P6(0), P6(0), P6(1)
#undef P2
#undef P4
#undef P6
};

CPU::CPU()
{
	type = CPU_8088;
	bus = 0; mem = 0; wmap = 0;
	hleTraps = false;
	repChunking = true;
	reset();
}

void CPU::reset()
{
	for (int i = 0; i < 8; i++) r[i] = 0;
	s[ES] = s[SS] = s[DS] = 0;
	s[CS] = 0xFFFF;
	ip = 0;
	flags = 0xF002;
	halted = false;
	irqLine = false;
	inhibitIrq = 0;
	divError = false;
	segOvr = -1;
}

void CPU::interrupt(u8 n)
{
	push(packedFlags());
	flags &= ~(F_IF | F_TF);
	push(s[CS]);
	push(ip);
	ip = rdS16(0, n * 4);
	s[CS] = rdS16(0, n * 4 + 2);
	halted = false;
}

// ---------------------------------------------------------------- flag helpers
void CPU::szp8(u8 v)
{
	flags &= ~(F_SF | F_ZF | F_PF);
	if (v & 0x80) flags |= F_SF;
	if (!v) flags |= F_ZF;
	if (parityTable[v]) flags |= F_PF;
}

void CPU::szp16(u16 v)
{
	flags &= ~(F_SF | F_ZF | F_PF);
	if (v & 0x8000) flags |= F_SF;
	if (!v) flags |= F_ZF;
	if (parityTable[v & 0xFF]) flags |= F_PF;
}

// op: 0 ADD 1 OR 2 ADC 3 SBB 4 AND 5 SUB 6 XOR 7 CMP
u8 CPU::alu8(int op, u8 a, u8 b)
{
	u32 res;
	u32 c = flags & F_CF;
	switch (op) {
	case 0: c = 0; /* fallthrough */
	case 2:
		res = a + b + c;
		flags &= ~(F_CF | F_OF | F_AF);
		if (res & 0x100) flags |= F_CF;
		if ((res ^ a) & (res ^ b) & 0x80) flags |= F_OF;
		if ((a ^ b ^ res) & 0x10) flags |= F_AF;
		szp8((u8)res);
		return (u8)res;
	case 5: case 7: c = 0; /* fallthrough */
	case 3:
		res = a - b - c;
		flags &= ~(F_CF | F_OF | F_AF);
		if (res & 0x100) flags |= F_CF;
		if ((a ^ b) & (a ^ res) & 0x80) flags |= F_OF;
		if ((a ^ b ^ res) & 0x10) flags |= F_AF;
		szp8((u8)res);
		return op == 7 ? a : (u8)res;
	case 1: res = a | b; break;
	case 4: res = a & b; break;
	default: res = a ^ b; break;
	}
	flags &= ~(F_CF | F_OF | F_AF);
	szp8((u8)res);
	return (u8)res;
}

u16 CPU::alu16(int op, u16 a, u16 b)
{
	u32 res;
	u32 c = flags & F_CF;
	switch (op) {
	case 0: c = 0; /* fallthrough */
	case 2:
		res = a + b + c;
		flags &= ~(F_CF | F_OF | F_AF);
		if (res & 0x10000) flags |= F_CF;
		if ((res ^ a) & (res ^ b) & 0x8000) flags |= F_OF;
		if ((a ^ b ^ res) & 0x10) flags |= F_AF;
		szp16((u16)res);
		return (u16)res;
	case 5: case 7: c = 0; /* fallthrough */
	case 3:
		res = a - b - c;
		flags &= ~(F_CF | F_OF | F_AF);
		if (res & 0x10000) flags |= F_CF;
		if ((a ^ b) & (a ^ res) & 0x8000) flags |= F_OF;
		if ((a ^ b ^ res) & 0x10) flags |= F_AF;
		szp16((u16)res);
		return op == 7 ? a : (u16)res;
	case 1: res = a | b; break;
	case 4: res = a & b; break;
	default: res = a ^ b; break;
	}
	flags &= ~(F_CF | F_OF | F_AF);
	szp16((u16)res);
	return (u16)res;
}

u8 CPU::inc8(u8 v)
{
	u8 r8 = v + 1;
	flags &= ~(F_OF | F_AF);
	if (r8 == 0x80) flags |= F_OF;
	if ((v ^ 1 ^ r8) & 0x10) flags |= F_AF;
	szp8(r8);
	return r8;
}
u8 CPU::dec8(u8 v)
{
	u8 r8 = v - 1;
	flags &= ~(F_OF | F_AF);
	if (r8 == 0x7F) flags |= F_OF;
	if ((v ^ 1 ^ r8) & 0x10) flags |= F_AF;
	szp8(r8);
	return r8;
}
u16 CPU::inc16(u16 v)
{
	u16 r16 = v + 1;
	flags &= ~(F_OF | F_AF);
	if (r16 == 0x8000) flags |= F_OF;
	if ((v ^ 1 ^ r16) & 0x10) flags |= F_AF;
	szp16(r16);
	return r16;
}
u16 CPU::dec16(u16 v)
{
	u16 r16 = v - 1;
	flags &= ~(F_OF | F_AF);
	if (r16 == 0x7FFF) flags |= F_OF;
	if ((v ^ 1 ^ r16) & 0x10) flags |= F_AF;
	szp16(r16);
	return r16;
}

// op: 0 ROL 1 ROR 2 RCL 3 RCR 4 SHL 5 SHR 6 SETMO(8088)/SHL 7 SAR
u8 CPU::shift8(int op, u8 v, int cnt)
{
	if (type == CPU_V20) cnt &= 0x1F;
	if (cnt == 0) return v;
	u8 res = v;
	extraCycles += cnt * 4;
	switch (op) {
	case 0:
		for (int i = 0; i < cnt; i++) res = (res << 1) | (res >> 7);
		setFlag(F_CF, res & 1);
		setFlag(F_OF, ((res >> 7) ^ res) & 1);
		break;
	case 1:
		for (int i = 0; i < cnt; i++) res = (res >> 1) | (res << 7);
		setFlag(F_CF, res & 0x80);
		setFlag(F_OF, ((res >> 7) ^ (res >> 6)) & 1);
		break;
	case 2:
		for (int i = 0; i < cnt; i++) {
			u8 c = flags & F_CF;
			setFlag(F_CF, res & 0x80);
			res = (res << 1) | c;
		}
		setFlag(F_OF, ((res >> 7) ^ (flags & F_CF)) & 1);
		break;
	case 3:
		for (int i = 0; i < cnt; i++) {
			u8 c = flags & F_CF;
			setFlag(F_CF, res & 1);
			res = (res >> 1) | (c << 7);
		}
		setFlag(F_OF, ((res >> 7) ^ (res >> 6)) & 1);
		break;
	case 4: case 6:
		if (op == 6 && type == CPU_8088) {      // SETMO non documentata
			res = 0xFF;
			flags &= ~(F_CF | F_OF | F_AF);
			szp8(res);
			return res;
		}
		{
			u32 t = (u32)v << (cnt > 9 ? 9 : cnt);
			if (cnt > 8) t = 0;
			res = (u8)t;
			setFlag(F_CF, cnt <= 8 ? (((u32)v << cnt) & 0x100) != 0 : false);
			setFlag(F_OF, ((res >> 7) ^ (flags & F_CF)) & 1);
			flags &= ~F_AF;
			szp8(res);
		}
		break;
	case 5:
		{
			setFlag(F_CF, cnt <= 8 ? ((v >> (cnt - 1)) & 1) : false);
			res = cnt >= 8 ? 0 : (u8)(v >> cnt);
			setFlag(F_OF, cnt == 1 ? (v & 0x80) != 0 : false);
			flags &= ~F_AF;
			szp8(res);
		}
		break;
	case 7:
		{
			s8 sv = (s8)v;
			int c = cnt > 8 ? 8 : cnt;
			setFlag(F_CF, ((sv >> (c - 1)) & 1));
			res = (u8)(sv >> (c > 7 ? 7 : c));
			flags &= ~(F_OF | F_AF);
			szp8(res);
		}
		break;
	}
	return res;
}

u16 CPU::shift16(int op, u16 v, int cnt)
{
	if (type == CPU_V20) cnt &= 0x1F;
	if (cnt == 0) return v;
	u16 res = v;
	extraCycles += cnt * 4;
	switch (op) {
	case 0:
		for (int i = 0; i < cnt; i++) res = (res << 1) | (res >> 15);
		setFlag(F_CF, res & 1);
		setFlag(F_OF, ((res >> 15) ^ res) & 1);
		break;
	case 1:
		for (int i = 0; i < cnt; i++) res = (res >> 1) | (res << 15);
		setFlag(F_CF, res & 0x8000);
		setFlag(F_OF, ((res >> 15) ^ (res >> 14)) & 1);
		break;
	case 2:
		for (int i = 0; i < cnt; i++) {
			u16 c = flags & F_CF;
			setFlag(F_CF, res & 0x8000);
			res = (res << 1) | c;
		}
		setFlag(F_OF, ((res >> 15) ^ (flags & F_CF)) & 1);
		break;
	case 3:
		for (int i = 0; i < cnt; i++) {
			u16 c = flags & F_CF;
			setFlag(F_CF, res & 1);
			res = (res >> 1) | (c << 15);
		}
		setFlag(F_OF, ((res >> 15) ^ (res >> 14)) & 1);
		break;
	case 4: case 6:
		if (op == 6 && type == CPU_8088) {
			res = 0xFFFF;
			flags &= ~(F_CF | F_OF | F_AF);
			szp16(res);
			return res;
		}
		{
			u64 t = cnt > 16 ? 0 : ((u64)v << cnt);
			res = (u16)t;
			setFlag(F_CF, (t & 0x10000) != 0);
			setFlag(F_OF, ((res >> 15) ^ (flags & F_CF)) & 1);
			flags &= ~F_AF;
			szp16(res);
		}
		break;
	case 5:
		setFlag(F_CF, cnt <= 16 ? ((v >> (cnt - 1)) & 1) : false);
		res = cnt >= 16 ? 0 : (u16)(v >> cnt);
		setFlag(F_OF, cnt == 1 ? (v & 0x8000) != 0 : false);
		flags &= ~F_AF;
		szp16(res);
		break;
	case 7:
		{
			s16 sv = (s16)v;
			int c = cnt > 16 ? 16 : cnt;
			setFlag(F_CF, ((sv >> (c - 1)) & 1));
			res = (u16)(sv >> (c > 15 ? 15 : c));
			flags &= ~(F_OF | F_AF);
			szp16(res);
		}
		break;
	}
	return res;
}

// ---------------------------------------------------------------- ModR/M
void CPU::decodeModRM()
{
	modrm = fetch8();
	mod_ = modrm >> 6;
	reg_ = (modrm >> 3) & 7;
	rm_ = modrm & 7;
	if (mod_ == 3) return;
	u16 off = 0;
	int defSeg = DS;
	switch (rm_) {
	case 0: off = r[BX] + r[SI]; extraCycles += 7; break;
	case 1: off = r[BX] + r[DI]; extraCycles += 8; break;
	case 2: off = r[BP] + r[SI]; defSeg = SS; extraCycles += 8; break;
	case 3: off = r[BP] + r[DI]; defSeg = SS; extraCycles += 7; break;
	case 4: off = r[SI]; extraCycles += 5; break;
	case 5: off = r[DI]; extraCycles += 5; break;
	case 6:
		if (mod_ == 0) { off = fetch16(); extraCycles += 6; }
		else { off = r[BP]; defSeg = SS; extraCycles += 5; }
		break;
	case 7: off = r[BX]; extraCycles += 5; break;
	}
	if (mod_ == 1) { off += (s16)(s8)fetch8(); extraCycles += 4; }
	else if (mod_ == 2) { off += fetch16(); extraCycles += 4; }
	eaOff = off;
	eaSeg = s[segOvr >= 0 ? segOvr : defSeg];
	extraCycles += 4;
}

u8 CPU::rm8() { return mod_ == 3 ? getR8(rm_) : rdS8(eaSeg, eaOff); }
u16 CPU::rm16() { return mod_ == 3 ? r[rm_] : rdS16(eaSeg, eaOff); }
void CPU::setRm8(u8 v) { if (mod_ == 3) setR8(rm_, v); else wrS8(eaSeg, eaOff, v); }
void CPU::setRm16(u16 v) { if (mod_ == 3) r[rm_] = v; else wrS16(eaSeg, eaOff, v); }

bool CPU::cond(int c)
{
	bool res;
	switch (c >> 1) {
	case 0: res = flags & F_OF; break;
	case 1: res = flags & F_CF; break;
	case 2: res = flags & F_ZF; break;
	case 3: res = (flags & (F_CF | F_ZF)) != 0; break;
	case 4: res = flags & F_SF; break;
	case 5: res = flags & F_PF; break;
	case 6: res = ((flags & F_SF) != 0) != ((flags & F_OF) != 0); break;
	default: res = (flags & F_ZF) || (((flags & F_SF) != 0) != ((flags & F_OF) != 0)); break;
	}
	return (c & 1) ? !res : res;
}

// ---------------------------------------------------------------- gruppo 3 (F6/F7)
bool CPU::grp3_8(int op)
{
	u8 v = rm8();
	switch (op) {
	case 0: case 1: alu8(4, v, fetch8()); break;
	case 2: setRm8(~v); break;
	case 3: { u8 res = alu8(5, 0, v); setRm8(res); setFlag(F_CF, v != 0); } break;
	case 4: {
		u16 res = (u16)(r[AX] & 0xFF) * v;
		r[AX] = res;
		bool hi = (res & 0xFF00) != 0;
		setFlag(F_CF, hi); setFlag(F_OF, hi);
		szp8((u8)(res >> 8));
		flags &= ~F_ZF; if (!res) flags |= F_ZF;  // ZF: 8088 dipende dal risultato
		extraCycles += 66;
	} break;
	case 5: {
		s16 res = (s16)(s8)(r[AX] & 0xFF) * (s16)(s8)v;
		r[AX] = (u16)res;
		bool ext = res != (s16)(s8)(res & 0xFF);
		setFlag(F_CF, ext); setFlag(F_OF, ext);
		szp8((u8)(res >> 8));
		extraCycles += 76;
	} break;
	case 6: {
		if (v == 0) return false;
		u16 q = r[AX] / v;
		if (q > 0xFF) return false;
		u8 rem = r[AX] % v;
		r[AX] = (rem << 8) | (u8)q;
		extraCycles += 76;
	} break;
	case 7: {
		if (v == 0) return false;
		s16 a = (s16)r[AX];
		s16 d = (s8)v;
		s32 q = a / d;
		s32 rem = a % d;
		if (curRep && type == CPU_8088) q = -q;
		if (q > 127 || q < -127) return false;
		r[AX] = ((u8)rem << 8) | (u8)q;
		extraCycles += 97;
	} break;
	}
	return true;
}

bool CPU::grp3_16(int op)
{
	u16 v = rm16();
	switch (op) {
	case 0: case 1: alu16(4, v, fetch16()); break;
	case 2: setRm16(~v); break;
	case 3: { u16 res = alu16(5, 0, v); setRm16(res); setFlag(F_CF, v != 0); } break;
	case 4: {
		u32 res = (u32)r[AX] * v;
		r[AX] = (u16)res; r[DX] = res >> 16;
		bool hi = r[DX] != 0;
		setFlag(F_CF, hi); setFlag(F_OF, hi);
		szp16(r[DX]);
		extraCycles += 120;
	} break;
	case 5: {
		s32 res = (s32)(s16)r[AX] * (s32)(s16)v;
		r[AX] = (u16)res; r[DX] = (u16)((u32)res >> 16);
		bool ext = res != (s32)(s16)(res & 0xFFFF);
		setFlag(F_CF, ext); setFlag(F_OF, ext);
		szp16(r[DX]);
		extraCycles += 130;
	} break;
	case 6: {
		if (v == 0) return false;
		u32 a = ((u32)r[DX] << 16) | r[AX];
		u32 q = a / v;
		if (q > 0xFFFF) return false;
		r[DX] = a % v;
		r[AX] = (u16)q;
		extraCycles += 140;
	} break;
	case 7: {
		if (v == 0) return false;
		s32 a = (s32)(((u32)r[DX] << 16) | r[AX]);
		s32 d = (s16)v;
		if (a == (s32)0x80000000 && d == -1) return false;
		s32 q = a / d;
		s32 rem = a % d;
		if (curRep && type == CPU_8088) q = -q;
		if (q > 32767 || q < -32767) return false;
		r[AX] = (u16)q; r[DX] = (u16)rem;
		extraCycles += 160;
	} break;
	}
	return true;
}

// ---------------------------------------------------------------- stringhe
// rep: 0 nessuno, 2 = REPNE (F2), 3 = REP/REPE (F3)
void CPU::stringOp(u8 op, int rep)
{
	int srcSeg = segOvr >= 0 ? segOvr : DS;
	int delta = (flags & F_DF) ? -1 : 1;
	bool word = op & 1;
	int sz = word ? 2 : 1;
	int iter = 0;
	if (rep && r[CX] == 0) return;
	for (;;) {
		switch (op) {
		case 0xA4: wrS8(s[ES], r[DI], rdS8(s[srcSeg], r[SI])); r[SI] += delta; r[DI] += delta; break;
		case 0xA5: wrS16(s[ES], r[DI], rdS16(s[srcSeg], r[SI])); r[SI] += 2 * delta; r[DI] += 2 * delta; break;
		case 0xA6: alu8(7, rdS8(s[srcSeg], r[SI]), rdS8(s[ES], r[DI])); r[SI] += delta; r[DI] += delta; break;
		case 0xA7: alu16(7, rdS16(s[srcSeg], r[SI]), rdS16(s[ES], r[DI])); r[SI] += 2 * delta; r[DI] += 2 * delta; break;
		case 0xAA: wrS8(s[ES], r[DI], (u8)r[AX]); r[DI] += delta; break;
		case 0xAB: wrS16(s[ES], r[DI], r[AX]); r[DI] += 2 * delta; break;
		case 0xAC: setR8(0, rdS8(s[srcSeg], r[SI])); r[SI] += delta; break;
		case 0xAD: r[AX] = rdS16(s[srcSeg], r[SI]); r[SI] += 2 * delta; break;
		case 0xAE: alu8(7, (u8)r[AX], rdS8(s[ES], r[DI])); r[DI] += delta; break;
		case 0xAF: alu16(7, r[AX], rdS16(s[ES], r[DI])); r[DI] += 2 * delta; break;
		case 0x6C: wrS8(s[ES], r[DI], bus->in8(r[DX])); r[DI] += delta; break;
		case 0x6D: { u16 v = bus->in8(r[DX]); v |= bus->in8(r[DX] + 1) << 8; wrS16(s[ES], r[DI], v); r[DI] += 2 * delta; } break;
		case 0x6E: bus->out8(r[DX], rdS8(s[srcSeg], r[SI])); r[SI] += delta; break;
		case 0x6F: { u16 v = rdS16(s[srcSeg], r[SI]); bus->out8(r[DX], (u8)v); bus->out8(r[DX] + 1, v >> 8); r[SI] += 2 * delta; } break;
		}
		(void)sz;
		extraCycles += 17;
		if (!rep) return;
		r[CX]--;
		if (r[CX] == 0) return;
		if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
			bool z = (flags & F_ZF) != 0;
			if (rep == 3 && !z) return;
			if (rep == 2 && z) return;
		}
		if (repChunking && ++iter >= 32) {
			// riprende l'istruzione dopo aver dato la possibilita' di servire gli interrupt
			ip = instStartIP;
			return;
		}
	}
}

// ---------------------------------------------------------------- step
int CPU::step()
{
	if (inhibitIrq) inhibitIrq--;
	else if (irqLine && (flags & F_IF)) {
		int v = bus->irqAck();
		if (v >= 0) {
			halted = false;
			interrupt((u8)v);
			return 61;
		}
	}
	if (halted) return 4;

	bool trapFlag = (flags & F_TF) != 0;
	instStartIP = ip;
	segOvr = -1;
	extraCycles = 0;
	divError = false;
	int rep = 0;
	curRep = 0;
	u8 op;
	int cyc = 0;
	for (int n = 0; ; n++) {
		op = fetch8();
		if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) { segOvr = (op >> 3) & 3; cyc += 2; continue; }
		if (op == 0xF2 || op == 0xF3) { rep = op & 3; curRep = rep; cyc += 2; continue; }
		if (op == 0xF0) { cyc += 2; continue; }
		if (op == 0xF1) {
			u32 lin = (((u32)s[CS] << 4) + (u16)(ip - 1)) & 0xFFFFF;
			if (hleTraps && lin >= 0xF0000) {
				u8 nTrap = fetch8();
				bus->trap(nTrap);
				return 20;
			}
			cyc += 2;
			continue;   // alias di LOCK
		}
		if (n > 14) break;
		break;
	}
	prefixCount = 0;
	if ((op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) ||
	    (type == CPU_V20 && op >= 0x6C && op <= 0x6F)) {
		cyc += baseCycles[op];
		stringOp(op, rep);
	} else {
		cyc += exec(op);
	}
	cyc += extraCycles;
	if (divError) interrupt(0);
	else if (trapFlag && (flags & F_TF) && !inhibitIrq) interrupt(1);
	return cyc;
}

// ---------------------------------------------------------------- esecuzione
int CPU::exec(u8 op)
{
	int cyc = baseCycles[op];
	switch (op) {
	// ALU classiche 00-3F
	case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
		decodeModRM(); { u8 res = alu8(op >> 3, rm8(), getR8(reg_)); if ((op >> 3) != 7) setRm8(res); } break;
	case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31: case 0x39:
		decodeModRM(); { u16 res = alu16(op >> 3, rm16(), r[reg_]); if ((op >> 3) != 7) setRm16(res); } break;
	case 0x02: case 0x0A: case 0x12: case 0x1A: case 0x22: case 0x2A: case 0x32: case 0x3A:
		decodeModRM(); { u8 res = alu8(op >> 3, getR8(reg_), rm8()); if ((op >> 3) != 7) setR8(reg_, res); } break;
	case 0x03: case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: case 0x33: case 0x3B:
		decodeModRM(); { u16 res = alu16(op >> 3, r[reg_], rm16()); if ((op >> 3) != 7) r[reg_] = res; } break;
	case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
		{ u8 res = alu8(op >> 3, (u8)r[AX], fetch8()); if ((op >> 3) != 7) setR8(0, res); } break;
	case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:
		{ u16 res = alu16(op >> 3, r[AX], fetch16()); if ((op >> 3) != 7) r[AX] = res; } break;

	case 0x06: case 0x0E: case 0x16: case 0x1E: push(s[(op >> 3) & 3]); break;
	case 0x07: case 0x17: case 0x1F: s[(op >> 3) & 3] = pop(); if (op == 0x17) inhibitIrq = 1; break;
	case 0x0F:
		if (type == CPU_8088) { s[CS] = pop(); }
		else { fetch8(); } // estensioni V20 non supportate: ignora
		break;

	case 0x27: { // DAA
		u8 al = (u8)r[AX]; bool cf = flags & F_CF; bool af = flags & F_AF;
		u8 old = al;
		if ((al & 0x0F) > 9 || af) { al += 6; flags |= F_AF; } else flags &= ~F_AF;
		if (old > (af ? 0x9F : 0x99) || cf) { al += 0x60; flags |= F_CF; } else flags &= ~F_CF;
		setR8(0, al); szp8(al);
		setFlag(F_OF, ((old ^ al) & al & 0x80) != 0);
	} break;
	case 0x2F: { // DAS
		u8 al = (u8)r[AX]; bool cf = flags & F_CF; bool af = flags & F_AF;
		u8 old = al;
		if ((al & 0x0F) > 9 || af) { al -= 6; flags |= F_AF; } else flags &= ~F_AF;
		if (old > (af ? 0x9F : 0x99) || cf) { al -= 0x60; flags |= F_CF; } else flags &= ~F_CF;
		setR8(0, al); szp8(al);
		setFlag(F_OF, ((old ^ al) & old & 0x80) != 0);
	} break;
	case 0x37: { // AAA
		u8 al = (u8)r[AX];
		if ((al & 0x0F) > 9 || (flags & F_AF)) {
			r[AX] = (u16)((((r[AX] >> 8) + 1) << 8) | ((r[AX] + 6) & 0xFF));
			flags |= F_AF | F_CF;
		} else flags &= ~(F_AF | F_CF);
		szp8((u8)r[AX]);
		r[AX] &= 0xFF0F;
	} break;
	case 0x3F: { // AAS
		u8 al = (u8)r[AX];
		if ((al & 0x0F) > 9 || (flags & F_AF)) {
			r[AX] = (u16)((((r[AX] >> 8) - 1) << 8) | ((r[AX] - 6) & 0xFF));
			flags |= F_AF | F_CF;
		} else flags &= ~(F_AF | F_CF);
		szp8((u8)r[AX]);
		r[AX] &= 0xFF0F;
	} break;

	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
		r[op & 7] = inc16(r[op & 7]); break;
	case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
		r[op & 7] = dec16(r[op & 7]); break;
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
		push(r[op & 7]); break;
	case 0x54:
		if (type == CPU_8088) { r[SP] -= 2; wrS16(s[SS], r[SP], r[SP]); }
		else push(r[SP]);
		break;
	case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		r[op & 7] = pop(); break;

	case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
	case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
		if (type == CPU_8088) {
			s8 d = (s8)fetch8();
			if (cond(op & 0x0F)) { ip += d; cyc += 8; }
			break;
		}
		switch (op) {
		case 0x60: { u16 sp = r[SP]; for (int i = 0; i < 8; i++) push(i == SP ? sp : r[i]); cyc = 36; } break;
		case 0x61: { for (int i = 7; i >= 0; i--) { u16 v = pop(); if (i != SP) r[i] = v; } cyc = 51; } break;
		case 0x62: { // BOUND
			decodeModRM();
			s16 lo = (s16)rdS16(eaSeg, eaOff), hi = (s16)rdS16(eaSeg, (u16)(eaOff + 2));
			s16 v = (s16)r[reg_];
			if (v < lo || v > hi) { ip = instStartIP; interrupt(5); }
		} break;
		case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: break;
		case 0x68: push(fetch16()); break;
		case 0x69: case 0x6B: {
			decodeModRM();
			s32 a = (s16)rm16();
			s32 b = op == 0x69 ? (s16)fetch16() : (s8)fetch8();
			s32 res = a * b;
			r[reg_] = (u16)res;
			bool ext = res != (s32)(s16)res;
			setFlag(F_CF, ext); setFlag(F_OF, ext);
			szp16((u16)res);
			cyc = 30;
		} break;
		case 0x6A: push((u16)(s16)(s8)fetch8()); break;
		default: break; // INS/OUTS gestite in step()
		}
		break;

	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		s8 d = (s8)fetch8();
		if (cond(op & 0x0F)) { ip += d; cyc += 8; }
	} break;

	case 0x80: case 0x82: { decodeModRM(); u8 res = alu8(reg_, rm8(), fetch8()); if (reg_ != 7) setRm8(res); } break;
	case 0x81: { decodeModRM(); u16 res = alu16(reg_, rm16(), fetch16()); if (reg_ != 7) setRm16(res); } break;
	case 0x83: { decodeModRM(); u16 res = alu16(reg_, rm16(), (u16)(s16)(s8)fetch8()); if (reg_ != 7) setRm16(res); } break;
	case 0x84: decodeModRM(); alu8(4, rm8(), getR8(reg_)); break;
	case 0x85: decodeModRM(); alu16(4, rm16(), r[reg_]); break;
	case 0x86: { decodeModRM(); u8 a = rm8(); setRm8(getR8(reg_)); setR8(reg_, a); } break;
	case 0x87: { decodeModRM(); u16 a = rm16(); setRm16(r[reg_]); r[reg_] = a; } break;
	case 0x88: decodeModRM(); setRm8(getR8(reg_)); break;
	case 0x89: decodeModRM(); setRm16(r[reg_]); break;
	case 0x8A: decodeModRM(); setR8(reg_, rm8()); break;
	case 0x8B: decodeModRM(); r[reg_] = rm16(); break;
	case 0x8C: decodeModRM(); setRm16(s[reg_ & 3]); break;
	case 0x8D: decodeModRM(); if (mod_ != 3) r[reg_] = eaOff; break;
	case 0x8E: decodeModRM(); s[reg_ & 3] = rm16(); if ((reg_ & 3) == SS) inhibitIrq = 1; break;
	case 0x8F: { decodeModRM(); u16 v = pop(); setRm16(v); } break;

	case 0x90: break;
	case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
		{ u16 t = r[AX]; r[AX] = r[op & 7]; r[op & 7] = t; } break;
	case 0x98: r[AX] = (u16)(s16)(s8)(r[AX] & 0xFF); break;
	case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xFFFF : 0; break;
	case 0x9A: { u16 o = fetch16(); u16 sg = fetch16(); push(s[CS]); push(ip); s[CS] = sg; ip = o; } break;
	case 0x9B: break;
	case 0x9C: push(packedFlags()); break;
	case 0x9D: flags = (pop() & 0x0FD5) | 0xF002; break;
	case 0x9E: flags = (flags & 0xFF00) | ((r[AX] >> 8) & 0xD5) | 0x02; break;
	case 0x9F: setR8(4, (u8)packedFlags()); break;

	case 0xA0: { u16 o = fetch16(); setR8(0, rdS8(s[segOvr >= 0 ? segOvr : DS], o)); } break;
	case 0xA1: { u16 o = fetch16(); r[AX] = rdS16(s[segOvr >= 0 ? segOvr : DS], o); } break;
	case 0xA2: { u16 o = fetch16(); wrS8(s[segOvr >= 0 ? segOvr : DS], o, (u8)r[AX]); } break;
	case 0xA3: { u16 o = fetch16(); wrS16(s[segOvr >= 0 ? segOvr : DS], o, r[AX]); } break;
	case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
		// eseguite qui solo se senza REP; con REP le gestisce step()
		// (step() chiama stringOp dopo exec se c'e' un prefisso REP)
		break;
	case 0xA8: alu8(4, (u8)r[AX], fetch8()); break;
	case 0xA9: alu16(4, r[AX], fetch16()); break;

	case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		setR8(op & 7, fetch8()); break;
	case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		r[op & 7] = fetch16(); break;

	case 0xC0: case 0xC1:
		if (type == CPU_8088) { // alias di C2/C3
			if (op == 0xC0) { u16 n = fetch16(); ip = pop(); r[SP] += n; } else ip = pop();
			break;
		}
		decodeModRM();
		if (op == 0xC0) { u8 v = rm8(); int c = fetch8(); setRm8(shift8(reg_, v, c)); }
		else { u16 v = rm16(); int c = fetch8(); setRm16(shift16(reg_, v, c)); }
		break;
	case 0xC2: { u16 n = fetch16(); ip = pop(); r[SP] += n; } break;
	case 0xC3: ip = pop(); break;
	case 0xC4: decodeModRM(); r[reg_] = rdS16(eaSeg, eaOff); s[ES] = rdS16(eaSeg, (u16)(eaOff + 2)); break;
	case 0xC5: decodeModRM(); r[reg_] = rdS16(eaSeg, eaOff); s[DS] = rdS16(eaSeg, (u16)(eaOff + 2)); break;
	case 0xC6: decodeModRM(); setRm8(fetch8()); break;
	case 0xC7: decodeModRM(); setRm16(fetch16()); break;
	case 0xC8: case 0xC9:
		if (type == CPU_8088) { // alias di CA/CB
			if (op == 0xC8) { u16 n = fetch16(); ip = pop(); s[CS] = pop(); r[SP] += n; }
			else { ip = pop(); s[CS] = pop(); }
			break;
		}
		if (op == 0xC8) { // ENTER
			u16 size = fetch16(); u8 level = fetch8() & 0x1F;
			push(r[BP]);
			u16 frame = r[SP];
			for (int i = 1; i < level; i++) { r[BP] -= 2; push(rdS16(s[SS], r[BP])); }
			if (level) push(frame);
			r[BP] = frame;
			r[SP] -= size;
		} else { r[SP] = r[BP]; r[BP] = pop(); } // LEAVE
		break;
	case 0xCA: { u16 n = fetch16(); ip = pop(); s[CS] = pop(); r[SP] += n; } break;
	case 0xCB: ip = pop(); s[CS] = pop(); break;
	case 0xCC: interrupt(3); break;
	case 0xCD: { u8 n = fetch8(); interrupt(n); } break;
	case 0xCE: if (flags & F_OF) { interrupt(4); cyc += 50; } break;
	case 0xCF: ip = pop(); s[CS] = pop(); flags = (pop() & 0x0FD5) | 0xF002; break;

	case 0xD0: decodeModRM(); setRm8(shift8(reg_, rm8(), 1)); break;
	case 0xD1: decodeModRM(); setRm16(shift16(reg_, rm16(), 1)); break;
	case 0xD2: decodeModRM(); setRm8(shift8(reg_, rm8(), r[CX] & 0xFF)); break;
	case 0xD3: decodeModRM(); setRm16(shift16(reg_, rm16(), r[CX] & 0xFF)); break;
	case 0xD4: { // AAM
		u8 base = fetch8();
		if (type == CPU_V20) base = 10;   // i NEC V20/V30/V40 ignorano l'operando
		if (base == 0) { divError = true; break; }
		u8 al = (u8)r[AX];
		r[AX] = ((al / base) << 8) | (al % base);
		szp8((u8)r[AX]);
		flags &= ~(F_CF | F_OF | F_AF);
	} break;
	case 0xD5: { // AAD
		u8 base = fetch8();
		if (type == CPU_V20) base = 10;
		u8 al = (u8)r[AX], ah = r[AX] >> 8;
		u8 res = alu8(0, al, (u8)(ah * base));
		r[AX] = res;
	} break;
	case 0xD6: setR8(0, (flags & F_CF) ? 0xFF : 0); break; // SALC
	case 0xD7: setR8(0, rdS8(s[segOvr >= 0 ? segOvr : DS], (u16)(r[BX] + (r[AX] & 0xFF)))); break;
	case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
		decodeModRM(); break; // ESC: nessun coprocessore

	case 0xE0: { s8 d = (s8)fetch8(); r[CX]--; if (r[CX] && !(flags & F_ZF)) { ip += d; cyc += 14; } } break;
	case 0xE1: { s8 d = (s8)fetch8(); r[CX]--; if (r[CX] && (flags & F_ZF)) { ip += d; cyc += 12; } } break;
	case 0xE2: { s8 d = (s8)fetch8(); r[CX]--; if (r[CX]) { ip += d; cyc += 12; } } break;
	case 0xE3: { s8 d = (s8)fetch8(); if (!r[CX]) { ip += d; cyc += 12; } } break;
	case 0xE4: { u8 p = fetch8(); setR8(0, bus->in8(p)); } break;
	case 0xE5: { u8 p = fetch8(); r[AX] = bus->in8(p) | (bus->in8((u8)(p + 1)) << 8); } break;
	case 0xE6: { u8 p = fetch8(); bus->out8(p, (u8)r[AX]); } break;
	case 0xE7: { u8 p = fetch8(); bus->out8(p, (u8)r[AX]); bus->out8((u8)(p + 1), r[AX] >> 8); } break;
	case 0xE8: { u16 d = fetch16(); push(ip); ip += d; } break;
	case 0xE9: { u16 d = fetch16(); ip += d; } break;
	case 0xEA: { u16 o = fetch16(); u16 sg = fetch16(); ip = o; s[CS] = sg; } break;
	case 0xEB: { s8 d = (s8)fetch8(); ip += d; } break;
	case 0xEC: setR8(0, bus->in8(r[DX])); break;
	case 0xED: r[AX] = bus->in8(r[DX]) | (bus->in8(r[DX] + 1) << 8); break;
	case 0xEE: bus->out8(r[DX], (u8)r[AX]); break;
	case 0xEF: bus->out8(r[DX], (u8)r[AX]); bus->out8(r[DX] + 1, r[AX] >> 8); break;

	case 0xF4: halted = true; break;
	case 0xF5: flags ^= F_CF; break;
	case 0xF6: decodeModRM(); if (!grp3_8(reg_)) divError = true; break;
	case 0xF7: decodeModRM(); if (!grp3_16(reg_)) divError = true; break;
	case 0xF8: flags &= ~F_CF; break;
	case 0xF9: flags |= F_CF; break;
	case 0xFA: flags &= ~F_IF; break;
	case 0xFB: flags |= F_IF; inhibitIrq = 1; break;
	case 0xFC: flags &= ~F_DF; break;
	case 0xFD: flags |= F_DF; break;
	case 0xFE:
		decodeModRM();
		if (reg_ == 0) setRm8(inc8(rm8()));
		else if (reg_ == 1) setRm8(dec8(rm8()));
		break;
	case 0xFF:
		decodeModRM();
		switch (reg_) {
		case 0: setRm16(inc16(rm16())); break;
		case 1: setRm16(dec16(rm16())); break;
		case 2: { u16 t = rm16(); push(ip); ip = t; cyc = 21; } break;
		case 3: { u16 o = rdS16(eaSeg, eaOff), sg = rdS16(eaSeg, (u16)(eaOff + 2)); push(s[CS]); push(ip); s[CS] = sg; ip = o; cyc = 37; } break;
		case 4: ip = rm16(); cyc = 11; break;
		case 5: { u16 o = rdS16(eaSeg, eaOff), sg = rdS16(eaSeg, (u16)(eaOff + 2)); s[CS] = sg; ip = o; cyc = 24; } break;
		default: // PUSH (anche /7)
			if (mod_ == 3 && rm_ == SP && type == CPU_8088) { r[SP] -= 2; wrS16(s[SS], r[SP], r[SP]); }
			else { u16 v = rm16(); push(v); }
			cyc = 16;
			break;
		}
		break;
	}
	return cyc;
}
