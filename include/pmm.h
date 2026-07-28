/*
 * pmm.h - 物理内存管理器（Physical Memory Manager）接口头文件
 *
 * 设计思路：
 *   物理内存管理是操作系统内存子系统的最底层，职责是把 GRUB/Multiboot2
 *   报告的可用物理页（4KiB）以位图（bitmap）形式管理起来，向上提供“分配一页”、
 *   “释放一页”的接口。位图的优势是空间确定、没有链表指针开销、分配/释放
 *   都是 O(1) 的位操作，非常适合 x86-32 小内存场景的教学实现。
 */

#ifndef PMM_H
#define PMM_H

/* 页大小：x86 分页机制使用 4KiB 页 */
#define PMM_PAGE_SIZE   4096
#define PMM_PAGE_SHIFT  12

/*
 * 初始化物理内存管理器
 *   基于 multiboot2 信息中的内存映射，建立物理页位图。
 */
void pmm_init(unsigned int multiboot_info_addr);

/* 分配一个物理页，返回其物理地址；失败返回 0 */
void *pmm_alloc_page(void);

/* 释放一个物理页，page 必须是 4KiB 对齐的物理地址 */
void pmm_free_page(void *page);

/* 获取可用物理页总数（初始化后不再改变） */
unsigned int pmm_total_pages(void);

/* 获取当前空闲物理页数量 */
unsigned int pmm_free_pages(void);

/* 打印物理内存统计信息，方便调试 */
void pmm_dump_stats(void);

#endif /* PMM_H */
