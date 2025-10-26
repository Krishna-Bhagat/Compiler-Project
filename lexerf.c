#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "lexerf.h"
#include "parserf.h"
#include "codegeneratorf.h"
#include "./hashmap/hashmapoperators.h"
#include "./hashmap/hashmap.h"

size_t line_number = 0;

void print_token(Token token)
{
  printf("Token Value: ");
  printf("'");
  for (int i = 0; token.value[i] != '\0'; i++)
  {
    printf("%c", token.value[i]);
  }
  printf("'");
  printf("\nline number: %zu\n", token.line_num);

  switch (token.type)
  {
  case BEGINNING:
    printf("BEGINNING\n");
    break;
  case INT:
    printf(" TOKEN TYPE: INT\n");
    break;
  case KEYWORD:
    printf(" TOKEN TYPE: KEYWORD\n");
    break;
  case IDENTIFIER:
    printf(" TOKEN TYPE: IDENTIFIER\n");
    break;
  case SEPARATOR:
    printf(" TOKEN TYPE: SEPARATOR\n");
    break;
  case OPERATOR:
    printf(" TOKEN TYPE: OPERATOR\n");
    break;
  case STRING:
    printf(" TOKEN TYPE: STRING\n");
    break;
  case COMP:
    printf(" TOKEN TYPE: COMPARATOR\n");
    break;
  case END_OF_TOKENS:
    printf(" END_OF_TOKEN\n");
    break;
  default:
    printf("UNKNOWN");
  }
}

Token *generate_number(char *current, int *current_index)
{
  Token *token = malloc(sizeof(Token));
  token->line_num = line_number;
  token->type = INT;

  char *value = malloc(32); // or bigger, safe size
  int value_index = 0;

  while (isdigit(current[*current_index]))
  {
    value[value_index++] = current[*current_index];
    (*current_index)++;
  }
  value[value_index] = '\0';

  token->value = value;
  return token;
}

