/* Multiboot 2 header and 32-bit -> 64-bit transition for the Lamp kernel.
 *
 * The C kernel (kmain) is freestanding: there is no crt0, so this file is
 * responsible for zeroing .bss, switching the CPU into long mode and building
 * a call stack.  Everything else is done in C.
 *
 * Multiboot 2 is used rather than version 1 because its memory map is a
 * self-describing list of fixed-size tags, with no struct layout to guess at.
 */

.set MB_MAGIC, 0xE85250D6
.set MB_ARCH,  0x00000000          /* i386 protected mode */
.set MB_LENGTH, 16                 /* this header is 16 bytes and has no tags */
.set MB_CHECKSUM, 0x17ADAF1A       /* -(MB_MAGIC + MB_ARCH + MB_LENGTH) */

.section .multiboot, "a"
.align 8
mb_header:
    .long MB_MAGIC
    .long MB_ARCH
    .long MB_LENGTH
    .long MB_CHECKSUM

.section .text.boot, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    cld

    /* A GDT is required as soon as we start building page tables. */
    lgdt gdt32_ptr

    /* 32-bit stack.  The multiboot info pointer is saved on it and popped
     * again after the mode switch, so it survives the transition. */
    movl $stack32_top, %esp
    pushl %ebx

    /* Zero .bss.  Nothing in C may rely on the boot ROM doing it. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    /* --- identity map the low 2 GiB with 2 MiB pages ------------------ */
    /* pml4[0] = pdpt | present | writable */
    movl $pml4, %edi
    movl $pdpt, %eax
    orl  $0x03, %eax
    movl %eax, (%edi)

    /* pdpt[0..1] = pd0, pd1  (pd0 covers 0-1 GiB, pd1 covers 1-2 GiB) */
    movl $pdpt, %edi
    movl $pd0, %eax
    orl  $0x03, %eax
    movl %eax, 0(%edi)
    addl $0x1000, %eax
    movl %eax, 4(%edi)

    /* pd0[j] = 0x83 for every j: physical 0 .. 1 GiB */
    movl $pd0, %edi
    movl $0x83, %eax
    movl $0x400, %ecx
    rep stosl

    /* pd1[j] = 0x80000083: bit 31 is physical bit 29, so this is 1..2 GiB */
    movl $pd1, %edi
    movl $0x80000083, %eax
    movl $0x400, %ecx
    rep stosl

    /* CR3 = pd0 | present | writable */
    movl $pd0, %eax
    orl  $0x03, %eax
    movl %eax, %cr3

    /* CR4.PAE */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* EFER.LME, via MSR 0xC0000080 */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $(1 << 8), %eax
    wrmsr

    /* CR0.PG | CR0.PE  -- this is the instruction that actually enters
     * long mode; the instructions after it are fetched as 64-bit code. */
    movl %cr0, %eax
    orl  $(1 << 31), %eax
    orl  $0x01, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_entry

.section .text
.code64
.type long_mode_entry, @function
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq $stack64_top, %rsp
    xorq %rbp, %rbp

    popq %rdi                     /* multiboot info pointer (32-bit value) */
    call kmain

/* kmain is not expected to return.  If it does, stop cleanly. */
hang:
    cli
    hlt
    jmp hang

/* --- GDTs ------------------------------------------------------------- */
/* One flat model for both modes.  Descriptor 1 (selector 0x08) is code,
 * descriptor 2 (selector 0x10) is data.  In 32-bit mode the long-mode code
 * descriptor still works: base 0, limit 4 GiB, 32-bit default operand size
 * for a byte granularity descriptor. */

.section .rodata
.align 8
gdt32:
    .quad 0x0000000000000000       /* null */
    .quad 0x00CF9A000000FFFF       /* 0x08: code, exec/read, 32-bit */
    .quad 0x00CF92000000FFFF       /* 0x10: data, read/write, 32-bit */
gdt32_end:
gdt32_ptr:
    .word gdt32_end - gdt32 - 1
    .long gdt32

.align 8
gdt64:
    .quad 0x0000000000000000       /* null */
    .quad 0x00AF9A000000FFFF       /* 0x08: code, exec/read, long */
    .quad 0x00CF92000000FFFF       /* 0x10: data, read/write, long */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

/* --- scratch memory --------------------------------------------------- */
.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd0:
    .skip 4096
pd1:
    .skip 4096

.align 16
stack32:
    .skip 16384
stack32_top:

.align 16
stack64:
    .skip 1048576
stack64_top:
