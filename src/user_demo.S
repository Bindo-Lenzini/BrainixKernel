// BrainixKernel (BKOS) — user_demo.S
// Три user-программы:
//   user_echo    — читает stdin, эхо-выводит
//   user_print_u — печатает "U\n" в цикле
//   user_cat     — open("readme.txt") → read/write loop → close → exit
//   user_fault   — читает невалидный адрес (тест изоляции памяти)

// ============================================================
// user_echo
// ============================================================
.section .text.user_echo, "ax", %progbits
.global user_echo_start
.global user_echo_end
user_echo_start:
echo_loop:
    mov x0, #0
    adr x1, echo_buf
    mov x2, #32
    mov x8, #3
    svc #0
    cbz x0, echo_loop
    mov x2, x0
    mov x0, #1
    adr x1, echo_buf
    mov x8, #0
    svc #0
    b echo_loop
echo_buf:
    .skip 32
user_echo_end:

// ============================================================
// user_print_u
// ============================================================
.section .text.user_print_u, "ax", %progbits
.global user_print_u_start
.global user_print_u_end
user_print_u_start:
u_loop:
    mov x0, #1
    adr x1, u_msg
    mov x2, #2
    mov x8, #0
    svc #0
    movz x9, #0x1200
    movk x9, #0x007A, lsl #16
u_delay:
    subs x9, x9, #1
    b.ne u_delay
    b u_loop
u_msg:
    .ascii "U\n"
user_print_u_end:

// ============================================================
// user_cat
// ============================================================
.section .text.user_cat, "ax", %progbits
.global user_cat_start
.global user_cat_end
user_cat_start:
cat_open:
    adr x0, cat_fname
    mov x1, #0
    mov x8, #4
    svc #0
    cmp x0, #0
    b.lt cat_fail
    mov x19, x0
cat_loop:
    mov x0, x19
    adr x1, cat_buf
    mov x2, #64
    mov x8, #3
    svc #0
    cbz x0, cat_done
    mov x2, x0
    mov x0, #1
    adr x1, cat_buf
    mov x8, #0
    svc #0
    b cat_loop
cat_done:
    mov x0, x19
    mov x8, #5
    svc #0
    mov x0, #0
    mov x8, #1
    svc #0
cat_fail:
    mov x0, #1
    adr x1, cat_err
    mov x2, #17
    mov x8, #0
    svc #0
    mov x0, #1
    mov x8, #1
    svc #0
cat_fname:
    .asciz "readme.txt"
cat_err:
    .ascii "open failed\n"
cat_buf:
    .skip 64
user_cat_end:

// ============================================================
// user_fault — тест изоляции памяти.
//   Печатает "before\n", читает невалидный адрес (0x10000000
//   не замаплен в AS этой задачи), ожидает Data Abort.
//   Kernel должен убить задачу и продолжить работу.
// ============================================================
.section .text.user_fault, "ax", %progbits
.global user_fault_start
.global user_fault_end
user_fault_start:
    // write(1, "before\n", 7)
    mov x0, #1
    adr x1, fault_msg_before
    mov x2, #7
    mov x8, #0
    svc #0

    // Читаем адрес 0x10000000 — вне AS этой задачи.
    // Ожидаем Data Abort от EL0 → kernel убьёт эту задачу.
    movz x0, #0x1000, lsl #16       // x0 = 0x10000000
    ldr x1, [x0]                    // ← fault здесь

    // Сюда не должны попасть.
    mov x0, #1
    adr x1, fault_msg_after
    mov x2, #6
    mov x8, #0
    svc #0

    mov x0, #0
    mov x8, #1
    svc #0

fault_msg_before:
    .ascii "before\n"
fault_msg_after:
    .ascii "after\n"
user_fault_end: