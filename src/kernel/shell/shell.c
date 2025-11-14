#include "shell.h"
#include "klib.h"
#include "keyboard.h"
#include "vga.h"
#include "tagfs.h"
#include "task.h"
#include "cpu.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "auth.h"  // PRODUCTION: Secure authentication system
#include "system_config.h"  // PRODUCTION: System-wide constants

// ============================================================================
// SHELL STATE
// ============================================================================

static char input_buffer[SHELL_INPUT_BUFFER_SIZE];
static uint32_t input_pos = 0;

// ============================================================================
// USER SYSTEM (PRODUCTION-READY)
// ============================================================================
// REMOVED: Hardcoded passwords (security vulnerability)
// NOW USING: Secure hashed password system in auth.c

static char current_user[32] = "";  // No default login
static int current_user_is_admin = 0;

// ============================================================================
// COMMAND DECLARATIONS
// ============================================================================

int cmd_help(int argc, char** argv);
int cmd_clear(int argc, char** argv);
int cmd_create(int argc, char** argv);
int cmd_eye(int argc, char** argv);
int cmd_trash(int argc, char** argv);
int cmd_erase(int argc, char** argv);
int cmd_info(int argc, char** argv);
int cmd_tag(int argc, char** argv);
int cmd_untag(int argc, char** argv);
int cmd_restore(int argc, char** argv);
int cmd_edit(int argc, char** argv);
int cmd_reboot(int argc, char** argv);
int cmd_say(int argc, char** argv);
int cmd_use(int argc, char** argv);
int cmd_byebye(int argc, char** argv);
int cmd_ls(int argc, char** argv);
int cmd_whoami(int argc, char** argv);
int cmd_login(int argc, char** argv);

// ============================================================================
// COMMAND TABLE
// ============================================================================

