# 定义编译器和编译选项
CC = gcc
CFLAGS = -m32 -ffreestanding -fno-pie -c -Iinclude -Wall -Wextra
LD = ld
LDFLAGS = -m elf_i386 
LDFLAGS_APPEND = -Ttext 0x7e00 --oformat binary

# 目录定义
SRC_DIR = ./src
ASM_DIR = ./asm
BIN_DIR = ./bin
OBJ_DIR = ./obj

# 自动查找所有源文件
C_SOURCES := $(wildcard $(SRC_DIR)/*.c)
ASM_SOURCES := $(wildcard $(ASM_DIR)/*.asm)
C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst $(ASM_DIR)/%.asm,$(OBJ_DIR)/%.o,$(filter-out $(ASM_DIR)/boot.asm,$(ASM_SOURCES)))

# 默认生成目标
all: $(BIN_DIR)/os-image.bin

# 1. 拼接最终的镜像文件
$(BIN_DIR)/os-image.bin: $(BIN_DIR)/boot.bin $(BIN_DIR)/kernel.bin $(BIN_DIR)/zeros.bin
	cat $^ > $@

# 2. 链接内核文件
$(BIN_DIR)/kernel.bin: $(C_OBJECTS) $(ASM_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(LDFLAGS_APPEND) $^

# 3. 编译C文件（从src目录编译到obj目录）
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@

# 4. 编译汇编文件（除boot.asm外）
$(OBJ_DIR)/%.o: $(ASM_DIR)/%.asm
	nasm -f elf32 $< -o $@

# 5. 编译引导扇区（特殊处理）
$(BIN_DIR)/boot.bin: $(ASM_DIR)/boot.asm
	nasm -f bin $< -o $@

# 6. 生成零填充文件
$(BIN_DIR)/zeros.bin:
	dd if=/dev/zero of=$@ bs=512 count=20

# 确保目录存在
$(shell mkdir -p $(BIN_DIR) $(OBJ_DIR))

# 清理命令
clean:
	rm -f $(OBJ_DIR)/*.o $(BIN_DIR)/*.bin

# 调试：打印变量值
print-%: ; @echo $* = $($*)