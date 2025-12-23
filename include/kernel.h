#include"type.h"
#ifndef __KERNEL__H__
#define __KERNEL__H__
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
void init_interrputs();
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


#endif