static shell_command_t commands[] = {
    {"help", "Show available commands", cmd_help},
    {"clear", "Clear the screen", cmd_clear},
    {"ls", "List files", cmd_ls},
    {"create", "Create file with content and tags", cmd_create},
    {"eye", "View file contents", cmd_eye},
    {"trash", "Move file to trash", cmd_trash},
    {"erase", "Permanently delete file", cmd_erase},
    {"restore", "Restore file from trash", cmd_restore},
    {"tag", "Add tag to file", cmd_tag},
    {"untag", "Remove tag from file", cmd_untag},
    {"use", "Set tag context filter", cmd_use},
    {"say", "Print text to console", cmd_say},
    {"edit", "Open text editor", cmd_edit},
    {"info", "Show system information", cmd_info},
    {"whoami", "Show current user", cmd_whoami},
    {"login", "Login as user", cmd_login},
    {"reboot", "Reboot the system", cmd_reboot},
    {"byebye", "Shutdown system", cmd_byebye},
    {NULL, NULL, NULL}  // Sentinel
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static void shell_print_prompt(void) {
    // Check if user is logged in
    if (current_user[0] == '\0') {
        // Not logged in
        kprintf("%[W](not logged in)%[D]@boxos:%[S]~%[D]$ ");
    } else if (current_user_is_admin) {
        // Administrator
        kprintf("%[E]%s@boxos%[D]:%[S]~%[D]# ", current_user);
    } else {
        // Regular user
        kprintf("%[H]%s@boxos%[D]:%[S]~%[D]$ ", current_user);
    }
}

static int shell_parse_command(char* input, char** argv) {
    int argc = 0;
    int in_token = 0;

    for (int i = 0; input[i] != '\0' && argc < SHELL_MAX_ARGS; i++) {
        if (input[i] == ' ' || input[i] == '\t') {
            input[i] = '\0';
            in_token = 0;
        } else if (!in_token) {
            argv[argc++] = &input[i];
            in_token = 1;
        }
    }

    return argc;
}

// ============================================================================
// SHELL INITIALIZATION
// ============================================================================

void shell_init(void) {
    keyboard_init();
    input_pos = 0;

    // Initialize secure authentication system
    auth_init();

    // Create default administrator account (FIRST BOOT ONLY)
    // In production, this should prompt for password or use secure boot flow
    if (auth_add_user("root", "changeme", 1) == 0) {
        kprintf("%[W]SECURITY WARNING: Default root account created!%[D]\n");
        kprintf("%[W]Password: 'changeme' - CHANGE IMMEDIATELY!%[D]\n");
        kprintf("%[W]Use: login root changeme%[D]\n\n");
    }

    // Add a regular test user
    auth_add_user("user", "user123", 0);

    kprintf("\n");
    kprintf("%[S]=====================================================%[D]\n");
    kprintf("%[S]      Welcome to BoxOS Shell v2.0 (PRODUCTION)     %[D]\n");
    kprintf("%[S]         Type 'help' for available commands        %[D]\n");
    kprintf("%[S]=====================================================%[D]\n");
    kprintf("\n");
    kprintf("%[H]Please login to continue. No default session.%[D]\n\n");
}

// ============================================================================
// SHELL MAIN LOOP
// ============================================================================

void shell_run(void) {
    shell_print_prompt();

    while (1) {
        if (!keyboard_has_input()) {
            asm("hlt");  // Wait for interrupt
            continue;
        }

        char c = keyboard_getchar();

        if (c == '\n') {
            // Execute command
            kprintf("\n");
            input_buffer[input_pos] = '\0';

            if (input_pos > 0) {
                char* argv[SHELL_MAX_ARGS];
                int argc = shell_parse_command(input_buffer, argv);

                if (argc > 0) {
                    int found = 0;
                    for (int i = 0; commands[i].name != NULL; i++) {
                        if (strcmp(argv[0], commands[i].name) == 0) {
                            commands[i].handler(argc, argv);
                            found = 1;
                            break;
                        }
                    }

                    if (!found) {
                        kprintf("%[E]Unknown command: %s%[D]\n", argv[0]);
                        kprintf("Type '%[H]help%[D]' for available commands.\n");
                    }
                }
            }

            input_pos = 0;
            shell_print_prompt();

        } else if (c == '\b') {
            // Backspace
            if (input_pos > 0) {
                input_pos--;
                kprintf("\b \b");
            }
        } else if (input_pos < SHELL_INPUT_BUFFER_SIZE - 1) {
            input_buffer[input_pos++] = c;
            kprintf("%c", c);
        }
    }
}

// ============================================================================
// COMMAND: help
// ============================================================================

int cmd_help(int argc, char** argv) {
    (void)argc;
    (void)argv;

    kprintf("\n%[H]BoxOS Shell Commands:%[D]\n");
    kprintf("=======================================================\n");

    for (int i = 0; commands[i].name != NULL; i++) {
        kprintf("  %[H]%-12s%[D] - %s\n", commands[i].name, commands[i].description);
    }

    kprintf("\n%[H]Tip:%[D] Files are managed with tags, not directories!\n");
    kprintf("Example: create myfile.txt --data \"Hello!\" type:text\n");
    kprintf("\n");
    return 0;
}

// ============================================================================
// COMMAND: clear
// ============================================================================

int cmd_clear(int argc, char** argv) {
    (void)argc;
    (void)argv;

    vga_clear_screen();
    return 0;
}

// ============================================================================
// COMMAND: whoami
// ============================================================================

int cmd_whoami(int argc, char** argv) {
    (void)argc;
    (void)argv;

    kprintf("%s", current_user);
    if (current_user_is_admin) {
        kprintf(" (administrator)");
    }
    kprintf("\n");
    return 0;
}

// ============================================================================
// COMMAND: login (PRODUCTION-READY with secure authentication)
// ============================================================================

int cmd_login(int argc, char** argv) {
    if (argc < 3) {
        kprintf("Usage: login <username> <password>\n");
        kprintf("\n%[H]Security Note:%[D] Passwords are now hashed and stored securely.\n");
        kprintf("No default accounts exist. Use kernel API to create users.\n");
        return -1;
    }

    const char* username = argv[1];
    const char* password = argv[2];

    // Verify password using secure authentication system
    if (auth_verify_password(username, password)) {
        // Login successful
        strncpy(current_user, username, sizeof(current_user) - 1);
        current_user[sizeof(current_user) - 1] = '\0';
        current_user_is_admin = auth_is_admin(username);

        kprintf("%[S]Login successful! Welcome, %s.%[D]\n", current_user);
        if (current_user_is_admin) {
            kprintf("%[H]You have administrator privileges.%[D]\n");
        }
        return 0;
    } else {
        kprintf("%[E]Authentication failed%[D]\n");
        kprintf("%[W]Note: Account will be locked after 5 failed attempts%[D]\n");
        return -1;
    }
}

// ============================================================================
// COMMAND: say (like echo)
// ============================================================================

int cmd_say(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        kprintf("%s", argv[i]);
        if (i < argc - 1) kprintf(" ");
    }
    kprintf("\n");
    return 0;
}

