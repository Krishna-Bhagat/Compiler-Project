nasm -f win64 -g -F cv8 -o test.obj generated.asm
gcc test.obj -o test.exe
