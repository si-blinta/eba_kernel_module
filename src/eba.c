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
int eba_debug;
/* Register it as a module param */
module_param(eba_debug, int, 0644);
MODULE_PARM_DESC(eba_debug, "Enable EBA_DBG messages");
/* Global variables for character device registration */
static dev_t eba_devno;
static struct cdev eba_cdev;
static struct class *eba_class = NULL;
extern spinlock_t waiter_lock;
extern spinlock_t buffer_waiter_lock;
extern spinlock_t node_info_lock;

extern struct node_info node_infos[MAX_NODE_COUNT];

// Function prototypes for IOCTL handlers
static long handle_eba_ioctl_alloc(unsigned long arg);
static long handle_eba_ioctl_write(unsigned long arg);
static long handle_eba_ioctl_read(unsigned long arg);
static long handle_eba_ioctl_remote_alloc(unsigned long arg);
static long handle_eba_ioctl_remote_write(unsigned long arg);
static long handle_eba_ioctl_remote_read(unsigned long arg);
static long handle_eba_ioctl_discover(void);
static long handle_eba_ioctl_export_node_specs(void);
static long handle_eba_ioctl_get_node_infos(unsigned long arg);
static long handle_eba_ioctl_wait_iid(unsigned long arg);
static long handle_eba_ioctl_wait_buffer(unsigned long arg);
static long handle_eba_ioctl_register_service(unsigned long arg);
static long handle_eba_ioctl_register_queue(unsigned long arg);
static long handle_eba_ioctl_enqueue(unsigned long arg);
static long handle_eba_ioctl_dequeue(unsigned long arg);
static long handle_eba_ioctl_remote_register_queue(unsigned long arg);
static long handle_eba_ioctl_remote_enqueue(unsigned long arg);
static long handle_eba_ioctl_remote_dequeue(unsigned long arg);

/*
 * eba_wait_for_ack() - sleep until the pre-registered waiter is signalled.
 *
 * The waiter must have been obtained with ebp_alloc_iid_waiter() *before*
 * the corresponding request packet was sent.  That ordering ensures there is
 * no window during which an ACK could arrive and be discarded because no
 * waiter was registered yet.
 *
 * After this function returns, the waiter slot is released automatically.
 *
 * @w:          pre-registered iid_waiter (must not be NULL)
 * @timeout_ms: maximum time to wait in milliseconds; 0 means wait forever
 *
 * Return: 0 on success, -ETIMEDOUT if the timeout expired before the ACK
 *         arrived, or another negative errno set by the remote side.
 */
static long eba_wait_for_ack(struct iid_waiter *w, uint32_t timeout_ms)
{
     long rc;

     /*
      * Set state to INTERRUPTIBLE before checking w->done.  If the ACK
      * already arrived (done==1) the check below will skip the scheduler
      * call entirely, but the ordering guarantees we cannot miss a wakeup:
      *
      *   CPU A (ACK path)               CPU B (this path)
      *   wake_up_process() → RUNNING    set_current_state(INTERRUPTIBLE)
      *                                  READ_ONCE(w->done) == 1 → skip schedule
      */
     set_current_state(TASK_INTERRUPTIBLE);
     if (!READ_ONCE(w->done)) {
          if (timeout_ms)
               schedule_timeout(msecs_to_jiffies(timeout_ms));
          else
               schedule();
     }
     __set_current_state(TASK_RUNNING);

     rc = w->done ? (long)w->rc : -ETIMEDOUT;
     ebp_free_iid_waiter(w);
     return rc;
}


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
 *   EBA_IOCTL_GET_NODE_INFOS    – get node information
 *   EBA_IOCTL_WAIT_IID          – wait for an invocation to complete
 *   EBA_IOCTL_WAIT_BUFFER       – wait for a buffer to be freed
 *
 * Return: 0 on success or a negative errno on failure.
 */
