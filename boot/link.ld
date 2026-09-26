/* Link script for the Lamp kernel image.
 *
 * The image is loaded by QEMU's multiboot loader at 0x100000, so the link
 * address is fixed.  The multiboot header lives in its own section that is
 * placed first, which keeps it inside the first 8 KiB of the file as the
 * multiboot specification requires.
 *
 * Everything ends up in one RWX load segment; there is no other program
 * header, no interpreter and no dynamic linking.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .multiboot ALIGN(8) :
    {
        KEEP(*(.multiboot))
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
        *(.note .note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
