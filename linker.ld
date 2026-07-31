/*
 * 链接脚本 - 高半核（High-Half Kernel）内存布局
 *
 * 设计思路（类 Linux 高半核）：
 *   1. 将内核镜像分为两段：
 *      - .boot 段：引导启动代码，VMA = LMA = 1MB，运行在物理地址，
 *        因为此时分页尚未开启，CPU 必须通过物理地址执行代码。
 *      - 内核主体段：.text/.rodata/.data/.bss，VMA = 0xC0100000，
 *        LMA 紧跟 .boot 段之后（物理地址约 1MB+）。
 *   2. 0xC0000000 以上的高 1GB 空间为内核空间，低 3GB 为用户空间，
 *        这与 Linux x86-32 的默认 3G/1G 划分一致。
 *   3. ELF 入口 _start 在 .boot 段（物理 1MB），GRUB 无需分页即可跳转；
 *      _start 设置好页表后跳转到高虚拟地址执行内核主体。
 *   4. AT() 指令指定每个段的加载地址（LMA），使 GRUB 按物理地址
 *      将内核段加载到正确的物理内存位置。
 */

/* 指定入口点：_start 在 .boot 段，物理地址 1MB */
ENTRY(_start)

/* 内核虚拟地址偏移量：所有内核虚拟地址 = 物理地址 + 此偏移 */
KERNEL_VMA = 0xC0000000;

SECTIONS
{
    /* ============ 引导段：VMA = LMA = 1MB ============ */
    /*
     * 引导段包含 multiboot2 头、引导代码、页表和栈，
     * 全部放在一个输出段中，避免 VMA 重叠问题。
     * 页表和栈通过 .ALIGN(4096) 在段内对齐到页边界。
     */
    . = 1M;

    .boot : {
        *(.multiboot)     /* multiboot2 头信息必须在最前面 */
        *(.boot)          /* 引导代码 */
        . = ALIGN(4096);  /* 页表对齐到 4K 边界 */
        *(.boot.bss)      /* 引导数据：页目录、页表、引导栈 */
    }

    /*
     * 记录引导段占用的物理页数，供 PMM 保留内核区域使用。
     * _boot_end_phys 是引导段在物理内存中的结束地址。
     */
    _boot_end_phys = 1M + SIZEOF(.boot);

    /*
     * 内核主体段：高虚拟地址。
     * VMA = KERNEL_VMA + _boot_end_phys，使 VMA 偏移 = 物理加载地址，
     * 这样 identity 映射的页表（PDE[768]）能正确将 VMA 映射到物理地址。
     * 例如：boot 段占 0x7000，内核 VMA = 0xC0107000 -> 物理 0x107000。
     *
     * LMA 使用 AT(ADDR(section) - KERNEL_VMA) 而非累加 SIZEOF，
     * 这样即使 VMA 有对齐填充，LMA 也精确等于 VMA - KERNEL_VMA，
     * 保证页表映射（VMA -> VMA - KERNEL_VMA）与实际加载地址一致。
     */
    . = KERNEL_VMA + _boot_end_phys;

    /* 文本段（代码段），LMA = VMA - KERNEL_VMA */
    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text)
        *(.text.*)
    }

    /* 只读数据段 */
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata)
        *(.rodata.*)
    }

    /* 数据段 */
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data)
        *(.data.*)
    }

    /* 未初始化数据段（BSS），不占文件空间但占内存空间 */
    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /*
     * 导出内核结束符号（VMA）和物理结束地址，
     * 供 PMM 识别内核占用的物理内存范围。
     * 由于 LMA = VMA - KERNEL_VMA，物理结束地址 = VMA 结束 - KERNEL_VMA。
     */
    _kernel_end = .;
    _kernel_phys_end = _kernel_end - KERNEL_VMA;

    /*
     * 丢弃不需要的 ELF 节，避免干扰内核内存布局
     * .eh_frame：GCC 异常处理框架，内核不需要
     * .comment：编译器版本信息
     * .note：编译器注释
     */
    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note*)
    }
}
