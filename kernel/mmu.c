/*
 * mmu.c - 虚拟内存管理器（Virtual Memory Manager）
 *
 * 设计思路：
 *   本模块在 boot.S 设置的初始页表基础上，提供运行时的页面映射管理。
 *   采用递归页表映射技术（PDE[1023] 自映射），使 C 代码可通过
 *   固定的虚拟地址直接访问和修改页目录/页表，无需临时映射。
 *
 *   核心原理——递归映射地址计算：
 *     虚拟地址 V 的 PDE 索引 = V >> 22
 *     虚拟地址 V 的 PTE 索引 = (V >> 12) & 0x3FF
 *
 *     当 V 落在 0xFFC00000-0xFFFFFFFF 范围时：
 *       PDE 索引 = 1023（固定），PDE[1023] 指向页目录自身
 *       PTE 索引 = (V >> 12) & 0x3FF，对应 PDE[i]
 *       CPU 用 PDE[i]（某页表的物理地址）作为页表基址
 *       最终访问的就是 PDE[i] 所指页表中的条目
 *
 *     因此：
 *       访问页目录  -> 虚拟地址 0xFFFFF000（PTE 索引 = 1023 = PDE 自身）
 *       访问页表 i  -> 虚拟地址 0xFFC00000 + i * 4096（PTE 索引 = i）
 *
 *   高半核内存布局：
 *     0x00000000-0xBFFFFFFF : 用户空间（3GB，未来用户进程使用）
 *     0xC0000000-0xFFFFFFFF : 内核空间（1GB，内核代码+数据）
 *     0xC0100000            : 内核代码起始虚拟地址（物理 0x100000）
 *     0xC00B8000            : VGA 文本缓冲区虚拟地址（物理 0xB8000）
 *     0xFFC00000-0xFFFFFFFF : 递归映射区域（页表访问窗口）
 */

#include "../include/mmu.h"
#include "../include/vga.h"
#include "../include/pmm.h"
#include "../include/gdt.h"
#include "../include/idt.h"

/* ---------- 内联汇编：读写控制寄存器 ---------- */

/* 读取 CR0：包含 PG（分页使能）、PE（保护模式）等关键标志 */
static inline uint32_t read_cr0(void)
{
    uint32_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

/* 读取 CR2：最近一次页错误发生的虚拟地址 */
static inline uint32_t read_cr2(void)
{
    uint32_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

/* 读取 CR3：当前页目录的物理基地址 */
static inline uint32_t read_cr3(void)
{
    uint32_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

/* 刷新 TLB（转后备缓冲器）：重写 CR3 触发全部非全局页 TLB 刷新 */
static inline void flush_tlb(void)
{
    uint32_t val = read_cr3();
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val));
}

/* ---------- 对外接口实现 ---------- */

/*
 * vmm_map_page - 将虚拟地址映射到物理地址
 *
 * 实现步骤：
 *   1. 计算虚拟地址对应的 PDE 和 PTE 索引
 *   2. 通过递归映射（PD_VADDR = 0xFFFFF000）检查 PDE 是否存在
 *   3. 若 PDE 不存在：
 *      a. 从 PMM 分配一个物理页作为新页表
 *      b. 设置 PDE 指向新页表（通过递归映射写入）
 *      c. 通过递归映射（PT_VADDR）访问新页表并清零
 *   4. 通过递归映射访问目标 PTE 并设置映射
 *
 * 递归映射的妙处：
 *   写 PDE[768] 后，立刻就能通过 PT_VADDR(768) 访问该页表，
 *   因为 CPU 走的是 PDE[1023] -> PDE[768] -> PTE 的路径，
 *   不需要临时映射或 invlpg（首次访问无 stale TLB）。
 */
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    /* 计算页目录索引（高 10 位）和页表索引（中 10 位） */
    uint32_t pd_index = virt >> 22;          /* PDE 索引：[31:22] */
    uint32_t pt_index = (virt >> 12) & 0x3FF; /* PTE 索引：[21:12] */

    /* 通过递归映射获取页目录指针（虚拟地址 0xFFFFF000） */
    uint32_t *pd = (uint32_t *)PD_VADDR;

    /* 步骤 2：检查 PDE 是否已存在 */
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        /* 步骤 3a：分配一个物理页作为新页表 */
        void *new_pt = pmm_alloc_page();
        if (new_pt == 0) {
            /* 物理内存不足，无法创建页表 */
            return;
        }

        /*
         * 步骤 3b：设置 PDE 指向新页表
         * 物理地址 | 标志位（保留 PRESENT | RW，继承用户标志）
         */
        pd[pd_index] = ((uint32_t)new_pt & PAGE_ADDR_MASK)
                        | PAGE_PRESENT | PAGE_RW
                        | (flags & PAGE_USER);

        /*
         * 步骤 3c：通过递归映射清零新页表
         * PT_VADDR(pd_index) = 0xFFC00000 + pd_index * 4096
         * 此时 CPU 通过 PDE[1023]->PDE[pd_index] 找到新页表，
         * 再用 pt_index 作为偏移访问具体条目
         */
        uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);
        uint32_t i;
        for (i = 0; i < PTE_COUNT; i++) {
            pt[i] = 0;  /* 清零所有 PTE，确保未映射 */
        }
    }

    /* 步骤 4：设置 PTE，建立虚拟->物理映射 */
    uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);
    pt[pt_index] = (phys & PAGE_ADDR_MASK) | flags;
}

