/*
 * boot_paging.c - 引导阶段分页初始化（C 语言实现）
 *
 * 设计思路：
 *   本文件替代原 boot.S 中的 setup_paging 汇编函数。
 *   通过 __attribute__((section(".boot"))) 将 C 函数放入 .boot 段，
 *   使其在分页开启前运行于物理地址（VMA = LMA）。
 *
 *   为什么必须放在 .boot 段：
 *     C 编译器将函数中的符号引用解析为 VMA。setup_paging_c 引用的
 *     boot_pde、boot_pt0 位于 .boot.bss（VMA = 物理地址），因此引用正确。
 *     若本函数在 .text 段（VMA = 0xC0107000），分页未开启时调用它
 *     会跳转到物理 0xC0107000，该地址无内核代码，导致崩溃。
 *
 *   为什么不能用 PMM 分配页表：
 *     此函数在 kernel_main 之前执行，PMM 尚未初始化（pmm_init 在
 *     kernel_main 中调用）。页表 boot_pde/boot_pt0 在 boot.S 的
 *     .boot.bss 中静态声明，编译时地址确定，无需运行时分配。
 *
 * 页表布局（与原汇编版本完全一致）：
 *   PDE[0]     -> boot_pt0 | PRESENT|RW  : identity 映射 0-4MB
 *   PDE[768]   -> boot_pt0 | PRESENT|RW  : 高半核 0xC0000000-0xC03FFFFF -> 0-4MB
 *   PDE[1023]  -> boot_pde | PRESENT|RW  : 递归映射（页目录自映射）
 */

#include <stdint.h>

/* 页表项标志位 */
#define BOOT_PAGE_PRESENT  0x001
#define BOOT_PAGE_RW       0x002

/*
 * boot_pde / boot_pt0 在 boot.S 的 .boot.bss 段中静态声明。
 * 由于 VMA = LMA = 物理地址，C 代码直接通过符号名访问即可。
 */
extern uint32_t boot_pde[1024];
extern uint32_t boot_pt0[1024];

/*
 * setup_paging_c - 设置初始页目录和页表（C 语言版本）
 *
 * 实现步骤：
 *   1. 清零页目录（所有 1024 个 PDE 初始化为 0，表示"不存在"）
 *   2. 填充页表 boot_pt0：PTE[i] = (i * 4096) | PRESENT | RW
 *      即物理页 i 映射到虚拟页 i（identity 映射前 4MB）
 *   3. 设置 PDE[0]：identity 映射，使引导代码在分页开启后仍可执行
 *   4. 设置 PDE[768]：高半核映射，0xC0000000 -> 0x00000000
 *   5. 设置 PDE[1023]：递归映射，使 C 代码可通过 0xFFFFF000 访问页目录
 *
 * 返回值：页目录的物理地址（通过 %eax 返回，供 boot.S 写入 CR3）
 *
 * 调用约定：cdecl（boot.S 通过 call 调用，返回值在 %eax）
 */
__attribute__((section(".boot"), noinline))
uint32_t setup_paging_c(void)
{
    int i;

    /* 步骤 1：清零页目录，所有 PDE 初始为"不存在" */
    for (i = 0; i < 1024; i++) {
        boot_pde[i] = 0;
    }

    /*
     * 步骤 2：填充页表，identity 映射前 4MB
     * PTE[i] = (i * 4096) | PRESENT | RW
     * 物理页 0 -> 虚拟页 0, 物理页 1 -> 虚拟页 1, ...
     */
    for (i = 0; i < 1024; i++) {
        boot_pt0[i] = (uint32_t)(i * 4096) | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;
    }

    /*
     * 步骤 3：PDE[0] — identity 映射 0-4MB
     * 虚拟 0x00000000-0x003FFFFF -> 物理 0x00000000-0x003FFFFF
     * 保证开启分页后，当前 EIP 所在的引导代码仍可取指执行
     */
    boot_pde[0] = (uint32_t)boot_pt0 | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /*
     * 步骤 4：PDE[768] — 高半核映射
     * 虚拟 0xC0000000-0xC03FFFFF -> 物理 0x00000000-0x003FFFFF
     * 内核在物理 1MB，映射后虚拟地址为 KERNEL_VMA + 1MB
     * VGA 缓冲区在物理 0xB8000，映射后虚拟地址为 KERNEL_VMA + 0xB8000
     */
    boot_pde[768] = (uint32_t)boot_pt0 | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /*
     * 步骤 5：PDE[1023] — 递归映射
     * PDE[1023] 指向页目录自身，使 C 代码运行时可通过：
     *   虚拟 0xFFFFF000  访问页目录（PDE[1023] 的 PTE[1023] = PDE 自身）
     *   虚拟 0xFFC00000 + i*4096  访问页表 i
     */
    boot_pde[1023] = (uint32_t)boot_pde | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /* 返回页目录物理地址，供 boot.S 写入 CR3 寄存器 */
    return (uint32_t)boot_pde;
}
