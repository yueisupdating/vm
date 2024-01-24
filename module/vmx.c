#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include"vmx.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Updating");

#define VMX_SIZE 4096

struct vmcs_hw {
	u32 revision_id:31;
	u32 shadow:1;
};

struct vmcs{
    struct vmcs_hw hw;
    u32 abort;
	char data[VMX_SIZE - 8];
};

void _vmexit_handler(void); 
static dev_t vm_dev; // 设备结构体
static struct cdev vm_cdev; // 字符设备

static long vm_ioctl(struct file *file,unsigned int cmd,unsigned long data);

static struct file_operations vm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vm_ioctl, // 指定了ioctl接口的handle函数（全部由ioctl完成）
};

struct guest_regs{
	u64 rax;
	u64 rcx;
	u64 rdx;
	u64 rbx;
	u64 rbp;
	u64 rsp;
	u64 rsi;
	u64 rdi;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
};

static int vm_init(void)
{
	printk("**********\n");
    printk("Init\n");
	vm_dev = MKDEV(VM_MAJOR,VM_MINOR); // （主设备号，次设备号）
	if (0 < register_chrdev_region(vm_dev, 1, "vm")) { // 设备注册
		printk("register_chrdev_region error\n");
        return -1;
	}
	cdev_init(&vm_cdev, &vm_fops); // 初始化字符设备，绑定字符设备的操作方法接口
	vm_cdev.owner = THIS_MODULE;
	if (0 < cdev_add(&vm_cdev, vm_dev, 1)) { // 向操作系统添加vm字符设备
		printk("cdev_add error\n");
		unregister_chrdev_region(vm_dev, 1);// 取消注册
	}
	return 0;
}

static void vm_exit(void)
{
	printk("Exit\n");
    printk("**********\n");
	cdev_del(&vm_cdev); // 从操作系统中删去字符设备
	unregister_chrdev_region(vm_dev, 1); // 取消注册
	return;
}

void write_vmcs(u64 vmcs_field,u64 vmcs_field_value){
	asm volatile (
			"vmwrite %1, %0\n\t"
			:
			: "r" (vmcs_field), "r" (vmcs_field_value)
		);
}

static u64 shutdown_rsp;
static u64 shutdown_rbp;

