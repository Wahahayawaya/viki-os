/*
 * pmm.c - 物理内存管理器（Bitmap 页帧分配器）
 *
 * 设计思路：
 *   1. 以 4KiB 为一页，把整个 4GB 物理地址空间抽象为 2^20 个页帧。
 *   2. 使用一个静态位图 pmm_bitmap 记录每个页帧是否被占用。
 *      位图中 bit=1 表示“已占用/不可分配”，bit=0 表示“空闲可分配”。
 *   3. 初始化时：
 *      - 先把所有页帧标记为已占用（保守策略，避免误用未知内存）；
 *      - 然后遍历 Multiboot2 内存映射，把 type==AVAILABLE 且
 *        高于内核末尾（_kernel_end）的物理页标记为可用；
 *      - 内核自身镜像、栈、multiboot 信息等位于 _kernel_end 以下，
 *        始终保留，不会被分配。
 *   4. 分配页帧时扫描位图找到第一个 0 位并置 1；释放时清 0。
 *   5. 使用 pmm_next_free 维护下一个搜索起点，避免每次从 0 开始扫描，
 *      将平均复杂度从 O(N) 降到接近 O(1)。
 */

#include "../include/pmm.h"
#include "../include/vga.h"
#include "../include/multiboot2.h"

/* 本地类型定义，避免依赖标准库 */
typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;

/* 物理地址空间上限（x86-32 无 PAE 时为 4GB） */
#define PMM_MAX_PHYS     (1ULL << 32)
/* 总页帧数 = 4GB / 4KB = 1,048,576 */
#define PMM_MAX_FRAMES   (PMM_MAX_PHYS / PMM_PAGE_SIZE)
/* 位图字节数 = 总页帧数 / 8 = 128KB */
#define PMM_BITMAP_SIZE  (PMM_MAX_FRAMES / 8)

/* 位图本身放在 .bss 段，链接脚本已保证其位于内核镜像内，因此会被保留 */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* 物理内存统计 */
static uint32_t pmm_total = 0;  /* 初始化后可用页总数 */
static uint32_t pmm_free  = 0;  /* 当前空闲页数 */

/* 内核末尾页帧号，所有低于该地址的页帧都不应被分配 */
static uint32_t kernel_end_frame = 0;

/* 下一次分配时从该页帧开始扫描，减少重复遍历 */
static uint32_t pmm_next_free = 0;

/* 链接脚本中导出的内核结束符号，表示内核镜像及 .bss 的结束地址 */
extern char _kernel_end[];

/* ---------- 位图辅助函数 ---------- */

/* 将指定页帧标记为已占用（置 1） */
static void pmm_bitmap_set(uint32_t frame)
{
    pmm_bitmap[frame >> 3] |= (uint8_t)(1u << (frame & 7));
}

/* 将指定页帧标记为空闲（清 0） */
static void pmm_bitmap_clear(uint32_t frame)
{
    pmm_bitmap[frame >> 3] &= (uint8_t)~(1u << (frame & 7));
}

/* 查询指定页帧是否被占用（bit=1） */
static int pmm_bitmap_test(uint32_t frame)
{
    return (pmm_bitmap[frame >> 3] >> (frame & 7)) & 1;
}

/* ---------- 工具函数 ---------- */

/* 向上取整对齐到 4KiB */
static uint64_t align_up_4k(uint64_t addr)
{
    return (addr + (PMM_PAGE_SIZE - 1)) & ~((uint64_t)PMM_PAGE_SIZE - 1);
}

/* 向下取整对齐到 4KiB */
static uint64_t align_down_4k(uint64_t addr)
{
    return addr & ~((uint64_t)PMM_PAGE_SIZE - 1);
}

/* 在 Multiboot2 信息结构中寻找内存映射标签 */
static struct multiboot_tag_mmap *pmm_find_mmap(unsigned int multiboot_info_addr)
{
    uint8_t *info_base = (uint8_t *)multiboot_info_addr;
    struct multiboot_tag *tag;
    struct multiboot_tag_mmap *mmap_tag = 0;

    /* 跳过 multiboot2 信息头 8 字节：total_size(4) + reserved(4) */
    for (tag = (struct multiboot_tag *)(info_base + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (struct multiboot_tag_mmap *)tag;
            break;
        }
    }

    return mmap_tag;
}

/* ---------- 对外接口 ---------- */

/*
 * pmm_init - 初始化物理内存管理器
 *
 * 实现步骤：
 *   1. 将位图全部置 1（全部保留）。
 *   2. 计算内核结束地址对应的页帧号。
 *   3. 解析 Multiboot2 内存映射。
 *   4. 对每个 AVAILABLE 区域：
 *        - 跳过内核已占用的低地址部分；
 *        - 按页边界裁剪；
 *        - 将范围内的页帧位图清 0，并统计可用页数。
 */
