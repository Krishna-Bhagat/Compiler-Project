#ifndef GENERATOR_STATE_H_
#define GENERATOR_STATE_H_

#include <stddef.h>
#include <stdio.h>
#include "hashmap/hashmap.h"
#include "parserf.h"

// External functions needed by the code generator
// Note: the hashmap API is provided by "hashmap/hashmap.h". Do not redeclare
// conflicting prototypes here. Only declare the small utility 'insert' added
// in this project which is implemented in `hashmapoperators.c`.
extern void insert(int key, char *data);

// State management functions
void initialize_generator_state(void);
void cleanup_generator_state(void);
void reset_line_tracking(void);
void reset_stack_state(void);
void reset_label_state(void);

#endif /* GENERATOR_STATE_H_ */