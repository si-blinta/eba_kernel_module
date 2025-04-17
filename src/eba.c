/*
 * eba.c - EBA Kernel Module with IOCTL Support.
 */

#include <linux/module.h>  /* Core header for loading LKMs into the kernel */
#include <linux/init.h>    /* Macros: __init, __exit */
#include <linux/fs.h>      /* File operations, dev_t, etc. */
#include <linux/cdev.h>    /* Character device registration */
#include <linux/device.h>  /* Device class creation */
#include <linux/uaccess.h> /* copy_to_user(), copy_from_user() */
#include <linux/slab.h>    /* kmalloc() and kfree() */
#include <linux/string.h>  /* memcpy(), memcmp() */
#include <linux/timer.h>   /* struct timer_list */
#include "eba_internals.h" /* Internal memory pool functions */
#include "eba.h"           /* Public definitions, IOCTL commands, structures */
#include "eba_net.h"       /* network helpers */
#include "ebp.h"           /* protocol */
#include "eba_utils.h"     /* buff <-> file helpers */

/* Global variables for character device registration */
static dev_t eba_devno;
static struct cdev eba_cdev;
static struct class *eba_class = NULL;

/*
 * IOCTL handler --------------------------------------------------------
 */
/**
 * eba_ioctl() - dispatch IOCTL requests coming from user space
 * @file: file pointer (unused)
 * @cmd:  IOCTL number (see eba.h)
 * @arg:  user‑space pointer to command‑specific structure
 *
 * Supported commands:
 *   EBA_IOCTL_ALLOC             – allocate a local buffer
 *   EBA_IOCTL_WRITE             – write to a local buffer
 *   EBA_IOCTL_READ              – read  from a local buffer
 *   EBA_IOCTL_REMOTE_ALLOC      – allocate a buffer on a remote node
 *   EBA_IOCTL_REMOTE_WRITE      – write data to a remote buffer
 *   EBA_IOCTL_REMOTE_READ       – read  data from a remote node
 *   EBA_IOCTL_DISCOVER          – broadcast discovery packets
 *   EBA_IOCTL_EXPORT_NODE_SPECS – publish node capabilities
 *
 * Return: 0 on success or a negative errno on failure.
 */
static long eba_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
     int ret = 0;
     void *kbuf; /* Temporary kernel buffer for data transfers */

     switch (cmd)
     {
     case EBA_IOCTL_ALLOC:

          struct eba_alloc_data alloc_data;
          /* Copy the allocation parameters from user space */
          if (copy_from_user(&alloc_data, (void __user *)arg, sizeof(alloc_data)))
               return -EFAULT;

          /* Perform allocation and record buff_id */
          alloc_data.buff_id = (uint64_t)eba_internals_malloc(alloc_data.size, alloc_data.life_time);
          if (!alloc_data.buff_id)
               ret = -ENOMEM;

          /* Return buff_id back to user */
          if (copy_to_user((void __user *)arg, &alloc_data, sizeof(alloc_data)))
               ret = -EFAULT;
          break;

     case EBA_IOCTL_WRITE:

          struct eba_write wr;
          /* Copy the write parameters from user space */
          if (copy_from_user(&wr, (void __user *)arg, sizeof(wr)))
               return -EFAULT;
          ret = eba_internals_write(wr.payload, wr.buff_id, wr.offset, wr.size);
          break;

     case EBA_IOCTL_READ:

          struct eba_read r;
          /* Copy the read parameters from user space */
          if (copy_from_user(&r, (void __user *)arg, sizeof(r)))
               return -EFAULT;

          /* Allocate temp kernel buffer to stage data */
          kbuf = kmalloc(r.size, GFP_KERNEL);
          if (!kbuf)
               return -ENOMEM;

          /* Read into kbuf, then copy to user */
          ret = eba_internals_read(kbuf, r.buffer_id, r.offset, r.size);
          if (ret)
          {
               kfree(kbuf);
               return ret;
          }
          if (copy_to_user((void __user *)(r.user_addr), kbuf, r.size))
               ret = -EFAULT;
          kfree(kbuf);
          break;

     case EBA_IOCTL_REMOTE_ALLOC:

          struct eba_remote_alloc ra;
          /* Copy the remote alloc parameters from user space */
          if (copy_from_user(&ra, (void __user *)arg, sizeof(ra)))
               return -EFAULT;
          ret = ebp_remote_alloc(ra.size, ra.life_time, ra.buffer_id, ra.mac);
          break;

     case EBA_IOCTL_REMOTE_WRITE:

          struct eba_remote_write rwr;
          /* Copy the remote write parameters from user space */
          if (copy_from_user(&rwr, (void __user *)arg, sizeof(rwr)))
               return -EFAULT;
          ret = ebp_remote_write(rwr.buff_id, rwr.offset, rwr.size, rwr.payload, rwr.mac);
          break;

     case EBA_IOCTL_REMOTE_READ:

          struct eba_remote_read rr;
          /* Copy the remote read parameters from user space */
          if (copy_from_user(&rr, (void __user *)arg, sizeof(rr)))
               return -EFAULT;
          ret = ebp_remote_read(rr.dst_buffer_id, rr.src_buffer_id, rr.dst_offset, rr.src_offset, rr.size, rr.mac);
          break;

     case EBA_IOCTL_DISCOVER:

          ret = ebp_discover();
          break;

     case EBA_IOCTL_EXPORT_NODE_SPECS:

          ret = eba_export_node_specs();
          break;

     default:

          ret = -ENOTTY;
          break;
     }
     return ret;
}

