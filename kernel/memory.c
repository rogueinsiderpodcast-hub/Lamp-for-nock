/* Memory: read the boot loader's memory map, pick a heap, hand out bump
 * allocations.
 *
 * There is no free list and there never will be one in this machine.  Nouns
 * are append-only, so the only thing the allocator has to get right is "don't
 * hand out the same bytes twice" and "don't hand out the kernel image".  A
 * bump pointer is the whole allocator.
 *
 * The boot loader is QEMU's PVH loader.  It enters the kernel in 32-bit
 * protected mode with a physical address in %ebx, pointing at a start-info
 * structure.  That structure carries a physical address and an entry count for
 * a memory map: a list of fixed-size entries, each giving a base, a size and a
 * type.
 *
 * Unlike the older multiboot 1 map, the PVH map has no reading to be guessed
 * at.  Every entry is exactly 24 bytes, the base and size are 64-bit, and the
 * count is given separately rather than encoded in the data.  So there is one
 * layout, not two to be told apart, and this file has a single straight path
 * through the entries.
 *
 * The structures are read field by field at fixed offsets rather than by
 * casting to a C struct.  The layout happens to be padding-free, so a struct
 * would come out the same, but offsets say what they mean and cannot drift if
 * the compiler or the word size changes.
 */

#include "kernel.h"

extern u8 __bss_start[];
extern u8 __bss_end[];
extern u8 __kernel_start[];

u64 kernel_start_phys;
u64 kernel_end_phys;

#define PVH_MAGIC      0x336ec578u

/* Memory-map entry types.  Only RAM is ours to hand out. */
#define PVH_MEM_UNUSED 0u
#define PVH_MEM_RAM    1u

/* Field offsets within the start-info structure. */
#define PVH_OFF_MAGIC           0
#define PVH_OFF_MEMMAP_PADDR   40
#define PVH_OFF_MEMMAP_ENTRIES 48

/* Field offsets within one memory-map entry, and the size of the whole entry. */
#define PVH_MAP_OFF_ADDR  0
#define PVH_MAP_OFF_SIZE  8
#define PVH_MAP_OFF_TYPE 16
#define PVH_MAP_SIZE     24

/* No real e820 table comes close to this.  It exists so a nonsense count cannot
 * walk us off the end of the map. */
#define PVH_MAX_ENTRIES 128

static u64 heap_start;
static u64 heap_end;
static u64 heap_next;
static u64 largest_free;


/* Number of map entries the parser could not make sense of.  Should be zero;
 * a non-zero count is printed so a bad layout cannot pass unnoticed. */
static u64 map_unreadable;

u64 mem_heap_start(void) { return heap_start; }
u64 mem_heap_end(void)   { return heap_end; }
u64 mem_heap_used(void)  { return heap_next - heap_start; }
u64 mem_largest_free_region(void) { return largest_free; }
u64 mem_map_unreadable(void) { return map_unreadable; }

/* Does the boot command line contain this word?  A substring search, because
 * that is the whole of the grammar: the command line is a bag of flags and
 * there is exactly one we care about.  A missing command line, or a needle
 * longer than the command line, is simply a no rather than an error: "not
 * asked for" and "asked for and absent" want the same answer here. */

/* Read the boot loader's structures byte by byte.  They are normally aligned,
 * but this costs nothing and it means the kernel never depends on being able
 * to make an unaligned access. */
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

/* Is this plausible as a memory map entry?
 *
 * The type is a small number, the length is not zero, and the region does not
 * wrap round the end of the address space.  An entry that fails is counted
 * rather than believed, so a map read wrongly cannot quietly hand back a heap
 * that overlaps the kernel or runs off the end of RAM. */
static int entry_plausible(u64 base, u64 length, u32 type)
{
    if (type > 5)
        return 0;
    if (length == 0)
        return 0;
    if (base + length < base)
        return 0;
    if (base + length > 0x1000000000000ULL)   /* 2^48: no real machine is bigger */
        return 0;
    return 1;
}

/* Read one fixed-layout map entry.  There is only one layout to read, so this
 * fills in what it was asked for and returns 0 if the entry is not believable. */
static int read_entry(const u8 *p, u64 *base, u64 *length, u32 *type)
{
    *base   = rd64(p + PVH_MAP_OFF_ADDR);
    *length = rd64(p + PVH_MAP_OFF_SIZE);
    *type   = rd32(p + PVH_MAP_OFF_TYPE);

    return entry_plausible(*base, *length, *type);
}

void mem_init(u64 boot_params_phys)
{
    kernel_start_phys = (u64)__kernel_start;
    kernel_end_phys   = (u64)__bss_end;

    heap_start   = 0;
    heap_end     = 0;
    heap_next    = 0;
    largest_free = 0;
    map_unreadable = 0;

    if (boot_params_phys == 0) {
        machine_crash("no boot loader information, so no memory map");
        return;
    }

    /* The identity map covers the low 64 MiB, and the boot loader places its
     * structures below 1 MiB, so a physical address it hands over can be
     * dereferenced directly. */
    const u8 *si = (const u8 *)(u64)boot_params_phys;

    if (rd32(si + PVH_OFF_MAGIC) != PVH_MAGIC) {
        machine_crash("boot parameters are not a PVH start-info structure");
        return;
    }

    u64 map_paddr = rd64(si + PVH_OFF_MEMMAP_PADDR);
    u32 entries   = rd32(si + PVH_OFF_MEMMAP_ENTRIES);

    if (map_paddr == 0 || entries == 0) {
        machine_crash("the boot loader gave no memory map");
        return;
    }

    if (entries > PVH_MAX_ENTRIES) {
        /* More entries than any e820 table holds.  Walk the sane part and say
         * so, rather than trusting a count that cannot be right. */
        map_unreadable = entries - PVH_MAX_ENTRIES;
        entries = PVH_MAX_ENTRIES;
    }

    const u8 *p = (const u8 *)(u64)map_paddr;

    for (u32 i = 0; i < entries; i++, p += PVH_MAP_SIZE) {
        u64 base   = 0;
        u64 length = 0;
        u32 type   = 0;

        /* Type 0 is the conventional end-of-map marker, even though the count
         * should already have stopped us. */
        if (rd32(p + PVH_MAP_OFF_TYPE) == PVH_MEM_UNUSED)
            break;

        if (!read_entry(p, &base, &length, &type)) {
            map_unreadable++;
            continue;
        }

        if (type == PVH_MEM_RAM && length > largest_free) {
            largest_free = length;
            heap_start   = base;
            heap_end     = base + length;
        }
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
