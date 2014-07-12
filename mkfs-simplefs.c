#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simple.h"

const uint64_t WELCOMEFILE_DATABLOCK_NUMBER = 3;
const uint64_t WELCOMEFILE_INODE_NUMBER = 2;

static int write_superblock(int fd)
{
	struct simplefs_super_block sb = {
		.version = 1,
		.magic = SIMPLEFS_MAGIC,
		.block_size = SIMPLEFS_DEFAULT_BLOCK_SIZE,
		/* One inode for rootdirectory and another for a welcome file that we are going to create */
		.inodes_count = 2,
		/* FIXME: Free blocks management is not implemented yet */
		.free_blocks = (~0) & ~(1 << WELCOMEFILE_DATABLOCK_NUMBER),
	};
	ssize_t ret;

	ret = write(fd, &sb, sizeof(sb));
	if (ret != SIMPLEFS_DEFAULT_BLOCK_SIZE) {
		printf
		    ("bytes written [%d] are not equal to the default block size\n",
		     (int)ret);
		return -1;
	}

	printf("Super block written succesfully\n");
	return 0;
}

static int write_inode_store(int fd)
{
	ssize_t ret;

	struct simplefs_inode root_inode;

	root_inode.mode = S_IFDIR;
	root_inode.inode_no = SIMPLEFS_ROOTDIR_INODE_NUMBER;
	root_inode.data_block_number = SIMPLEFS_ROOTDIR_DATABLOCK_NUMBER;
	root_inode.dir_children_count = 1;

	ret = write(fd, &root_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf
		    ("The inode store was not written properly. Retry your mkfs\n");
		return -1;
	}

	printf("root directory inode written succesfully\n");
	return 0;
}

static int write_inode(int fd, const struct simplefs_inode *i)
{
	off_t nbytes;
	ssize_t ret;

	ret = write(fd, i, sizeof(*i));
	if (ret != sizeof(*i)) {
		printf
		    ("The welcomefile inode was not written properly. Retry your mkfs\n");
		return -1;
	}
	printf("welcomefile inode written succesfully\n");

	nbytes = SIMPLEFS_DEFAULT_BLOCK_SIZE - sizeof(*i) - sizeof(*i);
	ret = lseek(fd, nbytes, SEEK_CUR);
	if (ret == (off_t)-1) {
		printf
		    ("The padding bytes are not written properly. Retry your mkfs\n");
		return -1;
	}

	printf
	    ("inode store padding bytes (after the two inodes) written sucessfully\n");
	return 0;
}
int write_dirent(int fd, const struct simplefs_dir_record *record)
{
	ssize_t nbytes = sizeof(*record), ret;

	ret = write(fd, record, nbytes);
	if (ret != nbytes) {
		printf
		    ("Writing the rootdirectory datablock (name+inode_no pair for welcomefile) has failed\n");
		return -1;
	}
	printf
	    ("root directory datablocks (name+inode_no pair for welcomefile) written succesfully\n");

	nbytes = SIMPLEFS_DEFAULT_BLOCK_SIZE - sizeof(*record);
	ret = lseek(fd, nbytes, SEEK_CUR);
	if (ret == (off_t)-1) {
		printf
		    ("Writing the padding for rootdirectory children datablock has failed\n");
		return -1;
	}
	printf
	    ("padding after the rootdirectory children written succesfully\n");
	return 0;
}
int write_block(int fd, char *block, size_t len)
{
	ssize_t ret;

	ret = write(fd, block, len);
	if (ret != len) {
		printf("Writing file body has failed\n");
		return -1;
	}
	printf("block has been written succesfully\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;

	char welcomefile_body[] = "Love is God. God is Love. Anbe Murugan.\n";
	struct simplefs_inode welcome = {
		.mode = S_IFREG,
		.inode_no = WELCOMEFILE_INODE_NUMBER,
		.data_block_number = WELCOMEFILE_DATABLOCK_NUMBER,
		.file_size = sizeof(welcomefile_body),
	};
	struct simplefs_dir_record record = {
		.filename = "vanakkam",
		.inode_no = WELCOMEFILE_INODE_NUMBER,
	};

	if (argc != 2) {
		printf("Usage: mkfs-simplefs <device>\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	ret = 1;
	do {
		if (write_superblock(fd))
			break;
		if (write_inode_store(fd))
			break;

		if (write_inode(fd, &welcome))
			break;
		if (write_dirent(fd, &record))
			break;
		if (write_block(fd, welcomefile_body, welcome.file_size))
			break;

		ret = 0;
	} while (0);

	close(fd);
	return ret;
}
