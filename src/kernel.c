// kernel.c
#include"mem.h"
//显存配置
#define VIDEO_MEMORY 0xb8000
#define MAX_COLS 80
#define MAX_ROWS 25

// 端口配置（VGA控制端口）
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

// 键盘端口配置
#define KEYBOARD_PORT 0x60

typedef struct{
    u16 low_offset; // 中断处理函数的低16位地址
    u16 sel;        // 内核段选择子
    u8 always0;

    // 标志位 byte: 
    // Bit 7: Present (1)
    // Bit 5-6: DPL (Privilege) (00)
    // Bit 0-4: Type (01110 = 32-bit Interrupt Gate)
    // 所以通常是 0x8E (1 00 0 1110)
    u8 flags;
    u16 high_offset; // 中断处理函数的高16位地址
} __attribute__((packed)) idt_gate_t;

typedef struct{
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_register_t;

#define IDT_ETRIES 256  // IDT表的大小，每个项目占据8字节 
idt_gate_t idt[IDT_ETRIES];
idt_register_t idt_reg;

// 键盘扫描码映射表 (Scan Code Set 1)
// 索引是扫描码，值是对应的 ASCII 字符
// 0 表示不可打印字符 (如 Ctrl, Alt)
char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', /* Backspace */
    '\t', /* Tab */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* Enter key */
    0, /* 29   - Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, /* Left shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, /* Right shift */
    '*',
    0,  /* Alt */
    ' ', /* Space bar */
    // ... 后面的键先省略，基本够用了 ...
};

void print_hex32(u32 n);
void print_hex(u8 n);
void kprint_at(char *message, int row, int col);
void port_byte_out(unsigned short port, unsigned char data);
unsigned char port_byte_in(unsigned short port);

int get_cursor_offset();
void set_cursor_offset(int offset);

void print_char(char character, int col, int row, char attribute_byte);
void kprint(char *message);
void clean_screen();

void set_idt_gate(int n, u32 handler);
void set_idt();

extern void isr_stub();
void init_keyboard();
void keyboard_handler_c();

void user_input(char *input);

// 引用string.c中的函数
extern int strlen(char s[]);
extern int strcmp(char s1[], char s2[]);
extern void append(char s[], char n);
extern void backspace(char s[]);
void string_copy(char *source, char *dest, int no_bytes);

// 处理字符超出命令行界限的函数
int handle_scrolling(int offset);

// 全局缓冲区，用来存储用户输入的命令
char key_buffer[256];


void main(){
    clean_screen();

    for(int i = 0; i < IDT_ETRIES; i++){
        set_idt_gate(i, (u32) isr_stub);
    }

    kprint("> ");
    key_buffer[0] = '\0';
    init_keyboard();
    
    while(1);
}

void print_hex32(u32 n) {
    print_hex((n >> 24) & 0xFF);
    print_hex((n >> 16) & 0xFF);
    print_hex((n >> 8) & 0xFF);
    print_hex(n & 0xFF);
}

void print_hex(u8 n) {
    char *hex = "0123456789ABCDEF";
    char out[4]; // 2个数字 + 空格 + 结束符
    out[0] = hex[(n >> 4) & 0xF]; // 取高4位
    out[1] = hex[n & 0xF];        // 取低4位
    out[2] = ' ';                 // 加个空格方便看
    out[3] = 0;
    kprint(out);
}

void kprint_at(char *message, int row, int col){
    char *video_memory = (char *) VIDEO_MEMORY;
    
    int offset = (row * MAX_COLS + col) * 2;
    int i;
    for(i = 0; message[i] != '\0'; i++){
        int actually_offset = offset + i * 2;
        video_memory[actually_offset] = message[i];
        video_memory[actually_offset + 1] = 0x4f;
    }
}
// ---------------------------------------------------------
// 1. 底层硬件通信函数 (内联汇编)
// ---------------------------------------------------------
// 向端口写一个字节(outb)
void port_byte_out(unsigned short port, unsigned char data){
    __asm__("out %%al, %%dx" : : "a"(data), "d"(port));     //AT&T语法，源操作数在前目标操作数在后
    //也就是说这其实是把al寄存器中的值写到dx对应的端口中去
}
// 从端口读一个字节(inb)
unsigned char port_byte_in(unsigned short port){
    unsigned char result;
    __asm__("in %%dx, %%al" : "=a"(result) : "d"(port));
    return result;
}
// ---------------------------------------------------------
// 2. 屏幕驱动函数
// ---------------------------------------------------------
// 获取当前光标位置(0-1999)
int get_cursor_offset(){
    port_byte_out(REG_SCREEN_CTRL, 14); // 将数据14写入到索引寄存器，告诉它我们需要获取光标位置的高8位
    int offset = port_byte_in(REG_SCREEN_DATA) << 8; //获取光标位置的高8位，左移保证数据是高8位的
    port_byte_out(REG_SCREEN_CTRL, 15); // 声明我们要读取光标位置的低8位
    offset += port_byte_in(REG_SCREEN_DATA);
    return offset * 2;  // 显存中每个字符占2字节
}

