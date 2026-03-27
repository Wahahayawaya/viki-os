#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_ENTRIES 256

/* IDT门描述符结构 */
struct idt_entry {
    uint16_t base_low;      /* 中断处理函数地址低16位 */
    uint16_t selector;      /* 代码段选择子 */
    uint8_t  zero;          /* 必须为0 */
    uint8_t  flags;         /* 标志位：P(1) DPL(2) S(1) TYPE(4) */
    uint16_t base_high;     /* 中断处理函数地址高16位 */
} __attribute__((packed));

/* IDT指针结构，供lidt指令使用 */
struct idt_ptr {
    uint16_t limit;         /* IDT限长 */
    uint32_t base;          /* IDT基地址 */
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

extern void idt_flush(uint32_t idt_ptr);

#endif /* IDT_H */
