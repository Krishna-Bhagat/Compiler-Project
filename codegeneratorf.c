#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <ctype.h>
#include <assert.h>

#include "lexerf.h"
#include "parserf.h"
#include "./hashmap/hashmapoperators.h"
#include "./hashmap/hashmap.h"

#define MAX_STACK_SIZE_SIZE 1024

char *curly_stack[MAX_STACK_SIZE_SIZE];
size_t curly_stack_size = 0;
int curly_count = 0;
int global_curly = 0;
size_t stack_size = 0;
int current_stack_size_size = 0;
int label_number = 0;
int loop_label_number = 0;
int text_label = 0;
size_t current_stack_size[MAX_STACK_SIZE_SIZE];
const unsigned initial_size = 100;
struct hashmap_s hashmap;

typedef enum
{
  ADD,
  SUB,
  DIV,
  MUL,
  MOD,
  NOT_OPERATOR
} OperatorType;

void create_label(FILE *file, int num)
{
  // label_number--;
  fprintf(file, "label%d:\n", num);
}

void create_end_loop(FILE *file)
{
  loop_label_number--;
  fprintf(file, " jmp loop%d\n", loop_label_number);
}

void create_loop_label(FILE *file)
{
  fprintf(file, "loop%d:\n", loop_label_number);
  loop_label_number++;
}

void if_label(FILE *file, char *comp, int num)
{
  if (strcmp(comp, "EQ") == 0)
  {
    fprintf(file, "  jne label%d\n", num);
  }
  else if (strcmp(comp, "NEQ") == 0)
  {
    fprintf(file, "  je label%d\n", num);
  }
  else if (strcmp(comp, "LESS") == 0)
  {
    fprintf(file, "  jge label%d\n", num);
  }
  else if (strcmp(comp, "GREATER") == 0)
  {
    fprintf(file, "  jle label%d\n", num);
  }
  else
  {
    printf("ERROR: Unexpected comparator\n");
    exit(1);
  }
  label_number++;
}

void stack_push(size_t value)
{
  if (current_stack_size_size >= MAX_STACK_SIZE_SIZE)
  {
    fprintf(stderr, "ERROR: stack overflow (max size %d)\n", MAX_STACK_SIZE_SIZE);
    exit(1);
  }
  if (value > 10000) // Add reasonability check
  {
    fprintf(stderr, "ERROR: Attempting to push suspiciously large value %zu\n", value);
    exit(1);
  }
  current_stack_size[current_stack_size_size++] = value;
}

size_t stack_pop()
{
  if (current_stack_size_size == 0)
  {
    printf("ERROR: stack is already empty\n");
    exit(1);
  }
  current_stack_size_size--;
  return current_stack_size[current_stack_size_size];
}

void curly_stack_push(char *value)
{
  if (curly_stack_size >= MAX_STACK_SIZE_SIZE)
  {
    fprintf(stderr, "ERROR: curly stack overflow\n");
    exit(1);
  }
  curly_stack[curly_stack_size++] = value;
}

char *curly_stack_pop()
{
  if (curly_stack_size == 0)
  {
    return NULL;
  }
  /* pop from last occupied slot */
  curly_stack_size--;
  return curly_stack[curly_stack_size];
}

char *curly_stack_peek()
{
  if (curly_stack_size == 0)
  {
    return NULL;
  }
  return curly_stack[curly_stack_size - 1];
}

static int log_and_free_out_of_scope(void *const context, struct hashmap_element_s *const e)
{
  (void)(context);
  if (current_stack_size_size == 0)
  {
    return 0;
  }

  /* currently current_stack_size_size is the count of entries; top index is count-1 */
  size_t top_index = current_stack_size_size - 1;
  if (*(size_t *)e->data > (current_stack_size[top_index] + 1))
  {
    if (hashmap_remove(&hashmap, e->key, strlen(e->key)) != 0)
    {
      printf("COULD NOT REMOVE ELEMENT\n");
    }
  }
  return 0;
}

