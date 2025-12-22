# 定义编译器和编译选项
CC = gcc
CFLAGS = -m32 -ffreestanding -fno-pie -c
LD = ld
LDFLAGS = -m elf_i386 
LDFLAGS_APPEND = -Ttext 0x7e00 --oformat binary

# 默认生成目标，直接make就会执行这个
all: os-image.bin

# 1. 拼接最终的镜像文件
os-image.bin: boot.bin kernel.bin zeros.bin
		cat boot.bin kernel.bin zeros.bin > os-image.bin

# 2. 链接内核文件
kernel.bin: kernel.o string.o interrupt.o
		$(LD) $(LDFLAGS) -o kernel.bin $(LDFLAGS_APPEND) kernel.o string.o interrupt.o

# 3. 编译C文件（通用规则，把所有的.c文件编译成.o文件）
%.o: %.c
		$(CC) $(CFLAGS) $< -o $@

# 4. 编译汇编文件
interrupt.o: interrupt.asm
		nasm -f elf32 interrupt.asm -o interrupt.o

boot.bin: boot.asm
		nasm -f bin boot.asm -o boot.bin

zeros.bin:
		dd if=/dev/zero of=zero.bin bs=512 count=20

# 清理命令: make clean
clean:
	rm -f *.o *.bin

