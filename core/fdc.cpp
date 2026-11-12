// Prodest PC1 Emulator - controller floppy NEC uPD765 (usato con una ROM BIOS esterna)
#include "machine.h"

void Fdc::reset()
{
	dor = 0; msr = 0x80; st0 = 0;
	cmdLen = cmdPos = 0; resLen = resPos = 0;
	for (int i = 0; i < 4; i++) cyl[i] = 0;
	pendingSis = 0; irqDelay = 0;
}

static int fdcCmdLength(u8 c)
{
	switch (c & 0x1F) {
	case 0x02: case 0x05: case 0x06: case 0x09: case 0x0C: case 0x11: case 0x19: case 0x1D: return 9;
	case 0x03: return 3;
	case 0x04: return 2;
	case 0x07: return 2;
	case 0x08: return 1;
	case 0x0A: return 2;
	case 0x0D: return 6;
	case 0x0F: return 3;
	default: return 1;
	}
}

void Machine::fdcRaiseIrq(int delay)
{
	fdc.irqDelay = delay;
}

void Machine::fdcResult(int n, const u8 *r)
{
	for (int i = 0; i < n; i++) fdc.res[i] = r[i];
	fdc.resLen = n; fdc.resPos = 0;
	fdc.msr = n ? 0xD0 : 0x80;
}

u8 Machine::fdcRead(int port)
{
	if (port == 0x3F4) return fdc.msr;
	if (port == 0x3F5) {
		if (fdc.resPos < fdc.resLen) {
			u8 v = fdc.res[fdc.resPos++];
			if (fdc.resPos >= fdc.resLen) { fdc.msr = 0x80; fdc.resLen = fdc.resPos = 0; }
			return v;
		}
		return 0xFF;
	}
	if (port == 0x3F7) return 0x00;
	return 0xFF;
}

void Machine::fdcWrite(int port, u8 v)
{
	if (port == 0x3F2) {
		u8 old = fdc.dor;
		fdc.dor = v;
		if (!(v & 0x04)) { fdc.msr = 0x80; fdc.cmdPos = 0; fdc.resLen = 0; }
		else if (!(old & 0x04)) {
			// uscita dal reset: interrupt e 4 stati per SENSE INTERRUPT (modo polling)
			fdc.pendingSis = 4;
			for (int i = 0; i < 4; i++) { fdc.sisSt0[i] = 0xC0 | i; fdc.sisDrive[i] = i; }
			fdc.msr = 0x80;
			fdcRaiseIrq(50);
		}
		return;
	}
	if (port != 0x3F5) return;
	if (fdc.resLen) return;          // in fase risultati
	if (fdc.cmdPos == 0) fdc.cmdLen = fdcCmdLength(v);
	fdc.cmd[fdc.cmdPos++] = v;
	fdc.msr = 0x90;
	if (fdc.cmdPos >= fdc.cmdLen) {
		fdc.cmdPos = 0;
		fdcExecute();
	}
}

void Machine::fdcExecute()
{
	u8 c = fdc.cmd[0] & 0x1F;
	int drv = fdc.cmd[1] & 3;
	switch (c) {
	case 0x03: fdc.msr = 0x80; break;        // SPECIFY
	case 0x04: {                             // SENSE DRIVE STATUS
		u8 st3 = drv | (fdc.cmd[1] & 4) | 0x20 | 0x08;
		if (fdc.cyl[drv] == 0) st3 |= 0x10;
		if (drv < 2 && disk[drv].present && disk[drv].writeProtect) st3 |= 0x40;
		fdcResult(1, &st3);
	} break;
	case 0x07:                               // RECALIBRATE
		fdc.cyl[drv] = 0;
		fdc.pendingSis = 1; fdc.sisSt0[0] = 0x20 | drv; fdc.sisDrive[0] = drv;
		fdc.msr = 0x80;
		fdcRaiseIrq(200);
		break;
	case 0x0F:                               // SEEK
		fdc.cyl[drv] = fdc.cmd[2];
		fdc.pendingSis = 1; fdc.sisSt0[0] = 0x20 | drv; fdc.sisDrive[0] = drv;
		fdc.msr = 0x80;
		fdcRaiseIrq(200);
		break;
	case 0x08: {                             // SENSE INTERRUPT STATUS
		u8 r[2];
		if (fdc.pendingSis > 0) {
			int k = (fdc.pendingSis == 4) ? 0 : 0;
			r[0] = fdc.sisSt0[k]; r[1] = (u8)fdc.cyl[fdc.sisDrive[k]];
			for (int i = 0; i < 3; i++) { fdc.sisSt0[i] = fdc.sisSt0[i + 1]; fdc.sisDrive[i] = fdc.sisDrive[i + 1]; }
			fdc.pendingSis--;
			fdcResult(2, r);
		} else {
			r[0] = 0x80;
			fdcResult(1, r);
		}
	} break;
	case 0x0A: {                             // READ ID
		u8 r[7];
		int h = (fdc.cmd[1] >> 2) & 1;
		bool ok = drv < 2 && disk[drv].present;
		r[0] = (ok ? 0 : 0x40) | (h << 2) | drv; r[1] = ok ? 0 : 0x01; r[2] = 0;
		r[3] = fdc.cyl[drv]; r[4] = h; r[5] = 1; r[6] = 2;
		fdcResult(7, r);
		fdcRaiseIrq(100);
	} break;
	case 0x05: case 0x06: case 0x09: case 0x0C: case 0x02:
		fdcReadWrite(c == 0x05 || c == 0x09);
		break;
	case 0x0D:
		fdcFormat();
		break;
	default: {
		u8 r = 0x80;                          // comando non valido
		fdcResult(1, &r);
	} break;
	}
}

