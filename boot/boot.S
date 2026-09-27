/* PVH ELF note, and the switch from 32-bit protected mode into 64-bit long
 * mode, for the Lamp kernel.
 *
 * The C kernel (kmain) is freestanding: there is no crt0, so this file has to
 * clear .bss, get the CPU into long mode and build a stack.  Everything else
 * is done in C.
 *
 * QEMU finds the entry point in the ELF note below, sets up 32-bit protected
 * mode with paging off, and jumps to _start with a pointer to its boot
 * parameters in ebx.  The order of the next few steps matters:
 *
 *   - the GDT descriptors are loaded through a register, never as `lgdt
 *     some_label`.  That spelling assembles to an absolute address in 32-bit
 *     mode and a RIP-relative one in 64-bit mode, so the CPU would read the
 *     descriptor from a different place depending on when it ran.  A register
 *     operand means the same address in both modes.
 *
 *   - long mode is entered by one instruction.  CR0.PG does not just enable
 *     paging, it also sets LMA, and LMA is what makes the CPU decode 64-bit
 *     code.  So the far jump to 64-bit code has to be the instruction
 *     immediately after the CR0 write, with nothing in between to be fetched
 *     in the wrong mode.  That is also why the long-mode GDT is loaded before
 *     the CR0 write rather than after it: after it, every instruction fetch
 *     until the jump is in an awkward state.
 */

.set PVH_NOTE_PHYS32_ENTRY, 18
.set PVH_NOTE_MAGIC2,      0x12345678
.set KERNEL_LINK_ADDRESS,  0x100000

.section .note.Xen, "a", @note
.align 4
    .long 4                       /* name length, including the NUL */
    .long 16                      /* descriptor length */
    .long PVH_NOTE_PHYS32_ENTRY
    .asciz "Xen"
.align 4
    .long _start                  /* 32-bit entry point */
    .long KERNEL_LINK_ADDRESS     /* where the image is loaded */
    .long KERNEL_LINK_ADDRESS     /* kernel base */
    .long PVH_NOTE_MAGIC2         /* says the descriptor is this newer shape */

.section .text.boot, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    cld

    /* QEMU's PVH loader leaves the start-info pointer in %ebx.  Save it before
     * anything below uses %ebx as a scratch register. */
    movl %ebx, boot_params_phys

    /* A GDT is needed before anything can be addressed, and the data segments
     * have to be reloaded against it: what the firmware left in ds/es/ss is
     * not ours to rely on, and the next thing is a rep stosb. */
    movl $gdt32_ptr, %ebx
    lgdt (%ebx)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* Zero .bss.  Nothing in C may assume the boot ROM did it.  This is why
     * boot_params_phys cannot live there: it is cleared by this loop. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    /* CR4.PAE.  Besides widening the top-level entries to 8 bytes, this is
     * what makes the CR0.PG write below stick: with PAE clear, QEMU drops
     * the write and the far jump lands back in 32-bit mode. */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* PML4[0] = pdpt | 3, as a full 8-byte entry.  A 4-byte store would put
     * the low half of this entry's address into what is, under PAE, the
     * high half of the entry. */
    movl $pml4, %edi
    movl $pdpt, %eax
    orl  $0x03, %eax
    movl %eax, 0(%edi)
    movl $0, 4(%edi)

    /* PDPT[0] = pd | 3, likewise a full 8-byte entry. */
    movl $pdpt, %edi
    movl $pd, %eax
    orl  $0x03, %eax
    movl %eax, 0(%edi)
    movl $0, 4(%edi)

    /* Identity map the low 64 MiB with 2 MiB leaves.  The transition into
     * 64-bit mode touches linear addresses above the 8 MiB that four leaves
     * used to cover, so a 4-leaf map leaves a hole exactly there and the
     * far jump faults on it.  Entries 128+ are left alone: the 4 KiB frames
     * installed below live there. */
    movl $pd, %edi
    xorl %eax, %eax
    movl $32, %ecx
3:  movl %eax, %edx
    orl  $0x83, %edx
    movl %edx, 0(%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    decl %ecx
    jnz 3b

    /* The one instruction fetched after CR0.PG is still fetched in 32-bit
     * mode, so it is resolved by a *three*-level walk: CR3 -> PML4 -> PDPT
     * as if it were a page directory -> this one as a page table.  That
     * walk reads this directory at a different index than the four-level
     * one does, so the 1 MiB page holding the code can be described with
     * 4 KiB frames here without disturbing the 2 MiB leaf at index 0.
     *
     * The kernel is linked at 1 MiB, so linear 1-2 MiB is all it ever asks
     * for before the far jump.  If it ever grows past 2 MiB, this stops
     * covering the entry point and the far jump faults instead of running
     * 32-bit code, which is a loud failure rather than a quiet one. */
    movl $pd + 1024, %edi
    movl $0x100003, %eax
    movl $0x100, %ecx
2:  movl %eax, (%edi)
    addl $0x1000, %eax
    addl $4, %edi
    decl %ecx
    jnz 2b

    /* CR3 points at the PML4. */
    movl $pml4, %eax
    orl  $0x03, %eax
    movl %eax, %cr3

    /* EFER.LME, via MSR 0xC0000080.  This arms long mode; it does not enter
     * it.  Nothing observable changes until CR0.PG is set. */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $(1 << 8), %eax
    wrmsr

    /* The long-mode GDT, loaded while still unambiguously in 32-bit mode. */
    movl $gdt64_ptr, %ebx
    lgdt (%ebx)

    /* CR0.PG.  PE is already set.  This is the instruction that turns long
     * mode on, so the very next instruction is the jump into 64-bit code. */
    movl %cr0, %eax
    orl  $(1 << 31), %eax
    movl %eax, %cr0
    ljmp $0x08, $long_mode_entry

.section .text
.code64
.type long_mode_entry, @function
long_mode_entry:
    /* 64-bit data segments, then the 64-bit stack. */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movl $stack64_top, %esp
    xorq %rbp, %rbp

    /* Long mode.  Hand the boot parameters to C and go. */
    movl boot_params_phys, %edi
    call kmain

/* kmain is not expected to return.  If it does, stop cleanly. */
hang:
    cli
    hlt
    jmp hang

/* --- GDTs --------------------------------------------------------------- */
/* One flat model for both modes: descriptor 1 (selector 0x08) is code,
 * descriptor 2 (selector 0x10) is data.  They differ only in the long-mode
 * bit, so each mode gets its own table and the far jump above swaps them. */

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

/* --- the one piece of state that has to outlive .bss being cleared -------- */
/* It is an initialised word in .data rather than a slot in .bss, because the
 * .bss clearing loop runs after it is written and would wipe it. */
.section .data
.align 4
boot_params_phys:
    .long 0

/* --- scratch memory ----------------------------------------------------- */
.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096

.align 16
stack64:
    .skip 1048576
stack64_top:
