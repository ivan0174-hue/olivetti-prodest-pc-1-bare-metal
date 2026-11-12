// Prodest PC1 Emulator - kernel bare metal per Raspberry Pi (libreria Circle)
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/bcmframebuffer.h>
#include <circle/cputhrottle.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/sound/soundbasedevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>

#include "../core/ui.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CFatIO : public HostIO
{
public:
	int  listDir(const char *dir, const char *const *exts, char (*names)[64], int max) override;
	int  loadFile(const char *path, u8 *buf, u32 max) override;
	bool saveFile(const char *path, const u8 *buf, u32 len) override;
	void makeDir(const char *path) override;
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

private:
	void SetupVideo (void);
	void UpdatePalette (void);
	void Present (void);
	void ProcessKeys (void);
	void PumpAudio (void);
	void UpdateLEDs (void);

	static void KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6]);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);

private:
	// non cambiare l'ordine
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CCPUThrottle		m_CPUThrottle;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;

	CBcmFrameBuffer		*m_pFrameBuffer;
	CSoundBaseDevice	*m_pSound;
	CUSBKeyboardDevice * volatile m_pKeyboard;

	Machine			*m_pMachine;
	Emulator		*m_pEmu;
	CFatIO			m_IO;

	u8			*m_pScreen;		// 720x400 indicizzato
	u32			m_nScreenSum;
	int			m_nPaletteMonitor;
	u16			m_Palette[32];
	unsigned		m_nOutX, m_nOutY, m_nOutW, m_nOutH;
	u16			*m_pXMap;
	u8			m_LastLEDs;

	static CKernel *s_pThis;
};

#endif
