/*
 * 中断服务例程(ISR) - 中断处理汇编实现
 * 采用Linux统一中断框架设计思路：所有中断统一入口，保存上下文，调用C处理函数
 * 定义三种宏分别处理不同类型中断:
 *  1. IRQ_MACRO - 硬件中断，无错误码，需手动push $0
 *  2. EXC_NOERR_MACRO - CPU异常，无错误码，需手动push $0
 *  3. EXC_ERR_MACRO - CPU异常，CPU已经自动push错误码，无需额外操作
 */

.global interrupt_handler

/* 保存所有段寄存器和通用寄存器，栈指针指向pt_regs结构
 * cdecl调用约定: 第一参数通过栈传递，所以我们直接把esp压栈，让它成为第一个参数
 */
.macro SAVE_ALL
    pushl %ds             /* 保存当前数据段选择子 */
    pushl %es
    pushl %fs
    pushl %gs
    pusha                 /* 保存所有通用寄存器 */
    pushl %esp            /* 栈指针就是pt_regs*，压入栈作为C函数第一参数 */
    cli                   /* 进入中断处理前关闭中断，禁止嵌套 */
.endm

/* 恢复所有寄存器，从中断返回，清理栈 */
.macro RESTORE_ALL
    addl $4, %esp        /* 清理call压入的返回地址和我们压入的参数(pt_regs*) */
    popa                 /* 恢复通用寄存器 */
    popl %gs
    popl %fs
    popl %es
    popl %ds             /* 恢复段寄存器 */
    addl $8, %esp        /* 清理int_no和err_code，共8字节 */
    sti                  /* 恢复中断允许 */
    iret                 /* 从中断返回，CPU自动弹出eip/cs/eflags等 */
.endm

/*
 * 宏1: 处理无错误码异常
 * 手动压入错误码0，保存上下文，调用C处理函数，恢复返回
 */
.macro EXC_NOERR_MACRO num
.global isr\num
isr\num:
    pushl $0             /* 没有错误码，手动压入0保持栈结构对齐 */
    pushl $\num          /* 压入中断向量号 */
    SAVE_ALL
    call interrupt_handler
    RESTORE_ALL
.endm

/*
 * 宏2: 处理有错误码异常
 * CPU已经自动压入错误码，只需压入中断向量号
 */
.macro EXC_ERR_MACRO num
.global isr\num
isr\num:
    pushl $\num          /* 只压入中断向量号，错误码已经由CPU压入 */
    SAVE_ALL
    call interrupt_handler
    RESTORE_ALL
.endm

/*
 * 宏3: 处理硬件中断
 * 和无错误码异常类似，处理流程一样
 */
.macro IRQ_MACRO num
.global irq\num
irq\num:
    pushl $0             /* 硬件中断没有错误码，手动压0对齐 */
    pushl $(32 + \num)   /* 硬件中断从32号向量开始 */
    SAVE_ALL
    call interrupt_handler
    RESTORE_ALL
.endm

/* ---------------- 生成0-31 CPU异常中断 ---------------- */
/* 0: 除法错误 - 无错误码 */
EXC_NOERR_MACRO 0
/* 1: 调试异常 - 无错误码 */
EXC_NOERR_MACRO 1
/* 2: NMI - 无错误码 */
EXC_NOERR_MACRO 2
/* 3: 断点 - 无错误码 */
EXC_NOERR_MACRO 3
/* 4: 溢出 - 无错误码 */
EXC_NOERR_MACRO 4
/* 5: 越界 - 无错误码 */
EXC_NOERR_MACRO 5
/* 6: 无效操作码 - 无错误码 */
EXC_NOERR_MACRO 6
/* 7: 设备不可用 - 无错误码 */
EXC_NOERR_MACRO 7
/* 8: 双重错误 - 有错误码 */
EXC_ERR_MACRO 8
/* 9: 协处理器段越界 - 无错误码 */
EXC_NOERR_MACRO 9
/* 10: 无效TSS - 有错误码 */
EXC_ERR_MACRO 10
/* 11: 段不存在 - 有错误码 */
EXC_ERR_MACRO 11
/* 12: 栈错误 - 有错误码 */
EXC_ERR_MACRO 12
/* 13: 通用保护 - 有错误码 */
EXC_ERR_MACRO 13
/* 14: 页错误 - 有错误码 */
EXC_ERR_MACRO 14
/* 15: Intel保留 - 跳过 */
EXC_NOERR_MACRO 15
/* 16: FPU浮点错误 - 无错误码 */
EXC_NOERR_MACRO 16
/* 17: 对齐检查 - 有错误码 */
EXC_ERR_MACRO 17
/* 18: 机器检查 - 无错误码 */
EXC_NOERR_MACRO 18
/* 19: SIMD浮点异常 - 无错误码 */
EXC_NOERR_MACRO 19
/* 20-31: Intel保留 */
.irp i, 20,21,22,23,24,25,26,27,28,29,30,31
EXC_NOERR_MACRO \i
.endr

/* ---------------- 生成32-47 硬件中断(16个IRQ) ---------------- */
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
IRQ_MACRO \i
.endr

/* ---------------- 128号中断 - 系统调用 ---------------- */
EXC_NOERR_MACRO 128
