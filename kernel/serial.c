/* Polled 16550 UART driver.
 *
 * The only hardware this machine owns.  The port numbers are the PC legacy
 * ones: QEMU wires the same 0x3F8 UART into `isa-serial` on the `pc` machine.
 * No interrupts, no FIFO juggling, no baud rate negotiation beyond "make it
 * 115200 8N1" -- a character at a time, polled.
 *
 * Both directions are here because Step 2 needs them: this is the machine's
 * entire user interface, in and out, for as long as it lives.
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

    /* The receive and transmit FIFOs are left switched off, and this is a
     * measured choice rather than a tidy one.
     *
     * QEMU has already buffered the first byte of a session by the time the
     * kernel gets here -- a pipe delivers every byte at once -- and QEMU
     * discards whatever it is holding whenever the FIFO is enabled, no matter
     * which clear bits are set.  Enabling it therefore costs the first byte of
     * every session, whether the machine is being typed at or fed a script.
     * Measured on QEMU 11.1: 0xC1, 0xC7, 0x01 and 0x00 were tried, and only
     * 0x00 kept the first byte.
     *
     * Nothing is lost by it.  A machine that reads one byte at a time has no
     * use for a 16-byte FIFO; the cost is that QEMU can only offer one byte
     * per read, which at 115200 baud is not a limit anything here can feel. */
    outb(COM1 + REG_FIFO_CTRL,  0x00);  /* no interrupts from the FIFO either */

    outb(COM1 + REG_MODEM_CTRL, 0x03);  /* DTR | RTS */
}

void serial_putc(char c)
{
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_THR_EMPTY) == 0)
        ;
    outb(COM1 + REG_DATA, (u8)c);
}

/* One byte in, blocking until the UART has one.  This is the only place the
 * machine ever waits for anything, and it is why there are no interrupts and
 * no scheduler: there is exactly one thing to wait for. */
u8 serial_getc(void)
{
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_DATA_READY) == 0)
        ;
    return inb(COM1 + REG_DATA);
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
