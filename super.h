#include <simple.h>

static inline struct simplefs_super_block *SIMPLEFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}
