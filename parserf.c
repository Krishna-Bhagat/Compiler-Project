#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexerf.h"

#define MAX_CURLY_STACK_LENGTH 64

typedef struct Node
{
    char *value;
    TokenType type;
    struct Node *right;
    struct Node *left;
} Node;

typedef struct
{
    Node *content[MAX_CURLY_STACK_LENGTH];
    int top;
} curly_stack;

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

Node *init_node(Node *node, char *value, TokenType type)
{
    (void)node;
    node = malloc(sizeof(Node));
    if (!node)
    {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        exit(1);
    }
    node->value = value; // reference lexer's token string
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
    (void)current_node;
    if (!current_token)
        return NULL;

    // First operand
    if (current_token->type == END_OF_TOKENS)
        return NULL;

    Node *left = init_node(NULL, current_token->value, current_token->type);
    current_token++;

    while (current_token && current_token->type == OPERATOR)
    {
        // Operator node
        Node *op = init_node(NULL, current_token->value, current_token->type);
        current_token++;

        if (!current_token || current_token->type == END_OF_TOKENS)
        {
            /* malformed expression: no right operand */
            print_error("Malformed expression (missing right operand)", 0);
        }

        // Right operand
        Node *right = init_node(NULL, current_token->value, current_token->type);
        current_token++;

        // Attach children directly
        op->left = left;
        op->right = right;

        // That operator node becomes the new "left" for the next iteration
        left = op;
    }

    return left; // final expression tree
}

Token *generate_operation_nodes(Token *current_token, Node *current_node)
{
    if (!current_token || !current_node)
        return current_token;
    if (current_token->type == END_OF_TOKENS)
        return current_token;
    Node *oper_node = init_node(NULL, current_token->value, OPERATOR);
    current_node->left = oper_node;
    current_node = oper_node;

    // Move to previous token safely (for left operand)
    if (current_token > 0)
        current_token--;
    else
    {
        fprintf(stderr, "ERROR: Missing operand before operator\n");
        exit(1);
    }

    if (current_token && current_token->type == INT)
    {
        current_node->left = init_node(NULL, current_token->value, INT);
    }
    else if (current_token && current_token->type == IDENTIFIER)
    {
        current_node->left = init_node(NULL, current_token->value, IDENTIFIER);
    }
    else
    {
        printf("ERROR: expected int or identifier\n");
        exit(1);
    }
    current_token++;
    current_token++;

    if (!current_token || current_token->type == END_OF_TOKENS)
        return current_token;

    /* If immediate operand is INT or IDENTIFIER, attach it as right child */
    if (current_token->type == INT || current_token->type == IDENTIFIER)
    {
        current_node->right = init_node(NULL, current_token->value, current_token->type);
        current_token++;
    }
    else
    {
        fprintf(stderr, "ERROR: expected INT or IDENTIFIER after operator\n");
        exit(1);
    }

    /* Continue parsing sequences like: OP (right) OP (right) ... */
    while (current_token && current_token->type != END_OF_TOKENS)
    {
        if (current_token->type == OPERATOR)
        {
            Node *next_oper_node = init_node(NULL, current_token->value, OPERATOR);
            current_node->right = next_oper_node;
            current_node = next_oper_node;

            /* next token must be INT or IDENTIFIER */
            current_token++;
            if (!current_token || current_token->type == END_OF_TOKENS)
            {
                fprintf(stderr, "ERROR: expected operand after operator\n");
                exit(1);
            }
            if (current_token->type == INT || current_token->type == IDENTIFIER)
            {
                current_node->left = init_node(NULL, current_token->value, current_token->type);
                current_token++;
            }
            else
            {
                fprintf(stderr, "ERROR: expected INT or IDENTIFIER after operator\n");
                exit(1);
            }
        }
        else
        {
            /* token is not an operator, stop operation parsing */
            break;
        }
    }

    return current_token;
}