/* Timer for checking expired buffers */
static struct timer_list eba_expired_buf_timer;

/**
 * eba_clean_buffers() - free expired buffers periodically
 * @t: kernel timer instance (unused)
 */
static void eba_clean_buffers(struct timer_list *t)
{
     eba_check_expired_buffers();
     /* re‑arm  timer after EBA_CLEAN_BUFFER_CALLBACK_TIMER ms */
     mod_timer(&eba_expired_buf_timer, jiffies + msecs_to_jiffies(EBA_CLEAN_BUFFER_CALLBACK_TIMER));
}

/*========================================================================*/
/*                  File‑operations helpers                               */
/*========================================================================*/

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

/*========================================================================*/
/*                            Module lifecycle                            */
/*========================================================================*/

/**
 * eba_module_init() - entry point executed when the module is insmod‑ed
 *
 * Allocates a char‑device number, registers /dev/eba, sets up the internal
 * memory pool, starts the periodic cleanup timer, and initialises ebp.
 */
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

     /* Create a device class */
     eba_class = class_create("eba_class");
     if (IS_ERR(eba_class))
     {
          ret = PTR_ERR(eba_class);
          goto err_class;
     }
     /* Create the device node /dev/eba */
     device_create(eba_class, NULL, eba_devno, NULL, "eba");

     /* Initialize the memory pool */
     ret = eba_internals_mempool_init();
     if (ret)
     {
          EBA_ERR("Mempool Init failed\n");
          goto err_mempool;
     }
     /* Initialize the timer */
     timer_setup(&eba_expired_buf_timer, eba_clean_buffers, 0);
     /* Schedule the timer for the first time */
     mod_timer(&eba_expired_buf_timer, jiffies + msecs_to_jiffies(EBA_CLEAN_BUFFER_CALLBACK_TIMER));

     ebp_init();
     EBA_INFO("Module loaded\n");
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

/**
 * eba_module_exit() - invoked at rmmod time. Frees all resources.
 */
static void __exit eba_module_exit(void)
{
     del_timer_sync(&eba_expired_buf_timer);
     eba_internals_mempool_free();
     device_destroy(eba_class, eba_devno);
     class_destroy(eba_class);
     cdev_del(&eba_cdev);
     unregister_chrdev_region(eba_devno, 1);
     ebp_exit();
     EBA_INFO("Module unloaded\n");
}
module_init(eba_module_init);
module_exit(eba_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nader BEN AMMAR");
MODULE_DESCRIPTION("EBA Kernel Module with IOCTL Support");
MODULE_VERSION("0.1");