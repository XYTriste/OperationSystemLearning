[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov bx, 0x7e00  ;调用约定，读扇区服务使用es:bx作为段:偏移读地址

    mov ah, 0x02
    mov al, 15

    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [boot_drive]
    int 0x13

    jc disk_error

    mov si, msg_read_ok
    call print_string

    cli
    lgdt [gdt_descriptor]
    
    in al, 0x92
    or al, 2
    out 0x92, al
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_pm

print_string:
    .loop:
        lodsb
        or al, al
        jz .done

        mov ah, 0x0e
        int 0x10
        jmp .loop
    .done:
        ret

loop_start:
    mov ah, 0x00
    int 0x16

    cmp al, 0x0d
    je handler_enter

    cmp al, 0x08
    je handler_backspace
    
    mov ah, 0x0e
    int 0x10
    jmp loop_start

handler_enter:
    mov ah, 0x0e
    int 0x10
    mov al, 0x0a
    int 0x10

    jmp loop_start

handler_backspace:
    mov ah, 0x0e
    int 0x10
    mov al, 0x20
    int 0x10
    mov al, 0x08
    int 0x10
    jmp loop_start

disk_error:
    mov si, msg_error
    call print_string
    jmp $

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp    ;初始化栈

    jmp 0x7e00

msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0
boot_drive db 0
msg_loading db "Loading...", 0x0d, 0x0a, 0
msg_error db "Disk Error!", 0
msg_read_ok db "Sector loading OK.", 0x0d, 0x0a, 0

gdt_start:

gdt_null:
    dd 0x0
    dd 0x0

gdt_code:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 0x9a
    db 11001111b
    db 0x00

gdt_data:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 0x92
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

times 510-($-$$) db 0
dw 0xaa55


;sector2_start_32:
;    ;mov si, msg_kernel_success
;    ;call print_string
;    ;上面两行代码在cli清空所有中断后不再可用
;    
;    mov ebx, 0xb8000
;    mov byte [ebx], 'P'
;    mov byte [ebx + 1], 0x0f
;
;    mov byte [ebx + 2], 'M'
;    mov byte [ebx + 3], 0x0f
;
;    jmp $
;
;msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0
;msg_kernel_success db "Kernel Loaded Successfully! You are in Sector 2 now!", 0x0d, 0x0a, 0
;times 512-($-sector2_start_32) db 0