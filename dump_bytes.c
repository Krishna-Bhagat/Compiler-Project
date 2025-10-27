#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        perror("fopen");
        return 1;
    }
    int c;
    size_t i = 0;
    while ((c = fgetc(f)) != EOF)
    {
        printf("%04zu: 0x%02X '%c'\n", i, (unsigned char)c, (c >= 32 && c < 127) ? c : '.');
        i++;
    }
    fclose(f);
    return 0;
}
