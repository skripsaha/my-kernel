#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

// ============================================================================
// BoxOS SYSTEM CONFIGURATION (Production-Ready)
// ============================================================================
// This file contains all system-wide constants and configuration values.
// NO MAGIC NUMBERS IN CODE - all values should be defined here.
// ============================================================================

// ============================================================================
// TIMING & DELAYS
// ============================================================================

#define DELAY_1_MILLISECOND     100000      // CPU cycles per ~1ms
#define DELAY_1_SECOND          (DELAY_1_MILLISECOND * 1000)
#define REBOOT_DELAY_CYCLES     10000000    // Delay before reboot (10M cycles)
#define SHUTDOWN_DELAY_CYCLES   10000000    // Delay before shutdown

// ============================================================================
// MEMORY CONFIGURATION
// ============================================================================

// Identity mapping range (critical for VMM)
#define IDENTITY_MAPPING_SIZE   (256 * 1024 * 1024)  // 256 MB
#define IDENTITY_MAPPING_SIZE_EXTENDED  (512 * 1024 * 1024)  // 512 MB for larger systems

// Kernel heap
#define KERNEL_HEAP_BASE        0xFFFFFFFF80000000ULL
#define KERNEL_HEAP_SIZE        (64 * 1024 * 1024)   // 64 MB

// Maximum password length
#define PASSWORD_MIN_LENGTH     4
#define PASSWORD_MAX_LENGTH     128

// ============================================================================
// HARDWARE I/O PORTS
// ============================================================================

// Keyboard
#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_COMMAND_PORT   0x64

// PIC (Programmable Interrupt Controller)
#define PIC1_COMMAND            0x20
#define PIC1_DATA               0x21
#define PIC2_COMMAND            0xA0
#define PIC2_DATA               0xA1

// PIT (Programmable Interval Timer)
#define PIT_CHANNEL0            0x40
#define PIT_COMMAND             0x43
#define PIT_FREQUENCY_HZ        100         // 100 Hz = 10ms per tick

// QEMU/Bochs shutdown ports
#define SHUTDOWN_PORT_BOCHS     0xB004
#define SHUTDOWN_PORT_QEMU_NEW  0x604
#define SHUTDOWN_PORT_QEMU_OLD  0x4004
#define SHUTDOWN_VALUE          0x2000
#define SHUTDOWN_VALUE_OLD      0x3400

// ============================================================================
// SECURITY CONFIGURATION
// ============================================================================

#define MAX_LOGIN_ATTEMPTS      5           // Account locks after this many failed logins
#define SESSION_TIMEOUT_SECONDS (30 * 60)   // 30 minutes
#define PASSWORD_HASH_ROUNDS    1000        // Number of hash iterations

// ============================================================================
// FILESYSTEM LIMITS
// ============================================================================

#define MAX_FILENAME_LENGTH     256
#define MAX_PATH_LENGTH         4096
#define MAX_OPEN_FILES          1024

// ============================================================================
// LOGGING & DEBUG
// ============================================================================

#define LOG_LEVEL_NONE          0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARNING       2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4
#define LOG_LEVEL_TRACE         5

// Default log level (change for production vs development)
#ifdef DEBUG_BUILD
    #define DEFAULT_LOG_LEVEL   LOG_LEVEL_DEBUG
#else
    #define DEFAULT_LOG_LEVEL   LOG_LEVEL_WARNING
#endif

// ============================================================================
// TASK/PROCESS LIMITS
// ============================================================================

#define MAX_TASKS               256
#define MAX_THREADS_PER_TASK    64
#define TASK_STACK_SIZE         (64 * 1024)   // 64 KB per task stack
#define KERNEL_STACK_SIZE       (16 * 1024)   // 16 KB per kernel stack

// ============================================================================
// NETWORK CONFIGURATION (Future)
// ============================================================================

#define MAX_NETWORK_CONNECTIONS 256
#define NETWORK_BUFFER_SIZE     (64 * 1024)
#define MTU_SIZE                1500

// ============================================================================
// VERSION INFORMATION
// ============================================================================

#define BOXOS_VERSION_MAJOR     2
#define BOXOS_VERSION_MINOR     0
#define BOXOS_VERSION_PATCH     0
#define BOXOS_VERSION_STRING    "2.0.0-production"
#define BOXOS_CODENAME          "InnovativeCore"

// ============================================================================
// BUILD CONFIGURATION
// ============================================================================

// Enable/disable features
#define FEATURE_NETWORKING      0           // Not yet implemented
#define FEATURE_USB             0           // Not yet implemented
#define FEATURE_AUDIO           0           // Not yet implemented
#define FEATURE_GUI             0           // Not yet implemented

// Debug features (disable in production)
#ifdef DEBUG_BUILD
    #define ENABLE_VERBOSE_LOGGING  1
    #define ENABLE_MEMORY_DEBUG     1
    #define ENABLE_ASSERTIONS       1
#else
    #define ENABLE_VERBOSE_LOGGING  0
    #define ENABLE_MEMORY_DEBUG     0
    #define ENABLE_ASSERTIONS       0
#endif

#endif // SYSTEM_CONFIG_H
