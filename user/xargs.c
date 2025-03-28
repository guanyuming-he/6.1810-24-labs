#include "../kernel/types.h"
#include "../kernel/stat.h"
// for MAXARG
#include "../kernel/param.h"
#include "../user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int main(int argc, char* argv[])
{
	// a simple trick to convert
	// a list of args separated by space into
	// an array argv of char*:
	// make each space into '\0' and each
	// argv[i] points to the corresponding word in the whole string.
	// This way, no additional buffer is needed.
	// I will then have to assume a single space separates the args.
	
	// argv[0] is xargs.
	// argv[1] is cmd.
	// argv[2:] is initial args for cmd.
	if (argc < 2)
	{
		fprintf(2, "No command given.");
		exit(-1);
	}

	const char* const cmd = argv[1];
	char* args[MAXARG];
	// copy initial args.
	// be sure to copy the cmd name as well.
	for (int i = 1; i < argc; ++i)
	{
		args[i-1] = argv[i];
	}

	// new args start at args[argc-1].
	
	// now, read from stdin.
	// for each line, append that as new arguments to args,
	// and then fork and execute cmd for them.
	
	// assumes a line is less than 512 chars
	char buf[512];
	int i = 0;
	char ch;
	int eof = 0;
	
	while (!eof)
	{
		i = 0;

		for(;;)
		{
			if (0 != read(0, &ch, 1))
			{
				// read characters 1-by-1
				// to buf until we encounter a \n;
				
				if (i >= 512)
				{
					fprintf(2, "line too long!");
					exit(-1);
				}

				if ('\n' == ch)
				{
					buf[i] = '\0';
					break;
				}
				else 
				{
					buf[i++] = ch;
				}
			}
			else // end of file.
			{
				eof = 1;
				buf[i] = '\0';
				break;
			}
		}

		if (i == 0)
		{
			// file ended with '\n'
			break;
		}
		
		// invariant when exiting for loop:
		// buf contains the line of extra arguments,
		// and strlen(buf) = i. 

		// make the entire line the new cmd.
		args[argc-1] = buf;
		// end it with 0 so that exec can deduce argc for the new process.
		args[argc] = 0;
		// fork and execute.
		if (fork() == 0)
		{
			// child: exec.
			exec(cmd, args);
			// no need to exit. whole memory
			// is replaced by the new cmd.
			// But if the flow ever reaches here,
			// it means exec has failed.
			fprintf(2, "exec %s has failed.", cmd);
			exit(-1);
		}
		else 
		{
			// Parent. It should wait, because otherwise new output may be
			// written before the child writes its own output.
			// In principle, I should wait until all children finish.
			// But I don't know why in sh only it only waits for its own forks.
			wait(0);

			// then return to the loop.
		}

	}

	exit(0);

}
