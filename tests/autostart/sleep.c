/* sleep hard coded amount of secs */
#include <unistd.h>
#include <tos.h>

int main()
{
	Cconws("Sleeping "SECS" secs.\r\n");
	sleep(SLEEP);
	return 0;
}
