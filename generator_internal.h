#ifndef GENERATOR_INTERNAL_H_
#define GENERATOR_INTERNAL_H_

#include "parserf.h"
#include <stdio.h>

// Internal helpers used by the code generator.
// These declarations intentionally match the definitions in
// `codegeneratorf.c` to avoid conflicting types when the header is
// included. Functions that are defined static in the .c file are not
// declared here as static; only the externally visible helpers are
// declared.
void push_var(size_t offset, char *var_name, FILE *file);
void push(char *reg, FILE *file);
int mov_if_var_or_not(char *reg, Node *node, FILE *file);
void if_label(FILE *file, char *comp, int num);

// Note: `traverse_tree` and `generate_operator_code` are implemented as
// static functions within `codegeneratorf.c` and therefore are not
// declared here to avoid linkage/type conflicts.

#endif /* GENERATOR_INTERNAL_H_ */