/* Polled 16550 UART driver.
 *
 * The only hardware this machine owns.  The port numbers are the PC legacy
 * ones: QEMU wires the same 0x3F8 UART into `isa-serial` on the `pc` machine.
 * No interrupts, no FIFO juggling, no baud rate negotiation beyond "make it
 * 115200 8N1" -- a character at a time, polled.
 */

#include "kernel.h"

#define COM1 0x3F8

#define REG_DATA         0   /* DLAB=0: r/w data            */
#define REG_INT_ENABLE   1   /* DLAB=0: interrupt enable   */
#define REG_DIVISOR_LO   0   /* DLAB=1: divisor low byte   */
#define REG_DIVISOR_HI   1   /* DLAB=1: divisor high byte  */
#define REG_FIFO_CTRL    2   /* DLAB=0: fifo control       */
#define REG_LINE_CTRL    3   /* DLAB=0: line control       */
#define REG_MODEM_CTRL   4   /* DLAB=0: modem control      */
#define REG_LINE_STATUS  5   /* DLAB=0: line status        */

#define LSR_DATA_READY   0x01
#define LSR_THR_EMPTY     0x20

/* 115200 baud with a 1.8432 MHz clock is divisor 16. */
#define DIVISOR 16

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    outb(COM1 + REG_INT_ENABLE, 0x00);  /* no interrupts, ever */
    outb(COM1 + REG_LINE_CTRL,   0x80);  /* DLAB on, to set the divisor */
    outb(COM1 + REG_DIVISOR_LO, DIVISOR & 0xFF);
    outb(COM1 + REG_DIVISOR_HI, (DIVISOR >> 8) & 0xFF);
    outb(COM1 + REG_LINE_CTRL,   0x03);  /* DLAB off, 8 data bits, no parity, 1 stop */
    outb(COM1 + REG_FIFO_CTRL,  0xC7);  /* enable + clear FIFOs, 14-byte trigger */
    outb(COM1 + REG_MODEM_CTRL, 0x03);  /* DTR | RTS */
}

void serial_putc(char c)
{
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_THR_EMPTY) == 0)
        ;
    outb(COM1 + REG_DATA, (u8)c);
}

static void serial_put_raw(const char *s, u64 len)
{
    for (u64 i = 0; i < len; i++)
        serial_putc(s[i]);
}

void serial_puts(const char *s)
{
    if (s == NULL)
        return;
    u64 len = 0;
    while (s[len] != '\0')
        len++;
    serial_put_raw(s, len);
}

void serial_put_dec(u64 v)
{
    char buf[24];
    int  n = 0;

    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        serial_putc(buf[--n]);
}

void serial_put_hex(u64 v)
{
    static const char digits[] = "0123456789ABCDEF";

    serial_puts("0x");
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((v >> shift) & 0xF);
        if (nibble == 0 && !started && shift != 0)
            continue;
        started = 1;
        serial_putc(digits[nibble]);
    }
}

void serial_put_nl(void)
{
    serial_putc('\r');
    serial_putc('\n');
}
