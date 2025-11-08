section .data
  format_string_label: db "%d", 10, 0
section .bss
  ; reserve uninitialized data here if needed
section .text
  global main
  extern printf
main:
  push rbp
  mov rbp, rsp
  sub rsp, 16
  mov QWORD [rbp - 8], 5
  mov rsi, QWORD [rbp - 8]
  mov rdi, format_string_label
  xor rax, rax
  call printf
  jmp end_program

; --- Division by zero handler ---
section .text
div_by_zero:
  ; exit(1) via linux syscall
  mov rax, 60
  mov rdi, 1
  syscall
end_program:
  mov rdi, 0
  mov rax, 60       ; syscall: exit
  syscall
  mov rsp, rbp
  pop rbp
