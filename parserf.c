#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexerf.h"
#include "parserf.h"

#define MAX_CURLY_STACK_LENGTH 64
#define MAX_TOKEN_DEPTH 1000

// Forward declarations of structures
typedef struct
{
    Token *start;
    Token *current;
    size_t depth;
    size_t max_depth;
} TokenContext;

// Global token context
static TokenContext token_ctx = {0};

typedef struct
{
    Node *content[MAX_CURLY_STACK_LENGTH];
    int top;
} curly_stack;

// Global curly brace stack for tracking block nesting
static curly_stack curly_stack_instance;

// Forward declarations of functions
void init_curly_stack(curly_stack *stack);
Node *peek_curly(curly_stack *stack);
void push_curly(curly_stack *stack, Node *element);
Node *pop_curly(curly_stack *stack);
void print_tree(Node *node, int indent, char *identifier);
Node *init_node(Node *node, char *value, TokenType type, size_t line_num);
void print_error(char *error_type, size_t line_number);
void handle_token_errors(char *error_text, Token *current_token, TokenType type);
Node *parse_expression(Token *current_token, Node *current_node);
Token *generate_operation_nodes(Token *current_token, Node *current_node);
Node *handle_exit_syscall(Node *root, Token *current_token, Node *current);
Node *create_variable_reusage(Token *current_token, Node *current);
Node *create_variables(Token *current_token, Node *current);
Token *generate_if_operation_nodes(Token *current_token, Node *current_node);
Token *generate_if_operation_nodes_right(Token *current_token, Node *current_node);
Node *create_if_statement(Token *current_token, Node *current);
Node *handle_write_node(Token *current_token, Node *current);
Node *parser(Token *tokens);

/* Token management functions */
void init_token_context(Token *tokens)
{
    token_ctx.start = tokens;
    token_ctx.current = tokens;
    token_ctx.depth = 0;
    token_ctx.max_depth = MAX_TOKEN_DEPTH;
}

Token *advance_token(void)
{
    if (!token_ctx.current || token_ctx.current->type == END_OF_TOKENS)
    {
        return NULL;
    }

    token_ctx.depth++;
    if (token_ctx.depth > token_ctx.max_depth)
    {
        fprintf(stderr, "ERROR: Maximum parsing depth exceeded. Possible infinite loop at line %zu\n",
                token_ctx.current->line_num);
        exit(1);
    }

    token_ctx.current++;
    return token_ctx.current;
}

Token *peek_next_token(void)
{
    if (!token_ctx.current || token_ctx.current->type == END_OF_TOKENS)
    {
        return NULL;
    }
    return token_ctx.current + 1;
}

Token *get_current_token(void)
{
    return token_ctx.current;
}

/* Allow updating the current token pointer (used after expression parsing helpers)
 * This keeps the token context consistent across generator functions.
 */
void set_current_token(Token *token)
{
    if (!token)
        return;
    token_ctx.current = token;
}

/* Initialize a curly_stack (must be called before use) */
void init_curly_stack(curly_stack *stack)
{
    if (!stack)
        return;
    stack->top = -1;
}

Node *peek_curly(curly_stack *stack)
{
    if (!stack || stack->top < 0)
        return NULL;
    return stack->content[stack->top];
}

void push_curly(curly_stack *stack, Node *element)
{
    if (!stack)
        return;
    if (stack->top + 1 >= MAX_CURLY_STACK_LENGTH)
    {
        printf("ERROR: curly stack overflow\n");
        exit(1);
    }
    stack->top++;
    stack->content[stack->top] = element;
}

Node *pop_curly(curly_stack *stack)
{
    if (!stack || stack->top < 0)
        return NULL;
    Node *result = stack->content[stack->top];
    stack->top--;
    return result;
}

void print_tree(Node *node, int indent, char *identifier)
{
    if (node == NULL)
    {
        return;
    }
    for (int i = 0; i < indent; i++)
    {
        printf(" ");
    }
    printf("%s -> %s\n", identifier, node->value ? node->value : "(null)");
    // for (size_t i = 0; node->value[i] != '\0'; i++)
    // {
    //     printf("%c", node->value[i]);
    // }
    // printf("\n");
    print_tree(node->left, indent + 1, "left");
    print_tree(node->right, indent + 1, "right");
}

