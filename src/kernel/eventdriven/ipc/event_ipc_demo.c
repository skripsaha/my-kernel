#include "event_ipc_demo.h"
#include "event_ipc.h"
#include "klib.h"
#include "task.h"

// ============================================================================
// EVENT-BASED IPC DEMONSTRATION - NO SYSCALLS!
// ============================================================================
// This demo shows how BoxOS tasks communicate using EVENTS instead of syscalls!
// ============================================================================

// Demo task IDs (simulate different tasks)
#define TASK_ALICE      1
#define TASK_BOB        2
#define TASK_CHARLIE    3
#define TASK_WIZARD     0

// Demo guild ID
#define GUILD_APPRENTICES  1000

// ============================================================================
// DEMO: SIMPLE MESSAGE PASSING
// ============================================================================

static void demo_simple_messages(void) {
    kprintf("\n%[H]=== DEMO 1: Simple Message Passing ===%[D]\n");
    kprintf("Task Alice → Task Bob (NO syscalls, pure events!)\n\n");

    // Alice sends message to Bob
    const char* msg1 = "Hello Bob!";
    int result = event_ipc_send(TASK_ALICE, TASK_BOB, msg1, strlen(msg1) + 1);

    if (result == 0) {
        kprintf("✓ Alice sent message to Bob\n");
    }

    // Bob receives message
    IPCMessage* received = event_ipc_receive(TASK_BOB);
    if (received) {
        kprintf("✓ Bob received: \"%s\" (from task %lu)\n",
                (char*)received->data, received->sender_id);
    }

    // Bob replies to Alice
    const char* msg2 = "Hi Alice! How are you?";
    event_ipc_send(TASK_BOB, TASK_ALICE, msg2, strlen(msg2) + 1);

    // Alice receives reply
    received = event_ipc_receive(TASK_ALICE);
    if (received) {
        kprintf("✓ Alice received: \"%s\"\n", (char*)received->data);
    }

    kprintf("\n%[S]Demo 1 complete - Tasks communicated without syscalls!%[D]\n");
}

// ============================================================================
// DEMO: REQUEST-RESPONSE PATTERN
// ============================================================================

static void demo_request_response(void) {
    kprintf("\n%[H]=== DEMO 2: Request-Response Pattern ===%[D]\n");
    kprintf("Alice asks Bob to calculate 2+2, Bob responds!\n\n");

    // Alice sends calculation request
    const char* request = "CALC:2+2";
    uint64_t request_id = event_ipc_request(TASK_ALICE, TASK_BOB,
                                            request, strlen(request) + 1);

    if (request_id > 0) {
        kprintf("✓ Alice sent request #%lu to Bob\n", request_id);
    }

    // Bob receives request
    IPCMessage* req_msg = event_ipc_receive(TASK_BOB);
    if (req_msg && req_msg->message_type == EVENT_IPC_REQUEST) {
        kprintf("✓ Bob received request: \"%s\"\n", (char*)req_msg->data);

        // Bob processes request and sends response
        const char* response = "RESULT:4";
        event_ipc_respond(TASK_BOB, req_msg->request_id, response, strlen(response) + 1);
        kprintf("✓ Bob sent response to request #%lu\n", req_msg->request_id);
    }

    // Alice receives response (in production, would check request_id matches)
    IPCMessage* resp_msg = event_ipc_receive(TASK_ALICE);
    if (resp_msg && resp_msg->message_type == EVENT_IPC_RESPONSE) {
        kprintf("✓ Alice received response: \"%s\"\n", (char*)resp_msg->data);
    }

    kprintf("\n%[S]Demo 2 complete - Request-response works!%[D]\n");
}

// ============================================================================
// DEMO: GUILD BROADCAST
// ============================================================================

static void demo_broadcast(void) {
    kprintf("\n%[H]=== DEMO 3: Guild Broadcast ===%[D]\n");
    kprintf("Wizard broadcasts announcement to all Apprentices guild!\n\n");

    const char* announcement = "Meeting at noon in the Great Hall!";
    int result = event_ipc_broadcast(TASK_WIZARD, GUILD_APPRENTICES,
                                     announcement, strlen(announcement) + 1);

    if (result == 0) {
        kprintf("✓ Wizard broadcast to guild %u\n", GUILD_APPRENTICES);
        kprintf("  Message: \"%s\"\n", announcement);
    }

    kprintf("\n%[S]Demo 3 complete - Guild broadcast sent!%[D]\n");
    kprintf("(In production, all guild members would receive this)\n");
}

// ============================================================================
// DEMO: SUBSCRIBE / UNSUBSCRIBE
// ============================================================================

