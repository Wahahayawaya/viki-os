#include "../include/multiboot2.h"
#include "../include/vga.h"

/* 内核入口函数，接收multiboot2魔数和信息指针 */
void kernel_main(unsigned int magic, unsigned int addr) {
    /* 检查是否由支持multiboot2的引导加载器启动 */
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        /* 如果魔数不匹配，说明不是通过multiboot2启动的 */
        return;
    }
    
    /* 初始化VGA文本输出模块 */
    vga_init();
    
    /* 使用新的printf功能显示消息 */
    kprintf("Hi, I'm VIKI...\n");
    kprintf("Magic: 0x%x, Info Addr: 0x%x\n", magic, addr);
    kprintf("VGA initialized successfully!\n");
    
    /* 进入无限循环，防止内核退出 */
    while (1) {
        __asm__ volatile ("hlt"); /* 停止CPU直到下一个中断 */
    }
}