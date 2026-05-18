#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 65536

/**
 * 使用异或加密/解密文件
 * @param input_file 输入文件名
 * @param output_file 输出文件名
 * @param password 密码字符串
 * @return 0表示成功，非0表示错误
 */
int xor_encrypt_file(const char *input_file, const char *output_file, const char *password) {
    FILE *in = NULL;
    FILE *out = NULL;
    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;
    size_t password_len;
    size_t i;
    int result = 0;
    char temp_file[256];
    int use_temp_file = 0;
    
    // 检查输入和输出文件是否相同
    if (strcmp(input_file, output_file) == 0) {
        // 创建临时文件名
        snprintf(temp_file, sizeof(temp_file), "%s.tmp", input_file);
        use_temp_file = 1;
        printf("注意: 输入和输出文件相同，将使用临时文件: %s\n", temp_file);
    }
    
    // 打开输入文件
    in = fopen(input_file, "rb");
    if (in == NULL) {
        fprintf(stderr, "无法打开输入文件 '%s': %s\n", input_file, strerror(errno));
        return 1;
    }
    
    // 打开输出文件（或临时文件）
    const char *output_to_use = use_temp_file ? temp_file : output_file;
    out = fopen(output_to_use, "wb");
    if (out == NULL) {
        fprintf(stderr, "无法打开输出文件 '%s': %s\n", output_to_use, strerror(errno));
        fclose(in);
        return 2;
    }
    
    // 获取密码长度
    password_len = strlen(password);
    if (password_len == 0) {
        fprintf(stderr, "错误: 密码不能为空\n");
        fclose(in);
        fclose(out);
        return 3;
    }
    
    // 处理文件数据
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, in)) > 0) {
        // 对每个字节进行异或加密
        for (i = 0; i < bytes_read; i++) {
            buffer[i] ^= password[i % password_len];
        }
        
        // 写入输出文件
        if (fwrite(buffer, 1, bytes_read, out) != bytes_read) {
            fprintf(stderr, "写入输出文件时出错: %s\n", strerror(errno));
            result = 4;
            break;
        }
    }
    
    // 检查读取错误
    if (ferror(in)) {
        fprintf(stderr, "读取输入文件时出错: %s\n", strerror(errno));
        result = 5;
    }
    
    // 清理资源
    fclose(in);
    fclose(out);
    
    // 如果使用了临时文件，将其重命名为目标文件
    // rename 在 Linux 上是原子操作，会直接覆盖目标文件，无需先 remove
    if (use_temp_file && result == 0) {
        if (rename(temp_file, output_file) != 0) {
            fprintf(stderr, "无法重命名临时文件 '%s' 到 '%s': %s\n",
                    temp_file, output_file, strerror(errno));
            result = 6;
            remove(temp_file);
        }
    }
    
    return result;
}

/**
 * 打印使用说明
 */
void print_usage(const char *program_name) {
    printf("文件加密/解密工具\n");
    printf("用法: %s <输入文件> <输出文件> <密码>\n", program_name);
    printf("\n");
    printf("参数说明:\n");
    printf("  输入文件: 要加密或解密的文件路径\n");
    printf("  输出文件: 加密或解密后的文件路径\n");
    printf("  密码: 用于加密/解密的密码字符串\n");
    printf("\n");
    printf("注意: 使用相同的密码再次运行可以对文件进行解密\n");
}

int main(int argc, char *argv[]) {
    // 检查参数数量
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *output_file = argv[2];
    const char *password = argv[3];
    
    printf("正在处理文件: %s -> %s\n", input_file, output_file);
    
    // 执行加密/解密
    int result = xor_encrypt_file(input_file, output_file, password);
    
    if (result == 0) {
        printf("文件处理成功完成!\n");
    } else {
        printf("文件处理失败 (错误代码: %d)\n", result);
    }
    
    return result;
}

