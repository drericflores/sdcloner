// sdcloner_engine.h
// Public API for SD Cloner engine (C)
// License: GPLv3

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// High-level clone entry point.
// If dest_disk == NULL or empty, a local image is created in ~/SDCloner/images/.
// If dest_disk is provided, the engine decides raw vs FS-aware and burns it.
// dest_capacity_hint (bytes) is optional (0 if not used).
// Returns 0 on success, non-zero on failure.
int sdcloner_clone(const char* src_disk, const char* dest_disk, uint64_t dest_capacity_hint);

// Burn an existing image (.img or .img.gz) to a destination block device.
// Returns 0 on success, non-zero on failure.
int burn_image_to_disk(const char* image_path, const char* dest_disk);

#ifdef __cplusplus
}
#endif
