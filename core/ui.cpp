// Prodest PC1 Emulator - menu di configurazione (F12) e gestione supporti
#include "ui.h"
#include "font.h"

// colori dell'interfaccia (vedi Machine::palette)
enum { C_BG = 16, C_SEL = 17, C_TEXT = 18, C_VALUE = 19, C_DIM = 20, C_TITLE = 21, C_SHADOW = 22, C_OK = 23, C_BG2 = 24, C_WHITE = 25, C_FRAME = 26 };

static const char *const extFloppy[] = { ".IMG", ".IMA", ".DSK", ".360", ".720", ".VFD", ".FLP", 0 };
static const char *const extTape[]   = { ".WAV", 0 };
static const char *const extRom[]    = { ".BIN", ".ROM", 0 };

static const char *cpuLabel[CPUM_COUNT] = { "NEC V40 8 MHz (PC1)", "Intel 8088 4,77 MHz (IBM PC)", "NEC V40 turbo (~32 MHz)" };
static const char *videoLabel[VID_COUNT] = { "CGA (Yamaha V6355)", "Hercules monocromatica" };
static const char *monLabel[MON_COUNT] = { "Colori RGB", "Fosfori verdi", "Fosfori ambra", "Bianco e nero" };
static const char *volLabel[3] = { "Spento", "Basso", "Alto" };
static const int ramSizes[4] = { 128, 256, 512, 640 };

enum {
	IT_FLOPPY_A, IT_FLOPPY_B, IT_TAPE, IT_TAPE_REWIND, IT_CPU, IT_RAM, IT_VIDEO, IT_MONITOR,
	IT_BIOS, IT_VOLUME, IT_SAVE, IT_RESET, IT_POWER, IT_BACK, IT_COUNT
};

// ------------------------------------------------------------------ util testo
static void putText(u8 *fb, int pitch, int x, int y, const char *s, u8 fg, int bg = -1, int maxChars = 999)
{
	for (int i = 0; s[i] && i < maxChars; i++, x += 8) {
		if (x + 8 > SCREEN_W) break;
		const u8 *g = font8x14 + (u8)s[i] * 14;
		for (int l = 0; l < 14; l++) {
			if (y + l < 0 || y + l >= SCREEN_H) continue;
			u8 *d = fb + (y + l) * pitch + x;
			for (int k = 0; k < 8; k++) {
				if (g[l] & (0x80 >> k)) d[k] = fg;
				else if (bg >= 0) d[k] = (u8)bg;
			}
		}
	}
}

static void fillRect(u8 *fb, int pitch, int x, int y, int w, int h, u8 c)
{
	for (int j = y; j < y + h; j++) {
		if (j < 0 || j >= SCREEN_H) continue;
		for (int i = x; i < x + w; i++) if (i >= 0 && i < SCREEN_W) fb[j * pitch + i] = c;
	}
}

static void catStr(char *d, const char *s, int max)
{
	int l = pc_strlen(d);
	pc_strncpy(d + l, s, max - l);
}

static void catNum(char *d, int v, int max)
{
	char t[12]; int n = 0;
	if (v < 0) { catStr(d, "-", max); v = -v; }
	do { t[n++] = '0' + v % 10; v /= 10; } while (v);
	char r[12]; int k = 0;
	while (n) r[k++] = t[--n];
	r[k] = 0;
	catStr(d, r, max);
}

static void catTime(char *d, u32 secs, int max)
{
	char t[8] = { (char)('0' + (secs / 60) / 10 % 10), (char)('0' + (secs / 60) % 10), ':',
	               (char)('0' + (secs % 60) / 10), (char)('0' + secs % 10), 0 };
	catStr(d, t, max);
}

// ------------------------------------------------------------------ Emulator
Emulator::Emulator(Machine &mach, HostIO &hio) : m(mach), io(hio)
{
	cfg.setDefaults();
	active = false; browsing = false; sel = 0;
	nFiles = bsel = btop = 0; browseKind = 0;
	msg[0] = 0; msgFrames = 0;
	romSize = 0;
	quitRequested = false;
}

void Emulator::path(char *out, const char *dir, const char *name)
{
	pc_strncpy(out, dir, 160);
	catStr(out, "/", 160);
	catStr(out, name, 160);
}

void Emulator::message(const char *s, int frames)
{
	pc_strncpy(msg, s, sizeof msg);
	msgFrames = frames;
}

