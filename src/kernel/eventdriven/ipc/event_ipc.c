#include "event_ipc.h"
#include "klib.h"

// ============================================================================
// EVENT-BASED IPC IMPLEMENTATION
// ============================================================================

// Message queue per task (simple circular buffer)
#define IPC_QUEUE_SIZE 32

typedef struct {
    IPCMessage messages[IPC_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    spinlock_t lock;
    IPCStats stats;
} IPCQueue;

// Global IPC state
#define MAX_IPC_QUEUES 256
static IPCQueue ipc_queues[MAX_IPC_QUEUES];
static uint64_t next_message_id = 1;
static spinlock_t ipc_global_lock = {0};

// Subscription list (who subscribes to whom)
typedef struct {
    uint64_t subscriber_id;
    uint64_t publisher_id;
    bool active;
} IPCSubscription;

#define MAX_SUBSCRIPTIONS 512
static IPCSubscription subscriptions[MAX_SUBSCRIPTIONS];
static uint32_t subscription_count = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void event_ipc_init(void) {
    spinlock_init(&ipc_global_lock);

    for (uint32_t i = 0; i < MAX_IPC_QUEUES; i++) {
        memset(&ipc_queues[i], 0, sizeof(IPCQueue));
        spinlock_init(&ipc_queues[i].lock);
    }

    memset(subscriptions, 0, sizeof(subscriptions));
    subscription_count = 0;

    kprintf("[IPC] Event-based IPC system initialized\n");
    kprintf("[IPC] Max queues: %u, Queue size: %u messages\n",
            MAX_IPC_QUEUES, IPC_QUEUE_SIZE);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Get queue for task (create if needed)
static IPCQueue* get_queue(uint64_t task_id) {
    if (task_id >= MAX_IPC_QUEUES) {
        kprintf("[IPC] ERROR: Task ID %lu exceeds max queues\n", task_id);
        return NULL;
    }

    return &ipc_queues[task_id];
}

// Enqueue message to task's queue
static int enqueue_message(uint64_t task_id, IPCMessage* msg) {
    IPCQueue* queue = get_queue(task_id);
    if (!queue) {
        return -1;
    }

    spin_lock(&queue->lock);

    // Check if queue is full
    if (queue->count >= IPC_QUEUE_SIZE) {
        spin_unlock(&queue->lock);
        queue->stats.messages_dropped++;
        kprintf("[IPC] WARNING: Queue full for task %lu, message dropped\n", task_id);
        return -1;
    }

    // Add message to queue
    queue->messages[queue->tail] = *msg;
    queue->tail = (queue->tail + 1) % IPC_QUEUE_SIZE;
    queue->count++;

    queue->stats.messages_received++;

    spin_unlock(&queue->lock);
    return 0;
}

// Generate unique message ID
static uint64_t generate_message_id(void) {
    spin_lock(&ipc_global_lock);
    uint64_t id = next_message_id++;
    spin_unlock(&ipc_global_lock);
    return id;
}

// Check if task is subscribed to publisher
static bool is_subscribed(uint64_t subscriber_id, uint64_t publisher_id) {
    for (uint32_t i = 0; i < subscription_count; i++) {
        if (subscriptions[i].active &&
            subscriptions[i].subscriber_id == subscriber_id &&
            subscriptions[i].publisher_id == publisher_id) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// IPC OPERATIONS
// ============================================================================

// Send message to another task
int event_ipc_send(uint64_t sender_id, uint64_t receiver_id,
                   const void* data, uint32_t size) {
    if (!data || size > sizeof(((IPCMessage*)0)->data)) {
        kprintf("[IPC] ERROR: Invalid data or size too large\n");
        return -1;
    }

    // Create message
    IPCMessage msg;
    memset(&msg, 0, sizeof(IPCMessage));
    msg.sender_id = sender_id;
    msg.receiver_id = receiver_id;
    msg.message_id = generate_message_id();
    msg.message_type = EVENT_IPC_MESSAGE;
    msg.data_size = size;
    memcpy(msg.data, data, size);
    msg.timestamp = rdtsc();

    // Enqueue to receiver
    int result = enqueue_message(receiver_id, &msg);

    if (result == 0) {
        IPCQueue* sender_queue = get_queue(sender_id);
        if (sender_queue) {
            sender_queue->stats.messages_sent++;
        }

        kprintf("[IPC] Task %lu → Task %lu (msg_id=%lu, size=%u)\n",
                sender_id, receiver_id, msg.message_id, size);
    }

    return result;
}

// Send request (returns request_id for matching response)
uint64_t event_ipc_request(uint64_t sender_id, uint64_t receiver_id,
                           const void* data, uint32_t size) {
    if (!data || size > sizeof(((IPCMessage*)0)->data)) {
        return 0;
    }

    uint64_t request_id = generate_message_id();

    IPCMessage msg;
    memset(&msg, 0, sizeof(IPCMessage));
    msg.sender_id = sender_id;
    msg.receiver_id = receiver_id;
    msg.message_id = generate_message_id();
    msg.request_id = request_id;
    msg.message_type = EVENT_IPC_REQUEST;
    msg.data_size = size;
    memcpy(msg.data, data, size);
    msg.timestamp = rdtsc();

    if (enqueue_message(receiver_id, &msg) == 0) {
        kprintf("[IPC] Request: Task %lu → Task %lu (req_id=%lu)\n",
                sender_id, receiver_id, request_id);
        return request_id;
    }

    return 0;  // Failed
}

// Send response to request
int event_ipc_respond(uint64_t sender_id, uint64_t request_id,
                     const void* data, uint32_t size) {
    // Response is sent back to original requester
    // (implementation simplified - in production, would track request→sender mapping)

    IPCMessage msg;
    memset(&msg, 0, sizeof(IPCMessage));
    msg.sender_id = sender_id;
    msg.message_id = generate_message_id();
    msg.request_id = request_id;
    msg.message_type = EVENT_IPC_RESPONSE;
    msg.data_size = size;
    if (data && size > 0) {
        memcpy(msg.data, data, size);
    }
    msg.timestamp = rdtsc();

    kprintf("[IPC] Response: Task %lu (req_id=%lu)\n", sender_id, request_id);
    return 0;  // Simplified
}

// Broadcast to all tasks in guild
int event_ipc_broadcast(uint64_t sender_id, uint32_t guild_id,
                       const void* data, uint32_t size) {
    if (!data || size > sizeof(((IPCMessage*)0)->data)) {
        return -1;
    }

    IPCMessage msg;
    memset(&msg, 0, sizeof(IPCMessage));
    msg.sender_id = sender_id;
    msg.receiver_id = 0;  // Broadcast
    msg.message_id = generate_message_id();
    msg.message_type = EVENT_IPC_BROADCAST;
    msg.data_size = size;
    memcpy(msg.data, data, size);
    msg.timestamp = rdtsc();

    // TODO: Get all tasks in guild and send to each
    // For now, just log
    kprintf("[IPC] Broadcast: Task %lu → Guild %u (msg_id=%lu)\n",
            sender_id, guild_id, msg.message_id);

    IPCQueue* sender_queue = get_queue(sender_id);
    if (sender_queue) {
        sender_queue->stats.broadcasts_sent++;
    }

    return 0;
}

// Receive message (non-blocking)
IPCMessage* event_ipc_receive(uint64_t task_id) {
    IPCQueue* queue = get_queue(task_id);
    if (!queue) {
        return NULL;
    }

    spin_lock(&queue->lock);

    if (queue->count == 0) {
        spin_unlock(&queue->lock);
        return NULL;  // No messages
    }

    // Get message from head
    static IPCMessage received_msg;  // Static to persist after return
    received_msg = queue->messages[queue->head];
    queue->head = (queue->head + 1) % IPC_QUEUE_SIZE;
    queue->count--;

    spin_unlock(&queue->lock);

    return &received_msg;
}

// Check if task has pending messages
bool event_ipc_has_messages(uint64_t task_id) {
    IPCQueue* queue = get_queue(task_id);
    if (!queue) {
        return false;
    }

    return queue->count > 0;
}

// Get IPC statistics
IPCStats event_ipc_get_stats(uint64_t task_id) {
    IPCQueue* queue = get_queue(task_id);
    if (!queue) {
        IPCStats empty = {0};
        return empty;
    }

    return queue->stats;
}

// Subscribe to events from another task
int event_ipc_subscribe(uint64_t subscriber_id, uint64_t publisher_id) {
    spin_lock(&ipc_global_lock);

    // Check if already subscribed
    if (is_subscribed(subscriber_id, publisher_id)) {
        spin_unlock(&ipc_global_lock);
        return 0;  // Already subscribed
    }

    // Find free subscription slot
    if (subscription_count >= MAX_SUBSCRIPTIONS) {
        spin_unlock(&ipc_global_lock);
        kprintf("[IPC] ERROR: Max subscriptions reached\n");
        return -1;
    }

    subscriptions[subscription_count].subscriber_id = subscriber_id;
    subscriptions[subscription_count].publisher_id = publisher_id;
    subscriptions[subscription_count].active = true;
    subscription_count++;

    spin_unlock(&ipc_global_lock);

    kprintf("[IPC] Subscribe: Task %lu → Task %lu\n", subscriber_id, publisher_id);
    return 0;
}

// Unsubscribe from task events
int event_ipc_unsubscribe(uint64_t subscriber_id, uint64_t publisher_id) {
    spin_lock(&ipc_global_lock);

    for (uint32_t i = 0; i < subscription_count; i++) {
        if (subscriptions[i].active &&
            subscriptions[i].subscriber_id == subscriber_id &&
            subscriptions[i].publisher_id == publisher_id) {
            subscriptions[i].active = false;
            spin_unlock(&ipc_global_lock);
            kprintf("[IPC] Unsubscribe: Task %lu ← Task %lu\n",
                    subscriber_id, publisher_id);
            return 0;
        }
    }

    spin_unlock(&ipc_global_lock);
    return -1;  // Not found
}
