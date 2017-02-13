#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "kci.h"

#define _BSD_SOURCE

void copy_log_file();

/*
 * Auxiliary function. Handles the "-init" case.
 *
 * @param path - path to the ".ko" file
 */
void case_init(char* path){
	int 	fd_ko;
	int 	rc;
	dev_t 	dev;

	fd_ko = open(path, 0);
	if (fd_ko < 0)
	{
		printf("Error opening kernel object: %s\n", strerror(errno));
		exit(errno);
	}

	// Installing KERNEL module
	rc = syscall(__NR_finit_module, fd_ko, "", 0);
	if (rc)
	{
		printf("Error initialising module: %s\n", strerror(errno));
		exit(errno);
	}

	close(fd_ko);

	// Making device with major MAJOR and minor MINOR
	dev = makedev(MAJOR_NUM, MINOR_NUM);

	// Creating file system node
	rc = mknod("/dev/"DEVICE_NAME, S_IFCHR, dev);
	if (rc)
	{
		printf("Error creating FS node: %s\n", strerror(errno));
		exit(errno);
	}
}

/*
 * Auxiliary function. Handles the "-rm" case.
 */
void case_rm(){
	int rc;

	// Copies old log file to new log file in current dir
	copy_log_file();

	// Removing module from kernel space
	rc = syscall(__NR_delete_module, MODULE_NAME, O_NONBLOCK); 
	if (rc)
	{
		printf("Error removing module from kernel space: %s\n", strerror(errno));
		exit(errno);
	}

	// Removing device file
	rc = unlink("/dev/"DEVICE_NAME);
	if (rc)
	{
		printf("Error removing device: %s\n", strerror(errno));
		exit(errno);
	}
}

/*
 * Auxiliary function.
 *
 * @param cmd = IOCTL_SET_PID / IOCTL_SET_FD / IOCTL_CIPHER
 * @param arg = passed parameter to ioctl command
 *
 */
void open_and_send_ioctl_cmd(int cmd, int arg){
	int fd;
	int rc;

	// Opening device file
	fd = open("/dev/"DEVICE_NAME, 0);
	if (fd < 0)
	{
		printf("Error opening device file: %s\n", strerror(errno));
		exit(errno);
	}

	// Sending command to kernel module
	rc = ioctl(fd, cmd, arg);
	if (rc < 0)
	{
		printf("Error sending ioctl command to KERNEL module: %s\n", strerror(errno));
		exit(errno);
	}

	close(fd);
}

/*
 * Auxiliary function.
 * Copies data from log file to a log in current directory.
 */
void copy_log_file(){
	int 	fd_old;
	int 	fd_new;
	int 	read_bytes;
	int 	written_bytes;
	int		total_written;
	char 	buff[BUFFSIZE];

	// Opening old log file
	fd_old = open(LOG_FILE_PATH"/calls", O_RDONLY);
	if (fd_old < 0)
	{
		printf("Error opening old log file for reading: %s\n", strerror(errno));
		exit(errno);
	}

	// Opening/creating new log file
	fd_new = open(NEW_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
	if (fd_new < 0)
	{
		printf("Error opening new log file for writing: %s\n", strerror(errno));
		exit(errno);
	}

	// Reading from old log and then writing to new log file
	while ((read_bytes = read(fd_old, buff, BUFFSIZE)) > 0)
	{
		total_written = 0;

		while (total_written < read_bytes)
		{
			written_bytes = write(fd_new, buff + total_written, read_bytes - total_written);
			if (written_bytes < 0)
			{
				printf("Error writing to new log file: %s\n", strerror(errno));
				exit(errno);
			}

			total_written += written_bytes;
		}
	}

	close(fd_new);
	close(fd_old);
}

int main(int argc, char** argv) {
	int 	rc;
	int 	fd;
	int 	pid;

	if (argc == 1)
	{
		printf("Wrong no. of CMD-line arguments\n");
		return -1;
	}

	if (!strcmp(argv[1], "-init"))
	{
		case_init(argv[2]); 									// Initialising device
	}

	else if (!strcmp(argv[1], "-pid"))
	{
		pid = strtol(argv[2], NULL, 10);
		open_and_send_ioctl_cmd(IOCTL_SET_PID, pid);			// Sending IOCTL_SET_PID
	}

	else if (!strcmp(argv[1], "-fd"))
	{
		fd = strtol(argv[2], NULL, 10);
		open_and_send_ioctl_cmd(IOCTL_SET_FD, fd);				// Sending IOCTL_SET_FD
	}

	else if (!strcmp(argv[1], "-start"))
	{
		open_and_send_ioctl_cmd(IOCTL_CIPHER, CIPHER_FLAG); 	// Sending IOCTL_CIPHER start
	}

	else if (!strcmp(argv[1], "-stop"))
	{
		open_and_send_ioctl_cmd(IOCTL_CIPHER, !CIPHER_FLAG);	// Sending IOCTL_CIPHER stop
	}

	else if (!strcmp(argv[1], "-rm"))
	{
		case_rm();
	}

}





