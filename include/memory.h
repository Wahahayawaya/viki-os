#ifndef MEMORY_H
#define MEMORY_H

/* 打印GRUB/multiboot2传递的物理内存布局 */
void memory_print_map(unsigned int multiboot_info_addr);

#endif /* MEMORY_H */
