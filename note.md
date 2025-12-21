# 操作系统
## 启动计算机
### 阶段一：硬件唤醒与复位 (Power On & Reset)
> 时间：0ms - 100ms

1. 当按下电源键时，电源供应器（PSU）开始工作，当电压稳定后，它会向主板发送一个“Power Good”信号。
2. CPU 复位 (Reset)：主板收到信号后，撤销对 CPU 的“复位”保持。此时，CPU 的寄存器被强制初始化为特定状态。
   > 这里的“CPU 的寄存器被强制初始化”是由CPU内部的微码完成的，微码指的是CPU内部ROM中的“固件指令集”。它可以直接驱动内部数据总线，把值写入到寄存器中。此时CS(Code Segment,代码指针寄存器)被初始化为0xF000,IP(Instruction Pointer，指令指针寄存器)被初始化为0xFFF0。而CPU内部维持了一个不可见的Cache of Segment Descriptor Base（段描述符基址缓存），它被初始化为0xFFFF。同时<font color="red">地址总线中的A31-A20位被强制置为高电平（表示1，Intel 手册叫 “Reset vector” 或 “Boot strap” 区域）。</font>由于硬件电路在复位到微码状态时，它的走线方式为“段描述符基址 + IP”这样的方式，因此计算得到的物理地址为0xFFFFFFF0。

3. `0xFFFFFFF0`这一地址会被送到CPU 内部的PCIe Root Complex（或叫 System Agent），在这一部件中会把地址和一组硬连线寄存器进行比对：
    - 0x0000_0000 – Top-of-RAM → DRAM 控制器
    - 0xFE00_0000 – 0xFFFF_FFFF → SPI Flash 窗口
    - 其他区间 → PCIe、MMIO、APIC …

    此时命中 Flash 窗口，CPU 自己就把事务转成SPI周期，通过专用SPI引脚直接去找焊在主板上的 8-pin/16-pin Flash；这里通常保存了一个远跳转指令，CPU读取后会把下一条要读取的指令地址放在跳转后的位置，也就是bios的入口处。远跳转指令执行后，Cache of Segment Descriptor Base会被改为0x000F0000，以回到真实的实模式状态下，地址总线中的强制高电平也会被解除。


总而言之，微观流程最终为：
1. Power On。
2. CPU Reset：
   - CS_Selector = 0xF000
   - CS_Base (Hidden) = 0xFFFF0000
   - EIP = 0xFFF0
3. Address Generation：CPU 生成物理地址 0xFFFF0000 + 0xFFF0 = 0xFFFFFFF0。
4. Routing：芯片组识别该地址，将其路由至 SPI Flash 芯片的尾部。
5. Fetch：CPU 拿到 Flash 中最后的 16 字节指令。这里的指令通常都是远跳转指令，跳转到BIOS的入口处。
   > 为什么是最后16字节？因为0xFFFFFFF0 - 0xFFFFFFFF（4GB的内存空间顶端，至于为什么是4GB，因为早期只有32位地址总线。这么做某种意义上也是兼容旧的设计，而且也足够用，只要保证0xFFFFFFF0一定会被芯片组映射到SPI Flash中读取即可。）
6. Execute：执行 JMP 指令。
7. State Change：
   - 执行 JMP 后，CS Base 被更新，EIP 指向新的 BIOS 入口。
   - 此时，CPU 正式开始执行 BIOS 的 POST（自检）代码。

### 阶段二：实模式下的 BIOS 环境 (Legacy BIOS)
现在的状态是：

- CPU 模式：16 位实模式 (Real Mode)。
- CS:IP：跳转到了 BIOS 代码的主体部分（通常在低端内存 0xF0000 区域）。
- 内存视角：CPU 现在能“看”到 1MB 的内存空间 (0x00000 - 0xFFFFF)。

此时，CPU会开始执行BIOS的代码（BIOS本质上也是一个程序），BIOS的代码主要做了两件事：
<font color="red">**建立IVT(Interrupt Vector Table， 中断向量表)以及提供BIOS中断服务。**</font>
> 中断向量表的功能在于提供硬件中断操作。
1. 物理位置与结构
        在 实模式 (Real Mode) 下，CPU 硬件设计死了：内存的最开始 1KB 就是 IVT。
        - 地址范围：0x00000 - 0x003FF。
        - 大小：1024 字节。
        - 容量：256 个表项（也就是支持 0-255 号中断）。

    每个表项的结构（关键）： 每个中断向量占据 4 个字节。它存储了一个“远指针 (Far Pointer)”，指向实际处理代码的位置。 
    - 低 2 字节：偏移地址 (Offset / IP)
    - 高 2 字节：段地址 (Segment / CS)

    <font color="red">注意：x86 是 小端序 (Little Endian)。如果读取内存 0x00000 看到的是 `12 34 56 78`，那么：
    ```
        IP = 0x3412

        CS = 0x7856

        跳转目标 = 0x7856:0x3412
    ```
    </font>
2. 工作原理：CPU 如何查表？
    
    当发生中断（例如按下键盘触发 INT 9，或者代码执行 INT 0x10）时，CPU 内部硬件会自动执行以下微操：
    1. 计算入口地址：**中断号 × 4**。
    例如 INT 0x10（显卡服务），地址就是 0x10 × 4 = 0x40。
    2. 取地址：CPU 直接去内存 0x00040 处读取 4 个字节。
    3. 保存现场：CPU 自动把当前的标志寄存器 (FLAGS)、CS、IP 压入堆栈（这也是为什么堆栈必须先设好的原因！）。
    4. 飞跃：CPU 将读取到的 CS 和 IP 赋值给寄存器，执行流瞬间跳转到中断服务程序 (ISR)。

    > 为什么BIOS要建立中断向量表？在我的理解中，最重要的原因是因为此时操作系统还没有被加载，自然也就不包含任何的驱动程序。此时如果我们需要进行某些操作，例如把操作系统从磁盘读取到内存进行加载（软件中断），或者通过键盘按键执行某个操作（硬件中断），就必须“告诉”CPU当我使用这些方法时，你需要执行哪些指令。中断向量表实际上是提供了一系列“函数”的地址，让CPU在执行中断操作时知道要去哪里“找到应该怎么做”。

    > 而驱动程序的本质，就是覆盖IVT表中对应的地址指针，使其指向驱动程序的代码。装好了驱动以后，CPU在执行对应中断的时候调用的“函数”就是我们的驱动程序，而不是BIOS预设好的代码。（注意，这一原理仅适用于实模式，在保护模式以及别的模式下，IVT不再使用，驱动程序的设计更加复杂。）

    > 当我们产生某个中断（比如键盘操作触发中断）时，CPU首先会通过中断号 * 4的方式，计算中断号所对应的中断向量表中的4字节地址。例如，按下键盘触发的中断号是`09h`，因此中断向量表中的地址为`09h * 4 = 0x24`，CPU会到内存中`0x24`这个起始位置读取4个字节，前面2个字节保存在偏移地址指针（IP）中，而后面2个字节则保存在段指针（CS）中。这4个字节共同组成了一个**远地址**，对应着实际的键盘处理函数的位置。

    > IVT 里放的“远地址”是 16 位段 + 16 位偏移，共 32 位（4 字节），能表示的极限地址就是`0xFFFF:0xFFFF → 0x10FFEF`，仍在 1 MB 范围内。硬件设计时已经掐死了：只要 CPU 跑在实模式，中断处理程序必须落在 1 MB 以内。