// --- configurazione su file
static int parseInt(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

void Emulator::loadConfig()
{
	cfg.setDefaults();
	int n = io.loadFile(CFG_FILE, ioBuf, 4096);
	if (n <= 0) return;
	ioBuf[n] = 0;
	char *p = (char *)ioBuf;
	while (*p) {
		char *line = p;
		while (*p && *p != '\n' && *p != '\r') p++;
		if (*p) *p++ = 0;
		char *eq = line;
		while (*eq && *eq != '=') eq++;
		if (!*eq) continue;
		*eq = 0;
		const char *k = line, *v = eq + 1;
		#define KEY(s) (pc_strlen(k) == pc_strlen(s) && memcmp(k, s, pc_strlen(s)) == 0)
		if (KEY("cpu")) { int x = parseInt(v); if (x >= 0 && x < CPUM_COUNT) cfg.cpuModel = x; }
		else if (KEY("ram")) { int x = parseInt(v); for (int i = 0; i < 4; i++) if (ramSizes[i] == x) cfg.ramKB = x; }
		else if (KEY("video")) { int x = parseInt(v); if (x >= 0 && x < VID_COUNT) cfg.video = x; }
		else if (KEY("monitor")) { int x = parseInt(v); if (x >= 0 && x < MON_COUNT) cfg.monitor = x; }
		else if (KEY("volume")) { int x = parseInt(v); if (x >= 0 && x <= 2) cfg.volume = x; }
		else if (KEY("bios")) cfg.builtinBios = !(v[0] == 'r' || v[0] == 'R');
		else if (KEY("rom")) pc_strncpy(cfg.romFile, v, 64);
		else if (KEY("floppya")) pc_strncpy(cfg.floppyA, v, 64);
		else if (KEY("floppyb")) pc_strncpy(cfg.floppyB, v, 64);
		else if (KEY("tape")) pc_strncpy(cfg.tapeFile, v, 64);
		#undef KEY
	}
}

bool Emulator::saveConfig()
{
	char b[512]; b[0] = 0;
	catStr(b, "# Prodest PC1 Emulator - configurazione\n", 512);
	catStr(b, "cpu=", 512); catNum(b, cfg.cpuModel, 512); catStr(b, "\n", 512);
	catStr(b, "ram=", 512); catNum(b, cfg.ramKB, 512); catStr(b, "\n", 512);
	catStr(b, "video=", 512); catNum(b, cfg.video, 512); catStr(b, "\n", 512);
	catStr(b, "monitor=", 512); catNum(b, cfg.monitor, 512); catStr(b, "\n", 512);
	catStr(b, "volume=", 512); catNum(b, cfg.volume, 512); catStr(b, "\n", 512);
	catStr(b, cfg.builtinBios ? "bios=builtin\n" : "bios=rom\n", 512);
	catStr(b, "rom=", 512); catStr(b, cfg.romFile, 512); catStr(b, "\n", 512);
	catStr(b, "floppya=", 512); catStr(b, cfg.floppyA, 512); catStr(b, "\n", 512);
	catStr(b, "floppyb=", 512); catStr(b, cfg.floppyB, 512); catStr(b, "\n", 512);
	catStr(b, "tape=", 512); catStr(b, cfg.tapeFile, 512); catStr(b, "\n", 512);
	return io.saveFile(CFG_FILE, (const u8 *)b, pc_strlen(b));
}

void Emulator::powerOn()
{
	flushMedia();
	romSize = 0;
	if (!cfg.builtinBios) {
		char p[160];
		path(p, DIR_ROM, cfg.romFile);
		int n = cfg.romFile[0] ? io.loadFile(p, romBuf, sizeof romBuf) : -1;
		if (n >= 2048) romSize = n;
		else { cfg.builtinBios = true; message("ROM non trovata: uso il BIOS integrato", 300); }
	}
	m.powerOn(cfg, romBuf, romSize);
	// i supporti restano inseriti; al primo avvio li carica dalla configurazione
	if (cfg.floppyA[0] && !m.disk[0].present) insertDisk(0, cfg.floppyA);
	if (cfg.floppyB[0] && !m.disk[1].present) insertDisk(1, cfg.floppyB);
	if (cfg.tapeFile[0] && !m.tape.present) insertTape(cfg.tapeFile);
}

void Emulator::flushMedia()
{
	char p[160];
	for (int d = 0; d < 2; d++) {
		Disk &k = m.disk[d];
		const char *name = d ? cfg.floppyB : cfg.floppyA;
		if (k.present && k.dirty && name[0]) {
			path(p, DIR_FLOPPY, name);
			if (io.saveFile(p, k.data, k.size)) k.dirty = false;
			else message("Errore di scrittura del floppy su SD", 240);
		}
	}
	if (m.tape.present && m.tape.dirty && cfg.tapeFile[0]) {
		path(p, DIR_TAPE, cfg.tapeFile);
		u32 n = m.tape.saveWav(ioBuf, sizeof ioBuf);
		if (n && io.saveFile(p, ioBuf, n)) m.tape.dirty = false;
		else message("Errore di scrittura della cassetta su SD", 240);
	}
}

bool Emulator::insertDisk(int drv, const char *nameIn)
{
	char name[64];
	pc_strncpy(name, nameIn, 64);
	ejectDisk(drv);
	char p[160];
	path(p, DIR_FLOPPY, name);
	Disk &d = m.disk[drv];
	int n = io.loadFile(p, d.data, DISK_MAX);
	if (n <= 0 || !d.insert(n)) {
		d.eject();
		message("Immagine floppy non valida", 200);
		return false;
	}
	d.writeProtect = false;
	pc_strncpy(drv ? cfg.floppyB : cfg.floppyA, name, 64);
	return true;
}

void Emulator::ejectDisk(int drv)
{
	flushMedia();
	m.disk[drv].eject();
	(drv ? cfg.floppyB : cfg.floppyA)[0] = 0;
}

bool Emulator::insertTape(const char *nameIn)
{
	char name[64];
	pc_strncpy(name, nameIn, 64);
	ejectTape();
	char p[160];
	path(p, DIR_TAPE, name);
	int n = io.loadFile(p, ioBuf, sizeof ioBuf);
	if (n <= 0 || !m.tape.loadWav(ioBuf, n)) {
		m.tape.present = false;
		message("File WAV non valido (serve PCM 8/16 bit)", 200);
		return false;
	}
	pc_strncpy(cfg.tapeFile, name, 64);
	return true;
}

bool Emulator::newTape()
{
	ejectTape();
	char names[256][64];
	int n = io.listDir(DIR_TAPE, extTape, names, 256);
	char name[64];
	for (int i = 1; i < 1000; i++) {
		name[0] = 0;
		catStr(name, "NASTRO", 64);
		if (i < 100) catStr(name, "0", 64);
		if (i < 10) catStr(name, "0", 64);
		catNum(name, i, 64);
		catStr(name, ".WAV", 64);
		bool used = false;
		for (int k = 0; k < n; k++) {
			bool eq = true;
			for (int c = 0; eq && (name[c] || names[k][c]); c++) eq = pc_toupper(name[c]) == pc_toupper(names[k][c]);
			if (eq) { used = true; break; }
		}
		if (!used) break;
	}
	m.tape.blank();
	m.tape.dirty = true;
	pc_strncpy(cfg.tapeFile, name, 64);
	flushMedia();
	return true;
}

void Emulator::ejectTape()
{
	flushMedia();
	m.tape.present = false; m.tape.len = 0; m.tape.pos = 0;
	cfg.tapeFile[0] = 0;
}

// ------------------------------------------------------------------ menu
bool Emulator::needsRestart() const
{
	return cfg.ramKB != m.cfg.ramKB || cfg.video != m.cfg.video || cfg.builtinBios != m.cfg.builtinBios ||
	       (!cfg.builtinBios && memcmp(cfg.romFile, m.cfg.romFile, 64) != 0);
}

void Emulator::openMenu() { active = true; browsing = false; }

void Emulator::closeMenu()
{
	active = false; browsing = false;
	flushMedia();
}

void Emulator::openBrowser(int kind)
{
	browseKind = kind;
	const char *dir = kind == 2 ? DIR_TAPE : kind == 3 ? DIR_ROM : DIR_FLOPPY;
	const char *const *ext = kind == 2 ? extTape : kind == 3 ? extRom : extFloppy;
	int off = 0;
	if (kind == 2) { pc_strncpy(files[0], "<Nuova cassetta vuota>", 64); off = 1; }
	nFiles = off + io.listDir(dir, ext, files + off, 256 - off);
	// ordinamento alfabetico
	for (int i = off + 1; i < nFiles; i++) {
		char t[64]; memcpy(t, files[i], 64);
		int j = i - 1;
		while (j >= off) {
			int c = 0, r = 0;
			while (files[j][c] || t[c]) { r = pc_toupper(files[j][c]) - pc_toupper(t[c]); if (r) break; c++; }
			if (r <= 0) break;
			memcpy(files[j + 1], files[j], 64); j--;
		}
		memcpy(files[j + 1], t, 64);
	}
	bsel = btop = 0;
	browsing = true;
	if (nFiles == 0) {
		browsing = false;
		message(kind == 3 ? "Nessuna ROM in /prodest/rom" : "Nessun file in /prodest/floppy", 200);
	}
}

void Emulator::browserPick()
{
	if (bsel < 0 || bsel >= nFiles) return;
	const char *name = files[bsel];
	switch (browseKind) {
	case 0: case 1:
		if (insertDisk(browseKind, name)) message(browseKind ? "Floppy inserito in B:" : "Floppy inserito in A:");
		break;
	case 2:
		if (bsel == 0) { newTape(); message("Cassetta vuota pronta per la registrazione"); }
		else if (insertTape(name)) message("Cassetta inserita");
		break;
	case 3:
		pc_strncpy(cfg.romFile, name, 64);
		cfg.builtinBios = false;
		message("ROM scelta: usare 'Spegni e riaccendi'");
		break;
	}
	browsing = false;
}

void Emulator::itemAction(int item, int dir)
{
	switch (item) {
	case IT_FLOPPY_A: case IT_FLOPPY_B: {
		int drv = item - IT_FLOPPY_A;
		if (dir == 2) { ejectDisk(drv); message("Floppy espulso"); }
		else if (dir == 0) openBrowser(drv);
		else if (m.disk[drv].present) {
			m.disk[drv].writeProtect = !m.disk[drv].writeProtect;
		}
	} break;
	case IT_TAPE:
		if (dir == 2) { ejectTape(); message("Cassetta espulsa"); }
		else if (dir == 0) openBrowser(2);
		break;
	case IT_TAPE_REWIND:
		if (dir == 0) { m.tape.pos = 0; message("Cassetta riavvolta"); }
		break;
	case IT_CPU:
		if (dir) { cfg.cpuModel = (cfg.cpuModel + (dir > 0 ? 1 : CPUM_COUNT - 1)) % CPUM_COUNT; m.applyCpuModel(cfg.cpuModel); }
		break;
	case IT_RAM:
		if (dir == 1 || dir == -1) {
			int i = 0; while (i < 4 && ramSizes[i] != cfg.ramKB) i++;
			i = (i + (dir > 0 ? 1 : 3)) % 4;
			cfg.ramKB = ramSizes[i];
		}
		break;
	case IT_VIDEO:
		if (dir == 1 || dir == -1) cfg.video = (cfg.video + 1) % VID_COUNT;
		break;
	case IT_MONITOR:
		if (dir == 1 || dir == -1) { cfg.monitor = (cfg.monitor + (dir > 0 ? 1 : MON_COUNT - 1)) % MON_COUNT; m.cfg.monitor = cfg.monitor; }
		break;
	case IT_BIOS:
		if (dir == 0 && !cfg.builtinBios) openBrowser(3);
		else if (dir == 0 || dir == 1 || dir == -1) {
			if (cfg.builtinBios) {
				cfg.builtinBios = false;
				if (!cfg.romFile[0]) openBrowser(3);
				if (!cfg.romFile[0]) cfg.builtinBios = true;
			} else cfg.builtinBios = true;
		}
		break;
	case IT_VOLUME:
		if (dir == 1 || dir == -1) { cfg.volume = (cfg.volume + (dir > 0 ? 1 : 2)) % 3; m.cfg.volume = cfg.volume; }
		break;
	case IT_SAVE:
		if (dir == 0) message(saveConfig() ? "Configurazione salvata su SD" : "Impossibile salvare la configurazione", 180);
		break;
	case IT_RESET:
		if (dir == 0) { flushMedia(); m.warmReset(); closeMenu(); }
		break;
	case IT_POWER:
		if (dir == 0) { powerOn(); closeMenu(); }
		break;
	case IT_BACK:
		if (dir == 0) closeMenu();
		break;
	}
}

void Emulator::menuKey(UiKey k)
{
	if (browsing) {
		switch (k) {
		case UK_UP: if (bsel > 0) bsel--; break;
		case UK_DOWN: if (bsel < nFiles - 1) bsel++; break;
		case UK_PGUP: bsel -= 14; if (bsel < 0) bsel = 0; break;
		case UK_PGDN: bsel += 14; if (bsel >= nFiles) bsel = nFiles - 1; break;
		case UK_HOME: bsel = 0; break;
		case UK_END: bsel = nFiles - 1; break;
		case UK_ENTER: browserPick(); break;
		case UK_ESC: case UK_LEFT: browsing = false; break;
		default: break;
		}
		return;
	}
	switch (k) {
	case UK_UP: sel = (sel + IT_COUNT - 1) % IT_COUNT; break;
	case UK_DOWN: sel = (sel + 1) % IT_COUNT; break;
	case UK_HOME: sel = 0; break;
	case UK_END: sel = IT_COUNT - 1; break;
	case UK_LEFT: itemAction(sel, -1); break;
	case UK_RIGHT: itemAction(sel, 1); break;
	case UK_ENTER: itemAction(sel, 0); break;
	case UK_DEL: itemAction(sel, 2); break;
	case UK_ESC: closeMenu(); break;
	default: break;
	}
}

void Emulator::draw(u8 *fb, int pitch)
{
	if (msgFrames > 0) msgFrames--;
	if (!active) {
		if (msgFrames > 0) {
			int w = pc_strlen(msg) * 8 + 16;
			fillRect(fb, pitch, (SCREEN_W - w) / 2, SCREEN_H - 24, w, 20, C_TITLE);
			putText(fb, pitch, (SCREEN_W - w) / 2 + 8, SCREEN_H - 21, msg, C_WHITE);
		}
		return;
	}
	const int W = 600, H = 362, X = (SCREEN_W - W) / 2, Y = (SCREEN_H - H) / 2;
	fillRect(fb, pitch, X + 6, Y + 6, W, H, C_SHADOW);
	fillRect(fb, pitch, X, Y, W, H, C_BG);
	fillRect(fb, pitch, X, Y, W, 2, C_FRAME);
	fillRect(fb, pitch, X, Y + H - 2, W, 2, C_FRAME);
	fillRect(fb, pitch, X, Y, 2, H, C_FRAME);
	fillRect(fb, pitch, X + W - 2, Y, 2, H, C_FRAME);
	fillRect(fb, pitch, X + 2, Y + 2, W - 4, 22, C_TITLE);
	putText(fb, pitch, X + 12, Y + 6, "OLIVETTI PRODEST PC1", C_WHITE);
	putText(fb, pitch, X + W - 12 - 21 * 8, Y + 6, "emulatore bare metal", C_VALUE);

	char line[96];
	if (browsing) {
		static const char *titles[4] = { "Scegli il floppy per A:", "Scegli il floppy per B:", "Scegli la cassetta (WAV)", "Scegli la ROM del BIOS" };
		putText(fb, pitch, X + 16, Y + 34, titles[browseKind], C_VALUE);
		const int rows = 17;
		if (bsel < btop) btop = bsel;
		if (bsel >= btop + rows) btop = bsel - rows + 1;
		for (int i = 0; i < rows && btop + i < nFiles; i++) {
			int idx = btop + i;
			int y = Y + 56 + i * 16;
			if (idx == bsel) fillRect(fb, pitch, X + 10, y - 1, W - 20, 16, C_SEL);
			putText(fb, pitch, X + 20, y, files[idx], idx == bsel ? C_WHITE : C_TEXT, -1, 64);
		}
		if (nFiles > rows) {
			line[0] = 0; catNum(line, bsel + 1, 96); catStr(line, "/", 96); catNum(line, nFiles, 96);
			putText(fb, pitch, X + W - 16 - pc_strlen(line) * 8, Y + 34, line, C_DIM);
		}
		putText(fb, pitch, X + 16, Y + H - 22, "Invio: scegli    Esc: indietro    PagSu/PagGiu: scorri", C_DIM);
	} else {
		for (int i = 0; i < IT_COUNT; i++) {
			int y = Y + 32 + i * 18 + (i >= IT_SAVE ? 6 : 0);
			const char *label = "";
			char val[96]; val[0] = 0;
			bool star = false;
			switch (i) {
			case IT_FLOPPY_A: case IT_FLOPPY_B: {
				int d = i - IT_FLOPPY_A;
				label = d ? "Floppy B:" : "Floppy A:";
				if (m.disk[d].present) {
					catStr(val, d ? cfg.floppyB : cfg.floppyA, 96);
					if (m.disk[d].writeProtect) catStr(val, " [protetto]", 96);
					if (m.disk[d].dirty) catStr(val, " *", 96);
				} else catStr(val, "(vuoto)", 96);
			} break;
			case IT_TAPE:
				label = "Cassetta:";
				if (m.tape.present) {
					catStr(val, cfg.tapeFile, 96);
					catStr(val, "  ", 96);
					u32 rate = m.tape.rate ? m.tape.rate : 44100;
					catTime(val, (u32)((m.tape.pos >> 16) / rate), 96);
					catStr(val, "/", 96);
					catTime(val, m.tape.len / rate, 96);
				} else catStr(val, "(nessuna)", 96);
				break;
			case IT_TAPE_REWIND: label = "  Riavvolgi cassetta"; break;
			case IT_CPU: label = "CPU:"; catStr(val, cpuLabel[cfg.cpuModel], 96); break;
			case IT_RAM: label = "Memoria RAM:"; catNum(val, cfg.ramKB, 96); catStr(val, " KB", 96); star = cfg.ramKB != m.cfg.ramKB; break;
			case IT_VIDEO: label = "Scheda video:"; catStr(val, videoLabel[cfg.video], 96); star = cfg.video != m.cfg.video; break;
			case IT_MONITOR: label = "Monitor:"; catStr(val, monLabel[cfg.monitor], 96); break;
			case IT_BIOS:
				label = "BIOS:";
				if (cfg.builtinBios) catStr(val, "Integrato (HLE)", 96);
				else { catStr(val, "ROM ", 96); catStr(val, cfg.romFile, 96); }
				star = cfg.builtinBios != m.cfg.builtinBios || (!cfg.builtinBios && memcmp(cfg.romFile, m.cfg.romFile, 64) != 0);
				break;
			case IT_VOLUME: label = "Altoparlante:"; catStr(val, volLabel[cfg.volume], 96); break;
			case IT_SAVE: label = "Salva configurazione"; break;
			case IT_RESET: label = "Reset (Ctrl+Alt+Canc)"; break;
			case IT_POWER: label = "Spegni e riaccendi"; break;
			case IT_BACK: label = "Torna all'emulazione"; break;
			}
			if (i == sel) fillRect(fb, pitch, X + 10, y - 2, W - 20, 18, C_SEL);
			putText(fb, pitch, X + 20, y, label, i == sel ? C_WHITE : C_TEXT);
			if (val[0]) {
				bool arrows = i >= IT_CPU && i <= IT_VOLUME;
				int vx = X + 200;
				if (arrows && i == sel) putText(fb, pitch, vx - 12, y, "\x11", C_VALUE);
				putText(fb, pitch, vx, y, val, C_VALUE, -1, 46);
				if (arrows && i == sel) putText(fb, pitch, vx + (pc_strlen(val) < 46 ? pc_strlen(val) : 46) * 8 + 4, y, "\x10", C_VALUE);
				if (star) putText(fb, pitch, X + W - 26, y, "*", C_VALUE);
			}
		}
		const char *hint = "\x18\x19 scegli   \x1b\x1a cambia   Invio conferma   Canc espelli   Esc esci";
		if (sel == IT_FLOPPY_A || sel == IT_FLOPPY_B) hint = "Invio: inserisci   Canc: espelli   \x1b\x1a: protezione scrittura";
		putText(fb, pitch, X + 16, Y + H - 40, hint, C_DIM);
		if (needsRestart()) putText(fb, pitch, X + 16, Y + H - 22, "* attivo dopo \"Spegni e riaccendi\"", C_VALUE);
		else putText(fb, pitch, X + 16, Y + H - 22, "F12 apre/chiude questo menu", C_DIM);
	}
	if (msgFrames > 0) {
		int w = pc_strlen(msg) * 8 + 16;
		fillRect(fb, pitch, (SCREEN_W - w) / 2, Y + H - 64, w, 20, C_OK);
		putText(fb, pitch, (SCREEN_W - w) / 2 + 8, Y + H - 61, msg, C_SHADOW);
	}
}
