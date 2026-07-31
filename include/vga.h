#ifndef VGA_H
#define VGA_H

#include "port.h"

/* 手动定义va_list相关宏（简化版本） */
typedef char* va_list;

#define va_start(ap, last_arg) (ap = (va_list)((char*)&(last_arg) + sizeof(last_arg)))
#define va_arg(ap, type) (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap) ((void)0)

/*
 * 定义VGA文本缓冲区的虚拟地址
 * 物理地址 0xB8000 经过高半核映射后变为 KERNEL_VMA + 0xB8000 = 0xC00B8000
 * 分页开启后，内核通过此虚拟地址访问 VGA 文本缓冲区
 */
#define VGA_BUFFER (0xC00B8000)

/* 定义颜色属性 */
#define VGA_COLOR_BLACK         0x00
#define VGA_COLOR_BLUE          0x01
#define VGA_COLOR_GREEN         0x02
#define VGA_COLOR_CYAN          0x03
#define VGA_COLOR_RED           0x04
#define VGA_COLOR_MAGENTA       0x05
#define VGA_COLOR_BROWN         0x06
#define VGA_COLOR_LIGHT_GRAY    0x07
#define VGA_COLOR_DARK_GRAY     0x08
#define VGA_COLOR_LIGHT_BLUE    0x09
#define VGA_COLOR_LIGHT_GREEN   0x0A
#define VGA_COLOR_LIGHT_CYAN    0x0B
#define VGA_COLOR_LIGHT_RED     0x0C
#define VGA_COLOR_LIGHT_MAGENTA 0x0D
#define VGA_COLOR_YELLOW        0x0E
#define VGA_COLOR_WHITE         0x0F

/* 默认颜色：白色前景，黑色背景 */
#define VGA_COLOR_DEFAULT (VGA_COLOR_WHITE | (VGA_COLOR_BLACK << 4))

/* VGA缓冲区尺寸：80列 × 25行 */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER_SIZE (VGA_WIDTH * VGA_HEIGHT)

/* 函数声明 */
void vga_init(void);
void vga_clear(void);
void vga_set_cursor(int x, int y);
void vga_get_cursor(int *x, int *y);
void vga_putc(char c);
void vga_puts(const char *str);
void vga_scroll_up(void);
int vga_printf(const char *format, ...);
int vga_vprintf(const char *format, va_list args);

/* 颜色相关函数 */
void vga_set_color(uint8_t color);
uint8_t vga_get_color(void);

typedef int (*vga_printf_ptr)(const char *format, ...);
extern vga_printf_ptr kprintf;

#endif /* VGA_H */