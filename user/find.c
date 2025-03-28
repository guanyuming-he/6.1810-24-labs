#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

// Finds and prints all files of name
// target in dir root, and prints them,
// each on a single line.
// The call is made recursively. At each dir,
// find only searches on root's entries.
// Should any of them be a dir, find recurses into it.
// @returns 0 on success or < 0 on error. If some file or dir cannot be read,
// then it is not an error, but find() will print a msg about that.
// Will print error msg on error.
// @param prefix is used to control output,
// because output has to be relative to the cwd,
// but getcwd is not a syscall.
int find(const char* target, const char* root, const char* prefix)
{
	int rfd = open(root, O_RDONLY);
	if (rfd < 0)
	{
		printf("cannot open %s\n", root);
		// not an error.
		return 0;
	}

	// dirent stands for dir entry.
	// A dir is a file containing a number of dirents.
	struct dirent de;
	// Meta-data of a file.
	struct stat st;

	// Read the meta data of rfd to see what's next.
	if(fstat(rfd, &st) < 0) 
	{
		printf("cannot stat %s\n", root);
		close(rfd);
		return 0;
	}

	// used to contain the output string.
	char buf[512];
	char *p = buf;
	switch (st.type)
	{
	case T_DEVICE:
	case T_FILE:
		fprintf(2, "%s is not a dir.", root);
		return -1;
	
	case T_DIR:
		// DIRSIZ is the maximum length of a dir entry name,
		// i.e. a file name.
		// It's set to 14, the same as in the original Unix paper.
		if (strlen(root) + 1 + DIRSIZ + 1 > sizeof buf)
		{
			printf("path is too long.\n");
			// not an error.
			return 0;
		}

		// put prefix at the beginning of buf.
		strcpy(buf, prefix);
		p = buf + strlen(buf);

		// read each dir entry.
		while(read(rfd, &de, sizeof(de)) == sizeof(de))
		{
			if (0 == de.inum) // somehow the dir stores inode 0
				continue; // ignore.
						 
			// de.name is not ended by 0 if the len is 14.
			// Thus, do a memmove, and append 0 at the end.
			// If the len < 14, then it is ended by 0, and the extra 0
			// does not matter.
			memmove(p, de.name, DIRSIZ);
			*(p + DIRSIZ) = '\0';

			// st is unused now, so just recycle it.
			// Even if we know the inum, we cannot access it this way,
			// and still have to fallback to accessing it with the path.
			if (stat(buf, &st) < 0)
			{
				printf("cannot stat %s", buf);
				continue;
			}

			// now st is the this entry in root.
			switch (st.type)
			{
			case T_DEVICE:
				break;
			case T_FILE:
				// print if the names match.
				if (0 == strcmp(p, target))
				{
					printf("%s\n", buf);
				}
				break;
			case T_DIR:
				// Recurse into the entry,
				// if it's not . or ..
				
				if ((0 == strcmp(".", p)) ||
					(0 == strcmp("..", p))
				) {
					break;
				}
				else 
				{
					// append '/' at then end of prefix.
					char new_prefix[512];
					strcpy(new_prefix, buf);
					int len = strlen(new_prefix);
					new_prefix[len++] = '/';
					new_prefix[len++] = '\0';

					if (find(target, buf, new_prefix) < 0)
						return -1;
				}
				break;
			}
		}
		break;
	}

	close(rfd);
	return 0;
}

int main(int argc, char *argv[])
{
	// root directory in which search is performed.
	const char* root;
	// the file name to find.
	const char* target;
	if (argc == 2)
	{
		// find <file-name>
		root = ".";
		target = argv[1];
	}
	else if (argc == 3)
	{
		// find <root-dir> <file-name>
		root = argv[1];
		target = argv[2];
	}
	else 
	{
		fprintf(2, "invalide number of args");
		exit(-1);
	}

	// root must be a relative path.
	if (root[0] == '/')
	{
		fprintf(2, "root is not relative.");
		exit(-1);
	}

	// prefix is root/.
	char prefix[512];
	strcpy(prefix, root);
	char *p = prefix + strlen(prefix);
	*p = '/';
	*(p+1) = '\0';

	exit(find(target, root, prefix));
}

