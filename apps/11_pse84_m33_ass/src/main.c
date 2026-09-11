#include <stdio.h>
#include <stdint.h>

extern int asm_add(uint32_t a, uint32_t b);

int main(void)
{
	printf("Hello World from: %s running Zephyr\n", CONFIG_BOARD_TARGET);
	printf("This application will add numbers using inline assembly and an externally called assembly function\n\n");
	
	//inline assembly portion
	printf("inline assembly: clearing r0, adding 7 to r0 then storing to r0.\n");
	uint32_t r0;

	__asm__("mov r0, #2"); // 
	// printf("register zero value: %d\n", r0); // !!! THIS WILL CLOBBER r0.
	__asm__("add r0, r0, #7 ;");	/* register 0 = register 0 + 7 */
	__asm__("mov %0, r0" : "=r"(r0)); // for printing purposes in C.
	printf("\tadded 7 to register 0, stored in register zero.\n\tresult (r0 value): %d\n", r0);

	//external .s file portion
	printf("\nexternal assembly function called in C.\n");
	uint32_t num_a = 5;
	uint32_t num_b = 7;
	uint32_t asm_add_result = asm_add(num_a, num_b);
	printf("\t %d + %d = %d via asm\n", num_a, num_b, asm_add_result);

	return 0;
}