Node *handle_exit_syscall(Node *root, Token *current_token, Node *current)
{
    if (!current_token || current_token->type == END_OF_TOKENS)
        return current;
    Node *exit_node = init_node(NULL, current_token->value, KEYWORD);
    if (current)
        current->right = exit_node;
    current = exit_node;
    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
    {
        print_error("Invalid Syntax on OPEN", current_token->line_num);
    }
    // Expect '('
    if (!(current_token->type == SEPARATOR && strcmp(current_token->value, "(") == 0))
        print_error("Expected '(' after 'exit'", current_token->line_num);

    Node *open_paren_node = init_node(NULL, current_token->value, SEPARATOR);
    current->left = open_paren_node;
    Node *expr_parent = open_paren_node;
    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
    {
        print_error("Expected INT or IDENTIFIER inside 'exit()'", current_token->line_num);
    }
    if (current_token->type == INT || current_token->type == IDENTIFIER)
    {
        // Simple argument (like exit(1);)
        expr_parent->left = init_node(NULL, current_token->value, current_token->type);
        current_token++;
    }
    else if (current_token->type == OPERATOR)
    {
        // Expression like exit(a + b);
        current_token = generate_operation_nodes(current_token, expr_parent);
    }
    else
    {
        print_error("Invalid argument inside 'exit()'", current_token->line_num);
    }

    // Expect ')'
    if (!current_token || !(current_token->type == SEPARATOR && strcmp(current_token->value, ")") == 0))
        print_error("Expected ')' after 'exit' argument", current_token->line_num);

    Node *close_paren_node = init_node(NULL, current_token->value, SEPARATOR);
    expr_parent->right = close_paren_node;

    current_token++;

    // Expect ';'
    if (!current_token || !(current_token->type == SEPARATOR && strcmp(current_token->value, ";") == 0))
        print_error("Expected ';' after 'exit()'", current_token->line_num);

    Node *semi_node = init_node(NULL, current_token->value, SEPARATOR);
    current->right = semi_node;
    current = semi_node;

    return current;
}

void handle_token_errors(char *error_text, Token *current_token, TokenType type)
{
    if (!current_token || current_token->type != type)
    {
        print_error(error_text, current_token->line_num);
    }
}

Node *create_variable_reusage(Token *current_token, Node *current)
{
    if (!current_token || !current)
        print_error("Internal parser error: null token or node", 0);

    /* Create main identifier node and attach */
    Node *main_identifier_node = init_node(NULL, current_token->value, IDENTIFIER);
    current->left = main_identifier_node;
    current = main_identifier_node;

    /* Advance to token after identifier */
    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
        print_error("Invalid syntax after identifier", current_token ? current_token->line_num : 0);

    /* Expect '=' operator */
    if (current_token->type != OPERATOR || !current_token->value || strcmp(current_token->value, "=") != 0)
        print_error("Invalid Variable Syntax: expected '='", current_token->line_num);

    /* Create equals node and attach under identifier (right child keeps structure clear) */
    Node *equals_node = init_node(NULL, current_token->value, OPERATOR);
    main_identifier_node->right = equals_node;
    current = equals_node;

    /* Move to token after '=' */
    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
        print_error("Invalid Syntax After Equals", current_token ? current_token->line_num : 0);

    /* The next token should be an INT or IDENTIFIER (first operand) */
    if (current_token->type != INT && current_token->type != IDENTIFIER)
        print_error("Invalid Syntax After Equals: expected INT or IDENTIFIER", current_token->line_num);

    /* Attach first operand as left child of equals */
    Node *first_operand = init_node(NULL, current_token->value, current_token->type);
    equals_node->left = first_operand;

    /* Advance to token after first operand */
    current_token++;
    if (!current_token)
        print_error("Unexpected end of tokens", 0);

    /* If there's an operator after the first operand, build the operation chain */
    if (current_token->type == OPERATOR)
    {
        /* We pass the operator token and the equals_node (whose left child is set) */
        current_token = generate_operation_nodes(current_token, equals_node);

        /* generate_operation_nodes returns the token after the operation chain */
        if (!current_token || current_token->type == END_OF_TOKENS)
            print_error("Invalid Syntax After Expression", current_token ? current_token->line_num : 0);
    }

    /* At this point we expect a separator ';' */
    if (current_token->type != SEPARATOR || !current_token->value || strcmp(current_token->value, ";") != 0)
        print_error("Invalid Syntax After Expression: expected ';'", current_token->line_num);

    /* Create semicolon node and attach to the main identifier node's right (to mark statement end) */
    Node *semi_node = init_node(NULL, current_token->value, SEPARATOR);
    main_identifier_node->right = semi_node;
    current = semi_node;

    return current;
}

Node *create_variables(Token *current_token, Node *current)
{
    if (!current_token || !current)
        print_error("Internal parser error: null token or node", 0);

    /* Create variable keyword node (e.g. 'int') */
    Node *var_node = init_node(NULL, current_token->value, KEYWORD);
    current->left = var_node;
    current = var_node;

    /* Expect an identifier after 'int' */
    current_token++;
    handle_token_errors("Invalid syntax after type keyword", current_token, IDENTIFIER);

    Node *identifier_node = init_node(NULL, current_token->value, IDENTIFIER);
    current->left = identifier_node;
    current = identifier_node;

    /* Expect '=' */
    current_token++;
    handle_token_errors("Invalid syntax after identifier", current_token, OPERATOR);

    if (strcmp(current_token->value, "=") != 0)
        print_error("Invalid variable syntax: expected '='", current_token->line_num);

    Node *equals_node = init_node(NULL, current_token->value, OPERATOR);
    identifier_node->left = equals_node;
    current = equals_node;

    /* Expect value after '=' */
    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
        print_error("Invalid syntax after '='", current_token ? current_token->line_num : 0);

    if (current_token->type != INT && current_token->type != IDENTIFIER)
        print_error("Expected value or identifier after '='", current_token->line_num);

    /* Attach first operand */
    Node *first_operand = init_node(NULL, current_token->value, current_token->type);
    current->left = first_operand;

    /* Check for further operations */
    current_token++;
    if (current_token && current_token->type == OPERATOR)
    {
        current_token = generate_operation_nodes(current_token, current);

        if (!current_token || current_token->type == END_OF_TOKENS)
            print_error("Invalid syntax after expression", current_token ? current_token->line_num : 0);
    }

    /* Expect ';' */
    handle_token_errors("Expected ';' after expression", current_token, SEPARATOR);

    Node *semi_node = init_node(NULL, current_token->value, SEPARATOR);
    var_node->right = semi_node;

    return semi_node;
}