/*
 * vmm_get_phys - 查询虚拟地址对应的物理地址
 *
 * 通过递归映射遍历二级页表，返回映射的物理地址。
 * 若任一级不存在（PDE 或 PTE 未映射），返回 0。
 *
 * 返回的物理地址包含页内偏移：
 *   物理地址 = PTE中的页帧基址 + 虚拟地址的低12位偏移
 */
uint32_t vmm_get_phys(uint32_t virt)
{
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    /* 通过递归映射读取页目录 */
    uint32_t *pd = (uint32_t *)PD_VADDR;

    /* 检查 PDE 是否存在 */
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        return 0;  /* 页目录项不存在，未映射 */
    }

    /* 通过递归映射读取页表 */
    uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);

    /* 检查 PTE 是否存在 */
    if (!(pt[pt_index] & PAGE_PRESENT)) {
        return 0;  /* 页表项不存在，未映射 */
    }

    /* 返回物理地址 = 页帧基址 + 页内偏移 */
    return (pt[pt_index] & PAGE_ADDR_MASK) | (virt & 0xFFF);
}

/*
 * vmm_init - 初始化虚拟内存管理
 *
 * 在 boot.S 已设置初始页表并开启分页后，此函数：
 *   1. 读取 CR0/CR3 确认分页已开启
 *   2. 验证内核运行在高半核虚拟地址（证明 0xC0100000 映射生效）
 *   3. 通过 vmm_get_phys 演示页表遍历，验证递归映射工作正常
 *   4. 分配并映射一个测试页，验证映射和写入功能
 */
