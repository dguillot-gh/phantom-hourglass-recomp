// ___chkstk_ms for the clang-built game DLL on Windows (no compiler runtime is linked): clang calls it before a stack
// frame bigger than a page, with the frame size in RAX; it touches each page down to the new stack bottom so the
// guard page moves along. Keeps every register.
    .text
    .globl ___chkstk_ms
___chkstk_ms:
    pushq %rcx
    pushq %rax
    leaq 24(%rsp), %rcx        // the caller's stack pointer
1:  cmpq $0x1000, %rax
    jb 2f
    subq $0x1000, %rcx
    orq $0, (%rcx)             // touch the next page down
    subq $0x1000, %rax
    jmp 1b
2:  subq %rax, %rcx
    orq $0, (%rcx)
    popq %rax
    popq %rcx
    ret
