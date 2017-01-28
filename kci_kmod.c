#undef __KERNEL__
#define __KERNEL__ /* We're part of the kernel */
#undef MODULE
#define MODULE     /* Not a permanent part, though. */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <asm/paravirt.h>
#include <asm/uaccess.h>
#include <linux/fs.h>

#include "kci.h"

MODULE_LICENSE("GPL v2");

/* Global variables */
static int pid;
static int filedesc;
static int cipher_flag;

/* Declarations */
static struct dentry 	 *file;		 	       	// Path to logger file
static struct dentry 	 *subdir;		       	// Path to logger sub directory
static unsigned long 	**sys_call_table;	       	// Pointer to syscall table
static unsigned long 	  original_cr0;	 	       	// To keep original syscall table permissions
static size_t 		  buf_pos;		       	// logger_buf index
static char   		  logger_buf[BUFFSIZE] = { 0 }; 	// Writing to logger with this buffer

/* Original syscalls declarations */
asmlinkage long (*ref_read)(int fd, char* __user buf,size_t count);
asmlinkage long (*ref_write)(int fd, char* __user buf,size_t count);

static long device_ioctl(struct file* file, unsigned int ioctl_num, unsigned long ioctl_param);

/* Write to debug logger */
int write_to_logger(int successful, int wanted, char* func){
		char 	msg[BUFFSIZE/2] = { 0 };
		int 	ret = 0;
		
		// Constructing message to be written to logger
		ret = sprintf(msg, "FD = %d; PID = %d; Tried to %s %d bytes, Succeeded - %d",
							filedesc, pid, func, wanted, successful);
		if (ret < 0)
		{
			printk("Error constructing message to write to logger.\n");
			return -1;
		}
		
		// If exceeds BUFFSIZE -> zero buf_pos
		if ((buf_pos + ret) >= BUFFSIZE)
		{
			memset(logger_buf, 0, BUFFSIZE);
			buf_pos = 0;
		}

		strncpy(logger_buf + buf_pos, msg, ret);
		buf_pos += ret;
		logger_buf[buf_pos++] = '\n';

		pr_debug("%s\n", msg);
		
		return SUCCESS;
}

/*
 * Auxiliary function. Encrypts/Decrypts data
 */
static int get_and_set(char* buff, int count, int x){
	int 	i = 0;
	int 	rc = 0;
	char 	c = 0;

	for (i = 0; i < count; i++)
	{
		rc = get_user(c, buff + i);
		if (rc < 0)
		{
			printk("Error getting i-th char.\n");
			return rc;
		}

		c += x;
		rc = put_user(c, buff + i);
		if (rc < 0)
		{
			printk("Error writing i-th char.\n");
			return rc;
		}
	}

	return SUCCESS;
}

/* Modifying READ and WRITE sycalls */

// New READ syscall
asmlinkage long new_read(int fd, char* __user buf,size_t count){
	int 	bytes;
	int 	rc = 0;
	int 	to_decrypt = cipher_flag == 1 && current->pid == pid && fd == filedesc;

	// Reading to user buffer using regular read
	bytes = ref_read(fd, buf, count);

	if (to_decrypt && bytes > 0) 		  // If true - decrypt
	{
		rc = get_and_set(buf, bytes, -1); // Subtract 1 out of each char
		if (rc)
		{
			return -1;
		}

		rc = write_to_logger(bytes, count, "READ");
		if (rc)
		{
			printk("Error writing to logger.\n");
			return -1;
		}
	}

	return (long)bytes;
}

// New WRITE syscall
asmlinkage long new_write(int fd, char* __user buf,size_t count){
	long 	bytes = -1;
	int 	rc = 0;
	int 	to_encrypt = cipher_flag && current->pid == pid && fd == filedesc;

	if (to_encrypt)				 // If true - encrypt
	{
		rc = get_and_set(buf, count, 1); // Add 1 to each char
		if (rc)
		{
			return -1;
		}
	}

	// Writing to user file using regular write
	bytes = ref_write(fd, buf, count);

	// Restoring user buffer
	if (to_encrypt && bytes > 0)
	{
		rc = get_and_set(buf, count, -1);
		if (rc)
		{
			return -1;
		}
		
		rc = write_to_logger(bytes, count, "WRITE");
		if (rc)
		{
			printk("Error writing to logger.\n");
			return -1;
		}
	}

	return bytes;
}

