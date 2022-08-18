/*
 * events.c
 *
 * Event stuff - currently just simplified to the bare minimum
 * in Hatari, but we might want to extend this one day...
 */

#include "main.h"

#include "sysconfig.h"
#include "sysdeps.h"

#include "options_cpu.h"
#include "events.h"
#include "newcpu.h"

void do_cycles_slow (int cycles_to_add)
{
//fprintf ( stderr , "  do_cycles_slow add=%d curr=%d -> new=%d\n" , cycles_to_add , currcycle , currcycle+cycles_to_add );
	currcycle += cycles_to_add;
}
