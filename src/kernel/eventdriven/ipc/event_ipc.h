#ifndef EVENT_IPC_H
#define EVENT_IPC_H

#include "ktypes.h"
#include "task.h"

// ============================================================================
// EVENT-BASED IPC - INNOVATIVE BoxOS Communication!
// ============================================================================
// PHILOSOPHY: NO SYSCALLS, NO PIPES, NO SOCKETS!
//
// Tasks communicate by sending EVENTS to each other or to DECKS:
//   Task A → Event → Task B (direct task-to-task)
//   Task A → Event → Deck → Process → Response Event → Task A
//
// This is ASYNCHRONOUS, NON-BLOCKING, and SCALABLE!
// ============================================================================

// Event types for IPC
#define EVENT_IPC_MESSAGE       0x1000  // Simple message between tasks
#define EVENT_IPC_REQUEST       0x1001  // Request with expected response
#define EVENT_IPC_RESPONSE      0x1002  // Response to request
#define EVENT_IPC_BROADCAST     0x1003  // Broadcast to all tasks in guild
#define EVENT_IPC_SUBSCRIBE     0x1004  // Subscribe to events from task
#define EVENT_IPC_UNSUBSCRIBE   0x1005  // Unsubscribe from events

// IPC Message structure
typedef struct {
    uint64_t sender_id;          // Task ID of sender
    uint64_t receiver_id;        // Task ID of receiver (0 = broadcast)
    uint64_t message_id;         // Unique message ID
    uint64_t request_id;         // For request/response pairing
    uint32_t message_type;       // EVENT_IPC_*
    uint32_t data_size;          // Size of data
    uint8_t  data[256];          // Message payload
    uint64_t timestamp;          // When sent (RDTSC)
} IPCMessage;

// IPC Statistics per task
typedef struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t messages_dropped;   // Rate-limited
    uint64_t broadcasts_sent;
    uint64_t broadcasts_received;
} IPCStats;

// ============================================================================
// IPC API
// ============================================================================

// Initialize IPC system
void event_ipc_init(void);

// Send message to another task
int event_ipc_send(uint64_t sender_id, uint64_t receiver_id,
                   const void* data, uint32_t size);

// Send request (expect response)
uint64_t event_ipc_request(uint64_t sender_id, uint64_t receiver_id,
                           const void* data, uint32_t size);

// Send response to request
int event_ipc_respond(uint64_t sender_id, uint64_t request_id,
                     const void* data, uint32_t size);

// Broadcast message to all tasks in guild
int event_ipc_broadcast(uint64_t sender_id, uint32_t guild_id,
                       const void* data, uint32_t size);

// Receive message (non-blocking)
IPCMessage* event_ipc_receive(uint64_t task_id);

// Check if task has pending messages
bool event_ipc_has_messages(uint64_t task_id);

// Get IPC statistics for task
IPCStats event_ipc_get_stats(uint64_t task_id);

// Subscribe to events from another task
int event_ipc_subscribe(uint64_t subscriber_id, uint64_t publisher_id);

// Unsubscribe from task events
int event_ipc_unsubscribe(uint64_t subscriber_id, uint64_t publisher_id);

#endif // EVENT_IPC_H
