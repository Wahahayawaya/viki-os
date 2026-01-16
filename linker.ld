/* 链接脚本 - 定义内核的内存布局 */

/* 指定入口点 */
ENTRY(_start)

/* 内核加载地址（1MB） */
SECTIONS
{
    /* 从1MB开始加载 */
    . = 1M;
    
    /* 文本段（代码段） */
    .text : {
        *(.multiboot)  /* multiboot头信息必须在最前面 */
        *(.text)       /* 代码段 */
    }
    
    /* 只读数据段 */
    .rodata : {
        *(.rodata)
    }
    
    /* 数据段 */
    .data : {
        *(.data)
    }
    
    /* 未初始化数据段 */
    .bss : {
        *(.bss)
        *(COMMON)
    }
}