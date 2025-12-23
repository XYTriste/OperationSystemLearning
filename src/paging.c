#include"paging.h"
#include"mem.h"
#include"kernel.h"

// 页目录，包含1024个页表入口
// 使用u32指针表示它，并用kmalloc分配空间
u32* page_directory = 0;

void init_paging(){
    // 1. 分配页目录
    // 必须4KB对齐，关于这一点可以利用kmalloc函数的align参数
    page_directory = (u32 *)kmalloc(1024 * sizeof(u32), 1, 0);

    // 2. 分配第一张页表，映射0 - 4MB的内存空间
    // 同样需要4KB对齐
    u32* first_page_table = (u32 *)kmalloc(1024 * 4, 1, 0);

    // 3. 填充第一张页表
    // 把虚拟地址0 - 4MB 映射到物理地址，恒等映射
    /*
    标志位从低到高分别是： bit0( 1表示存在，0表示不存在，访问发生缺页中断)
    bit1 (1 表示可读写 0 表示只读)
    bit2 (1 表示用户态可访问 0 表示仅内核可访问)
    */ 
    u32 addr = 0;
    for(int i = 0; i < 1024; i++){
        first_page_table[i] = addr | 3;     // 标志位设置为011
        addr += 4096;
    }

    // 4. 填充页目录
    // 第一项指向我们创建的第一张页表
    page_directory[0] = (u32) first_page_table | 3;

    // 5. 初始化页目录的其他项
    // 由于此时还未分配内存，因此其他页此时不可用（bit0 设置为0）
    for(int i = 1; i < 1024; i++){
        page_directory[i] = 0 | 2;
    }

    // 6. 加载CR3寄存器
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_directory));

    // // 7. 开启分页(CR0的PG位)
    // u32 cr0;
    // __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    // // PG位在第31位(0x8000000)
    // cr0 |= 0x80000000;
    // __asm__ volatile("mov %0, %%cr0" : "=r"(cr0));

    __asm__ volatile(
        "mov %%cr0, %%eax\n\t"    // 1. 把 CR0 搬到 EAX
        "or $0x80000000, %%eax\n\t" // 2. 把 EAX 的第 31 位置 1 (0x80000000)
        "mov %%eax, %%cr0"        // 3. 把 EAX 写回 CR0
        :                         // 无输出
        :                         // 无输入
        : "eax"                   // 告诉编译器：如果不小心用了 EAX，请避让，因为我这里改了它
    );
}