/*
 * eba.c - EBA Kernel Module with IOCTL Support
 *
 * This module demonstrates a simple character device that supports IOCTL commands.
 * It registers a device node (/dev/eba) so that user programs can interact with the
 * memory pool via IOCTL calls for allocation, read, and write operations.
 *
 * Author: Nader BEN AMMAR
 * License: GPL
 * Version: 1.0
 */

 #include <linux/module.h>       /* Core header for loading LKMs into the kernel */
 #include <linux/init.h>         /* Macros: __init, __exit */
 #include <linux/fs.h>           /* File operations, dev_t, etc. */
 #include <linux/cdev.h>         /* Character device registration */
 #include <linux/device.h>       /* Device class creation */
 #include <linux/uaccess.h>      /* copy_to_user(), copy_from_user() */
 #include <linux/slab.h>         /* kmalloc() and kfree() */
 #include <linux/string.h>       /* memcpy(), memcmp() */
 #include <linux/timer.h>
 #include "eba_internals.h"      /* Internal memory pool functions */
 #include "eba.h"                /* Public definitions, IOCTL commands, structures */
 #include "eba_net.h"
 #include "ebp.h"
 
 /* Global variables for character device registration */
 static dev_t eba_devno;
 static struct cdev eba_cdev;
 static struct class *eba_class = NULL;
 
 /* IOCTL handler: dispatches commands to allocation, read, and write operations */
 static long eba_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
 {
     int ret = 0;
     struct eba_alloc_data alloc_data;
     struct eba_rw_data rw_data;
     void *kbuf; /* Temporary kernel buffer for data transfers */
 
     switch (cmd) {
     case EBA_IOCTL_ALLOC:
          /* Copy the allocation parameters from user space */
          if (copy_from_user(&alloc_data, (void __user *)arg, sizeof(alloc_data)))
               return -EFAULT;
 
          /* Call internal allocation; buff_id will hold the virtual address */
          alloc_data.buff_id = (uint64_t)eba_internals_malloc(alloc_data.size,
                                                               alloc_data.life_time);
          if (!alloc_data.buff_id)
               ret = -ENOMEM;
 
          /* Return the allocation info (including buff_id) to user space */
          if (copy_to_user((void __user *)arg, &alloc_data, sizeof(alloc_data)))
               ret = -EFAULT;
          break;
 
     case EBA_IOCTL_WRITE:
          /* Copy the read/write structure from user space */
          if (copy_from_user(&rw_data, (void __user *)arg, sizeof(rw_data)))
               return -EFAULT;
 
          /* Allocate a temporary kernel buffer for the write data */
          kbuf = kmalloc(rw_data.size, GFP_KERNEL);
          if (!kbuf)
               return -ENOMEM;
          /* Copy the data from the user-supplied pointer into the kernel buffer */
          if (copy_from_user(kbuf, (void __user *)(rw_data.user_addr), rw_data.size)) {
               kfree(kbuf);
               return -EFAULT;
          }
 
          /* Call the internal write function */
          ret = eba_internals_write(kbuf, rw_data.buff_id, rw_data.off, rw_data.size);
          kfree(kbuf);
          break;
 
     case EBA_IOCTL_READ:
          /* Copy the read/write structure from user space */
          if (copy_from_user(&rw_data, (void __user *)arg, sizeof(rw_data)))
               return -EFAULT;
 
          /* Allocate a temporary kernel buffer to hold the read data */
          kbuf = kmalloc(rw_data.size, GFP_KERNEL);
          if (!kbuf)
               return -ENOMEM;
 
          /* Call the internal read function to copy data into the kernel buffer */
          ret = eba_internals_read(kbuf, rw_data.buff_id, rw_data.off, rw_data.size);
          if (ret) {
               kfree(kbuf);
               return ret;
          }
 
          /* Copy the data from the kernel buffer to the user-supplied pointer */
          if (copy_to_user((void __user *)(rw_data.user_addr), kbuf, rw_data.size))
               ret = -EFAULT;
          kfree(kbuf);
          break;
 
     default:
          ret = -ENOTTY;
          break;
     }
     return ret;
 }
 
/* Timer for checking expired buffers */
static struct timer_list eba_timer;

/* Timer callback function */
static void eba_timer_callback(struct timer_list *t)
{
    eba_check_expired_buffers();
    mod_timer(&eba_timer, jiffies + msecs_to_jiffies(EBA_CLEAN_BUFFER_CALLBACK_TIMER)); // reschedule for next second
}

 static int eba_open(struct inode *inode, struct file *file)
 {
     if (!try_module_get(THIS_MODULE))
          return -ENODEV;
     return 0;
 }
 
 static int eba_release(struct inode *inode, struct file *file)
 {
     module_put(THIS_MODULE);
     return 0;
 }
 
 
 /* File operations structure */
 static const struct file_operations eba_fops = {
     .owner = THIS_MODULE,
     .open = eba_open,
     .release = eba_release,
     .unlocked_ioctl = eba_ioctl,
 };
 
 /* Module initialization: register character device and initialize memory pool */
 static int __init eba_module_init(void)
 {
     int ret;
     /* Allocate a character device region */
     ret = alloc_chrdev_region(&eba_devno, 0, 1, "eba");
     if (ret < 0)
          goto err_alloc;
 
     /* Initialize and add the character device */
     cdev_init(&eba_cdev, &eba_fops);
     eba_cdev.owner = THIS_MODULE;
     ret = cdev_add(&eba_cdev, eba_devno, 1);
     if (ret)
          goto err_cdev;
 
     /* Create a device class using the single-argument version of class_create() */
     eba_class = class_create("eba_class");
     if (IS_ERR(eba_class)) {
          ret = PTR_ERR(eba_class);
          goto err_class;
     }
     /* Create the device node /dev/eba */
     device_create(eba_class, NULL, eba_devno, NULL, "eba");
 
     /* Initialize the memory pool */
     ret = eba_internals_mempool_init();
     if (ret) {
          pr_err("EBA: Mempool Init failed\n");
          goto err_mempool;
     }
     // Initialize the timer:
     timer_setup(&eba_timer, eba_timer_callback, 0);
     // Schedule the timer for the first time
     mod_timer(&eba_timer, jiffies + msecs_to_jiffies(EBA_CLEAN_BUFFER_CALLBACK_TIMER));
     
     ebp_init();
     pr_info("EBA: Module loaded\n");
     return 0;
 
 err_mempool:
     device_destroy(eba_class, eba_devno);
     class_destroy(eba_class);
 err_class:
     cdev_del(&eba_cdev);
 err_cdev:
     unregister_chrdev_region(eba_devno, 1);
 err_alloc:
     return ret;
 }
 
 /* Module cleanup: free memory pool and unregister character device */
 static void __exit eba_module_exit(void)
 {
     del_timer_sync(&eba_timer);
     eba_internals_mempool_free();
     device_destroy(eba_class, eba_devno);
     class_destroy(eba_class);
     cdev_del(&eba_cdev);
     unregister_chrdev_region(eba_devno, 1);
     ebp_exit();
     pr_info("EBA: Module unloaded\n");
 }
 module_init(eba_module_init);
 module_exit(eba_module_exit);
 
 MODULE_LICENSE("GPL");
 MODULE_AUTHOR("Nader BEN AMMAR");
 MODULE_DESCRIPTION("EBA Kernel Module with IOCTL Support");
 MODULE_VERSION("0.1");