Node *init_node(Node *node, char *value, TokenType type, size_t line_num)
{
    (void)node;
    node = malloc(sizeof(Node));
    if (!node)
    {
        fprintf(stderr, "ERROR: Memory allocation failed for node\n");
        exit(1);
    }

    node->line_num = line_num; // Set the provided line number
    node->right = NULL;
    node->left = NULL;

    // Duplicate the string value to prevent dangling pointers
    if (value)
    {
        node->value = strdup(value);
        if (!node->value)
        {
            free(node);
            fprintf(stderr, "ERROR: Memory allocation failed for node value\n");
            exit(1);
        }
    }
    else
    {
        node->value = NULL;
    }

    node->type = type;
    node->left = NULL;
    node->right = NULL;
    return node;
}

void print_error(char *error_type, size_t line_number)
{
    printf("ERROR: %s on line number: %zu\n", error_type, line_number);
    exit(1);
}

Node *parse_expression(Token *current_token, Node *current_node)
{
    (void)current_node; // current_node is unused but kept for API compatibility

    Token *token = get_current_token();
    if (!token)
    {
        print_error("Unexpected NULL token", 0);
    }

    // First operand
    if (token->type == END_OF_TOKENS)
    {
        return NULL;
    }

    // Create node for first operand
    Node *left = init_node(NULL, token->value, token->type, token->line_num);
    token = advance_token();

    // Process operators and build expression tree
    while (token && token->type == OPERATOR)
    {
        // Create operator node
        Node *op = init_node(NULL, token->value, token->type, token->line_num);
        token = advance_token();

        if (!token || token->type == END_OF_TOKENS)
        {
            print_error("Malformed expression (missing right operand)", token_ctx.current->line_num);
        }

        // Create right operand node
        Node *right = init_node(NULL, token->value, token->type, token->line_num);
        token = advance_token();

        // Build expression tree
        op->left = left;
        op->right = right;
        left = op; // The operator becomes the new left node for next iteration
    }

    return left;
}

Token *generate_operation_nodes(Token *current_token, Node *current_node)
{
    static int op_depth = 0;
    op_depth++;

    // Prevent deep recursion
    if (op_depth > 10)
    {
        print_error("Expression too complex or recursive", token_ctx.current->line_num);
    }

    Token *token = get_current_token();
    if (!token || !current_node)
    {
        return NULL;
    }

    if (token->type == END_OF_TOKENS)
    {
        return token;
    }

    // Create operator node (e.g., +, -, *, /)
    Node *oper_node = init_node(NULL, token->value, OPERATOR, token->line_num);
    current_node->left = oper_node;
    current_node = oper_node;

    // Get the previous token for left operand (we'll restore position later)
    Token *prev = token_ctx.current - 1;
    if (!prev || prev < token_ctx.start)
    {
        print_error("Missing operand before operator", token->line_num);
    }

    // Create left operand node
    if (prev->type == INT || prev->type == IDENTIFIER)
    {
        Node *left_operand = init_node(NULL, prev->value, prev->type, prev->line_num);
        current_node->left = left_operand;
    }
    else
    {
        print_error("Expected integer or identifier before operator", prev->line_num);
    }

    // Move to next token after operator
    token = advance_token();
    if (!token || token->type == END_OF_TOKENS)
    {
        print_error("Unexpected end of expression", token_ctx.current->line_num);
    }

    // Handle right operand
    if (token->type == INT || token->type == IDENTIFIER)
    {
        current_node->right = init_node(NULL, token->value, token->type, token->line_num);
        token = advance_token();
    }
    else
    {
        print_error("Expected integer or identifier after operator", token->line_num);
    }

    op_depth--;
    return token;
}

Node *handle_exit_syscall(Node *root, Token *unused_token, Node *current)
{
    (void)unused_token; // Parameter kept for API compatibility

    Token *token = get_current_token();
    if (!token || token->type == END_OF_TOKENS)
    {
        return current;
    }

    // Create and attach EXIT node
    Node *exit_node = init_node(NULL, token->value, KEYWORD, token->line_num);
    if (current)
    {
        current->right = exit_node;
    }
    current = exit_node;

    // Expect '('
    token = advance_token();
    if (!token || token->type != SEPARATOR || strcmp(token->value, "(") != 0)
    {
        print_error("Expected '(' after 'exit'", token ? token->line_num : 0);
    }

    // Create open parenthesis node
    Node *open_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    current->left = open_paren_node;
    Node *expr_parent = open_paren_node;

    // Handle argument
    token = advance_token();
    if (!token || token->type == END_OF_TOKENS)
    {
        print_error("Expected argument in exit()", token_ctx.current->line_num);
    }

    if (token->type == INT || token->type == IDENTIFIER)
    {
        // Simple argument (like exit(1);)
        expr_parent->left = init_node(NULL, token->value, token->type, token->line_num);
        token = advance_token();
    }
    else if (token->type == OPERATOR)
    {
        // Expression like exit(a + b);
        token = generate_operation_nodes(token, expr_parent);
    }
    else
    {
        print_error("Invalid argument in exit()", token->line_num);
    }

    // Expect ')'
    if (!token || token->type != SEPARATOR || strcmp(token->value, ")") != 0)
    {
        print_error("Expected ')' after exit argument", token->line_num);
    }

    Node *close_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    expr_parent->right = close_paren_node;

    // Expect ';'
    token = advance_token();
    if (!token || token->type != SEPARATOR || strcmp(token->value, ";") != 0)
    {
        print_error("Expected ';' after exit()", token->line_num);
    }

    Node *semi_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    current->right = semi_node;
    current = semi_node;

    return current;
}

void handle_token_errors(char *error_text, Token *unused_token, TokenType expected_type)
{
    (void)unused_token; // Parameter kept for API compatibility

    Token *token = get_current_token();
    if (!token || token->type != expected_type)
    {
        print_error(error_text, token ? token->line_num : 0);
    }
}

Node *create_variable_reusage(Token *unused_token, Node *current)
{
    (void)unused_token; // Parameter kept for API compatibility

    static int depth = 0;
    if (depth > 50)
    {
        print_error("Expression too complex or possible recursion", token_ctx.current->line_num);
    }
    depth++;

    Token *token = get_current_token();
    if (!token || !current)
    {
        print_error("Internal parser error: null token or node", token ? token->line_num : 0);
    }

    if (!token->value)
    {
        print_error("Invalid token: missing value", token->line_num);
    }

    // Create and attach identifier node
    Node *main_identifier_node = init_node(NULL, token->value, IDENTIFIER, token->line_num);
    current->left = main_identifier_node;
    current = main_identifier_node;

    // Expect '=' operator
    token = advance_token();
    if (!token || token->type != OPERATOR || strcmp(token->value, "=") != 0)
    {
        print_error("Invalid Variable Syntax: expected '='", token ? token->line_num : 0);
    }

    // Create and attach equals node
    Node *equals_node = init_node(NULL, token->value, OPERATOR, token->line_num);
    main_identifier_node->right = equals_node;
    current = equals_node;

    // Get first operand (INT or IDENTIFIER)
    token = advance_token();
    if (!token || (token->type != INT && token->type != IDENTIFIER))
    {
        print_error("Invalid Syntax After Equals: expected INT or IDENTIFIER", token ? token->line_num : 0);
    }

    // Create and attach first operand node
    Node *first_operand = init_node(NULL, token->value, token->type, token->line_num);
    equals_node->left = first_operand;

    // Check for operator after first operand
    token = advance_token();
    if (!token)
    {
        print_error("Unexpected end of tokens", 0);
    }

    // Handle operation chain if present
    if (token->type == OPERATOR)
    {
        token = generate_operation_nodes(token, equals_node);
        if (!token)
        {
            print_error("Invalid syntax after expression", token_ctx.current->line_num);
        }
    }

    // Expect semicolon
    if (token->type != SEPARATOR || strcmp(token->value, ";") != 0)
    {
        print_error("Expected ';' after expression", token->line_num);
    }

    // Create and attach semicolon node
    Node *semi_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    main_identifier_node->right = semi_node;
    current = semi_node;

    // Reset recursion depth counter
    depth--;
    return current;
}

