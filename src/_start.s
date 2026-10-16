// BrainixKernel (BKOS) — boot stub + diagnostics.
//
// Печатает цифры в UART0 по мере продвижения:
//   1 — _start
//   2 — EL2 detected (drop на EL1)
//   3 — EL1 entry
//   4 — SP set (boot stack)
//   5 — MMU on (всё ещё low VA)
//   6 — high_entry (PC = high VA)
//   S — SP переключён на high VA стек
//   7 — BSS cleared
//   8 — перед mmu_init
//   9 — после mmu_init
//   X — сразу после mmu_init (маркер)
//   Y — до ldr x1, =kernel_main
//   Z — перед br x1
//
// ВАЖНО: до mmu_init TTBR0 = boot_l0 (identity map), поэтому PUTC
// пишет в UART по low VA 0x09000000. После mmu_init TTBR0 = l0_empty
// (пустая таблица для user-AS), поэтому low VA недоступен везде,
// включая старый SP=boot_stack_top. Поэтому SP переключаем на high
// VA __stack_top сразу в high_entry.
//
// ВАЖНО #2: PUTCH клобберит x1 и x2. Не размещайте между
//   ldr x1, =addr
//   br  x1
// без перезагрузки x1 после последнего PUTCH.

.section .boot.head, "ax"
.global _start
.global exception_vectors
.global jump_to_frame
.extern kernel_main
.extern exception_sync_handler
.extern irq_handler
.extern mmu_init

// UART через low VA. Работает ТОЛЬКО пока TTBR0 = boot_l0.
.macro PUTC ch
    movz x1, #0x0900, lsl #16
    mov  w2, #\ch
    str  w2, [x1]
.endm

// UART через high VA (TTBR1). Клобберит x1 и x2.
.macro PUTCH ch
    movz x1, #0x0900, lsl #16
    movk x1, #0xFFFF, lsl #48
    mov  w2, #\ch
    str  w2, [x1]
.endm

.macro VEC target
    b   \target
    .balign 0x80
.endm

