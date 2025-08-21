/* dumb tool to sleep number of secs specified in the program name */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <tos.h>

int main()
{
	int ret, secs = 0;
	const char *pos;
	DTA dta;

	/* locate program name from AUTO/ folder
	 * (as argv[0] is missing under TOS).
	 */
	Fsetdta(&dta);
	ret = Fsfirst("\\AUTO\\??_SLEEP.PRG", 0);

	/* found -> extract seconds value from name */
	if (ret == 0) {
		printf("Found '%s'.\n", dta.d_fname);
		pos = strrchr(dta.d_fname, '\\');
		if (!pos) {
			pos = dta.d_fname;
		}
		secs = atoi(pos);
	}
	/* sleep or complain */
	if (secs > 0) {
		printf("=> Sleeping %d seconds.\n", secs);
		sleep(secs);
	} else {
		printf("No \\AUTO\\<secs>_SLEEP.PRG program found!\n");
	}
	return 0;
}