Node *create_variables(Token *unused_token, Node *current)
{
    (void)unused_token; // Parameter kept for API compatibility

    static int var_count = 0;
    Token *token = get_current_token();

    // Safety checks
    if (!token || !current)
    {
        print_error("Internal parser error: null token or node", token ? token->line_num : 0);
    }

    // Prevent excessive variable declarations
    if (++var_count > 100)
    {
        print_error("Too many variable declarations", token->line_num);
    }

    // Check token context depth
    if (token_ctx.depth > token_ctx.max_depth)
    {
        print_error("Possible infinite loop in variable creation", token->line_num);
    }

    // Create variable keyword node (e.g. 'int')
    Node *var_node = init_node(NULL, token->value, KEYWORD, token->line_num);
    current->left = var_node;
    current = var_node;

    // Expect an identifier after 'int'
    token = advance_token();
    handle_token_errors("Invalid syntax after type keyword", NULL, IDENTIFIER);

    Node *identifier_node = init_node(NULL, token->value, IDENTIFIER, token->line_num);
    current->left = identifier_node;
    current = identifier_node;

    // Expect '='
    token = advance_token();
    handle_token_errors("Invalid syntax after identifier", NULL, OPERATOR);

    if (strcmp(token->value, "=") != 0)
    {
        print_error("Invalid variable syntax: expected '='", token->line_num);
    }

    Node *equals_node = init_node(NULL, token->value, OPERATOR, token->line_num);
    identifier_node->left = equals_node;
    current = equals_node;

    // Expect value after '='
    token = advance_token();
    if (!token || token->type == END_OF_TOKENS)
    {
        print_error("Invalid syntax after '='", token ? token->line_num : 0);
    }

    if (token->type != INT && token->type != IDENTIFIER)
    {
        print_error("Expected value or identifier after '='", token->line_num);
    }

    // Create and attach first operand node
    Node *first_operand = init_node(NULL, token->value, token->type, token->line_num);
    current->left = first_operand;

    // Check for and handle any operations
    token = advance_token();
    if (token && token->type == OPERATOR)
    {
        token = generate_operation_nodes(token, current);
        if (!token)
        {
            print_error("Invalid syntax after expression", token_ctx.current->line_num);
        }
    }

    // Expect ';'
    handle_token_errors("Expected ';' after expression", NULL, SEPARATOR);

    // Create and attach semicolon node
    Node *semi_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    var_node->right = semi_node;

    // Cleanup and return
    var_count--; // Decrement declaration count as we're done
    return semi_node;
}

Token *generate_if_operation_nodes(Token *current_token, Node *current_node)
{
    if (!current_token || !current_node)
        print_error("Internal parser error in generate_if_operation_nodes", 0);

    /* Create operator node (e.g. <, >, ==) */
    Node *oper_node = init_node(NULL, current_token->value, OPERATOR, current_token->line_num);
    current_node->left->left = oper_node;

    /* Go back to get left operand safely */
    if (current_token == NULL || current_token == (Token *)-1)
        print_error("Invalid token pointer before operator", 0);

    Token *prev_token = current_token - 1;
    if (!prev_token || prev_token->type == END_OF_TOKENS)
        print_error("Missing left operand in if-condition", current_token->line_num);

    Node *left_expr = init_node(NULL, prev_token->value, prev_token->type, prev_token->line_num);
    oper_node->left = left_expr;

    /* Move ahead to get right operand */
    current_token++;
    if (!current_token)
        print_error("Unexpected end of tokens after operator", 0);

    while (current_token && current_token->type != END_OF_TOKENS)
    {
        if (current_token->type == INT || current_token->type == IDENTIFIER)
        {
            Node *right_expr = init_node(NULL, current_token->value, current_token->type, current_token->line_num);
            oper_node->right = right_expr;
        }
        else if (current_token->type == OPERATOR && strcmp(current_token->value, "=") != 0)
        {
            /* Chain another operator if needed */
            Node *next_oper = init_node(NULL, current_token->value, OPERATOR, current_token->line_num);
            oper_node->right = next_oper;
            oper_node = next_oper;
        }
        else
        {
            /* Stop if we reach '=', ')' or a separator */
            break;
        }
        current_token++;
    }

    return current_token;
}

