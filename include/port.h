#ifndef PORT_H
#define PORT_H

/* 定义基本类型 */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

/* 端口I/O函数声明 */
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);

/* 串口（COM1）输出函数，用于调试和验证 */
void serial_init(void);
void serial_putc(char c);

#endif /* PORT_H */