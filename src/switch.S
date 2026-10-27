// BrainixKernel — switch.S
// context_switch(task_t *prev, task_t *next)
//
// Сохраняет callee-saved регистры (x19..x30, sp) в prev и
// восстанавливает их из next. Если prev == NULL — только загрузка.
//
// Layout task_t (см. task.h):
//   offset 0   : x19
//   offset 8   : x20
//   ...
//   offset 72  : x27
//   offset 80  : x28
//   offset 88  : fp (x29)
//   offset 96  : lr (x30)
//   offset 104 : sp

.section .text
.global context_switch
context_switch:
    cbz x0, 1f

    // Сохранить callee-saved в prev (x0)
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2,       [x0, #96]

1:
    // Восстановить из next (x1)
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x2,       [x1, #96]
    mov sp, x2

    ret