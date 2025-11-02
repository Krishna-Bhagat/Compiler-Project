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
  sub rsp, 24
  mov QWORD [rbp - 8], 14
  mov QWORD [rbp - 16], 5
  mov rax, QWORD [rbp - 8]
  mov rbx, QWORD [rbp - 16]
  ; Modulo operation
  cqo
  test rbx, rbx
  jz div_by_zero
  idiv rbx
  mov rax, rdx
  push rax
  pop rax
  mov QWORD [rbp - 24], rax
  mov rax, 3
  push rax
  mov rsi, QWORD [rbp - 24]
  mov rdi, format_string_label
  xor rax, rax
  call printf

; --- Division by zero handler ---
section .text
div_by_zero:
  ; exit(1) via linux syscall
  mov rax, 60
  mov rdi, 1
  syscall
  mov rax, 3
  mov rdi, rax
end_program:
  mov rax, 60       ; syscall: exit
  syscall
  mov rsp, rbp
  pop rbp
