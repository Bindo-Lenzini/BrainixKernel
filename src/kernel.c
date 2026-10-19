// BrainixKernel (BKOS) — kernel.c
// Headless-версия: без графики. Консоль — UART (последовательный порт).
#include <stdint.h>
#include "protosw.h"
#include "physmem.h"
#include "heap.h"
#include "dtb.h"
#include "task.h"
#include "sched.h"
#include "syscall.h"
#include "initrd.h"
#include "ksh.h"
#include "blk.h"
#include "bkfs.h"
#include "virtio_net.h"
#include "net.h"
#include "arp.h"
#include "dhcp.h"
#include "sha256.h"
#include "icmp.h"
#include "mmu.h"
#include "elf.h"

extern volatile int g_in_syscall;

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void)    { __asm__ volatile("isb"    ::: "memory"); }

// ============================================================
// UART (PL011)
// ============================================================
#define UART0_BASE   (0x09000000ULL + KERNEL_VA_OFFSET)
#define UART0_DR     ((volatile uint32_t *)(UART0_BASE + 0x00))
#define UART0_FR     ((volatile uint32_t *)(UART0_BASE + 0x18))
#define UART0_IMSC   ((volatile uint32_t *)(UART0_BASE + 0x38))
#define UART0_ICR    ((volatile uint32_t *)(UART0_BASE + 0x44))

#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)
#define UART_RXIM    (1u << 4)
#define UART_RTIM    (1u << 6)
#define UART_RXIC    (1u << 4)
#define UART_RTIC    (1u << 6)
#define UART_IRQ     33

void uart_putc(char c) {
    while (*UART0_FR & UART_FR_TXFF) {}
    *UART0_DR = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_puthex(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) uart_putc(hex[(v >> (uint64_t)i) & 0xF]);
}

void uart_putu(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && i > 0);
    uart_puts(&buf[i]);
}

// Приёмный буфер UART (кольцевой). Читается из syscall'ов шелла.
#define RX_BUF_SIZE 256
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

static void rx_push(uint8_t b) {
    uint32_t next = (rx_head + 1) % RX_BUF_SIZE;
    if (next == rx_tail) return;      // буфер полон — байт теряется
    rx_buf[rx_head] = b;
    rx_head = next;
}

int uart_rx_pop(uint8_t *out) {
    if (rx_head == rx_tail) return 0;
    *out = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return 1;
}

static void uart_drain_rx(void) {
    while (!(*UART0_FR & UART_FR_RXFE)) {
        rx_push((uint8_t)(*UART0_DR & 0xFF));
    }
}

static void uart_init_rx(void) {
    *UART0_IMSC = UART_RXIM | UART_RTIM;
    dsb_sy();
}

void panic(const char *msg) {
    uart_puts("\n[panic] ");
    uart_puts(msg);
    uart_puts("\n");
    for (;;) __asm__ volatile("wfe");
}

// ============================================================
// GICv2 и таймер
// ============================================================
#define GICD_BASE   (0x08000000ULL + KERNEL_VA_OFFSET)
#define GICC_BASE   (0x08010000ULL + KERNEL_VA_OFFSET)
#define GICD_CTLR          ((volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n)  ((volatile uint32_t *)(GICD_BASE + 0x100 + 4*(n)))
#define GICC_CTLR          ((volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR           ((volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR           ((volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR          ((volatile uint32_t *)(GICC_BASE + 0x010))
#define GIC_ENABLE           1u
#define GIC_PMR_ALL          0xFFu
#define GIC_IAR_SPURIOUS_MIN 1020u

static void gic_init(void) {
    *GICD_CTLR = 0; dsb_sy();
    *GICD_ISENABLER(0) = (1u << 30);             // timer PPI (IRQ 30)
    *GICD_ISENABLER(1) = (1u << (UART_IRQ - 32));
    dsb_sy();
    *GICD_CTLR = GIC_ENABLE; dsb_sy();
    *GICC_PMR = GIC_PMR_ALL; dsb_sy();
    *GICC_CTLR = GIC_ENABLE; dsb_sy();
}

#define TIMER_IRQ        30
#define TIMER_HZ_DEFAULT 24000000ull
static uint64_t timer_hz = TIMER_HZ_DEFAULT;
static uint32_t tick_count = 0;

static void timer_set(uint64_t ticks_from_now) {
    uint64_t now;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(now + ticks_from_now));
    uint64_t ctl = 1;
    __asm__ volatile("msr cntp_ctl_el0, %x0" :: "r"(ctl));
    isb();
}

static void timer_init(void) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq != 0) timer_hz = freq;
    timer_set(timer_hz / 60);
}

