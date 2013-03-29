const int SIMPLEFS_MAGIC = 0x10032013;
const int SIMPLEFS_DEFAULT_BLOCK_SIZE = 4 * 1024;

struct simplefs_super_block {
	unsigned int version;
	unsigned int magic;
	unsigned int block_size;
	unsigned int free_blocks;

	char padding[ (4 * 1024) - (4 * sizeof(unsigned int))];
};