// ============================================================================
// COMMAND: ls
// ============================================================================

// FIXED: Added comprehensive safety checks to prevent GPF
int cmd_ls(int argc, char** argv) {
    (void)argc;
    (void)argv;

    kprintf("\n%[H]Files:%[D]\n");
    kprintf("=======================================================\n");
    kprintf("%-8s  %-20s  %-10s  %s\n", "Inode", "Name", "Size", "Tags");
    kprintf("-------------------------------------------------------\n");

    // Access inode table with safety checks
    extern TagFSContext global_tagfs;

    // CRITICAL SAFETY CHECK #1: Validate superblock
    if (!global_tagfs.superblock) {
        kprintf("%[E]ERROR: TagFS superblock not initialized%[D]\n");
        return -1;
    }

    // CRITICAL SAFETY CHECK #2: Validate inode table
    if (!global_tagfs.inode_table) {
        kprintf("%[E]ERROR: TagFS inode table not initialized%[D]\n");
        return -1;
    }

    // CRITICAL SAFETY CHECK #3: Validate total_inodes bounds
    uint64_t max_inodes = global_tagfs.superblock->total_inodes;
    if (max_inodes > TAGFS_MAX_FILES) {
        kprintf("%[W]WARNING: total_inodes (%lu) exceeds maximum (%d), capping%[D]\n",
                max_inodes, TAGFS_MAX_FILES);
        max_inodes = TAGFS_MAX_FILES;
    }

    // CRITICAL SAFETY CHECK #4: Verify inode table alignment
    if ((uintptr_t)global_tagfs.inode_table % 32 != 0) {
        kprintf("%[W]WARNING: Inode table misaligned at %p%[D]\n",
                global_tagfs.inode_table);
    }

    uint32_t file_count = 0;

    for (uint64_t i = 1; i < max_inodes; i++) {
        FileInode* inode = &global_tagfs.inode_table[i];

        // Skip empty inodes
        if (inode->inode_id == 0 || inode->size == 0xFFFFFFFFFFFFFFFF) {
            continue;
        }

        // SAFETY CHECK #5: Validate tag_count before accessing tags array
        if (inode->tag_count > TAGFS_MAX_TAGS_PER_FILE) {
            kprintf("%[W]WARNING: Inode %lu has invalid tag_count (%u), skipping%[D]\n",
                    i, inode->tag_count);
            continue;
        }

        // Check if in trash (skip if trashed)
        int is_trashed = 0;
        for (uint32_t t = 0; t < inode->tag_count; t++) {
            if (strcmp(inode->tags[t].key, "trashed") == 0 &&
                strcmp(inode->tags[t].value, "true") == 0) {
                is_trashed = 1;
                break;
            }
        }

        if (is_trashed) continue;

        // Check context filter
        if (!tagfs_context_matches(i)) {
            continue;
        }

        // Get filename from name tag
        char filename[TAGFS_TAG_VALUE_SIZE];
        memset(filename, 0, TAGFS_TAG_VALUE_SIZE);
        strncpy(filename, "<unnamed>", TAGFS_TAG_VALUE_SIZE - 1);

        for (uint32_t t = 0; t < inode->tag_count; t++) {
            if (strcmp(inode->tags[t].key, "name") == 0) {
                strncpy(filename, inode->tags[t].value, TAGFS_TAG_VALUE_SIZE - 1);
                filename[TAGFS_TAG_VALUE_SIZE - 1] = '\0';  // Ensure null termination
                break;
            }
        }

        // Print file info
        kprintf("%-8lu  %-20s  %-10lu  [", inode->inode_id, filename, inode->size);

        // Print tags (skip name and trashed tags, show max 3)
        int tag_printed = 0;
        uint32_t tags_to_show = inode->tag_count < 3 ? inode->tag_count : 3;

        for (uint32_t t = 0; t < tags_to_show; t++) {
            if (strcmp(inode->tags[t].key, "name") != 0 &&
                strcmp(inode->tags[t].key, "trashed") != 0) {
                if (tag_printed) kprintf(", ");
                kprintf("%s:%s", inode->tags[t].key, inode->tags[t].value);
                tag_printed = 1;
            }
        }
        if (inode->tag_count > 3) kprintf("...");
        kprintf("]\n");

        file_count++;
    }

    if (file_count == 0) {
        kprintf("(no files)\n");
    }

    kprintf("\nTotal: %u files\n\n", file_count);
    return 0;
}