static long vm_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
	int i;
	u8 ret1;
	static struct vmcs *vmxon;
	u64 vmxon__pa;

	static struct vmcs *vmcs;
	u64 vmcs__pa;

    static u8 *guest_memory;
	
	u32 edx, eax, ecx;
	u64 rdx;
	static u8 *stack;
	u8 xdtr[10];
	u64 vmcs_field;
	u64 vmcs_field_value;
	u64 host_tr_selector;
	u64 host_gdt_base;
	u64 host_tr_desc;
	u64 guest_memory__pa;
	static u64 shutdown_rsp;
	static u64 shutdown_rbp;


	switch (cmd) {
        case INIT:
			printk("**********\n");
    		printk("VMX init\n");
			vmxon = (struct vmcs*) kmalloc(VMX_SIZE, GFP_KERNEL);
            memset(vmxon,0,VMX_SIZE);
			vmxon->hw.revision_id= 0x00000001;
			vmxon->hw.shadow = 0x00000000;
			vmxon__pa = __pa(vmxon);

			// 从cr4中取出第13位(VMXE)并将该位设为1，再更新cr4
			asm volatile (
				"movq %cr4, %rax\n\t"
				"bts $13, %rax\n\t"
				"movq %rax, %cr4"
			);
			// 执行vmxon并检查结果是否正确
			asm volatile (
				"vmxon %[pa]\n\t"
				"setna %[ret]"
				: [ret] "=rm" (ret1)
				: [pa] "m" (vmxon__pa)
				: "cc", "memory"
			);
			printk("vmxon result=%d\n",ret1);
			break;

		case CREATE:
			printk("**********\n");
    		printk("create vm\n");

			vmcs = (struct vmcs *) kmalloc(VMX_SIZE, GFP_KERNEL);
			memset(vmcs, 0, VMX_SIZE);
			vmcs->hw.revision_id = 0x00000001;
			vmcs->hw.shadow = 0x00000000;
			vmcs__pa = __pa(vmcs);

			guest_memory = (u8 *) kmalloc(GUEST_MEMORY_SIZE,GFP_KERNEL);
			guest_memory__pa = __pa(guest_memory);
			
			for (i = 0; i < guest_bin_len; i++) {
				guest_memory[i] = guest_bin[i];
			}				
			
			asm volatile (
				"vmclear %[pa]\n\t"
				"setna %[ret]"
				: [ret] "=rm" (ret1)
				: [pa] "m" (vmcs__pa)
				: "cc", "memory"
			);
			printk("vmclear result= %d\n", ret1);
			
			asm volatile (
				"vmptrld %[pa]\n\t"
				"setna %[ret]"
				: [ret] "=rm" (ret1)
				: [pa] "m" (vmcs__pa)
				: "cc", "memory"
			);
			printk("vmptrld = %d\n", ret1);

			// Guest
			vmcs_field = 0x00000802; 
			vmcs_field_value = 0x0000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CS selctor
			
			vmcs_field =  0x00006808;
			vmcs_field_value = 0x0000000000000000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CS base

			vmcs_field = 0x00004802;
			vmcs_field_value = 0x0000FFFF;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CS limit

			vmcs_field = 0x00004816;
			vmcs_field_value = 0x0000009B;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CS access rights
			
			vmcs_field = 0x0000080E;
			vmcs_field_value = 0x0000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest TR selctor

			vmcs_field =  0x00006814;
			vmcs_field_value = 0x0000000000008000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest TR base

			vmcs_field = 0x0000480E; 
			vmcs_field_value = 0x0000000FF;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest TR limit

			vmcs_field = 0x00004822;
			vmcs_field_value = 0x0000008B;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest TR access rights

			vmcs_field =  0x00006800;
			vmcs_field_value = 0x00000020;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CR0

			vmcs_field =  0x00006804;
			vmcs_field_value = 0x0000000000002000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest CR4

			vmcs_field = 0x00002800;
			vmcs_field_value = 0xFFFFFFFFFFFFFFFF;
			write_vmcs(vmcs_field,vmcs_field_value); // VMCS link pointer

			vmcs_field = 0x0000681E; 
			vmcs_field_value = 0x0000000000000000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest RIP,vm 执行起点
			
			vmcs_field = 0x00006820;
			vmcs_field_value = 0x0000000000000002;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest RFLAGS

			vmcs_field = 0x0000481A; 
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest DS access rights

			vmcs_field = 0x00004814; 
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest ES access rights

			vmcs_field = 0x0000481C; 
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest FS access rights

			vmcs_field = 0x0000481E;
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest GS access rights

			vmcs_field = 0x00004818;
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest SS access rights

			vmcs_field = 0x00004820;
			vmcs_field_value = 0x00010000;
			write_vmcs(vmcs_field,vmcs_field_value); // Guest LDTR access rights

			// host
			vmcs_field = 0x00000C02;
			asm volatile (
				"movq %%cs, %0\n\t"	// 取出host当前cs值
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8; // 取出低位的段选择子部分
			write_vmcs(vmcs_field,vmcs_field_value); // Host CS selctor

			vmcs_field = 0x00000C06;
			asm volatile (
				"movq %%ds, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); // Host DS selctor

			vmcs_field = 0x00000C00;
			asm volatile (
				"movq %%es, %0\n\t" 
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); //Host ES selctor

			vmcs_field = 0x00000C08;
			asm volatile (
				"movq %%fs, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); //Host FS selctor

			vmcs_field = 0x00000C0A;
			asm volatile (
				"movq %%gs, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); //Host GS selctor

			vmcs_field = 0x00000C04; 
			asm volatile (
				"movq %%ss, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); //Host SS selctor

			vmcs_field = 0x00000C0C; // 设置host tr段选择子
			asm volatile (
				"str %0\n\t" // 读出host中的tr段寄存器值
				: "=a" (vmcs_field_value)
				:
			);
			vmcs_field_value &= 0xF8;
			write_vmcs(vmcs_field,vmcs_field_value); //Host TR selctor

			vmcs_field = 0x00002C00;
			ecx = 0x277;
			asm volatile (
				"rdmsr\n\t" // 该值位于msr寄存器中
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host IA32__paT

			vmcs_field = 0x00002C02;
			ecx = 0xC0000080;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host IA32_EFER 

			vmcs_field = 0x00004C00;
			ecx = 0x174;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host IA32_SYSENTER_CS

			vmcs_field = 0x00006C00;
			asm volatile (
				"movq %%cr0, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			write_vmcs(vmcs_field,vmcs_field_value); //Host CR0

			vmcs_field = 0x00006C02; 
			asm volatile (
				"movq %%cr3, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			write_vmcs(vmcs_field,vmcs_field_value); //Host CR3

			vmcs_field = 0x00006C04;
			asm volatile (
				"movq %%cr4, %0\n\t"
				: "=a" (vmcs_field_value)
				:
			);
			write_vmcs(vmcs_field,vmcs_field_value); //Host CR4

			vmcs_field = 0x00006C06; // 设置host FS_BASE
			ecx = 0xC0000100;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host FS_BASE

			vmcs_field = 0x00006C08;
			ecx = 0xC0000101;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host GS_BASE


			asm volatile (
				"str %0\n\t"
				: "=a" (host_tr_selector)
				:
			);
			host_tr_selector &= 0xF8;
			asm volatile (
				"sgdt %0\n\t"
				: "=m" (xdtr)
				:
			);
			host_gdt_base = *((u64 *) (xdtr + 2)); // 加一个偏移主要用于计算出GDT_BASE部分
			host_tr_desc = *((u64 *) (host_gdt_base + host_tr_selector));
			vmcs_field_value = ((host_tr_desc & 0x000000FFFFFF0000) >> 16) | ((host_tr_desc & 0xFF00000000000000) >> 32);
			host_tr_desc = *((u64 *) (host_gdt_base + host_tr_selector + 8));
			host_tr_desc <<= 32;
			vmcs_field_value |= host_tr_desc;
			vmcs_field = 0x00006C0A; // 设置host TR_BASE为host_tr_desc
			write_vmcs(vmcs_field,vmcs_field_value); //Host TR_BASE

			vmcs_field = 0x00006C0C;
			asm volatile (
				"sgdt %0\n\t"
				: "=m" (xdtr)
				:
			);
			vmcs_field_value = *((u64 *) (xdtr + 2)); // 取得GDT_BASE部分的值
			write_vmcs(vmcs_field,vmcs_field_value); //Host GDTR_BASE

			vmcs_field = 0x00006C0E;
			asm volatile (
				"sidt %0\n\t"
				: "=m" (xdtr)
				:
			);
			vmcs_field_value = *((u64 *) (xdtr + 2)); // 取得IDT_BASE部分的值
			write_vmcs(vmcs_field,vmcs_field_value); //Host IDTR_BASE

			vmcs_field = 0x00006C10; // 设置host IA32_SYSENTER_ESP
			ecx = 0x175;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host IA32_SYSENTER_ESP

			vmcs_field = 0x00006C12;
			ecx = 0x176;
			asm volatile (
				"rdmsr\n\t"
				: "=a" (eax), "=d" (edx)
				: "c" (ecx)
			);
			rdx = edx;
			vmcs_field_value = rdx << 32 | eax;
			write_vmcs(vmcs_field,vmcs_field_value); //Host IA32_SYSENTER_EIP

			// RSP和RIP用于处理VM-Exit
			// #VMEXIT handler分配了单独的栈，Host RSP指向该栈
			stack = (u8 *) kmalloc(0x8000, GFP_KERNEL); // 通过kmalloc为host rsp指向的栈分配了空间
			vmcs_field = 0x00006C14; 
			vmcs_field_value = (u64) stack + 0x8000;
			write_vmcs(vmcs_field,vmcs_field_value); //Host RSP

			// RIP作为VM-Exit事件处理的入口地址
			vmcs_field = 0x00006C16;
			vmcs_field_value = (u64) _vmexit_handler; // _vmexit_handler作为#VMEXIT的事件处理函数的地址
			write_vmcs(vmcs_field,vmcs_field_value); //Host RIP

			/*
			_vmexit_handler是用汇编语言编写的函数，
			同时，还定义了C函数handle_vmexit用于处理具体的处理逻辑。
			产生#VMEXIT事件时，CPU只在VMCS保存Guest一些系统相关寄存器，
			而没有保存Guest的通用寄存器，
			C函数也只会保存/恢复少量通用寄存器，
			因此，定义了一个汇编函数用于保存/恢复需要的寄存器。
			*/
			
			/* 64位Linux中，调用函数前,前6个整数参数依次放入
			rdi、rsi、rdx、rcx、r8、r9寄存器，后面的参数依次压入栈中。
			handle_vmexit只有一个参数，
			因此，调用handle_vmexit时，其参数需存入rdi中。
			*/

			vmcs_field = 0x00000000;
			vmcs_field_value = 0x0001; // vCPU ID被设为常量1
			write_vmcs(vmcs_field,vmcs_field_value); // VIRTUAL_PROCESSOR_ID

			vmcs_field = 0x00004000;
			vmcs_field_value = 0x00000016;
			write_vmcs(vmcs_field,vmcs_field_value); // Pin-based VM-execution controls

			vmcs_field = 0x00004002;
			vmcs_field_value = 0x840061F2;
			write_vmcs(vmcs_field,vmcs_field_value); // Primary Processor-based VM-execution controls

			vmcs_field = 0x0000401E;
			vmcs_field_value = 0x000000A2;
			write_vmcs(vmcs_field,vmcs_field_value); // Secondary Processor-based VM-execution controls

			vmcs_field = 0x00004012;
			vmcs_field_value = 0x000011fb;
			write_vmcs(vmcs_field,vmcs_field_value); // VM-entry controls

			vmcs_field = 0x0000400C;
			vmcs_field_value = 0x00036ffb;
			write_vmcs(vmcs_field,vmcs_field_value); // VM-exit controls

			// 保存正式进入虚拟机前的rsp和rbp
			asm volatile (
				"movq %%rsp, %0\n\t"
				"movq %%rbp, %1\n\t"
				: "=a" (shutdown_rsp), "=b" (shutdown_rbp)
				:
			);

			asm volatile (
				"vmlaunch\r\n" // 一切的准备都是为了最后vmlaunch进入客户机的vCPU
				"setna %[ret]"
				: [ret] "=rm" (ret1)
				:
				: "cc", "memory"
			);
			printk("vmlaunch = %d\n", ret1);



			// 控制流转到了客户机,之后exit
			vmcs_field = 0x00004402;
			asm volatile (
				"vmread %1, %0\n\t" 
				: "=r" (vmcs_field_value)
				: "r" (vmcs_field)
			);
			
			printk("----------\n");
			printk("EXIT_REASON = 0x%llx\n", vmcs_field_value);
			printk("----------\n");
			break;

	
			case OFF:
				asm volatile ("vmxoff"); 
				asm volatile (
					"movq %cr4, %rax\n\t"
					"btr $13, %rax\n\t"
					"movq %rax, %cr4"
				);
			break;
    }
	return 0;
}

void handle_vmexit(struct guest_regs *regs)
{
	u64 vmcs_field;
	u64 vmcs_field_value;
	u64 guest_rip;

	vmcs_field = 0x00004402;
	asm volatile (
		"vmread %1, %0\n\t"
		: "=r" (vmcs_field_value)
		: "r" (vmcs_field)
	);// EXIT_REASON
	printk("----------\n");
	printk("EXIT_REASON = 0x%llx\n", vmcs_field_value);
	printk("----------\n");

	switch (vmcs_field_value) {
	case 0x0C: // EXIT_REASON_HLT
		/* 
		恢复先前保存的launch前的rsp和rbp指针，
		然后跳转执行流到预先定义好的shutdown LABLE处 
		*/
		asm volatile (
			"movq %0, %%rsp\n\t"
			"movq %1, %%rbp\n\t"
			//attention: "jmp shutdown\n\t"
			:
			: "a" (shutdown_rsp), "b" (shutdown_rbp)
		);

		break;
	default: // 否则跳过导致退出的指令,继续执行下一条指令
		break;
	}

	vmcs_field = 0x0000681E; 
	asm volatile (
		"vmread %1, %0\n\t"
		: "=r" (vmcs_field_value)
		: "r" (vmcs_field)
	);
	guest_rip = vmcs_field_value;// GUEST_RIP

	vmcs_field = 0x0000440C; 
	asm volatile (
		"vmread %1, %0\n\t"
		: "=r" (vmcs_field_value)
		: "r" (vmcs_field)
	);// VM_EXIT_INSTRUCTION_LEN

	vmcs_field = 0x0000681E; 
	vmcs_field_value = guest_rip + vmcs_field_value;
	write_vmcs(vmcs_field,vmcs_field_value); // 设置GUEST_RIP
	return;
}

module_init(vm_init);
module_exit(vm_exit);
