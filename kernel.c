// kernel.c

//显存配置
#define VIDEO_MEMORY 0xb8000
#define MAX_COLS 80
#define MAX_ROWS 25

// 端口配置（VGA控制端口）
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

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

#define IDT_ETRIES 256
idt_gate_t idt[IDT_ETRIES];
idt_register_t idt_reg;
 
void kprint_at(char *message, int row, int col);
void port_byte_out(unsigned short port, unsigned char data);
unsigned char port_byte_in(unsigned short port);

int get_cursor_offset();
void set_cursor_offset(int offset);

void print_char(char character, int col, int row, char attribute_byte);
void kprint(char *message);
void clean_screen();

void set_idt_gate(int n, u32 handler);

void main(){
    clean_screen();
    kprint("This is a XYTriste kernel\n");
    kprint("It works!!!");

    // asm volatile("sti");

    while(1);
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
    set_cursor_offset(offset);
}
// 打印字符串（对print_char)的封装
void kprint(char *message){
    int i = 0;
    for(; message[i] != '\0'; i++){
        print_char(message[i], -1, -1, 0);
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
    
    idt[n].low_offset = handler & 0xff;  //设置idt的低16位

    idt[n].sel = 0x08;  // 设置段选择子，实际上是在计算相对于GDT起始地址的偏移

    idt[n].always0 = 0; // 保留位，必须为0

    idt[n].flags = 0x8e;    // 0x8E = 1(Present) 00(Privilege) 0(System) 1110(32-bit Interrupt Gate)

    idt[n].high_offset = handler >> 8;
}

// 设置IDT表的地址
void set_idt(){
    idt_reg.base = (u32) &idt; // 设置IDT表的起始地址
    idt_reg.limit = sizeof(idt) - 1; // 设置IDT表的大小（按字节表示，确保不会数组越界。

    __asm__ volatile("lidt (%0)" : : "r"(&idt_reg));
}