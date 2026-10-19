/* BrainixKernel (BKOS) — linker script
 *
 * Двухсекционная схема:
 *
 *   .boot   — VMA = LMA = 0x40080000. Здесь _start, ранний MMU setup
 *             на ассемблере, статические таблицы (1GB блоки), временный
 *             стек. Всё это исполняется ДО включения MMU, по PC = LMA.
 *
 *   остальное (.text/.rodata/.data/.bss/.stack) — VMA = high (0xFFFF...),
 *             LMA = сразу за .boot в физической памяти.
 *
 * После включения MMU (в _start.s) PC прыгает на high VA. C-код с
 * -fno-pic использует абсолютные VMA-адреса — они уже корректны,
 * потому что MMU включена.
 *
 * Трюк: TTBR0 и TTBR1 указывают на одну таблицу; low VA и high VA
 * резолвятся в одну и ту же физическую страницу.
 */

ENTRY(_start)

KERNEL_VA_OFFSET = 0xFFFF000000000000;

SECTIONS
{
    /* ===================== .boot ===================== */
    . = 0x40080000;

    .boot : {
        __boot_start = .;
        KEEP(*(.boot.head))     /* _start — самая первая инструкция */
        *(.boot.text)
        *(.boot.rodata)
        *(.boot.data)
        . = ALIGN(4096);
        *(.boot.bss)
        . = ALIGN(4096);
        __boot_end = .;
    }

    /* ===================== main kernel ===================== */
    . = __boot_end + KERNEL_VA_OFFSET;

    __kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_VA_OFFSET) ALIGN(4096) {
        __text_start = .;
        *(.text*)
        __text_end = .;
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VA_OFFSET) ALIGN(4096) {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    }

    .data : AT(ADDR(.data) - KERNEL_VA_OFFSET) ALIGN(4096) {
        __data_start = .;
        *(.data*)
        __data_end = .;
    }

    . = ALIGN(4096);
    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(4096);
    __bss_end = .;

    . = ALIGN(4096);
    .stack (NOLOAD) : {
        __stack_guard_start = .;
        . += 4096;
        __stack_guard_end = .;
        __stack_bottom = .;
        . += 4096 * 16;
        __stack_top = .;
    }

    __kernel_end = .;
    _end = .;

    /* Экспортируем KERNEL_VA_OFFSET как absolute symbol — для C-кода. */
    __kernel_offset = KERNEL_VA_OFFSET;
}