void Machine::fdcReadWrite(bool write)
{
	int drv = fdc.cmd[1] & 3;
	int h = (fdc.cmd[1] >> 2) & 1;
	int C = fdc.cmd[2], H = fdc.cmd[3], R = fdc.cmd[4], N = fdc.cmd[5], EOT = fdc.cmd[6];
	bool mt = fdc.cmd[0] & 0x80;
	u8 st0 = (h << 2) | drv, st1 = 0, st2 = 0;
	u8 sec[512];
	dma.status &= ~0x04;
	Disk *d = drv < 2 ? &disk[drv] : 0;
	(void)N;
	if (!d || !d->present) {
		st0 |= 0x40; st1 = 0x01;   // nessun disco: address mark mancante
	} else if (write && d->writeProtect) {
		st0 |= 0x40; st1 = 0x02;
	} else {
		for (;;) {
			if (R < 1 || R > d->spt || h >= d->heads || C >= d->cyls) { st0 |= 0x40; st1 |= 0x04; break; }
			u32 off = (u32)d->lba(C, h, R) * 512;
			int n;
			if (write) {
				n = dmaTransfer(2, sec, 512, false);
				memcpy(d->data + off, sec, n);
				if (n < 512) memset(d->data + off + n, 0, 512 - n);
				d->dirty = true;
			} else {
				memcpy(sec, d->data + off, 512);
				n = dmaTransfer(2, sec, 512, true);
			}
			bool tc = (dma.status & 0x04) != 0 || n < 512;
			if (R == EOT) {
				R = 1;
				if (mt && h == 0 && d->heads > 1) { h = 1; H = 1; if (tc) break; continue; }
				C++;
				if (mt) { h = 0; H = 0; }
				if (!tc) { st0 |= 0x40; st1 |= 0x80; }   // fine cilindro senza TC
				break;
			}
			R++;
			if (tc) break;
		}
	}
	st0 = (st0 & ~0x04) | (h << 2);
	u8 r[7] = { st0, st1, st2, (u8)C, (u8)H, (u8)R, 2 };
	fdcResult(7, r);
	fdc.msr = 0xD0;
	fdcRaiseIrq(300);
}

void Machine::fdcFormat()
{
	int drv = fdc.cmd[1] & 3;
	int h = (fdc.cmd[1] >> 2) & 1;
	int sc = fdc.cmd[3];
	u8 fill = fdc.cmd[5];
	Disk *d = drv < 2 ? &disk[drv] : 0;
	u8 st0 = (h << 2) | drv, st1 = 0;
	u8 id[4] = { 0, 0, 0, 2 };
	if (!d || !d->present) { st0 |= 0x40; st1 = 0x01; }
	else if (d->writeProtect) { st0 |= 0x40; st1 = 0x02; }
	else {
		for (int i = 0; i < sc; i++) {
			if (dmaTransfer(2, id, 4, false) < 4 && i < sc - 1) break;
			int C = id[0], H = id[1], R = id[2];
			if (C < d->cyls && H < d->heads && R >= 1 && R <= d->spt) {
				memset(d->data + (u32)d->lba(C, H, R) * 512, fill, 512);
				d->dirty = true;
			}
		}
	}
	u8 r[7] = { st0, st1, 0, id[0], id[1], id[2], id[3] };
	fdcResult(7, r);
	fdc.msr = 0xD0;
	fdcRaiseIrq(300);
}
