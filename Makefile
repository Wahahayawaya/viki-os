# VIKI操作系统构建脚本

# 编译器设置
CC = gcc
AS = gcc
LD = ld

# 编译选项
CFLAGS = -m32 -ffreestanding -fno-stack-protector -nostdlib -Wall -Wextra
ASFLAGS = -m32
LDFLAGS = -m elf_i386 -T linker.ld

# 目标文件
BOOT_OBJ = boot/boot.o
KERNEL_OBJ = kernel/kernel.o
KERNEL_BIN = kernel.bin

# 所有目标文件
OBJS = $(BOOT_OBJ) $(KERNEL_OBJ)

# 默认目标：构建内核
all: iso

# 编译引导汇编代码
$(BOOT_OBJ): boot/boot.S include/multiboot2.h
	@echo "编译引导代码: $<"
	$(AS) $(ASFLAGS) -c $< -o $@

# 编译内核C代码
$(KERNEL_OBJ): kernel/kernel.c include/multiboot2.h
	@echo "编译内核代码: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 链接内核
$(KERNEL_BIN): $(OBJS) linker.ld
	@echo "链接内核: $@"
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# 构建ISO镜像
iso: $(KERNEL_BIN)
	@echo "创建ISO目录结构..."
	cp $(KERNEL_BIN) iso/boot/
	@echo "生成GRUB引导ISO镜像..."
	grub-mkrescue -o viki-os.iso iso/

# 在QEMU中从ISO镜像启动
run_qemu: iso
	@echo "从ISO镜像在QEMU中启动..."
	qemu-system-i386 -cdrom viki-os.iso

# 清理构建文件
clean:
	@echo "清理构建文件..."
	rm -f $(OBJS) $(KERNEL_BIN) viki-os.iso iso/boot/$(KERNEL_BIN)

.PHONY: all iso run_qemu clean