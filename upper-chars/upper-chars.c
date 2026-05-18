#include <stdio.h>
#include <ctype.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <string>\n", argv[0]);
        return 1;
    }

    int len = strlen(argv[1]);
    char list[len + 1];

    for (int i = 0; i < len; ++i) {
        list[i] = islower((unsigned char)argv[1][i])
                  ? toupper((unsigned char)argv[1][i])
                  : argv[1][i];
    }
    list[len] = '\0';
    printf("%s\n", list);
    return 0;
}
