#ifndef PARSER_H_
#define PARSER_H_

typedef struct Node
{
  char *value;
  TokenType type;
  struct Node *right;
  struct Node *left;
  size_t line_num; // Track source line number for error reporting
} Node;

Node *parser(Token *tokens);
void print_tree(Node *node, int indent, char *identifier);
Node *init_node(Node *node, char *value, TokenType type, size_t line_num);
void print_error(char *error_type, size_t line_number);

#endif