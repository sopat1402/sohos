#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/virtual_manager.h"
#include "../include/elf.h"
#include "../include/phys.h"
#include "../include/kstring.h"

#define ELF_USER_LIMIT 0x0000800000000000ULL
#define ELF_PAGE_SIZE  4096ULL
#define ELF_PF_X       1u
#define ELF_PF_W       2u

int elf_load(const void *elf_data, uint64_t size, struct address_space *target_as, uint64_t *entry_out) {
    if (elf_data == NULL || target_as == NULL || entry_out == NULL || size < sizeof(Elf64_Ehdr))
        return -1;

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F')
        return -1;

    if (ehdr->e_ident[4] != 2 || ehdr->e_machine != EM_X86_64 || ehdr->e_type != ET_EXEC)
        return -2;

    if (ehdr->e_phnum == 0 || ehdr->e_phentsize != sizeof(Elf64_Phdr))
        return -1;

    uint64_t ph_bytes = (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr);

    if (ehdr->e_phoff > size || ph_bytes > size - ehdr->e_phoff)
        return -1;

    if (ehdr->e_entry == 0 || ehdr->e_entry >= ELF_USER_LIMIT)
        return -1;

    const Elf64_Phdr *phdr = (const Elf64_Phdr *)((const char *)elf_data + ehdr->e_phoff);
    int entry_ok = 0;
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint64_t p_vaddr = phdr[i].p_vaddr;
        uint64_t filesz  = phdr[i].p_filesz;
        uint64_t memsz   = phdr[i].p_memsz;
        uint64_t offset  = phdr[i].p_offset;
        uint32_t p_flags = phdr[i].p_flags;

        if (memsz == 0)
            continue;

        if (filesz > memsz)
            return -1;

        if (offset > size || filesz > size - offset)
            return -1;

        if (p_vaddr >= ELF_USER_LIMIT || memsz > ELF_USER_LIMIT - p_vaddr)
            return -1;

        if ((p_flags & ELF_PF_W) && (p_flags & ELF_PF_X))
            return -2;

        if ((p_flags & ELF_PF_X) && ehdr->e_entry >= p_vaddr && ehdr->e_entry < p_vaddr + memsz)
            entry_ok = 1;

        uint64_t vstart = p_vaddr & ~(ELF_PAGE_SIZE - 1);
        uint64_t vend = (p_vaddr + memsz + ELF_PAGE_SIZE - 1) & ~(ELF_PAGE_SIZE - 1);

        uint64_t vm_flags = VM_USER | VM_READ;
        if (p_flags & ELF_PF_W)
            vm_flags |= VM_WRITE;
        if (p_flags & ELF_PF_X)
            vm_flags |= VM_EXEC;

        for (uint64_t curr_vaddr = vstart; curr_vaddr < vend; curr_vaddr += ELF_PAGE_SIZE) {
            uint64_t phys_frame = 0;
            enum vm_map_status status = vm_map(target_as->pml4_phys, curr_vaddr, vm_flags, &phys_frame);

            if (status != VM_MAP_OK && status != VM_MAP_ALREADY_MAPPED)
                return -3;

            if (phys_frame == 0)
                return -3;

            uint8_t *kernel_dest = (uint8_t *)phys_to_virt(phys_frame);

            if (status == VM_MAP_OK)
                __builtin_memset(kernel_dest, 0, ELF_PAGE_SIZE);

            uint64_t file_end = p_vaddr + filesz;
            uint64_t page_end = curr_vaddr + ELF_PAGE_SIZE;
            uint64_t copy_start = curr_vaddr > p_vaddr ? curr_vaddr : p_vaddr;
            uint64_t copy_end = page_end < file_end ? page_end : file_end;

            if (copy_start < copy_end) {
                const uint8_t *src = (const uint8_t *)elf_data + offset + (copy_start - p_vaddr);
                kmemcpy(kernel_dest + (copy_start - curr_vaddr), src, copy_end - copy_start);
            }
        }
    }
    if (!entry_ok)
        return -1;

    *entry_out = ehdr->e_entry;
    return 0;
}
