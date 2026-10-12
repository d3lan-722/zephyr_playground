
/* Auto generated code. Do not modify.*/
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <kernel_internal.h>
#include <zephyr/arch/common/init.h>

extern char __m33scode_text_reloc_start[];
extern char __m33scode_text_rom_start[];
extern char __m33scode_text_reloc_size[];

extern char __m33scode_rodata_reloc_start[];
extern char __m33scode_rodata_rom_start[];
extern char __m33scode_rodata_reloc_size[];

extern char __m33scode_bss_reloc_start[];
extern char __m33scode_bss_rom_start[];
extern char __m33scode_bss_reloc_size[];

FUNC_NO_STACK_PROTECTOR void data_copy_xip_relocation(void)
{

	arch_early_memcpy(&__m33scode_text_reloc_start, &__m33scode_text_rom_start,
		           (size_t) &__m33scode_text_reloc_size);


	arch_early_memcpy(&__m33scode_rodata_reloc_start, &__m33scode_rodata_rom_start,
		           (size_t) &__m33scode_rodata_reloc_size);


}

void bss_zeroing_relocation(void)
{

	arch_early_memset(&__m33scode_bss_reloc_start, 0,
		           (size_t) &__m33scode_bss_reloc_size);

}
