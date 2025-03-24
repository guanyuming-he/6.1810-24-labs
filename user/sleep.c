#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

void main(int argc, char *argv[])
{
	const char err_nenough_args[] = 
		"Time to sleep not given.\n";
	const char err_sleep[] =
		"Sleep failed.\n";

	if (argc < 2)
	{
		write(1, err_nenough_args, sizeof(err_nenough_args)-1);
		exit(-1);
	}	

	// by reading the code,
	// atoi only handlers characters between '0' and '9'.
	// If any other is encountered, it stops.
	// If nothing is read, then 0 is returned.
	int time = atoi(argv[1]);
	if (sleep(time) < 0)
	{
		write(1, err_sleep, sizeof(err_sleep)-1);
	}

	exit(0);
}
