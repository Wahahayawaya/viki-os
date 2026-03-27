#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

/* 
 * 中断上下文寄存器结构
 * 按照中断发生时栈中寄存器顺序排列，所有被保存的寄存器都在这里
 * 设计思路: 这个结构让C处理函数可以直接访问所有被保存的寄存器
 * 顺序说明: 
 *   段寄存器 -> pusha保存通用寄存器 -> 我们手动压入中断号和错误码 -> CPU自动压入的寄存器
 */
typedef struct pt_regs
{
  uint32_t gs, fs, es, ds;                         /* Data segment selector */
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha. */
  uint32_t int_no, err_code;                       /* Interrupt number and error code (if applicable) */
  uint32_t eip, cs, eflags, useresp, ss;           /* Pushed by the processor automatically. */
} __attribute__((packed)) pt_regs;

/* 中断处理函数，由汇编调用 */
void interrupt_handler(struct pt_regs *regs);

/* 中断向量表初始化 */
void interrupt_init(void);

/* 8259 PIC中断控制器初始化 */
void pic_init(void);
/* 发送中断结束信号(EOI)给PIC */
void pic_send_eoi(uint8_t irq);

/* 中断向量号定义 */
#define IRQ0  32
#define IRQ1  33
#define IRQ2  34
#define IRQ3  35
#define IRQ4  36
#define IRQ5  37
#define IRQ6  38
#define IRQ7  39
#define IRQ8  40
#define IRQ9  41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

/* 异常中断向量号定义 */
#define DIVIDE_ERROR      0
#define DEBUG             1
#define NMI               2
#define BREAKPOINT        3
#define OVERFLOW          4
#define BOUND_RANGE_EX    5
#define INVALID_OPCODE    6
#define DEVICE_NOT_AVAIL  7
#define DOUBLE_FAULT      8
#define COPROCESSOR_SEG   9
#define INVALID_TSS       10
#define SEG_NOT_PRESENT   11
#define STACK_FAULT       12
#define GENERAL_PROTECTION 13
#define PAGE_FAULT        14
/* 保留15 */
#define FPU_ERROR         16
#define ALIGNMENT_CHECK   17
#define MACHINE_CHECK     18
#define SIMD_FLOAT        19

/* 系统调用中断向量号 */
#define SYSCALL_INT       128

#endif /* INTERRUPT_H */