void push(char *reg, FILE *file)
{
  fprintf(file, "  push %s\n", reg);
  stack_size++;
}

void push_var(size_t stack_pos, char *var_name, FILE *file)
{
  fprintf(file, "  push QWORD [rsp + %zu]\n", (stack_size - stack_pos) * 8);
  stack_size++;
}

void modify_var(size_t stack_pos, char *new_value, char *var_name, FILE *file)
{
  fprintf(file, "  mov QWORD [rsp + %zu], %s\n", ((stack_size) - (stack_pos)) * 8, new_value);
  fprintf(file, "  push QWORD [rsp + %zu]\n", (stack_size - stack_pos) * 8);
}

void pop(char *reg, FILE *file)
{
  if (stack_size == 0)
  {
    printf("ERROR: stack underflow\n");
    exit(1);
  }
  stack_size--;
  fprintf(file, "  pop %s\n", reg);
  if (stack_size > 1000)
  {
    fprintf(stderr, "ERROR: suspicious stack size > 1000\n");
    exit(1);
  }
}

void emit_call_with_shadow(FILE *file, const char *func, const char *reg)
{
  fprintf(file, "  mov rdi, %s\n", reg); /* Linux: first arg in rdi */
  /* for variadic functions (printf) ensure rax=0 per System V ABI */
  fprintf(file, "  xor rax, rax\n");
  fprintf(file, "  call %s\n", func);
}

void mov(char *reg1, char *reg2, FILE *file)
{
  fprintf(file, "  mov %s, %s\n", reg1, reg2);
}

OperatorType check_operator(Node *node)
{
  if (node->type != OPERATOR)
  {
    return NOT_OPERATOR;
  }

  if (strcmp(node->value, "+") == 0)
  {
    return ADD;
  }
  if (strcmp(node->value, "-") == 0)
  {
    return SUB;
  }
  if (strcmp(node->value, "/") == 0)
  {
    return DIV;
  }
  if (strcmp(node->value, "*") == 0)
  {
    return MUL;
  }
  if (strcmp(node->value, "%") == 0)
  {
    return MOD;
  }
  return NOT_OPERATOR;
}

int mov_if_var_or_not(char *reg, Node *node, FILE *file)
{
  if (node == NULL)
  {
    printf("ERROR: Null node passed to mov_if_var_or_not\n");
    exit(1);
  }
  if (node->type == IDENTIFIER)
  {
    int *value = hashmap_get(&hashmap, node->value, strlen(node->value));
    if (value == NULL)
    {
      printf("ERROR: Variable %s not declared in current scope\n", node->value);
      exit(1);
    }
    // push_var(*value, node->value, file);
    // pop(reg, file);
    fprintf(file, "  mov %s, QWORD [rsp + %zu]\n", reg, (stack_size - *value) * 8);
    return 0;
  }
  if (node->type == INT)
  {
    fprintf(file, "  mov %s, %s\n", reg, node->value);
    return 0;
  }
  return -1;
}

Node *generate_operator_code(Node *node, FILE *file)
{
  static int op_depth = 0;

  if (node == NULL)
  {
    printf("ERROR: Null node passed to generate_operator_code\n");
    exit(1);
  }

  op_depth++;
  if (op_depth > 50) // Prevent deep operator chains
  {
    printf("ERROR: Operation chain too deep (>50), possible infinite recursion\n");
    exit(1);
  }

  // Move left operand into RAX
  mov_if_var_or_not("rax", node->left, file);

  op_depth--;

  // Determine the operator type
  OperatorType oper_type = check_operator(node);
  if (oper_type == NOT_OPERATOR)
  {
    printf("ERROR: Invalid operator '%s'\n", node->value);
    exit(1);
  }

  // Move right operand into RBX
  mov_if_var_or_not("rbx", node->right, file);

  // Perform the corresponding operation
  switch (oper_type)
  {
  case ADD:
    fprintf(file, "  add rax, rbx\n");
    break;

  case SUB:
    fprintf(file, "  sub rax, rbx\n");
    break;

  case MUL:
    fprintf(file, "  imul rbx\n");
    break;

  case DIV:
    fprintf(file, "  cqo\n");      // sign extend RAX into RDX:RAX
    fprintf(file, "  idiv rbx\n"); // signed division
    break;

  case MOD:
    fprintf(file, "  cqo\n");
    fprintf(file, "  idiv rbx\n");
    fprintf(file, "  mov rax, rdx\n"); // store remainder in RAX
    break;

  default:
    printf("ERROR: Unknown operator\n");
    exit(1);
  }

  // Push result onto stack for later use
  push("rax", file);

  // Return without detaching nodes - let parent handle cleanup
  return node;
}

