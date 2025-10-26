#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "lexerf.h"
#include "parserf.h"

void generate_code(Node *test, char *filename);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error: Correct syntax: %s <filename.np>\n", argv[0]);
        exit(1);
    }
    char *output_filename = "generated.asm";

    FILE *file;
    file = fopen(argv[1], "r");

    if (file == NULL)
    {
        printf("Error: Could not open file %s\n", argv[1]);
        exit(1);
    }
    Token *tokens = lexer(file);

    for (size_t i = 0; tokens[i].type != END_OF_TOKENS; i++)
    {
        print_token(tokens[i]);
    }

    Node *test = parser(tokens);
    generate_code(test, output_filename);
    FILE *assembly_file = fopen(output_filename, "r");
    if (assembly_file == NULL)
    {
        printf("Error: Could not open file generated.asm\n");
        exit(1);
    }

    char *nasm = malloc(sizeof(char) * 64);
    char *gcc = malloc(sizeof(char) * 64);
    sprintf(nasm, "nasm -f elf64 generated.asm -o generated.o");
    sprintf(gcc, "gcc generated.o -o generated -no-pie -lc");
    system(nasm);
    system(gcc);
    printf("Compiled successfully to %s\n", "generated");
    return 0;
}