#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <ctype.h>
#include <assert.h>

#include "lexerf.h"
#include "parserf.h"
#include "codegeneratorf.h"
#include "generator_internal.h"
#include "generator_state.h"
#include "./hashmap/hashmapoperators.h"
#include "./hashmap/hashmap.h"

// Safety limits
#define MAX_STACK_SIZE_SIZE 1024
#define MAX_STRING_LENGTH 1024
#define MAX_LABEL_COUNT 10000
#define MAX_RECURSION_DEPTH 100
#define MAX_OPERATION_CHAIN_LENGTH 50
#define MAX_VARIABLE_NAME_LENGTH 64
#define MAX_VARIABLES 1000
#define MAX_SCOPE_DEPTH 100

// Hashmap validation
static size_t variable_count = 0;
static size_t scope_depth = 0;

// Track current source line for better diagnostics
static size_t current_line_num = 0;

// Unified error handling macro that includes line information when available
#define SAFE_EXIT(msg, ...)                                                             \
  do                                                                                    \
  {                                                                                     \
    if (current_line_num > 0)                                                           \
    {                                                                                   \
      fprintf(stderr, "ERROR at line %zu: " msg "\n", current_line_num, ##__VA_ARGS__); \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
      fprintf(stderr, "ERROR: " msg "\n", ##__VA_ARGS__);                               \
    }                                                                                   \
    exit(1);                                                                            \
  } while (0)

static void validate_variable_insert(const char *var_name)
{
  if (!var_name)
  {
    SAFE_EXIT("NULL variable name");
  }

  if (strlen(var_name) >= MAX_VARIABLE_NAME_LENGTH)
  {
    SAFE_EXIT("Variable name too long: %s", var_name);
  }

  if (variable_count >= MAX_VARIABLES)
  {
    SAFE_EXIT("Too many variables (max: %d)", MAX_VARIABLES);
  }

  if (scope_depth >= MAX_SCOPE_DEPTH)
  {
    SAFE_EXIT("Maximum scope depth exceeded: %zu", scope_depth);
  }

  variable_count++;
}

char *curly_stack[MAX_STACK_SIZE_SIZE];
static size_t curly_stack_size = 0;
static int curly_count = 0;
static int global_curly = 0;
static size_t stack_size = 0;
static int current_stack_size_size = 0;
static int label_number = 0;
static int loop_label_number = 0;
static int text_label = 0;
/* Guard to ensure we only emit a single exit syscall when generating code */
static int exit_emitted = 0;
/* Track string literals for emission in data section */
#define MAX_STRING_LITERALS 10000
static struct {
  char label[32];
  char value[MAX_STRING_LENGTH];
} string_literals[MAX_STRING_LITERALS];
static int string_literal_count = 0;
/* If an EXIT is encountered during traversal we store the argument node here
  and emit the actual exit code at the end of the generated program so that
  it runs after all other statements (e.g., WRITE). */
static Node *exit_node_arg = NULL;
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

// Track used labels to prevent duplicates
static int used_labels[MAX_LABEL_COUNT] = {0};

static void validate_label_number(int num, const char *label_type)
{
  if (num < 0 || num >= MAX_LABEL_COUNT)
  {
    SAFE_EXIT("Invalid %s label number: %d (max: %d)",
              label_type, num, MAX_LABEL_COUNT);
  }
  if (used_labels[num])
  {
    SAFE_EXIT("Duplicate %s label number: %d", label_type, num);
  }
  used_labels[num] = 1;
}

/* Visited set for traverse_tree: moved to file scope so it can be
  explicitly reset before starting a fresh traversal. */
static void *visited_nodes_global[10000];
static size_t visited_count_global = 0;

// Validate hashmap operations
static void validate_hashmap_get(const char *key)
{
  if (!key)
  {
    SAFE_EXIT("NULL key in hashmap get operation");
  }

  if (strlen(key) >= MAX_VARIABLE_NAME_LENGTH)
  {
    SAFE_EXIT("Key too long in hashmap get: %s", key);
  }
}

static void validate_hashmap_put(const char *key, void *value)
{
  if (!key)
  {
    SAFE_EXIT("NULL key in hashmap put operation");
  }

  if (!value)
  {
    SAFE_EXIT("NULL value in hashmap put operation for key: %s", key);
  }

  if (strlen(key) >= MAX_VARIABLE_NAME_LENGTH)
  {
    SAFE_EXIT("Key too long in hashmap put: %s", key);
  }

  if (variable_count >= MAX_VARIABLES)
  {
    SAFE_EXIT("Maximum variables exceeded in hashmap put: %zu", variable_count);
  }
}

static void validate_hashmap_remove(const char *key)
{
  if (!key)
  {
    SAFE_EXIT("NULL key in hashmap remove operation");
  }

  if (strlen(key) >= MAX_VARIABLE_NAME_LENGTH)
  {
    SAFE_EXIT("Key too long in hashmap remove: %s", key);
  }

  if (variable_count == 0)
  {
    SAFE_EXIT("Attempt to remove from empty hashmap: %s", key);
  }

  variable_count--;
}

static void create_label(FILE *file, int num)
{
  if (!file)
  {
    SAFE_EXIT("NULL file pointer in create_label");
  }
  validate_label_number(num, "regular");
  fprintf(file, "label%d:\n", num);
}

/* Pre-pass: assign fixed rbp-relative offsets for all variables. Each
   variable gets 8 bytes. We store the byte offset (positive) in the
   hashmap as a size_t* so existing lookup code can retrieve it. */
static void assign_variable_offsets(Node *node, size_t *next_offset)
{
  if (!node)
    return;

  /* If this is an INT declaration node, extract variable name */
  if ((node->type == INT || (node->value && strcmp(node->value, "INT") == 0)) && node->left && node->left->type == IDENTIFIER)
  {
    const char *var_name = node->left->value;
    if (var_name)
    {
      /* If not already present, insert with next offset */
      size_t *existing = hashmap_get(&hashmap, var_name, strlen(var_name));
      if (!existing)
      {
        size_t *off = malloc(sizeof(size_t));
        if (!off)
          SAFE_EXIT("Memory allocation failed while assigning variable offsets");
        *off = *next_offset; /* bytes from rbp (positive) */
        char *key = strdup(var_name);
        if (!key)
        {
          free(off);
          SAFE_EXIT("Memory allocation failed while assigning variable name");
        }
        if (hashmap_put(&hashmap, key, strlen(key), off) != 0)
        {
          free(off);
          free(key);
          SAFE_EXIT("Failed to insert variable '%s' during prepass", var_name);
        }
        *next_offset += 8; /* reserve 8 bytes per variable */
      }
    }
  }

  /* Recurse into children */
  assign_variable_offsets(node->left, next_offset);
  assign_variable_offsets(node->right, next_offset);
}

static void create_end_loop(FILE *file)
{
  if (!file)
  {
    SAFE_EXIT("NULL file pointer in create_end_loop");
  }
  if (loop_label_number <= 0)
  {
    SAFE_EXIT("Invalid loop label number: %d", loop_label_number);
  }
  loop_label_number--;
  fprintf(file, " jmp loop%d\n", loop_label_number);
}

void create_loop_label(FILE *file)
{
  if (!file)
  {
    SAFE_EXIT("NULL file pointer in create_loop_label");
  }
  if (loop_label_number >= MAX_LABEL_COUNT)
  {
    SAFE_EXIT("Too many loop labels (max: %d)", MAX_LABEL_COUNT);
  }
  fprintf(file, "loop%d:\n", loop_label_number);
  validate_label_number(loop_label_number, "loop");
  loop_label_number++;
}

void if_label(FILE *file, char *comp, int num)
{
  if (!file || !comp)
  {
    SAFE_EXIT("NULL pointer in if_label");
  }

  validate_label_number(num, "if");

  // Use constant strings for comparison to prevent typos
  const char *EQ = "EQ";
  const char *NEQ = "NEQ";
  const char *LESS = "LESS";
  const char *GREATER = "GREATER";

  if (strcmp(comp, EQ) == 0)
  {
    fprintf(file, "  jne label%d\n", num);
  }
  else if (strcmp(comp, NEQ) == 0)
  {
    fprintf(file, "  je label%d\n", num);
  }
  else if (strcmp(comp, LESS) == 0)
  {
    fprintf(file, "  jge label%d\n", num);
  }
  else if (strcmp(comp, GREATER) == 0)
  {
    fprintf(file, "  jle label%d\n", num);
  }
  else
  {
    SAFE_EXIT("Invalid comparator: '%s'", comp);
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
  if (!value)
  {
    SAFE_EXIT("NULL pointer in curly_stack_push");
  }
  if (curly_stack_size >= MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Curly brace stack overflow (max size: %d)", MAX_STACK_SIZE_SIZE);
  }
  if (strlen(value) >= MAX_STRING_LENGTH)
  {
    SAFE_EXIT("Curly stack value too long: %zu chars (max: %d)",
              strlen(value), MAX_STRING_LENGTH);
  }

  // Make a copy of the string to prevent dangling pointers
  char *stored_value = strdup(value);
  if (!stored_value)
  {
    SAFE_EXIT("Memory allocation failed in curly_stack_push");
  }

  curly_stack[curly_stack_size++] = stored_value;
}

char *curly_stack_pop()
{
  if (curly_stack_size == 0)
  {
    return NULL;
  }

  curly_stack_size--;
  char *value = curly_stack[curly_stack_size];
  curly_stack[curly_stack_size] = NULL; // Clear the slot
  return value;
}

char *curly_stack_peek()
{
  if (curly_stack_size == 0)
  {
    return NULL;
  }
  if (curly_stack_size > MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Curly stack corruption detected");
  }
  return curly_stack[curly_stack_size - 1];
}

static int log_and_free_out_of_scope(void *const context, struct hashmap_element_s *const e)
{
  (void)(context);

  if (!e)
  {
    SAFE_EXIT("NULL element in hashmap cleanup");
  }

  if (!e->key || !e->data)
  {
    SAFE_EXIT("Invalid hashmap element: missing key or data");
  }

  if (current_stack_size_size == 0)
  {
    return 0;
  }

  /* Validate stack access */
  if (current_stack_size_size > MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Stack size corruption detected in cleanup");
  }

  size_t top_index = current_stack_size_size - 1;
  size_t *value_ptr = (size_t *)e->data;

  if (*value_ptr > (current_stack_size[top_index] + 1))
  {
    if (hashmap_remove(&hashmap, e->key, strlen(e->key)) != 0)
    {
      SAFE_EXIT("Failed to remove out-of-scope variable '%s'", (char *)e->key);
    }
    free(value_ptr); // Free the allocated memory for the variable
  }

  return 0;
}

void push(char *reg, FILE *file)
{
  if (!reg || !file)
  {
    SAFE_EXIT("NULL pointer in push operation");
  }
  if (stack_size >= MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Stack overflow: maximum stack size (%d) exceeded", MAX_STACK_SIZE_SIZE);
  }
  fprintf(file, "  push %s\n", reg);
  stack_size++;
}

void push_var(size_t stack_pos, char *var_name, FILE *file)
{
  if (!var_name || !file)
  {
    SAFE_EXIT("NULL pointer in push_var operation");
  }
  if (stack_size >= MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Stack overflow in push_var operation");
  }
  if (stack_pos > stack_size)
  {
    SAFE_EXIT("Invalid stack position %zu (stack size: %zu)", stack_pos, stack_size);
  }
  /* stack_pos previously held a stack index; now it represents byte offset from rbp */
  fprintf(file, "  mov rax, QWORD [rbp - %zu]\n", stack_pos);
  fprintf(file, "  push rax\n");
  stack_size++;
}

void modify_var(size_t stack_pos, char *new_value, char *var_name, FILE *file)
{
  if (!new_value || !var_name || !file)
  {
    SAFE_EXIT("NULL pointer in modify_var operation");
  }
  if (stack_pos > stack_size)
  {
    SAFE_EXIT("Invalid stack position in modify_var: %zu (stack size: %zu)",
              stack_pos, stack_size);
  }
  /* Write directly to rbp-relative slot */
  fprintf(file, "  mov QWORD [rbp - %zu], %s\n", stack_pos, new_value);
}

void pop(char *reg, FILE *file)
{
  if (!reg || !file)
  {
    SAFE_EXIT("NULL pointer in pop operation");
  }
  if (stack_size == 0)
  {
    SAFE_EXIT("Stack underflow in pop operation");
  }
  stack_size--;
  fprintf(file, "  pop %s\n", reg);

  // Sanity check for stack corruption
  if (stack_size > MAX_STACK_SIZE_SIZE)
  {
    SAFE_EXIT("Stack corruption detected: size %zu exceeds maximum %d",
              stack_size, MAX_STACK_SIZE_SIZE);
  }
}

// Valid register list for validation
static const char *const valid_registers[] = {
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9",
    "r10", "r11", "r12", "r13", "r14", "r15", NULL};

static void validate_register(const char *reg, const char *func_name)
{
  if (!reg)
  {
    SAFE_EXIT("NULL register in %s", func_name);
  }

  const char *const *valid = valid_registers;
  while (*valid)
  {
    if (strcmp(reg, *valid) == 0)
    {
      return;
    }
    valid++;
  }
  SAFE_EXIT("Invalid register '%s' in %s", reg, func_name);
}

void emit_call_with_shadow(FILE *file, const char *func, const char *reg)
{
  if (!file || !func || !reg)
  {
    SAFE_EXIT("NULL pointer in emit_call_with_shadow");
  }

  if (strlen(func) >= MAX_STRING_LENGTH)
  {
    SAFE_EXIT("Function name too long: %zu chars", strlen(func));
  }

  validate_register(reg, "emit_call_with_shadow");

  fprintf(file, "  mov rdi, %s\n", reg); // Linux: first arg in rdi
  fprintf(file, "  xor rax, rax\n");     // Clear rax for variadic functions
  fprintf(file, "  call %s\n", func);
}

void mov(char *reg1, char *reg2, FILE *file)
{
  if (!file)
  {
    SAFE_EXIT("NULL file pointer in mov");
  }

  validate_register(reg1, "mov");
  validate_register(reg2, "mov");

  fprintf(file, "  mov %s, %s\n", reg1, reg2);
}

// Define operator strings as constants to prevent typos
static const char *const OP_ADD = "+";
static const char *const OP_SUB = "-";
static const char *const OP_DIV = "/";
static const char *const OP_MUL = "*";
static const char *const OP_MOD = "%";

OperatorType check_operator(Node *node)
{
  if (!node)
  {
    SAFE_EXIT("NULL node in check_operator");
  }

  if (!node->value)
  {
    SAFE_EXIT("Node missing value in check_operator");
  }

  if (node->type != OPERATOR)
  {
    return NOT_OPERATOR;
  }

  // Use string length check for better safety
  if (strlen(node->value) != 1)
  {
    SAFE_EXIT("Invalid operator length: '%s'", node->value);
  }

  // Use constant strings for comparison
  if (strcmp(node->value, OP_ADD) == 0)
  {
    return ADD;
  }
  if (strcmp(node->value, OP_SUB) == 0)
  {
    return SUB;
  }
  if (strcmp(node->value, OP_DIV) == 0)
  {
    return DIV;
  }
  if (strcmp(node->value, OP_MUL) == 0)
  {
    return MUL;
  }
  if (strcmp(node->value, OP_MOD) == 0)
  {
    return MOD;
  }

  SAFE_EXIT("Unrecognized operator: '%s'", node->value);
  return NOT_OPERATOR; // Never reached, but keeps compiler happy
}

int mov_if_var_or_not(char *reg, Node *node, FILE *file)
{
  // printf("DEBUG: mov_if_var_or_not called with node=%p, type=%d\n",
  //        (void *)node, node ? node->type : -1);
  if (node == NULL)
  {
    SAFE_EXIT("Null node passed to mov_if_var_or_not");
  }

  // printf("DEBUG: Node value='%s', type=%d\n", node->value, node->type);

  if (node->type == IDENTIFIER)
  {
    /* Lookup precomputed rbp offset (in bytes) and load from [rbp - offset] */
    validate_hashmap_get(node->value);
    size_t key_len = strlen(node->value);
    size_t *off = (size_t *)hashmap_get(&hashmap, node->value, key_len);
    if (!off)
      SAFE_EXIT("Variable %s not declared in current scope", node->value);
    fprintf(file, "  mov %s, QWORD [rbp - %zu]\n", reg, *off);
    return 0;
  }
  if (node->type == INT)
  {
    fprintf(file, "  mov %s, %s\n", reg, node->value);
    return 0;
  }
  return -1;
}

static void generate_operator_code(Node *node, FILE *file)
{
  static int op_depth = 0;

  if (!node || !file)
  {
    SAFE_EXIT("NULL pointer in generate_operator_code");
  }

  if (!node->value)
  {
    SAFE_EXIT("Invalid operator node: missing value");
  }

  op_depth++;
  if (op_depth > MAX_OPERATION_CHAIN_LENGTH)
  {
    SAFE_EXIT("Operation chain too deep (>%d), possible infinite recursion",
              MAX_OPERATION_CHAIN_LENGTH);
  }

  // Validate operands
  if (!node->left)
  {
    SAFE_EXIT("Missing left operand for operator '%s'", node->value);
  }

  if (!node->right)
  {
    SAFE_EXIT("Missing right operand for operator '%s'", node->value);
  }

  // Move left operand into RAX
  mov_if_var_or_not("rax", node->left, file);

  op_depth--;

  // Determine the operator type
  OperatorType oper_type = check_operator(node);
  if (oper_type == NOT_OPERATOR)
  {
    SAFE_EXIT("Invalid or unsupported operator '%s'", node->value);
  }

  // Move right operand into RBX
  mov_if_var_or_not("rbx", node->right, file);

  // Validate both operands are loaded correctly
  if (!node->left || !node->right)
  {
    SAFE_EXIT("Missing operand in operator '%s'", node->value);
  }

  // Perform the corresponding operation
  switch (oper_type)
  {
  case ADD:
    fprintf(file, "  ; Add operation\n");
    fprintf(file, "  add rax, rbx\n");
    break;

  case SUB:
    fprintf(file, "  ; Subtract operation\n");
    fprintf(file, "  sub rax, rbx\n");
    break;

  case MUL:
    fprintf(file, "  ; Multiply operation\n");
    fprintf(file, "  imul rbx\n");
    break;

  case DIV:
    fprintf(file, "  ; Division operation\n");
    fprintf(file, "  cqo\n");           // sign extend RAX into RDX:RAX
    fprintf(file, "  test rbx, rbx\n"); // Check for division by zero
    fprintf(file, "  jz div_by_zero\n");
    fprintf(file, "  idiv rbx\n"); // signed division
    break;

  case MOD:
    fprintf(file, "  ; Modulo operation\n");
    fprintf(file, "  cqo\n");
    fprintf(file, "  test rbx, rbx\n"); // Check for modulo by zero
    fprintf(file, "  jz div_by_zero\n");
    fprintf(file, "  idiv rbx\n");
    fprintf(file, "  mov rax, rdx\n"); // store remainder in RAX
    break;

  default:
    SAFE_EXIT("Internal error: unhandled operator type %d", oper_type);
  }

  // Push result onto stack for later use
  push("rax", file);
}

static void traverse_tree(Node *node, int is_left, FILE *file, int syscall_number)
{
  static int depth = 0;
  static int debug_depth = 0;

  if (!node || !node->value)
  {
    if (depth > 0)
      depth--;
    return;
  }

  /* Hard depth limit to prevent stack overflow */
  if (depth > 20)
  {
    debug_depth--;
    if (depth > 0)
      depth--;
    return;
  }
  depth++;

  /* Simple cycle-avoidance: some parser constructions may reuse node
     pointers which would cause repeated visits and potential infinite
     recursion. Use file-scoped visited set so it can be reset before a
     traversal. */
  for (size_t vi = 0; vi < visited_count_global; ++vi)
  {
    if (visited_nodes_global[vi] == (void *)node)
    {
      if (depth > 0)
        depth--;
      return; /* already processed */
    }
  }
  if (visited_count_global < 10000)
    visited_nodes_global[visited_count_global++] = (void *)node;

  debug_depth++;

  // printf("DEBUG: Processing node @ %p, type=%d, value='%s', depth=%d\n",
  //        (void *)node, node->type, node->value, debug_depth);

  // Track line number for error reporting
  current_line_num = node->line_num;

  // Break infinite recursion
  if (debug_depth > 100)
  {
    SAFE_EXIT("Maximum AST depth exceeded - possible infinite recursion");
  }

  // For INT declarations, handle the variable and initialization now
  // The parser sometimes sets the node type to KEYWORD while the value
  // contains the string "INT". Accept either case so declarations are
  // recognized reliably and variables are pre-declared before use.
  if ((node->type == INT || (node->value && strcmp(node->value, "INT") == 0)) && node->left && node->left->type == IDENTIFIER)
  {
    const char *var_name = node->left->value;
    // printf("DEBUG: Found INT declaration for '%s'\n", var_name);

    // First, declare the variable before evaluating initialization
    size_t *cur_size = malloc(sizeof(size_t));
    if (!cur_size)
    {
      SAFE_EXIT("Memory allocation failed for variable tracking");
    }
    *cur_size = stack_size;

    // Create persistent key copy
    char *key = strdup(var_name);
    if (!key)
    {
      free(cur_size);
      SAFE_EXIT("Memory allocation failed for variable name");
    }

    /* Use precomputed offset (byte count) from hashmap if available; otherwise
       insert using the current stack size as a fallback (shouldn't normally
       happen because assign_variable_offsets ran earlier). */
    size_t *existing = hashmap_get(&hashmap, key, strlen(key));
    size_t *slot = NULL;
    if (existing)
    {
      slot = existing;
      free(key);
      free(cur_size);
    }
    else
    {
      /* Fallback insertion: assign at current simulated stack_size */
      *cur_size = stack_size * 8;
      if (hashmap_put(&hashmap, key, strlen(key), cur_size) != 0)
      {
        free(key);
        free(cur_size);
        SAFE_EXIT("Failed to declare variable '%s' (fallback)", var_name);
      }
      slot = cur_size;
    }

    /* Now handle initialization: write directly into rbp-relative slot */
    if (node->left->left && node->left->left->left)
    {
      Node *value_node = node->left->left->left;
      // printf("DEBUG: Processing initialization for '%s'\n", var_name);
      if (value_node->type == INT)
      {
        fprintf(file, "  mov QWORD [rbp - %zu], %s\n", *slot, value_node->value);
      }
      else if (value_node->type == IDENTIFIER)
      {
        size_t *src = hashmap_get(&hashmap, value_node->value, strlen(value_node->value));
        if (!src)
          SAFE_EXIT("Variable %s not declared", value_node->value);
        fprintf(file, "  mov rax, QWORD [rbp - %zu]\n", *src);
        fprintf(file, "  mov QWORD [rbp - %zu], rax\n", *slot);
      }
      else if (value_node->type == OPERATOR)
      {
        /* Generate operator code (pushes result), pop into rax and store */
        generate_operator_code(value_node, file);
        pop("rax", file);
        fprintf(file, "  mov QWORD [rbp - %zu], rax\n", *slot);
      }
      else
      {
        SAFE_EXIT("Invalid initialization value type");
      }
    }
    else
    {
      /* No initialization -> set 0 */
      fprintf(file, "  mov QWORD [rbp - %zu], 0\n", *slot);
    }

    /* Mark this declaration handled to avoid re-processing later. Do not
      traverse the right sibling here — let the central traversal flow
      continue so each subtree is visited only once. */
    node->left = NULL;
    debug_depth--;
    if (depth > 0)
      depth--;
    return;
  }

  // Process child nodes first for operator expressions
  // NOTE: skip assignment operators ('=') here — they are handled
  // elsewhere (variable declarations / reassignments). Treat only
  // arithmetic/compare operators with this branch.
  if (node->type == OPERATOR && strcmp(node->value, "=") != 0)
  {
    // printf("DEBUG: Processing operator '%s'\n", node->value);
    traverse_tree(node->left, 1, file, syscall_number);
    traverse_tree(node->right, 0, file, syscall_number);
    generate_operator_code(node, file);
    debug_depth--;
    if (depth > 0)
      depth--;
    return;
  }

  // Handle PROGRAM and semicolon nodes
  if (strcmp(node->value, ";") == 0 || strcmp(node->value, "PROGRAM") == 0)
  {
    // These nodes chain statements - traverse them in order
    traverse_tree(node->left, 1, file, syscall_number);
    traverse_tree(node->right, 0, file, syscall_number);
    debug_depth--;
    if (depth > 0)
      depth--;
    return;
  }

  // Update current line number for error reporting
  current_line_num = node->line_num;

  // Traverse child nodes (depth-first) - but NOT for WRITE which we handle above
  if (node->type != INT && strcmp(node->value, "WRITE") != 0)
  {
    traverse_tree(node->left, 1, file, syscall_number);
    traverse_tree(node->right, 0, file, syscall_number);
  }

  // Handle other node types
  if (strcmp(node->value, "EXIT") == 0)
  {
    /* If we've already emitted an exit syscall previously, skip duplicates.
       Duplicate visits happen because the traversal may revisit nodes in
       certain traversal orders; emitting multiple exit syscalls caused the
       generated program to exit prematurely. */
    if (exit_emitted)
    {
      // printf("DEBUG: EXIT already emitted; skipping duplicate at line %zu\n", node->line_num);
      debug_depth--;
      if (depth > 0)
        depth--;
      return;
    }
    exit_emitted = 1;
    if (node->left) /* exit with argument */
    {
      Node *arg = node->left;
      /* Unwrap parentheses if present */
      if (arg->type == SEPARATOR && arg->left)
        arg = arg->left;

      /* Defer evaluation of the exit argument until the end of codegen */
      exit_node_arg = arg;
      
      /* Mark all EXIT children as visited to prevent processing */
      if (visited_count_global < 10000 && node->left)
      {
        visited_nodes_global[visited_count_global++] = (void *)node->left;
        if (node->left->left && visited_count_global < 10000)
          visited_nodes_global[visited_count_global++] = (void *)node->left->left;
        if (node->left->right && visited_count_global < 10000)
          visited_nodes_global[visited_count_global++] = (void *)node->left->right;
      }
    }
    else
    {
      /* No argument -> default to 0 (handled at emission time) */
      exit_node_arg = NULL;
    }

    /* We do not emit assembly for exit now; it will be emitted once at the end */
    debug_depth--;
    if (depth > 0)
      depth--;
    return;
  }

  // Handle variable initialization: INT
  if (strcmp(node->value, "INT") == 0)
  {
    if (!node->left || !node->left->left || !node->left->left->left)
    {
      SAFE_EXIT("Invalid variable initialization");
    }

    Node *value = node->left->left->left;

    if (!value)
    {
      SAFE_EXIT("Missing value for variable");
    }

    if (value->type == IDENTIFIER)
    {
      size_t key_len = strlen(value->value);
      // printf("DEBUG: Lookup identifier '%s' (len=%zu)\n", value->value, key_len);
      // Make a persistent copy of the key for consistent lookups
      char *lookup_key = strdup(value->value);
      if (!lookup_key)
      {
        SAFE_EXIT("Memory allocation failed during variable lookup");
      }
      size_t *var_value = hashmap_get(&hashmap, lookup_key, key_len);
      free(lookup_key); // Free the temp copy after lookup
      if (!var_value)
      {
        SAFE_EXIT("Variable %s not declared in current context", value->value);
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

    // printf("DEBUG: INT node variable name = '%s'\n", node->left ? node->left->value : "NULL");
    size_t *cur_size = malloc(sizeof(size_t));
    if (!cur_size)
    {
      SAFE_EXIT("Memory allocation failed for stack size");
    }
    *cur_size = stack_size;

    const char *var_name = node->left ? node->left->value : NULL;
    if (!var_name)
    {
      free(cur_size);
      SAFE_EXIT("Missing variable name in INT declaration");
    }

    validate_hashmap_put(var_name, cur_size);
    size_t put_len = strlen(var_name);
    // Make a persistent copy of the key
    char *key = strdup(var_name);
    if (!key)
    {
      free(cur_size);
      SAFE_EXIT("Memory allocation failed for variable name");
    }
    // printf("DEBUG: Inserting variable '%s' (len=%zu) at stack pos %zu\n", key, put_len, *cur_size);
    // printf("DEBUG: Insert key bytes: ");
    for (size_t i = 0; i < put_len && i < 8; i++)
    {
      printf("%02x ", (unsigned char)key[i]);
    }
    printf("\n");
    // Check if variable already exists. If it was pre-declared earlier,
    // treat this as a no-op to avoid duplicate-insert errors caused by
    // traversing the same node multiple times.
    size_t *existing = hashmap_get(&hashmap, key, put_len);
    if (existing)
    {
      // The variable is already present (likely inserted by the pre-declare
      // path). Free our temporary allocation and continue without error.
      // printf("DEBUG: Variable '%s' already exists; skipping insertion\n", var_name);
      free(key);
      free(cur_size);
    }
    else
    {
      int put_rc = hashmap_put(&hashmap, key, put_len, cur_size);
      if (put_rc != 0)
      {
        free(key);
        free(cur_size);
        SAFE_EXIT("Failed to insert variable '%s' into hash table (rc=%d)", var_name, put_rc);
      }
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
    Node *arg = node->left;
    if (arg && arg->type == SEPARATOR && arg->value && strcmp(arg->value, "(") == 0)
    {
      arg = arg->left;
    }

    if (arg)
    {
      if (arg->type == IDENTIFIER)
        printf("DEBUG: Emitting WRITE for identifier '%s' (line %zu)\n", arg->value, arg->line_num);
      else if (arg->type == INT)
        printf("DEBUG: Emitting WRITE for int literal '%s' (line %zu)\n", arg->value, arg->line_num);
      else if (arg->type == STRING)
        printf("DEBUG: Emitting WRITE for string literal (line %zu)\n", arg->line_num);
      else
        printf("DEBUG: Emitting WRITE for unknown arg type=%d (line %zu)\n", arg->type, arg->line_num);
    }

    if (!arg)
    {
      fprintf(stderr, "ERROR: WRITE expects an argument on line %zu\n",
              node->line_num ? node->line_num : 0);
      exit(1);
    }

    if (arg->type == IDENTIFIER)
    {
      size_t *var_addr = hashmap_get(&hashmap, arg->value, strlen(arg->value));
      if (!var_addr)
      {
        fprintf(stderr, "ERROR: Variable '%s' is not defined on line %zu\n",
                arg->value, arg->line_num ? arg->line_num : 0);
        exit(1);
      }
      fprintf(file, "  mov rsi, QWORD [rbp - %zu]\n", *var_addr);
      fprintf(file, "  mov rdi, format_string_label\n");
      fprintf(file, "  xor rax, rax\n");
      fprintf(file, "  call printf\n");
    }
    else if (arg->type == INT)
    {
      fprintf(file, "  mov rsi, %s\n", arg->value);
      fprintf(file, "  mov rdi, format_string_label\n");
      fprintf(file, "  xor rax, rax\n");
      fprintf(file, "  call printf\n");
    }
    else if (arg->type == STRING)
    {
      if (string_literal_count >= MAX_STRING_LITERALS)
      {
        fprintf(stderr, "ERROR: Too many string literals (max %d)\n", MAX_STRING_LITERALS);
        exit(1);
      }
      char label[32];
      snprintf(label, sizeof(label), "str_label_%d", string_literal_count);
      snprintf(string_literals[string_literal_count].label, sizeof(string_literals[string_literal_count].label), "%s", label);
      snprintf(string_literals[string_literal_count].value, sizeof(string_literals[string_literal_count].value), "%s", arg->value);
      string_literal_count++;
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

  // Handle INT literals and declarations
  if (node->type == INT)
  {
    if (!node->left)
    {
      // INT literal
      fprintf(file, "  mov rax, %s\n", node->value);
      push("rax", file);
    }
    else
    {
      // INT declaration
      const char *var_name = node->left ? node->left->value : NULL;
      if (!var_name)
      {
        SAFE_EXIT("Missing variable name in INT declaration");
      }

      printf("DEBUG: Processing INT declaration for '%s'\n", var_name);

      // First evaluate the initialization value
      if (node->left->left && node->left->left->left)
      {
        Node *value_node = node->left->left->left;

        // Push the initialization value
        if (value_node->type == INT)
        {
          fprintf(file, "  mov rax, %s\n", value_node->value);
          push("rax", file);
        }
        else if (value_node->type == IDENTIFIER)
        {
          // Look up existing variable
          size_t key_len = strlen(value_node->value);
          printf("DEBUG: Looking up initializer variable '%s'\n", value_node->value);
          size_t *var_value = hashmap_get(&hashmap, value_node->value, key_len);
          if (!var_value)
          {
            SAFE_EXIT("Variable %s not declared", value_node->value);
          }
          // Push the value from stack
          push_var(*var_value, value_node->value, file);
        }
        else if (value_node->type == OPERATOR)
        {
          generate_operator_code(value_node, file);
        }
        else
        {
          SAFE_EXIT("Invalid initialization value type %d", value_node->type);
        }

        // Now declare the variable
        size_t *cur_size = malloc(sizeof(size_t));
        if (!cur_size)
        {
          SAFE_EXIT("Memory allocation failed for stack size");
        }
        *cur_size = stack_size;

        // Create stable copy of variable name
        char *key = strdup(var_name);
        if (!key)
        {
          free(cur_size);
          SAFE_EXIT("Memory allocation failed for variable name");
        }

        printf("DEBUG: Declaring variable '%s' at stack pos %zu\n", key, *cur_size);
        int put_rc = hashmap_put(&hashmap, key, strlen(key), cur_size);
        if (put_rc != 0)
        {
          free(key);
          free(cur_size);
          SAFE_EXIT("Failed to declare variable '%s'", var_name);
        }
      }

      // Don't traverse further - we've handled everything
      if (depth > 0)
        depth--;
      return;
    }
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

  // Handle declarations first
  if (node->type == INT)
  {
    // INT declaration requires evaluating left side (declaration) first
    if (node->left)
    {
      traverse_tree(node->left, 1, file, syscall_number);
    }
  }
  else if (strcmp(node->value, "EXIT") != 0 && strcmp(node->value, "WRITE") != 0)
  {
    // For other nodes, evaluate both sides (but not EXIT or WRITE which handle their own children)
    traverse_tree(node->left, 1, file, syscall_number);
    traverse_tree(node->right, 0, file, syscall_number);
  }
  debug_depth--;
}

/* Walk top-level PROGRAM/semicolon chains and invoke traverse_tree on each
   statement node. Kept as a separate function to avoid nested-function
   constructs which are non-portable. */
static void process_program(Node *n, FILE *f)
{
  if (!n || !n->value)
    return;
  // printf("PP: visiting node @ %p value='%s'\n", (void *)n, n->value ? n->value : "(null)");
  // printf("PP: node @ %p left=%p right=%p\n", (void *)n, (void *)n->left, (void *)n->right);
  if (strcmp(n->value, "PROGRAM") == 0 || strcmp(n->value, ";") == 0)
  {
    process_program(n->left, f);
    process_program(n->right, f);
  }
  else
  {
    traverse_tree(n, 0, f, 0);
    // Also process right child which may contain more statements
    process_program(n->right, f);
  }
}

void generate_code(Node *root, const char *filename)
{
  if (!root || !filename)
  {
    SAFE_EXIT("Invalid arguments to generate_code");
  }

  // Validate filename
  size_t filename_len = strlen(filename);
  if (filename_len == 0 || filename_len > MAX_STRING_LENGTH)
  {
    SAFE_EXIT("Invalid filename length: %zu", filename_len);
  }

  // Reset global state
  stack_size = 0;
  curly_stack_size = 0;
  text_label = 0;
  label_number = 0;
  loop_label_number = 0;

  // Map operators to instructions
  insert('-', "sub");
  insert('+', "add");
  insert('*', "mul");
  insert('/', "div");

  // Open output file
  FILE *file = fopen(filename, "w");
  if (!file)
  {
    SAFE_EXIT("Could not open file '%s' for writing", filename);
  }

  // Reset tracking variables
  variable_count = 0;
  scope_depth = 0;
  string_literal_count = 0;

  // Initialize hashmap with validation
  if (initial_size > MAX_VARIABLES)
  {
    fclose(file);
    SAFE_EXIT("Initial hashmap size too large: %u", initial_size);
  }

  if (hashmap_create(initial_size, &hashmap) != 0)
  {
    fclose(file);
    SAFE_EXIT("Failed to create variable tracking hashmap");
  }

  // Initialize stacks with validation
  if (sizeof(curly_stack) != sizeof(char *) * MAX_STACK_SIZE_SIZE)
  {
    fclose(file);
    SAFE_EXIT("Curly stack size mismatch");
  }

  memset(curly_stack, 0, sizeof(curly_stack));
  memset(current_stack_size, 0, sizeof(current_stack_size));

  // ----------------------------
  // DATA SECTION
  // ----------------------------
  fprintf(file, "section .data\n");
  fprintf(file, "  format_string_label: db \"%%d\", 10, 0\n"); // for printf
  
  // Emit string literals collected during traversal
  // Note: We do a pre-pass first to collect them
  // For now, we'll emit them after the main traversal

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

  /* Pre-pass: assign fixed rbp-relative offsets for variables and reserve stack */
  {
    size_t next_offset = 8; /* start at rbp-8 */
    assign_variable_offsets(root, &next_offset);
    size_t total_bytes = next_offset > 8 ? (next_offset - 8) : 0;
    /* Align to 16 bytes for System V ABI */
    if (total_bytes > 0)
    {
      if (total_bytes % 16 != 0)
        total_bytes = ((total_bytes / 16) + 1) * 16;
      fprintf(file, "  sub rsp, %zu\n", total_bytes);
      /* Remember reserved space if needed elsewhere */
      stack_size += (total_bytes / 8);
    }
  }

  // Reset traversal visited set
  memset(visited_nodes_global, 0, sizeof(visited_nodes_global));
  visited_count_global = 0;
  
  // Pre-pass: mark EXIT argument nodes as visited to prevent code generation for them                                     
  void mark_exit_args(Node *n) {               
    if (!n) return;
    if (n->value && strcmp(n->value, "EXIT") == 0) {
      if (n->left && visited_count_global < 10000) {
        visited_nodes_global[visited_count_global++] = (void *)n->left;
        if (n->left->left && visited_count_global < 10000)
          visited_nodes_global[visited_count_global++] = (void *)n->left->left;
        if (n->left->right && visited_count_global < 10000)
          visited_nodes_global[visited_count_global++] = (void *)n->left->right;
      }
    }
    mark_exit_args(n->left);
    mark_exit_args(n->right);
  }
  mark_exit_args(root);
  
  // Traverse the top-level statements
  process_program(root, file);
  
  // Jump to exit handling
  fprintf(file, "  jmp end_program\n");
  
  // Now emit all collected string literals in data section
  if (string_literal_count > 0)
  {
    fprintf(file, "\nsection .data\n");
    for (int i = 0; i < string_literal_count; i++)
    {
      fprintf(file, "%s: db \"%s\", 10, 0\n", string_literals[i].label, string_literals[i].value);
    }
    fprintf(file, "section .text\n");
  }

  // Emit division-by-zero handler (no output)
  fprintf(file, "\n; --- Division by zero handler ---\n");
  fprintf(file, "section .text\n");
  fprintf(file, "div_by_zero:\n");
  fprintf(file, "  ; exit(1) via linux syscall\n");
  fprintf(file, "  mov rax, 60\n");
  fprintf(file, "  mov rdi, 1\n");
  fprintf(file, "  syscall\n");

  /* Emit a single exit syscall at program end. If an EXIT was seen during
     traversal we jump to label `end_program` and the generator will emit a
     label here which performs the syscall without overriding rdi (so the
     requested exit code is preserved). If no EXIT was emitted, write the
     normal exit sequence with status 0. */
  fprintf(file, "end_program:\n");
  if (exit_emitted)
  {
    /* Generate code to evaluate the stored exit argument (if any) at the
       end of the program so it runs after other statements like WRITE. */
    if (exit_node_arg)
    {
      /* If the stored node is an INT literal, emit mov rax, <val>. If it's an
         identifier, load from the stack. If it's an operator, generate the
         operator code and pop the result into rax. */
      if (exit_node_arg->type == INT)
      {
        fprintf(file, "  mov rdi, %s\n", exit_node_arg->value);
      }
      else if (exit_node_arg->type == IDENTIFIER)
      {
        size_t *var_pos = hashmap_get(&hashmap, exit_node_arg->value, strlen(exit_node_arg->value));
        if (!var_pos)
          SAFE_EXIT("Variable %s used in exit() is not declared", exit_node_arg->value);
        push_var(*var_pos, exit_node_arg->value, file);
        pop("rdi", file);
      }
      else if (exit_node_arg->type == OPERATOR)
      {
        generate_operator_code(exit_node_arg, file);
        pop("rdi", file);
      }
      else
      {
        /* Fallback: default to 0 */
        fprintf(file, "  xor rdi, rdi\n");
      }
    }
    else
    {
      /* No argument supplied; default to 0 */
      fprintf(file, "  xor rdi, rdi\n");
    }
    fprintf(file, "  mov rax, 60       ; syscall: exit\n");
    fprintf(file, "  syscall\n");
  }
  else
  {
    /* No explicit EXIT call; default to exit(0) */
    fprintf(file, "  xor rdi, rdi      ; status 0\n");
    fprintf(file, "  mov rax, 60       ; syscall: exit\n");
    fprintf(file, "  syscall\n");
  }

  fprintf(file, "  mov rsp, rbp\n");
  fprintf(file, "  pop rbp\n");

  // Cleanup resources
  fclose(file);

  // Free any remaining curly stack entries
  for (size_t i = 0; i < curly_stack_size; i++)
  {
    free(curly_stack[i]);
    curly_stack[i] = NULL;
  }
  curly_stack_size = 0;

  // Clean up hashmap and free all variable entries
  void *cleanup_context = NULL;
  hashmap_iterate_pairs(&hashmap, log_and_free_out_of_scope, cleanup_context);
  hashmap_destroy(&hashmap);

  // Reset all global state
  stack_size = 0;
  global_curly = 0;
  current_stack_size_size = 0;
  label_number = 0;
  loop_label_number = 0;
  text_label = 0;
}
