#include "../include/multiboot2.h"
#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/interrupt.h"
#include "../include/memory.h"
#include "../include/pmm.h"

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

    /* initialize physical memory manager based on the memory map */
    pmm_init(addr);
    kprintf("Physical memory manager initialized.\n");
    pmm_dump_stats();

    /* simple allocation test */
    {
        void *p1, *p2;

        p1 = pmm_alloc_page();
        p2 = pmm_alloc_page();
        kprintf("pmm_alloc_page: p1=%p, p2=%p\n", p1, p2);
        pmm_dump_stats();

        pmm_free_page(p1);
        pmm_dump_stats();
        pmm_free_page(p2);
        pmm_dump_stats();
    }

    kprintf("Kernel entered protected mode!\n");
    kprintf("System ready.\n");

    while (1) { __asm__ volatile ("hlt"); }
}