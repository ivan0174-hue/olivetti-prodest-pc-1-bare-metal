// Prodest PC1 Emulator - kernel bare metal per Raspberry Pi (libreria Circle)
#include "kernel.h"
#include <circle/string.h>
#include <circle/util.h>
#include <circle/usb/usbhid.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>

#define DRIVE		"SD:"
#define FRAME_US	16667		// 60 Hz, come il monitor CGA
#define FB_HEIGHT	800		// 400 righe emulate x 2

static const char FromKernel[] = "prodest";

CKernel *CKernel::s_pThis = 0;

// ------------------------------------------------------------------ tastiera
// codici HID USB -> scancode XT (set 1). 0 = tasto ignorato
static const u8 HidToXt[256] = {
	/* 00 */ 0, 0, 0, 0, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
	/* 10 */ 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03,
	/* 20 */ 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,
	/* 30 */ 0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
	/* 40 */ 0x41, 0x42, 0x43, 0x44, 0x57, 0x00, 0x37, 0x46, 0x00, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51, 0x4D,
	/* 50 */ 0x4B, 0x50, 0x48, 0x45, 0x35, 0x37, 0x4A, 0x4E, 0x1C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47,
	/* 60 */ 0x48, 0x49, 0x52, 0x53, 0x56, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 70 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 80 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* A0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* B0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* C0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* D0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* E0 */ 0x1D, 0x2A, 0x38, 0, 0x1D, 0x36, 0x38, 0,   // Ctrl, Shift, Alt, Win (sx / dx)
};

#define HID_F12 0x45

struct TKeyEvent { u8 code; u8 down; };
static TKeyEvent s_KeyQueue[128];
static volatile unsigned s_KeyHead = 0, s_KeyTail = 0;
static u8 s_PrevMods = 0;
static u8 s_PrevKeys[6] = { 0 };

static void PushKey (u8 code, bool down)
{
	unsigned n = (s_KeyHead + 1) % 128;
	if (n == s_KeyTail) return;
	s_KeyQueue[s_KeyHead].code = code;
	s_KeyQueue[s_KeyHead].down = down;
	s_KeyHead = n;
}

void CKernel::KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	// modificatori come codici HID E0-E7
	u8 changed = ucModifiers ^ s_PrevMods;
	for (int i = 0; i < 8; i++) if (changed & (1 << i)) PushKey (0xE0 + i, (ucModifiers >> i) & 1);
	s_PrevMods = ucModifiers;
	// tasti rilasciati
	for (int i = 0; i < 6; i++) {
		u8 k = s_PrevKeys[i];
		if (k < 4) continue;
		bool still = false;
		for (int j = 0; j < 6; j++) if (RawKeys[j] == k) still = true;
		if (!still) PushKey (k, false);
	}
	// tasti premuti
	for (int i = 0; i < 6; i++) {
		u8 k = RawKeys[i];
		if (k < 4) continue;
		bool was = false;
		for (int j = 0; j < 6; j++) if (s_PrevKeys[j] == k) was = true;
		if (!was) PushKey (k, true);
	}
	for (int i = 0; i < 6; i++) s_PrevKeys[i] = RawKeys[i];
}

void CKernel::KeyboardRemovedHandler (CDevice *pDevice, void *pContext)
{
	s_pThis->m_pKeyboard = 0;
	s_PrevMods = 0;
	for (int i = 0; i < 6; i++) s_PrevKeys[i] = 0;
}

// ------------------------------------------------------------------ file system
static void FullPath (CString &out, const char *path)
{
	out = DRIVE;
	out.Append (path);
}

int CFatIO::listDir (const char *dir, const char *const *exts, char (*names)[64], int max)
{
	CString p; FullPath (p, dir);
	DIR d; FILINFO fi;
	if (f_opendir (&d, (const char *) p) != FR_OK) return 0;
	int n = 0;
	while (n < max && f_readdir (&d, &fi) == FR_OK && fi.fname[0]) {
		if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
		bool ok = false;
		for (int i = 0; exts[i]; i++) if (pc_endswith_ci (fi.fname, exts[i])) ok = true;
		if (ok && pc_strlen (fi.fname) < 64) pc_strncpy (names[n++], fi.fname, 64);
	}
	f_closedir (&d);
	return n;
}

int CFatIO::loadFile (const char *path, u8 *buf, u32 max)
{
	CString p; FullPath (p, path);
	FIL f;
	if (f_open (&f, (const char *) p, FA_READ | FA_OPEN_EXISTING) != FR_OK) return -1;
	UINT got = 0;
	FRESULT r = f_read (&f, buf, max, &got);
	f_close (&f);
	return r == FR_OK ? (int) got : -1;
}

