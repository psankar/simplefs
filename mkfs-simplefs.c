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
	struct simplefs_inode root_inode;

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

	/* Only the root dir will have an inode now */
	sb.inodes_count = 1;
	sb.free_blocks = ~0;

	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != SIMPLEFS_DEFAULT_BLOCK_SIZE) {
		printf
		    ("bytes written [%d] are not equal to the default block size\n",
		     (int)ret);
		ret = -1;
		goto exit;
	}

	printf("Super block written succesfully\n");

	root_inode.mode = S_IFDIR;
	root_inode.inode_no = SIMPLEFS_ROOTDIR_INODE_NUMBER;
	root_inode.data_block_number = SIMPLEFS_ROOTDIR_DATABLOCK_NUMBER;
	root_inode.dir_children_count = 0;

	ret = write(fd, (char *)&root_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf("The inode store was not written properly. Retry your mkfs\n");
		ret = -1;
		goto exit;
	}
	printf("inode store written succesfully\n");
	ret = 0;

exit:
	close(fd);
	return ret;
}
