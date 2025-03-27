// the two kernel headers are needed for various
// definitions.
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

// helper function
void panic(const char* msg)
{
	write(1, msg, strlen(msg));
	exit(-1);
}

/**
 * Idea: first processor (call it P0)
 * 1. creates pipe pip_0 for it and the first prime process (call it P2) and
 * 	then forks into P2.
 * 2. starts feeding numbers to P2.
 * 3. wait until P2 terminates.
 *
 * P2, and all other prime processes 
 * 1. remembers its prime number, which is
 * 	passed to it by its left.
 * 2. reads int from pip_0, which connects to its left and it.
 * 	if its prime does not divide this i, then create pip_1, and fork
 * 	into a child to which pip_1 feeds into, and give the child the i.
 *
 * Note that all prime processes will 
 * reuse variables pip_0 and pip_1 such that pip_0 always is the one
 * connecting its left and it, and pip_1 the one connecting it and its right.
 *
 * To pass the prime to the child, we could in principle use two variables,
 * my_pr and your_pr.
 * Before forking, the left sets your_pr.
 * When the new processes sees light, it copies the your_pr into my_pr.
 * And when it is ready to hand out the next undivisible number, it sets 
 * your_pr to it.
 *
 * The instructions implied using a function,
 * but I feel that the call stack is simply wasted this way.
 *
 */

// assigns right to left.
void pip_assign(int* left, int* right)
{
	*left = *right;
	*(left+1) = *(right+1);
}
void pip_close(int* pip)
{
	close(*pip); close(*(pip+1));
}

int main()
{
	// variables used for communication.
	int pip_0[2];
	int pip_1[2];
	int my_pr = -1, your_pr = -1;

	const char* pip_err_msg = "Cannot create pipe.";

	if (pipe(pip_0) < 0)
		panic(pip_err_msg);

	if (fork() != 0)
	// number-feeding proc. 
	{
		// close recv end as soon as we can.
		// can't do it before, because child needs it.
		close(pip_0[0]);
		for (int i = 2; i <= 280; ++i)
		{
			write(pip_0[1], &i, sizeof(int));
		}
		close(pip_0[1]);
		// wait until all children are finished,
		// this is because wait() also waits for
		// non-immediate children, as demonstrated in 
		// ./test_wait.c.
		// i.e. until wait() returns -1.
		while (wait(0) != -1) {}

		exit(0);
	}
	else // first prime proc.
	{
		your_pr = 2;
		// here pip_1 is not initialized
		// and pip_0 connects it to its left.
		// to satisfy the following invariant,
		// pip_1 <- pip_0.
		pip_assign(pip_1, pip_0);
		// and we need to at least give pip_0[0] some other value to close.
		pip_0[0] = dup(pip_1[0]);

// invariant before prime_start:
// Your_pr is the prime the new process should have.
// Both ends of pip_1 are valid and 
// are connected to its left.
// The sending end, pip_0[1], is closed.
// The receiving end of pip_0 is valid and needs to be closed in the child.
// We cannot close it as parent, because at the moment of forking,
// the parent may still need to receive from its left.
prime_start:
		my_pr = your_pr;
		// print self prime
		fprintf(1, "prime %d\n", my_pr);

		// we want to establish the following invariant
		// before receiving from the left:
		// pip_0 connects to its left and it, and its sending
		// end is closed.
		// pip_1 is unused.
		//
		// To do that, first close pip_0[0]
		close(pip_0[0]);
		// Then, pip_0 <- pip_1;
		pip_assign(pip_0, pip_1);
		// then close pip_0 sending end.
		close(pip_0[1]);

		// on first indivisible, we have a prime,
		// then create a process on the right.
		// the following indivisibles are simply passed to the process.
		int right_created = 0;

		int num;
		// read returns 0 if there's no more input
		int no_more_input;
		do 
		{
			no_more_input = read(pip_0[0], &num, sizeof(int));

			if (num % my_pr != 0)
			{

				if (!right_created) // need to create a new proc.
				{
					// Now we have an indivisible number.
					//
					// Before forking into a child, we want to establish the
					// invariant before prime_start.
					// 
					// create pip_1 to connect it with its child.
					if (pipe(pip_1) < 0)
						panic(pip_err_msg);
					// then set the prime number.
					your_pr = num;
					// we can now start the new process
					if (fork() == 0)
					{
						goto prime_start;
					}
					else
					{
						// now we need to communicate with both the left and the
						// right. Here, the invariant should be:
						// pip_0[1] is closed. pip_0[0] is open.
						// pip_1[0] is closed. pip_1[1] is open.
						close(pip_1[0]);
					}

					right_created = 1;
				}
				else 
				{
					// the right already has a process,
					// simply pass the number down.
					write(pip_1[1], &num, sizeof(int));
				}
			}
		}
		while (no_more_input != 0);

		// clean up.
		// this process co-shared pip_0,
		// and also pip_1, if right_created = true.
		close(pip_0[0]); 
		if (right_created)
		{ close(pip_1[1]); }

		// we could exit straight away in principle.
		// However, as the shell in xv6 only waits if one of the children
		// (including our children) exits, exiting before all primes are printed 
		// will cause disrupted output.
		// As such, I will wait for all children to exit.
		// This way, the right most exit first, the second right most exit
		// second, and so on.
		if (right_created)
			while (wait(0) != -1);

		exit(0);
	}
}