Token *generate_if_operation_nodes_right(Token *current_token, Node *current_node)
{
    if (!current_token || !current_node)
        print_error("Internal parser error in generate_if_operation_nodes_right", 0);

    /* Create operator node (e.g. <, >, ==) */
    Node *oper_node = init_node(NULL, current_token->value, OPERATOR, current_token->line_num);
    current_node->left->right = oper_node;

    /* Get left operand safely */
    Token *prev_token = current_token - 1;
    if (!prev_token)
        print_error("Missing left operand in if-condition (right side)", 0);

    Node *left_expr = init_node(NULL, prev_token->value, prev_token->type, prev_token->line_num);
    oper_node->left = left_expr;

    /* Advance to get right operand(s) */
    current_token++;
    if (!current_token)
        print_error("Unexpected end of tokens in if-condition (right side)", 0);

    while (current_token && current_token->type != END_OF_TOKENS)
    {
        if (current_token->type == INT || current_token->type == IDENTIFIER)
        {
            Node *right_expr = init_node(NULL, current_token->value, current_token->type, current_token->line_num);
            oper_node->right = right_expr;
        }
        else if (current_token->type == OPERATOR && strcmp(current_token->value, "=") != 0)
        {
            Node *next_oper = init_node(NULL, current_token->value, OPERATOR, current_token->line_num);
            oper_node->right = next_oper;
            oper_node = next_oper;
        }
        else
        {
            break;
        }
        current_token++;
    }

    return current_token;
}

Node *create_if_statement(Token *unused_token, Node *current)
{
    (void)unused_token; // Parameter kept for API compatibility

    Token *token = get_current_token();
    if (!token || !current)
    {
        print_error("Internal parser error: null token or node in create_if_statement",
                    token ? token->line_num : 0);
    }

    // Create IF node and attach
    Node *if_node = init_node(NULL, token->value, token->type, token->line_num);
    current->left = if_node;
    current = if_node;

    // Expect '(' separator
    token = advance_token();
    handle_token_errors("Expected '(' after if", NULL, SEPARATOR);

    Node *open_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    current->left = open_paren_node;
    current = open_paren_node;

    // Get first token of condition
    token = advance_token();
    if (!token || token->type == END_OF_TOKENS)
    {
        print_error("Invalid if condition: unexpected end", token_ctx.current->line_num);
    }

    // Validate first token of condition
    if (!(token->type == IDENTIFIER || token->type == INT || token->type == OPERATOR))
    {
        print_error("Expected identifier or integer in if condition", token->line_num);
    }

    // Find comparator token
    Token *scan = token_ctx.current;
    size_t scan_depth = 0;
    while (scan && scan->type != END_OF_TOKENS && scan->type != COMP && scan_depth < 100)
    {
        scan++;
        scan_depth++;
    }

    if (!scan || scan->type != COMP || scan_depth >= 100)
    {
        print_error("Expected comparator in if condition", token->line_num);
    }

    // Create comparator node
    Node *comp_node = init_node(NULL, scan->value, scan->type, scan->line_num);
    open_paren_node->left = comp_node;

    // Find left operand of comparison
    Token *left_tok = token; // Start from the first token of condition
    while (left_tok && left_tok->type != END_OF_TOKENS &&
           !(left_tok->type == INT || left_tok->type == IDENTIFIER))
    {
        left_tok++;
        if (left_tok >= scan)
        {
            break;
        }
    }

    if (!left_tok || !(left_tok->type == INT || left_tok->type == IDENTIFIER))
    {
        print_error("Expected left-hand operand for comparator", scan->line_num);
    }

    // Create and attach left operand node
    comp_node->left = init_node(NULL, left_tok->value, left_tok->type, left_tok->line_num); /* Now position at token immediately after COMP to parse right-hand side */
    Token *right_start = scan + 1;
    if (!right_start || right_start->type == END_OF_TOKENS)
        print_error("ERROR: Unexpected end after comparator", scan->line_num);

    // If right operand is INT/IDENTIFIER, check for operation chain
    if (right_start->type == INT || right_start->type == IDENTIFIER)
    {
        Token *peek = right_start + 1;
        if (peek && peek->type == OPERATOR)
        {
            // Handle operation chain starting with operator
            token = generate_operation_nodes(peek, comp_node);
            if (!token || token->type == END_OF_TOKENS)
            {
                print_error("Invalid right-hand expression", right_start->line_num);
            }
            // Update token_ctx with post-operation position
            set_current_token(token);
        }
        else
        {
            /* simple right operand */
            comp_node->right = init_node(NULL, right_start->value, right_start->type, right_start->line_num);
        }
    }
    else if (right_start->type == OPERATOR)
    {
        /* If operator appears right after comparator, treat as complex expression starting at operator */
        Token *after_ops = generate_if_operation_nodes_right(right_start, comp_node);
        if (!after_ops)
            print_error("Invalid syntax in if right-hand side", right_start->line_num);
    }
    else
    {
        /* If we hit something else, try to continue scanning until a separator or end */
        print_error("Invalid token after comparator in if condition", right_start->line_num);
    }

    // Find closing parenthesis and open brace
    token = advance_token();
    if (!token || strcmp(token->value, ")") != 0)
    {
        print_error("Expected closing parenthesis in if condition", token ? token->line_num : 0);
    }

    // Create and attach closing parenthesis node
    Node *close_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    open_paren_node->right = close_paren_node;
    current = close_paren_node;

    // Look for opening brace
    token = advance_token();
    if (!token || strcmp(token->value, "{") != 0)
    {
        print_error("Expected opening brace for if body", token ? token->line_num : 0);
    }

    // Register if statement for brace matching
    push_curly(&curly_stack_instance, if_node);

    return current;
}