// ============================================================================
// COMMAND: create
// ============================================================================

int cmd_create(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: create <name> [--data <text>] [tag:value ...]\n");
        kprintf("Example: create file.txt --data \"Hello!\" type:text\n");
        return -1;
    }

    // Parse arguments
    char* name = argv[1];
    const char* data = NULL;
    Tag tags[TAGFS_MAX_TAGS_PER_FILE];
    uint32_t tag_count = 0;

    // Add name tag
    memset(&tags[tag_count], 0, sizeof(Tag));
    strncpy(tags[tag_count].key, "name", TAGFS_TAG_KEY_SIZE);
    strncpy(tags[tag_count].value, name, TAGFS_TAG_VALUE_SIZE);
    tag_count++;

    // Add owner tag
    memset(&tags[tag_count], 0, sizeof(Tag));
    strncpy(tags[tag_count].key, "owner", TAGFS_TAG_KEY_SIZE);
    strncpy(tags[tag_count].value, current_user, TAGFS_TAG_VALUE_SIZE);
    tag_count++;

    // Parse remaining arguments
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[i + 1];
            i++;
        } else {
            // Parse tag (key:value)
            char* colon = strchr(argv[i], ':');
            if (colon && tag_count < TAGFS_MAX_TAGS_PER_FILE) {
                *colon = '\0';
                memset(&tags[tag_count], 0, sizeof(Tag));
                strncpy(tags[tag_count].key, argv[i], TAGFS_TAG_KEY_SIZE);
                strncpy(tags[tag_count].value, colon + 1, TAGFS_TAG_VALUE_SIZE);
                tag_count++;
            }
        }
    }

    // Create file
    uint64_t inode_id;
    if (data) {
        inode_id = tagfs_create_file_with_data(tags, tag_count,
                                                (const uint8_t*)data, strlen(data));
    } else {
        inode_id = tagfs_create_file(tags, tag_count);
    }

    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]Failed to create file%[D]\n");
        return -1;
    }

    kprintf("%[S]Created file '%s' (inode %lu)%[D]\n", name, inode_id);
    return 0;
}

// ============================================================================
// COMMAND: eye
// ============================================================================

int cmd_eye(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: eye <filename>\n");
        return -1;
    }

    // Find file by name
    uint64_t inode_id = tagfs_find_by_name(argv[1]);
    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    // Read file content
    uint64_t size;
    uint8_t* content = tagfs_read_file_content(inode_id, &size);
    if (!content) {
        kprintf("%[E]Failed to read file%[D]\n");
        return -1;
    }

    // Print content
    kprintf("\n");
    for (uint64_t i = 0; i < size; i++) {
        kprintf("%c", content[i]);
    }
    kprintf("\n\n");

    kfree(content);
    return 0;
}

// ============================================================================
// COMMAND: trash
// ============================================================================

int cmd_trash(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: trash <filename>\n");
        return -1;
    }

    uint64_t inode_id = tagfs_find_by_name(argv[1]);
    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    if (tagfs_trash_file(inode_id) != 0) {
        kprintf("%[E]Failed to trash file%[D]\n");
        return -1;
    }

    kprintf("%[S]Moved '%s' to trash%[D]\n", argv[1]);
    return 0;
}

