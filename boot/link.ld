/* Link script for the Lamp kernel image.
 *
 * The image is a 64-bit ELF loaded by QEMU's -kernel path, which finds the
 * 32-bit entry point in a PVH ELF note inside the first loadable segment (see
 * boot/boot.S).  The link address is fixed at 0x100000, which is well above
 * the low memory the firmware and the boot parameters occupy.
 *
 * Everything ends up in one RWX load segment; there is no other program
 * header, no interpreter and no dynamic linking.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    /* The PVH note has to be inside a loadable segment and near its start, so
     * it goes before .text rather than being discarded as a normal note. */
    .note.Xen ALIGN(4) :
    {
        KEEP(*(.note.Xen))
    }

    .text ALIGN(16) :
    {
        *(.text.boot)
        *(.text .text.*)
    }

    .rodata ALIGN(16) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(16) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(16) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(16);
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.gnu.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
