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

void cleanup_tokens(Token *tokens)
{
  if (!tokens)
    return;
  Token *current = tokens;
  while (current->type != END_OF_TOKENS)
  {
    free(current->value);
    current++;
  }
  free(tokens);
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
    token->value = strdup("EXIT");
    free(keyword);
  }
  else if (strcmp(keyword, "int") == 0)
  {
    token->type = KEYWORD;
    token->value = strdup("INT");
    free(keyword);
  }
  else if (strcmp(keyword, "if") == 0)
  {
    token->type = KEYWORD;
    token->value = strdup("IF");
    free(keyword);
  }
  else if (strcmp(keyword, "while") == 0)
  {
    token->type = KEYWORD;
    token->value = strdup("WHILE");
    free(keyword);
  }
  else if (strcmp(keyword, "write") == 0)
  {
    token->type = KEYWORD;
    token->value = strdup("WRITE");
    free(keyword);
  }
  else if (strcmp(keyword, "eq") == 0)
  {
    token->type = COMP;
    token->value = strdup("EQ");
    free(keyword);
  }
  else if (strcmp(keyword, "neq") == 0)
  {
    token->type = COMP;
    token->value = strdup("NEQ");
    free(keyword);
  }
  else if (strcmp(keyword, "less") == 0)
  {
    token->type = COMP;
    token->value = strdup("LESS");
    free(keyword);
  }
  else if (strcmp(keyword, "greater") == 0)
  {
    token->type = COMP;
    token->value = strdup("GREATER");
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

Token *lexer(FILE *file)
{
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *buf = malloc(length + 1);
  if (!buf)
    return NULL;
  if (fread(buf, 1, length, file) != (size_t)length)
  {
    // continue even if partial
  }
  buf[length] = '\0';

  size_t capacity = 64;
  size_t count = 0;
  Token *tokens = malloc(sizeof(Token) * capacity);
  if (!tokens)
  {
    free(buf);
    return NULL;
  }

  int i = 0;
  while (buf[i] != '\0')
  {
    char c = buf[i];

    // Skip whitespace
    if (c == ' ' || c == '\t' || c == '\r')
    {
      i++;
      continue;
    }
    if (c == '\n')
    {
      line_number++;
      i++;
      continue;
    }

    // Ensure capacity
    if (count + 2 >= capacity)
    {
      capacity *= 2;
      Token *tmp = realloc(tokens, sizeof(Token) * capacity);
      if (!tmp)
      { /* cleanup */
        while (count--)
          free(tokens[count].value);
        free(tokens);
        free(buf);
        return NULL;
      }
      tokens = tmp;
    }

    Token tk = {0};
    tk.line_num = line_number;

    if (isalpha((unsigned char)c) || c == '_')
    {
      int start = i;
      while (isalpha((unsigned char)buf[i]) || buf[i] == '_')
        i++;
      int len = i - start;
      char *s = malloc(len + 1);
      memcpy(s, buf + start, len);
      s[len] = '\0';
      // keywords
      if (strcmp(s, "exit") == 0)
      {
        tk.type = KEYWORD;
        free(s);
        tk.value = strdup("EXIT");
      }
      else if (strcmp(s, "int") == 0)
      {
        tk.type = KEYWORD;
        free(s);
        tk.value = strdup("INT");
      }
      else if (strcmp(s, "if") == 0)
      {
        tk.type = KEYWORD;
        free(s);
        tk.value = strdup("IF");
      }
      else if (strcmp(s, "while") == 0)
      {
        tk.type = KEYWORD;
        free(s);
        tk.value = strdup("WHILE");
      }
      else if (strcmp(s, "write") == 0)
      {
        tk.type = KEYWORD;
        free(s);
        tk.value = strdup("WRITE");
      }
      else if (strcmp(s, "eq") == 0)
      {
        tk.type = COMP;
        free(s);
        tk.value = strdup("EQ");
      }
      else if (strcmp(s, "neq") == 0)
      {
        tk.type = COMP;
        free(s);
        tk.value = strdup("NEQ");
      }
      else if (strcmp(s, "less") == 0)
      {
        tk.type = COMP;
        free(s);
        tk.value = strdup("LESS");
      }
      else if (strcmp(s, "greater") == 0)
      {
        tk.type = COMP;
        free(s);
        tk.value = strdup("GREATER");
      }
      else
      {
        tk.type = IDENTIFIER;
        tk.value = s;
      }
      tokens[count++] = tk;
      continue;
    }

    if (isdigit((unsigned char)c))
    {
      int start = i;
      while (isdigit((unsigned char)buf[i]))
        i++;
      int len = i - start;
      char *s = malloc(len + 1);
      memcpy(s, buf + start, len);
      s[len] = '\0';
      tk.type = INT;
      tk.value = s;
      tokens[count++] = tk;
      continue;
    }

    if (c == '"')
    {
      i++;
      int start = i;
      while (buf[i] != '\0' && buf[i] != '"')
        i++;
      int len = i - start;
      char *s = malloc(len + 1);
      memcpy(s, buf + start, len);
      s[len] = '\0';
      if (buf[i] == '"')
        i++;
      tk.type = STRING;
      tk.value = s;
      tokens[count++] = tk;
      continue;
    }

    // separators and operators
    if (strchr(";(),{}=+-*/%", c))
    {
      char *s = malloc(2);
      s[0] = c;
      s[1] = '\0';
      tk.value = s;
      if (strchr("=+-*/%", c))
        tk.type = OPERATOR;
      else
        tk.type = SEPARATOR;
      tokens[count++] = tk;
      i++;
      continue;
    }

    // Unknown char: skip
    i++;
  }

  // sentinel
  tokens[count].type = END_OF_TOKENS;
  tokens[count].value = NULL;
  free(buf);
  return tokens;
}
