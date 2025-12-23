# 定义编译器和编译选项
CC = gcc
CFLAGS = -m32 -ffreestanding -fno-pie -c -Iinclude
LD = ld
LDFLAGS = -m elf_i386 
LDFLAGS_APPEND = -Ttext 0x7e00 --oformat binary

# 目录定义
SRC_DIR = ./src
ASM_DIR = ./asm
BIN_DIR = ./bin
OBJ_DIR = ./obj

# 默认生成目标
all: $(BIN_DIR)/os-image.bin

# 1. 拼接最终的镜像文件
$(BIN_DIR)/os-image.bin: $(BIN_DIR)/boot.bin $(BIN_DIR)/kernel.bin $(BIN_DIR)/zeros.bin
		cat $(BIN_DIR)/boot.bin $(BIN_DIR)/kernel.bin $(BIN_DIR)/zeros.bin > $(BIN_DIR)/os-image.bin

# 2. 链接内核文件
$(BIN_DIR)/kernel.bin: $(OBJ_DIR)/kernel.o $(OBJ_DIR)/string.o $(OBJ_DIR)/interrupt.o $(OBJ_DIR)/mem.o
		$(LD) $(LDFLAGS) -o $(BIN_DIR)/kernel.bin $(LDFLAGS_APPEND) $(OBJ_DIR)/kernel.o $(OBJ_DIR)/string.o $(OBJ_DIR)/interrupt.o $(OBJ_DIR)/mem.o

# 3. 编译C文件（从src目录编译到obj目录）
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
		$(CC) $(CFLAGS) $< -o $@

# 4. 编译汇编文件
$(OBJ_DIR)/interrupt.o: $(ASM_DIR)/interrupt.asm
		nasm -f elf32 $(ASM_DIR)/interrupt.asm -o $(OBJ_DIR)/interrupt.o

$(BIN_DIR)/boot.bin: $(ASM_DIR)/boot.asm
		nasm -f bin $(ASM_DIR)/boot.asm -o $(BIN_DIR)/boot.bin

$(BIN_DIR)/zeros.bin:
		dd if=/dev/zero of=$(BIN_DIR)/zeros.bin bs=512 count=20

# 确保目录存在
$(shell mkdir -p $(BIN_DIR) $(OBJ_DIR))

# 清理命令
clean:
	rm -f $(OBJ_DIR)/*.o $(BIN_DIR)/*.bin