#include "../include/interrupt.h"
#include "../include/idt.h"
#include "../include/port.h"
#include "../include/vga.h"
#include "../include/gdt.h"

/* 定义中断处理函数指针数组，保存每个中断向量的C处理函数 */
static void (*interrupt_handlers[IDT_ENTRIES])(struct pt_regs *);

/* PIC端口定义 */
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

/* ICW1 - 初始化命令字: 边缘触发|级联|需要ICW4 */
#define ICW1_ICW4   0x01
#define ICW1_INIT   0x10
/* ICW4 - 8086模式|正常EOI|非缓冲|全嵌套 */
#define ICW4_8086   0x01

/* 
 * 8259 PIC初始化
 * 设计思路: 重新映射中断向量，将主片IRQ 0-7映射到32-39，从片8-15映射到40-47
 * 这样避开了CPU异常使用的0-31
 */
void pic_init(void) {
    /* 保存原来的中断屏蔽寄存器 */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t b1 = inb(PIC2_DATA);

    /* 开始初始化序列，级联模式，需要ICW4 */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: 设置中断向量偏移 */
    outb(PIC1_DATA, IRQ0);       /* 主片从32开始 */
    outb(PIC2_DATA, IRQ8);        /* 从片从40开始 */

    /* ICW3: 告诉主片有从片在IRQ2，告诉从片它的级联编号 */
    outb(PIC1_DATA, 1 << 2);     /* 主片IRQ2接从片 */
    outb(PIC2_DATA, 2);          /* 从片ID是2 */

    /* ICW4: 设置为8086模式 */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* 恢复中断屏蔽，此时全部中断屏蔽掉，需要时再打开 */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, b1);

    /* 全部屏蔽，只在需要时打开特定IRQ */
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);
}

/* 
 * 发送中断结束(EOI)给PIC
 * 如果是从片中断，需要给两级都发EOI
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);  /* EOI命令 */
    }
    outb(PIC1_COMMAND, 0x20);      /* EOI命令 */
}

/* 
 * 注册中断处理函数
 * 这里只需要把汇编中生成的isr入口地址设置到IDT中即可
 */
static void register_interrupt_handler(uint8_t n, void *handler) {
    idt_set_gate(n, (uint32_t)handler, GDT_KERNEL_CODE_SEL, 0x8E);
}

/* 
 * 通用中断处理函数，所有中断都汇到这里
 * 设计思路: 采用Linux统一中断框架，无论什么中断都先保存上下文，然后走统一分发
 */
void interrupt_handler(struct pt_regs *regs) {
    /* 如果是异常，打印异常信息 */
    if (regs->int_no < 32) {
        vga_printf("CPU Exception %d: ", regs->int_no);
        switch (regs->int_no) {
            case 0: vga_puts("Divide by zero"); break;
            case 1: vga_puts("Debug"); break;
            case 2: vga_puts("NMI Interrupt"); break;
            case 3: vga_puts("Breakpoint"); break;
            case 4: vga_puts("Overflow"); break;
            case 5: vga_puts("Bound Range Exceeded"); break;
            case 6: vga_puts("Invalid Opcode"); break;
            case 7: vga_puts("Device Not Available"); break;
            case 8: vga_puts("Double Fault"); break;
            case 9: vga_puts("Coprocessor Segment Overrun"); break;
            case 10: vga_puts("Invalid TSS"); break;
            case 11: vga_puts("Segment Not Present"); break;
            case 12: vga_puts("Stack Fault"); break;
            case 13: vga_puts("General Protection Fault"); break;
            case 14: vga_puts("Page Fault"); break;
            default: vga_puts("Unknown exception"); break;
        }
        vga_printf(" [err_code=%x]\n", regs->err_code);
        /* 这里可以添加死循环或panic */
        while (1) { __asm__ volatile ("hlt"); }
    } 
    /* 如果是硬件中断，发送EOI */
    else if (regs->int_no >= IRQ0 && regs->int_no <= IRQ15) {
        /* 执行注册的处理函数，如果有的话 */
        if (interrupt_handlers[regs->int_no]) {
            interrupt_handlers[regs->int_no](regs);
        }
        pic_send_eoi(regs->int_no - IRQ0);
    }
}

