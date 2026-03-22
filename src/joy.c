/*
  Hatari - joy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Handling of the ST joysticks and STE/Falcon enhanced joypads.

  NOTE: The ST uses the joystick port 1 as the default controller.
*/
const char Joy_fileid[] = "Hatari joy.c";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "joy_ui.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "video.h"
#include "statusbar.h"

#define JOY_DEBUG 0
#if JOY_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

#define STE_JOY_ANALOG_MIN_VALUE 0x04	/* minimum value for STE analog joystick/paddle axis */
#define STE_JOY_ANALOG_MID_VALUE 0x24	/* neutral mid value for STE analog joystick/paddle axis */
#define STE_JOY_ANALOG_MAX_VALUE 0x43	/* maximum value for STE analog joystick/paddle axis */

int JoystickSpaceBar = JOYSTICK_SPACE_NULL;   /* State of space-bar on joystick button 2 */
static uint32_t nJoyKeyEmu[JOYSTICK_COUNT];
static uint16_t nSteJoySelect;


/**
 * Enable PC Joystick button press to mimic space bar
 * (For XenonII, Flying Shark etc...) or joystick up (jump)
 *
 * Return up bit or zero
 */
static uint8_t Joy_ButtonSpaceJump(int press, bool jump)
{
	/* If "Jump on Button" is enabled, button acts as "ST Joystick up" */
	if (jump)
	{
		if (press)
			return ATARIJOY_BITMASK_UP;
		return 0;
	}
	/* Otherwise, it acts as pressing SPACE on the ST keyboard
	 *
	 * "JoystickSpaceBar" goes through following transitions:
	 * - JOYSTICK_SPACE_NULL   (joy.c:  init)
	 * - JOYSTICK_SPACE_DOWN   (joy.c:  button pressed)
	 * - JOYSTICK_SPACE_DOWNED (ikbd.c: space  pressed)
	 * - JOYSTICK_SPACE_UP     (joy.c:  button released)
	 * - JOYSTICK_SPACE_NULL   (ikbd.c: space  released)
	 */
	if (press)
	{
		if (JoystickSpaceBar == JOYSTICK_SPACE_NULL)
			JoystickSpaceBar = JOYSTICK_SPACE_DOWN;
	}
	else
	{
		if (JoystickSpaceBar == JOYSTICK_SPACE_DOWNED)
			JoystickSpaceBar = JOYSTICK_SPACE_UP;
	}
	return 0;
}


/**
 * Read PC joystick and return ST format byte, i.e. lower 4 bits direction
 * and top bit fire.
 * NOTE : ID 0 is Joystick 0/Mouse and ID 1 is Joystick 1 (default),
 *        ID 2 and 3 are STE joypads and ID 4 and 5 are parport joysticks.
 */
uint8_t Joy_GetStickData(int nStJoyId)
{
	uint8_t nData = 0;
	JOYREADING JoyReading;

	/* Are we emulating the joystick via the keyboard? */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_KEYBOARD)
	{
		/* If holding 'SHIFT' we actually want cursor key movement, so ignore any of this */
		if (!Keymap_IsShiftPressed())
		{
			nData = nJoyKeyEmu[nStJoyId] & 0xff;
		}
	}
	else if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_REALSTICK)
	{
		/* Read real joystick and map to emulated ST joystick for emulation */
		if (!JoyUI_ReadJoystick(nStJoyId, &JoyReading))
		{
			/* Something is wrong, we cannot read the real joystick data */
			return 0;
		}

		/* Directions */
		if (JoyReading.YPos <= JOYRANGE_UP_VALUE)
			nData |= ATARIJOY_BITMASK_UP;
		else if (JoyReading.YPos >= JOYRANGE_DOWN_VALUE)
			nData |= ATARIJOY_BITMASK_DOWN;
		if (JoyReading.XPos <= JOYRANGE_LEFT_VALUE)
			nData |= ATARIJOY_BITMASK_LEFT;
		else if (JoyReading.XPos >= JOYRANGE_RIGHT_VALUE)
			nData |= ATARIJOY_BITMASK_RIGHT;

		/* PC Joystick button 1 is set as ST joystick button */
		if (JoyReading.Buttons & JOYREADING_BUTTON1)
			nData |= ATARIJOY_BITMASK_FIRE;

		/* PC Joystick button 2 mimics space bar or jump */
		const bool press = JoyReading.Buttons & JOYREADING_BUTTON2;
		const bool jump = ConfigureParams.Joysticks.Joy[nStJoyId].bEnableJumpOnFire2;
		nData |= Joy_ButtonSpaceJump(press, jump);

		/* PC Joystick button 3 is autofire button for ST joystick button */
		if (JoyReading.Buttons & JOYREADING_BUTTON3)
		{
			nData |= ATARIJOY_BITMASK_FIRE;
			if ((nVBLs&0x7)<4)
				nData &= ~ATARIJOY_BITMASK_FIRE;          /* Remove top bit! */
		}
	}

	/* Ignore fire button every 8 frames if enabled autofire (for both cursor emulation and joystick) */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].bEnableAutoFire)
	{
		if ((nVBLs&0x7)<4)
			nData &= ~ATARIJOY_BITMASK_FIRE;          /* Remove top bit! */
	}

	return nData;
}


