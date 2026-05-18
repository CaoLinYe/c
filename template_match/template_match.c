#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 结构体表示图片位置
struct ImagePosition {
    int x;
    int y;
    int found;  // 0表示未找到，1表示找到
};

// 结构体表示图片数据
struct ImageData {
    png_bytep *row_pointers;
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
    
    // 检查参数数量
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
    } else {
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

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    struct ImageData *image = malloc(sizeof(struct ImageData));
    if (!image) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

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

    // 分配内存存储像素数据
    image->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * image->height);
    for (int y = 0; y < image->height; y++) {
        image->row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, image->row_pointers);

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return image;
}

// 释放图片数据
void free_image_data(struct ImageData *image) {
    if (!image) return;
    
    if (image->row_pointers) {
        for (int y = 0; y < image->height; y++) {
            free(image->row_pointers[y]);
        }
        free(image->row_pointers);
    }
    free(image);
}

// 比较两个像素是否相同（考虑RGBA格式）
int pixels_equal(png_byte *pixel1, png_byte *pixel2) {
    // 比较RGBA四个通道
    return (pixel1[0] == pixel2[0] &&  // R
            pixel1[1] == pixel2[1] &&  // G
            pixel1[2] == pixel2[2] &&  // B
            pixel1[3] == pixel2[3]);   // A
}

// 查找小图片在大图片中的位置
struct ImagePosition find_image_position(const char *large_image_path, const char *small_image_path) {
    struct ImagePosition result = {0, 0, 0};
    
    // 读取大图片
    struct ImageData *large_image = read_png_file(large_image_path);
    if (!large_image) {
        fprintf(stderr, "Error: Cannot read large image %s\n", large_image_path);
        return result;
    }
    
    // 读取小图片
    struct ImageData *small_image = read_png_file(small_image_path);
    if (!small_image) {
        fprintf(stderr, "Error: Cannot read small image %s\n", small_image_path);
        free_image_data(large_image);
        return result;
    }
    
    // 检查小图片是否比大图片大
    if (small_image->width > large_image->width || small_image->height > large_image->height) {
        fprintf(stderr, "Error: Small image is larger than large image\n");
        free_image_data(small_image);
        free_image_data(large_image);
        return result;
    }
    
    // 在大图片中搜索小图片
    int max_x = large_image->width - small_image->width;
    int max_y = large_image->height - small_image->height;
    
    for (int y = 0; y <= max_y; y++) {
        for (int x = 0; x <= max_x; x++) {
            int match = 1;
            
            // 检查当前位置是否匹配
            for (int sy = 0; sy < small_image->height && match; sy++) {
                for (int sx = 0; sx < small_image->width && match; sx++) {
                    png_byte *large_pixel = &large_image->row_pointers[y + sy][(x + sx) * 4];
                    png_byte *small_pixel = &small_image->row_pointers[sy][sx * 4];
                    
                    if (!pixels_equal(large_pixel, small_pixel)) {
                        match = 0;
                        break;
                    }
                }
            }
            
            if (match) {
                result.x = x;
                result.y = y;
                result.found = 1;
                break;
            }
        }
        
        if (result.found) {
            break;
        }
    }
    
    // 释放内存
    free_image_data(small_image);
    free_image_data(large_image);
    
    return result;
}


