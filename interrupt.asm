[bits 32]
extern keyboard_handler_c
global isr_stub     ; 全局符号，确保能被编译器看到
global isr_keyboard

isr_stub:
    pusha
    mov al, 0x20
    out 0x20, al

    popa
    iretd

isr_keyboard:
    pusha   ; 保存所有通用寄存器（push all）
    call keyboard_handler_c
    popa    ; 恢复所有通用寄存器 (pop all)

    iretd
    

