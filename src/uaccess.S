// BrainixKernel (BKOS) — uaccess.S
// Ассемблерные хелперы для безопасного копирования kernel<->user.

.section .text

.global copy_from_user
.global copy_to_user
.global __uaccess_recovery_elr
.global __uaccess_active

// int copy_from_user(void *dst, const void *user_src, uint64_t len)
copy_from_user:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    adr x9, .Lcff_recovery
    adr x10, __uaccess_recovery_elr
    str x9, [x10]

    adr x10, __uaccess_active
    mov x9, #1
    str x9, [x10]

    mov x9, #0
.Lcff_loop:
    cmp x9, x2
    b.hs .Lcff_done
    ldrb w10, [x1, x9]
    strb w10, [x0, x9]
    add x9, x9, #1
    b .Lcff_loop

.Lcff_done:
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #0
    b .Lcff_epilogue

.Lcff_recovery:
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #-1

.Lcff_epilogue:
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

// int copy_to_user(void *user_dst, const void *src, uint64_t len)
copy_to_user:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    adr x9, .Lctu_recovery
    adr x10, __uaccess_recovery_elr
    str x9, [x10]

    adr x10, __uaccess_active
    mov x9, #1
    str x9, [x10]

    mov x9, #0
.Lctu_loop:
    cmp x9, x2
    b.hs .Lctu_done
    ldrb w10, [x1, x9]
    strb w10, [x0, x9]
    add x9, x9, #1
    b .Lctu_loop

.Lctu_done:
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #0
    b .Lctu_epilogue

.Lctu_recovery:
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #-1

.Lctu_epilogue:
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

.section .data
.align 3
.global __uaccess_recovery_elr
__uaccess_recovery_elr:
    .quad 0

.global __uaccess_active
__uaccess_active:
    .quad 0