实战演示：按下 "A" 键时发生了什么？
跟踪一下在实模式下按下键盘 "A" 键的瞬间：

1. 硬件层：键盘控制器通过电路向 CPU 发送信号（IRQ 1）。
2. 中断控制器 (PIC)：把 IRQ 1 转换成中断号 0x09，告诉 CPU。
3. CPU 查表：CPU 计算 9 × 4 = 36 (0x24)。它去内存 0x00024 读取地址。
4. 跳转 BIOS 代码：CPU 跳到了 BIOS 预设的键盘处理函数。
5. BIOS 干活：BIOS 读取端口，发现是 "A" 的扫描码，把它转成 ASCII 'a'，存入 BIOS 数据区（BDA）的缓冲区。
6. 中断返回 (IRET)：BIOS 执行完后，执行 IRET 指令，CPU 从堆栈弹出之前的 CS:IP，回到刚才被中断的地方继续运行。

### 阶段三、四 神奇的 0x7C00 与 引导扇区
在这一时刻，BIOS已经准备好了IVT，中断服务也可以用了。下一步就是加载我们的操作系统了。
这就涉及到了操作系统中一个最著名的数字：`0x7C00`。
首先，BIOS会检查硬盘中的**第一个扇区（Sector 0, Cylinder 0, Head 0）**。
- 物理硬盘的最小读写单位就是扇区，通常是 512 字节。
- BIOS 会把这 512 字节读入内存 <font color="red">**（注意，读取到`0x7C00`为起始地址的512个字节中）**</font>。

读取这512字节后，BIOS会检查最后两个字节（第510、511字节）。
 - 如果是 `0x55` 和 `0xAA`：BIOS 认为“这是引导程序”，于是跳转去执行。
 - 如果不是：BIOS 认为这只是数据，跳过，去检查下一个设备。

> 为什么是读取到`0x7C00`，而不是什么别的位置？这是由于0x0000-0x003ff保存了IVT，而BIOS从0xF0000开始,操作系统又想要尽可能放在低地址（例如`0x00500`）的位置，因此IBM工程师们约定好放在了`0x7C00`（32KB）处，既不会干扰IVT以及操作系统加载，又留下了高地址空间作为栈来使用。


## 实模式操作系统
### HellWorld
在BIOS的工作完成之后，我们就可以开始尝试加载我们自己的操作系统了。这里我用的是WSL（Windows SubSystem Linux）环境下的NASM以及QEMU进行模拟。
HelloWorld操作系统的代码如下：
```assembly
[org 0x7c00]    ; 告诉编译器这段代码会被加载到 0x7c00

    ; --- 初始化 ---
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00 ; 设置栈顶在 0x7c00，栈是向下生长的，安全！

    ; --- 打印字符 'H' ---
    mov ah, 0x0e   ; BIOS 0x10 中断的 "打印字符" 功能
    mov al, 'H'
    int 0x10

    ; --- 打印字符 'i' ---
    mov al, 'i'
    int 0x10

    ; --- 打印字符 '!' ---
    mov al, '!'
    int 0x10

    ; --- 死循环 ---
    jmp $          ; 让 CPU 停在这里，否则它会乱跑执行后面的垃圾数据

    ; --- 填充与签名 ---
    times 510-($-$$) db 0  ; 填充 0 直到第 510 字节
    dw 0xaa55              ; 它是引导扇区的标志！
```
这里需要解释的几个部分：
1. 首先就是`org 0x7c00`，org是origin的缩写，这句话的意思是告诉编译器，以下代码的地址都要加上`0x7C00`的偏移，才是代码在物理内存中的实际地址。这句代码在编译后并不会成为操作系统代码的一部分，只是为了保证在模拟时按照真实操作系统的情况进行加载。
2. 初始化的部分完成了对于通用寄存器以及段寄存器和栈基址和栈顶指针的的加载，其中`SS:SP`分别表示栈基址和栈顶指针，它们分别初始化为`0x00`和`0x7C00`。因此`0x00-0x7C00`这`32KB`的空间就成了我们的IVT、BIOS数据以及引导扇区的保存处。
3. `ax`是一个通用寄存器，它的特殊之处在于**可以通过指定功能号来告诉ROM例程需要调用的功能**。例如`mov ah, 0x0e`就是告诉ROM例程，需要执行"打印字符"的功能。
    > 与`ax`类似的还有`bx、cx、dx`这三个通用寄存器，它们都可以拆成高8位和低8位的两个寄存器使用，高位就是high，写做“ah”，低位就是low，写作“al”。在调用功能时，`ah`用于指定功能号，`al`用作保存需要使用的值。
4. `int 0x10`这里的`int`指的是`interrupt`,通过后面的中断号来执行相应的中断。这里的`0x10`指的就是BIOS视频中断，完成显示功能。
5. `jmp`是跳转指令,`$`则表示当前指令地址，这里就是让操作系统一直跳转到`jmp $`这条指令处，模拟死循环，避免操作系统去读更低地址的代码，从而导致读到无效区域导致崩溃。
6. `times`是一个伪指令，用于指定某操作重复的次数。后面的`510-($-$$)`中，`$$`表示的是栈的起始地址（0x7C00），所以`$-$$`就是在计算我们编写的代码占用了多少个字节，再用510-字节数得出需要填充的字节数量，再用`db(define byte)`命令向其中填充0。最后使用`dw 0xaa55`把最后两字节填充为`0x55 0xaa`（小端序）。