static long eba_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
     switch (cmd)
     {
     case EBA_IOCTL_ALLOC:
          return handle_eba_ioctl_alloc(arg);
     case EBA_IOCTL_WRITE:
          return handle_eba_ioctl_write(arg);
     case EBA_IOCTL_READ:
          return handle_eba_ioctl_read(arg);
     case EBA_IOCTL_REMOTE_ALLOC:
          return handle_eba_ioctl_remote_alloc(arg);
     case EBA_IOCTL_REMOTE_WRITE:
          return handle_eba_ioctl_remote_write(arg);
     case EBA_IOCTL_REMOTE_READ:
          return handle_eba_ioctl_remote_read(arg);
     case EBA_IOCTL_DISCOVER:
          return handle_eba_ioctl_discover();
     case EBA_IOCTL_EXPORT_NODE_SPECS:
          return handle_eba_ioctl_export_node_specs();
     case EBA_IOCTL_GET_NODE_INFOS:
          return handle_eba_ioctl_get_node_infos(arg);
     case EBA_IOCTL_WAIT_IID:
          return handle_eba_ioctl_wait_iid(arg);
     case EBA_IOCTL_WAIT_BUFFER:
          return handle_eba_ioctl_wait_buffer(arg);
     case EBA_IOCTL_REGISTER_SERVICE:
          return handle_eba_ioctl_register_service(arg);
     case EBA_IOCTL_REGISTER_QUEUE:
          return handle_eba_ioctl_register_queue(arg);
     case EBA_IOCTL_ENQUEUE:
          return handle_eba_ioctl_enqueue(arg);
     case EBA_IOCTL_DEQUEUE:
          return handle_eba_ioctl_dequeue(arg);
     case EBA_IOCTL_REMOTE_REGISTER_QUEUE:
          return handle_eba_ioctl_remote_register_queue(arg);
     case EBA_IOCTL_REMOTE_ENQUEUE:
          return handle_eba_ioctl_remote_enqueue(arg);
     case EBA_IOCTL_REMOTE_DEQUEUE:
          return handle_eba_ioctl_remote_dequeue(arg);
     default:
          return -ENOTTY;
     }
}

