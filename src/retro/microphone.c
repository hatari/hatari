/*
  Hatari - microphone.c

  microphone (jack connector) emulation (Falcon mode only)

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"

#include "microphone.h"
#include "configuration.h"
#include "crossbar.h"
#include "log.h"


/**
 * Microphone (jack) inits : init microphone emulation
 *   - sampleRate : system sound frequency
 * return true on success, false on error or if mic disabled
 */
bool Microphone_Start(int sampleRate)
{
	return false;
}

/**
 * Microphone (jack) stop : stops the current recording
 */
void Microphone_Stop(void)
{
}