/**
 * Get the fire button states.
 * Note: More than one fire buttons are only supported for real joystick,
 * not for keyboard emulation!
 */
static int Joy_GetFireButtons(int nStJoyId)
{
	int nButtons = 0;

	/* Are we emulating the joystick via the keyboard? */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_KEYBOARD)
	{
		nButtons |= nJoyKeyEmu[nStJoyId] >> 7;
	}
	else if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_REALSTICK)
	{
		nButtons = JoyUI_GetRealFireButtons(nStJoyId);
	}

	return nButtons;
}


/*-----------------------------------------------------------------------*/
/**
 * Set joystick cursor emulation for given port.  This assumes that
 * if the same keys have been defined for "cursor key emulation" in
 * other ports, the emulation for them has been switched off. Returns
 * true if the port number was OK, asserts otherwise.
 */
bool Joy_SetCursorEmulation(int port)
{
	assert(port >= 0 && port < JOYSTICK_COUNT);
	ConfigureParams.Joysticks.Joy[port].nJoystickMode = JOYSTICK_KEYBOARD;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Toggle joystick cursor emulation between port 0, port 1 and being off
 * from them. When it's turned off from them, the port's previous state
 * is restored
 */
void Joy_ToggleCursorEmulation(void)
{
	static JOYSTICKMODE saved[2] = { JOYSTICK_DISABLED, JOYSTICK_DISABLED };
	JOYSTICKMODE state;
	int i, port = 2;
	for (i = 0; i < 2; i++) {
		state = ConfigureParams.Joysticks.Joy[i].nJoystickMode;
		if (state == JOYSTICK_KEYBOARD) {
			port = i;
		} else {
			saved[i] = state;
		}
	}
	switch (port) {
	case 0:  /* (only) in port 0, disable cursor emu */
		ConfigureParams.Joysticks.Joy[0].nJoystickMode = saved[0];
		break;
	case 1:  /* (at least) in port 1, switch cursor emu to port 0 */
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = saved[1];
		ConfigureParams.Joysticks.Joy[0].nJoystickMode = JOYSTICK_KEYBOARD;
		break;
	default:  /* neither in port 0 or 1, enable cursor emu to port 1 */
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_KEYBOARD;
	}
	Statusbar_UpdateInfo();
}


/*-----------------------------------------------------------------------*/
/**
 * Switch between joystick types in given joyport
 */
bool Joy_SwitchMode(int port)
{
	int mode;
	if (port < 0 || port >= JOYSTICK_COUNT) {
		return false;
	}
	mode = (ConfigureParams.Joysticks.Joy[port].nJoystickMode + 1) % JOYSTICK_MODES;
	ConfigureParams.Joysticks.Joy[port].nJoystickMode = mode;
	Statusbar_UpdateInfo();
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Translate key press into joystick/-pad button
 */
static uint32_t Joy_KeyToButton(int joyid, int symkey)
{
	uint32_t nButtons = 0;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeFire)
		nButtons |= ATARIJOY_BITMASK_FIRE;

	if (joyid != JOYID_JOYPADA && joyid != JOYID_JOYPADB)
		return nButtons;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeB)
		nButtons |= 0x100;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeC)
		nButtons |= 0x200;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeOption)
		nButtons |= 0x400;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodePause)
		nButtons |= 0x800;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeHash)
		nButtons |= 0x1000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[9])
		nButtons |= 0x2000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[6])
		nButtons |= 0x4000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[3])
		nButtons |= 0x8000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[0])
		nButtons |= 0x10000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[8])
		nButtons |= 0x20000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[5])
		nButtons |= 0x40000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[2])
		nButtons |= 0x80000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeStar)
		nButtons |= 0x100000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[7])
		nButtons |= 0x200000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[4])
		nButtons |= 0x400000;

	if (symkey == ConfigureParams.Joysticks.Joy[joyid].nKeyCodeNum[1])
		nButtons |= 0x800000;

	return nButtons;
}