// IOCTL handler for EBA_IOCTL_ALLOC
static long handle_eba_ioctl_alloc(unsigned long arg)
{
     struct eba_alloc_data alloc_data;
     if (copy_from_user(&alloc_data, (void __user *)arg, sizeof(alloc_data)))
          return -EFAULT;

     alloc_data.buff_id = eba_internals_malloc(alloc_data.size, alloc_data.life_time);
     if (!alloc_data.buff_id)
          return -ENOMEM;

     if (copy_to_user((void __user *)arg, &alloc_data, sizeof(alloc_data)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_WRITE
static long handle_eba_ioctl_write(unsigned long arg)
{
     struct eba_write wr;
     if (copy_from_user(&wr, (void __user *)arg, sizeof(wr)))
          return -EFAULT;

     return eba_internals_write(wr.payload, wr.buff_id, wr.offset, wr.size);
}

// IOCTL handler for EBA_IOCTL_READ
static long handle_eba_ioctl_read(unsigned long arg)
{
     struct eba_read r;
     void *kbuf;

     if (copy_from_user(&r, (void __user *)arg, sizeof(r)))
          return -EFAULT;

     kbuf = kmalloc(r.size, GFP_KERNEL);
     if (!kbuf)
          return -ENOMEM;

     int ret = eba_internals_read(kbuf, r.buffer_id, r.offset, r.size);
     if (ret)
     {
          kfree(kbuf);
          return ret;
     }

     if (copy_to_user((void __user *)(r.user_addr), kbuf, r.size))
          ret = -EFAULT;

     kfree(kbuf);
     return ret;
}

// IOCTL handler for EBA_IOCTL_REMOTE_ALLOC
static long handle_eba_ioctl_remote_alloc(unsigned long arg)
{
     struct eba_remote_alloc ra;
     if (copy_from_user(&ra, (void __user *)arg, sizeof(ra)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet to eliminate the race
      * where the ACK arrives before eba_wait_iid() could register a waiter. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     ra.iid = iid;

     int ret = ebp_remote_alloc(ra.size, ra.life_time, ra.buffer_id, ra.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     ra.rc = (int)eba_wait_for_ack(w, ra.timeout_ms);

     if (copy_to_user((void __user *)arg, &ra, sizeof(ra)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_REMOTE_WRITE
static long handle_eba_ioctl_remote_write(unsigned long arg)
{
     struct eba_remote_write rwr;
     if (copy_from_user(&rwr, (void __user *)arg, sizeof(rwr)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     rwr.iid = iid;

     int ret = ebp_remote_write(rwr.buff_id, rwr.offset, rwr.size, rwr.payload, rwr.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     rwr.rc = (int)eba_wait_for_ack(w, rwr.timeout_ms);

     if (copy_to_user((void __user *)arg, &rwr, sizeof(rwr)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_REMOTE_READ
static long handle_eba_ioctl_remote_read(unsigned long arg)
{
     struct eba_remote_read rr;
     if (copy_from_user(&rr, (void __user *)arg, sizeof(rr)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     rr.iid = iid;

     int ret = ebp_remote_read(rr.dst_buffer_id, rr.src_buffer_id, rr.dst_offset, rr.src_offset, rr.size, rr.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     rr.rc = (int)eba_wait_for_ack(w, rr.timeout_ms);

     if (copy_to_user((void __user *)arg, &rr, sizeof(rr)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_DISCOVER
static long handle_eba_ioctl_discover(void)
{
     return ebp_discover();
}

// IOCTL handler for EBA_IOCTL_EXPORT_NODE_SPECS
static long handle_eba_ioctl_export_node_specs(void)
{
     return eba_export_node_specs();
}

// IOCTL handler for EBA_IOCTL_GET_NODE_INFOS
static long handle_eba_ioctl_get_node_infos(unsigned long arg)
{
     struct eba_node_info kbuf[MAX_NODE_COUNT];
     memset(kbuf, 0, sizeof(kbuf));

     spin_lock(&node_info_lock);
     for (u64 src = 0, dst = 0; src < MAX_NODE_COUNT; src++)
     {
          if (node_infos[src].id == UNUSED_NODE_ID)
               continue;

          kbuf[dst].id = node_infos[src].id;
          kbuf[dst].mtu = node_infos[src].mtu;
          kbuf[dst].node_specs = node_infos[src].node_specs;
          memcpy(kbuf[dst].mac, node_infos[src].mac, ETH_ALEN);
          dst++;
     }
     spin_unlock(&node_info_lock);

     if (copy_to_user((void __user *)arg, kbuf, sizeof(kbuf)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_WAIT_IID
static long handle_eba_ioctl_wait_iid(unsigned long arg)
{
     struct eba_wait_iid wi;
     if (copy_from_user(&wi, (void __user *)arg, sizeof(wi)))
          return -EFAULT;

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

     set_current_state(TASK_INTERRUPTIBLE);
     if (wi.timeout_ms)
          schedule_timeout(msecs_to_jiffies(wi.timeout_ms));
     else
          schedule();

     wi.rc = w->rc;
     wi.timed_out = (w->done == 0);
     spin_lock(&waiter_lock);
     w->iid = 0;
     spin_unlock(&waiter_lock);

copy_to_usr:
     if (copy_to_user((void __user *)arg, &wi, sizeof(wi)))
          return -EFAULT;

     return 0;
}

// IOCTL handler for EBA_IOCTL_WAIT_BUFFER
static long handle_eba_ioctl_wait_buffer(unsigned long arg)
{
     struct eba_wait_buffer wb;
     if (copy_from_user(&wb, (void __user *)arg, sizeof(wb)))
          return -EFAULT;

     struct buffer_waiter *bw;
     spin_lock(&buffer_waiter_lock);
     bw = buffer_waiter_alloc(wb.buff_id, current);
     if (!bw)
     {
          spin_unlock(&buffer_waiter_lock);
          wb.rc = -ENOSPC;
          goto copy_to_usr_buf;
     }
     spin_unlock(&buffer_waiter_lock);

     set_current_state(TASK_INTERRUPTIBLE);
     if (wb.timeout_ms)
          schedule_timeout(msecs_to_jiffies(wb.timeout_ms));
     else
          schedule();

     wb.rc = bw->rc;
     wb.timed_out = (bw->done == 0);
     spin_lock(&buffer_waiter_lock);
     bw->buffer_id = 0;
     spin_unlock(&buffer_waiter_lock);

copy_to_usr_buf:
     if (copy_to_user((void __user *)arg, &wb, sizeof(wb)))
          return -EFAULT;

     return 0;
}

static long handle_eba_ioctl_register_service(unsigned long arg)
{

     struct eba_register_service reg;
     if (copy_from_user(&reg, (void __user *)arg, sizeof(reg)))
          return -EFAULT;

     int ret = register_service(reg.buff_id, reg.new_id);
     if (copy_to_user((void __user *)arg, &reg, sizeof(reg)))
          return -EFAULT;

     return ret;
}

static long handle_eba_ioctl_register_queue(unsigned long arg)
{
     struct eba_register_queue rq;
     if (copy_from_user(&rq, (void __user *)arg, sizeof(rq)))
          return -EFAULT;

     int ret = register_queue(rq.buff_id);
     if (copy_to_user((void __user *)arg, &rq, sizeof(rq)))
          return -EFAULT;

     return ret;
}

static long handle_eba_ioctl_enqueue(unsigned long arg)
{
     struct eba_enqueue enq;
     if (copy_from_user(&enq, (void __user *)arg, sizeof(enq)))
          return -EFAULT;

     int ret = eba_internals_enqueue(enq.buff_id, (void *)enq.data, enq.size);
     if (copy_to_user((void __user *)arg, &enq, sizeof(enq)))
          return -EFAULT;

     return ret;
}
static long handle_eba_ioctl_dequeue(unsigned long arg)
{
     struct eba_dequeue deq;
     if (copy_from_user(&deq, (void __user *)arg, sizeof(deq)))
          return -EFAULT;

     int ret = eba_internals_dequeue(deq.buff_id, (void *)deq.data, deq.size);
     if (copy_to_user((void __user *)arg, &deq, sizeof(deq)))
          return -EFAULT;

     return ret;
}
static long handle_eba_ioctl_remote_register_queue(unsigned long arg)
{
     struct eba_remote_register_queue rq;
     if (copy_from_user(&rq, (void __user *)arg, sizeof(rq)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     rq.iid = iid;

     int ret = ebp_remote_register_queue(rq.buff_id, rq.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     rq.rc = (int)eba_wait_for_ack(w, rq.timeout_ms);

     if (copy_to_user((void __user *)arg, &rq, sizeof(rq)))
          return -EFAULT;

     return 0;
}
static long handle_eba_ioctl_remote_enqueue(unsigned long arg)
{
     struct eba_remote_enqueue re;
     if (copy_from_user(&re, (void __user *)arg, sizeof(re)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     re.iid = iid;

     int ret = ebp_remote_enqueue(re.buff_id, re.size, (char *)re.data, re.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     re.rc = (int)eba_wait_for_ack(w, re.timeout_ms);

     if (copy_to_user((void __user *)arg, &re, sizeof(re)))
          return -EFAULT;

     return 0;
}
static long handle_eba_ioctl_remote_dequeue(unsigned long arg)
{
     struct eba_remote_dequeue rd;
     if (copy_from_user(&rd, (void __user *)arg, sizeof(rd)))
          return -EFAULT;

     /* Pre-register waiter BEFORE sending the packet. */
     uint32_t iid = 0;
     struct iid_waiter *w = ebp_alloc_iid_waiter(&iid, current);
     if (!w)
          return -ENOSPC;
     rd.iid = iid;

     int ret = ebp_remote_dequeue(rd.dst_buffer_id, rd.src_buffer_id, rd.dst_offset, rd.size, rd.node_id, &iid);
     if (ret < 0) {
          ebp_free_iid_waiter(w);
          return ret;
     }

     rd.rc = (int)eba_wait_for_ack(w, rd.timeout_ms);

     if (copy_to_user((void __user *)arg, &rd, sizeof(rd)))
          return -EFAULT;

     return 0;
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
