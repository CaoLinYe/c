#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// 结构体表示图片位置
struct ImagePosition {
    int x;
    int y;
    int found;  // 0表示未找到，1表示找到
};

// 结构体表示图片数据
struct ImageData {
    png_bytep *row_pointers;
    png_byte *data;  // 连续内存块，用于统一释放
    int width;
    int height;
    png_byte color_type;
    png_byte bit_depth;
};

// 函数声明
struct ImageData *read_png_file(const char *filename);
void free_image_data(struct ImageData *image);
struct ImagePosition find_image_position(const char *large_image_path, const char *small_image_path);

int main(int argc, char *argv[]) {
    printf("libpng version: %s\n", PNG_LIBPNG_VER_STRING);

    if (argc != 3) {
        printf("Usage: %s <large_image> <small_image>\n", argv[0]);
        printf("Example: %s large.png small.png\n", argv[0]);
        return 1;
    }

    const char *large_image = argv[1];
    const char *small_image = argv[2];

    struct ImagePosition pos = find_image_position(large_image, small_image);

    if (pos.found) {
        printf("Small image found at position: (%d, %d)\n", pos.x, pos.y);
    } 
    else {
        printf("Small image not found in large image\n");
    }

    return 0;
}

// 读取PNG文件
struct ImageData *read_png_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return NULL;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    // 在 setjmp 之前将 image 初始化为 NULL，避免跳转时释放未初始化指针
    struct ImageData *image = NULL;

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        free_image_data(image);
        fclose(fp);
        return NULL;
    }

    image = malloc(sizeof(struct ImageData));
    if (!image) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    memset(image, 0, sizeof(*image));

    png_init_io(png, fp);
    png_read_info(png, info);

    image->width = png_get_image_width(png, info);
    image->height = png_get_image_height(png, info);
    image->color_type = png_get_color_type(png, info);
    image->bit_depth = png_get_bit_depth(png, info);

    // 转换为8位RGBA格式以便比较
    if (image->bit_depth == 16) {
        png_set_strip_16(png);
    }

    if (image->color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }

    if (image->color_type == PNG_COLOR_TYPE_GRAY && image->bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    if (image->color_type == PNG_COLOR_TYPE_RGB ||
        image->color_type == PNG_COLOR_TYPE_GRAY ||
        image->color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    if (image->color_type == PNG_COLOR_TYPE_GRAY ||
        image->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    // 分配连续内存存储所有像素行
    size_t rowbytes = png_get_rowbytes(png, info);
    size_t total_size = rowbytes * image->height;
    image->data = malloc(total_size);
    if (!image->data) {
        free_image_data(image);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    image->row_pointers = malloc(sizeof(png_bytep) * image->height);
    if (!image->row_pointers) {
        free_image_data(image);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    for (int y = 0; y < image->height; y++) {
        image->row_pointers[y] = &image->data[y * rowbytes];
    }

    png_read_image(png, image->row_pointers);

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return image;
}

// 释放图片数据
void free_image_data(struct ImageData *image) {
    if (!image) return;

    free(image->row_pointers);
    free(image->data);
    free(image);
}

// 比较两个像素是否相同（RGBA格式，用 uint32_t 一次比较）
static inline int pixels_equal(png_byte *pixel1, png_byte *pixel2) {
    return *(uint32_t*)pixel1 == *(uint32_t*)pixel2;
}

// 查找小图片在大图片中的位置
struct ImagePosition find_image_position(const char *large_image_path, const char *small_image_path) {
    struct ImagePosition result = {0, 0, 0};

    struct ImageData *large_image = read_png_file(large_image_path);
    if (!large_image) {
        fprintf(stderr, "Error: Cannot read large image %s\n", large_image_path);
        return result;
    }

    struct ImageData *small_image = read_png_file(small_image_path);
    if (!small_image) {
        fprintf(stderr, "Error: Cannot read small image %s\n", small_image_path);
        free_image_data(large_image);
        return result;
    }

    if (small_image->width > large_image->width || small_image->height > large_image->height) {
        fprintf(stderr, "Error: Small image is larger than large image\n");
        free_image_data(small_image);
        free_image_data(large_image);
        return result;
    }

    int max_x = large_image->width - small_image->width;
    int max_y = large_image->height - small_image->height;
    int sw = small_image->width;
    int sh = small_image->height;

    // 小图首像素和尾像素（用于快速拒绝）
    uint32_t small_first = *(uint32_t*)&small_image->row_pointers[0][0];
    uint32_t small_last  = *(uint32_t*)&small_image->row_pointers[sh - 1][(sw - 1) * 4];

    for (int y = 0; y <= max_y; y++) {
        for (int x = 0; x <= max_x; x++) {
            // 快速拒绝：检查首尾像素是否匹配
            png_byte *lg_row0 = large_image->row_pointers[y];
            png_byte *lg_rowN = large_image->row_pointers[y + sh - 1];
            if (*(uint32_t*)&lg_row0[x * 4] != small_first ||
                *(uint32_t*)&lg_rowN[(x + sw - 1) * 4] != small_last) {
                continue;
            }

            int match = 1;
            for (int sy = 0; sy < sh && match; sy++) {
                for (int sx = 0; sx < sw && match; sx++) {
                    uint32_t lg = *(uint32_t*)&large_image->row_pointers[y + sy][(x + sx) * 4];
                    uint32_t sm = *(uint32_t*)&small_image->row_pointers[sy][sx * 4];
                    if (lg != sm) {
                        match = 0;
                    }
                }
            }

            if (match) {
                result.x = x;
                result.y = y;
                result.found = 1;
                goto done;
            }
        }
    }

done:
    free_image_data(small_image);
    free_image_data(large_image);
    return result;
}