// Write to logger from buffer logger_buf
static ssize_t logger_read(struct file *filp,
		char *buffer,
		size_t len,
		loff_t *offset)
{
	return simple_read_from_buffer(buffer, len, offset, logger_buf, buf_pos);
}

const struct file_operations logger_fops = {
	.owner = THIS_MODULE,
	.read = logger_read,
};

const struct file_operations ioctl_fops = {
    .unlocked_ioctl = device_ioctl,
};

// Setting module variables
static long device_ioctl(struct file* file, unsigned int ioctl_num, unsigned long ioctl_param){
	switch (ioctl_num){

	case (IOCTL_SET_PID):
		pid = 		(int)ioctl_param;
		break;
	case (IOCTL_SET_FD):
		filedesc = 	(int)ioctl_param;
		break;
	case(IOCTL_CIPHER):
		cipher_flag = 	(int)ioctl_param;
		break;
	default:
		return -1;
	}

	return SUCCESS;
}

/*
 * Auxiliary function. Finds sycall table pointer.
 */
static unsigned long **aquire_sys_call_table(void)
{
	unsigned long int offset = PAGE_OFFSET;
	unsigned long **sct;

	while (offset < ULLONG_MAX) {
		sct = (unsigned long **)offset;

		if (sct[__NR_close] == (unsigned long *) sys_close)
			return sct;

		offset += sizeof(void *);
	}

	return NULL;
}

/* INIT module */
static int __init kcikmod_init(void)
{
	int rc = 0;
	buf_pos = 0;

	// Creating logfile sub directory
	subdir = debugfs_create_dir("kcikmod", NULL);
	if (IS_ERR(subdir)){
		printk("Failed creating directory for log file.\n");
		return PTR_ERR(subdir);
	}
	
	if (!subdir){
		printk("Failed creating directory for log file.\n");
		return -ENOENT;
	}

	// Creating logfile
	file = debugfs_create_file("calls", S_IRUSR, subdir, NULL, &logger_fops);
	if (!file) {
		printk("Failed creating log file.\n");
		debugfs_remove_recursive(subdir);
		return -ENOENT;
	}

	pid = -1;
	filedesc = -1;
	cipher_flag = 0;

	// Registering device to OS
	rc = register_chrdev(MAJOR_NUM, "/dev/"DEVICE_NAME, &ioctl_fops);
	if (rc < 0)
	{
		printk("Error registering device.\n");
		return -1;
	}

	// Acquiring syscall table pointer
	if(!(sys_call_table = aquire_sys_call_table())){
		printk("Failed to acquire syscall table pointer.\n");
		return -1;
	}

	// Intercpeting read and write syscalls
	original_cr0 = read_cr0();
	write_cr0(original_cr0 & ~0x00010000);

	// Saving old syscalls
	ref_read = (void *)sys_call_table[__NR_read];
	ref_write = (void *)sys_call_table[__NR_write];
	
	// Changing syscalls in syscall table
	sys_call_table[__NR_read] = (unsigned long *)new_read;
	sys_call_table[__NR_write] = (unsigned long *)new_write;

	// Restoring table default setting
	write_cr0(original_cr0);

	printk("Finished init.\n");

	return SUCCESS;
}

/* EXIT module */
static void __exit kcikmod_exit(void)
{
	// Clear and remove log
	debugfs_remove_recursive(subdir);

	// Changing permissions in table
	write_cr0(original_cr0 & ~0x00010000);

	if (sys_call_table)
	{
		// Restoring syscalls to syscall table
		sys_call_table[__NR_read] = (unsigned long *)ref_read;
		sys_call_table[__NR_write] = (unsigned long *)ref_write;
	}

	// Restoring table default setting
	write_cr0(original_cr0);

	msleep(2000);

	// Unregistering device
	unregister_chrdev(MAJOR_NUM, "/dev/"DEVICE_NAME);
}

module_init(kcikmod_init);
module_exit(kcikmod_exit);



