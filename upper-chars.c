#include <stdio.h>
#include <ctype.h> 
#include <stdlib.h>
#include <string.h>

int main (int argc, char* argv[]) {
    char *list; 
    int len, i;
    printf("%ld\n", __STDC_VERSION__);
    if (argc == 2) {
        len = strlen(argv[1]);
        printf("len: %d\n", len);
        list = (char *)malloc(len+1);
        for (i = 0; i < len; ++i) {
            if (islower((unsigned char)argv[1][i])) {
                list[i] = toupper((unsigned char)argv[1][i]);
            }
            else {
                list[i] = argv[1][i];
            }
        }
        list[len] = '\0';
        printf("%s\n", list);
        free(list);
    }
    return 0;
}
