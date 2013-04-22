#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simple.h"

int main(int argc, char *argv[])
{
	int fd, nbytes;
	ssize_t ret;
	struct simplefs_super_block sb;
	struct simplefs_inode root_inode;
	struct simplefs_inode welcomefile_inode;

	char welcomefile_name[] = "vanakkam";
	char welcomefile_body[] = "Love is God. God is Love. Anbe Murugan.";
	const uint64_t WELCOMEFILE_INODE_NUMBER = 2;
	const uint64_t WELCOMEFILE_DATABLOCK_NUMBER = 3;

	char *block_padding;

	struct simplefs_dir_record record;

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

	/* One inode for rootdirectory and another for a welcome file that we are going to create */
	sb.inodes_count = 2;

	/* FIXME: Free blocks management is not implemented yet */
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
	root_inode.dir_children_count = 1;

	ret = write(fd, (char *)&root_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf("The inode store was not written properly. Retry your mkfs\n");
		ret = -1;
		goto exit;
	}
	printf("root directory inode written succesfully\n");

	welcomefile_inode.mode = S_IFREG;
	welcomefile_inode.inode_no = WELCOMEFILE_INODE_NUMBER;
	welcomefile_inode.data_block_number = WELCOMEFILE_DATABLOCK_NUMBER;
	welcomefile_inode.file_size = sizeof(welcomefile_body);
	ret = write(fd, (char *)&welcomefile_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf("The welcomefile inode was not written properly. Retry your mkfs\n");
		ret = -1;
		goto exit;
	}
	printf("welcomefile inode written succesfully\n");

	nbytes = SIMPLEFS_DEFAULT_BLOCK_SIZE - sizeof(root_inode) - sizeof(welcomefile_inode);
	block_padding = malloc(nbytes);
	
	ret = write(fd, block_padding, nbytes);

	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		ret = -1;
		goto exit;
	}
	printf("inode store padding bytes (after the two inodes) written sucessfully\n");

	strcpy(record.filename, welcomefile_name);
	record.inode_no = WELCOMEFILE_INODE_NUMBER;
	nbytes = sizeof(record);

	ret = write(fd, (char *)&record, nbytes);
	if (ret != nbytes) {
		printf("Writing the rootdirectory datablock (name+inode_no pair for welcomefile) has failed\n");
		ret = -1;
		goto exit;
	}
	printf("root directory datablocks (name+inode_no pair for welcomefile) written succesfully\n");

	nbytes = SIMPLEFS_DEFAULT_BLOCK_SIZE - sizeof(record);
	block_padding = realloc(block_padding, nbytes);

	ret = write(fd, block_padding, nbytes);
	if (ret != nbytes) {
		printf("Writing the padding for rootdirectory children datablock has failed\n");
		ret = -1;
		goto exit;
	}
	printf("padding after the rootdirectory children written succesfully\n");

	nbytes = sizeof(welcomefile_body);
	ret = write(fd, welcomefile_body, nbytes);
	if (ret != nbytes) {
		printf("Writing welcomefile body has failed\n");
		ret = -1;
		goto exit;
	}
	printf("welcomefilebody has been written succesfully\n");

	ret = 0;

exit:
	close(fd);
	return ret;
}
