#include"mem.h"

size_t free_mem_addr = 0x100000;

size_t kmalloc(size_t size, int align, size_t *phys_addr){
    size_t* _allocate_start;
    if(align == 1 && (free_mem_addr & 0xFFFFF000)){ // 如果一个地址是4KB对齐的，那么这个地址的十六进制低3位，对应二进制的低12位一定是0。
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }
    _allocate_start = (size_t *)free_mem_addr;
    free_mem_addr += size;

    memory_set(_allocate_start, 0, size);

    if(phys_addr != 0){
        *phys_addr = _allocate_start;
    }
    return (size_t) _allocate_start;
}

void memory_copy(size_t *source, size_t *dest, int nbytes){
    for(int i = 0; i < nbytes; i++){
        *(dest + i) = *(source + i);
    }
}

void memory_set(size_t *dest, size_t val, size_t len){
    for(int i = 0; i < len; i++){
        *(dest + i) = val;
    }
}