#define SIMPLEFS_MAGIC 0x10032013
#define SIMPLEFS_DEFAULT_BLOCK_SIZE 4096
#define SIMPLEFS_FILENAME_MAXLEN 255

/* Hard-coded inode number for the root directory */
const int SIMPLEFS_ROOT_INODE_NUMBER = 1;

/* The disk block where super block is stored */
const int SIMPLEFS_SUPERBLOCK_BLOCK_NUMBER = 0;

/* The disk block where the inodes are stored */
const int SIMPLEFS_INODESTORE_BLOCK_NUMBER = 1;

/* The disk block where the name+inode_number pairs of the
 * contents of the root directory are stored */
const int SIMPLEFS_ROOTDIR_DATABLOCK_NUMBER = 2;

/* The name+inode_number pair for each file in a directory.
 * This gets stored as the data for a directory */
struct simplefs_dir_record {
	char filename[SIMPLEFS_FILENAME_MAXLEN];
	uint32_t inode_no;
};

struct simplefs_inode {
	mode_t mode;
	uint32_t inode_no;
	uint32_t data_block_number;

	union {
		uint32_t file_size;
		uint32_t dir_children_count;
	};
};

struct simplefs_super_block {
	uint32_t version;
	uint32_t magic;
	uint32_t block_size;
	uint32_t free_blocks;

	struct simplefs_inode root_inode;

	char padding[SIMPLEFS_DEFAULT_BLOCK_SIZE - (4 * sizeof(uint32_t)) - sizeof(struct simplefs_inode)];
};