// Node *handle_write_node(Token *current_token, Node *current)
// {
//     Node *write_node = init_node(NULL, current_token->value, current_token->type);
//     current->left = write_node;
//     current = write_node;

//     current_token++;

//     handle_token_errors("ERROR: Expected (", current_token, SEPARATOR);

//     current_token++;
//     if (current_token->type != STRING && current_token->type != IDENTIFIER)
//     {
//         handle_token_errors("ERROR: Expected String Literal", current_token, STRING);
//     }

//     Node *string_node = init_node(NULL, current_token->value, current_token->type);
//     current->left = string_node;

//     current_token++;

//     handle_token_errors("ERROR: Expected ,", current_token, SEPARATOR);

//     current_token++;

//     Node *number_node = init_node(NULL, current_token->value, current_token->type);
//     current->right = number_node;

//     current_token++;

//     handle_token_errors("ERROR: Expected )", current_token, SEPARATOR);

//     current_token++;

//     if (strcmp(current_token->value, ";") != 0)
//     {
//         print_error("ERROR: Expected ;", current_token->line_num);
//     }

//     Node *semi_node = init_node(NULL, current_token->value, current_token->type);
//     number_node->right = semi_node;
//     current = semi_node;
//     return current;
// }

Node *handle_write_node(Token *unused_token, Node *current)
{
    (void)unused_token; // keep API consistent

    Token *token = get_current_token();
    if (!token || !current)
        print_error("Internal parser error: null token in handle_write_node", token ? token->line_num : 0);

    // Create write node and attach
    Node *write_node = init_node(NULL, token->value, token->type, token->line_num);
    current->left = write_node;
    current = write_node;

    // Expect '('
    token = advance_token();
    handle_token_errors("ERROR: Expected (' after write", NULL, SEPARATOR);
    Node *open_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    current->left = open_paren_node;
    Node *arg_parent = open_paren_node;

    // Move to argument
    token = advance_token();
    if (!token || (token->type != STRING && token->type != IDENTIFIER))
        print_error("ERROR: Expected String literal or Identifier inside write()", token ? token->line_num : 0);

    Node *arg1_node = init_node(NULL, token->value, token->type, token->line_num);
    arg_parent->left = arg1_node;

    // Advance to next token
    token = advance_token();

    // Optional comma for second argument
    if (token && token->type == SEPARATOR && token->value && strcmp(token->value, ",") == 0)
    {
        token = advance_token(); // move to second arg
        if (!token || (token->type != INT && token->type != IDENTIFIER))
            print_error("ERROR: Expected Number or Identifier after ',' in write()", token ? token->line_num : 0);

        Node *arg2_node = init_node(NULL, token->value, token->type, token->line_num);
        arg_parent->right = arg2_node;
        token = advance_token();
    }

    // Expect ')'
    handle_token_errors("ERROR: Expected ')' after write arguments", NULL, SEPARATOR);
    Node *close_paren_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    open_paren_node->right = close_paren_node;

    // Expect ';'
    token = advance_token();
    handle_token_errors("ERROR: Expected ';' after write()", NULL, SEPARATOR);
    Node *semi_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
    current->right = semi_node;
    current = semi_node;

    return current;
}

