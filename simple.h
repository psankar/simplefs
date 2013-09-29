

#define SIMPLEFS_MAGIC 0x10032013
#define SIMPLEFS_DEFAULT_BLOCK_SIZE 4096
#define SIMPLEFS_FILENAME_MAXLEN 255
#define SIMPLEFS_START_INO 10
/**
 * Reserver inodes for super block, inodestore
 * and datablock
 */
#define SIMPLEFS_RESERVED_INODES 3

#ifdef SIMPLEFS_DEBUG
#define sfs_trace(fmt, ...) {                       \
	printk(KERN_ERR "[simplefs] %s +%d:" fmt,       \
	       __FILE__, __LINE__, ##__VA_ARGS__);      \
}
#define sfs_debug(level, fmt, ...) {                \
	printk(level "[simplefs]:" fmt, ##__VA_ARGS__); \
}
#else
#define sfs_trace(fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#define sfs_debug(level, fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#endif

/* Hard-coded inode number for the root directory */
const int SIMPLEFS_ROOTDIR_INODE_NUMBER = 1;

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
	uint64_t inode_no;
};

struct simplefs_inode {
	mode_t mode;
	uint64_t inode_no;
	uint64_t data_block_number;

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

const int SIMPLEFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED = 64;
/* min (
		SIMPLEFS_DEFAULT_BLOCK_SIZE / sizeof(struct simplefs_inode),
		sizeof(uint64_t) //The free_blocks tracker in the sb
 	); */

/* FIXME: Move the struct to its own file and not expose the members
 * Always access using the simplefs_sb_* functions and
 * do not access the members directly */
struct simplefs_super_block {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;

	/* FIXME: This should be moved to the inode store and not part of the sb */
	uint64_t inodes_count;

	uint64_t free_blocks;

	char padding[SIMPLEFS_DEFAULT_BLOCK_SIZE - (5 * sizeof(uint64_t))];
};
