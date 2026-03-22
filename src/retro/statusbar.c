/*
  Hatari - statusbar.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Code to draw statusbar area, floppy leds etc.
*/
const char Statusbar_fileid[] = "Hatari statusbar.c";

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "screenSnapShot.h"
#include "statusbar.h"
#include "tos.h"
#include "video.h"
#include "sound.h"
#include "avi_record.h"
#include "vdi.h"
#include "fdc.h"
#include "stMemory.h"
#include "blitter.h"
#include "str.h"
#include "lilo.h"


/**
 * Return statusbar height for given width and height
 */
int Statusbar_GetHeightForSize(int width, int height)
{
	return 0;
}

/**
 * Set screen height used for statusbar height calculation.
 *
 * Return height of statusbar that should be added to the screen
 * height when screen is (re-)created, or zero if statusbar will
 * not be shown
 */
int Statusbar_SetHeight(int width, int height)
{
	return Statusbar_GetHeightForSize(width, height);
}

/**
 * Return height of statusbar set with Statusbar_SetHeight()
 */
int Statusbar_GetHeight(void)
{
	return 0;
}


/**
 * Enable HD drive led, it will be automatically disabled after a while.
 */
void Statusbar_EnableHDLed(drive_led_t state)
{
}

/**
 * Set given floppy drive led state, anything enabling led with this
 * needs also to take care of disabling it.
 */
void Statusbar_SetFloppyLed(drive_index_t drive, drive_led_t state)
{
	assert(drive == DRIVE_LED_A || drive == DRIVE_LED_B);
}


/**
 * Set TOS etc information and initial help message
 */
void Statusbar_InitialSetup(void)
{
}


/**
 * Queue new statusbar message 'msg' to be shown for 'msecs' milliseconds
 */
void Statusbar_AddMessage(const char *msg, uint32_t msecs)
{
}

/**
 * Retrieve/update default statusbar information
 */
void Statusbar_UpdateInfo(void)
{
}