static void demo_subscribe(void) {
    kprintf("\n%[H]=== DEMO 4: Publish-Subscribe Pattern ===%[D]\n");
    kprintf("Charlie subscribes to Bob's events (pub-sub architecture!)\n\n");

    // Charlie subscribes to Bob
    int result = event_ipc_subscribe(TASK_CHARLIE, TASK_BOB);
    if (result == 0) {
        kprintf("✓ Charlie is now subscribed to Bob's events\n");
    }

    // Later, Charlie unsubscribes
    result = event_ipc_unsubscribe(TASK_CHARLIE, TASK_BOB);
    if (result == 0) {
        kprintf("✓ Charlie unsubscribed from Bob's events\n");
    }

    kprintf("\n%[S]Demo 4 complete - Pub-sub works!%[D]\n");
}

// ============================================================================
// DEMO: IPC STATISTICS
// ============================================================================

static void demo_statistics(void) {
    kprintf("\n%[H]=== DEMO 5: IPC Statistics ===%[D]\n");
    kprintf("Check how many messages each task sent/received\n\n");

    // Get stats for each task
    IPCStats alice_stats = event_ipc_get_stats(TASK_ALICE);
    IPCStats bob_stats = event_ipc_get_stats(TASK_BOB);

    kprintf("Task Alice (ID=%u):\n", TASK_ALICE);
    kprintf("  Sent: %lu, Received: %lu, Dropped: %lu\n",
            alice_stats.messages_sent,
            alice_stats.messages_received,
            alice_stats.messages_dropped);

    kprintf("Task Bob (ID=%u):\n", TASK_BOB);
    kprintf("  Sent: %lu, Received: %lu, Dropped: %lu\n",
            bob_stats.messages_sent,
            bob_stats.messages_received,
            bob_stats.messages_dropped);

    kprintf("\n%[S]Demo 5 complete - Statistics tracked!%[D]\n");
}

// ============================================================================
// DEMO: NON-BLOCKING NATURE
// ============================================================================

static void demo_non_blocking(void) {
    kprintf("\n%[H]=== DEMO 6: Non-Blocking Communication ===%[D]\n");
    kprintf("Check for messages without blocking (async!)\n\n");

    // Check if Charlie has messages (should be none)
    if (!event_ipc_has_messages(TASK_CHARLIE)) {
        kprintf("✓ Charlie has no pending messages (non-blocking check!)\n");
    }

    // Send message to Charlie
    event_ipc_send(TASK_BOB, TASK_CHARLIE, "Hello!", 7);

    // Now Charlie should have messages
    if (event_ipc_has_messages(TASK_CHARLIE)) {
        kprintf("✓ Charlie now has pending messages\n");

        // Receive without blocking
        IPCMessage* msg = event_ipc_receive(TASK_CHARLIE);
        if (msg) {
            kprintf("✓ Charlie received: \"%s\" (non-blocking!)\n", (char*)msg->data);
        }
    }

    kprintf("\n%[S]Demo 6 complete - Non-blocking I/O works!%[D]\n");
}

// ============================================================================
// MAIN DEMO RUNNER
// ============================================================================

void event_ipc_demo_run(void) {
    kprintf("\n");
    kprintf("╔═══════════════════════════════════════════════════════════════╗\n");
    kprintf("║     EVENT-BASED IPC DEMONSTRATION - NO SYSCALLS!             ║\n");
    kprintf("║     BoxOS Innovative Communication System                    ║\n");
    kprintf("╚═══════════════════════════════════════════════════════════════╝\n");

    kprintf("\n%[H]PHILOSOPHY:%[D]\n");
    kprintf("  Traditional OS: Task → syscall() → Kernel (BLOCKING)\n");
    kprintf("  BoxOS: Task → Event → Deck → Response (ASYNC, NON-BLOCKING!)\n");
    kprintf("\n");

    // Run all demos
    demo_simple_messages();
    demo_request_response();
    demo_broadcast();
    demo_subscribe();
    demo_statistics();
    demo_non_blocking();

    kprintf("\n");
    kprintf("╔═══════════════════════════════════════════════════════════════╗\n");
    kprintf("║                  ALL IPC DEMOS COMPLETED!                     ║\n");
    kprintf("║                                                               ║\n");
    kprintf("║  ✓ Simple messaging                                          ║\n");
    kprintf("║  ✓ Request-response pattern                                  ║\n");
    kprintf("║  ✓ Guild broadcasting                                        ║\n");
    kprintf("║  ✓ Publish-subscribe                                         ║\n");
    kprintf("║  ✓ Statistics tracking                                       ║\n");
    kprintf("║  ✓ Non-blocking I/O                                          ║\n");
    kprintf("║                                                               ║\n");
    kprintf("║  This is INNOVATIVE! Not like Unix/Linux!                    ║\n");
    kprintf("╚═══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");
}
