#include "../include/multiboot2.h"
#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/interrupt.h"
#include "../include/memory.h"

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
    
    /* initialize interrupt system and IDT */
    interrupt_init();
    kprintf("Interrupt subsystem initialized successfully!\n");

    /* print physical memory layout from multiboot2 info */
    memory_print_map(addr);

    kprintf("Kernel entered protected mode!\n");
    kprintf("System ready.\n");

    while (1) { __asm__ volatile ("hlt"); }
}