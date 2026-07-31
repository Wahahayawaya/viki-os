# viki-os 分页机制详解：从裸机启动到虚拟内存

> **适用对象**：计算机入门学生，已了解 C 语言基本语法和计算机组成原理基础概念
>
> **目标**：通过 viki-os 操作系统的真实代码，讲清楚 x86-32 分页机制从 0 到 1 的实现过程
>
> **涉及源文件**：`linker.ld`、`boot/boot.S`、`kernel/boot_paging.c`、`kernel/mmu.c`、`kernel/pmm.c`

---

## 目录

1. [核心概念：物理地址 vs 虚拟地址](#1-核心概念物理地址-vs-虚拟地址)
2. [全局设计：高半核（High-Half Kernel）](#2-全局设计高半核high-half-kernel)
3. [linker.ld：内存布局的蓝图](#3-linkerld内存布局的蓝图)
4. [boot.S：从物理地址到虚拟地址的跳板](#4-boots从物理地址到虚拟地址的跳板)
5. [boot_paging.c：页表的诞生](#5-boot_pagingc页表的诞生)
6. [x86-32 分页硬件机制详解](#6-x86-32-分页硬件机制详解)
7. [递归页表映射：巧妙的自指技巧](#7-递归页表映射巧妙的自指技巧)
8. [mmu.c：运行时的虚拟内存管理](#8-mmuc运行时的虚拟内存管理)
9. [pmm.c：物理内存的位图分配器](#9-pmmc物理内存的位图分配器)
10. [完整启动链路总结](#10-完整启动链路总结)

---

## 1. 核心概念：物理地址 vs 虚拟地址

### 1.1 一个关键问题

打开电脑电源后，CPU 执行的第一条指令通过什么地址找到它？

答案是 **物理地址**——内存条上真实存在的地址。比如地址 `0x00100000` 就是内存条上第 1MB 位置的那个存储单元。

但是现代操作系统（包括 viki-os）给每个程序看到的地址不是物理地址，而是 **虚拟地址**。CPU 内部的 **MMU（Memory Management Unit，内存管理单元）** 会自动把虚拟地址翻译成物理地址。

```
程序看到的地址          CPU 翻译              内存条上的真实位置
（虚拟地址）            （MMU 分页硬件）       （物理地址）

0xC0100000  ──────►  [页表查找]  ──────►  0x00100000
```

### 1.2 为什么要这么做？

| 问题 | 解决方案 |
|------|----------|
| 多个程序都想用地址 0x00000000 | 每个程序有独立的虚拟地址空间，互不干扰 |
| 内核代码需要被保护，不能被用户程序修改 | 内核映射到高地址，用户程序映射到低地址，用权限位隔离 |
| 程序需要的内存比实际物理内存大 | 可以把暂时不用的页存到硬盘，用时再换回来（交换机制） |

### 1.3 分页 vs 分段

x86 提供了两种地址翻译机制：**分段（Segmentation）** 和 **分页（Paging）**。

- **分段**：通过 GDT（全局描述符表）给地址加一个基地址偏移
- **分页**：通过页表把虚拟地址映射到任意物理地址

viki-os 采用 Linux 的策略：**分段 "扁平化"（base=0, limit=4GB），实际隔离靠分页实现**。在 `gdt.c` 中可以看到，所有段描述符的 base 都是 0：

```c
// kernel/gdt.c — 段描述符 base=0，相当于"不分段"
gdt_set_gate(GDT_KERNEL_CODE, 0, 0xFFFFFFFF, 0x9A, 0xCF);
//                              ^base=0     ^limit=4GB
```

这样虚拟地址就等于段内偏移，所有真正的地址隔离都由分页完成。

---

## 2. 全局设计：高半核（High-Half Kernel）

### 2.1 3G/1G 划分

viki-os 采用类 Linux 的 **高半核设计**：把 4GB 虚拟地址空间分成两部分：

```
虚拟地址空间 (4GB)
┌───────────────────────────────┐ 0xFFFFFFFF
│                               │
│    内核空间 (1GB)              │
│    0xC0000000 ~ 0xFFFFFFFF    │
├───────────────────────────────┤ 0xC0000000  ← KERNEL_VMA
│                               │
│                               │
│    用户空间 (3GB)              │
│    0x00000000 ~ 0xBFFFFFFF    │
│                               │
│                               │
└───────────────────────────────┘ 0x00000000
```

**为什么是 3G/1G？** 这是 Linux x86-32 的默认划分，内核占高 1GB，用户进程占低 3GB。内核代码、数据都链接到 `0xC0000000` 以上的地址，用户进程永远不会看到它们。

### 2.2 一个"鸡生蛋"问题

内核代码链接在 `0xC0100000`（虚拟地址），但开机时 **分页还没开启**，CPU 只能用物理地址。那内核代码怎么执行？

这就是 **boot.S + linker.ld + boot_paging.c** 三个文件协同解决的核心问题。我们逐步来看。

---

## 3. linker.ld：内存布局的蓝图

> 源文件：`linker.ld`

链接脚本（linker script）告诉链接器：每个代码段最终放在什么地址。它定义了两类地址：

- **VMA（Virtual Memory Address）**：链接后的逻辑地址，即程序"以为"自己在哪
- **LMA（Load Memory Address）**：GRUB 实际把数据放在物理内存的什么位置

### 3.1 两段式布局

```
                    VMA（虚拟地址）              LMA（物理加载地址）
                    ┌────────────┐              ┌────────────┐
                    │            │              │            │
  .boot 段          │ 0x00100000 │  ← identity →│ 0x00100000 │
  (引导段)          │     ~      │   VMA = LMA  │     ~      │
                    │ boot_end  │              │ boot_end   │
                    ├────────────┤              ├────────────┤
                    │    ...     │              │            │
                    │ (3GB 空隙) │              │  (连续!)   │
                    │    ...     │              │  .text     │
  内核主体           ├────────────┤              │  .rodata   │
  (.text 等)        │0xC0100000+ │  ← VMA-LMA  │  .data     │
                    │  .text     │    偏移=     │  .bss      │
                    │  .rodata   │   0xC0000000│      ~     │
                    │  .data     │              │ kernel_end │
                    │  .bss      │              └────────────┘
                    └────────────┘
```

### 3.2 逐段解析

**引导段 `.boot`：**

```ld
. = 1M;                          /* 位置计数器设为 1MB */

.boot : {
    *(.multibout)                /* multiboot2 头，GRUB 识别用 */
    *(.boot)                     /* 引导代码 */
    . = ALIGN(4096);             /* 页表 4K 对齐 */
    *(.boot.bss)                 /* 页目录、页表、引导栈 */
}
```

- VMA = LMA = `0x00100000`（1MB），**没有偏移**
- 这是因为开机时分页未开启，CPU 必须用物理地址执行
- GRUB 把内核加载到 1MB，直接跳转到这里就能执行

**内核主体段 `.text/.rodata/.data/.bss`：**

```ld
. = KERNEL_VMA + _boot_end_phys;     /* VMA = 0xC0000000 + 引导段大小 */

.text : AT(ADDR(.text) - KERNEL_VMA) { ... }
```

- `AT()` 指定 LMA = VMA - 0xC0000000
- 举例：如果 `.boot` 占了 0x7000 字节，那么：
  - `.text` 的 VMA = `0xC0107000`
  - `.text` 的 LMA = `0x00107000`（物理上紧跟 `.boot` 后面）

**关键结论：物理地址是连续的！** `.boot → .text → .rodata → .data → .bss` 在物理内存中首尾相接，从 `0x100000` 开始一直排列到 `_kernel_phys_end`。

### 3.3 三个导出符号

```ld
_boot_end_phys    = 1M + SIZEOF(.boot);         /* 引导段物理结束 */
_kernel_end       = .;                           /* 内核虚拟地址结束 */
_kernel_phys_end  = _kernel_end - KERNEL_VMA;    /* 内核物理地址结束 */
```

这三个符号供 **PMM（物理内存管理器）** 知道内核占用了哪些物理页，避免把内核区域分配出去。

---

## 4. boot.S：从物理地址到虚拟地址的跳板

> 源文件：`boot/boot.S`

### 4.1 启动时的 CPU 状态

GRUB 把内核加载到物理 1MB 后跳转到 `_start`，此时：

| 项目 | 状态 |
|------|------|
| 分页 | **未开启**（CR0.PG = 0） |
| 地址 | CPU 使用物理地址取指 |
| 栈 | 未定义，需要自己设置 |
| EAX | GRUB 传入魔数 `0x36d76289` |
| EBX | GRUB 传入 multiboot2 信息结构体地址 |

### 4.2 _start 的执行流程

```asm
_start:
    movl $boot_stack_top, %esp      /* 1. 设置引导栈 */
    push %ebx                       /* 2. 保存 multiboot 参数 */
    push %eax
    call setup_paging_c              /* 3. 建立页表（C 函数） */
    movl %eax, %cr3                 /* 4. 加载页目录地址到 CR3 */
    movl %cr0, %eax
    orl  $0x80000000, %eax          /* 5. 设置 PG 位（开启分页） */
    movl %eax, %cr0
    lea  _start_high, %ecx          /* 6. 计算高半核虚拟地址 */
    jmp  *%ecx                      /*    跳转到 0xC0100000+ */
```

逐步解释：

**第 1 步：设置栈**

`boot_stack_top` 在 `.boot.bss` 段中静态声明（16KB 栈空间），由于此段 VMA = LMA，地址就是物理地址，直接可用。

**第 3 步：建立页表**

调用 `setup_paging_c()`（`boot_paging.c` 中实现），该函数填充页目录和页表，返回页目录的物理地址。

> **为什么用 C 而不用汇编写页表？** C 可读性好、可维护性强。但必须用 `__attribute__((section(".boot")))` 把它放到 `.boot` 段，否则分页未开启时跳转到 `0xC010xxxx` 会崩溃。

**第 4-5 步：开启分页**

```
CR3 ← 页目录物理地址    （告诉 MMU 页目录在哪）
CR0.PG ← 1              （开启分页硬件）
```

**第 6 步：跳转到高半核**

`_start_high` 的虚拟地址是 `0xC010xxxx`。`lea` 指令取出这个虚拟地址后，`jmp` 让 CPU 跳过去。此时分页已开启，MMU 会通过页表把 `0xC010xxxx` 翻译成物理 `0x010xxxx`，内核代码就在那里。

### 4.3 identity 映射：为什么不能省？

在开启分页的那一瞬间，EIP 指向的是 `_start` 附近某条指令的物理地址（约 `0x100xxx`）。分页开启后，CPU 会用虚拟地址去取下一条指令。

如果只有高半核映射（PDE[768]），那虚拟地址 `0x100xxx` 没有任何映射，CPU 取指失败 → 崩溃。

所以必须同时建立 **identity 映射**（虚拟地址 = 物理地址），让低地址的代码在分页开启后仍然可见。等跳转到高半核后，可以移除 identity 映射。

### 4.4 _start_high：进入 C 世界

```asm
_start_high:
    pop %eax           /* 恢复 multiboot 魔数 */
    pop %ebx           /* 恢复 multiboot 信息指针 */
    push %ebx          /* 按 cdecl 调用约定压栈 */
    push %eax
    call kernel_main   /* 进入 C 语言内核！ */
```

此时 EIP 已经在 `0xC010xxxx`，栈还在物理 `boot_stack_top`（identity 映射保证可访问）。恢复之前压栈的 GRUB 参数后，调用 `kernel_main()`。

---

## 5. boot_paging.c：页表的诞生

> 源文件：`kernel/boot_paging.c`

### 5.1 页表布局总览

`setup_paging_c()` 建立三个关键的页目录项（PDE）：

```
页目录 (boot_pde[1024])
┌──────────────────────────────────────────┐
│ PDE[0]     → boot_pt0 | PRESENT|RW      │  identity 映射: 0x00000000 ~ 0x003FFFFF → 0x00000000 ~ 0x003FFFFF
│ PDE[1-767] = 0 (不存在)                  │
│ PDE[768]   → boot_pt0 | PRESENT|RW      │  高半核映射: 0xC0000000 ~ 0xC03FFFFF → 0x00000000 ~ 0x003FFFFF
│ PDE[769-1022] = 0 (不存在)               │
│ PDE[1023]  → boot_pde | PRESENT|RW      │  递归映射: 页目录自指
└──────────────────────────────────────────┘
```

### 5.2 代码逐行解析

```c
__attribute__((section(".boot"), noinline))
uint32_t setup_paging_c(void)
{
    int i;

    /* 步骤 1：清零页目录 */
    for (i = 0; i < 1024; i++)
        boot_pde[i] = 0;

    /* 步骤 2：填充页表，identity 映射前 4MB */
    for (i = 0; i < 1024; i++)
        boot_pt0[i] = (i * 4096) | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /* 步骤 3：PDE[0] — identity 映射 */
    boot_pde[0] = (uint32_t)boot_pt0 | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /* 步骤 4：PDE[768] — 高半核映射 */
    boot_pde[768] = (uint32_t)boot_pt0 | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    /* 步骤 5：PDE[1023] — 递归映射 */
    boot_pde[1023] = (uint32_t)boot_pde | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;

    return (uint32_t)boot_pde;  /* 返回页目录物理地址 */
}
```

**步骤 2 详解：** 页表 `boot_pt0` 有 1024 个条目，每个条目映射一个 4KB 页。`PTE[i] = i * 4096` 意味着虚拟页 i → 物理页 i，这就是 identity 映射。1024 页 × 4KB = 4MB，所以 `boot_pt0` 覆盖了前 4MB 物理内存。

**步骤 3 和 4 详解：** PDE[0] 和 PDE[768] 都指向同一张页表 `boot_pt0`！这意味着：
- 访问虚拟地址 `0x00000000` → MMU 查 PDE[0] → boot_pt0 → 物理地址 `0x00000000`
- 访问虚拟地址 `0xC0000000` → MMU 查 PDE[768] → 同一张 boot_pt0 → 物理地址 `0x00000000`

同一个物理页通过两个不同的虚拟地址都能访问。这正是高半核设计的核心：内核代码物理上在 1MB，但虚拟上在 `0xC0100000`。

**步骤 5 详解（递归映射）：** 见[第 7 节](#7-递归页表映射巧妙的自指技巧)。

### 5.3 为什么必须放在 .boot 段？

```c
__attribute__((section(".boot"), noinline))
```

这是整个设计中最精妙的一处。C 编译器会把函数中的符号引用（如 `boot_pde`）解析为 VMA。如果此函数在 `.text` 段（VMA = `0xC0107000`），那么：

1. `boot.S` 的 `call setup_paging_c` 会跳到 `0xC0107000`
2. 但分页未开启，`0xC0107000` 是物理地址，那里什么都没有
3. → 崩溃

放入 `.boot` 段后，VMA = LMA = 物理地址，`call` 跳到物理地址，正确执行。

### 5.4 页表和栈的内存布局

这些数据在 `boot.S` 的 `.boot.bss` 段中静态声明：

```asm
.section .boot.bss
.align 4096

boot_pde:          /* 页目录：4KB = 1024 个 32 位条目 */
    .skip 4096

boot_pt0:          /* 页表：4KB = 1024 个 32 位条目 */
    .skip 4096

boot_stack_bottom: /* 引导栈：16KB */
    .skip 0x4000
boot_stack_top:
```

页目录和页表都是 4KB 对齐的（`.align 4096`），因为页表项中的物理地址只有高 20 位，低 12 位是标志位，必须保证基址是 4K 的整数倍。

---

## 6. x86-32 分页硬件机制详解

### 6.1 二级页表结构

x86-32 使用 **二级页表** 把 32 位虚拟地址翻译成物理地址：

```
32 位虚拟地址
┌──────────┬──────────┬──────────┐
│  PDE 索引 │  PTE 索引 │  页内偏移 │
│ [31:22]  │ [21:12]  │ [11:0]   │
│  10 位   │   10 位  │   12 位  │
└────┬─────┴────┬─────┴────┬─────┘
     │          │          │
     ▼          ▼          ▼

  页目录        页表        物理内存
┌────────┐  ┌────────┐  ┌────────────┐
│PDE[0]  │→ │PTE[0]  │→ │物理页 0    │
│PDE[1]  │  │PTE[1]  │→ │物理页 1    │
│ ...    │  │ ...    │  │ ...        │
│PDE[1023│  │PTE[1023│  │物理页 1023 │
└────────┘  └────────┘  └────────────┘
```

- **页目录（Page Directory）**：1024 个条目，每个条目指向一张页表
- **页表（Page Table）**：1024 个条目，每个条目指向一个 4KB 物理页
- 每级 10 位索引 = 2^10 = 1024 个条目，两级共 1024 × 1024 × 4KB = 4GB

### 6.2 地址翻译过程（MMU 硬件自动完成）

以虚拟地址 `0xC0100000` 为例：

```
步骤 1：取高 10 位 → PDE 索引
  0xC0100000 >> 22 = 768

步骤 2：查页目录
  CR3 指向页目录物理地址
  读取 PDE[768] → 得到页表 boot_pt0 的物理地址

步骤 3：取中 10 位 → PTE 索引
  (0xC0100000 >> 12) & 0x3FF = 256

步骤 4：查页表
  读取 boot_pt0[256] → 得到物理页地址 = 256 * 4096 = 0x100000

步骤 5：取低 12 位 → 页内偏移
  0xC0100000 & 0xFFF = 0

步骤 6：最终物理地址
  物理页基址 0x100000 + 偏移 0x000 = 0x00100000  ✓
```

**结论：** 虚拟地址 `0xC0100000` 被翻译成物理地址 `0x00100000`，这正是内核代码所在位置。

### 6.3 关键控制寄存器

| 寄存器 | 作用 | 代码位置 |
|--------|------|----------|
| **CR0** | bit 0 = PE（保护模式），bit 31 = PG（分页使能） | `boot.S:75-77` 开启 PG 位 |
| **CR2** | 最近一次页错误发生的虚拟地址 | `mmu.c:49` `read_cr2()` |
| **CR3** | 页目录的物理基地址 | `boot.S:68` 写入 CR3 |

```asm
// boot.S — 开启分页
movl %eax, %cr3              ; CR3 = 页目录物理地址
movl %cr0, %eax
orl  $0x80000000, %eax       ; 置 PG 位 (bit 31)
movl %eax, %cr0              ; 分页开启！
```

### 6.4 页表项格式（32 位）

```
31                              12 11    9 8 7 6 5 4 3 2 1 0
┌──────────────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
│        物理页帧基址 (20位)         │ AVL │G│P│D│A│C│P│U│R│P│
│        (4K 对齐，左移 12 位后使用)  │     │ │ │ │ │D│W│/│/│ │
└──────────────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
                                          │     │   │ │
                                          │     │   │ └─ 0=只读, 1=读写
                                          │     │   └─── 0=内核态, 1=用户态
                                          │     └─────── 0=禁用缓存
                                          └─────────── 0=写回, 1=写透
                                     P=存在位 (1=在内存中)
```

viki-os 在 `mmu.h` 中定义了这些标志位：

```c
#define PAGE_PRESENT   0x001   // 存在位
#define PAGE_RW        0x002   // 读写位
#define PAGE_USER      0x004   // 用户态可访问
#define PAGE_ACCESSED  0x020   // 已访问（CPU 自动设置）
#define PAGE_DIRTY     0x040   // 已修改（CPU 自动设置）
```

---

## 7. 递归页表映射：巧妙的自指技巧

> 这是 viki-os 分页设计中最巧妙的部分，源自 Linux 的递归页表技术。

### 7.1 问题：如何修改页表？

页表本身存在物理内存中，但内核运行在虚拟地址空间。要修改页表（比如建立新映射），需要先能访问到页表所在的物理内存。

常规思路：为页表建立临时映射。但这会导致"先有鸡还是先有蛋"的问题——映射页表本身就需要页表。

### 7.2 解法：PDE[1023] 指向自己

在 `boot_paging.c` 中：

```c
/* PDE[1023] 指向页目录自身 */
boot_pde[1023] = (uint32_t)boot_pde | BOOT_PAGE_PRESENT | BOOT_PAGE_RW;
```

这创建了一个 **自指** 结构：页目录的第 1023 项指向页目录自己。

### 7.3 为什么这能工作？

当 CPU 翻译虚拟地址时，它先查 PDE[高 10 位]，再用 PDE 结果查 PTE[中 10 位]。

如果虚拟地址的高 10 位 = 1023，CPU 查到 PDE[1023] = 页目录自身的物理地址。然后 CPU 把页目录 **当作页表** 来用，用中 10 位作为索引去查它。

```
虚拟地址 0xFFFFF000 的翻译过程：

高 10 位 = 1023  →  查 PDE[1023] → 得到页目录自身物理地址
中 10 位 = 1023  →  把页目录当页表，查"页表"[1023] = PDE[1023] = 页目录自身
低 12 位 = 0x000 →  页内偏移 0

结果：访问 0xFFFFF000 = 访问页目录自身的第 0 个条目 (PDE[0])
```

更一般地：

| 想访问的内容 | 使用的虚拟地址 | 原理 |
|-------------|--------------|------|
| 页目录 (PDE 数组) | `0xFFFFF000` | PDE[1023]→PDE[1023]，偏移 0~4095 遍历 PDE[0]~PDE[1023] |
| 页表 i (PTE 数组) | `0xFFC00000 + i*4096` | PDE[1023]→PDE[i]，然后偏移遍历 PTE[0]~PTE[1023] |

在 `mmu.h` 中定义：

```c
#define PD_VADDR      0xFFFFF000           /* 访问页目录 */
#define PT_VADDR(i)   (0xFFC00000 + (i)*4096)  /* 访问页表 i */
```

### 7.4 一个具体例子

假设要修改 PDE[768]（高半核映射的页目录项），直接写虚拟地址 `0xFFFFF000 + 768*4 = 0xFFFFFC00` 处的 32 位整数即可。

```c
uint32_t *pd = (uint32_t *)0xFFFFF000;  // 页目录的虚拟地址
pd[768] = new_page_table_phys | PAGE_PRESENT | PAGE_RW;  // 修改 PDE[768]
```

写完后，想访问该页表的内容：

```c
uint32_t *pt = (uint32_t *)0xFFC00000 + 768*1024;  // = 0xFFC00000 + 768*4096
pt[0] = some_phys_addr | PAGE_PRESENT | PAGE_RW;  // 修改 PTE[0]
```

整个过程不需要任何临时映射，完全靠硬件的地址翻译机制完成。

---

## 8. mmu.c：运行时的虚拟内存管理

> 源文件：`kernel/mmu.c`

### 8.1 vmm_map_page：建立新的虚拟→物理映射

这是运行时最核心的函数。当内核需要把一个虚拟地址映射到新的物理页时调用。

```c
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    /* 1. 计算索引 */
    uint32_t pd_index = virt >> 22;           /* PDE 索引 */
    uint32_t pt_index = (virt >> 12) & 0x3FF; /* PTE 索引 */

    /* 2. 通过递归映射访问页目录 */
    uint32_t *pd = (uint32_t *)PD_VADDR;      /* = 0xFFFFF000 */

    /* 3. 如果页目录项不存在，分配新页表 */
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        void *new_pt = pmm_alloc_page();      /* 从 PMM 分配物理页 */
        pd[pd_index] = (uint32_t)new_pt       /* PDE 指向新页表 */
                       | PAGE_PRESENT | PAGE_RW;

        /* 4. 通过递归映射清零新页表 */
        uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);
        for (int i = 0; i < 1024; i++)
            pt[i] = 0;
    }

    /* 5. 设置 PTE，建立映射 */
    uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);
    pt[pt_index] = (phys & PAGE_ADDR_MASK) | flags;
}
```

**递归映射的妙处** 在步骤 3→4：刚把 `PDE[pd_index]` 设为新页表物理地址后，立刻就能通过 `PT_VADDR(pd_index)` 访问该页表。因为 CPU 走的是 `PDE[1023] → PDE[pd_index] → PTE` 的路径，完全利用硬件机制，不需要额外操作。

### 8.2 vmm_get_phys：查询虚拟地址的物理映射

```c
uint32_t vmm_get_phys(uint32_t virt)
{
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    uint32_t *pd = (uint32_t *)PD_VADDR;

    if (!(pd[pd_index] & PAGE_PRESENT)) return 0;  /* PDE 不存在 */

    uint32_t *pt = (uint32_t *)PT_VADDR(pd_index);
    if (!(pt[pt_index] & PAGE_PRESENT)) return 0;  /* PTE 不存在 */

    return (pt[pt_index] & PAGE_ADDR_MASK) | (virt & 0xFFF);
    /*      ↑物理页帧基址            ↑页内偏移 */
}
```

### 8.3 页错误处理

当 CPU 访问未映射或权限不足的虚拟地址时，触发 **中断 14（Page Fault）**：

```c
void page_fault_handler(struct pt_regs *regs)
{
    uint32_t fault_addr = read_cr2();  /* CR2 保存出错地址 */

    kprintf("Faulting address (CR2): 0x%x\n", fault_addr);
    kprintf("Error code: 0x%x\n", regs->err_code);
    /* bit 0: 0=页不存在, 1=权限冲突 */
    /* bit 1: 0=读, 1=写 */
    /* bit 2: 0=内核态, 1=用户态 */
}
```

### 8.4 vmm_init：初始化验证

```c
void vmm_init(void)
{
    /* 读取 CR0 确认分页已开启 */
    cr0 = read_cr0();
    kprintf("CR0 = 0x%x  [PG=%d, PE=%d]\n", cr0, (cr0>>31)&1, cr0&1);

    /* 验证内核运行在高半核 */
    kprintf("vmm_init() runs at virtual addr: 0x%p\n", vmm_init);
    // 输出 0xC01xxxxx，证明高半核映射生效

    /* 通过递归映射查询 _start_high 的物理地址 */
    uint32_t paddr = vmm_get_phys((uint32_t)_start_high);
    // 输出 0x001xxxxx = 虚拟地址 - 0xC0000000，验证映射正确

    /* 测试：分配并映射一个新页面 */
    void *page = pmm_alloc_page();
    vmm_map_page(0xD0000000, (uint32_t)page, PAGE_PRESENT | PAGE_RW);
    *(uint32_t*)0xD0000000 = 0xDEADBEEF;  /* 写入测试 */
}
```

---

## 9. pmm.c：物理内存的位图分配器

> 源文件：`kernel/pmm.c`

### 9.1 设计思路

PMM 是内存管理的最底层，管理"哪些物理页可以用"。它使用 **位图（Bitmap）** 方式：

- 4GB 物理空间 / 4KB 每页 = 1,048,576 个页帧
- 每个页帧用 1 个 bit 表示：1=已占用，0=空闲
- 位图大小 = 1,048,576 / 8 = 128KB

```c
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];  /* 128KB 静态数组 */
```

**为什么用位图而不是链表？** 空间确定、无指针开销、分配/释放是 O(1) 位操作，适合 x86-32 小内存教学场景。

### 9.2 初始化流程

```c
void pmm_init(unsigned int multiboot_info_addr)
{
    /* 1. 全部位图置 1（保守策略：先假设全部不可用） */
    for (i = 0; i < PMM_BITMAP_SIZE; i++)
        pmm_bitmap[i] = 0xFF;

    /* 2. 计算内核物理结束页帧号 */
    kernel_end = (uint64_t)(uint32_t)&_kernel_phys_end[0];
    kernel_end_frame = kernel_end >> 12;  /* 4K 对齐 */

    /* 3. 解析 GRUB 传入的 multiboot2 内存映射 */
    mmap_tag = pmm_find_mmap(multiboot_info_addr);

    /* 4. 遍历每个 AVAILABLE 区域，标记为可用 */
    for (i = 0; i < entry_count; i++) {
        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) continue;

        /* 跳过内核已占用的区域 */
        if (start < kernel_end) start = kernel_end;

        /* 按页对齐后逐页清零位图 */
        while (start < end) {
            pmm_bitmap_clear(frame);
            pmm_total++;
            pmm_free++;
        }
    }
}
```

**关键：** 用 `_kernel_phys_end`（linker.ld 导出的物理地址）而非 `_kernel_end`（虚拟地址）来判断内核占用范围。如果用虚拟地址 `0xC01xxxxx` 去查位图，会超出 4GB 范围导致越界。

### 9.3 分配与释放

```c
void *pmm_alloc_page(void)
{
    /* 从 pmm_next_free 开始扫描位图，找到第一个 bit=0 的页帧 */
    for (frame = pmm_next_free; frame < PMM_MAX_FRAMES; frame++) {
        if (!pmm_bitmap_test(frame)) {
            pmm_bitmap_set(frame);           /* 标记为已用 */
            pmm_free--;
            pmm_next_free = frame + 1;      /* 记录搜索位置 */
            return (void*)(frame << 12);    /* 转为物理地址 */
        }
    }
    return 0;  /* 无可用页 */
}
```

`pmm_next_free` 是一个优化：记录上次分配的位置，下次从这里继续扫描，避免每次从 0 开始。平均复杂度从 O(N) 降为接近 O(1)。

---

## 10. 完整启动链路总结

### 10.1 时间线

```
┌─────────────────────────────────────────────────────────────────────┐
│ 时间        │ 地址空间     │ 执行的代码          │ 做了什么          │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T0: GRUB    │ 物理地址      │ GRUB 引导程序       │ 加载 kernel.bin  │
│             │              │                    │ 到物理 1MB       │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T1: _start  │ 物理地址      │ boot.S: _start     │ 设栈, 调用        │
│             │ (无分页)      │                    │ setup_paging_c() │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T2: 建页表  │ 物理地址      │ boot_paging.c      │ 填充 PDE[0]      │
│             │ (无分页)      │ setup_paging_c()   │ PDE[768]         │
│             │              │                    │ PDE[1023]        │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T3: 开分页  │ 物理地址      │ boot.S             │ CR3 = 页目录地址  │
│             │ (identity    │                    │ CR0.PG = 1       │
│             │  映射生效)    │                    │                  │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T4: 跳转    │ 虚拟地址      │ boot.S: jmp        │ EIP 从 0x100xxx  │
│             │ 0xC010xxxx   │ _start_high        │ 跳到 0xC010xxx   │
├─────────────┼──────────────┼────────────────────┼─────────────────┤
│ T5: C 入口  │ 虚拟地址      │ kernel.c           │ vga_init()       │
│             │ 0xC010xxxx   │ kernel_main()      │ gdt_init()       │
│             │              │                    │ interrupt_init() │
│             │              │                    │ pmm_init()       │
│             │              │                    │ vmm_init()       │
└─────────────┴──────────────┴────────────────────┴─────────────────┘
```

### 10.2 文件职责矩阵

| 文件 | 职责 | 运行时段 | 地址空间 |
|------|------|---------|---------|
| `linker.ld` | 定义内存布局（VMA/LMA） | 编译链接时 | — |
| `boot/boot.S` | 引导入口、设置栈、开启分页、跳转 | T1-T4 | 物理→虚拟 |
| `kernel/boot_paging.c` | 填充页目录和页表 | T2 | 物理地址 |
| `boot/gdt_flush.S` | 加载 GDT、刷新段寄存器 | T5（kernel_main 内） | 虚拟地址 |
| `kernel/gdt.c` | 构建 GDT 描述符表 | T5 | 虚拟地址 |
| `kernel/mmu.c` | 运行时页表管理（map/unmap/查询） | T5+ | 虚拟地址 |
| `kernel/pmm.c` | 物理页帧位图分配/释放 | T5+ | 虚拟地址 |
| `kernel/kernel.c` | C 语言入口，初始化各子系统 | T5+ | 虚拟地址 |

### 10.3 三个页目录项的作用总结

```
┌──────────────────────────────────────────────────────────────────────┐
│  PDE 索引  │  作用              │  何时使用            │  能否移除   │
├──────────────────────────────────────────────────────────────────────┤
│  PDE[0]   │  identity 映射      │  开启分页瞬间,        │  跳转到高半 │
│           │  虚拟 0~4MB         │  EIP 还在物理地址     │  核后可以   │
│           │  → 物理 0~4MB       │  时保证可取指         │             │
├───────────┼─────────────────────┼──────────────────────┼─────────────┤
│  PDE[768] │  高半核映射          │  内核始终使用          │  否（内核   │
│           │  虚拟 0xC0000000+    │  0xC0100000+          │  依赖它运行）│
│           │  → 物理 0~4MB       │  所有代码和数据       │             │
├───────────┼─────────────────────┼──────────────────────┼─────────────┤
│  PDE[1023]│  递归自映射          │  运行时修改/查询      │  否（VMM    │
│           │  虚拟 0xFFC00000+    │  页表时（vmm_map_     │  依赖它）    │
│           │  → 页目录/页表自身   │  page 等）            │             │
└──────────────────────────────────────────────────────────────────────┘
```

### 10.4 核心设计思想总结

1. **两段式链接**：`.boot` 段（VMA=LMA=物理地址）解决"开机无分页"问题；内核主体段（VMA=0xC0000000+）实现高半核隔离。

2. **identity 映射**：开启分页的那一瞬间，EIP 还在物理地址，必须保证该地址在虚拟空间也可见。跳转到高半核后可移除。

3. **高半核映射**：PDE[768] 把 `0xC0000000+` 映射到物理 `0x00000000+`，内核代码物理上在 1MB 但虚拟上在 3GB+1MB，与用户空间隔离。

4. **递归自映射**：PDE[1023] 指向页目录自身，利用 MMU 硬件的二级查表机制，无需临时映射即可通过固定虚拟地址访问任意页目录/页表条目。

5. **物理地址连续性**：虽然 VMA 有 3GB 空洞，但 LMA 连续（`.boot → .text → .rodata → .data → .bss`），PMM 用 `_kernel_phys_end` 一次性保留整个内核区域。

---

## 附录：编译与运行

```bash
# 编译内核
make clean && make

# 在 QEMU 中运行
make run_qemu

# 串口模式（方便查看输出）
make run_qemu_serial
```

预期输出关键信息：
```
CR0 = 0x...  [PG=1, PE=1]                    # 分页已开启
vmm_init() runs at virtual addr: 0xc01xxxxx   # 高半核映射生效
_start_high: vaddr=0xc01xxxxx -> paddr=0x1xxxxx # 虚拟→物理翻译正确
Mapping test PASSED!                          # 运行时映射功能正常
```
