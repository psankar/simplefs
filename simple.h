const int SIMPLEFS_MAGIC = 0x10032013;
const int SIMPLEFS_DEFAULT_BLOCK_SIZE = 4 * 1024;

struct simplefs_super_block {
	uint32_t version;
	uint32_t magic;
	uint32_t block_size;
	uint32_t free_blocks;

	char padding[(4 * 1024) - (4 * sizeof(uint32_t))];
};
