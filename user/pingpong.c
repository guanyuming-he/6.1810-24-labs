// the two kernel headers are needed for various
// definitions.
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

int main()
{
	int parent_pip[2];
	if (pipe(parent_pip) < 0)
		exit(-1);
	int child_pip[2];
	if (pipe(child_pip) < 0)
		exit(-1);

	const char child_msg[] =
		": received ping\n";
	const char parent_msg[] =
		": received pong\n";
	
	// can be anything.
	char buf[1];

	if (fork() == 0)
	{
		read(child_pip[0], buf, 1);	
		close(child_pip[0]);

		// write msg.
		int pid = getpid();
		char pid_str[9];
		itoa(pid, pid_str, 9);
		write(1, pid_str, strlen(pid_str));
		write(1, child_msg, sizeof(child_msg)-1);

		// send parent the signal.
		write(parent_pip[1], buf, 1);
		close(parent_pip[1]);
		
		exit(0);
	}
	// parent
	// send child the signal.
	write(child_pip[1], buf, 1);
	close(child_pip[1]);
	// wait for child to write the byte.
	read(parent_pip[0], buf, 1);
	close(parent_pip[0]);

	// write msg.
	int pid = getpid();
	char pid_str[9];
	itoa(pid, pid_str, 9);
	write(1, pid_str, strlen(pid_str));
	write(1, parent_msg, sizeof(parent_msg)-1);

	exit(0);
}
