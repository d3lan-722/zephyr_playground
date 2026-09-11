.section .text
.globl asm_add
.type asm_add, %function
asm_add:
    add r0,r1,r0
    bx lr 

# r14 is the link register. It stores the return information for subroutines, function calls, and exceptions.