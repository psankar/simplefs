/*
 * A Simple Filesystem for the Linux Kernel.
 *
 * Initial author: Sankar P <sankar.curiosity@gmail.com>
 * License: Creative Commons Zero License - http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <linux/init.h>
#include <linux/module.h>

static int simplefs_init(void)
{
        printk(KERN_ALERT "Hello World\n");
        return 0;
}

static void simplefs_exit(void)
{
        printk(KERN_ALERT "Goodbye World\n");
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("CC0");
MODULE_AUTHOR("Sankar P");
