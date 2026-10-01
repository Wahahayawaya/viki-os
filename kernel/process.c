#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/vga.h"

/* 内部 helper：不依赖外部 libc，freestanding 环境自带 */
static void mem_zero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = 0;
}

static void str_copy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

/*
 * 就绪队列
 * 设计思路：单链表 + 头尾指针，O(1) 入队、O(1) 出队。
 * 调度器要按时间片轮转，队列顺序即执行顺序。
 */
static process_t *ready_queue_head = 0;
static process_t *ready_queue_tail = 0;

/* 当前运行的进程。初始指向 boot_process，让第一次调度前有"上一个"可记录。 */
process_t *current_process = 0;

/* 占位进程：process_init 之后、第一个真进程创建之前充当 current_process */
static process_t boot_process;

static uint32_t next_pid = 1;

void process_init(void) {
    ready_queue_head = 0;
    ready_queue_tail = 0;
    next_pid = 1;

    /* 清空占位进程，把它当作"boot 自己" */
    mem_zero(&boot_process, sizeof(boot_process));
    boot_process.pid = 0;
    boot_process.state = TASK_RUNNING;
    str_copy(boot_process.name, "boot");
    current_process = &boot_process;
}

/*
 * 创建内核线程
 *
 * 栈帧布局（从高地址往低地址）：
 *   [栈顶]
 *   entry      <- ret 地址，将来 switch_to 的 ret 跳到这里
 *   0          <- ebp
 *   0          <- ebx
 *   0          <- esi
 *   0          <- edi   <- esp 指向这里
 *
 * 本 PR 只负责"造出这个栈"和"塞进队列"，不负责真正切过去。
 * 实际切换（isr.S 保存/恢复 esp）由后续 PR 完成。
 */
process_t *process_create(const char *name, void (*entry)(void)) {
    process_t *proc = (process_t *)pmm_alloc_page();
    if (!proc) return 0;
    mem_zero(proc, sizeof(process_t));

    proc->pid = next_pid++;
    str_copy(proc->name, name);
    proc->state = TASK_READY;
    proc->cr3 = 0;
    proc->kstack_top = 0;  /* 内核线程暂不需要单独的 ring0 栈 */

    /* 分配一页内核栈 */
    uint32_t *stack = (uint32_t *)pmm_alloc_page();
    if (!stack) return 0;
    uint32_t *sp = (uint32_t *)((uint32_t)stack + 4096);

    /* 构造中断返回栈，与 RESTORE_ALL 的 pop 顺序一致：
     *   [pt_regs_ptr]                    ← addl $4 跳过
     *   edi,esi,ebp,esp,ebx,edx,ecx,eax  ← popa
     *   gs,fs,es,ds                      ← popl
     *   int_no,err_code                  ← addl $8 跳过
     *   eip,cs,eflags                    ← iret（ring0，3 字段）
     */
    *(--sp) = 0x202;              /* eflags: IF=1 */
    *(--sp) = 0x08;               /* cs: 内核代码段 */
    *(--sp) = (uint32_t)entry;    /* eip */
    *(--sp) = 0;                  /* err_code */
    *(--sp) = 0;                  /* int_no */
    *(--sp) = 0x10;               /* ds */
    *(--sp) = 0x10;               /* es */
    *(--sp) = 0x10;               /* fs */
    *(--sp) = 0x10;               /* gs */
    *(--sp) = 0;                  /* eax */
    *(--sp) = 0;                  /* ecx */
    *(--sp) = 0;                  /* edx */
    *(--sp) = 0;                  /* ebx */
    *(--sp) = 0;                  /* esp (pusha 槽位) */
    *(--sp) = 0;                  /* ebp */
    *(--sp) = 0;                  /* esi */
    *(--sp) = 0;                  /* edi */
    *(--sp) = 0;                  /* pt_regs_ptr 占位 */

    proc->esp = (uint32_t)sp;

    /* 入队 */
    proc->next = 0;
    if (ready_queue_tail) {
        ready_queue_tail->next = proc;
    } else {
        ready_queue_head = proc;
    }
    ready_queue_tail = proc;

    vga_printf("process_create: pid=%u name=%s esp=0x%x\n",
               proc->pid, proc->name, proc->esp);

    return proc;
}

/*
 * schedule - 轮转调度
 *
 * 设计思路：中断返回路径（isr.S 的 RESTORE_ALL）会自动从
 * current_process->esp 恢复栈，所以这里只负责"选下一个"，不切栈。
 *
 * 调用时机：IRQ0 时钟中断的 EOI 之后，每 10ms 一次。
 */
void schedule(void) {
    if (!ready_queue_head) return;

    /* 把当前进程放回队尾（如果还在运行态） */
    if (current_process && current_process->state == TASK_RUNNING) {
        current_process->state = TASK_READY;
        current_process->next = 0;
        if (ready_queue_tail) {
            ready_queue_tail->next = current_process;
        } else {
            ready_queue_head = current_process;
        }
        ready_queue_tail = current_process;
    }

    /* 取队首作为下一个 */
    process_t *next = ready_queue_head;
    if (!next) return;

    ready_queue_head = next->next;
    if (!ready_queue_head) ready_queue_tail = 0;

    next->state = TASK_RUNNING;
    next->next = 0;
    current_process = next;
}