/**
 * A key has been pressed down, check if we use it for joystick emulation
 * via keyboard.
 */
bool Joy_KeyDown(int symkey)
{
	int i;

	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode != JOYSTICK_KEYBOARD)
			continue;

		if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_DOWN;   /* Disable down */
			nJoyKeyEmu[i] |= ATARIJOY_BITMASK_UP;    /* Enable up */
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_UP;   /* Disable up */
			nJoyKeyEmu[i] |= ATARIJOY_BITMASK_DOWN;    /* Enable down */
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_RIGHT;   /* Disable right */
			nJoyKeyEmu[i] |= ATARIJOY_BITMASK_LEFT;    /* Enable left */
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_LEFT;   /* Disable left */
			nJoyKeyEmu[i] |= ATARIJOY_BITMASK_RIGHT;    /* Enable right */
			return true;
		}
		else
		{
			uint32_t nButtons = Joy_KeyToButton(i, symkey);
			if (nButtons)
			{
				nJoyKeyEmu[i] |= nButtons;
				return true;
			}
		}
	}

	return false;
}


/**
 * A key has been released, check if we use it for joystick emulation
 * via keyboard.
 */
bool Joy_KeyUp(int symkey)
{
	int i;

	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode != JOYSTICK_KEYBOARD)
			continue;

		if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_UP;
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_DOWN;
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_LEFT;
			return true;
		}
		else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
		{
			nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_RIGHT;
			return true;
		}
		else
		{
			uint32_t nButtons = Joy_KeyToButton(i, symkey);
			if (nButtons)
			{
				nJoyKeyEmu[i] &= ~nButtons;
				return true;
			}
		}
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Read from STE / Falcon joypad buttons register (0xff9200)
 * - On MegaSTE and Falcon, the byte at $ff9200 also contains the state of the 8 DIP switches
 *   available on the motherboard
 * - Note that on STE/MegaSTE $ff9200 can only be accessed as word, not byte. $ff9201 can be
 *   accessed as byte
 */