Node *parser(Token *tokens)
{
    if (!tokens)
    {
        print_error("Parser received NULL tokens", 0);
    }

    // Initialize token tracking
    init_token_context(tokens);
    Token *token = get_current_token();

    // Initialize the AST
    Node *root = init_node(NULL, "PROGRAM", BEGINNING, 0);
    Node *current = root;
    Node *open_curly = NULL;

    // Initialize global curly brace tracking
    init_curly_stack(&curly_stack_instance);

    while (token && token->type != END_OF_TOKENS)
    {
        if (!current)
        {
            break;
        }

        // Safety check for parsing depth
        if (token_ctx.depth > token_ctx.max_depth)
        {
            print_error("Too many tokens or possible infinite loop", token->line_num);
        }

        switch (token->type)
        {
        case KEYWORD:
            if (token->value)
            {
                if (strcmp(token->value, "EXIT") == 0)
                {
                    current = handle_exit_syscall(root, token, current);
                    token = get_current_token();
                }
                else if (strcmp(token->value, "INT") == 0)
                {
                    current = create_variables(token, current);
                    token = get_current_token();
                }
                else if (strcmp(token->value, "IF") == 0)
                {
                    current = create_if_statement(token, current);
                    token = get_current_token();
                }
                else if (strcmp(token->value, "WHILE") == 0)
                {
                    Node *while_node = init_node(NULL, token->value, token->type, token->line_num);
                    current->left = while_node;
                    current = create_if_statement(token, while_node);
                    token = get_current_token();
                }
                else if (strcmp(token->value, "WRITE") == 0)
                {
                    current = handle_write_node(token, current);
                    token = get_current_token();
                }
            }
            break;

        case SEPARATOR:
            if (token->value && strcmp(token->value, "{") == 0)
            {
                Node *open_curly_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
                current->right = open_curly_node;
                push_curly(&curly_stack_instance, open_curly_node);
                current = open_curly_node;
                token = advance_token();
            }
            else if (token->value && strcmp(token->value, "}") == 0)
            {
                open_curly = pop_curly(&curly_stack_instance);
                if (!open_curly)
                {
                    print_error("Unexpected closing brace '}'", token->line_num);
                }
                Node *close_curly_node = init_node(NULL, token->value, SEPARATOR, token->line_num);
                open_curly->right = close_curly_node;
                current = close_curly_node;
                token = advance_token();
            }
            else
            {
                token = advance_token(); // Skip other separators
            }
            break;

        case IDENTIFIER:
        {
            // Get previous token if there is one
            Token *prev = NULL;
            if (token_ctx.current > token_ctx.start)
            {
                prev = token_ctx.current - 1;
            }

            // Check if this identifier starts a new statement
            if (prev && prev->type == SEPARATOR &&
                (strcmp(prev->value, ";") == 0 ||
                 strcmp(prev->value, "{") == 0 ||
                 strcmp(prev->value, "}") == 0))
            {
                current = create_variable_reusage(token, current);
            }
            token = advance_token();
        }
        break;

        default:
            token = advance_token(); // Skip unhandled token types
            break;
        }

        // Safety check - if we haven't moved forward, force advance
        if (token == get_current_token())
        {
            token = advance_token();
        }
    }

    return root;
}