### 实现“打字机”功能
**目标**：系统启动后，屏幕黑屏等待。当您按下键盘上的任意字符键，屏幕上立刻显示该字符。这需要无限循环进行。
1. 键盘输入服务 (BIOS Keyboard Service)

    这是“读取按键”的方法。
    - 中断号：`int 0x16`
    - 功能号：ah = 0x00 (代表“读取下一个按键”)
    - 行为：这是一个阻塞 (Blocking) 调用。如果不按键，CPU 会停在这行指令不动，直到按下一个键。

    返回值：
    - al 寄存器：存放按键的 ASCII 码（比如按下 'A'，al 就是 0x41）。
    - ah 寄存器：存放按键的扫描码（Scan Code，也就是键盘上那个键的物理位置编号，暂时不用管）。

2. 屏幕输出服务 (BIOS Video Service)
    - 中断号：int 0x10
    - 功能号：ah = 0x0e (代表“电传打印模式”)

    参数：
    - al 寄存器：要打印的字符的 ASCII 码。

代码如下：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

loop_start:
    mov ah, 0x00
    int 0x16

    mov ah, 0x0e
    int 0x10

    jmp loop_start

    times 510-($-$$) db 0
    dw 0xaa55
```
其实没有太多特殊的地方，主要就是设置循环的代码`loop_start：`。然后注意`int 0x16`中断会使得计算机等待用户输入，用户按下按键后会把按键值保存在`al`中，直接指定功能号并使用`int 0x10`中断进行显示即可。最后通过跳转到开头实现不断读取字符的功能。
> 值得注意的是，`org times db dw`这些指令实际上是伪汇编指令，它们在汇编器进行汇编时会直接对相应位置的字节进行操作（“填充0”、“填充某字节”），而不是经由汇编器翻译后形成对应的汇编指令。这也就意味着上述代码的`loop_start`的循环一直到了代码结束的部分。这也就是为什么不用担心这段代码被加载到引导后，循环反复填充相同位置的字节的原因。

### 修复“回车键”

当我们按下回车键（Enter）时，会发现屏幕光标确实回到了行首，但没有下一行。这是因为在那个古老的电传打字机（Teletype）时代，**“回车”和“换行”**是两个完全独立的机械动作：
1. Carriage Return (CR, 0x0D)：字车（打印头）弹回到最左边。

    - 这就是键盘上 Enter 键发送的字符：0x0D。

    - 这也是为什么光标跑回了行首。

2. Line Feed (LF, 0x0A)：卷纸滚筒转动一下，纸张往上走一行。

    - BIOS 的 0x0E 功能非常“老实”，给它 `0x0D`，它就只做 CR；除非再给它一个 `0x0A`，否则它绝不换行。

这就是 Windows 里换行符是 `\r\n (CR+LF)` 的由来，而 Linux 里只有 `\n`。 此时的 Toy OS 现在就像一台原始打字机，需要手动去推那个换行杆。

为什么 Backspace 删不掉字符？
按下退格键（Backspace）时，实际上是发送了 ASCII 码 0x08。 在 BIOS 的电传模式下，它接收到 0x08，只会把光标往左移动一格（非破坏性退格）。它不会自动把那个字擦掉（变成空格）。
要想实现“删除”效果，我们必须手动指挥 CPU 做三个动作：
- 退格：输出 `0x08`（光标左移）。
- 覆盖：输出 `0x20`（空格），把刚才写的字盖住。
- 再退格：再次输出 `0x08`（让光标回到正确的位置准备写新字）。

那么，如果要实现换行以及删除的效果，我们需要两个新指令：

1. `cmp a, b (Compare)`：比较 a 和 b。

2. `je label (Jump if Equal)`：如果刚才比较的结果是相等，就跳转到 label 处执行。

新的代码为：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

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

times 510-($-$$) db 0
dw 0xaa55
```
这段代码实现了对于回车以及退格键的处理，实际上也就是多打印了几个字符实现换行和退格的效果。

### 让 OS“学会说话” (字符串与内存寻址)
在这一示例中，我们添加代码来实现打开OS时，显示一行欢迎语句。
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg_welcome
    call print_string_byloop
    jmp loop_start

print_string_byloop:
    .loop:
        mov al, [si]
        inc si
        cmp al, 0
        je .done

        mov ah, 0x0e
        int 0x10
        jmp .loop
    .done:
        ret

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

msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0

times 510-($-$$) db 0
dw 0xaa55
```
这里的改进主要是定义了一个字符串来作为我们OS打开时的欢迎语句。也就是`msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0`，其中`msg_welcome`的作用类似于变量名，告诉调用者去哪里找到这个字符串。`db`表示将内容按字节存储为ASCII码，`0x0d`和`0x0a`分别表示将字符回到行首以及换行。
除此之外，我们还定义了一个函数`print_string`，并在引导程序初始化了相关寄存器后立即调用该函数以显示欢迎语句。在函数中我们使用了一些新的内容：
1. 为了读取这个字符串，我们需要一个寄存器来充当“手指”，指着当前要打印的字母。 在 x86 汇编中，专门有一个寄存器叫 `SI (Source Index，源变址寄存器)`，专门用来干这个。它的作用是，指向一个要打印的字符串的某个字母（初始的时候指向第0个字母）。
2. `LODSB`指令，它的作用是把`SI`指向的字符加载到`al`中，并让`SI`指向字符串中下一个字符。类似于：
    ```C
    al = *SI;
    SI++;
    ```
在`print_string`中，我们还使用了一些小技巧。通常情况下，我们可以使用`cmp`来判断是否读到字符串结尾：
```assembly
cmp al, 0
je .done
```
而寄存器是存在着若干个标志位的（有关标志位的内容后续了解），当我们使用：
```assembly
or al, al
jz .done
```
此时，如果`al`的值为0，那么它的零标志位会被标记为`1`，此时可以通过`jz(jump if zero)`命令进行跳转。

### 读取并执行 Sector 2
现在我们的任务是读取磁盘中的第二个扇区，我们已知扇区1（扇区编号从1开始，扇区1指引导扇区）从地址`0x7C00`开始，大小为512字节，那么也就是到`0x7DFF`结束正好结束。扇区2的开始地址为`0x7E00`，大小同样为512字节。
由于我们的`times`命令实现了对于第一个扇区的填充，因此我们只要接着后面写代码，实际上在将其加载到内存中时，后面的代码就处于第二个扇区中。我们可以通过指定功能号(`0x02`)、读取数量(`1`)以及指定柱面、磁头、扇区号、驱动器号来指定要读取的扇区，最后调用`int 0x13`中断要求BIOS读取磁盘。完整代码如下：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov bx, 0x7e00
    mov ah, 0x02
    mov al, 1

    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [boot_drive]
    int 0x13

    jc disk_error

    mov si, 0x7e00
    call print_string

    mov si, msg_welcome
    call print_string

    jmp loop_start

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

msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0
boot_drive db 0
msg_loading db "Loading...", 0x0d, 0x0a, 0
msg_error db "Disk Error!", 0

times 510-($-$$) db 0
dw 0xaa55

;msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0
times 512 db 'A'
```

