// Prodest PC1 Emulator - harness di test per Linux (senza grafica)
// Esegue la macchina per N frame, puo' digitare testo e salva uno screenshot PPM.
#include "../core/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>

static std::string root = ".";

class PosixIO : public HostIO {
public:
	int listDir(const char *dir, const char *const *exts, char (*names)[64], int max) override {
		DIR *d = opendir((root + dir).c_str());
		if (!d) return 0;
		int n = 0;
		while (dirent *e = readdir(d)) {
			if (e->d_name[0] == '.') continue;
			bool ok = false;
			for (int i = 0; exts[i]; i++) if (pc_endswith_ci(e->d_name, exts[i])) ok = true;
			if (ok && n < max) pc_strncpy(names[n++], e->d_name, 64);
		}
		closedir(d);
		return n;
	}
	int loadFile(const char *path, u8 *buf, u32 max) override {
		FILE *f = fopen((root + path).c_str(), "rb");
		if (!f) return -1;
		int n = (int)fread(buf, 1, max, f);
		fclose(f);
		return n;
	}
	bool saveFile(const char *path, const u8 *buf, u32 len) override {
		FILE *f = fopen((root + path).c_str(), "wb");
		if (!f) return false;
		bool ok = fwrite(buf, 1, len, f) == len;
		fclose(f);
		return ok;
	}
};

// ASCII -> scancode XT (layout USA)
static bool asciiToScan(char c, u8 &sc, bool &shift)
{
	static const char *lo = "\0\x1b" "1234567890-=\b\t" "qwertyuiop[]\r\0" "asdfghjkl;'`\0\\" "zxcvbnm,./";
	static const char *hi = "\0\x1b" "!@#$%^&*()_+\b\t" "QWERTYUIOP{}\r\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?";
	if (c == ' ') { sc = 0x39; shift = false; return true; }
	if (c == '\n') c = '\r';
	for (int i = 1; i < 0x36; i++) {
		if (lo[i] == c) { sc = i; shift = false; return true; }
		if (hi[i] == c) { sc = i; shift = true; return true; }
	}
	return false;
}

int main(int argc, char **argv)
{
	int frames = 600;
	const char *ppm = 0;
	std::vector<std::pair<int, std::string>> typing;   // (frame, testo)
	std::vector<std::pair<int, int>> menuKeys;         // (frame, UiKey) ; UiKey -1 = F12
	bool dumpText = false;
	Config base; base.setDefaults();
	bool cfgFromArgs = false;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
		if (a == "--root") root = next();
		else if (a == "--frames") frames = atoi(next().c_str());
		else if (a == "--ppm") ppm = argv[++i];
		else if (a == "--text") dumpText = true;
		else if (a == "--type") { int f = atoi(next().c_str()); typing.push_back({ f, next() }); }
		else if (a == "--menu") { int f = atoi(next().c_str()); menuKeys.push_back({ f, atoi(next().c_str()) }); }
		else if (a == "--cpu") { base.cpuModel = atoi(next().c_str()); cfgFromArgs = true; }
		else if (a == "--ram") { base.ramKB = atoi(next().c_str()); cfgFromArgs = true; }
		else if (a == "--video") { base.video = atoi(next().c_str()); cfgFromArgs = true; }
		else if (a == "--rom") { base.builtinBios = false; pc_strncpy(base.romFile, next().c_str(), 64); cfgFromArgs = true; }
		else if (a == "--a") { pc_strncpy(base.floppyA, next().c_str(), 64); cfgFromArgs = true; }
		else if (a == "--b") { pc_strncpy(base.floppyB, next().c_str(), 64); cfgFromArgs = true; }
		else if (a == "--tape") { pc_strncpy(base.tapeFile, next().c_str(), 64); cfgFromArgs = true; }
	}
	static Machine *m = new Machine();
	static PosixIO io;
	static Emulator *emu = new Emulator(*m, io);
	emu->loadConfig();
	if (cfgFromArgs) emu->cfg = base;
	emu->powerOn();

	static u8 fb[SCREEN_W * SCREEN_H];
	std::vector<std::pair<u8, bool>> pending;
	int peak = 0, peakFrame = -1;
	for (int f = 0; f < frames; f++) {
		for (auto &t : typing) if (t.first == f) {
			for (char c : t.second) {
				u8 sc; bool sh;
				if (!asciiToScan(c, sc, sh)) continue;
				if (sh) pending.push_back({ 0x2A, true });
				pending.push_back({ sc, true }); pending.push_back({ sc, false });
				if (sh) pending.push_back({ 0x2A, false });
			}
		}
		for (auto &k : menuKeys) if (k.first == f) {
			if (k.second < 0) { if (emu->menuActive()) emu->closeMenu(); else emu->openMenu(); }
			else emu->menuKey((UiKey)k.second);
		}
		// una pressione ogni 2 frame, come una tastiera reale
		if (!pending.empty() && (f & 1)) { m->keyEvent(pending[0].first, pending[0].second); pending.erase(pending.begin()); }
		if (!emu->menuActive()) m->runFrame();
		s16 snd[800]; int got = m->audioRead(snd, 800);
		for (int i = 0; i < got; i++) { int a = snd[i] < 0 ? -snd[i] : snd[i]; if (a > peak) { peak = a; peakFrame = f; } }
	}
	emu->flushMedia();
	m->render(fb, SCREEN_W);
	emu->draw(fb, SCREEN_W);
	if (dumpText) {
		u16 seg = m->cfg.video == VID_HERCULES ? 0xB000 : 0xB800;
		u8 mode = m->mem[0x449];
		int cols = mode <= 1 ? 40 : 80;
		u16 start = m->mem[0x44E] | (m->mem[0x44F] << 8);
		for (int r = 0; r < 25; r++) {
			char line[81];
			for (int c = 0; c < cols; c++) { u8 ch = m->mem[seg * 16 + start + (r * cols + c) * 2]; line[c] = ch >= 32 && ch < 127 ? ch : ' '; }
			int l = cols; while (l > 0 && line[l - 1] == ' ') l--;
			line[l] = 0;
			printf("|%s\n", line);
		}
		printf("CS:IP=%04X:%04X  mode=%02X  audio peak=%d @frame %d\n", m->cpu.s[CS], m->cpu.ip, mode, peak, peakFrame);
	}
	if (ppm) {
		u32 pal[32];
		Machine::palette(emu->cfg.monitor, pal);
		FILE *f = fopen(ppm, "wb");
		fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
		for (int y = 0; y < SCREEN_H; y++)
			for (int x = 0; x < SCREEN_W; x++) {
				u32 c = pal[fb[y * SCREEN_W + x] & 31];
				u8 rgb[3] = { (u8)(c >> 16), (u8)(c >> 8), (u8)c };
				fwrite(rgb, 1, 3, f);
			}
		fclose(f);
	}
	return 0;
}
