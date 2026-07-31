/*
 * boot.S - 高半核内核引导代码
 *
 * 设计思路（类 Linux 高半核启动流程）：
 *   1. GRUB 在物理地址 1MB 加载内核，跳转到 _start（物理地址，无分页）
 *   2. _start 设置初始页目录和页表：
 *      - PDE[0]：identity 映射前 4MB，使引导代码继续执行
 *      - PDE[768]：映射 0xC0000000 -> 0x00000000，内核高半核虚拟地址
 *      - PDE[1023]：递归映射，页目录自映射，方便 C 代码遍历页表
 *   3. 写 CR3 加载页目录基址，置 CR0.PG 位开启分页
 *   4. 跳转到高虚拟地址 _start_high，此后内核运行在 0xC0100000+
 *   5. _start_high 调用 C 语言 kernel_main
 *
 * 为什么需要 identity 映射：
 *   开启分页的那条 mov %cr0 指令本身位于物理地址，
 *   CPU 取指需要通过虚拟地址访问，identity 映射保证
 *   当前 EIP 所在的物理页在虚拟地址空间可见。
 */

#define ASM_FILE 1
#include "../include/multiboot2.h"

/* 引导栈大小：16KB */
#define STACK_SIZE 0x4000

/* 内核虚拟地址偏移 */
#define KERNEL_VMA 0xC0000000

/* 声明外部C语言内核入口函数 */
.extern kernel_main
/* 声明C语言实现的页表设置函数（在 kernel/boot_paging.c 中） */
.extern setup_paging_c

/* 定义全局符号，供链接器使用 */
.globl _start
/* 导出页表符号供 C 代码访问（boot_paging.c 引用） */
.globl boot_pde
.globl boot_pt0

/* ============ 引导段：物理地址 1MB ============ */
.section .boot
.code32

/*
 * 内核入口点，由 GRUB 跳转到这里
 * 入口时：
 *   %eax = MULTIBOOT2_BOOTLOADER_MAGIC (0x36d76289)
 *   %ebx = multiboot2 信息结构体物理地址
 *   %esp = 未定义（由引导程序设置）
 */
_start:
    /* 设置引导栈指针到物理地址（.boot.bss 中，VMA=LMA=物理地址） */
    movl $boot_stack_top, %esp

    /* 保存 GRUB 传入的 multiboot2 魔数和信息指针到栈上 */
    push %ebx                /* 第二个参数：multiboot 信息结构体指针 */
    push %eax                /* 第一个参数：multiboot 魔数 */

    /*
     * 设置初始页表：调用 C 语言实现的 setup_paging_c
     * 该函数通过 __attribute__((section(".boot"))) 放入 .boot 段，
     * 运行在物理地址，可直接访问 boot_pde/boot_pt0（VMA=物理地址）
     * 返回值 %eax = 页目录物理地址
     */
    call setup_paging_c

    /* 加载 CR3：设置页目录基址寄存器 */
    movl %eax, %cr3

    /*
     * 开启分页：设置 CR0 的 PG 位（bit 31）
     * 此后 CPU 通过虚拟地址访问内存
     * identity 映射保证当前 EIP 所在物理页仍可访问
     */
    movl %cr0, %eax
    orl $0x80000000, %eax     /* PG = 1 */
    movl %eax, %cr0

    /*
     * 跳转到高虚拟地址 _start_high
     * _start_high 在 .text 段（VMA = 0xC01xxxxx），
     * 通过 PDE[768] 映射到物理 0x01xxxxx
     */
    lea _start_high, %ecx
    jmp *%ecx

/* ============ multiboot2 头信息 ============ */
.section .multiboot
.align MULTIBOOT_HEADER_ALIGN
.type multiboot_header, @object
multiboot_header:
    /* 多重引导魔数 */
    .long MULTIBOOT2_HEADER_MAGIC
    /* 架构类型（i386） */
    .long MULTIBOOT_ARCHITECTURE_I386
    /* 头部长度 */
    .long multiboot_header_end - multiboot_header
    /* 校验和（magic + architecture + length + checksum = 0） */
    .long -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT_ARCHITECTURE_I386 + (multiboot_header_end - multiboot_header))
multiboot_request_begin:
    .align 8
    /* 信息请求标签 - 同时请求基本内存信息和内存映射 */
    .short MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST
    .short 0  /* 不是可选的，必须存在 */
    .long  multiboot_request_end - multiboot_request_begin
    .long MULTIBOOT_TAG_TYPE_BASIC_MEMINFO
    .long MULTIBOOT_TAG_TYPE_MMAP
multiboot_request_end:
    /* 结束标签 */
    .align 8
    .short MULTIBOOT_HEADER_TAG_END
    .short 0
    .long 8   /* 标签大小 */

multiboot_header_end:

/* ============ 引导段 BSS：页表和栈 ============ */
.section .boot.bss
.align 4096

/* 初始页目录（4KB，1024 个条目） */
boot_pde:
    .skip 4096

/* 初始页表（4KB，1024 个条目，映射前 4MB） */
boot_pt0:
    .skip 4096

/* 引导栈空间（16KB） */
.align 16
boot_stack_bottom:
    .skip STACK_SIZE
boot_stack_top:

/* ============ 内核主体段：高虚拟地址 0xC0100000+ ============ */
.section .text
.code32
.globl _start_high

/*
 * _start_high - 分页开启后的高半核入口
 *
 * 此时 EIP 已在 0xC01xxxxx 高虚拟地址，
 * 引导栈仍在物理地址 boot_stack_top（identity 映射保证可访问）。
 * 从栈上恢复 GRUB 传入的 multiboot 参数，调用 kernel_main。
 */
_start_high:
    /* 栈仍在 boot_stack_top（物理地址，PDE[0] identity 映射） */
    /* 恢复 GRUB 传入的 multiboot 魔数和信息指针 */
    pop %eax                   /* %eax = multiboot 魔数 */
    pop %ebx                   /* %ebx = multiboot 信息指针 */

    /* 按 cdecl 调用约定压栈：右参数先压 */
    push %ebx                  /* 第二个参数：multiboot 信息结构体指针 */
    push %eax                  /* 第一个参数：multiboot 魔数 */
    call kernel_main
    add $8, %esp               /* 清理栈（两个 32 位参数） */

    /* 如果 kernel_main 返回，则进入无限循环 */
_hang:
    hlt
    jmp _hang