void traverse_tree(Node *node, int is_left, FILE *file, int syscall_number)
{
  if (!node || !node->value)
    return;

  static int debug_depth = 0;
  static Node *last_processed = NULL;

  // Prevent processing the same node twice
  if (node == last_processed)
  {
    printf("WARNING: Attempting to process the same node twice, skipping to prevent recursion\n");
    return;
  }
  last_processed = node;

  debug_depth++;
  printf("DEBUG: Node @ %p, value=%s, depth=%d\n", (void *)node, node->value, debug_depth);
  if (debug_depth > 100) // Reduced from 1000 to catch issues earlier
  {
    printf("ERROR: Traverse_tree called too deeply (AST depth > 100), possible infinite recursion or cyclic tree.\n");
    exit(1);
  }
  printf("Codegen: At node type=%d, value='%s', depth=%d\n", node->type, node->value, debug_depth);

  // Handle EXIT syscall
  if (strcmp(node->value, "EXIT") == 0)
  {
    if (node->left) // exit with argument
    {
      if (mov_if_var_or_not("rax", node->left, file) != 0)
      {
        if (node->left->type == OPERATOR)
          generate_operator_code(node->left, file);
        else
        {
          fprintf(stderr, "ERROR: Unsupported exit argument\n");
          exit(1);
        }
      }
      fprintf(file, "  mov rdi, rax\n");
    }
    else
    {
      fprintf(file, "  mov rdi, 0\n"); // default exit code
    }
    fprintf(file, "  mov rax, 60\n"); // syscall number for exit
    fprintf(file, "  syscall\n");
    return;
  }

  // Handle variable initialization: INT
  if (strcmp(node->value, "INT") == 0)
  {
    if (!node->left || !node->left->left || !node->left->left->left)
    {
      printf("ERROR: Invalid variable initialization\n");
      exit(1);
    }

    Node *value = node->left->left->left;

    if (!value)
    {
      printf("ERROR: Missing value for variable\n");
      exit(1);
    }

    if (value->type == IDENTIFIER)
    {
      size_t *var_value = hashmap_get(&hashmap, value->value, strlen(value->value));
      if (!var_value)
      {
        printf("ERROR: Variable %s not declared in current context\n", value->value);
        exit(1);
      }
      push_var(*var_value, value->value, file);
    }
    else if (value->type == INT)
    {
      push(value->value, file);
    }
    else if (value->type == OPERATOR)
    {
      generate_operator_code(value, file);
    }
    else
    {
      printf("ERROR: Invalid variable value type\n");
      exit(1);
    }

    size_t *cur_size = malloc(sizeof(size_t));
    *cur_size = stack_size;

    if (!hashmap_put(&hashmap, node->left->value, strlen(node->left->value), cur_size))
    {
      printf("ERROR: Could not insert variable into hash table\n");
      exit(1);
    }

    node->left = NULL;
  }

  // Handle IF statements
  if (strcmp(node->value, "IF") == 0)
  {
    if (!node->left || !node->left->left)
    {
      printf("ERROR: IF node missing condition\n");
      exit(1);
    }

    Node *condition = node->left->left;

    if (condition->left)
    {
      if (condition->left->type == INT || condition->left->type == IDENTIFIER)
        mov_if_var_or_not("rax", condition->left, file);
      else
        generate_operator_code(condition->left, file);
    }

    if (condition->right)
    {
      if (condition->right->type == INT || condition->right->type == IDENTIFIER)
        mov_if_var_or_not("rbx", condition->right, file);
      else
        generate_operator_code(condition->right, file);
    }

    fprintf(file, "  cmp rax, rbx\n");
    if_label(file, condition->value, curly_count);

    node->left->left = NULL;
  }

  // Handle WHILE loops
  if (strcmp(node->value, "WHILE") == 0)
  {
    if (!node->left || !node->left->left)
    {
      printf("ERROR: WHILE node missing condition\n");
      exit(1);
    }

    Node *condition = node->left->left;
    create_loop_label(file);

    if (condition->left)
    {
      if (condition->left->type == INT || condition->left->type == IDENTIFIER)
        mov_if_var_or_not("rax", condition->left, file);
      else
        generate_operator_code(condition->left, file);
    }

    if (condition->right)
    {
      if (condition->right->type == INT || condition->right->type == IDENTIFIER)
        mov_if_var_or_not("rbx", condition->right, file);
      else
        generate_operator_code(condition->right, file);
    }

    fprintf(file, "  cmp rax, rbx\n");

    if (strcmp(condition->value, "EQ") == 0)
      if_label(file, "EQ", curly_count);
    else if (strcmp(condition->value, "NEQ") == 0)
      if_label(file, "NEQ", curly_count);
    else if (strcmp(condition->value, "LESS") == 0)
      if_label(file, "LESS", curly_count);
    else if (strcmp(condition->value, "GREATER") == 0)
      if_label(file, "GREATER", curly_count);
    else
    {
      printf("ERROR: Unknown WHILE comparison operator\n");
      exit(1);
    }

    node->left->left = NULL;
  }

  // Handle WRITE statements
  if (strcmp(node->value, "WRITE") == 0)
  {
    if (!node->left)
    {
      printf("ERROR: WRITE expects an argument\n");
      exit(1);
    }

    if (node->left->type == IDENTIFIER)
    {
      size_t *var_addr = hashmap_get(&hashmap, node->left->value, strlen(node->left->value));
      if (!var_addr)
      {
        printf("ERROR: Variable %s is not defined\n", node->left->value);
        exit(1);
      }
      push_var(*var_addr, node->left->value, file);
      pop("rsi", file);
      fprintf(file, "  mov rdi, format_string_label\n");
      fprintf(file, "  xor rax, rax\n");
      fprintf(file, "  call printf\n");
    }
    else if (node->left->type == INT)
    {
      fprintf(file, "  mov rsi, %s\n", node->left->value);
      fprintf(file, "  mov rdi, format_string_label\n");
      fprintf(file, "  xor rax, rax\n");
      fprintf(file, "  call printf\n");
    }
    else if (node->left->type == STRING)
    {
      char label[32];
      sprintf(label, "str_label_%d", text_label++);
      fprintf(file, "section .data\n");
      fprintf(file, "%s: db \"%s\", 10, 0\n", label, node->left->value);
      fprintf(file, "section .text\n");
      fprintf(file, "  lea rdi, [%s]\n", label);
      fprintf(file, "  xor rax, rax\n");
      fprintf(file, "  call printf\n");
    }
    else
    {
      printf("ERROR: Invalid WRITE argument type\n");
      exit(1);
    }
  }

  // Handle operators
  if (node->type == OPERATOR && node->value[0] != '=')
    generate_operator_code(node, file);

  // Handle INT literals
  if (node->type == INT)
  {
    fprintf(file, "  mov rax, %s\n", node->value);
    push("rax", file);
  }

  // Handle IDENTIFIER nodes
  if (node->type == IDENTIFIER)
  {
    if (syscall_number == 60)
    {
      size_t *var_value = hashmap_get(&hashmap, node->value, strlen(node->value));
      if (!var_value)
      {
        printf("ERROR: Not Declared in current scope: %s\n", node->value);
        exit(1);
      }
      push_var(*var_value, node->value, file);
      pop("rdi", file);
      fprintf(file, "  mov rax, %d\n", syscall_number);
      fprintf(file, "  syscall\n");
      syscall_number = 0;
    }
    else
    {
      if (hashmap_get(&hashmap, node->value, strlen(node->value)) == NULL)
      {
        printf("ERROR: Variable %s is not declared in current scope\n", node->value);
        exit(1);
      }
    }
  }

  // Handle curly braces
  if (strcmp(node->value, "{") == 0)
  {
    stack_push(stack_size);
    curly_count++;
    char curly_count_str[16];
    sprintf(curly_count_str, "%d", curly_count);
    curly_stack_push(curly_count_str);
  }

  if (strcmp(node->value, "}") == 0)
  {
    char *current_curly = curly_stack_pop();
    char *next_curly = curly_stack_pop();
    if (!current_curly || !next_curly)
    {
      printf("ERROR: Unmatched curly brace\n");
      exit(1);
    }

    if (next_curly[0] == 'I')
    {
      create_label(file, atoi(current_curly) - 1);
      global_curly = atoi(current_curly);
    }
    else if (next_curly[0] == 'W')
    {
      create_end_loop(file);
      create_label(file, atoi(current_curly) - 1);
      global_curly = atoi(current_curly);
    }

    size_t stack_value = stack_pop();
    while (stack_size != stack_value)
      pop("rsi", file);

    void *log = malloc(sizeof(char));
    if (hashmap_iterate_pairs(&hashmap, log_and_free_out_of_scope, log) != 0)
      exit(1);
  }

  // Recurse
  traverse_tree(node->left, 1, file, syscall_number);
  traverse_tree(node->right, 0, file, syscall_number);
  debug_depth--;
}

