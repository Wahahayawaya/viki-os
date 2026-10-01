#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

/* 进程状态机
 * 设计思路：借鉴 Linux 的 task_state，先用最简四态，够轮转调度用。
 * 后续加阻塞/唤醒时再扩展。
 */
#define TASK_RUNNING 0   /* 正在 CPU 上 */
#define TASK_READY   1   /* 就绪，等调度器选 */
#define TASK_BLOCKED 2   /* 阻塞，等事件（未用） */
#define TASK_ZOMBIE  3   /* 已退出，等回收（未用） */

/* 进程名长度，够放 "idle"、"shell" 之类短名 */
#define PROC_NAME_LEN 32

/*
 * 进程控制块（PCB）
 * 每个进程一份，描述"它是谁、跑到哪、栈在哪"。
 *
 * 字段顺序设计：
 *   - esp 放在偏移 8，是为了让 isr.S 里 `movl %esp, 8(%ecx)` 这类
 *     访问保持稳定偏移，将来中断路径切换任务时能直接引用。
 *   - kstack_top 只在用户态进程里用，ring3 -> ring0 时 CPU 从
 *     TSS.esp0 加载它。内核线程可以留 0。
 */
typedef struct process {
    uint32_t pid;                /* 进程 ID */
    uint32_t state;              /* 状态：TASK_* */
    uint32_t esp;                /* 保存的栈指针（偏移 8，isr.S 依赖） */
    uint32_t cr3;                /* 页目录物理地址（0 表示与内核共享） */
    struct process *next;        /* 就绪队列链表指针 */
    char name[PROC_NAME_LEN];    /* 进程名（调试用） */
    uint32_t kstack_top;         /* ring0 内核栈顶（用户进程用） */
} process_t;

/* 初始化：清空就绪队列，创建占位 boot_process 作为 current_process */
void process_init(void);

/* 创建一个内核线程，entry 是新线程入口，加入就绪队列 */
process_t *process_create(const char *name, void (*entry)(void));
void schedule(void);

#endif
