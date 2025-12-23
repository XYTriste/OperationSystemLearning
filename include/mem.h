#include "type.h"

#ifndef __MEM_H__
#define __MEM_H__

/*
size: 请求分配的字节大小
align: 对齐方式(1: 表示分配内存空间的起始地址按4KB对齐，0则紧接上次分配结束的下一个字节即可)
phys_addr: 可选参数，如果非NULL，则让该指针指向分配的地址起始位置
*/
size_t kmalloc(size_t size, int align, size_t *phys_addr);

// 内存拷贝：把 source 处的 n 个字节拷贝到 dest
void memory_copy(size_t *source, size_t *dest, int nbytes);

// 内存设置：把 dest 开始的 n 个字节全部设为 val
// 这对于初始化结构体或者清空数组（置0）非常有用
void memory_set(size_t *dest, size_t val, size_t len);

#endif