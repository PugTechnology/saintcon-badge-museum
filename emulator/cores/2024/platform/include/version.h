#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_VERSION_MAJOR    2
#define FIRMWARE_VERSION_MINOR    6
#define FIRMWARE_VERSION_PATCH    9
#define FIRMWARE_VERSION_GIT_HASH "5e9412c8"
#define FIRMWARE_VERSION_STRING   "2.6.9+5e9412c8"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    char git_hash[9];
} version_t;

bool version_parse(const char *version_str, version_t *version);
int version_compare(version_t *a, version_t *b);
version_t firmware_version();
const char *firmware_version_str();
int firmware_version_compare(const char *version);

#ifdef __cplusplus
}
#endif
