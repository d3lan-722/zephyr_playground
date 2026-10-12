
/* Auto generated code. Do not modify.*/
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <kernel_internal.h>
#include <zephyr/arch/common/init.h>

extern char __itcm_text_reloc_start[];
extern char __itcm_text_rom_start[];
extern char __itcm_text_reloc_size[];

extern char __itcm_data_reloc_start[];
extern char __itcm_data_rom_start[];
extern char __itcm_data_reloc_size[];

extern char __itcm_bss_reloc_start[];
extern char __itcm_bss_rom_start[];
extern char __itcm_bss_reloc_size[];

FUNC_NO_STACK_PROTECTOR void data_copy_xip_relocation(void)
{

	arch_early_memcpy(&__itcm_text_reloc_start, &__itcm_text_rom_start,
		           (size_t) &__itcm_text_reloc_size);


	arch_early_memcpy(&__itcm_data_reloc_start, &__itcm_data_rom_start,
		           (size_t) &__itcm_data_reloc_size);


}

void bss_zeroing_relocation(void)
{

	arch_early_memset(&__itcm_bss_reloc_start, 0,
		           (size_t) &__itcm_bss_reloc_size);

}
