#include "usermode.h"
#include "klib.h"
#include "vmm.h"
#include "pmm.h"
#include "task.h"
#include "gdt.h"

// ============================================================================
// USER MODE IMPLEMENTATION - Ring 3 Support for BoxOS
// ============================================================================

// User mode task extension (stored in task->args for user tasks)
typedef struct {
    UserModeContext context;
    uint64_t* user_page_table;
    void* user_stack;
    uint64_t user_stack_size;

    // Event tracking for rate limiting
    uint64_t last_event_times[USER_EVENT_BURST_LIMIT];
    uint32_t event_index;
    bool throttled;
    uint64_t throttle_until;
} UserModeTaskData;

// Global state
static spinlock_t usermode_lock = {0};
static bool usermode_initialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void usermode_init(void) {
    spinlock_init(&usermode_lock);
    usermode_initialized = true;

    kprintf("[USERMODE] User mode subsystem initialized\n");
    kprintf("[USERMODE] Security: Ring 3 tasks use event-based kernel communication\n");
    kprintf("[USERMODE] Rate limit: Max %u events per task, min %ums between events\n",
            USER_EVENT_BURST_LIMIT, USER_EVENT_RATE_LIMIT_MS);
}

// ============================================================================
// USER MODE TASK CREATION
// ============================================================================

Task* usermode_create_task(const char* name, void* entry_point,
                           const char* username, uint8_t energy) {
    if (!usermode_initialized) {
        kprintf("[USERMODE] ERROR: Not initialized\n");
        return NULL;
    }

    // Allocate user mode data
    UserModeTaskData* user_data = (UserModeTaskData*)kmalloc(sizeof(UserModeTaskData));
    if (!user_data) {
        kprintf("[USERMODE] ERROR: Failed to allocate user data\n");
        return NULL;
    }

    memset(user_data, 0, sizeof(UserModeTaskData));

    // Set up user context
    strncpy(user_data->context.username, username, sizeof(user_data->context.username) - 1);
    user_data->context.user_id = 1000;  // Non-root user
    user_data->context.group_id = 1000;

    // Default permissions for normal user
    user_data->context.permissions = PERM_READ_FILES | PERM_WRITE_FILES |
                                     PERM_CREATE_FILES | PERM_CREATE_TASKS;

    // Resource limits
    user_data->context.memory_limit = 16 * 1024 * 1024;  // 16MB per task
    user_data->context.cpu_quota = 1000000;  // CPU time units
    user_data->context.io_quota = 10000;     // I/O operations

    // Create user page table (separate from kernel)
    user_data->user_page_table = (uint64_t*)usermode_create_page_table();
    if (!user_data->user_page_table) {
        kfree(user_data);
        return NULL;
    }

    // Create user stack (4KB for now, can grow)
    user_data->user_stack_size = 4096;
    user_data->user_stack = usermode_create_stack((uint64_t)user_data->user_page_table,
                                                   user_data->user_stack_size);
    if (!user_data->user_stack) {
        kfree(user_data);
        return NULL;
    }

    // Create kernel task (this will be upgraded to Ring 3)
    Task* task = task_spawn_with_args(name, entry_point, user_data, energy);
    if (!task) {
        kfree(user_data);
        return NULL;
    }

    // Mark as user-mode task
    task->user_mode = true;  // Ring 3 task
    task->page_table = (uint64_t)user_data->user_page_table;

    kprintf("[USERMODE] Created user task '%s' for user '%s'\n", name, username);
    kprintf("[USERMODE]   Memory limit: %lu KB\n", user_data->context.memory_limit / 1024);
    kprintf("[USERMODE]   Permissions: 0x%x\n", user_data->context.permissions);

    return task;
}

// ============================================================================
// PAGE TABLE MANAGEMENT
// ============================================================================

uint64_t usermode_create_page_table(void) {
    // Allocate PML4 (top-level page table)
    void* pml4 = pmm_alloc(1);  // Allocate 1 page
    if (!pml4) {
        kprintf("[USERMODE] ERROR: Failed to allocate PML4\n");
        return 0;
    }

    memset(pml4, 0, PMM_PAGE_SIZE);

    // Copy kernel mappings to upper half (0xFFFF800000000000+)
    // This allows kernel to remain accessible even in user mode page tables
    // But user code can't access it due to Ring protection

    kprintf("[USERMODE] Created user page table at %p\n", pml4);
    return (uint64_t)pml4;
}

