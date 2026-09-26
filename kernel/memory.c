/* Memory: read the multiboot memory map, pick a heap, hand out bump
 * allocations.
 *
 * There is no free list and there never will be one in this machine.  Nouns
 * are append-only, so the only thing the allocator has to get right is "don't
 * hand out the same bytes twice" and "don't hand out the kernel image".  A
 * bump pointer is the whole allocator.
 *
 * The boot loader hands us a multiboot 2 tag list.  Tag type 6 is the memory
 * map, and it is a sequence of 24-byte entries: base, length, type, and a
 * reserved word that must be zero.  Each entry is 8-byte aligned and every
 * tag is padded to a multiple of 8, so there is no struct layout to guess at
 * and no compiler-dependent padding to worry about.
 */

#include "kernel.h"

extern u8 __bss_start[];
extern u8 __bss_end[];
extern u8 __kernel_start[];

u64 kernel_start_phys;
u64 kernel_end_phys;

#define MB2_MAGIC        0xE85250D6u
#define MB2_TAG_END      0u
#define MB2_TAG_MMAP     6u
#define MB2_MEM_AVAILABLE 1u
#define MB2_TAG_HEADER   8u
#define MB2_MMAP_ENTRY   24u

static u64 heap_start;
static u64 heap_end;
static u64 heap_next;
static u64 largest_free;

u64 mem_heap_start(void) { return heap_start; }
u64 mem_heap_end(void)   { return heap_end; }
u64 mem_heap_used(void)  { return heap_next - heap_start; }
u64 mem_largest_free_region(void) { return largest_free; }

/* Read the multiboot structures byte by byte.  They are 8-byte aligned, but
 * this costs nothing and it means the kernel never depends on being able to
 * make an unaligned access. */
static u32 rd32(const u8 *p)
{
    return (u32)p[0]
         | ((u32)p[1] << 8)
         | ((u32)p[2] << 16)
         | ((u32)p[3] << 24);
}

static u64 rd64(const u8 *p)
{
    return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

static u64 align_up(u64 value, u64 align)
{
    return (value + align - 1) & ~(align - 1);
}

void mem_init(u64 mb_info_phys)
{
    kernel_start_phys = (u64)__kernel_start;
    kernel_end_phys   = (u64)__bss_end;

    heap_start   = 0;
    heap_end     = 0;
    heap_next    = 0;
    largest_free = 0;

    if (mb_info_phys == 0) {
        machine_crash("no multiboot information, so no memory map");
        return;
    }

    /* The identity map covers the low 2 GiB, so a physical address from the
     * boot loader can be dereferenced directly. */
    const u8 *info = (const u8 *)mb_info_phys;

    if (rd32(info) != MB2_MAGIC) {
        machine_crash("the multiboot magic number is wrong");
        return;
    }

    u32 header_length = rd32(info + 8);
    if (header_length < 16) {
        machine_crash("the multiboot header claims to be shorter than it is");
        return;
    }

    const u8 *tag     = info + 16;
    const u8 *tag_end = info + header_length;
    int found_map     = 0;

    while ((const u8 *)tag + MB2_TAG_HEADER <= tag_end) {
        u32 type = rd32(tag);
        u32 size = rd32(tag + 4);

        if (size < MB2_TAG_HEADER)
            break;

        if (type == MB2_TAG_MMAP) {
            found_map = 1;
            u32 count = (size - MB2_TAG_HEADER) / MB2_MMAP_ENTRY;
            const u8 *entry = tag + MB2_TAG_HEADER;

            for (u32 i = 0; i < count; i++) {
                u64 base   = rd64(entry);
                u64 length = rd64(entry + 8);
                u32 kind   = rd32(entry + 16);

                if (kind == MB2_MEM_AVAILABLE && length > largest_free) {
                    largest_free = length;
                    heap_start   = base;
                    heap_end     = base + length;
                }
                entry += MB2_MMAP_ENTRY;
            }
        }

        if (type == MB2_TAG_END)
            break;

        tag = (const u8 *)tag + ((size + 7u) & ~7u);
    }

    if (!found_map) {
        machine_crash("the boot loader gave no memory map");
        return;
    }
    if (largest_free == 0) {
        machine_crash("no available memory in the memory map");
        return;
    }

    /* The kernel image is loaded at 0x100000, which is inside the largest
     * available region, so the heap starts after the image instead. */
    if (heap_start < kernel_end_phys && heap_end > kernel_end_phys)
        heap_start = kernel_end_phys;

    heap_start = align_up(heap_start, 16);
    heap_end   = align_up(heap_end, 16);
    heap_next  = heap_start;
}

void *mem_alloc(u64 size, u64 align)
{
    if (machine_err)
        return NULL;
    if (align == 0 || (align & (align - 1)) != 0) {
        machine_crash("mem_alloc was given an alignment that is not a power of two");
        return NULL;
    }
    if (size == 0)
        size = 1;

    u64 base = align_up(heap_next, align);
    if (base < heap_next || base + size > heap_end) {
        machine_crash("out of heap memory");
        return NULL;
    }
    heap_next = base + size;
    return (void *)base;
}
