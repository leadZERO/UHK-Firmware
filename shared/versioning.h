#ifndef __VERSIONING_H__
#define __VERSIONING_H__

// Includes:

#include "slave_protocol.h"
#include "device.h"

// Macros:

#define MD5_CHECKSUM_LENGTH 32

// Provisional firmware identity for relay-enabled UHK80 halves. Agent must match
// this version exactly on both halves until an official support version is agreed.
// Keep the ordinary release version in scripts/package.json unchanged.
#define UHK_USB_LEFT_RELAY_FIRMWARE_VERSION { 18, 0, 65535 }

#define VERSION_AT_LEAST(v, MAJ, MIN, PATCH)                                                       \
    (((v).major > (MAJ)) || ((v).major == (MAJ) && (v).minor > (MIN)) ||                           \
        ((v).major == (MAJ) && (v).minor == (MIN) && (v).patch >= (PATCH)))

#define VERSION_EQUAL(v, MAJ, MIN, PATCH)                                                         \
    ((v).major == (MAJ) && (v).minor == (MIN) && (v).patch == (PATCH))

#define VERSIONS_EQUAL(v, w)                                                                      \
    ((v).major == (w).major && (v).minor == (w).minor && (v).patch == (w).patch)

// Typedefs:

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} version_t;

extern const version_t firmwareVersion;
extern const version_t deviceProtocolVersion;
extern const version_t moduleProtocolVersion;
extern const version_t userConfigVersion;
extern const version_t hardwareConfigVersion;
extern const version_t smartMacrosVersion;
extern const version_t dongleProtocolVersion;

extern const char gitRepo[];
extern const char gitTag[];

#ifdef DEVICE_COUNT
extern const char *const DeviceMD5Checksums[DEVICE_COUNT + 1];
#endif
extern const char *const ModuleMD5Checksums[ModuleId_AllModuleCount];

#endif
