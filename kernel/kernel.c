#include "../include/multiboot2.h"
#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/interrupt.h"

/* kernel entry function, receives multiboot2 magic and info pointer */
void kernel_main(unsigned int magic, unsigned int addr) {
    /* check if booted by a multiboot2 compliant bootloader */
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        return;
    }
    
    /* initialize VGA text output module */
    vga_init();
    
    kprintf("Hi, I'm VIKI OS...\n");
    kprintf("Magic: 0x%x, Info Addr: 0x%x\n", magic, addr);
    
    /* initialize GDT */
    gdt_init();
    kprintf("GDT initialized successfully!\n");
    
    /* initialize interrupt system */
    interrupt_init();
    
    /* enable interrupts */
    __asm__ volatile ("sti");
    
    kprintf("Kernel entered protected mode!\n");
    
    kprintf("Interrupts enabled!\n");
    kprintf("System ready.\n");
    
    /* 启用中断 */
    __asm__ volatile ("sti");
    
    /* 除零测试 - 触发 #DE 异常 */
    int a = 1;
    int b = 0;
    int c = a / b;
    
    kprintf("After div: %d\n", c);
    while (1) { __asm__ volatile ("hlt"); }
}