#define ASM_FILE 1
#include "../include/multiboot2.h"

/* 定义栈大小为16KB */
#define STACK_SIZE 0x4000

/* 声明外部C语言内核入口函数 */
.extern kernel_main

/* 定义全局符号，供链接器使用 */
.globl _start

.section .text
.code32

_start:
    /* 设置栈指针到栈顶 */
    mov $(stack_top), %esp
    
    /* 调用C语言内核入口函数，传递multiboot2魔数和信息指针 */
    /* 参数：%eax = magic, %ebx = info */
    push %ebx    /* 第二个参数：multiboot信息结构体指针 */
    push %eax    /* 第一个参数：multiboot魔数 */
    call kernel_main
    add $8, %esp /* 清理栈（两个32位参数） */
    
    /* 如果kernel_main返回，则进入无限循环 */
_hang:
    hlt
    jmp _hang

/* 定义multiboot2头信息 - 使用正确的节名称 */
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
    /* 信息请求标签 - 同时请求基本内存信息和内存映射，方便后续内存管理
     * 标签头8字节，后跟两个4字节请求字段，总大小16字节 */
    .short MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST
    .short 0  /* 不是可选的，必须存在 */
    .long  multiboot_request_end - multiboot_request_begin  /* 标签大小：type(2) + flags(2) + size(4) + 两个请求(8) */
    .long MULTIBOOT_TAG_TYPE_BASIC_MEMINFO
    .long MULTIBOOT_TAG_TYPE_MMAP
multiboot_request_end:
    /* 结束标签 */
    .align 8
    .short MULTIBOOT_HEADER_TAG_END
    .short 0
    .long 8   /* 标签大小 */

multiboot_header_end:

/* 定义栈空间 */
.section .bss
.align 16
stack_bottom:
    .skip STACK_SIZE
stack_top: