#include "../include/memory.h"
#include "../include/vga.h"
#include "../include/multiboot2.h"

/* 本文件使用64位整数处理物理地址和长度，因为multiboot2内存映射
 * 中的基址和长度都是64位 */
typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

/* 将内存类型编号转换为可读的英文字符串，输出中不能包含中文 */
static const char *memory_type_name(uint32_t type)
{
    switch (type) {
    case MULTIBOOT_MEMORY_AVAILABLE:
        return "available";
    case MULTIBOOT_MEMORY_RESERVED:
        return "reserved";
    case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
        return "acpi_reclaimable";
    case MULTIBOOT_MEMORY_NVS:
        return "nvs";
    case MULTIBOOT_MEMORY_BADRAM:
        return "badram";
    default:
        return "unknown";
    }
}

/* 解析并打印multiboot2信息中的物理内存布局
 * multiboot_info_addr: bootloader传递的multiboot2信息结构体地址
 *
 * multiboot2信息结构体格式：
 *   [0..3]  total_size:  整个信息区域的大小
 *   [4..7]  reserved:    保留为0
 *   [8..]   标签数组，每个标签按8字节对齐
 *
 * 我们关心两类标签：
 *   MULTIBOOT_TAG_TYPE_BASIC_MEMINFO：低1MB内存和扩展内存大小
 *   MULTIBOOT_TAG_TYPE_MMAP：完整的物理内存映射
 */
void memory_print_map(unsigned int multiboot_info_addr)
{
    struct multiboot_tag *tag;
    struct multiboot_tag_basic_meminfo *meminfo = 0;
    struct multiboot_tag_mmap *mmap_tag = 0;
    struct multiboot_mmap_entry *entry;
    uint8_t *info_base;
    uint32_t entry_count;
    uint32_t i;

    info_base = (uint8_t *)multiboot_info_addr;

    /* 打印表头，全部使用英文 */
    kprintf("=== Physical Memory Layout ===\n");

    /* 遍历multiboot2标签，直到END标签或超出信息区域 */
    for (tag = (struct multiboot_tag *)(info_base + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {

        if (tag->type == MULTIBOOT_TAG_TYPE_BASIC_MEMINFO) {
            meminfo = (struct multiboot_tag_basic_meminfo *)tag;
        } else if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (struct multiboot_tag_mmap *)tag;
        }
    }

    /* 打印BIOS提供的简单内存信息，单位转换为字节 */
    if (meminfo != 0) {
        kprintf("Basic memory info:\n");
        kprintf("  lower (below 1MB): %u bytes\n",
                (uint32_t)((uint64_t)meminfo->mem_lower * 1024));
        kprintf("  upper (above 1MB): %u bytes\n",
                (uint32_t)((uint64_t)meminfo->mem_upper * 1024));
    }

    if (mmap_tag == 0) {
        kprintf("Memory map not provided by bootloader.\n");
        return;
    }

    /* multiboot_tag_mmap结构体大小为16字节（type/size/entry_size/entry_version) */
    entry_count = (mmap_tag->size - 16) / mmap_tag->entry_size;

    kprintf("Memory map entries (count = %u):\n", entry_count);

    /* 计算第一个条目的地址：紧跟在mmap_tag结构体之后 */
    entry = (struct multiboot_mmap_entry *)((uint8_t *)mmap_tag + 16);

    for (i = 0; i < entry_count; i++) {
        uint64_t base = entry->addr;
        uint64_t end = entry->addr + entry->len;

        kprintf("  region %u: base=0x%x%08x length=%u end=0x%x%08x type=%s\n",
                i + 1,
                (uint32_t)(base >> 32), (uint32_t)(base & 0xFFFFFFFF),
                (uint32_t)entry->len,
                (uint32_t)(end >> 32), (uint32_t)(end & 0xFFFFFFFF),
                memory_type_name(entry->type));

        entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + mmap_tag->entry_size);
    }

    kprintf("================================\n");
}
