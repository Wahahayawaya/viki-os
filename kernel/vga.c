#include "../include/vga.h"
#include "../include/port.h"

/* VGA文本缓冲区指针 */
static volatile char *vga_buffer = (volatile char *)VGA_BUFFER;

/* 当前光标位置 */
static int cursor_x = 0;
static int cursor_y = 0;

vga_printf_ptr kprintf = vga_printf;

/* 当前颜色属性 */
static uint8_t current_color = VGA_COLOR_DEFAULT;

/* 设置硬件光标位置 */
static void vga_set_hw_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    
    /* 设置高字节 */
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    
    /* 设置低字节 */
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}


/* 初始化VGA模块 */
void vga_init(void) {
    vga_clear();
    cursor_x = 0;
    cursor_y = 0;
    current_color = VGA_COLOR_DEFAULT;
    vga_set_hw_cursor(cursor_x, cursor_y);
}

/* 清屏 */
void vga_clear(void) {
    for (int i = 0; i < VGA_BUFFER_SIZE; i++) {
        vga_buffer[i * 2] = ' ';
        vga_buffer[i * 2 + 1] = current_color;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_set_hw_cursor(cursor_x, cursor_y);
}

/* 设置光标位置 */
void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
        vga_set_hw_cursor(cursor_x, cursor_y);
    }
}

/* 获取光标位置 */
void vga_get_cursor(int *x, int *y) {
    *x = cursor_x;
    *y = cursor_y;
}

/* 向上滚动屏幕 */
void vga_scroll_up(void) {
    /* 将第1行到第24行的内容复制到第0行到第23行 */
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga_buffer[i * 2] = vga_buffer[(i + VGA_WIDTH) * 2];
        vga_buffer[i * 2 + 1] = vga_buffer[(i + VGA_WIDTH) * 2 + 1];
    }
    
    /* 清空最后一行 */
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_buffer[((VGA_HEIGHT - 1) * VGA_WIDTH + i) * 2] = ' ';
        vga_buffer[((VGA_HEIGHT - 1) * VGA_WIDTH + i) * 2 + 1] = current_color;
    }
    
    /* 如果光标在最后一行，移动到倒数第二行 */
    if (cursor_y >= VGA_HEIGHT - 1) {
        cursor_y = VGA_HEIGHT - 2;
    }
}

/* 输出单个字符 */
void vga_putc(char c) {
    switch (c) {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7; // 对齐到8的倍数
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                vga_buffer[(cursor_y * VGA_WIDTH + cursor_x) * 2] = ' ';
                vga_buffer[(cursor_y * VGA_WIDTH + cursor_x) * 2 + 1] = current_color;
            }
            break;
        default:
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
            }
            
            if (cursor_y >= VGA_HEIGHT) {
                vga_scroll_up();
                cursor_y = VGA_HEIGHT - 1;
            }
            
            vga_buffer[(cursor_y * VGA_WIDTH + cursor_x) * 2] = c;
            vga_buffer[(cursor_y * VGA_WIDTH + cursor_x) * 2 + 1] = current_color;
            cursor_x++;
            break;
    }
    
    /* 更新硬件光标 */
    vga_set_hw_cursor(cursor_x, cursor_y);
}

/* 输出字符串 */
void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str);
        str++;
    }
}

/* 设置颜色 */
void vga_set_color(uint8_t color) {
    current_color = color;
}

/* 获取当前颜色 */
uint8_t vga_get_color(void) {
    return current_color;
}

/* 计算字符串长度 */
static int strlen(const char *str) {
    int len = 0;
    while (*str++) {
        len++;
    }
    return len;
}

/* 内部辅助函数：将数字转换为字符串 */
static void itoa(int value, char *buffer, int base) {
    char *p = buffer;
    char *p1, *p2;
    unsigned int ui;
    int negative = 0;
    
    if (base == 10 && value < 0) {
        negative = 1;
        ui = (unsigned int)(-value);
    } else {
        ui = (unsigned int)value;
    }
    
    /* 处理0的特殊情况 */
    if (ui == 0) {
        *p++ = '0';
        *p = '\0';
        return;
    }
    
    /* 生成数字字符串（逆序） */
    while (ui != 0) {
        int digit = ui % base;
        if (digit < 10) {
            *p++ = (char)('0' + digit);
        } else {
            *p++ = (char)('A' + digit - 10);
        }
        ui /= base;
    }
    
    if (negative) {
        *p++ = '-';
    }
    
    *p = '\0';
    
    /* 反转字符串 */
    p1 = buffer;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        p1++;
        p2--;
    }
}

/* 内部辅助函数：输出格式化字符串 */
static int vga_vprintf_internal(const char *format, va_list args) {
    int count = 0;
    const char *p = format;
    
    while (*p) {
        if (*p != '%') {
            vga_putc(*p);
            count++;
            p++;
            continue;
        }
        
        p++; // 跳过'%'
        
        // 处理格式说明符
        switch (*p) {
            case 'c': {
                char c = (char)va_arg(args, int);
                vga_putc(c);
                count++;
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                if (str) {
                    int len = strlen(str);
                    for (int i = 0; i < len; i++) {
                        vga_putc(str[i]);
                        count++;
                    }
                } else {
                    const char *null_str = "(null)";
                    int len = strlen(null_str);
                    for (int i = 0; i < len; i++) {
                        vga_putc(null_str[i]);
                        count++;
                    }
                }
                break;
            }
            case 'd':
            case 'i': {
                int value = va_arg(args, int);
                char buffer[32];
                itoa(value, buffer, 10);
                int len = strlen(buffer);
                vga_puts(buffer);
                count += len;
                break;
            }
            case 'x':
            case 'X': {
                unsigned int value = va_arg(args, unsigned int);
                char buffer[32];
                itoa((int)value, buffer, 16);
                if (*p == 'X') {
                    // 转换为大写
                    char *ptr = buffer;
                    while (*ptr) {
                        if (*ptr >= 'a' && *ptr <= 'f') {
                            *ptr = *ptr - 'a' + 'A';
                        }
                        ptr++;
                    }
                }
                int len = strlen(buffer);
                vga_puts(buffer);
                count += len;
                break;
            }
            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                char buffer[32];
                itoa((int)value, buffer, 10);
                int len = strlen(buffer);
                vga_puts(buffer);
                count += len;
                break;
            }
            case '%': {
                vga_putc('%');
                count++;
                break;
            }
            default: {
                vga_putc('%');
                vga_putc(*p);
                count += 2;
                break;
            }
        }
        p++;
    }
    
    return count;
}

/* printf函数实现 */
int vga_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vga_vprintf_internal(format, args);
    va_end(args);
    return result;
}

/* vprintf函数实现 */
int vga_vprintf(const char *format, va_list args) {
    return vga_vprintf_internal(format, args);
}