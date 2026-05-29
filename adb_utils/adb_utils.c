#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#define CMD_BUF_SIZE 4096
#define MAX_CHANNELS 4

/* ------------------------------------------------------------------ */
/*  ADB helpers                                                        */
/* ------------------------------------------------------------------ */

static char *adb_cmd(const char *fmt, ...) {
    static char buf[CMD_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

/* Run adb command, return stdout (caller must free). NULL on error. */
static char *adb_exec(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t n = 0;
    char *out = NULL;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        char *p = realloc(out, n + len + 1);
        if (!p) { free(out); pclose(fp); return NULL; }
        out = p;
        memcpy(out + n, buf, len + 1);
        n += len;
    }
    int rc = pclose(fp);
    if (rc) { free(out); return NULL; }
    return out;
}

/* Run adb command that produces binary output to a file. */
static int adb_exec_out(const char *cmd, const char *out_path) {
    char buf[CMD_BUF_SIZE];
    snprintf(buf, sizeof(buf), "%s > \"%s\"", cmd, out_path);
    int rc = system(buf);
    return rc;
}

/* Build adb prefix with optional serial */
static void adb_prefix(char *buf, size_t size, const char *serial) {
    if (serial && serial[0])
        snprintf(buf, size, "adb -s %s", serial);
    else
        snprintf(buf, size, "adb");
}

/* ------------------------------------------------------------------ */
/*  Pixel compare with tolerance                                       */
/* ------------------------------------------------------------------ */

/* Normalised sum of absolute differences: 1.0 = identical */
static double pixel_sim(const uint8_t *a, const uint8_t *b, int ch) {
    double max_diff = ch * 255.0;
    if (max_diff == 0) return 1.0;
    double diff = 0;
    for (int i = 0; i < ch; i++)
        diff += abs((int)a[i] - (int)b[i]);
    return 1.0 - diff / max_diff;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int adb_tap(const char *serial, int x, int y) {
    char pre[256];
    adb_prefix(pre, sizeof(pre), serial);
    char *cmd = adb_cmd("%s shell input tap %d %d", pre, x, y);
    return system(cmd);
}

int adb_swipe(const char *serial, int x1, int y1, int x2, int y2, int ms) {
    char pre[256];
    adb_prefix(pre, sizeof(pre), serial);
    char *cmd = adb_cmd("%s shell input swipe %d %d %d %d %d",
                        pre, x1, y1, x2, y2, ms);
    return system(cmd);
}

int adb_screenshot(const char *serial, const char *out_path) {
    char pre[256];
    adb_prefix(pre, sizeof(pre), serial);
    char *cmd = adb_cmd("%s exec-out screencap -p", pre);
    return adb_exec_out(cmd, out_path);
}

int adb_screen_size(const char *serial, int *w, int *h) {
    char pre[256];
    adb_prefix(pre, sizeof(pre), serial);
    char *cmd = adb_cmd("%s shell wm size", pre);
    char *out = adb_exec(cmd);
    if (!out) return -1;

    /* "Physical size: 1080x2400" or "Override size: ..." */
    char *p = strstr(out, "Physical size: ");
    if (!p) { free(out); return -1; }
    p += 15;
    char *q = strchr(p, 'x');
    if (!q) { free(out); return -1; }
    *q = '\0';
    *w = atoi(p);
    *h = atoi(q + 1);
    free(out);
    return 0;
}

/* ADB devices list */
char **adb_devices(int *count) {
    char *out = adb_exec("adb devices");
    if (!out) { *count = 0; return NULL; }

    int cap = 8, n = 0;
    char **list = malloc(cap * sizeof(char *));

    char *line = out;
    char *next;
    while ((next = strchr(line, '\n')) != NULL) {
        *next = '\0';
        /* lines ending with "device" */
        size_t len = strlen(line);
        if (len > 7 && strcmp(line + len - 7, "\tdevice") == 0) {
            line[len - 7] = '\0';
            if (n >= cap) {
                cap *= 2;
                list = realloc(list, cap * sizeof(char *));
            }
            list[n++] = strdup(line);
        }
        line = next + 1;
    }
    free(out);
    *count = n;
    return list;
}

void adb_devices_free(char **list, int count) {
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

/* ------------------------------------------------------------------ */
/*  Template matching (stb_image-based)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    int w, h, ch;
    uint8_t *pixels;
} Image;

static Image *img_load(const char *path) {
    Image *im = malloc(sizeof(Image));
    if (!im) return NULL;
    im->pixels = stbi_load(path, &im->w, &im->h, &im->ch, MAX_CHANNELS);
    if (!im->pixels) {
        free(im);
        return NULL;
    }
    im->ch = MAX_CHANNELS; /* stb forced to 4 with STBI_rgba */
    return im;
}

static void img_free(Image *im) {
    if (!im) return;
    stbi_image_free(im->pixels);
    free(im);
}

static uint8_t *img_pixel(Image *im, int x, int y) {
    return im->pixels + (y * im->w + x) * im->ch;
}

/*
 * Find needle in haystack using sliding window.
 * Returns 1 if found, with output params set (center-x, center-y).
 * similarity: 0..1, higher = more strict.  0.95 = 95% pixels within tolerance.
 */
int template_match(const char *haystack_path, const char *needle_path,
                   double threshold,
                   int *out_x, int *out_y, int *out_w, int *out_h) {
    Image *haystack = img_load(haystack_path);
    Image *needle   = img_load(needle_path);
    if (!haystack || !needle) {
        img_free(haystack);
        img_free(needle);
        return 0;
    }

    int max_x = haystack->w - needle->w;
    int max_y = haystack->h - needle->h;
    if (max_x < 0 || max_y < 0) {
        img_free(haystack);
        img_free(needle);
        return 0;
    }

    int best_x = 0, best_y = 0;
    double best_sim = 0;
    int ch = needle->ch; /* both 4 */

    for (int y = 0; y <= max_y; y++) {
        for (int x = 0; x <= max_x; x++) {
            double total = 0;
            for (int sy = 0; sy < needle->w && sy < needle->h; sy++) {
                for (int sx = 0; sx < needle->w && sx < needle->h; sx++) {
                    uint8_t *hp = img_pixel(haystack, x + sx, y + sy);
                    uint8_t *np = img_pixel(needle, sx, sy);
                    total += pixel_sim(hp, np, ch);
                }
            }
            double sim = total / (needle->w * needle->h);
            if (sim > best_sim) {
                best_sim = sim;
                best_x = x;
                best_y = y;
            }
        }
    }

    img_free(haystack);
    img_free(needle);

    if (best_sim >= threshold) {
        *out_x = best_x;
        *out_y = best_y;
        *out_w = needle->w;
        *out_h = needle->h;
        return 1;
    }
    return 0;
}

/* Combined: screenshot -> find -> tap center. Returns 1 on success. */
int template_find_and_tap(const char *serial, const char *needle_path,
                          double threshold) {
    char ss_path[] = "/tmp/adb_ss_XXXXXX";
    int fd = mkstemp(ss_path);
    if (fd == -1) return 0;
    close(fd);

    int ok = 0;
    if (adb_screenshot(serial, ss_path) == 0) {
        int x, y, w, h;
        if (template_match(ss_path, needle_path, threshold, &x, &y, &w, &h)) {
            adb_tap(serial, x + w / 2, y + h / 2);
            ok = 1;
        }
    }
    remove(ss_path);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  CLI                                                                */
/* ------------------------------------------------------------------ */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> [args...]\n"
        "\n"
        "Commands:\n"
        "  tap <x> <y> [serial]           Tap at coordinates\n"
        "  swipe <x1> <y1> <x2> <y2> [ms] [serial]\n"
        "                                 Swipe from (x1,y1) to (x2,y2)\n"
        "  screenshot [path] [serial]     Take screenshot\n"
        "  size [serial]                   Get screen size\n"
        "  devices                         List connected devices\n"
        "  find <needle> [threshold] [serial]\n"
        "                                 Find needle in screenshot\n"
        "  find-tap <needle> [threshold] [serial]\n"
        "                                 Find needle and tap its center\n"
        "  help                            Show this message\n"
        "\n"
        "threshold: 0.0-1.0 (default 0.95)\n",
        prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(cmd, "devices") == 0) {
        int count;
        char **list = adb_devices(&count);
        if (!list || count == 0) {
            printf("No devices found.\n");
        } else {
            for (int i = 0; i < count; i++)
                printf("%s\n", list[i]);
        }
        adb_devices_free(list, count);
        return 0;
    }

    /* Extract optional serial from last arg if it looks like a serial */
    const char *serial = NULL;
    int argc_minus_serial = argc;
    if (argc > 3) {
        /* crude: if last arg is longer than 4 chars and contains a dot or is
         * alphanumeric long string, treat as serial */
        const char *last = argv[argc - 1];
        size_t llen = strlen(last);
        if (llen > 4 && (strchr(last, '.') != NULL || strchr(last, ':') != NULL))
            serial = last, argc_minus_serial--;
    }

    if (strcmp(cmd, "tap") == 0 && argc_minus_serial >= 4) {
        int x = atoi(argv[2]);
        int y = atoi(argv[3]);
        return adb_tap(serial, x, y) != 0;
    }

    if (strcmp(cmd, "swipe") == 0 && argc_minus_serial >= 6) {
        int x1 = atoi(argv[2]), y1 = atoi(argv[3]);
        int x2 = atoi(argv[4]), y2 = atoi(argv[5]);
        int ms = argc_minus_serial >= 7 ? atoi(argv[6]) : 300;
        return adb_swipe(serial, x1, y1, x2, y2, ms) != 0;
    }

    if (strcmp(cmd, "screenshot") == 0) {
        const char *path;
        if (argc_minus_serial >= 3)
            path = argv[2];
        else {
            static char auto_path[256];
            snprintf(auto_path, sizeof(auto_path), "screenshot_%d.png", (int)time(NULL));
            path = auto_path;
        }
        return adb_screenshot(serial, path) != 0;
    }

    if (strcmp(cmd, "size") == 0) {
        int w, h;
        if (adb_screen_size(serial, &w, &h) != 0) {
            fprintf(stderr, "Failed to get screen size\n");
            return 1;
        }
        printf("%dx%d\n", w, h);
        return 0;
    }

    if ((strcmp(cmd, "find") == 0 || strcmp(cmd, "find-tap") == 0) &&
        argc_minus_serial >= 3) {
        const char *needle = argv[2];
        double threshold = 0.95;
        if (argc_minus_serial >= 4)
            threshold = atof(argv[3]);

        if (strcmp(cmd, "find-tap") == 0) {
            return template_find_and_tap(serial, needle, threshold) ? 0 : 1;
        }

        char ss_path[] = "/tmp/adb_ss_XXXXXX";
        int fd = mkstemp(ss_path);
        if (fd == -1) { perror("mkstemp"); return 1; }
        close(fd);

        int ok = 0;
        if (adb_screenshot(serial, ss_path) == 0) {
            int x, y, w, h;
            if (template_match(ss_path, needle, threshold, &x, &y, &w, &h)) {
                printf("Found at (%d,%d) size %dx%d center (%d,%d)\n",
                       x, y, w, h, x + w / 2, y + h / 2);
                ok = 1;
            } else {
                printf("Not found (threshold=%.2f)\n", threshold);
            }
        }
        remove(ss_path);
        return ok ? 0 : 1;
    }

    print_usage(argv[0]);
    return 1;
}
