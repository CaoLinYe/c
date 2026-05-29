#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image_path>\n", argv[0]);
        return 1;
    }

    int w, h, channels;
    unsigned char *data = stbi_load(argv[1], &w, &h, &channels, 0);

    if (!data) {
        fprintf(stderr, "Error: %s\n", stbi_failure_reason());
        return 1;
    }

    printf("%s: %d x %d, %d channel(s)\n", argv[1], w, h, channels);
    stbi_image_free(data);
    return 0;
}