Token *generate_if_operation_nodes(Token *current_token, Node *current_node)
{
    if (!current_token || !current_node)
        print_error("Internal parser error in generate_if_operation_nodes", 0);

    /* Create operator node (e.g. <, >, ==) */
    Node *oper_node = init_node(NULL, current_token->value, OPERATOR);
    current_node->left->left = oper_node;

    /* Go back to get left operand safely */
    if (current_token == NULL || current_token == (Token *)-1)
        print_error("Invalid token pointer before operator", 0);

    Token *prev_token = current_token - 1;
    if (!prev_token || prev_token->type == END_OF_TOKENS)
        print_error("Missing left operand in if-condition", current_token->line_num);

    Node *left_expr = init_node(NULL, prev_token->value, prev_token->type);
    oper_node->left = left_expr;

    /* Move ahead to get right operand */
    current_token++;
    if (!current_token)
        print_error("Unexpected end of tokens after operator", 0);

    while (current_token && current_token->type != END_OF_TOKENS)
    {
        if (current_token->type == INT || current_token->type == IDENTIFIER)
        {
            Node *right_expr = init_node(NULL, current_token->value, current_token->type);
            oper_node->right = right_expr;
        }
        else if (current_token->type == OPERATOR && strcmp(current_token->value, "=") != 0)
        {
            /* Chain another operator if needed */
            Node *next_oper = init_node(NULL, current_token->value, OPERATOR);
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
    Node *oper_node = init_node(NULL, current_token->value, OPERATOR);
    current_node->left->right = oper_node;

    /* Get left operand safely */
    Token *prev_token = current_token - 1;
    if (!prev_token)
        print_error("Missing left operand in if-condition (right side)", 0);

    Node *left_expr = init_node(NULL, prev_token->value, prev_token->type);
    oper_node->left = left_expr;

    /* Advance to get right operand(s) */
    current_token++;
    if (!current_token)
        print_error("Unexpected end of tokens in if-condition (right side)", 0);

    while (current_token && current_token->type != END_OF_TOKENS)
    {
        if (current_token->type == INT || current_token->type == IDENTIFIER)
        {
            Node *right_expr = init_node(NULL, current_token->value, current_token->type);
            oper_node->right = right_expr;
        }
        else if (current_token->type == OPERATOR && strcmp(current_token->value, "=") != 0)
        {
            Node *next_oper = init_node(NULL, current_token->value, OPERATOR);
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

Node *create_if_statement(Token *current_token, Node *current)
{
    if (!current_token || !current)
        print_error("Internal parser error: null token or node in create_if_statement", 0);

    /* Create IF node and attach */
    Node *if_node = init_node(NULL, current_token->value, current_token->type);
    current->left = if_node;
    current = if_node;
    current_token++;

    /* Expect '(' separator */
    handle_token_errors("ERROR: Expected (", current_token, SEPARATOR);

    Node *open_paren_node = init_node(NULL, current_token->value, SEPARATOR);
    current->left = open_paren_node;
    current = open_paren_node;

    current_token++;
    if (!current_token || current_token->type == END_OF_TOKENS)
        print_error("Invalid if condition: unexpected end", 0);

    /* First token in condition should be an identifier or int or a subexpression/operator */
    if (!(current_token->type == IDENTIFIER || current_token->type == INT || current_token->type == OPERATOR))
        print_error("ERROR: Expected Identifier or INT in if condition", current_token->line_num);

    /* Find comparator (COMP) token ahead (stop at END_OF_TOKENS) */
    Token *scan = current_token;
    while (scan && scan->type != END_OF_TOKENS && scan->type != COMP)
        scan++;

    if (!scan || scan->type != COMP)
        print_error("ERROR: Expected comparator in if condition", current_token->line_num);

    /* Attach comparator node */
    Node *comp_node = init_node(NULL, scan->value, scan->type);
    open_paren_node->left = comp_node;

    /* Determine left-hand operand: scan backwards from COMP to first IDENTIFIER/INT */
    Token *left_tok = scan;
    while (left_tok && left_tok->type != END_OF_TOKENS && !(left_tok->type == INT || left_tok->type == IDENTIFIER))
    {
        if (left_tok == current_token)
            break;
        left_tok--;
    }
    if (!left_tok || !(left_tok->type == INT || left_tok->type == IDENTIFIER))
        print_error("ERROR: Expected left-hand operand for comparator", scan->line_num);

    comp_node->left = init_node(NULL, left_tok->value, left_tok->type);

    /* Now position at token immediately after COMP to parse right-hand side */
    Token *right_start = scan + 1;
    if (!right_start || right_start->type == END_OF_TOKENS)
        print_error("ERROR: Unexpected end after comparator", scan->line_num);

    /* If next token is INT/IDENTIFIER -> either simple right operand or expression if operator follows */
    if (right_start->type == INT || right_start->type == IDENTIFIER)
    {
        /* look ahead: if next token after right_start is OPERATOR, handle operation chain */
        Token *peek = right_start + 1;
        if (peek && peek->type == OPERATOR)
        {
            /* pass peek (operator) and comp_node as the parent for operation nodes */
            Token *after_ops = generate_if_operation_nodes(peek, comp_node);
            /* If generate returned NULL or END_OF_TOKENS, that's an error */
            if (!after_ops || after_ops->type == END_OF_TOKENS)
                print_error("Invalid syntax after right-hand expression", right_start->line_num);
            /* we don't need to update current_token here — parser's main loop advances separately */
        }
        else
        {
            /* simple right operand */
            comp_node->right = init_node(NULL, right_start->value, right_start->type);
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

    /* Find the closing ')' for the if condition */
    Token *close_scan = scan + 1;
    while (close_scan && close_scan->type != END_OF_TOKENS)
    {
        if (close_scan->type == SEPARATOR && close_scan->value && strcmp(close_scan->value, ")") == 0)
            break;
        close_scan++;
    }
    if (!close_scan || close_scan->type == END_OF_TOKENS)
        print_error("ERROR: Expected closing ')' in if condition", scan->line_num);

    Node *close_paren_node = init_node(NULL, close_scan->value, SEPARATOR);
    open_paren_node->right = close_paren_node;
    current = close_paren_node;

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

Node *handle_write_node(Token *current_token, Node *current)
{
    if (!current_token || !current)
        print_error("Internal parser error: null token in handle_write_node", 0);

    // Create write node
    Node *write_node = init_node(NULL, current_token->value, current_token->type);
    current->left = write_node;
    current = write_node;
    current_token++;

    // Expect '('
    if (!current_token || current_token->type != SEPARATOR || !current_token->value || strcmp(current_token->value, "(") != 0)
        print_error("ERROR: Expected '(' after write", current_token ? current_token->line_num : 0);

    Node *open_paren_node = init_node(NULL, current_token->value, SEPARATOR);
    current->left = open_paren_node;
    Node *arg_parent = open_paren_node;

    current_token++;

    // Expect STRING or IDENTIFIER
    if (!current_token || (current_token->type != STRING && current_token->type != IDENTIFIER))
        print_error("ERROR: Expected String literal or Identifier inside write()", current_token ? current_token->line_num : 0);

    Node *arg1_node = init_node(NULL, current_token->value, current_token->type);
    arg_parent->left = arg1_node;
    current_token++;

    // Optional comma for second argument
    if (current_token && current_token->type == SEPARATOR && current_token->value && strcmp(current_token->value, ",") == 0)
    {
        current_token++; // skip comma

        if (!current_token || (current_token->type != INT && current_token->type != IDENTIFIER))
            print_error("ERROR: Expected Number or Identifier after ',' in write()", current_token ? current_token->line_num : 0);

        Node *arg2_node = init_node(NULL, current_token->value, current_token->type);
        arg_parent->right = arg2_node;
        current_token++;
    }

    // Expect ')'
    if (!current_token || current_token->type != SEPARATOR || !current_token->value || strcmp(current_token->value, ")") != 0)
        print_error("ERROR: Expected ')' after write arguments", current_token ? current_token->line_num : 0);

    Node *close_paren_node = init_node(NULL, current_token->value, SEPARATOR);
    open_paren_node->right = close_paren_node;
    current_token++;

    // Expect ';'
    if (!current_token || current_token->type != SEPARATOR || !current_token->value || strcmp(current_token->value, ";") != 0)
        print_error("ERROR: Expected ';' after write()", current_token ? current_token->line_num : 0);

    Node *semi_node = init_node(NULL, current_token->value, SEPARATOR);
    current->right = semi_node;
    current = semi_node;

    return current;
}

Node *parser(Token *tokens)
{
    if (!tokens)
        print_error("Parser received NULL tokens", 0);

    Token *current_token = tokens;
    Node *root = init_node(NULL, "PROGRAM", BEGINNING);
    Node *current = root;
    Node *open_curly = NULL;

    curly_stack *stack = malloc(sizeof(curly_stack));
    if (!stack)
    {
        printf("ERROR: malloc failed\n");
        exit(1);
    }
    init_curly_stack(stack);

    while (current_token->type != END_OF_TOKENS)
    {
        if (!current)
            break;

        switch (current_token->type)
        {
        case KEYWORD:
            if (current_token->value)
            {
                if (strcmp(current_token->value, "EXIT") == 0)
                {
                    current = handle_exit_syscall(current_token, current);
                }
                else if (strcmp(current_token->value, "INT") == 0)
                {
                    current = create_variables(current_token, current);
                }
                else if (strcmp(current_token->value, "IF") == 0)
                {
                    current = create_if_statement(current_token, current);
                }
                else if (strcmp(current_token->value, "WHILE") == 0)
                {
                    current = create_if_statement(current_token, current);
                }
                else if (strcmp(current_token->value, "WRITE") == 0)
                {
                    current = handle_write_node(current_token, current);
                }
            }
            break;

        case SEPARATOR:
            if (current_token->value && strcmp(current_token->value, "{") == 0)
            {
                Node *open_curly_node = init_node(NULL, current_token->value, SEPARATOR);
                current->right = open_curly_node;
                push_curly(stack, open_curly_node);
                current = open_curly_node;
            }
            else if (current_token->value && strcmp(current_token->value, "}") == 0)
            {
                open_curly = pop_curly(stack);
                if (!open_curly)
                {
                    print_error("ERROR: Unexpected closing brace '}'", current_token->line_num);
                }
                Node *close_curly_node = init_node(NULL, current_token->value, SEPARATOR);
                open_curly->right = close_curly_node;
                current = close_curly_node;
            }
            break;

        case IDENTIFIER:
            if (current_token != tokens)
            {
                Token *prev = current_token - 1;
                if (prev->type == SEPARATOR &&
                    (strcmp(prev->value, ";") == 0 || strcmp(prev->value, "{") == 0 || strcmp(prev->value, "}") == 0))
                {
                    current = create_variable_reusage(current_token, current);
                }
            }
            break;

        default:
            // Other token types can be ignored or handled separately if needed
            break;
        }

        current_token++;
    }

    return root;
}
