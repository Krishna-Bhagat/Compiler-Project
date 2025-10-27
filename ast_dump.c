#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexerf.h"
#include "parserf.h"

static void dump_ast(const Node *node, int depth)
{
    if (!node)
        return;

    for (int i = 0; i < depth; ++i)
        printf("  ");
    printf("%s (type=%d, line=%zu)\n",
           node->value ? node->value : "NULL",
           node->type,
           node->line_num);

    dump_ast(node->left, depth + 1);
    dump_ast(node->right, depth + 1);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file)
    {
        fprintf(stderr, "Failed to open source file\n");
        return 1;
    }

    Token *tokens = lexer(file);
    fclose(file);

    if (!tokens)
    {
        fprintf(stderr, "Failed to tokenize input\n");
        return 1;
    }

    // Dump tokens for debugging to stderr
    fprintf(stderr, "Tokens:\n");
    size_t toks = 0;
    for (size_t i = 0; tokens[i].type != END_OF_TOKENS; ++i)
    {
        fprintf(stderr, "  [%zu] type=%d value='%s' line=%zu ptr=%p len=%zu\n", i, tokens[i].type, tokens[i].value ? tokens[i].value : "NULL", tokens[i].line_num, (void *)tokens[i].value, tokens[i].value ? strlen(tokens[i].value) : 0);
        toks = i + 1;
    }
    fprintf(stderr, "Token count: %zu\n", toks);
    fprintf(stderr, "---\n");

    Node *ast = parser(tokens);
    if (!ast)
    {
        fprintf(stderr, "Failed to parse tokens\n");
        cleanup_tokens(tokens);
        return 1;
    }

    printf("AST Structure:\n");
    dump_ast(ast, 0);

    cleanup_tokens(tokens);
    return 0;
}