// ============================================================
// Обработчик прерываний
// ============================================================
uint64_t irq_handler(uint64_t sp) {
    uint32_t iar = *GICC_IAR;
    uint32_t irq = iar & 0x3FF;
    if (irq >= GIC_IAR_SPURIOUS_MIN) return sp;

    int do_switch = 0;

    if (irq == UART_IRQ) {
        uart_drain_rx();
        *UART0_ICR = UART_RXIC | UART_RTIC;
        dsb_sy();
        sched_wake_blocked();
    }

    if (irq == TIMER_IRQ) {
        uart_drain_rx();
        timer_set(timer_hz / 60);
        tick_count++;
        net_poll();
        // Переключаем задачу только вне syscall (см. exception.c).
        if (!g_in_syscall) do_switch = 1;
    }

    *GICC_EOIR = iar;
    dsb_sy();
    if (do_switch) return sched_yield_from_frame(sp);
    return sp;
}

// ============================================================
// Задачи ядра
// ============================================================
static void yield_kernel(void) {
    __asm__ volatile("svc #0");
}

// Простейшая задача простоя: отдаёт процессор, пока не запущен шелл.
static void task_idle(void) {
    for (;;) yield_kernel();
}

// ============================================================
// Точка входа
// ============================================================
void kernel_main(uint64_t dtb) {
    uart_puts("\033[?2004l");
    uart_puts("BrainixKernel (BKOS) booted successfully! (headless)\n");

    uart_puts("[dtb] at "); uart_puthex(dtb); uart_puts("\n");
    dtb_set(PA_TO_KVA(dtb));
    ram_bank_t banks[DTB_MAX_RAM_BANKS];
    int nb = dtb_get_ram_banks(banks, DTB_MAX_RAM_BANKS);
    uart_puts("  RAM banks: "); uart_putu((uint64_t)nb); uart_puts("\n");

    uart_puts("[physmem] init\n");
    physmem_init();
    uart_puts("  total pages: "); uart_putu(physmem_total_pages());
    uart_puts("\n  free pages:  "); uart_putu(physmem_free_pages()); uart_puts("\n");

    uart_puts("[heap] init\n");
    heap_init();

    uart_puts("[initrd] init\n");
    int nfiles = initrd_init();
    uart_puts("  files in initrd: "); uart_putu((uint64_t)nfiles); uart_puts("\n");

    uart_puts("[ksh] init\n");
    ksh_init();

    uart_puts("[gic] init + timer\n");
    gic_init();
    timer_init();

    uart_puts("[uart] RX interrupt enabled\n");
    uart_init_rx();

    pci_ecam_t ecam;
    if (!dtb_get_pci_ecam(&ecam)) panic("no PCI ECAM");

    uart_puts("[blk] init\n");
    if (!blk_init(ecam.ecam_base)) uart_puts("  [blk] not found\n");
    else { uart_puts("  [blk] capacity = "); uart_putu(blk_capacity()); uart_puts(" sectors\n"); }

    uart_puts("[net] init\n");
    if (!vnet_init(ecam.ecam_base)) uart_puts("  [net] not found\n");
    else uart_puts("  [net] ready\n");

    domaininit();
    net_init();
    if (!dhcp_acquire()) uart_puts("  [net] DHCP failed, using fallback config\n");
    arp_init();

    uart_puts("[sha256] selftest\n");
    if (!sha256_selftest()) panic("sha256 selftest FAILED");

    net_print_mac("  [net] my MAC = ", net_get_mac().b);
    net_print_ip ("  [net] my IP  = ", net_get_ip());
    net_print_ip ("  [net] gateway = ", net_get_gateway());

    uart_puts("[bkfs] mount\n");
    if (bkfs_init()) {
        uart_puts("  [bkfs] mounted\n");
    } else {
        uart_puts("  [bkfs] not formatted, formatting...\n");
        if (bkfs_format() == BKFS_OK) uart_puts("  [bkfs] formatted\n");
        else                          uart_puts("  [bkfs] format failed\n");
    }

    uart_puts("[task] create idle + shell\n");
    task_t *idle = task_create("idle", task_idle);
    if (!idle) panic("idle task create failed");

    const initrd_file_t *elf_file = initrd_find("sh.elf");
    if (!elf_file) panic("sh.elf not found in initrd");

    task_t *shell = task_create_from_elf("U-shell", elf_file->data, elf_file->size);
    if (!shell) {
        uart_puts("  [elf] task_create_from_elf failed: ");
        uart_puts(elf_last_error());
        uart_puts("\n");
        panic("cannot start shell");
    }

    sched_add(idle);
    sched_add(shell);

    uart_puts("[sched] start\n");
    sched_start();
    panic("sched_start returned");
}