//设置光标位置
void set_cursor_offset(int offset){
    //把显存偏移量转换回字符索引（2）
    offset /= 2;

    //写入高8位来调整光标位置
    port_byte_out(REG_SCREEN_CTRL, 14);
    port_byte_out(REG_SCREEN_DATA, (unsigned char)(offset >> 8));
    
    //写入低8位来调整光标位置
    port_byte_out(REG_SCREEN_CTRL, 15);
    port_byte_out(REG_SCREEN_DATA, (unsigned char)(offset & 0xff));
}

//打印单个字符并处理光标移动和换行
void print_char(char character, int col, int row, char attribute_byte){
    //创建显存指针
    unsigned char* video_memory = (unsigned char*) VIDEO_MEMORY;

    //默认颜色为白底黑字
    if(!attribute_byte){
        attribute_byte = 0x0f;
    }

    int offset;
    if(col >= 0 && row >= 0){
        offset = (row * MAX_COLS + col) * 2;
    }else{
        offset = get_cursor_offset();
    }
    if(character == '\n'){ //处理换行符
        int rows = offset / (2 * MAX_COLS);     // 计算当前所在行，由于offset表示字节偏移量，所以其实要把列字节数*2
        // 移动到下一行开头
        offset = get_cursor_offset();
        rows = offset / (2 * MAX_COLS);
        offset = (rows + 1) * (2 * MAX_COLS); // 下一行的开头
    }else{
        video_memory[offset] = character;
        video_memory[offset + 1] = attribute_byte;
        offset += 2;
    }
    offset = handle_scrolling(offset);
    set_cursor_offset(offset);
}
// 打印字符串（对print_char)的封装
void kprint(char *message){
    int i = 0;
    for(; message[i] != '\0'; i++){
        print_char(message[i], -1, -1, 0);
    }
}
// 打印退格键，本质上是退回一个光标位置输出空格覆盖原字符，再把光标回退一格。
void kprint_backspace(){
    int offset = get_cursor_offset() - 2;
    int row = offset / (2 * MAX_COLS);
    int col = (offset / 2) % MAX_COLS;

    if(offset > 0){ // 只有当偏移量大于0的时候才可以删除，要不然会把提示符'>'也给删除掉
        print_char(' ', col, row, 0x0f);
        set_cursor_offset(offset);
    }
}
void clean_screen(){
    int screen_size = MAX_ROWS * MAX_COLS;
    unsigned char *video_memory = (unsigned char *)VIDEO_MEMORY;
    for(int i = 0; i < screen_size; i++){
        video_memory[i * 2] = ' ';
        video_memory[i * 2 + 1] = 0x0f;
    }
    set_cursor_offset(0);
}

//设置IDT门
void set_idt_gate(int n, u32 handler){
    // n:中断号，大小从0-255;  handler:函数的地址，32位的数字
    
    idt[n].low_offset = handler & 0xffff;  //设置idt的低16位

    idt[n].sel = 0x08;  // 设置段选择子，实际上是在计算相对于GDT起始地址的偏移
    //注意在平台模式下，内核代码段的起始位置为0x08（基址偏移）。

    idt[n].always0 = 0; // 保留位，必须为0

    idt[n].flags = 0x8e;    // 0x8E = 1(Present) 00(Privilege) 0(System) 1110(32-bit Interrupt Gate)

    idt[n].high_offset = (handler >> 16) & 0xFFFF;
}

// 设置IDT表的地址
void set_idt(){
    idt_reg.base = (u32) &idt; // 设置IDT表的起始地址
    idt_reg.limit = sizeof(idt) - 1; // 设置IDT表的大小（按字节表示，确保不会数组越界。

    __asm__ volatile("lidt (%0)" : : "r"(&idt_reg));
}

