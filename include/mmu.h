/*
 * mmu.h - 内存管理单元（Memory Management Unit）接口头文件
 *
 * 设计思路：
 *   本模块实现 x86-32 分页机制，采用类 Linux 的高半核（High-Half）
 *   设计：内核映射到 0xC0000000 以上的高 1GB 虚拟地址空间，
 *   低 3GB（0x00000000-0xBFFFFFFF）预留给未来的用户进程。
 *
 *   核心技术——递归页表映射（Recursive Page Table Mapping）：
 *   将 PDE[1023] 指向页目录自身，使得无需额外临时映射即可
 *   通过虚拟地址访问任意页目录/页表条目：
 *     - 访问页目录：  虚拟地址 0xFFFFF000（PDE[1023] 的 PTE[1023]）
 *     - 访问页表 i：  虚拟地址 0xFFC00000 + i * 4096（PDE[1023] 的 PTE[i]）
 *   原理：PDE[1023] 作为"页表"时，PTE[i] = PDE[i]，
 *         CPU 用 PDE[i] 的物理地址作为页表基址，
 *         再用虚拟地址的低 12 位作为页内偏移，
 *         最终访问的就是 PDE[i] 所指页表的条目。
 */

#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include "interrupt.h"

/* ============ 基本常量 ============ */

/* 页大小：x86 分页使用 4KiB 页 */
#define PAGE_SIZE   4096
#define PAGE_SHIFT  12

/* 内核虚拟地址基址：高半核设计，内核位于 0xC0000000 以上 */
#define KERNEL_VMA  0xC0000000

/* 页目录/页表条目数：每级 1024 个条目（32位地址 / 2级索引 / 2^10） */
#define PDE_COUNT 1024
#define PTE_COUNT 1024

/* ============ 递归映射地址 ============ */
/*
 * 递归映射虚拟地址：
 *   PDE[1023] 自映射，虚拟地址范围 0xFFC00000-0xFFFFFFFF
 *   用于访问页目录和页表，无需临时映射
 */

/* 页目录的虚拟地址：访问此地址等同于读取/修改 PDE 条目 */
#define PD_VADDR  0xFFFFF000

/* 页表 i 的虚拟地址：访问此地址等同于读取/修改 PTE 条目 */
#define PT_VADDR(i) (0xFFC00000 + (uint32_t)(i) * PAGE_SIZE)

/* ============ 页表项标志位 ============ */
/*
 * x86-32 页目录/页表条目格式（32位）：
 *   bit 0:     P  (Present)         页是否在物理内存中
 *   bit 1:     R/W (Read/Write)     0=只读, 1=可读写
 *   bit 2:     U/S (User/Supervisor) 0=内核态, 1=用户态可访问
 *   bit 3:     PWT (Write-Through)  写透策略
 *   bit 4:     PCD (Cache Disable)  禁用缓存
 *   bit 5:     A  (Accessed)        CPU 自动置位，表示已访问
 *   bit 6:     D  (Dirty)           CPU 自动置位，表示已写入
 *   bit 7:     PS (Page Size)       PDE中=1表示4MB大页
 *   bit 8:     G  (Global)          全局页，TLB不随CR3刷新
 *   bit 9-11:  Available           系统软件可用
 *   bit 12-31: Base Address         物理页帧地址（4K对齐）
 */

#define PAGE_PRESENT   0x001   /* 存在位：页在物理内存中 */
#define PAGE_RW        0x002   /* 读写位：1=可读写，0=只读 */
#define PAGE_USER      0x004   /* 用户态位：1=用户态可访问 */
#define PAGE_WRITETHRU 0x008   /* 写透策略 */
#define PAGE_NOCACHE   0x010   /* 禁用缓存 */
#define PAGE_ACCESSED  0x020   /* 已访问（CPU自动设置） */
#define PAGE_DIRTY     0x040   /* 已修改（CPU自动设置） */
#define PAGE_4MB       0x080   /* 4MB大页（仅PDE） */
#define PAGE_GLOBAL    0x100   /* 全局页（TLB不刷新） */

/* 页表项物理地址掩码：高 20 位为物理页帧号 */
#define PAGE_ADDR_MASK 0xFFFFF000

/* ============ 对外接口 ============ */

/*
 * vmm_init - 初始化虚拟内存管理
 *
 * 在 boot.S 已设置初始页表的基础上：
 *   1. 读取 CR0/CR3 确认分页已开启
 *   2. 验证内核运行在高半核虚拟地址
 *   3. 演示页面映射和访问
 *   4. 打印页表映射信息
 */
void vmm_init(void);

/*
 * vmm_map_page - 将一个虚拟页映射到物理页
 *
 * 参数：
 *   virt  - 虚拟地址（自动对齐到 4KB 边界）
 *   phys  - 物理地址（自动对齐到 4KB 边界）
 *   flags - 标志位（PAGE_PRESENT | PAGE_RW | PAGE_USER 等）
 *
 * 如果对应 PDE 不存在，自动从 PMM 分配页表物理页。
 * 通过递归映射访问新分配的页表，无需临时映射。
 */
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/*
 * vmm_get_phys - 查询虚拟地址对应的物理地址
 *
 * 通过递归映射遍历页表，返回映射的物理地址。
 * 若未映射，返回 0。
 */
uint32_t vmm_get_phys(uint32_t virt);

/*
 * page_fault_handler - 页错误处理函数
 *
 * 读取 CR2 获取引发页错误的虚拟地址，
 * 解析错误码，打印详细诊断信息。
 * 由 interrupt.c 的中断分发器在 vector 14 时调用。
 */
void page_fault_handler(struct pt_regs *regs);

#endif /* MMU_H */
