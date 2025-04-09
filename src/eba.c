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
 #include <linux/fs.h>          // File operations and device registration
 #include <linux/cdev.h>        // Character device definitions
 #include <linux/device.h>      // Device creation
 #include <linux/uaccess.h>     // Copy to/from user functions
 
 #include "eba.h"               // Public definitions, including ioctl commands
 
 /* Define device name and class names */
 #define DEVICE_NAME "eba"
 #define CLASS_NAME  "eba_class"
 
 /* Module information */
 MODULE_LICENSE("GPL");
 MODULE_AUTHOR("Nader BEN AMMAR");
 MODULE_DESCRIPTION("EBA Kernel Module with IOCTL Support");
 MODULE_VERSION("0.1");
 
 /*
  * eba_init - Module initialization function.
  *
  * This function registers the device, allocates major number, sets up
  * the character device, and creates the device node in /dev/.
  */
 static int __init eba_init(void)
 {
 
     pr_info("EBA: Initializing the module\n");
 

     return 0;
 }
 
 /*
  * eba_exit - Module cleanup function.
  *
  * This function removes the device, unregisters the character device,
  * and frees allocated resources.
  */
 static void __exit eba_exit(void)
 {
     pr_info("EBA: Module unloaded successfully\n");
 }
 
 /*
  * eba_open - Open the device.
  *
  * This function is called each time the device is opened.
  */
 static int eba_open(struct inode *inode, struct file *file)
 {
     pr_info("EBA: Device opened\n");
     return 0;
 }
 
 /*
  * eba_release - Close the device.
  *
  * This function is called when the device is closed.
  */
 static int eba_release(struct inode *inode, struct file *file)
 {
     pr_info("EBA: Device closed\n");
     return 0;
 }