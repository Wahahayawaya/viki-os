#include "../include/multiboot2.h"

/* 定义VGA文本缓冲区的起始地址 */
#define VGA_BUFFER 0xb8000

/* 定义颜色属性：白色前景，黑色背景 */
#define VGA_COLOR_WHITE_ON_BLACK 0x07

/* 定义VGA缓冲区的大小：80列 × 25行 = 2000个字符 */
#define VGA_BUFFER_SIZE 2000

/* 内核入口函数，接收multiboot2魔数和信息指针 */
void kernel_main(unsigned int magic, unsigned int addr) {
    /* 检查是否由支持multiboot2的引导加载器启动 */
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        /* 如果魔数不匹配，说明不是通过multiboot2启动的 */
        return;
    }
    
    /* 获取VGA文本缓冲区指针 */
    volatile char *vga_buffer = (volatile char *)VGA_BUFFER;
    
    /* 清理屏幕：将整个VGA缓冲区设置为空格和默认颜色 */
    for (int i = 0; i < VGA_BUFFER_SIZE; i++) {
        vga_buffer[i * 2] = ' ';                    /* 空格字符 */
        vga_buffer[i * 2 + 1] = VGA_COLOR_WHITE_ON_BLACK; /* 颜色属性 */
    }
    
    /* 要显示的消息 */
    const char *message = "Hi, I'm VIKI...";
    int i = 0;
    
    /* 逐字符写入VGA缓冲区 */
    while (message[i] != '\0') {
        vga_buffer[i * 2] = message[i];           /* 字符 */
        vga_buffer[i * 2 + 1] = VGA_COLOR_WHITE_ON_BLACK; /* 颜色属性 */
        i++;
    }
    
    /* 进入无限循环，防止内核退出 */
    while (1) {
        __asm__ volatile ("hlt"); /* 停止CPU直到下一个中断 */
    }
}