### 控制权的交接
现在，我们的引导扇区的代码越来越膨胀了，也即将到达边缘。为了保证我们的操作系统能够正常运行，我们需要去执行扇区2的代码，为了达到这一目标，我们需要做的事为：读取第2个扇区，然后跳转到第2个扇区开始执行代码。因此我们修改我们的代码，完整如下：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov bx, sector2_start

    mov ah, 0x02
    mov al, 1

    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [boot_drive]
    int 0x13

    jc disk_error

    jmp sector2_start

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

msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0
boot_drive db 0
msg_loading db "Loading...", 0x0d, 0x0a, 0
msg_error db "Disk Error!", 0

times 510-($-$$) db 0
dw 0xaa55

sector2_start:
    mov si, msg_kernel_success
    call print_string
    jmp $

msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0
msg_kernel_success db "Kernel Loaded Successfully! You are in Sector 2 now!", 0x0d, 0x0a, 0
times 512-($-sector2_start) db 0
```
这里我们需要注意的坑就是，我们需要知道`jmp`指令实际上是在告诉CPU，让`IP`指针指向对应的地址，而`IP`指针指向的位置会被CPU当做代码去执行。因此，如果我们在`jmp sector2_start`后跟了一条数据定义语句，例如`msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0`的话，CPU会把这条指令所对应的二进制代码当做代码去理解和执行，这会导致不可预料的后果。因此，就目前的知识范围而言，我们需要把数据定义部分的代码全部放在代码的后面，确保不会把数据当做代码去执行。

## 保护模式操作系统
在实模式下，地址总线可用数量为20，编号为`A0-A19`，这意味着仅有1MB空间可以访问。为了拓展可用空间的大小，同时避免实模式下的“任意物理地址都可以直接读写”带来的危险性，科学家们引入了保护模式。
在实模式下，我们寻址的方式为：
$$
物理地址\ = \ 段（CS）:偏移（IP）
$$
在保护模式下，寻址的方式仍然是`CS:IP`,但是此时`CS`不再直接表示某个物理地址，而是存储着某个索引，指向`GDT(Global descriptor table，全局描述符表)`中的某一行。
> 我们需要了解到，CS作为代码指针寄存器本身大小就是16位。因此，它本身在保护模式下被称为“段选择子”，CPU根据段选择子中保存的值，到相应的GDT/IDT表中去查询相应的段描述符。

GDT 表中的每一行（称为一个 描述符 Descriptor）有 8 个字节（64位）。它描述了一块内存区域的三个核心属性：
- Base (基址)：这块内存从哪里开始？（我们设为 0）

- Limit (界限)：这块内存有多大？（我们设为 4GB）

- Access/Flags (权限)：这是代码还是数据？内核级还是用户级？

`GDT`描述符的8字节结构可以表示为：
字节 | 位范围 | 内容
-----|--------|------
0-1  | 0-15   | 段界限低16位
2-3  | 16-31  | 段基址低16位
4    | 32-39  | 段基址中间8位
5    | 40-47  | 访问权限字节
6    | 48-55  | 段界限高4位 + 标志位
7    | 56-63  | 段基址高8位

详细说明：
基地址（32位）
- 分成三部分存储：低16位、中间8位、高8位

- 这是段的起始物理地址

段界限（20位）
- 表示段的大小

- 与粒度位（G）配合：

    - G=0：界限以字节为单位，最大1MB段

    - G=1：界限以4KB页为单位，最大4GB段

- 计算：实际大小 = (界限 + 1) * (G?4096:1)

> 这里我们需要注意到，粒度位的作用其实是一个开关，它决定了我们如何解释`limit`。当`G=0`时，我们把`limit`的界限按照字节进行解释。换句话说，`limit`每增长1，我们认为段界限增加1个字节。而`G=1`时，`limit`的界限按照`4KB`解释，此时`limit`每增长1，段界限增长`4KB`。这也是`x86`中分页机制一个标准页的大小。

访问权限字节（8位）
位 | 名称 | 含义
---|------|------
7  | P    | 段是否存在（1=存在）
6-5| DPL  | 描述符特权级（0-3，0最高）
4  | S    | 描述符类型（1=代码/数据段，0=系统段）
3-0| Type | 段类型（见下表）

`Type`字段详解：
```
对于代码段（S=1，Type[3]=1）：
  Type[2]: C - 一致位（0=非一致，1=一致）
  Type[1]: R - 可读（0=只执行，1=可读）
  Type[0]: A - 已访问（CPU自动设置）

对于数据段（S=1，Type[3]=0）：
  Type[2]: E - 扩展方向（0=向上扩展，1=向下扩展）
  Type[1]: W - 可写（0=只读，1=可写）
  Type[0]: A - 已访问