void Joy_StePadButtons_DIPSwitches_ReadWord(void)
{
	uint16_t nData = 0xff;
	uint8_t DIP;

	if ( !Config_IsMachineFalcon()
	  && ( nIoMemAccessSize == SIZE_BYTE ) && ( IoAccessCurrentAddress == 0xff9200 ) )
	{
		/* This register does not like to be accessed in byte mode at $ff9200 */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	if (ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0x0f) != 0x0f)
	{
		int nButtons = Joy_GetFireButtons(JOYID_JOYPADA);
		if (!(nSteJoySelect & 0x1))
		{
			if (nButtons & 0x01)  /* Fire button A pressed? */
				nData &= ~2;
			if (nButtons & 0x10)  /* Fire button PAUSE pressed? */
				nData &= ~1;
		}
		else if (!(nSteJoySelect & 0x2))
		{
			if (nButtons & 0x02)  /* Fire button B pressed? */
				nData &= ~2;
		}
		else if (!(nSteJoySelect & 0x4))
		{
			if (nButtons & 0x04)  /* Fire button C pressed? */
				nData &= ~2;
		}
		else if (!(nSteJoySelect & 0x8))
		{
			if (nButtons & 0x08)  /* Fire button OPTION pressed? */
				nData &= ~2;
		}
	}

	if (ConfigureParams.Joysticks.Joy[JOYID_JOYPADB].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0xf0) != 0xf0)
	{
		int nButtons = Joy_GetFireButtons(JOYID_JOYPADB);
		if (!(nSteJoySelect & 0x10))
		{
			if (nButtons & 0x01)  /* Fire button A pressed? */
				nData &= ~8;
			if (nButtons & 0x10)  /* Fire button PAUSE pressed? */
				nData &= ~4;
		}
		else if (!(nSteJoySelect & 0x20))
		{
			if (nButtons & 0x02)  /* Fire button B pressed? */
				nData &= ~8;
		}
		else if (!(nSteJoySelect & 0x40))
		{
			if (nButtons & 0x04)  /* Fire button C pressed? */
				nData &= ~8;
		}
		else if (!(nSteJoySelect & 0x80))
		{
			if (nButtons & 0x08)  /* Fire button OPTION pressed? */
				nData &= ~8;
		}
	}

	/* On MegaSTE and Falcon, add the state of the 8 DIP Switches in upper byte */
	if ( Config_IsMachineMegaSTE() )
		DIP = IoMemTabMegaSTE_DIPSwitches_Read();
	else if ( Config_IsMachineFalcon() )
		DIP = IoMemTabFalcon_DIPSwitches_Read();
	else
		DIP = 0xff;				/* STE, No DIP switches */
	nData |= ( DIP << 8 );

	Dprintf(("0xff9200 -> 0x%04x\n", nData));
	IoMem_WriteWord(0xff9200, nData);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to STE / Falcon joypad buttons register (0xff9200)
 * This does nothing, but we still check that $ff9200 is not accessed as byte
 * on STE/MegaSTE, else we trigger a bus error
 */
void Joy_StePadButtons_DIPSwitches_WriteWord(void)
{
	if ( !Config_IsMachineFalcon()
	  && ( nIoMemAccessSize == SIZE_BYTE ) && ( IoAccessCurrentAddress == 0xff9200 ) )
	{
		/* This register does not like to be accessed in byte mode at $ff9200 */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read from STE joypad direction/buttons register (0xff9202)
 *
 * This is used e.g. by Reservoir Gods' Tautology II
 */
void Joy_StePadMulti_ReadWord(void)
{
	uint16_t nData = 0xff;

	if (ConfigureParams.Joysticks.Joy[JOYID_JOYPADA].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0x0f) != 0x0f)
	{
		nData &= 0xf0;
		if (!(nSteJoySelect & 0x1))
		{
			nData |= ~Joy_GetStickData(JOYID_JOYPADA) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x2))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADA) >> 13) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x4))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADA) >> 9) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x8))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADA) >> 5) & 0x0f;
		}
	}

	if (ConfigureParams.Joysticks.Joy[JOYID_JOYPADB].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0xf0) != 0xf0)
	{
		nData &= 0x0f;
		if (!(nSteJoySelect & 0x10))
		{
			nData |= ~Joy_GetStickData(JOYID_JOYPADB) << 4;
		}
		else if (!(nSteJoySelect & 0x20))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADB) >> (13-4)) & 0xf0;
		}
		else if (!(nSteJoySelect & 0x40))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADB) >> (9-4)) & 0xf0;
		}
		else if (!(nSteJoySelect & 0x80))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_JOYPADB) >> (5-4)) & 0xf0;
		}
	}

	nData = (nData << 8) | 0x0ff;
	Dprintf(("0xff9202 -> 0x%04x\n", nData));
	IoMem_WriteWord(0xff9202, nData);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to STE joypad selection register (0xff9202)
 */
void Joy_StePadMulti_WriteWord(void)
{
	nSteJoySelect = IoMem_ReadWord(0xff9202);
	Dprintf(("0xff9202 <- 0x%04x\n", nSteJoySelect));
}


/*-----------------------------------------------------------------------*/
/**
 * Read STE lightpen X register (0xff9220)
 */
