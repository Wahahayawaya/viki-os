/* idt_flush - 加载IDT到IDTR寄存器
 * 参数: 位于栈上(4(%esp)) - IDT指针的地址
 * C原型: void idt_flush(uint32_t idt_ptr);
 * 
 * 设计思路: lidt指令从内存加载IDT指针到IDTR，这是一个特权指令
 *           只能在保护模式下、内核态执行
 */
.global idt_flush
idt_flush:
    movl 4(%esp), %eax   /* 从栈获取参数: IDT指针地址 */
    lidt (%eax)          /* 加载IDT */
    ret