```

标志位（4位）
位 | 名称 | 含义
---|------|------
7  | G    | 粒度（0=字节，1=4KB）
6  | D/B  | 默认操作数大小（0=16位，1=32位）
5  | L    | 64位代码段标志
4  | AVL  | 保留给操作系统使用

段选择子的设计：
```
15              3  2  1  0
+----------------+--+--+--+
|     索引       |TI|RPL|
+----------------+--+--+--+
```
其中：
- 索引（13位）：指向GDT/LDT中的描述符（0-8191）
- TI（表指示符）：0=GDT，1=LDT
- RPL（请求特权级）：0-3

> 注意到，由于段选择子中，用于索引的位数有13位，自然而然`GDT`表中能够描述的段的数量就是$2^13 = 8192$个了。

在之前的代码中，我们新增了GDT表：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov bx, sector2_start

    mov ah, 0x02
    mov al, 1

    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [boot_drive]
    int 0x13

    jc disk_error

    jmp sector2_start
    

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

msg_welcome db "Welcome to XYTriste OS!!!", 0x0d, 0x0a, 0
boot_drive db 0
msg_loading db "Loading...", 0x0d, 0x0a, 0
msg_error db "Disk Error!", 0

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
    db 0x90
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

sector2_start:
    mov si, msg_kernel_success
    call print_string
    jmp $

msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0
msg_kernel_success db "Kernel Loaded Successfully! You are in Sector 2 now!", 0x0d, 0x0a, 0
times 512-($-sector2_start) db 0
```
现在，在引导扇区的最后一部分（或者说代码段之后）我们新增了一张`GDT`表，该表中正如之前所说的，按**每8个字节**作为一个描述符描述段的大小。
由于我们采用的是**平坦模式**，也就是整个内存空间都为一个段的设计，因此我们的段基址和段界限部分分别为**全0和全F**，而重点则是在`Access Byte(权限字节)`以及`Granularity Flags(粒度标志)`的部分，我们通过指定这部分来说明段的类型以及访问权限级别等关键内容。
此外，我们还需要了解一个重要的事实:<font color="red">CS作为代码段的段选择子，在我们使用`jmp`等指令跳转到下一条指令位置时，CPU永远都会用CS来作为段选择子来索引`GDT`表，这样在索引过程中就可以了解到被索引项是否是一个代码段（通过权限字节）。而在我们使用`mov`这样的内存读写指令时,CPU会使用`ds、es、fs、gs、ss`等数据段段选择子作为索引项，从而在索引过程中判断被索引的是否是一个数据段。从而避免了对于段的误读写。</font>

### 切换到保护模式
接下来，我们要做的就是编写相应的代码，使得计算机从实模式切换到保护模式。在这之前，我们需要确定几个要做的工作：
- 告诉CPU，接下来我们需要通过**段选择子：偏移地址**的形式来确定我们要访问的内存地址，这意味着我们需要一张`GDT`表，定义完`GDT`表以后，我们通过`lgdt`这一指令告诉CPU：GDT表的大小以及起始地址的位置。
- 告诉CPU，我们需要切换到保护模式。这一切换是通过改变CPU内部的`CR0`控制寄存器的**第0位**来实现的，当这一位是0时，CPU处于实模式。而当我们将其切换到1时，则CPU会按照`GDT`的规则去完成相应的寻址操作。

以下是修改后，读取第二个扇区并进入保护模式的代码：
```assembly
[org 0x7C00]

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov si, msg_loading
    call print_string

    mov bx, sector2_start_32  ;调用约定，读扇区服务使用es:bx作为段:偏移读地址

    mov ah, 0x02
    mov al, 1

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

    jmp sector2_start_32

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


sector2_start_32:
    ;mov si, msg_kernel_success
    ;call print_string
    ;上面两行代码在cli清空所有中断后不再可用
    
    mov ebx, 0xb8000
    mov byte [ebx], 'P'
    mov byte [ebx + 1], 0x0f

    mov byte [ebx + 2], 'M'
    mov byte [ebx + 3], 0x0f

    jmp $

msg_message_in_sector2 db "There is sector2!", 0x0d, 0x0a, 0
msg_kernel_success db "Kernel Loaded Successfully! You are in Sector 2 now!", 0x0d, 0x0a, 0
times 512-($-sector2_start_32) db 0
```
在这段代码中，我们需要注意`mov bx, sector2_start_32`这一行。我们需要知道的是，当我们使用`AH = 02h int 0x13`这样的读扇区服务时，<font color="red">BIOS程序员事先约定好将第二个扇区的内容加载到`es:bx`所对应的地址去。</font>
所以我们注意到，在初始化阶段我们将`es`初始化为0，而`bx`的地址就是`0x7c00 + 0x200`，也就是第二个扇区的起始位置`0x7e00`。
这就意味着，我们不能把`init_pm`的代码放在第二个扇区之前，由于我们使用`times`把引导扇区对齐到了512字节。如果我们紧接着`dw 0xaa55`后面写`init pm`的话，这意味着`sector_start_32`实际在第二扇区中的相对位置产生了偏移，进而在使用`jmp CODE_SEG:init_pm`时错误的跳转到了`0x0000:0x7e00`的位置，而实际要执行的代码`sector_start_32`在后面一部分，从而导致CPU无法找到要执行的指令。
> 更新于学习定义IDT结构体之前：需要注意到`cli`的作用并不是清除中断向量表，在CPU 内部有一个 EFLAGS 寄存器，里面有一位叫 **IF (Interrupt Flag)**。当我们调用`cli`时，实际上是把这一位置为0。此时键盘等硬件设备虽然能发出中断信号，但是不会被CPU进行处理，因此无法处理任何中断操作。

