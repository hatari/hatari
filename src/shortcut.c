/*
  Hatari - shortcut.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Shortcut keys
*/
const char ShortCut_rcsid[] = "Hatari $Id: shortcut.c,v 1.39 2008-10-26 22:39:50 eerot Exp $";

#include <SDL.h>

#include "main.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "reset.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "configuration.h"
#include "shortcut.h"
#include "debugui.h"
#include "sound.h"

static SHORTCUTKEYIDX ShortCutKey = SHORTCUT_NONE;  /* current shortcut key */


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle full-screen
 */
static void ShortCut_FullScreen(void)
{
	if (!bInFullScreen)
	{
		Screen_EnterFullScreen();
	}
	else
	{
		Screen_ReturnFromFullScreen();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle mouse mode
 */
static void ShortCut_MouseMode(void)
{
	bGrabMouse = !bGrabMouse;        /* Toggle flag */

	/* If we are in windowed mode, toggle the mouse cursor mode now: */
	if (!bInFullScreen)
	{
		if (bGrabMouse)
		{
			SDL_WM_GrabInput(SDL_GRAB_ON);
		}
		else
		{
			SDL_WM_GrabInput(SDL_GRAB_OFF);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle YM/WAV sound recording
 */
static void ShortCut_RecordSound(void)
{
	/* Is working? */
	if (bSoundWorking)
	{
		/* Are we currently recording? If so stop */
		if (Sound_AreWeRecording())
		{
			/* Stop, and save */
			Sound_EndRecording();
		}
		else
		{
			/* Begin recording */
			Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle screen animation recording
 */
static void ShortCut_RecordAnimation(void)
{
	/* Are we currently recording? If so stop */
	if (ScreenSnapShot_AreWeRecording())
	{
		/* Stop */
		ScreenSnapShot_EndRecording();
	}
	else
	{
		/* Start animation */
		ScreenSnapShot_BeginRecording(ConfigureParams.Screen.bCaptureChange,
		                              ConfigureParams.Screen.nFramesPerSecond);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to sound on/off
 */
static void ShortCut_SoundOnOff(void)
{
	/* Toggle sound on/off */
	ConfigureParams.Sound.bEnableSound ^= TRUE;

	/* And start/stop if need to */
	if (!ConfigureParams.Sound.bEnableSound)
	{
		if (Sound_AreWeRecording())
			Sound_EndRecording();
		Audio_UnInit();
	}
	else
	{
		Audio_Init();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to fast forward
 */
static void ShortCut_FastForward(void)
{
	/* If already on max speed, switch back to normal */
	if (ConfigureParams.System.bFastForward == TRUE)
	{
		/* Restore */
		ConfigureParams.System.bFastForward = FALSE;

		/* Reset the sound emulation variables: */
		Sound_ResetBufferIndex();
	}
	else
	{
		/* Set maximum speed */
		ConfigureParams.System.bFastForward = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to 'Boss' key, ie minmize Window and switch to another application
 */
static void ShortCut_BossKey(void)
{
	/* If we are in full-screen, then return to a window */
	Screen_ReturnFromFullScreen();

	if (bGrabMouse)
	{
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		bGrabMouse = FALSE;
	}

	/* Minimize Window and give up processing to next one! */
	SDL_WM_IconifyWindow();
}


/*-----------------------------------------------------------------------*/
/**
 * Shorcut to pause or debug interface
 */
static void ShortCut_Pause(void)
{
	if (bEnableDebug)
	{
		int running;
		
		if (bInFullScreen)
			Screen_ReturnFromFullScreen();

		/* Call the debugger */
		running = Main_PauseEmulation(TRUE);
		DebugUI();
		if (running)
			Main_UnPauseEmulation();
	}
	else
	{
		if (!Main_UnPauseEmulation())
			Main_PauseEmulation(TRUE);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Check to see if pressed any shortcut keys, and call handling function
 */
void ShortCut_ActKey(void)
{
	if (ShortCutKey == SHORTCUT_NONE)
		return;

	switch (ShortCutKey)
	{
	 case SHORTCUT_OPTIONS:
		Dialog_DoProperty();           /* Show options dialog */
		break;
	 case SHORTCUT_FULLSCREEN:
		ShortCut_FullScreen();         /* Switch between fullscreen/windowed mode */
		break;
	 case SHORTCUT_MOUSEMODE:
		ShortCut_MouseMode();          /* Toggle mouse mode */
		break;
	 case SHORTCUT_COLDRESET:
		Reset_Cold();                  /* Reset emulator with 'cold' (clear all) */
		break;
	 case SHORTCUT_WARMRESET:
		Reset_Warm();                  /* Emulator 'warm' reset */
		break;
	 case SHORTCUT_SCREENSHOT:
		ScreenSnapShot_SaveScreen();   /* Grab screenshot */
		break;
	 case SHORTCUT_BOSSKEY:
		ShortCut_BossKey();            /* Boss key */
		break;
	 case SHORTCUT_CURSOREMU:          /* Toggle joystick emu on/off */
		Joy_ToggleCursorEmulation();
		break;
	 case SHORTCUT_FASTFORWARD:
		ShortCut_FastForward();       /* Toggle Min/Max speed */
		break;
	 case SHORTCUT_RECANIM:
		ShortCut_RecordAnimation();    /* Record animation */
		break;
	 case SHORTCUT_RECSOUND:
		ShortCut_RecordSound();        /* Toggle sound recording */
		break;
	 case SHORTCUT_SOUND:
		ShortCut_SoundOnOff();         /* Enable/disable sound */
		break;
	 case SHORTCUT_PAUSE:
		ShortCut_Pause();              /* Invoke Pause or Debug UI */
		break;
	 case SHORTCUT_QUIT:
		Main_RequestQuit();
		break;
	 case SHORTCUT_LOADMEM:
		MemorySnapShot_Restore(ConfigureParams.Memory.szMemoryCaptureFileName, TRUE);
		break;
	 case SHORTCUT_SAVEMEM:
		MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName, TRUE);
		break;
	 case SHORTCUT_KEYS:
	 case SHORTCUT_NONE:
		/* ERROR: cannot happen, just make compiler happy */
		break;
	}
	ShortCutKey = SHORTCUT_NONE;
}


/*-----------------------------------------------------------------------*/
/**
 * Invoke shortcut identified by name. This supports only keys for
 * functionality that cannot be invoked with command line options
 * (for remote GUIs etc).
 */
bool Shortcut_Invoke(const char *shortcut)
{
	struct {
		SHORTCUTKEYIDX id;
		const char *name;
	} shortcuts[] = {
		{ SHORTCUT_MOUSEMODE, "mousemode" },
		{ SHORTCUT_COLDRESET, "coldreset" },
		{ SHORTCUT_WARMRESET, "warmreset" },
		{ SHORTCUT_SCREENSHOT, "screenshot" },
		{ SHORTCUT_BOSSKEY, "bosskey" },
		{ SHORTCUT_RECANIM, "recanim" },
		{ SHORTCUT_RECSOUND, "recsound" },
		{ SHORTCUT_SAVEMEM, "savemem" },
		{ SHORTCUT_QUIT, "quit" },
		{ SHORTCUT_NONE, NULL }
	};
	int i;

	if (ShortCutKey != SHORTCUT_NONE)
	{
		fprintf(stderr, "Shortcut invocation failed, shortcut already active\n");
		return FALSE;
	}
	for (i = 0; shortcuts[i].name; i++)
	{
		if (strcmp(shortcut, shortcuts[i].name) == 0)
		{
			ShortCutKey = shortcuts[i].id;
			ShortCut_ActKey();
			ShortCutKey = SHORTCUT_NONE;
			return TRUE;
		}
	}
	fprintf(stderr, "WARNING: unknown shortcut '%s'\n\n", shortcut);
	fprintf(stderr, "Hatari shortcuts are:\n");
	for (i = 0; shortcuts[i].name; i++)
	{
		fprintf(stderr, "- %s\n", shortcuts[i].name);
	}
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether given key was any of the ones in given shortcut array.
 * Return corresponding array index or SHORTCUT_NONE for no match
 */
static SHORTCUTKEYIDX ShortCut_CheckKey(int symkey, int *keys)
{
	SHORTCUTKEYIDX key;
	for (key = 0; key < SHORTCUT_KEYS; key++)
	{
		if (symkey == keys[key])
			return key;
	}
	return SHORTCUT_NONE;
}

/*-----------------------------------------------------------------------*/
/**
 * Check which Shortcut key is pressed/released.
 * If press is set, store the key array index.
 * Return zero if key didn't match to a shortcut
 */
int ShortCut_CheckKeys(int modkey, int symkey, bool press)
{
	SHORTCUTKEYIDX key;

	if (modkey & (KMOD_RALT|KMOD_LMETA|KMOD_RMETA|KMOD_MODE))
		key = ShortCut_CheckKey(symkey, ConfigureParams.Shortcut.withModifier);
	else
		key = ShortCut_CheckKey(symkey, ConfigureParams.Shortcut.withoutModifier);

	if (key == SHORTCUT_NONE)
		return 0;
	if (press)
		ShortCutKey = key;
	return 1;
}