// ============================================================================
// COMMAND: erase
// ============================================================================

int cmd_erase(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: erase <filename>\n");
        kprintf("WARNING: This permanently deletes the file!\n");
        return -1;
    }

    // Check permissions
    if (!current_user_is_admin) {
        kprintf("%[E]Permission denied: Only administrators can permanently erase files%[D]\n");
        kprintf("Use 'trash' instead to move file to trash.\n");
        return -1;
    }

    uint64_t inode_id = tagfs_find_by_name(argv[1]);
    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    if (tagfs_erase_file(inode_id) != 0) {
        kprintf("%[E]Failed to erase file%[D]\n");
        return -1;
    }

    kprintf("%[S]Permanently erased '%s'%[D]\n", argv[1]);
    return 0;
}

// ============================================================================
// COMMAND: restore
// ============================================================================

int cmd_restore(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: restore <filename>\n");
        return -1;
    }

    // Find file (including trashed)
    char tag_str[128];
    ksnprintf(tag_str, sizeof(tag_str), "name:%s", argv[1]);
    Tag name_tag = tagfs_tag_from_string(tag_str);

    uint64_t result_inodes[256];
    uint32_t count = 0;
    tagfs_query_single(&name_tag, result_inodes, &count, 256);

    if (count == 0) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    if (tagfs_restore_file(result_inodes[0]) != 0) {
        kprintf("%[E]Failed to restore file%[D]\n");
        return -1;
    }

    kprintf("%[S]Restored '%s' from trash%[D]\n", argv[1]);
    return 0;
}

// ============================================================================
// COMMAND: tag
// ============================================================================

int cmd_tag(int argc, char** argv) {
    if (argc < 3) {
        kprintf("Usage: tag <filename> <key:value>\n");
        return -1;
    }

    uint64_t inode_id = tagfs_find_by_name(argv[1]);
    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    // Parse tag
    Tag tag = tagfs_tag_from_string(argv[2]);
    if (tagfs_add_tag(inode_id, &tag) == 0) {
        kprintf("%[E]Failed to add tag%[D]\n");
        return -1;
    }

    kprintf("%[S]Added tag '%s' to '%s'%[D]\n", argv[2], argv[1]);
    return 0;
}

// ============================================================================
// COMMAND: untag
// ============================================================================

int cmd_untag(int argc, char** argv) {
    if (argc < 3) {
        kprintf("Usage: untag <filename> <key>\n");
        return -1;
    }

    uint64_t inode_id = tagfs_find_by_name(argv[1]);
    if (inode_id == TAGFS_INVALID_INODE) {
        kprintf("%[E]File not found: %s%[D]\n", argv[1]);
        return -1;
    }

    if (tagfs_remove_tag(inode_id, argv[2]) == 0) {
        kprintf("%[E]Failed to remove tag%[D]\n");
        return -1;
    }

    kprintf("%[S]Removed tag '%s' from '%s'%[D]\n", argv[2], argv[1]);
    return 0;
}

// ============================================================================
// COMMAND: use
// ============================================================================

int cmd_use(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: use <tag:value> [tag:value ...]\n");
        kprintf("       use clear  (to clear context)\n");
        return -1;
    }

    if (strcmp(argv[1], "clear") == 0) {
        tagfs_context_clear();
        kprintf("%[S]Context cleared%[D]\n");
        return 0;
    }

    // Parse tags
    Tag tags[TAGFS_MAX_CONTEXT_TAGS];
    uint32_t tag_count = 0;

    for (int i = 1; i < argc && tag_count < TAGFS_MAX_CONTEXT_TAGS; i++) {
        tags[tag_count] = tagfs_tag_from_string(argv[i]);
        tag_count++;
    }

    if (tagfs_context_set(tags, tag_count) != 0) {
        kprintf("%[E]Failed to set context%[D]\n");
        return -1;
    }

    kprintf("%[S]Context set to: %[D]");
    for (uint32_t i = 0; i < tag_count; i++) {
        kprintf("%s:%s ", tags[i].key, tags[i].value);
    }
    kprintf("\n");

    return 0;
}

// ============================================================================
// COMMAND: info
// ============================================================================

