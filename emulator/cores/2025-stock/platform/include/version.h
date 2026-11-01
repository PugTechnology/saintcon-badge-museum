#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_VERSION_MAJOR    0
#define FIRMWARE_VERSION_MINOR    2
#define FIRMWARE_VERSION_PATCH    1
#define FIRMWARE_VERSION_GIT_HASH "360c8929"
#define FIRMWARE_VERSION_STRING   "0.2.1+360c8929"

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    char git_hash[9];
} version_t;

bool version_parse(const char *version_str, version_t *version);
int version_compare(version_t *a, version_t *b);
version_t firmware_version(void);
const char *firmware_version_str(void);
int firmware_version_compare(const char *version);

#ifdef __cplusplus
}
#endif
