#include "../include/port.h"

/* 端口输出函数 */
void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 端口输入函数 */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*
 * COM1 串口端口定义
 * 串口是调试内核最便捷的输出通道，QEMU 默认将 COM1 映射到 stdio
 */
#define COM1_PORT 0x3F8

/* 串口初始化：设置波特率 38400, 8N1 模式 */
void serial_init(void)
{
    /* 关闭中断 */
    outb(COM1_PORT + 1, 0x00);
    /* 打开 DLAB，设置波特率分频器 */
    outb(COM1_PORT + 3, 0x80);
    /* 分频器低字节（38400 波特，分频值=3） */
    outb(COM1_PORT + 0, 0x03);
    /* 分频器高字节 */
    outb(COM1_PORT + 1, 0x00);
    /* 关闭 DLAB，设置 8N1 模式（8 数据位, 无校验, 1 停止位） */
    outb(COM1_PORT + 3, 0x03);
    /* 启用 FIFO，清空缓冲，14 字节阈值 */
    outb(COM1_PORT + 2, 0xC7);
    /* IRQ 使能，RTS/DSR 设置 */
    outb(COM1_PORT + 4, 0x0B);
}

/* 通过 COM1 串口输出单个字符 */
void serial_putc(char c)
{
    /* 等待发送缓冲区为空（Line Status Register bit 5 = Transmitter Holding Register Empty） */
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
        ;
    outb(COM1_PORT, (uint8_t)c);
}