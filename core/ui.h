// Prodest PC1 Emulator - menu di configurazione (OSD) e gestione dei supporti
#pragma once
#include "machine.h"

// Interfaccia verso il file system della piattaforma (FatFs su Pi, POSIX su Linux)
class HostIO
{
public:
	virtual ~HostIO() {}
	// elenca i file di 'dir' con una delle estensioni (es. ".IMG"); ritorna il numero di nomi
	virtual int  listDir(const char *dir, const char *const *exts, char (*names)[64], int max) = 0;
	// carica un file; ritorna la dimensione o -1
	virtual int  loadFile(const char *path, u8 *buf, u32 max) = 0;
	virtual bool saveFile(const char *path, const u8 *buf, u32 len) = 0;
	virtual void makeDir(const char *path) { (void)path; }
};

enum UiKey { UK_NONE, UK_UP, UK_DOWN, UK_LEFT, UK_RIGHT, UK_ENTER, UK_ESC, UK_DEL, UK_PGUP, UK_PGDN, UK_HOME, UK_END };

#define DIR_FLOPPY "/prodest/floppy"
#define DIR_TAPE   "/prodest/tape"
#define DIR_ROM    "/prodest/rom"
#define CFG_FILE   "/prodest/prodest.cfg"

class Emulator
{
public:
	Emulator(Machine &m, HostIO &io);

	// avvio
	void loadConfig();
	bool saveConfig();
	void powerOn();          // applica la configurazione e accende la macchina
	void flushMedia();       // salva su SD i dischi/cassette modificati

	// supporti
	bool insertDisk(int drv, const char *name);
	void ejectDisk(int drv);
	bool insertTape(const char *name);
	bool newTape();
	void ejectTape();

	// menu
	bool menuActive() const { return active; }
	void openMenu();
	void closeMenu();
	void menuKey(UiKey k);
	void draw(u8 *fb, int pitch);   // disegna il menu sopra lo schermo emulato
	bool quitRequested;

	Machine &m;
	HostIO  &io;
	Config   cfg;            // configurazione desiderata (puo' differire da quella in esecuzione)

	void message(const char *msg, int frames = 120);

private:
	bool active;
	int  sel;
	// browser file
	bool browsing;
	int  browseKind;         // 0 floppy A, 1 floppy B, 2 cassetta, 3 rom
	char files[256][64];
	int  nFiles, bsel, btop;
	char msg[80];
	int  msgFrames;
	u8   romBuf[0x40000];
	u32  romSize;
	u8   ioBuf[TAPE_MAX + 64];

	void openBrowser(int kind);
	void browserPick();
	void itemAction(int item, int dir);
	bool needsRestart() const;
	void path(char *out, const char *dir, const char *name);
};
