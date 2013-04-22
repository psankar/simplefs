#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include "simple.h"

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;
	struct simplefs_super_block sb;

	if (argc != 2) {
		printf("Usage: mkfs-simplefs <device>\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	sb.version = 1;
	sb.magic = SIMPLEFS_MAGIC;
	sb.block_size = SIMPLEFS_DEFAULT_BLOCK_SIZE;
	sb.free_blocks = ~0;

	sb.root_inode.mode = S_IFDIR;
	sb.root_inode.inode_no = SIMPLEFS_ROOT_INODE_NUMBER;
	sb.root_inode.data_block_number = SIMPLEFS_ROOTDIR_DATABLOCK_NUMBER;
	sb.root_inode.dir_children_count = 0;

	ret = write(fd, (char *)&sb, sizeof(sb));

	/* Just a redundant check. Not required ideally. */
	if (ret != SIMPLEFS_DEFAULT_BLOCK_SIZE)
		printf
		    ("bytes written [%d] are not equal to the default block size\n",
		     (int)ret);
	else
		printf("Super block written succesfully\n");

	close(fd);

	return 0;
}
