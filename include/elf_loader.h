#ifndef ELF_H
#define ELF_H
 
#include <stdint.h>
#include <stddef.h>

#define EI_NIDENT 16
 
#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3
 
#define EM_X86_64 62
 
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6
 
#define PF_X 1u
#define PF_W 2u
#define PF_R 4u
 
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;
 
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;
 
_Static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr must be 64 bytes");
_Static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr must be 56 bytes");
 
struct address_space;

 void *memcpy(void *dst, const void *src, size_t n);
int elf_load(const void *elf_data, uint64_t size, struct address_space *target_as, uint64_t *entry_out);
 
#endif
