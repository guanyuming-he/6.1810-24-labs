// the two kernel headers are needed for various
// definitions.
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

int main()
{
	int imm_pid, snd_pid;
	if ((imm_pid = fork()) != 0)
	{
		fprintf(1, "imme-child: %d\n", imm_pid);

		if ((snd_pid = fork()) != 0)
		{
			fprintf(1, "non-imme-child: %d\n", snd_pid);
		}
		else { exit(0); }

		int wait_1 = wait(0);
		int wait_2 = wait(0);

		fprintf(1, "wait1: %d\n", wait_1);
		fprintf(2, "wait1: %d\n", wait_2);
		 
		exit(0);
	}
	else { exit(0); }

}