void init_keyboard() {
    // --- 1. PIC 重映射 (魔术代码，照抄即可) ---
    // 这里的目的是把 IRQ1 (键盘) 映射到 IDT 的第 33 号位置 (0x21)
    port_byte_out(0x20, 0x11);
    port_byte_out(0xA0, 0x11);
    port_byte_out(0x21, 0x20); // 主 PIC 从 0x20 (32) 开始
    port_byte_out(0xA1, 0x28); // 从 PIC 从 0x28 (40) 开始
    port_byte_out(0x21, 0x04);
    port_byte_out(0xA1, 0x02);
    port_byte_out(0x21, 0x01);
    port_byte_out(0xA1, 0x01);
    port_byte_out(0x21, 0x0);
    port_byte_out(0xA1, 0x0);
    
    // --- 新增：设置中断屏蔽掩码 (IMR) ---
    // 0xFD = 1111 1101
    // 意思是：除了 IRQ1 (键盘) 是 0，其他全是 1 (屏蔽)
    // 这样 IRQ0 (时钟) 就不会来打扰我们了！
    // port_byte_out(0x21, 0xFD);

    // --- 2. 设置 IDT ---
    // 我们要把第 33 号中断 (IRQ1) 指向 isr_keyboard
    // 注意：isr_keyboard 需要先 extern 声明
    extern void isr_keyboard();
    
    // 33 是 0x21 (32 + 1)
    set_idt_gate(33, (u32)isr_keyboard);
    set_idt();

    // --- 3. 开启中断！---
    // 这就是之前的“危险动作”，现在可以安全执行了
    __asm__ volatile("sti");
    // kprint("Keyboard is init\n");
}

//  键盘处理函数
void keyboard_handler_c(){
    u8 scancode;    // 保存键盘按键时的扫描码
    char ascii_char;    // 对应的字符

    scancode = port_byte_in(KEYBOARD_PORT);  // 从键盘读取一个字符
    
    // print_hex(scancode);

    if(scancode > 57){  // 忽略键盘松开按键和其他无效键
        port_byte_out(0x20, 0x20);  
        return;
    }
    if(scancode == 0x0E){   // 退格键
        if(strlen(key_buffer) > 0){
            backspace(key_buffer);  // 从缓冲区中删除字符
            kprint_backspace();     // 从视觉上回退光标
        }  
    }else if(scancode == 0x1C){     // 处理回车键
        kprint("\n");
        user_input(key_buffer);
        key_buffer[0] = '\0';
        kprint("> ");
    }else{    // 键盘“按下”和“松开”是不同的扫描码，此时只处理按下逻辑
        ascii_char = keymap[scancode];

        // print_hex(scancode);
        // print_hex((u8) ascii_char);
        append(key_buffer, ascii_char);
        char str[2] = {ascii_char, 0};  // 构建字符串用于打印
        kprint(str);
    }

    port_byte_out(0x20, 0x20);
    //发送EOI(End of Interrupt，中断结束信号)给主PIC
    //目的在于表示中断已处理完成
}

// 命令执行函数
void user_input(char *input){
    if(strcmp(input, "exit") == 0){
        kprint("Stopping the opertion system, Good Bye!\n");
        __asm__ volatile("hlt");
    }else if(strcmp(input, "page") == 0){
        // 这是一个测试命令，用来测试动态内存分配是否还没实现
        // 实际上我们可以用它来打印当前物理内存位置
        u32 phys_addr;
        __asm__ volatile("mov %%cr3, %0" : "=r"(phys_addr));
        kprint("Page Directory: ");
        print_hex32(phys_addr);
        kprint("\n");
    }else if(strcmp(input, "hi") == 0){
        kprint("Hello, I am the XYTriste operation system.\n");
    }else if(strcmp(input, "cls") == 0){
        clean_screen();
    }else if(strcmp(input, "malloc") == 0){
        u32 ptr = kmalloc(50, 0, 0);
        print_hex32(ptr);
        kprint("\n");
        u32 ptr2 = kmalloc(50, 0, 0);
        print_hex32(ptr2);
        kprint("\n");
        u32 ptr3 = kmalloc(10, 1, 0);
        print_hex32(ptr3);
        kprint("\n");
    }else{
        kprint("The \"");
        kprint(input);
        kprint("\" is not an internal or external command,\n not a runnable program or batch file..\n");
    }
}

// 处理命令行中内容满时，应该进行滚屏操作
int handle_scrolling(int cursor_offset){
    char *video_memory = (char *) VIDEO_MEMORY;

    if(cursor_offset < MAX_COLS * MAX_ROWS * 2){ // 此时还没有到达屏幕下边缘
        return cursor_offset;
    }

    // 1.把第i行的内容复制到第i - 1行去
    for(int i = 1; i < MAX_ROWS; i++){
        string_copy(
            (char *)VIDEO_MEMORY + (i * MAX_COLS * 2),
            (char *)VIDEO_MEMORY + ((i - 1) * MAX_COLS * 2),
            MAX_COLS * 2
        );
    }
    
    // 2.清空最后一行
    char *last_line = video_memory + (MAX_ROWS - 1) * MAX_COLS * 2;
    for(int i = 0; i < MAX_COLS * 2; i++){
        // if(i % 2 == 0){
        //     last_line[i] = ' ';
        // }else{
        //     last_line[i] = 0x0f;
        // }
        last_line[i] = 0;
    }

    cursor_offset -= 2 * MAX_COLS;
    return cursor_offset;
}