int usermode_map_memory(uint64_t page_table, void* virt, void* phys,
                       uint64_t size, uint32_t flags) {
    // Map memory into user space
    // Flags should include USER bit (0x04) for Ring 3 access

    uint32_t user_flags = flags | 0x07;  // Present | RW | User

    // Use VMM to map pages
    for (uint64_t offset = 0; offset < size; offset += PMM_PAGE_SIZE) {
        void* v = (void*)((uintptr_t)virt + offset);
        void* p = (void*)((uintptr_t)phys + offset);

        // Map using vmm_map_page equivalent
        // TODO: Implement proper page table walking and mapping
    }

    return 0;
}

void* usermode_create_stack(uint64_t page_table, uint64_t size) {
    // Allocate physical frames for stack
    size_t pages = (size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;

    void* stack_phys = pmm_alloc(pages);  // Allocate required pages
    if (!stack_phys) {
        return NULL;
    }

    // Map stack to high user address (e.g., 0x7FFFFFFFFFFF - stack_size)
    void* stack_virt = (void*)(0x7FFFFFFFF000ULL - size);

    usermode_map_memory(page_table, stack_virt, stack_phys, size, 0x07);

    kprintf("[USERMODE] Created user stack at %p (%lu bytes)\n", stack_virt, size);

    // Return top of stack (stack grows down)
    return (void*)((uintptr_t)stack_virt + size);
}

// ============================================================================
// RING 3 TRANSITION
// ============================================================================

void usermode_enter(Task* task) {
    if (!task || !task->args) {
        kprintf("[USERMODE] ERROR: Invalid task for Ring 3 entry\n");
        return;
    }

    UserModeTaskData* user_data = (UserModeTaskData*)task->args;

    kprintf("[USERMODE] Entering Ring 3 for task '%s'\n", task->name);
    kprintf("[USERMODE]   User: %s\n", user_data->context.username);
    kprintf("[USERMODE]   Entry: %p\n", task->entry_point);
    kprintf("[USERMODE]   Stack: %p\n", user_data->user_stack);

    // Set up segments for Ring 3
    // GDT entries:
    //   0x18: Kernel Code (Ring 0)
    //   0x20: Kernel Data (Ring 0)
    //   0x28: User Code (Ring 3)  << We use this
    //   0x30: User Data (Ring 3)  << We use this

    uint64_t user_cs = 0x2B;  // User code segment (0x28 | 3 for RPL=3)
    uint64_t user_ds = 0x33;  // User data segment (0x30 | 3 for RPL=3)
    uint64_t user_rflags = 0x202;  // IF=1 (interrupts enabled)

    // Switch to user page table
    asm volatile("mov %0, %%cr3" :: "r"(task->page_table) : "memory");

    // Jump to Ring 3
    // This will use iret to switch to Ring 3
    usermode_jump_ring3((uint64_t)task->entry_point,
                       (uint64_t)user_data->user_stack,
                       user_rflags);

    // Should never return here
    panic("[USERMODE] CRITICAL: Returned from Ring 3!");
}

void usermode_exit(Task* task) {
    // Called when user task sends event or exits
    kprintf("[USERMODE] Exiting Ring 3 for task '%s'\n", task->name);

    // Switch back to kernel page table
    // (Interrupt handler already did this)

    // Task will be rescheduled by kernel
}

// ============================================================================
// PERMISSIONS & SECURITY
// ============================================================================

bool usermode_is_user_task(Task* task) {
    if (!task || !task->args) return false;

    // Check if task has UserModeTaskData
    // In a real implementation, we'd have a flag in Task struct
    return true;  // Simplified for now
}

UserModeContext* usermode_get_context(Task* task) {
    if (!task || !task->args) return NULL;

    UserModeTaskData* user_data = (UserModeTaskData*)task->args;
    return &user_data->context;
}

bool usermode_has_permission(Task* task, uint32_t permission) {
    UserModeContext* ctx = usermode_get_context(task);
    if (!ctx) return false;

    return (ctx->permissions & permission) != 0;
}

int usermode_grant_permission(Task* task, uint32_t permission) {
    UserModeContext* ctx = usermode_get_context(task);
    if (!ctx) return -1;

    spin_lock(&usermode_lock);
    ctx->permissions |= permission;
    spin_unlock(&usermode_lock);

    kprintf("[USERMODE] Granted permission 0x%x to task '%s'\n", permission, task->name);
    return 0;
}

int usermode_revoke_permission(Task* task, uint32_t permission) {
    UserModeContext* ctx = usermode_get_context(task);
    if (!ctx) return -1;

    spin_lock(&usermode_lock);
    ctx->permissions &= ~permission;
    spin_unlock(&usermode_lock);

    kprintf("[USERMODE] Revoked permission 0x%x from task '%s'\n", permission, task->name);
    return 0;
}

// ============================================================================
// EVENT-BASED COMMUNICATION (Instead of Syscalls!)
// ============================================================================

uint64_t usermode_send_event(Task* task, uint64_t deck_id,
                             uint64_t event_type, void* data, uint64_t size) {
    if (!usermode_can_send_event(task)) {
        kprintf("[USERMODE] Task '%s' throttled - too many events\n", task->name);
        return 0;
    }

    // Record event for rate limiting
    usermode_record_event(task);

    // Send event to kernel deck via event-driven system
    // This is implemented in eventdriven_system.c

    UserModeContext* ctx = usermode_get_context(task);
    if (ctx) {
        ctx->event_count++;
    }

    // Generate event ID
    uint64_t event_id = task->task_id << 32 | ctx->event_count;

    kprintf("[USERMODE] Task '%s' sent event 0x%llx to deck %llu\n",
            task->name, event_id, deck_id);

    return event_id;
}

int usermode_wait_response(Task* task, uint64_t event_id,
                           void* response_buf, uint64_t* size_out) {
    // Put task to sleep until response arrives
    // Event-driven system will wake task when deck responds

    kprintf("[USERMODE] Task '%s' waiting for response to event 0x%llx\n",
            task->name, event_id);

    // Sleep task
    task->state = TASK_STATE_WAITING_EVENT;

    return 0;
}

bool usermode_response_ready(Task* task, uint64_t event_id) {
    // Check task's message queue for response
    if (!task->message_queue) return false;

    // Search message queue for event_id
    for (uint32_t i = 0; i < task->message_queue->count; i++) {
        uint32_t idx = (task->message_queue->head + i) % TASK_MESSAGE_QUEUE_SIZE;
        if (task->message_queue->messages[idx].message_type == event_id) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// RATE LIMITING & THROTTLING
// ============================================================================

bool usermode_can_send_event(Task* task) {
    if (!task || !task->args) return false;

    UserModeTaskData* user_data = (UserModeTaskData*)task->args;

    // Check if task is currently throttled
    if (user_data->throttled) {
        uint64_t now;
        asm volatile("rdtsc" : "=A"(now));

        if (now < user_data->throttle_until) {
            return false;  // Still throttled
        }

        // Throttle expired
        user_data->throttled = false;
        kprintf("[USERMODE] Task '%s' throttle expired\n", task->name);
    }

    // Check burst limit
    uint64_t now;
    asm volatile("rdtsc" : "=A"(now));

    uint64_t oldest_event_time = user_data->last_event_times[0];

    // If we have a full burst buffer, check if oldest event was recent
    if (user_data->event_index >= USER_EVENT_BURST_LIMIT) {
        // Convert RDTSC to milliseconds (rough estimate: TSC / 2000000 ≈ ms)
        uint64_t time_diff = (now - oldest_event_time) / 2000000;

        if (time_diff < USER_EVENT_RATE_LIMIT_MS) {
            // Too fast! Throttle this task
            kprintf("[USERMODE] WARNING: Task '%s' exceeding event rate limit\n", task->name);
            usermode_throttle(task, 1000);  // Throttle for 1 second
            return false;
        }
    }

    return true;
}

void usermode_record_event(Task* task) {
    if (!task || !task->args) return;

    UserModeTaskData* user_data = (UserModeTaskData*)task->args;

    uint64_t now;
    asm volatile("rdtsc" : "=A"(now));

    uint32_t idx = user_data->event_index % USER_EVENT_BURST_LIMIT;
    user_data->last_event_times[idx] = now;
    user_data->event_index++;
}

void usermode_throttle(Task* task, uint64_t duration_ms) {
    if (!task || !task->args) return;

    UserModeTaskData* user_data = (UserModeTaskData*)task->args;

    uint64_t now;
    asm volatile("rdtsc" : "=A"(now));

    user_data->throttled = true;
    user_data->throttle_until = now + (duration_ms * 2000000);  // Rough TSC estimate
    user_data->context.throttle_count++;

    kprintf("[USERMODE] Task '%s' throttled for %llu ms (count: %llu)\n",
            task->name, duration_ms, user_data->context.throttle_count);
}