### C语言内核
我们发现，虽然我们在`boot.asm`中成功编写了引导扇区和第二个扇区的代码，但是迄今为止的效率非常低下，如果我们要打印一个字符串，那么就必须不断将字符搬运到`0xb8000`的位置，让字符显示在屏幕上。
因此，我们希望通过C语言的代码来实现字符的输出。首先，我们修改`boot.asm`的代码：
```assembly
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
    mov al, 1

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
```
这里我们注释掉了所有第二个扇区的代码，同时我们将原来的`mov bx, sector_start_32`修改成了`mov bx, 0x7e00`，这是因为我们现在希望将<font color="red">C语言的代码通过编译成二进制文件后，和`boot.asm`连接起来。</font>通过这样的方式，我们相当于多做了一步“翻译”的步骤，不再面对晦涩难懂的汇编代码，而是选择编写更加“高级”的C语言代码，再和汇编代码链接起来。
我们编写的第一个内核C语言`kernel.c`代码如下所示：
```C
void main(){
    char* video_memory = (char*)0xb8000;

    *video_memory = 'X';
    *(video_memory + 1) = 0x4f;

    while(1);
}
```
这一代码实现的效果也很简单，同样是直接写相应的内存去打印字符，同时利用`while(1)`实现了类似于之前`jmp $`的效果。
为什么是 0xb8000？ 这是一条硬连线。在 PC 的硬件架构中，显卡一直“监听”着内存地址 0xB8000 到 0xBFFFF 这一块区域。
- 你往内存里写一个字，显卡硬件就会立刻把它投射到显示器上。
- 这叫 MMIO (Memory Mapped I/O，内存映射输入输出)。你以为你在写内存，其实你在操作显卡。
真正复杂的部分发生在编译阶段，我们首先需要把C语言代码进行编译，生成目标文件:
```bash
gcc -m32 -fno-pie -ffreestanding -c kernel.c -o kernel.o
```
其中,`-m32`表示我们编译成32位的代码，`-fno-pie`表示禁用位置无关代码（位置无关的意思指的是，代码之间的关系表示为相对位置，例如某个位置的代码可能会调用“我后面0x200位置的代码”，而在内核中写这样的代码会带来额外的寻址负担，也会很麻烦），`-ffreestanding`用于禁用编译器自动添加头文件，这是因为我们正在编写的是操作系统内核的代码，此时无论我们的磁盘中是否存在标准库的相应实现，我们都是读取不到的，因此即便添加了相应的头文件，我们也无法进行标准库函数的调用。
编译完成后，我们链接生成相应的二进制文件：
```bash
ld -m elf_i386 -o kernel.bin -Ttext 0x7e00 --oformat binary kernel.o
```
在编译时，我们的编译器会保存各种符号（函数名称等），在链接时进行解析，标记它们的地址。首先，`-m elf_i386`表示将输入文件当做32位可执行文件进行处理，。`-o kernel.bin`指定了输出二进制位恩建的名称，<font color="red">`-Ttext 0x7e00`是一个关键的参数，它表示在这个可执行文件中，`.text(代码段)`的起始地址为`0x7e00`。</font>正是有了这段代码，我们才能保证在等会和`boot.bin`协作的过程中，能够让内核代码老老实实排在第二个扇区当中。最后，`-oformat`指定了输出文件的格式为原始二进制文件，不包含ELF格式下的各种文件头的内容。
最后，简单粗暴的用'cat'命令将两个二进制文件拼接起来：
```bash
cat boot.bin kernel.bin > os-image.bin
```
### 打印字符串
直接写内存进行显示的方式还是太过于简单粗暴了，我们希望设计一个显卡驱动程序，实现一个可以自动打印字符串的函数，于是我写出了这样的代码：
```C
#define VIDEO_MEMORY 0xb8000
#define MAX_COLS 80
#define MAX_ROWS 25

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
void main(){
    kprint_at("This is a XYTriste kernel", 0, 0);
    kprint_at("It works!!!", 1, 0);

    while(1);
}
```
这段代码没有任何编译上的问题，因此它通过了编译，但是在运行时，字符串不仅无法打印出来，QEMU窗口还一直处于一个无限刷新（类似崩溃了）的状态。经过多次尝试，我发现在`kprint_at`函数中，在循环前直接使用:
```C
*video_memory = 'A';
```
的方式仍然可以正常打印字符，这说明问题出现在了循环的部分，或者说出现在字符串的部分。我尝试把上面这条语句放在循环中，字符没有被打印出来，这说明循环没有被执行过，`message[0]`读取到的是空字符。
但是为什么会出现这样的问题呢？我们可以看到调用`kprint_at`的参数是一个常量字符串，为什么到了函数中却读不到字符串？
这里其实产生了两个错误：
1. 内存错位（main函数并没有被编译在`0x7e00`的位置）
2. 扇区读取错误

我们首先来解释第一个问题，从我们编写的代码中可以看到，在我们的内核中第一个出现并定义的方法其实是`kprint_at`，这就导致在编译链接的时候，`0x7e00`处保存的其实是`kprint_at`函数的代码，此时我们没有给这个函数设置任何参数，因此导致了栈的错乱，它想要尝试从寄存器中读取参数，却没有得到任何参数提供。解决方法当然就是调换`main`函数和`kprint_at`函数的顺序，首先声明`kprint_at`函数，但是不给出定义。从物理上改变两个函数的顺序，并始终保证main函数出现在整个内核代码的最前面，从而保证它在和`boot.bin`链接后物理位置处于`0x7e00`。
第二个问题就显得更加复杂一些了，在我们之前的`boot.asm`中，我们是这样读取扇区的：
```assembly
    mov ah, 0x02
    mov al, 1

    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [boot_drive]
    int 0x13

    jc disk_error
```
其中,`mov al, 1`表示我们要读取1个扇区的大小，需要注意的是，这里的读扇区实际上读的是我们的镜像文件，也就是我们的`os-image.bin`。它是由`boot.bin`以及`kernel.bin`合并而来。由于C语言代码进行汇编以后生成的内容相对臃肿，因此<font color="red">**拼接后的镜像文件实际上大小超过了2KB！！！**</font>这也就导致作为字符串常量的`"This is a XYTriste kernel"`，本来就保存在elf文件的数据段中，数据段落在了第二个扇区之后，因此上述读扇区的代码实际上没有读到这个字符串常量，自然也就无法循环进行正常打印了。
然而，问题并不仅仅只是有这2个，当我尝试把`mov al, 1`改成了`mov al, 15`时，我遇到了第3个错误，也就是镜像文件的大小太小，`mov al, 15`实际上读取了大约`7.2KB`大小的空间，此时我的`os-image.bin`只有2KB大小，没有足够的空间去读，而我们想要控制大小又比较麻烦。因此采用最粗暴的方法，在镜像文件的后面添加足够多的空扇区，确保有足够的扇区给CPU去读，首先生成空的镜像文件（20个扇区大小）：
```bash
dd if=/dev/zero of=zeros.bin bs=512 count=20
```
随后进行拼接（注意拼接顺序）：
```bash
cat boot.bin kernel.bin zeros.bin > os-image.bin
```
这样我们的引导扇区中的代码就可以正常读扇区了，以后如果我们新增了别的内核代码，可能还得继续增加读扇区的大小，确保能够把数据内容读到内存中供内核代码使用。

