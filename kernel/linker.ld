/* Rex OS - Kernel linker script
 * Target: x86_64, higher-half kernel
 * Loaded by Limine at virtual address 0xffffffff80000000
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

/* Program headers (segments).
 * The Limine bootloader reads these to know where to load each piece. */
PHDRS
{
    limine_requests  PT_LOAD;
    text             PT_LOAD;
    rodata           PT_LOAD;
    data             PT_LOAD;
}

SECTIONS
{
    /* Higher-half: a kernel az utolsó 2 GB virtuális címen él.
     * Ez a klasszikus -mcmodel=kernel layout. */
    . = 0xffffffff80000000;

    /* Limine "requests" szekció: a kernel itt deklarálja, milyen
     * szolgáltatásokat kér a bootloadertől (framebuffer, memory map stb.).
     * A KEEP() megakadályozza, hogy a linker kioptimalizálja, mert
     * statikusan "felhasználatlannak" tűnnek. */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __kernel_end = .;

    /* Felesleges szekciók eldobása (csökkenti a kernel ELF méretét). */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