void Joy_SteLightpenX_ReadWord(void)
{
	uint16_t nData = 0;	/* Lightpen is not supported yet */

	Dprintf(("0xff9220 -> 0x%04x\n", nData));

	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	IoMem_WriteWord(0xff9220, nData);
}

/**
 * Read STE lightpen Y register (0xff9222)
 */
void Joy_SteLightpenY_ReadWord(void)
{
	uint16_t nData = 0;	/* Lightpen is not supported yet */

	Dprintf(("0xff9222 -> 0x%04x\n", nData));

	if (nIoMemAccessSize == SIZE_BYTE)
	{
		/* This register does not like to be accessed in byte mode */
		M68000_BusError(IoAccessFullAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return;
	}

	IoMem_WriteWord(0xff9222, nData);
}

/*-----------------------------------------------------------------------*/
/**
 * Read PC joystick and return ST format analog value byte
 */
static uint8_t Joy_GetStickAnalogData(int nStJoyId, bool isXAxis)
{
	/* Only makes sense to call this for STE pads */
	assert(nStJoyId == 2 || nStJoyId == 3);

	/* Default to middle of Axis */
	uint8_t nData = STE_JOY_ANALOG_MID_VALUE;

	/* Are we emulating the joystick via the keyboard? */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_KEYBOARD)
	{
		/* If holding 'SHIFT' we actually want cursor key movement, so ignore any of this */
		if (!Keymap_IsShiftPressed())
		{
			uint8_t digiData = nJoyKeyEmu[nStJoyId];
			uint8_t bitmaskMin = isXAxis ? ATARIJOY_BITMASK_LEFT : ATARIJOY_BITMASK_UP;
			uint8_t bitmaskMax = isXAxis ? ATARIJOY_BITMASK_RIGHT : ATARIJOY_BITMASK_DOWN;

			if (digiData & bitmaskMin)
			{
				nData = STE_JOY_ANALOG_MIN_VALUE;
			}
			else if (digiData & bitmaskMax)
			{
				nData = STE_JOY_ANALOG_MAX_VALUE;
			}
		}
	}
	else if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_REALSTICK)
	{
		JOYREADING JoyReading;

		/* Read real joystick and map to emulated ST joystick for emulation */
		if (JoyUI_ReadJoystick(nStJoyId, &JoyReading))
		{
			int pos = isXAxis ? JoyReading.XPos : JoyReading.YPos;
			if (pos < -32768)
				pos = -32768;
			unsigned int upos = 32768 + pos;
			nData = STE_JOY_ANALOG_MIN_VALUE + ((upos & 0xff00) >> 8) / STE_JOY_ANALOG_MIN_VALUE;
		}
	}

	return nData;
}

/**
 * Read STE Pad 0 Analog X register (0xff9211)
 */
void Joy_StePadAnalog0X_ReadByte(void)
{
	uint8_t nData = Joy_GetStickAnalogData(2, true);
	Dprintf(("0xff9211 -> 0x%02x\n", nData));
	IoMem_WriteByte(0xff9211, nData);
}

/**
 * Read STE Pad 0 Analog Y register (0xff9213)
 */
void Joy_StePadAnalog0Y_ReadByte(void)
{
	uint8_t nData = Joy_GetStickAnalogData(2, false);
	Dprintf(("0xff9213 -> 0x%02x\n", nData));
	IoMem_WriteByte(0xff9213, nData);
}

/**
 * Read STE Pad 1 Analog X register (0xff9215)
 */
void Joy_StePadAnalog1X_ReadByte(void)
{
	uint8_t nData = Joy_GetStickAnalogData(3, true);
	Dprintf(("0xff9215 -> 0x%02x\n", nData));
	IoMem_WriteByte(0xff9215, nData);
}

/**
 * Read STE Pad 1 Analog Y register (0xff9217)
 */
void Joy_StePadAnalog1Y_ReadByte(void)
{
	uint8_t nData = Joy_GetStickAnalogData(3, false);
	Dprintf(("0xff9217 -> 0x%02x\n", nData));
	IoMem_WriteByte(0xff9217, nData);
}