### 打印字符串（调整光标位置）
先前，我们实现了打印字符串的功能，其实无非也就是通过写内存（MMIO，内存映射IO端口）的方式将文字进行循环打印输出到显示端中。
但是这样做的问题在于，显示端的光标并不会随着文字的输出而移动,这是因为输出的内容（写入到显存）以及控制光标位置的VGA寄存器本质上不是一个东西，所以仅仅只是写显存的内容并不会移动光标的位置。
因此，我们更新了我们`kernel.c`的代码，实现了光标随输出内容移动的功能：
```C
// kernel.c

//显存配置
#define VIDEO_MEMORY 0xb8000
#define MAX_COLS 80
#define MAX_ROWS 25

// 端口配置（VGA控制端口）
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

void kprint_at(char *message, int row, int col);
void port_byte_out(unsigned short port, unsigned char data);
unsigned char port_byte_in(unsigned short port);

int get_cursor_offset();
void set_cursor_offset(int offset);

void print_char(char character, int col, int row, char attribute_byte);
void kprint(char *message);
void clean_screen();

void main(){
    clean_screen();
    kprint("This is a XYTriste kernel\n");
    kprint("It works!!!");

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
```
在这一次的更新中，我们实现了多个函数。其中包含两个内联汇编函数`port_byte_out`和`port_byte_in`，在这部分中最值得注意的是，在`gcc`编译器中默认我们内联汇编采用的是`AT&T`语法，这与我们直接编写汇编代码所遵循的`Intel`标准不同。在`Intel`标准下，一个完整的汇编语句构成是由`关键字 目标操作数, 源操作数`构成的，而`AT&T`语法则交换了目标操作数和源操作数的顺序。
此外，我们也需要知道，`out`和`in`不同于`mov`这样的汇编指令，当CPU遇到`out`和`in`这样的指令时，它们会把相应的内容当作端口号（内存地址）去处理，从而实现对于端口的读写。
随后，我们实现了屏幕驱动的相关函数，这里需要注意的就是如何计算光标位置并打印的的函数`print_char`。参数`col`和`row`指定了输出位置的行列（这里的行列指的是VGA标准中的对应行列，行列下标分别从`0-24`以及`0-79`），经过计算得到要输出的位置以及光标放置的位置。
最后，我们也实现了一个清除屏幕的函数，实际上就是在整个显示位置上打印空字符，并把光标挪到最前面，模拟清空后的效果。

### IDT（Interrupt description table，中断描述符表）的建立
现在，我们的内核可以正常的打印出字符串并处理换行。然而，我们的内核此时仍然是一个无法从外界接收信息的残疾人，因为在引导扇区中，我们使用了`cli`来关闭CPU处理中断的功能，更加具体的说。在CPU中有一个特殊的标志位寄存器，当我们调用`cli`时，这一寄存器的值将变成0。此时虽然CPU可以接收到中断请求，但是不会进行任何处理，直到中断处理恢复。
当进入保护模式以后，寻址方式就变成了`段选择子：偏移`，且寻址能力也从20位提升到了32位。由于`IVT`以4字节为单位，寻址方式和保护模式下有所不同。因此，进入保护模式以后`IVT`就不再可用了。
此时，如果我们需要进行相应的中断处理，就必须设置`IDT`。更加具体的说，在保护模式下我们调用中断时，CPU会检查`IDT`表，根据我们使用的中断号（索引），去相应的地址中寻找中断处理函数。当然，在访问地址之前，`GDT`会检查相应地址的可用性以及特权等级、地址所在段的类型等信息，确保不会产生地址的误用。
`IDT`中一个条目大小为8个字节，每个字节具体含义如下所示：
```C
typedef struct{
    u16 low_offset; // 中断处理函数的低16位地址
    u16 sel;        // 内核段选择子
    u8 always0;     // 保留字节

    // 标志位 byte: 
    // Bit 7: Present (1)
    // Bit 5-6: DPL (Privilege) (00)
    // Bit 0-4: Type (01110 = 32-bit Interrupt Gate)
    // 所以通常是 0x8E (1 00 0 1110)
    u8 flags;
    u16 high_offset; // 中断处理函数的高16位地址
} __attribute__((packed)) idt_gate_t;
```
> __attribute__((packed))的作用在于<font color="red">取消结构体或联合体成员之间的填充（padding），使其内存布局完全紧凑，按字节顺序排列。</font>这样做的目的在于，避免例如`char`和`int`这样不同字节大小的结构体在声明时被强制对齐到8字节。
其中，`sel`的含义表示相应的中断处理函数所在的段。在平坦模式下，整个内存空间被划分为一个代码段，因此此处的段选择子的值就是`0x08`。此外，`always`所在的第5字节为保留字节，这一字节的值必须为0。
类似于GDT，我们同样需要让CPU知道`IDT`表所在位置，大小（确保访问不会越界）等信息，因此就有了我们的`set_idt`函数：
```C
// 设置IDT表的地址
void set_idt(){
    idt_reg.base = (u32) &idt; // 设置IDT表的起始地址
    idt_reg.limit = sizeof(idt) - 1; // 设置IDT表的大小（按字节表示，确保不会数组越界。

    __asm__ volatile("lidt (%0)" : : "r"(&idt_reg));
}
```
注意这里的内联汇编语句，`lidt`其实指的就是加载`idt`表，相应的信息都保存在了`idt_reg`这一结构中，它是一个6字节的结构体：
```C
typedef struct{
    u16 limit;   // IDT表的条目限制数量（最多256，索引从0-255）
    u32 base;   // IDT表的起始地址
} __attribute__((packed)) idt_register_t;
```
在我们的实现中，我们使用了一个外部声明的函数`isr1()`，模拟中断处理函数在其他地址的情景：
```C
extern void isr1();
```
这个函数没有任何内容，它被定义在`interrupt.asm`文件中，内容为：
```assembly
global isr1

isr1:
    iret
```
为什么要用这么复杂的方式，而不是直接定义函数然后`return`？这是因为中断处理程序的返回命令是`iret`（需额外弹出标志寄存器）而不是`ret`，因此只能使用汇编的方式实现。
最后，我们在编译的时候不要忘记了把新建的汇编文件进行编译:
```bash
nasm -f elf32 ./interrupt.asm -o interrupt.o
```
链接的时候同样需要加上：
```bash
ld -m elf_i386 -o kernel.bin -Ttext 0x7e00 --oformat binary kernel.o interrupt.o
```
最后，我们在`main`函数中进行相应的设置并打印语句，只要看到最后的`intetrupt handled!`就说明内核成功处理了中断并返回了。
```C
void main(){
    clean_screen();

    kprint("Interrupt Start!");
    
    set_idt_gate(1, (u32) isr1);
    set_idt();

    __asm__ volatile("int $1");
    kprint("Interrupt handled!");
    
    while(1);
}
```
### 键盘中断的处理
现在，我们需要让我们的操作系统进一步的完善了，我们希望操作系统可以正确的处理我们的键盘输入。
要处理键盘输入，我们首先需要了解键盘输入的原理，当我们按下键盘：
1. 触发 IRQ1 (中断号 33)。

2. CPU 查 IDT -> 查 GDT -> 跳到 keyboard_handler。

3. 最关键的一步：代码需要去读取 0x60 端口（还记得之前的显卡端口吗？键盘也有端口）。

