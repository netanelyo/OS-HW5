#ifndef KCI_H_
#define KCI_H_

#include <linux/ioctl.h>

#define MAJOR_NUM 245
#define MINOR_NUM 0
#define MODULE_NAME "kci_kmod"
#define DEVICE_NAME "kci_dev"
#define LOG_FILE_PATH "/sys/kernel/debug/kcikmod"
#define BUFFSIZE 1024
#define SUCCESS 0
#define BUF_LEN (PAGE_SIZE << 2) /* 16KB buffer (assuming 4KB PAGE_SIZE) */
#define CIPHER_FLAG 1
#define NEW_LOG "calls"

#define IOCTL_SET_PID _IOW(MAJOR_NUM, 0, unsigned long)
#define IOCTL_SET_FD _IOW(MAJOR_NUM, 1, unsigned long)
#define IOCTL_CIPHER _IOW(MAJOR_NUM, 2, unsigned long)

#endif /* KCI_H_ */
