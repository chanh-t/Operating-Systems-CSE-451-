.globl preface
preface:
    call main
    mov %rax, %rdi
    call exit
