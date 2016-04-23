/*
 * Tester for Hatari's implementation of Atari debugger
 * XBIOS Dbmsg() API
 * 
 * Test:
 *   hatari --bios-intercept --trace xbios dbmsg.tos
 * 
 * See:
 * - http://dev-docs.atariforge.org/files/Atari_Debugger_1-24-1990.pdf
 * - http://toshyp.atari.org/en/004012.html#Dbmsg
 */
#include "stdint.h"
#include "assert.h"
#include "tos.h"

#define DB_APP_CMD    0x0000
#define DB_COMMAND    0xF100
#define DB_NULLSTRING 0xF000
/* len should be: 1-255 */
#define DB_STRING(len) (DB_NULLSTRING + ((len) & 0xFF))

static void Dbmsg(int16_t reserved, uint16_t msg_num, uint32_t msg_arg)
{
	/* other 'reserved' values than 5 are undefined */
	xbios(11, reserved, msg_num, msg_arg);
}

int main()
{
	const char str[] = "Halting STRING";

	/* Print NULL-terminated string */
	Dbmsg(5, DB_NULLSTRING, (uint32_t)"Please print NULLSTRING");

	/* Print give string (which lenght is encoded to msg_num),
	 * and invoking debugger / halt
	 */
	Dbmsg(5, DB_STRING(sizeof(str)-1), (uint32_t)str);

	/* Print given value and invoke debugger */
	Dbmsg(5, DB_APP_CMD, (uint32_t)0xDEADBEEF);

	/* Give command for debugger to execute.
	 * In Hatari case this is currently same as DB_NULLSTRING.
	 */
	Dbmsg(5, DB_COMMAND, (uint32_t)"echo 'Debugging message';");

	return 0;
}
