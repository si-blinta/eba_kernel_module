/*
 * eba.c - EBA Kernel Module with IOCTL Support
 *
 * This module demonstrates a simple character device that supports
 * ioctl commands. It registers a device in /dev/eba, which user programs
 * can open and interact with via ioctl(2).
 *
 * Author: Nader BEN AMMAR
 * License: GPL
 * Version: 1.0
 */

#include <linux/module.h>      // Core header for loading LKMs into the kernel
#include <linux/init.h>        // Macros used to mark up functions e.g., __init, __exit
#include "eba_internals.h"
 
#include "eba.h"               // Public definitions, including ioctl commands
 /* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nader BEN AMMAR");
MODULE_DESCRIPTION("EBA Kernel Module with IOCTL Support");
MODULE_VERSION("0.1");
 
static int __init eba_module_init(void)
{
    int ret;

    ret = eba_internals_mempool_init();
    if (ret)
         return ret;

    /* Optional: Run the stress test immediately (for testing only) */
    ret = eba_internals_memory_stress();
    if (ret)
         pr_err("EBA: Memory stress test failed\n");
    else
         pr_info("EBA: Memory stress test passed\n");

    pr_info("EBA: Module loaded\n");
    return 0;
}

static void __exit eba_module_exit(void)
{
    eba_internals_mempool_cleanup();
    pr_info("EBA: Module unloaded\n");
}
module_init(eba_module_init);
module_exit(eba_module_exit);
