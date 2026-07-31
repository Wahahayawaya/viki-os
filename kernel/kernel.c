#include "../include/multiboot2.h"
#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/interrupt.h"
#include "../include/memory.h"
#include "../include/pmm.h"
#include "../include/mmu.h"

/*
 * kernel_main - 内核主函数（C 语言入口）
 *
 * 启动流程：
 *   1. boot.S 在物理地址 1MB 设置初始页表，开启分页，
 *      跳转到高半核虚拟地址 0xC0100000+ 调用本函数
 *   2. 本函数依次初始化各子系统：VGA、GDT、中断、PMM、VMM
 *   3. 此后内核运行在高半核虚拟地址空间，用户态地址（0-3GB）预留
 *
 * 参数：
 *   magic - multiboot2 引导魔数，用于验证引导方式正确性
 *   addr  - multiboot2 信息结构体地址（物理地址，identity 映射保证可访问）
 */
void kernel_main(unsigned int magic, unsigned int addr) {
    /* 检查是否由 multiboot2 兼容的引导程序加载 */
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        return;
    }

    /* 初始化 VGA 文本输出模块（此时分页已开启，VGA 映射在高半核地址） */
    vga_init();

    /* 初始化串口，用于 QEMU 自动化输出验证 */
    serial_init();

    kprintf("Hi, I'm VIKI OS...\n");
    kprintf("Magic: 0x%x, Info Addr: 0x%x\n", magic, addr);

    /* 初始化 GDT（在高半核地址下工作，段描述符 base=0，依赖分页映射） */
    gdt_init();
    kprintf("GDT initialized successfully!\n");

    /* 初始化中断系统：IDT + 8259 PIC + 异常/中断处理函数 */
    interrupt_init();
    kprintf("Interrupt subsystem initialized successfully!\n");

    /* 打印 multiboot2 提供的物理内存布局 */
    memory_print_map(addr);

    /* 初始化物理内存管理器（基于 multiboot2 内存映射建立位图） */
    pmm_init(addr);
    kprintf("Physical memory manager initialized.\n");
    pmm_dump_stats();

    /* 简单分配测试：验证物理页分配/释放 */
    {
        void *p1, *p2;

        p1 = pmm_alloc_page();
        p2 = pmm_alloc_page();
        kprintf("pmm_alloc_page: p1=%p, p2=%p\n", p1, p2);
        pmm_dump_stats();

        pmm_free_page(p1);
        pmm_dump_stats();
        pmm_free_page(p2);
        pmm_dump_stats();
    }

    /*
     * 初始化虚拟内存管理
     * 验证分页已开启、高半核映射生效、递归映射可用
     * 并演示页面映射和访问功能
     */
    vmm_init();

    kprintf("Kernel entered protected mode with paging!\n");
    kprintf("Running in high-half kernel at 0xC0100000+\n");
    kprintf("System ready.\n");

    while (1) { __asm__ volatile ("hlt"); }
}
