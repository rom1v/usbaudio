#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#include "config.h"

#ifndef NDEBUG
# define LOGD(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
# define LOGD(fmt, ...)
#endif
#define LOGI(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

#endif