void pmm_init(unsigned int multiboot_info_addr)
{
    uint32_t i;
    uint64_t kernel_end;
    struct multiboot_tag_mmap *mmap_tag;
    struct multiboot_mmap_entry *entry;
    uint32_t entry_count;

    /* 步骤 1：保守地先将所有页帧标记为已占用 */
    for (i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    /* 步骤 2：获取内核结束地址并计算页帧号（向上取整） */
    /* _kernel_end 是链接脚本导出的符号，取地址后得到内核结束物理地址 */
    kernel_end = (uint64_t)(uint32_t)(unsigned long)&_kernel_end[0];
    kernel_end_frame = (uint32_t)(kernel_end >> PMM_PAGE_SHIFT);
    if (kernel_end & (PMM_PAGE_SIZE - 1)) {
        kernel_end_frame++;
    }
    pmm_next_free = kernel_end_frame;

    /* 步骤 3：获取 Multiboot2 内存映射 */
    mmap_tag = pmm_find_mmap(multiboot_info_addr);
    if (mmap_tag == 0) {
        /* 没有内存映射时无法安全分配内存，保持全部保留状态 */
        return;
    }

    /* multiboot_tag_mmap 结构大小 16 字节，后面紧跟若干 entry */
    entry_count = (mmap_tag->size - 16) / mmap_tag->entry_size;
    entry = (struct multiboot_mmap_entry *)((uint8_t *)mmap_tag + 16);

    /* 步骤 4：遍历内存映射，把可用且安全的物理页释放 */
    for (i = 0; i < entry_count; i++) {
        uint64_t start, end;

        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) {
            entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + mmap_tag->entry_size);
            continue;
        }

        start = entry->addr;
        end = entry->addr + entry->len;

        /* 仅管理内核上方的内存；低于内核结束地址的部分保留 */
        if (end <= kernel_end) {
            entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + mmap_tag->entry_size);
            continue;
        }
        if (start < kernel_end) {
            start = kernel_end;
        }

        /* 对齐到 4KiB 边界 */
        start = align_up_4k(start);
        end = align_down_4k(end);

        /* 限制在 4GB 范围内 */
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }

        if (end <= start) {
            entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + mmap_tag->entry_size);
            continue;
        }

        /* 将该区域内的页帧标记为可用 */
        while (start < end) {
            uint32_t frame = (uint32_t)(start >> PMM_PAGE_SHIFT);

            if (frame >= PMM_MAX_FRAMES) {
                break;
            }

            /* 仅当该页帧此前未释放时才计数（理论上都是首次释放） */
            if (pmm_bitmap_test(frame)) {
                pmm_bitmap_clear(frame);
                pmm_total++;
                pmm_free++;
            }

            start += PMM_PAGE_SIZE;
        }

        entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + mmap_tag->entry_size);
    }
}

/*
 * pmm_alloc_page - 分配一个 4KiB 物理页
 *
 * 从 pmm_next_free 开始扫描位图，找到第一个空闲页帧后占用它，
 * 并返回对应的物理地址。若已无空闲页，返回 0。
 */
void *pmm_alloc_page(void)
{
    uint32_t frame;
    void *page;

    if (pmm_free == 0) {
        return 0;
    }

    /* 从上次扫描位置开始寻找空闲页帧 */
    for (frame = pmm_next_free; frame < PMM_MAX_FRAMES; frame++) {
        if (!pmm_bitmap_test(frame)) {
            pmm_bitmap_set(frame);
            pmm_free--;
            /* 记录下一个搜索起点，避免重复扫描已分配页 */
            pmm_next_free = frame + 1;
            page = (void *)((uint32_t)frame << PMM_PAGE_SHIFT);
            return page;
        }
    }

    /* 到达末尾仍未找到，尝试从 kernel_end_frame 到 pmm_next_free 之间查找 */
    for (frame = kernel_end_frame; frame < pmm_next_free; frame++) {
        if (!pmm_bitmap_test(frame)) {
            pmm_bitmap_set(frame);
            pmm_free--;
            pmm_next_free = frame + 1;
            page = (void *)((uint32_t)frame << PMM_PAGE_SHIFT);
            return page;
        }
    }

    return 0;
}

/*
 * pmm_free_page - 释放一个 4KiB 物理页
 *
 * 仅接受 4KiB 对齐、位于内核上方的物理地址；
 * 重复释放不会导致计数错误（通过 bit 判断）。
 */
void pmm_free_page(void *page)
{
    uint32_t addr;
    uint32_t frame;

    if (page == 0) {
        return;
    }

    addr = (uint32_t)page;

    /* 检查页对齐 */
    if (addr & (PMM_PAGE_SIZE - 1)) {
        return;
    }

    frame = addr >> PMM_PAGE_SHIFT;

    /* 不能释放内核区域或超出位图范围的地址 */
    if (frame < kernel_end_frame || frame >= PMM_MAX_FRAMES) {
        return;
    }

    /* 若当前为占用状态才释放，避免 double free 导致计数错误 */
    if (pmm_bitmap_test(frame)) {
        pmm_bitmap_clear(frame);
        pmm_free++;
        if (frame < pmm_next_free) {
            pmm_next_free = frame;
        }
    }
}

/* 返回初始化时统计的可用物理页总数 */
unsigned int pmm_total_pages(void)
{
    return pmm_total;
}

/* 返回当前空闲物理页数量 */
unsigned int pmm_free_pages(void)
{
    return pmm_free;
}

/* 打印物理内存统计，便于在 QEMU 中观察 */
void pmm_dump_stats(void)
{
    kprintf("PMM stats: total=%u pages (%u KB), free=%u pages (%u KB)\n",
            pmm_total, pmm_total * 4,
            pmm_free, pmm_free * 4);
}
