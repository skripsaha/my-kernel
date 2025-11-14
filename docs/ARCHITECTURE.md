# BoxOS Architecture - INNOVATIVE EVENT-DRIVEN OS

## 🧙 Philosophy: NO SYSCALLS, ONLY EVENTS!

BoxOS is **fundamentally different** from Unix/Linux/Windows.

### Traditional OS (Unix/Linux):
```
User Program → syscall(open, read, write) → Kernel
              ↓
         SYNCHRONOUS, BLOCKING
```

### BoxOS (INNOVATIVE):
```
User Task → Event → Deck → Process → Response
           ↓
      ASYNCHRONOUS, NON-BLOCKING
```

---

## 🎯 Core Concepts

### 1. **The Wizard** (not root)
- uid=0
- All-powerful user
- Full access to all capabilities

### 2. **Apprentices** (not users)
- uid=1000+
- Regular users learning the system
- Limited capabilities based on file access scope

### 3. **Guilds** (not groups)
- Communities of users working together
- Shared file access via `TAGFS_ACCESS_GUILD`

### 4. **Capabilities** (not permissions)
Files don't have "permissions", they have **CAPABILITIES**:
- `TAGFS_CAP_READ` - Can read
- `TAGFS_CAP_WRITE` - Can write
- `TAGFS_CAP_EXECUTE` - Can execute
- `TAGFS_CAP_SHARE` - Can share
- `TAGFS_CAP_DELETE` - Can delete
- `TAGFS_CAP_METADATA` - Can modify tags

### 5. **Access Scope** (not owner/group/other)
- `TAGFS_ACCESS_PRIVATE` - Only owner
- `TAGFS_ACCESS_GUILD` - Owner + guild members
- `TAGFS_ACCESS_PUBLIC` - Everyone

---

## ⚡ Event-Driven Architecture

### NO SYSCALLS!

Traditional OS:
```c
// Blocking syscall
int fd = open("/file.txt", O_RDONLY);  // BLOCKS!
read(fd, buffer, size);                // BLOCKS!
close(fd);                              // BLOCKS!
```

BoxOS:
```c
// Non-blocking event
Event* event = event_create(EVENT_FILE_OPEN, deck_id);
event_set_data(event, "file.txt");
event_send(event);  // Returns immediately!

// Later, deck processes event and sends response
```

### Decks (Kernel Services)

All kernel functionality is exposed via **Decks**:
- **Storage Deck** (`DECK_PREFIX_STORAGE`) - File operations
- **Network Deck** - Network I/O
- **Graphics Deck** - Display operations
- **Audio Deck** - Sound operations
- **Timer Deck** - Time-based events

### Event Flow

```
1. User Task creates Event
   ↓
2. Event sent to Deck (via eventdriven_center)
   ↓
3. Deck processes Event asynchronously
   ↓
4. Deck sends Response Event back to Task
   ↓
5. Task receives Response (non-blocking)
```

---

## 🗂️ TagFS - Tag-Based Filesystem

### NO DIRECTORIES!

Traditional FS:
```
/home/alice/documents/work/report.txt
```

BoxFS:
```
File: report.txt
Tags:
  - owner:alice
  - type:document
  - project:work
  - date:2025-11-14
  - guild:developers
  - access:private
```

### Query by Tags

```c
// Find all images from project "boxos"
Tag query_tags[] = {
    {"type", "image"},
    {"project", "boxos"}
};
tagfs_query(query_tags, 2, results);
```

### Context-Based View

```c
// Set context: only show work documents
Tag context[] = {
    {"type", "document"},
    {"project", "work"}
};
tagfs_context_set(context, 2);

// Now all operations only see work documents!
tagfs_context_list_files(results);
```

---

## 🔐 Security Model

### Capability-Based Access

```c
// Check if user can read file
if (tagfs_can_read(inode_id, user_id, guild_id)) {
    // The Wizard always passes
    // Owner always passes if file has CAP_READ
    // Guild members pass if access_scope = GUILD
    // Everyone passes if access_scope = PUBLIC
}
```

### Rate Limiting (Protection from abuse)

```c
#define USER_EVENT_BURST_LIMIT 20      // Max 20 events in burst
#define USER_EVENT_RATE_LIMIT_MS 10    // Min 10ms between events

// Prevents user tasks from flooding kernel
if (!usermode_can_send_event(task)) {
    // Throttled! Too many events
    return ERROR_RATE_LIMITED;
}
```

---

## 💾 Memory Management

### Energy-Based Scheduling

Tasks don't have "priority", they request **ENERGY**:

```c
Task* task = task_spawn("worker", entry_point, 50);  // 50 energy
// System allocates based on:
// - Energy requested
// - Task efficiency (learned over time)
// - System load
```

### User Mode Isolation

- Ring 3 tasks have separate page tables
- Memory limits enforced (16MB default per task)
- CPU quota tracked
- I/O quota tracked

---

## 🚀 Task System

### Lightweight Tasks (not heavy processes)

```c
Task* task = task_spawn("my_task", entry_point, 50);

// Task has:
task->energy_requested   // Energy asked for (0-100)
task->energy_allocated   // Energy actually given
task->health             // Responsiveness, efficiency, stability
task->user_session       // UserSession* (if user-owned)
task->message_queue      // For inter-task messages
```

### Task States

- `TASK_STATE_RUNNING` - Actively executing
- `TASK_STATE_PROCESSING` - Processing event
- `TASK_STATE_WAITING_EVENT` - Waiting for deck response
- `TASK_STATE_SLEEPING` - Sleeping
- `TASK_STATE_THROTTLED` - Rate-limited

---

## 🎨 Innovation Summary

| Concept | Unix/Linux | BoxOS |
|---------|-----------|-------|
| Superuser | root | The Wizard |
| Users | users | Apprentices |
| Groups | groups | Guilds |
| Permissions | rwxrwxrwx | Capabilities |
| Access Control | owner/group/other | private/guild/public |
| Filesystem | Directories | Tags |
| Kernel API | syscalls | Events to Decks |
| Processes | Heavy processes | Lightweight tasks |
| Priority | nice values | Energy requests |

---

## 📚 Code Structure

```
src/kernel/
├── arch/x86-64/
│   ├── usermode/          # Ring 3 support
│   │   ├── usermode.h     # User mode API
│   │   ├── usermode.c     # Implementation
│   │   └── usermode.asm   # Ring 0 ↔ Ring 3 switching
│   ├── gdt/               # Global Descriptor Table
│   ├── idt/               # Interrupt Descriptor Table
│   └── tss/               # Task State Segment
├── eventdriven/
│   ├── core/              # Event system core
│   │   ├── eventdriven_center.c  # Central event router
│   │   └── eventdriven_receiver.c
│   ├── decks/             # Kernel service decks
│   │   └── storage_deck.c # File I/O operations
│   ├── storage/           # TagFS filesystem
│   │   ├── tagfs.h        # TagFS API
│   │   └── tagfs.c        # Implementation
│   └── task/              # Task management
│       ├── task.h         # Task structures
│       └── task.c         # Task operations
├── security/
│   ├── auth.h             # Authentication
│   └── auth.c             # User management
└── main_box/
    └── main.c             # Kernel entry point
```

---

## 🔮 Future Enhancements

1. **Context Switching** - True preemptive multitasking
2. **Network Stack** - Event-based networking
3. **Graphics** - GPU deck for rendering
4. **JIT Compiler** - Dynamic code generation
5. **Distributed Events** - Multi-machine event routing

---

**BoxOS: The Future of Operating Systems** 🧙‍♂️⚡

*"Not a Unix clone. Not a Windows clone. Something NEW."*
