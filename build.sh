#!/bin/bash
# Compile program
gcc main.c lexerf.c parserf.c codegeneratorf.c hashmap/hashmapoperators.c -o build/main -Wall -Wextra
# ./build/main/exe test.np
# Run program
./build/main test.np