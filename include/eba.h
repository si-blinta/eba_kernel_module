/*
 * eba.h - Public header file for the EBA Kernel Module
 *
 * This header contains the ioctl definitions, public data types,
 * and function prototypes for the EBA module.
 *
 * Author: Nader BEN AMMAR
 * License: GPL
 * Version: 1.0
 */

 #ifndef EBA_H
 #define EBA_H
 
 #include <linux/ioctl.h>  // Required for _IO, _IOR, _IOW macros
 
 /*-------------------------------------------------------------------------
  * IOCTL definitions for the EBA module.
  *
  * We choose a unique magic number ('E') for our commands and use sequential
  * numbers to differentiate between commands.
  *-------------------------------------------------------------------------
  */
 #define EBA_IOC_MAGIC   'E'
 
 // Example IOCTL commands:
 // _IO      : Command without parameters.
 // _IOW     : Command that writes data from user space to the kernel.
 // _IOR     : Command that reads data from the kernel to user space.
 // _IOWR    : Command that exchanges data between the kernel and user space.
 
 #define EBA_IOCTL_CMD1  _IO(EBA_IOC_MAGIC, 1)            // Simple command; no data.
 #define EBA_IOCTL_CMD2  _IOW(EBA_IOC_MAGIC, 2, int)         // Command that sends an integer to the kernel.
 
 #define EBA_IOC_MAXNR   2  // Maximum command number
 
 #endif /* EBA_H */
 