Token *generate_keyword_or_identifier(char *current, int *current_index)
{
  Token *token = malloc(sizeof(Token));
  if (!token)
  {
    perror("malloc");
    exit(EXIT_FAILURE);
  } // Check for malloc failure
  token->line_num = line_number;

  // Dynamically grow keyword buffer if needed
  size_t bufsize = 32;
  char *keyword = malloc(bufsize);
  if (!keyword)
  {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  size_t keyword_index = 0;
  while (current[*current_index] != '\0' &&
         (isalpha((unsigned char)current[*current_index]) || current[*current_index] == '_'))
  {
    if (keyword_index + 1 >= bufsize)
    { // resize if needed
      bufsize *= 2;
      keyword = realloc(keyword, bufsize);
      if (!keyword)
      {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
    }
    keyword[keyword_index++] = current[*current_index];
    (*current_index)++;
  }
  keyword[keyword_index] = '\0';

  if (strcmp(keyword, "exit") == 0)
  {
    token->type = KEYWORD;
    token->value = "EXIT";
    free(keyword);
  }
  else if (strcmp(keyword, "int") == 0)
  {
    token->type = KEYWORD;
    token->value = "INT";
    free(keyword);
  }
  else if (strcmp(keyword, "if") == 0)
  {
    token->type = KEYWORD;
    token->value = "IF";
    free(keyword);
  }
  else if (strcmp(keyword, "while") == 0)
  {
    token->type = KEYWORD;
    token->value = "WHILE";
    free(keyword);
  }
  else if (strcmp(keyword, "write") == 0)
  {
    token->type = KEYWORD;
    token->value = "WRITE";
    free(keyword);
  }
  else if (strcmp(keyword, "eq") == 0)
  {
    token->type = COMP;
    token->value = "EQ";
    free(keyword);
  }
  else if (strcmp(keyword, "neq") == 0)
  {
    token->type = COMP;
    token->value = "NEQ";
    free(keyword);
  }
  else if (strcmp(keyword, "less") == 0)
  {
    token->type = COMP;
    token->value = "LESS";
    free(keyword);
  }
  else if (strcmp(keyword, "greater") == 0)
  {
    token->type = COMP;
    token->value = "GREATER";
    free(keyword);
  }
  else
  {
    token->type = IDENTIFIER;
    token->value = keyword;
  }
  return token;
}

Token *generate_string_token(char *current, int *current_index)
{
  Token *token = malloc(sizeof(Token));
  if (!token)
  {
    perror("malloc");
    exit(EXIT_FAILURE);
  } // Check for malloc failure
  token->line_num = line_number;
  // Dynamically grow buffer
  size_t bufsize = 64;
  char *value = malloc(bufsize);
  if (!value)
  {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  size_t value_index = 0;

  (*current_index)++; // skip opening quote
  while (current[*current_index] != '\0' && current[*current_index] != '"')
  {
    if (value_index + 1 >= bufsize)
    {
      bufsize *= 2;
      value = realloc(value, bufsize);
      if (!value)
      {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
    }
    value[value_index++] = current[*current_index];
    (*current_index)++;
  }
  value[value_index] = '\0';

  if (current[*current_index] == '"')
  {
    (*current_index)++; // consume closing quote
  }
  else
  {
    fprintf(stderr, "Warning: Unterminated string literal\n");
  }

  token->type = STRING;
  token->value = value;
  return token;
}

Token *generate_separator_or_operator(char *current, int *current_index, TokenType type)
{
  Token *token = malloc(sizeof(Token));
  if (!token)
    return NULL;

  token->value = malloc(2); // only 2 chars needed
  if (!token->value)
  {
    free(token);
    return NULL;
  }

  token->value[0] = current[*current_index];
  token->value[1] = '\0';
  token->line_num = line_number;
  token->type = type;

  return token;
}

size_t tokens_index;

Token *lexer(FILE *file)
{
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *current = malloc(length + 1); // ✅ +1 for null terminator
  if (!current)
    return NULL;

  fread(current, 1, length, file);
  fclose(file);

  current[length] = '\0';

  int current_index = 0;
  size_t number_of_tokens = 12;
  size_t tokens_size = 0;
  Token *tokens = malloc(sizeof(Token) * number_of_tokens);
  if (!tokens)
  {
    free(current);
    return NULL;
  }

  tokens_index = 0;

  while (current[current_index] != '\0')
  {
    Token *token = NULL;

    tokens_size++;
    if (tokens_size > number_of_tokens)
    {
      number_of_tokens = (size_t)(number_of_tokens * 1.5);
      tokens = realloc(tokens, sizeof(Token) * number_of_tokens);
      if (!tokens)
      {
        free(current);
        return NULL;
      }
    }

    if (strchr(";(),{}=+-*/%", current[current_index]))
    {
      token = generate_separator_or_operator(current, &current_index,
                                             strchr("=+-*/%", current[current_index]) ? OPERATOR : SEPARATOR);
    }
    else if (current[current_index] == '"')
    {
      token = generate_string_token(current, &current_index);
    }
    else if (isdigit((unsigned char)current[current_index]))
    {
      token = generate_number(current, &current_index);
      current_index--; // adjust because generate_number advanced index
    }
    else if (isalpha((unsigned char)current[current_index]))
    {
      token = generate_keyword_or_identifier(current, &current_index);
      current_index--; // adjust similarly
    }
    else if (current[current_index] == '\n')
    {
      line_number++;
    }

    if (token)
    {
      tokens[tokens_index++] = *token; // shallow copy
      free(token);                     // free struct only, not its value
    }

    current_index++;
  }

  // Add END_OF_TOKENS sentinel
  tokens[tokens_index].value = NULL; // ✅ better than '\0'
  tokens[tokens_index].type = END_OF_TOKENS;

  free(current);
  return tokens;
}
