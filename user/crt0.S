// BrainixKernel — user/crt0.S
// Точка входа. Kernel устанавливает:
//   x0 = argc
//   x1 = argv
//   SP_EL0 = указатель на верх стека
// Передаём x0, x1 в main как есть.

.section .text
.global _start

_start:
    mov x29, #0
    mov x30, #0
    // x0, x1 уже установлены kernel'ом (argc, argv).
    bl  main
    // main вернул результат в x0.
    mov x8, #1          // SYS_EXIT
    svc #0
1:  b 1b