4. 从 0x60 读出来的就是按键的扫描码（Scan Code）。

5. 把扫描码转换成 ASCII 字符，打印在屏幕上。

那么，根据上述内容，我们首先需要一个预定义好的扫描码映射ASCII字符表：
```C
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
```
建立好了映射表之后，我们进行键盘中断的初始化，包括端口号的映射（这部分暂时不纠结，照抄AI代码）以及设置`IDT`等等：
```C
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
    kprint("Keyboard is init\n");
}
```
初始化完成以后，我们就可以编写相应的中断处理函数，从对应端口读入键盘输入并进行相应处理：
```C
void keyboard_handler_c(){
    u8 scancode;    // 保存键盘按键时的扫描码
    char ascii_char;    // 对应的字符

    scancode = port_byte_in(KEYBOARD_PORT);  // 从键盘读取一个字符
    
    // print_hex(scancode);

    if(scancode < 0x80){    // 键盘“按下”和“松开”是不同的扫描码，此时只处理按下逻辑
        ascii_char = keymap[scancode];

        // print_hex(scancode);
        print_hex((u8) ascii_char);

        char str[2] = {ascii_char, 0};  // 构建字符串用于打印
        kprint(str);
    }

    port_byte_out(0x20, 0x20);
    //发送EOI(End of Interrupt，中断结束信号)给主PIC
    //目的在于表示中断已处理完成
}
```
这里有一个重点就是最后的`port_byte_out(0x20, 0x20);`，这是因为：
> PIC 是一个有状态的硬件。当它发出一个中断后，它会把对应的 ISR (In-Service Register) 位拉高，标记为“正在服务中”。 PIC 会一直等待 CPU 发送一个 EOI (End of Interrupt) 信号（往端口 0x20 写 0x20）。 如果不发送 EOI，PIC 会认为：“CPU 还在忙着处理上一个中断呢，我不能打扰它。”。因此，在硬件中断的最后我们需要发送一个信号告诉PIC中断已经处理完成。

除此之外，不要忘记中断处理是需要相应汇编指令来返回的：
```assembly
interrupt.asm

[bits 32]
extern keyboard_handler_c
global isr_stub     ; 全局符号，确保能被编译器看到
global isr_keyboard

isr_stub:
    iret

isr_keyboard:
    pusha   ; 保存所有通用寄存器（push all）
    call keyboard_handler_c
    popa    ; 恢复所有通用寄存器 (pop all)

    iretd
```
这样，我们就完成了对于键盘输入的处理...了吗？
在这里我踩了一个非常隐蔽的坑，当我完成一系列的`nasm`、`gcc`、`ld`以及`cat`最后`qemu`后，我发现系统正常的运行，但是对我的键盘输入没有任何反应。
由于我不会（我猜可能也不行）在QEMU中调试内核代码，因此只能采用最原始的方式，在`kernel.c`中添加输出代码：
```C
void print_hex(u8 n) {  // unsigned short
    char *hex = "0123456789ABCDEF";
    char out[4]; // 2个数字 + 空格 + 结束符
    out[0] = hex[(n >> 4) & 0xF]; // 取高4位
    out[1] = hex[n & 0xF];        // 取低4位
    out[2] = ' ';                 // 加个空格方便看
    out[3] = 0;
    kprint(out);
}
```
然后，在键盘中断处理函数中添加这个函数，按下键盘上的'A'键以后，观察扫描码的输出情况:
$$
(Press Key)A = 1E 9E
$$
可以看到系统确实正确的调用了键盘中断处理函数，并输出了扫描码。但是为什么就是不打印输入字符呢？明明我写了这几句：
```C
ascii_char = keymap[scancode];
char str[2] = {ascii_char, 0}; 
kprint(str);
```
于是我又尝试直接打印映射后的ASCII字符，终于发现了问题：
```C
ascii_char = keymap[scancode];

print_hex((u8) ascii_char);

char str[2] = {ascii_char, 0};  // 构建字符串用于打印
kprint(str);
```
打印出来的全都是`00`！这对我的世界观冲击实在是太大了，明明我通过`ascii_char = keymap[scancode];`获取了字符，`scancode`的`1E`映射到`keymap`里的确是对应字符'A'，结果到了输出的时候就变成了`00`了。
那么问题来了，`scancode`的`1E`映射到`keymap`里的确是对应字符'A'...吗？
这牵扯到了一个极其隐蔽的坑：数据段加载问题。
简而言之，在`bin`文件中，内容主要分为代码段和数据段。其中，数据段又区分为"可读写数据`.data`"以及"只读数据`.rodata`"。
在我们之前的定义中，扫描码与ASCII字符的数组声明为：
```C
char keymap[128] = {...};
```
问题出现在链接器（`ld`）生成二进制文件的时候，为了实现内存对齐，可能会在"代码段"与"数据段"之间留下空白区域（填充0），完成4KB的内存对齐。
对照我们编译链接前后的文件，我们发现：
$$
kernel.o = 4KB (before ld)
kernel.o = 6KB (after ld)
$$
我们发现的确发生了内存对齐，这里根据AI的说法，我们了解到：
> 默认链接脚本会把各段 (.text、.rodata、.data、.bss …) 按 4 KB 页边界对齐。
假设 kernel.o 里真正有用的东西只有 3.1 KB，可它落在两个 4 KB 页里，于是 ld 在第一个段末尾塞 0.9 KB 的填充，第二个段再塞 2 KB 的填充，结果就 “膨胀” 到 6 KB 甚至 8 KB。
这也就意味着，我们的**代码段和数据段之间隔着一段距离!!!**
而在我的`boot.asm`中，读取的扇区数量为：
```assembly
mov ah, 0x02
mov al, 15
```
这实际读取的大小大约是`7.5KB`，而我最终的文件`os-image.bin`由`boot.bin kernel.bin zeros.bin`拼接而成，大小约为`16KB`。
这就说明`keymap`所在的数据段被放在了文件中靠后的位置，因此在读取的时候,`keymap`的值所在的数据段实际上并没有被读入到内存。在裸机中，未被加载到内存的区域上的部分读出来的数据是随机的，根据那块区域的电平决定。
暂时的解决方案就是修改`keymap`的定义，为它增加只读属性：
```C
const char keymap[128] = {...};
```
由于只读数据段`.rodata`通常紧跟在代码段`.text`之后，因此只要保证代码段 + 只读数据段的大小没有超过15个扇区，就能够保证被加载到内存中，继而能够正常使用。