void generate_code(Node *root, char *filename)
{
  static int visited_nodes[1024] = {0}; // Track visited nodes
  memset(visited_nodes, 0, sizeof(visited_nodes));

  // Map operators to instructions
  insert('-', "sub");
  insert('+', "add");
  insert('*', "mul");
  insert('/', "div");

  FILE *file = fopen(filename, "w");
  if (!file)
  {
    fprintf(stderr, "ERROR: Could not open file %s\n", filename);
    exit(1);
  }

  if (hashmap_create(initial_size, &hashmap) != 0)
  {
    fprintf(stderr, "ERROR: Could not create hashmap\n");
    exit(1);
  }

  // ----------------------------
  // DATA SECTION
  // ----------------------------
  fprintf(file, "section .data\n");
  fprintf(file, "  format_string_label: db \"%%d\", 10, 0\n"); // for printf

  // ----------------------------
  // BSS SECTION
  // ----------------------------
  fprintf(file, "section .bss\n");
  fprintf(file, "  ; reserve uninitialized data here if needed\n");

  // ----------------------------
  // TEXT SECTION
  // ----------------------------
  fprintf(file, "section .text\n");
  fprintf(file, "  global main\n");
  fprintf(file, "  extern printf\n");
  fprintf(file, "main:\n");

  // Align stack for Linux ABI
  fprintf(file, "  push rbp\n");
  fprintf(file, "  mov rbp, rsp\n");

  // Traverse the AST and generate assembly
  traverse_tree(root, 0, file, 0);

  // Proper Linux exit (in case EXIT not called in program)
  fprintf(file, "  mov rax, 60       ; syscall: exit\n");
  fprintf(file, "  xor rdi, rdi      ; status 0\n");
  fprintf(file, "  syscall\n");

  fprintf(file, "  mov rsp, rbp\n");
  fprintf(file, "  pop rbp\n");

  fclose(file);

  // Free hashmap memory (optional, prevents memory leaks)
  hashmap_destroy(&hashmap);
}