.macro SAVE_FULL_FRAME
    sub sp, sp, #0x120
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    str x30,      [sp, #0xF0]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1,   [sp, #0x100]
    mrs x0, sp_el0
    str x0,       [sp, #0x110]
.endm

.macro RESTORE_FULL_FRAME
    ldr x0,       [sp, #0x110]
    msr sp_el0, x0
    ldp x0, x1,   [sp, #0x100]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldr x30,      [sp, #0xF0]
    ldp x28, x29, [sp, #0xE0]
    ldp x26, x27, [sp, #0xD0]
    ldp x24, x25, [sp, #0xC0]
    ldp x22, x23, [sp, #0xB0]
    ldp x20, x21, [sp, #0xA0]
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    add sp, sp, #0x120
.endm

// ============================================================
_start:
    PUTC 49                     // '1'
    mov  x19, x0                // save DTB PA

    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    b.lt el1_boot

    // EL2 → EL1 drop.
    PUTC 50                     // '2'
    mov x0, #0x80000000
    msr hcr_el2, x0
    mov x0, #3
    msr cnthctl_el2, x0
    mov x0, #0x3C5
    msr spsr_el2, x0
    adr x0, el1_boot
    msr elr_el2, x0
    isb
    eret

el1_boot:
    PUTC 51                     // '3'
    msr daifset, #0xF

    // Временный стек в .boot (low VA). Используется только до
    // момента входа в high_entry.
    adr x0, boot_stack_top
    mov sp, x0
    PUTC 52                     // '4'

    bl  boot_mmu_setup
    PUTC 53                     // '5'

    ldr x0, =high_entry
    br  x0

// ============================================================
// boot_mmu_setup — минимальная MMU: 1GB блоки, TTBR0 = TTBR1.
// ============================================================
boot_mmu_setup:
    movz x0, #0xFF
    msr mair_el1, x0

    ldr x0, =0x2B5103510
    msr tcr_el1, x0

    adr x0, boot_l0
    msr ttbr0_el1, x0
    msr ttbr1_el1, x0

    dsb sy
    isb

    mrs x0, sctlr_el1
    orr x0, x0, #1
    orr x0, x0, #(1 << 2)
    orr x0, x0, #(1 << 12)
    msr sctlr_el1, x0
    isb
    ret

// ============================================================
// Boot BSS: временный стек. До high_entry используется для
// bl boot_mmu_setup (C-вызовов нет, но BL сохраняет x30 в LR,
// не в стек — так что стек тут только на всякий случай).
// ============================================================
.section .boot.bss, "aw", @nobits
.align 12
boot_stack_bottom:
    .skip 4096
boot_stack_top:

// ============================================================
// Boot data: page tables.
// ============================================================
.section .boot.data, "aw"
.align 12
.global boot_l0
boot_l0:
    .quad boot_l1 + 3
    .rept 511
    .quad 0
    .endr

.align 12
.global boot_l1
boot_l1:
    // [0, 1GB): Device nGnRnE, PA=0, PXN=1, UXN=1, AttrIdx=1
    .quad 0x0060000000000405
    // [1GB, 2GB): Normal WB, PA=0x40000000, PXN=0, UXN=1, SH=Inner
    .quad 0x0040000040000701
    // [2GB, 3GB): Normal WB, PA=0x80000000, PXN=1, UXN=1, SH=Inner
    .quad 0x0060000080000701
    // [3GB, 4GB): Normal WB, PA=0xC0000000, PXN=1, UXN=1, SH=Inner
    .quad 0x00600000C0000701
    .rept 508
    .quad 0
    .endr

// ============================================================
// Main kernel: .text (VMA = high).
// ============================================================
.section .text, "ax"

high_entry:
    PUTC 54                     // '6' — PC на high VA, SP ещё low!

    // *** КРИТИЧНО ***
    // Переключаем SP на high VA стек из .stack (linker.ld).
    // До mmu_init этот стек в TTBR1 по адресу __stack_top, и
    // до mmu_init он нам не нужен. После mmu_init TTBR0 = l0_empty,
    // и старый low VA SP становится недоступным — первое же
    // сохранение в стек (stp в прологе kernel_main) даст
    // Data Abort.
    ldr x0, =__stack_top
    mov sp, x0
    PUTC 83                     // 'S' — SP переключён на high VA

    ldr x0, =exception_vectors
    msr vbar_el1, x0
    isb

    // Обнуляем .bss (high VA).
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    mov x2, #0
1:  cmp x0, x1
    b.hs 2f
    str x2, [x0], #8
    b 1b
2:
    PUTC 55                     // '7' — BSS cleared

    msr daifclr, #0xF

    PUTC 56                     // '8' — перед mmu_init

    bl  mmu_init
    // После этой точки TTBR0 = l0_empty. Low VA MMIO недоступен.
    // SP уже high VA — все push/pop работают через TTBR1.

    PUTCH 57                    // '9' — после mmu_init
    PUTCH 10                    // '\n' — разделитель

    PUTCH 88                    // 'X'
    mov x0, x19
    PUTCH 89                    // 'Y'
    ldr x1, =kernel_main
    PUTCH 90                    // 'Z'
    ldr x1, =kernel_main        // перезагрузка x1 (PUTCH клобберит x1)
    br  x1

hang:
    dsb sy
    wfe
    b hang

.p2align 11
exception_vectors:
    VEC vec_sync
    VEC vec_irq
    VEC vec_fiq
    VEC vec_serror
    VEC vec_sync
    VEC vec_irq
    VEC vec_fiq
    VEC vec_serror
    VEC vec_sync
    VEC vec_irq
    VEC vec_fiq
    VEC vec_serror
    VEC vec_sync
    VEC vec_irq
    VEC vec_fiq
    VEC vec_serror

vec_sync:
    SAVE_FULL_FRAME
    mov x0, sp
    bl exception_sync_handler
    mov sp, x0
    RESTORE_FULL_FRAME
    eret

vec_irq:
    SAVE_FULL_FRAME
    mov x0, sp
    bl irq_handler
    mov sp, x0
    RESTORE_FULL_FRAME
    eret

vec_fiq:
    dsb sy
    wfe
    b vec_fiq
vec_serror:
    dsb sy
    wfe
    b vec_serror

jump_to_frame:
    mov sp, x0
    RESTORE_FULL_FRAME
    eret