bool CFatIO::saveFile (const char *path, const u8 *buf, u32 len)
{
	CString p; FullPath (p, path);
	FIL f;
	if (f_open (&f, (const char *) p, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
	UINT put = 0;
	FRESULT r = f_write (&f, buf, len, &put);
	f_close (&f);
	return r == FR_OK && put == len;
}

void CFatIO::makeDir (const char *path)
{
	CString p; FullPath (p, path);
	f_mkdir ((const char *) p);
}

// ------------------------------------------------------------------ kernel
CKernel::CKernel (void)
:	m_CPUThrottle (CPUSpeedMaximum),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pFrameBuffer (0),
	m_pSound (0),
	m_pKeyboard (0),
	m_pMachine (0),
	m_pEmu (0),
	m_pScreen (0),
	m_nScreenSum (0),
	m_nPaletteMonitor (-1),
	m_pXMap (0),
	m_LastLEDs (0xFF)
{
	s_pThis = this;
	m_ActLED.Blink (2);
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	boolean bOK = m_Serial.Initialize (115200);
	if (bOK) bOK = m_Logger.Initialize (&m_Serial);
	if (bOK) bOK = m_Interrupt.Initialize ();
	if (bOK) bOK = m_Timer.Initialize ();
	if (bOK) bOK = m_USBHCI.Initialize ();
	if (bOK) bOK = m_EMMC.Initialize ();
	return bOK;
}

void CKernel::SetupVideo (void)
{
	// dimensioni del monitor (o quelle forzate con width= height= in cmdline.txt)
	unsigned dw = m_Options.GetWidth (), dh = m_Options.GetHeight ();
	if (!dw || !dh) {
		CBcmFrameBuffer Probe (0, 0, 16);
		dw = Probe.GetWidth (); dh = Probe.GetHeight ();
	}
	if (!dw || !dh) { dw = 640; dh = 480; }
	// framebuffer alto 800 righe con le proporzioni del monitor: la GPU lo scala a schermo intero
	unsigned fw = (FB_HEIGHT * dw / dh + 7) & ~7u;
	if (fw < 1068) fw = 1068;
	if (fw > 1920) fw = 1920;
	m_pFrameBuffer = new CBcmFrameBuffer (fw, FB_HEIGHT, 16);
	if (!m_pFrameBuffer->Initialize ()) {
		m_Logger.Write (FromKernel, LogPanic, "Impossibile inizializzare il framebuffer");
	}
	// area 4:3 centrata
	m_nOutH = FB_HEIGHT;
	m_nOutW = FB_HEIGHT * 4 / 3;
	if (m_nOutW > fw) m_nOutW = fw;
	m_nOutX = (fw - m_nOutW) / 2;
	m_nOutY = 0;
	m_pXMap = new u16[m_nOutW];
	for (unsigned x = 0; x < m_nOutW; x++) m_pXMap[x] = (u16) (x * SCREEN_W / m_nOutW);
	u16 *fb = (u16 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	unsigned pitch = m_pFrameBuffer->GetPitch () / 2;
	for (unsigned y = 0; y < m_pFrameBuffer->GetHeight (); y++) memset (fb + y * pitch, 0, fw * 2);
	m_Logger.Write (FromKernel, LogNotice, "Video: monitor %ux%u, framebuffer %ux%u", dw, dh, fw, FB_HEIGHT);
}

void CKernel::UpdatePalette (void)
{
	u32 rgb[32];
	Machine::palette (m_pMachine->cfg.monitor, rgb);
	for (int i = 0; i < 32; i++) {
		u32 c = rgb[i];
		m_Palette[i] = (u16) ((((c >> 19) & 0x1F) << 11) | (((c >> 10) & 0x3F) << 5) | ((c >> 3) & 0x1F));
	}
	m_nPaletteMonitor = m_pMachine->cfg.monitor;
}

void CKernel::Present (void)
{
	m_pMachine->render (m_pScreen, SCREEN_W);
	m_pEmu->draw (m_pScreen, SCREEN_W);

	bool palChanged = m_nPaletteMonitor != m_pMachine->cfg.monitor;
	if (palChanged) UpdatePalette ();

	// salta la copia se l'immagine non e' cambiata
	u32 sum = 2166136261u;
	const u32 *p = (const u32 *) m_pScreen;
	for (unsigned i = 0; i < SCREEN_W * SCREEN_H / 4; i++) sum = (sum ^ p[i]) * 16777619u;
	if (sum == m_nScreenSum && !palChanged) return;
	m_nScreenSum = sum;

	u16 *fb = (u16 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	unsigned pitch = m_pFrameBuffer->GetPitch () / 2;
	int prevSy = -1;
	u16 *prevRow = 0;
	for (unsigned y = 0; y < m_nOutH; y++) {
		int sy = y * SCREEN_H / m_nOutH;
		u16 *dst = fb + (m_nOutY + y) * pitch + m_nOutX;
		if (sy == prevSy) { memcpy (dst, prevRow, m_nOutW * 2); continue; }
		const u8 *src = m_pScreen + sy * SCREEN_W;
		for (unsigned x = 0; x < m_nOutW; x++) dst[x] = m_Palette[src[m_pXMap[x]] & 31];
		prevSy = sy; prevRow = dst;
	}
}

void CKernel::ProcessKeys (void)
{
	static bool shiftL = false, shiftR = false;
	while (s_KeyTail != s_KeyHead) {
		TKeyEvent ev = s_KeyQueue[s_KeyTail];
		s_KeyTail = (s_KeyTail + 1) % 128;

		if (ev.code == 0xE1) shiftL = ev.down;
		if (ev.code == 0xE5) shiftR = ev.down;

		if (ev.code == HID_F12) {
			if (!ev.down) continue;
			if (m_pEmu->menuActive ()) m_pEmu->closeMenu ();
			else {
				// rilascia i modificatori nella macchina emulata prima di aprire il menu
				m_pMachine->keyEvent (0x1D, false); m_pMachine->keyEvent (0x2A, false);
				m_pMachine->keyEvent (0x36, false); m_pMachine->keyEvent (0x38, false);
				m_pEmu->openMenu ();
			}
			continue;
		}

		if (m_pEmu->menuActive ()) {
			if (!ev.down) continue;
			UiKey k = UK_NONE;
			switch (ev.code) {
			case 0x52: case 0x60: k = UK_UP; break;
			case 0x51: case 0x5A: k = UK_DOWN; break;
			case 0x50: case 0x5C: k = UK_LEFT; break;
			case 0x4F: case 0x5E: k = UK_RIGHT; break;
			case 0x28: case 0x58: case 0x2C: k = UK_ENTER; break;
			case 0x29: k = UK_ESC; break;
			case 0x4C: case 0x2A: k = UK_DEL; break;
			case 0x4B: k = UK_PGUP; break;
			case 0x4E: k = UK_PGDN; break;
			case 0x4A: k = UK_HOME; break;
			case 0x4D: k = UK_END; break;
			}
			if (k != UK_NONE) m_pEmu->menuKey (k);
			continue;
		}

		u8 xt = HidToXt[ev.code];
		if (!xt) continue;
		// tasti cursore dedicati: sulla tastiera XT sono sul tastierino numerico.
		// Con Bloc Num attivo si simula Shift, come fanno le tastiere estese.
		bool nav = ev.code >= 0x49 && ev.code <= 0x52;
		bool numLock = m_pMachine->mem[0x417] & 0x20;
		if (nav && numLock && !shiftL && !shiftR && ev.down) {
			m_pMachine->keyEvent (0x2A, true);
			m_pMachine->keyEvent (xt, true);
			m_pMachine->keyEvent (0x2A, false);
			continue;
		}
		m_pMachine->keyEvent (xt, ev.down);
	}
}

void CKernel::UpdateLEDs (void)
{
	CUSBKeyboardDevice *kbd = m_pKeyboard;
	if (!kbd) return;
	u8 f = m_pMachine->mem[0x417];
	u8 leds = ((f & 0x20) ? LED_NUM_LOCK : 0) | ((f & 0x40) ? LED_CAPS_LOCK : 0) | ((f & 0x10) ? LED_SCROLL_LOCK : 0);
	if (leds != m_LastLEDs) {
		m_LastLEDs = leds;
		kbd->SetLEDs (leds);
	}
}

void CKernel::PumpAudio (void)
{
	static s16 buf[2048];
	if (!m_pSound) { m_pMachine->audioRead (buf, 2048); return; }
	unsigned queued = m_pSound->GetQueueFramesAvail ();
	unsigned size = m_pSound->GetQueueSizeFrames ();
	unsigned space = size > queued ? size - queued : 0;
	unsigned n = AUDIO_RATE / 60;
	if (queued < size / 4) n += AUDIO_RATE / 120;         // coda quasi vuota: aggiunge un po' di margine
	if (n > space) n = space;
	if (n > 2048) n = 2048;
	m_pMachine->audioRead (buf, n);
	if (m_pMachine->cfg.volume == 0) memset (buf, 0, n * 2);
	if (n) m_pSound->Write (buf, n * sizeof (s16));
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Olivetti Prodest PC1 Emulator - " __DATE__ " " __TIME__);

	if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK) {
		m_Logger.Write (FromKernel, LogError, "Impossibile montare la scheda SD");
	}
	m_IO.makeDir ("/prodest");
	m_IO.makeDir (DIR_FLOPPY);
	m_IO.makeDir (DIR_TAPE);
	m_IO.makeDir (DIR_ROM);

	SetupVideo ();

	// audio: jack 3,5 mm (PWM) oppure HDMI con "sounddev=sndhdmi" in cmdline.txt
	const char *pSoundDevice = m_Options.GetSoundDevice ();
#ifdef NO_PHYSICAL_COUNTER		// build per QEMU: l'audio PWM non e' emulato
	pSoundDevice = "none";
#endif
	if (strcmp (pSoundDevice, "none") == 0)
		m_pSound = 0;
	else if (strcmp (pSoundDevice, "sndhdmi") == 0)
		m_pSound = new CHDMISoundBaseDevice (&m_Interrupt, AUDIO_RATE, 384 * 10);
	else
		m_pSound = new CPWMSoundBaseDevice (&m_Interrupt, AUDIO_RATE, 2048);
	if (!m_pSound) {
		m_Logger.Write (FromKernel, LogNotice, "Audio disattivato");
	} else if (m_pSound->AllocateQueue (80)) {
		m_pSound->SetWriteFormat (SoundFormatSigned16, 1);
		static s16 silence[2048];
		memset (silence, 0, sizeof silence);
		unsigned pre = m_pSound->GetQueueSizeFrames () / 2;
		if (pre > 2048) pre = 2048;
		m_pSound->Write (silence, pre * 2);
		if (!m_pSound->Start ()) { m_Logger.Write (FromKernel, LogWarning, "Audio non disponibile"); delete m_pSound; m_pSound = 0; }
	} else { delete m_pSound; m_pSound = 0; }

	m_pMachine = new Machine ();
	m_pEmu = new Emulator (*m_pMachine, m_IO);
	m_pScreen = new u8[SCREEN_W * SCREEN_H];
	memset (m_pScreen, 0, SCREEN_W * SCREEN_H);
	m_pEmu->loadConfig ();
	m_pEmu->powerOn ();
	UpdatePalette ();
	if (!m_pEmu->cfg.floppyA[0]) m_pEmu->message ("Premi F12 per inserire un floppy", 400);

	u64 next = CTimer::GetClockTicks64 ();
	unsigned frame = 0;
	for (;;) {
		boolean bUpdated = m_USBHCI.UpdatePlugAndPlay ();
		if (bUpdated && m_pKeyboard == 0) {
			CUSBKeyboardDevice *kbd = (CUSBKeyboardDevice *) m_DeviceNameService.GetDevice ("ukbd1", FALSE);
			if (kbd) {
				kbd->RegisterRemovedHandler (KeyboardRemovedHandler);
				kbd->RegisterKeyStatusHandlerRaw (KeyStatusHandlerRaw);
				m_LastLEDs = 0xFF;
				m_pKeyboard = kbd;
				m_Logger.Write (FromKernel, LogNotice, "Tastiera USB collegata");
			}
		}

		ProcessKeys ();
		if (!m_pEmu->menuActive ()) m_pMachine->runFrame ();
		if (frame % 60 == 0) m_CPUThrottle.Update ();   // controllo temperatura
		PumpAudio ();
		Present ();
		UpdateLEDs ();

		// salvataggio periodico dei floppy modificati (ogni ~5 secondi)
		if (++frame % 300 == 0) {
			bool dirty = m_pMachine->disk[0].dirty || m_pMachine->disk[1].dirty || m_pMachine->tape.dirty;
			if (dirty) m_pEmu->flushMedia ();
		}

		// temporizzazione a 60 Hz (in turbo si va al massimo possibile)
		next += FRAME_US;
		u64 now = CTimer::GetClockTicks64 ();
		if (now > next + 100000) next = now;          // troppo indietro: risincronizza
		while (CTimer::GetClockTicks64 () < next) { }
	}

	return ShutdownHalt;
}
