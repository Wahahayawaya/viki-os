#include "../include/idt.h"
#include "../include/gdt.h"

/* 声明IDT表和IDT指针 */
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idt_ptr;

/* 
 * 设置一个IDT门描述符
 * 参数:
 *   num - IDT索引(中断向量号)
 *   base - 中断处理函数入口地址
 *   sel - 代码段选择子
 *   flags - 属性标志(P|DPL|S|TYPE)
 * 
 * 设计思路: 按照x86规范将32位base地址拆分到base_low和base_high，
 *           使用内核代码段选择子，符合GDT已经定义的分段布局
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
}

/* 
 * IDT初始化 
 * 设计思路: 先清空整个IDT，设置限长，然后调用flush加载到IDTR寄存器
 * 后续由interrupt.c逐个设置中断向量门描述符
 */
void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    /* 先清空所有IDT条目 */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, GDT_KERNEL_CODE_SEL, 0x8E);
    }

    /* 加载IDT */
    idt_flush((uint32_t)&idt_ptr);
}
