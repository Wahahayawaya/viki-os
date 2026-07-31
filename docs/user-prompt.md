### 引导
```markdown
请使用multiboot2协议实现一个可以使用grub2引导的x86 32位最简操作系统，只需要输出简单的"Hi, I'm VIKI..."即可。
要求： 
1.禁止对include/multiboot2.h进行修改(https://www.gnu.org/software/grub/manual/multiboot2/html_node/multiboot2_002eh.html)
2.汇编代码直接使用gcc进行构建
3.引导汇编代码中要包含multiboot2的一些宏定义，包含include/multiboot2.h头文件同时在汇编代码中定义ASM_FILE宏
4.在内核入口函数处，要将multiboot2魔数及信息传参给此入口函数
5.最后写一个Makefile来构建所有代码并制作gurb2引导ISO镜像
6.每行代码尽量使用中文注释，方便阅读
在当前项目根目录下创建如下项目组织结构：
├── include/
├── boot/
├── kernel/
├── iso/
└── Makefile
```

### 实现一个kprintf
```markdown
在当前代码的基础上，实现滚屏、光标自动移动及类似C标准库中printf函数功能（支持%s/%c/%x/%d/%p/%08x等常用项）并将以上功能封装到一个独立模块中，要考虑通用性及拓展性，禁止包含任何c标准库中的头文件。
```

### GDT全局描述符表初始化
```markdown
在当前OS代码的基础上，实现GDT的初始化功能，要求初始化内核代码段、内核数据段、用户代码段、用户数据段，并且在初始化完成后能够顺利进入分段模式。核心要求：符合平坦模型，新增功能要合理模块化，对已有代码改动要尽量小。
```

### 中断管理
```markdown
基于当前os代码，实现以下功能： 
编写中断管理系统，包括以下模块：
1. 编写中断描述符表（IDT）数据结构和初始化
2. 实现中断向量表初始化并将中断处理函数注册到中断向量表
3. 实现IDT加载汇编`idt_flush.S`
4. 初始化8259PIC可编程中断控制器 
5. 实现中断服务函数`isr.S`,  实现以下中断处理：
   -  CPU异常：中断向量号0-31
   -  硬件中断： 中断向量号32-47
   -  系统调用： 中断向量号128
   **核心要求：**
   使用汇编宏函数分别封装三种中断：1. 处理无错误码异常 2. 处理有错误码异常  3. 处理硬件中断（无错误码）。无错误码情况下手动pushl 0。每个宏函数最终都调用一个通用处理例程负责中断上下文保存与恢复并且调用同一个C接口`interrupt_handler`中断处理函数，这个函数接收一个如下结构的指针参数：  
      typedef struct pt_regs
      {
        uint32_t gs, fs, es, ds;                         // Data segment selector
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha.
        uint32_t int_no, err_code;                       // Interrupt number and error code (if applicable)
        uint32_t eip, cs, eflags, useresp, ss;           // Pushed by the processor automatically.
      } __attribute__((packed)) pt_regs;
      **注意：** 在`call interrupt_handler` 调用中断处理函数之前，要将esp压入栈，这样才能在`interrupt_handler`中通过只能拿到pt_regs获取中断上下文信息；注意中断发生时，默认被CPU压栈的寄存器；中断处理中不允许中断嵌套，在宏函数入口出关闭中断，调用完中断处理函数后再开启中断；汇编指令要精简没必要mov指令可以省去；只用实现以上三种中断例程，其他不用实现。

- **设计参考**：Linux架构采用统一中断处理框架，所有中断统一入口后分发
- **依赖**：当前GDT/I/O端口框架已就绪，直接开发即可
- **文件规划**：
  - `include/idt.h` - IDT数据结构
  - `kernel/idt.c` - IDT初始化和注册函数
  - `kernel/idt_flush.S` - 加载IDT汇编
  - `kernel/isr.S` - 中断处理函数
  - `include/interrupt.h` - 中断处理接口
  - `kernel/interrupt.c` - 中断向量表初始化并将中断处理函数注册到中断向量表
```


### 物理内存布局

```markdown
基于当前OS代码获取物理内存布局，并且将内存布局打印出来。
```

### 物理内存管理

```markdown
基于当前获取的物理内存布局，进一步完成物理内存管理，使用bitmap来管理。注意：模块化，原有代码修改尽量小
```

### 开启分页
```markdown
1. 添加一个串口调试功能，让qemu可以通过串口获取所有输出。
2. 基于当前代码实现带有高半核设计的虚拟内存分页模块
```