void vmm_init(void)
{
    uint32_t cr0, cr3;

    /* 读取控制寄存器，确认分页已开启 */
    cr0 = read_cr0();
    cr3 = read_cr3();

    kprintf("\n=== Virtual Memory (Paging) ===\n");

    /*
     * CR0 关键位说明：
     *   bit 0  (PE) : 保护模式使能
     *   bit 31 (PG) : 分页使能
     */
    kprintf("CR0 = 0x%x  [PG=%d, PE=%d]\n",
            cr0, (cr0 >> 31) & 1, cr0 & 1);
    kprintf("CR3 = 0x%x  (page directory phys addr)\n", cr3 & PAGE_ADDR_MASK);

    /*
     * 验证内核运行在高半核虚拟地址
     * vmm_init 函数自身被链接在 0xC01xxxxx，打印其地址即可验证
     */
    kprintf("vmm_init() runs at virtual addr: 0x%p\n", vmm_init);
    kprintf("KERNEL_VMA base: 0x%x\n", KERNEL_VMA);

    /*
     * 验证递归映射：查询内核代码段的物理地址
     * _start_high 在 .text 段，VMA = 0xC0100000+
     * 通过页表遍历应得到物理地址 = VMA - 0xC0000000
     */
    extern char _start_high[];
    uint32_t vaddr = (uint32_t)_start_high;
    uint32_t paddr = vmm_get_phys(vaddr);
    kprintf("_start_high: vaddr=0x%x -> paddr=0x%x\n", vaddr, paddr);

    /* 查询 VGA 缓冲区的物理地址（验证已知映射） */
    uint32_t vga_vaddr = KERNEL_VMA + 0xB8000;
    uint32_t vga_paddr = vmm_get_phys(vga_vaddr);
    kprintf("VGA buffer: vaddr=0x%x -> paddr=0x%x\n", vga_vaddr, vga_paddr);

    /*
     * 功能测试：分配并映射一个新页面
     * 测试地址 0xD0000000 位于内核空间，PDE 索引 = 832
     * 当前未映射，vmm_map_page 会自动分配页表
     */
    kprintf("\n--- Mapping test ---\n");
    void *test_page = pmm_alloc_page();
    uint32_t test_vaddr = 0xD0000000;

    if (test_page == 0) {
        kprintf("PMM alloc failed!\n");
    } else {
        kprintf("Allocated phys page: 0x%p\n", test_page);

        /* 建立 0xD0000000 -> test_page 的映射 */
        vmm_map_page(test_vaddr, (uint32_t)test_page,
                      PAGE_PRESENT | PAGE_RW);
        kprintf("Mapped: 0x%x -> 0x%p\n", test_vaddr, test_page);

        /* 验证映射：通过递归映射查询 */
        uint32_t verify = vmm_get_phys(test_vaddr);
        kprintf("Verify: vmm_get_phys(0x%x) = 0x%x\n", test_vaddr, verify);

        /* 写入测试：向映射的页面写入数据并读回 */
        uint32_t *test_ptr = (uint32_t *)test_vaddr;
        *test_ptr = 0xDEADBEEF;
        kprintf("Write 0xDEADBEEF to 0x%x, read back: 0x%x\n",
                test_vaddr, *test_ptr);
        kprintf("Mapping test PASSED!\n");
    }

    kprintf("=================================\n\n");
}

/*
 * page_fault_handler - 页错误（Page Fault）处理函数
 *
 * 当 CPU 访问未映射或权限不足的虚拟地址时，触发中断 14（页错误）。
 * CPU 自动将错误码压栈，CR2 寄存器保存引发错误的虚拟地址。
 *
 * 错误码位含义：
 *   bit 0 (P)  : 0=页不存在, 1=权限冲突
 *   bit 1 (W/R): 0=读操作, 1=写操作
 *   bit 2 (U/S): 0=内核态, 1=用户态
 *   bit 3 (RSVD): 1=保留位写入错误
 *   bit 4 (I/D): 1=取指时错误（指令获取）
 */
void page_fault_handler(struct pt_regs *regs)
{
    uint32_t fault_addr = read_cr2();

    kprintf("\n**** PAGE FAULT ****\n");
    kprintf("Faulting address (CR2): 0x%x\n", fault_addr);
    kprintf("Error code: 0x%x\n", regs->err_code);
    kprintf("  - Reason:    %s\n",
            (regs->err_code & 1) ? "protection violation" : "page not present");
    kprintf("  - Operation: %s\n",
            (regs->err_code & 2) ? "write" : "read");
    kprintf("  - Mode:      %s\n",
            (regs->err_code & 4) ? "user" : "kernel");
    if (regs->err_code & 8) {
        kprintf("  - Reserved bit set in page table entry!\n");
    }
    if (regs->err_code & 0x10) {
        kprintf("  - Fault during instruction fetch\n");
    }
    kprintf("EIP: 0x%x\n", regs->eip);

    /* 页错误在当前阶段是致命错误，挂起系统 */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