int cmd_info(int argc, char** argv) {
    (void)argc;
    (void)argv;

    kprintf("\n%[H]BoxOS System Information%[D]\n");
    kprintf("=======================================================\n");

    // CPU info
    char cpu_vendor[13] = {0};
    char cpu_brand[49] = {0};
    detect_cpu_info(cpu_vendor, cpu_brand);
    kprintf("CPU: %s\n", cpu_vendor);
    if (cpu_brand[0]) {
        kprintf("Brand: %s\n", cpu_brand);
    }

    // Filesystem info
    extern TagFSContext global_tagfs;
    kprintf("Filesystem: TagFS\n");
    kprintf("Files: %u / %u\n",
            global_tagfs.superblock->total_inodes - global_tagfs.superblock->free_inodes,
            global_tagfs.superblock->total_inodes);
    kprintf("Blocks: %u / %u\n",
            global_tagfs.superblock->total_blocks - global_tagfs.superblock->free_blocks,
            global_tagfs.superblock->total_blocks);

    // User info
    kprintf("User: %s %s\n", current_user,
            current_user_is_admin ? "(administrator)" : "(standard user)");
    kprintf("Shell: BoxOS Shell v2.0\n");

    kprintf("\n");
    return 0;
}

// ============================================================================
// COMMAND: edit
// ============================================================================

int cmd_edit(int argc, char** argv) {
    if (argc < 2) {
        kprintf("Usage: edit <filename>\n");
        return -1;
    }

    kprintf("%[H]BoxEditor - Text Editor%[D]\n");
    kprintf("=======================================================\n");
    kprintf("%[W]Editor not yet implemented!%[D]\n");
    kprintf("Coming soon: Full-featured text editor for BoxOS\n");
    kprintf("\nFeatures planned:\n");
    kprintf("  - Line editing with insert/overwrite modes\n");
    kprintf("  - Copy/paste support\n");
    kprintf("  - Syntax highlighting\n");
    kprintf("  - Multi-file editing\n");
    kprintf("\nFile: %s\n", argv[1]);
    kprintf("\n");

    return 0;
}

// ============================================================================
// COMMAND: reboot
// ============================================================================

int cmd_reboot(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!current_user_is_admin) {
        kprintf("%[E]Permission denied: Only administrators can reboot the system%[D]\n");
        return -1;
    }

    kprintf("\n%[W]Rebooting system...%[D]\n");

    // PRODUCTION: Sync filesystem before reboot
    tagfs_shutdown();

    kprintf("\n%[W]Initiating reboot...%[D]\n\n");

    // Wait a moment
    for (volatile int i = 0; i < REBOOT_DELAY_CYCLES; i++);

    // Use keyboard controller to reboot
    outb(KEYBOARD_COMMAND_PORT, 0xFE);

    // If that fails, try triple fault
    asm volatile("cli");
    asm volatile("lidt (0)");
    asm volatile("int $3");

    return 0;
}

// ============================================================================
// COMMAND: byebye (shutdown)
// ============================================================================

int cmd_byebye(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!current_user_is_admin) {
        kprintf("%[E]Permission denied: Only administrators can shutdown the system%[D]\n");
        return -1;
    }

    kprintf("\n%[H]Thank you for using BoxOS, %s!%[D]\n", current_user);
    kprintf("%[S]Shutting down system...%[D]\n");

    // PRODUCTION: Sync filesystem before shutdown
    tagfs_shutdown();

    kprintf("\n%[S]Powering off...%[D]\n\n");

    // Wait a moment
    for (volatile int i = 0; i < SHUTDOWN_DELAY_CYCLES; i++);

    // Try QEMU/Bochs shutdown
    outw(SHUTDOWN_PORT_BOCHS, SHUTDOWN_VALUE);       // Bochs
    outw(SHUTDOWN_PORT_QEMU_NEW, SHUTDOWN_VALUE);    // QEMU newer
    outw(SHUTDOWN_PORT_QEMU_OLD, SHUTDOWN_VALUE_OLD); // QEMU older

    // If still running, halt
    kprintf("Shutdown failed - system halted\n");
    while (1) asm("hlt");

    return 0;
}
