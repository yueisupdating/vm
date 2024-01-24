PWD := $(shell pwd)

obj-m += vm.o
vm-objs := vmx.o vmexit_handler.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
