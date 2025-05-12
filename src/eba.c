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

/* Boolean to print DBG logs */
static bool eba_debug;
/* Register it as a module param */
module_param(eba_debug, bool, 0644);
/*
    # read the current value
    cat /sys/module/eba/parameters/eba_debug
    # turn it on
    echo 1 > /sys/module/eba/parameters/eba_debug
    # turn it off
    echo 0 > /sys/module/eba/parameters/eba_debug
*/
/* Text description */
MODULE_PARM_DESC(eba_debug, "Enable EBA_DBG messages");
#undef EBA_DBG
#define EBA_DBG(fmt, ...)                         \
     do                                           \
     {                                            \
          if (eba_debug)                          \
               EBA_PR(fmt, DEBUG, ##__VA_ARGS__); \
     } while (0)

/* Global variables for character device registration */
static dev_t eba_devno;
static struct cdev eba_cdev;
static struct class *eba_class = NULL;
extern spinlock_t waiter_lock;
extern spinlock_t buffer_waiter_lock;
extern spinlock_t node_info_lock;

extern struct node_info node_infos[MAX_NODE_COUNT];

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
          ret = ebp_remote_alloc(ra.size, ra.life_time, ra.buffer_id, ra.node_id, &ra.iid);
          if (copy_to_user((void __user *)arg, &ra, sizeof(ra)))
               ret = -EFAULT;
          break;

     case EBA_IOCTL_REMOTE_WRITE:

          struct eba_remote_write rwr;
          /* Copy the remote write parameters from user space */
          if (copy_from_user(&rwr, (void __user *)arg, sizeof(rwr)))
               return -EFAULT;
          ret = ebp_remote_write(rwr.buff_id, rwr.offset, rwr.size, rwr.payload, rwr.node_id, &rwr.iid);
          /* propagate IID back */
          if (copy_to_user((void __user *)arg, &rwr, sizeof(rwr)))
               ret = -EFAULT;
          break;

     case EBA_IOCTL_REMOTE_READ:

          struct eba_remote_read rr;
          /* Copy the remote read parameters from user space */
          if (copy_from_user(&rr, (void __user *)arg, sizeof(rr)))
               return -EFAULT;
          ret = ebp_remote_read(rr.dst_buffer_id, rr.src_buffer_id, rr.dst_offset, rr.src_offset, rr.size, rr.node_id, &rr.iid);
          if (copy_to_user((void __user *)arg, &rr, sizeof(rr)))
               ret = -EFAULT;
          break;

     case EBA_IOCTL_DISCOVER:

          ret = ebp_discover();
          break;

     case EBA_IOCTL_EXPORT_NODE_SPECS:

          ret = eba_export_node_specs();
          break;

     case EBA_IOCTL_GET_NODE_INFOS:

          struct eba_node_info kbuf[MAX_NODE_COUNT];
          memset(kbuf, 0, sizeof(kbuf));
          spin_lock(&node_info_lock);
          for (u64 src = 0, dst = 0; src < MAX_NODE_COUNT; src++)
          {
               if (node_infos[src].id == UNUSED_NODE_ID)
                    continue; /* free slot    */

               kbuf[dst].id = node_infos[src].id;
               kbuf[dst].mtu = node_infos[src].mtu;
               kbuf[dst].node_specs = node_infos[src].node_specs;
               memcpy(kbuf[dst].mac, node_infos[src].mac, ETH_ALEN);
               dst++;
          }
          spin_unlock(&node_info_lock);

          /* copy the full array */
          if (copy_to_user((void __user *)arg, kbuf, sizeof(kbuf)))
          {
               ret = - EFAULT;
               break;
          }
          ret = 0;
          break;

     case EBA_IOCTL_WAIT_IID:

          struct eba_wait_iid wi;

          if (copy_from_user(&wi, (void __user *)arg, sizeof(wi))){
               ret= -EFAULT;
               break;
          }

          /* ---------- register the waiter ------------------------------ */
          struct iid_waiter *w;
          spin_lock(&waiter_lock);

          w = iid_waiter_alloc(wi.iid, wi.wanted_status, current);
          if (!w)
          {
               spin_unlock(&waiter_lock);
               wi.rc = -ENOSPC;
               goto copy_to_usr;
          }

          spin_unlock(&waiter_lock);

          /* ---------- sleep ------------------------------------------- */
          set_current_state(TASK_INTERRUPTIBLE);

          if (wi.timeout_ms)
               schedule_timeout(msecs_to_jiffies(wi.timeout_ms));
          else
               schedule(); /* unlimited                    */

          /* ---------- running again ----------------------------------- */
          wi.rc = w->rc;
          wi.timed_out = (w->done == 0); /* timed out = 1 if we never saw the ACK */
          /* free the slot                                                */
          spin_lock(&waiter_lock);
          w->iid = 0;
          spin_unlock(&waiter_lock);

     copy_to_usr:
          if (copy_to_user((void __user *)arg, &wi, sizeof(wi)))
          {
               ret =  -EFAULT;
               break;
          }
          ret = 0;
          break;
     
     case EBA_IOCTL_WAIT_BUFFER:

          struct eba_wait_buffer wb;
          if (copy_from_user(&wb, (void __user *)arg, sizeof(wb)))
          {
               ret = -EFAULT;
               break;
          }
           /* ---------- register the waiter ------------------------------ */
          struct buffer_waiter *bw;
          spin_lock(&buffer_waiter_lock);

          bw = buffer_waiter_alloc(wb.buff_id,current);
          if (bw == NULL)
          {
               spin_unlock(&buffer_waiter_lock);
               wb.rc = -ENOSPC;
               goto copy_to_usr_buf;
          }
          spin_unlock(&buffer_waiter_lock);
          /* ---------- sleep ------------------------------------------- */
           set_current_state(TASK_INTERRUPTIBLE);

          if (wb.timeout_ms)
               schedule_timeout(msecs_to_jiffies(wb.timeout_ms));
          else
               schedule(); /* unlimited                    */

          /* ---------- running again ----------------------------------- */
          wb.rc = bw->rc;
          wb.timed_out= (bw->done == 0); /* timed out = 1 if the buffer is never written onto it  */
          /* free the slot                                                */
          spin_lock(&buffer_waiter_lock);
          bw->buffer_id = 0;
          spin_unlock(&buffer_waiter_lock);

     copy_to_usr_buf:
          if (copy_to_user((void __user *)arg, &wb, sizeof(wb)))
          {
               ret =  -EFAULT;
               break;
          }
          ret = 0;
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

//  variable : 128 bits
// server - client
// recv char -> sends back
// then ; remote shell
// add permissions
