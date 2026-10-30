// BrainixKernel — uaccess.S
// Ассемблерные хелперы для безопасного копирования kernel<->user.

.section .text

.global copy_from_user
.global copy_to_user
.global __uaccess_recovery_elr
.global __uaccess_active
.global __uaccess_start
.global __uaccess_end

// int copy_from_user(void *dst, const void *user_src, uint64_t len)
copy_from_user:
    cbz x2, .Lcff_empty
    movz x11, #1, lsl #16
    cmp x1, x11
    b.lo .Lcff_bad_range
    movz x11, #0x8000, lsl #32
    adds x12, x1, x2
    b.cs .Lcff_bad_range
    cmp x12, x11
    b.hi .Lcff_bad_range

    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    mov x19, x0
    mov x20, x1
    mov x21, x2
    bl mmu_pan_off

    adr x9, .Lcff_recovery
    adr x10, __uaccess_recovery_elr
    str x9, [x10]

    adr x10, __uaccess_start
    str x20, [x10]
    add x9, x20, x21
    adr x10, __uaccess_end
    str x9, [x10]

    adr x10, __uaccess_active
    mov x9, #1
    str x9, [x10]

    mov x9, #0
.Lcff_loop:
    cmp x9, x21
    b.hs .Lcff_done
    ldrb w10, [x20, x9]
    strb w10, [x19, x9]
    add x9, x9, #1
    b .Lcff_loop

.Lcff_done:
    bl mmu_pan_on
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #0
    b .Lcff_epilogue

.Lcff_recovery:
    bl mmu_pan_on
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

.Lcff_empty:
    mov x0, #0
    ret

.Lcff_bad_range:
    mov x0, #-1
    ret

// int copy_to_user(void *user_dst, const void *src, uint64_t len)
copy_to_user:
    cbz x2, .Lctu_empty
    movz x11, #1, lsl #16
    cmp x0, x11
    b.lo .Lctu_bad_range
    movz x11, #0x8000, lsl #32
    adds x12, x0, x2
    b.cs .Lctu_bad_range
    cmp x12, x11
    b.hi .Lctu_bad_range

    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!

    mov x19, x0
    mov x20, x1
    mov x21, x2
    bl mmu_pan_off

    adr x9, .Lctu_recovery
    adr x10, __uaccess_recovery_elr
    str x9, [x10]

    adr x10, __uaccess_start
    str x19, [x10]
    add x9, x19, x21
    adr x10, __uaccess_end
    str x9, [x10]

    adr x10, __uaccess_active
    mov x9, #1
    str x9, [x10]

    mov x9, #0
.Lctu_loop:
    cmp x9, x21
    b.hs .Lctu_done
    ldrb w10, [x20, x9]
    strb w10, [x19, x9]
    add x9, x9, #1
    b .Lctu_loop

.Lctu_done:
    bl mmu_pan_on
    adr x10, __uaccess_active
    str xzr, [x10]
    mov x0, #0
    b .Lctu_epilogue

.Lctu_recovery:
    bl mmu_pan_on
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

.Lctu_empty:
    mov x0, #0
    ret

.Lctu_bad_range:
    mov x0, #-1
    ret

.section .data
.align 3
.global __uaccess_recovery_elr
__uaccess_recovery_elr:
    .quad 0

.global __uaccess_active
__uaccess_active:
    .quad 0

.global __uaccess_start
__uaccess_start:
    .quad 0

.global __uaccess_end
__uaccess_end:
    .quad 0
