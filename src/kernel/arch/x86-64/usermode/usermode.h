#ifndef USERMODE_H
#define USERMODE_H

#include "ktypes.h"
#include "task.h"

// ============================================================================
// USER MODE (Ring 3) - Production-Ready Implementation
// ============================================================================
//
// BoxOS Philosophy: NO SYSCALLS!
// Instead: Tasks send events to kernel decks and receive responses
//
// Security Model:
// - Ring 3: User code runs here (unprivileged)
// - Ring 0: Kernel code (privileged)
// - Transition Ring 3→0: Via event messages (monitored & throttled)
// - Transition Ring 0→3: Via iret instruction
//
// ============================================================================

// ============================================================================
// USER MODE TASK FLAGS
// ============================================================================

#define USER_TASK_FLAG_RING3        (1 << 0)   // Task runs in Ring 3
#define USER_TASK_FLAG_CAN_SEND     (1 << 1)   // Can send events to kernel
#define USER_TASK_FLAG_THROTTLED    (1 << 2)   // Rate-limited
#define USER_TASK_FLAG_SANDBOXED    (1 << 3)   // Extra restricted

// ============================================================================
// USER MODE CONTEXT
// ============================================================================

typedef struct {
    // User identity
    char username[32];             // Current user running this task
    uint32_t user_id;              // UID
    uint32_t group_id;             // GID

    // Permissions
    uint32_t permissions;          // Permission flags

    // Resource limits
    uint64_t memory_limit;         // Max memory in bytes
    uint64_t cpu_quota;            // CPU time quota
    uint64_t io_quota;             // I/O operations quota

    // Statistics
    uint64_t event_count;          // Events sent to kernel
    uint64_t last_event_time;      // RDTSC of last event
    uint64_t throttle_count;       // Times throttled
} UserModeContext;

// ============================================================================
// USER MODE API - For Kernel Use
// ============================================================================

// Initialize user mode subsystem
void usermode_init(void);

// Create user-mode task
Task* usermode_create_task(const char* name, void* entry_point,
                           const char* username, uint8_t energy);

// Switch to user mode (called by kernel to enter Ring 3)
void usermode_enter(Task* task);

// Return from user mode (called when user task sends event)
void usermode_exit(Task* task);

// Check if task is user-mode
bool usermode_is_user_task(Task* task);

// Get user context for task
UserModeContext* usermode_get_context(Task* task);

// ============================================================================
// USER MODE MEMORY - For Setting Up User Space
// ============================================================================

// Create user-mode page tables (separate from kernel)
uint64_t usermode_create_page_table(void);

// Map user code/data into user space
int usermode_map_memory(uint64_t page_table, void* virt, void* phys,
                       uint64_t size, uint32_t flags);

// Create user stack
void* usermode_create_stack(uint64_t page_table, uint64_t size);

// ============================================================================
// USER MODE PERMISSIONS
// ============================================================================

#define PERM_READ_FILES      (1 << 0)
#define PERM_WRITE_FILES     (1 << 1)
#define PERM_CREATE_FILES    (1 << 2)
#define PERM_DELETE_FILES    (1 << 3)
#define PERM_NETWORK         (1 << 4)
#define PERM_HARDWARE        (1 << 5)
#define PERM_CREATE_TASKS    (1 << 6)
#define PERM_ADMIN           (1 << 7)

// Check permission
bool usermode_has_permission(Task* task, uint32_t permission);

// Grant permission
int usermode_grant_permission(Task* task, uint32_t permission);

// Revoke permission
int usermode_revoke_permission(Task* task, uint32_t permission);

// ============================================================================
// EVENT-BASED COMMUNICATION (Instead of Syscalls!)
// ============================================================================

// User task sends event to kernel deck
// This is the ONLY way user code communicates with kernel
// Returns: event_id for tracking response
uint64_t usermode_send_event(Task* task, uint64_t deck_id,
                             uint64_t event_type, void* data, uint64_t size);

// Wait for event response (puts task to sleep until response arrives)
int usermode_wait_response(Task* task, uint64_t event_id,
                           void* response_buf, uint64_t* size_out);

// Check if response is ready (non-blocking)
bool usermode_response_ready(Task* task, uint64_t event_id);

// ============================================================================
// SECURITY & THROTTLING
// ============================================================================

// Rate limiting: prevent user tasks from flooding kernel with events
#define USER_EVENT_RATE_LIMIT_MS  10  // Min 10ms between events
#define USER_EVENT_BURST_LIMIT    20  // Max 20 events in burst

// Check if user task can send event now
bool usermode_can_send_event(Task* task);

// Record event (for rate limiting)
void usermode_record_event(Task* task);

// Throttle abusive task
void usermode_throttle(Task* task, uint64_t duration_ms);

// ============================================================================
// ASSEMBLY HELPERS (Implemented in usermode.asm)
// ============================================================================

// Jump to Ring 3 with given context
extern void usermode_jump_ring3(uint64_t rip, uint64_t rsp, uint64_t rflags);

// Return to Ring 0 (called by interrupt handler when user sends event)
extern void usermode_return_ring0(void);

#endif // USERMODE_H
