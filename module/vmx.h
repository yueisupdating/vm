#define VM_MAJOR 510
#define VM_MINOR 0

#define GUEST_MEMORY_SIZE (0x1000 * 16) // 最大guest内存

#define INIT _IO('M', 0)
#define CREATE _IO('M',1)
#define OFF _IO('M',2)

unsigned char guest_bin[] = {
  0xf4
};
unsigned int guest_bin_len = 1;
