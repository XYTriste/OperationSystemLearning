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
```asm
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
```asm
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
```asm
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
```asm
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
```asm
cmp al, 0
je .done
```
而寄存器是存在着若干个标志位的（有关标志位的内容后续了解），当我们使用：
```asm
or al, al
jz .done
```
此时，如果`al`的值为0，那么它的零标志位会被标记为`1`，此时可以通过`jz(jump if zero)`命令进行跳转。

### 读取并执行 Sector 2
现在我们的任务是读取磁盘中的第二个扇区，我们已知扇区1（扇区编号从1开始，扇区1指引导扇区）从地址`0x7C00`开始，大小为512字节，那么也就是到`0x7DFF`结束正好结束。扇区2的开始地址为`0x7E00`，大小同样为512字节。
由于我们的`times`命令实现了对于第一个扇区的填充，因此我们只要接着后面写代码，实际上在将其加载到内存中时，后面的代码就处于第二个扇区中。我们可以通过指定功能号(`0x02`)、读取数量(`1`)以及指定柱面、磁头、扇区号、驱动器号来指定要读取的扇区，最后调用`int 0x13`中断要求BIOS读取磁盘。完整代码如下：
```asm
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
```asm
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