/* 
 * 中断系统初始化: 
 *  1. 初始化IDT
 *  2. 注册所有CPU异常中断(0-31)
 *  3. 注册所有硬件中断(32-47)
 *  4. 注册系统调用中断(128)
 *  5. 初始化PIC
 */
void interrupt_init(void) {
    /* 首先初始化IDT */
    idt_init();

    /* 声明所有isr入口，C文件引用汇编导出的符号 */
    extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
    extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
    extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
    extern void isr9(void);  extern void isr10(void); extern void isr11(void);
    extern void isr12(void); extern void isr13(void); extern void isr14(void);
    extern void isr15(void); extern void isr16(void); extern void isr17(void);
    extern void isr18(void); extern void isr19(void); extern void isr20(void);
    extern void isr21(void); extern void isr22(void); extern void isr23(void);
    extern void isr24(void); extern void isr25(void); extern void isr26(void);
    extern void isr27(void); extern void isr28(void); extern void isr29(void);
    extern void isr30(void); extern void isr31(void);

    extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
    extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
    extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
    extern void irq9(void);  extern void irq10(void); extern void irq11(void);
    extern void irq12(void); extern void irq13(void); extern void irq14(void);
    extern void irq15(void);

    extern void isr128(void);

    /* 注册0-31 CPU异常中断 */
    register_interrupt_handler(0,  isr0);
    register_interrupt_handler(1,  isr1);
    register_interrupt_handler(2,  isr2);
    register_interrupt_handler(3,  isr3);
    register_interrupt_handler(4,  isr4);
    register_interrupt_handler(5,  isr5);
    register_interrupt_handler(6,  isr6);
    register_interrupt_handler(7,  isr7);
    register_interrupt_handler(8,  isr8);
    register_interrupt_handler(9,  isr9);
    register_interrupt_handler(10, isr10);
    register_interrupt_handler(11, isr11);
    register_interrupt_handler(12, isr12);
    register_interrupt_handler(13, isr13);
    register_interrupt_handler(14, isr14);
    register_interrupt_handler(15, isr15);
    register_interrupt_handler(16, isr16);
    register_interrupt_handler(17, isr17);
    register_interrupt_handler(18, isr18);
    register_interrupt_handler(19, isr19);
    register_interrupt_handler(20, isr20);
    register_interrupt_handler(21, isr21);
    register_interrupt_handler(22, isr22);
    register_interrupt_handler(23, isr23);
    register_interrupt_handler(24, isr24);
    register_interrupt_handler(25, isr25);
    register_interrupt_handler(26, isr26);
    register_interrupt_handler(27, isr27);
    register_interrupt_handler(28, isr28);
    register_interrupt_handler(29, isr29);
    register_interrupt_handler(30, isr30);
    register_interrupt_handler(31, isr31);

    /* 注册32-47 硬件中断 */
    register_interrupt_handler(IRQ0,  irq0);
    register_interrupt_handler(IRQ1,  irq1);
    register_interrupt_handler(IRQ2,  irq2);
    register_interrupt_handler(IRQ3,  irq3);
    register_interrupt_handler(IRQ4,  irq4);
    register_interrupt_handler(IRQ5,  irq5);
    register_interrupt_handler(IRQ6,  irq6);
    register_interrupt_handler(IRQ7,  irq7);
    register_interrupt_handler(IRQ8,  irq8);
    register_interrupt_handler(IRQ9,  irq9);
    register_interrupt_handler(IRQ10, irq10);
    register_interrupt_handler(IRQ11, irq11);
    register_interrupt_handler(IRQ12, irq12);
    register_interrupt_handler(IRQ13, irq13);
    register_interrupt_handler(IRQ14, irq14);
    register_interrupt_handler(IRQ15, irq15);

    /* 注册128号系统调用中断，DPL设置为3允许用户态触发 */
    idt_set_gate(128, (uint32_t)isr128, GDT_KERNEL_CODE_SEL, 0xEE);

    /* 初始化8259PIC */
    pic_init();
}
