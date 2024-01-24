#define _GNU_SOURCE

#include <sched.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "vmx.h"


static int vm_fd;
int ret;

int main(int argc, char **argv)
{
	cpu_set_t mask;
	CPU_ZERO(&mask); // 初始化cpu_set
	CPU_SET(1, &mask); // 设置比特位：将进程绑定到CPU1上
	printf("----------\n");
    printf("starting\n");
	printf("\n");
    if (-1 == sched_setaffinity(0, sizeof mask, &mask)) { // 完成实际的CPU绑定操作
		printf("failed to set affinity\n");
        return 0;
	}

	if ((vm_fd = open("/dev/vm", O_RDWR)) < 0) { // 拿到内核模块fd
		printf("failed to open device\n");
        return 0;
	}
    if ((ret = ioctl(vm_fd, INIT)) < 0) { 
		printf("failed to exec ioctl INIT\n");
        close(vm_fd);
	}
    if ((ret = ioctl(vm_fd, CREATE)) < 0) { 
		printf("failed to exec ioctl CREATE\n");
        close(vm_fd);
	}
    if ((ret = ioctl(vm_fd, CREATE)) < 0) { 
		printf("failed to exec ioctl CREATE\n");
        close(vm_fd);
	}
    if ((ret = ioctl(vm_fd, OFF)) < 0) { 
		printf("failed to exec ioctl OFF\n");
        close(vm_fd);
	}
    printf("see u again\n");
    printf("----------\n");
    return 0;
}