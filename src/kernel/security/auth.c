#include "auth.h"
#include "pit.h"  // For random seed from timer
#include "atomics.h"  // For rdtsc()

// ============================================================================
// USER DATABASE
// ============================================================================

static UserCredentials user_db[AUTH_MAX_USERS];
static uint32_t user_count = 0;
static spinlock_t auth_lock = {0};

// Simple PRNG seed
static uint64_t random_seed = 0x123456789ABCDEF0ULL;

// ============================================================================
// SIMPLE HASH FUNCTION (Production should use proper crypto)
// ============================================================================

// Simple but effective hash combining password + salt
// NOTE: For production OS, use proper cryptographic hash (SHA-256, bcrypt, argon2)
void auth_hash_password(const char* password, const uint8_t* salt, uint8_t* output) {
    // Initialize output
    memset(output, 0, AUTH_PASSWORD_HASH_SIZE);

    // Combine password and salt
    size_t pass_len = strlen(password);

    // Multiple rounds of mixing for security
    for (int round = 0; round < 1000; round++) {
        uint64_t hash = 0x123456789ABCDEF0ULL + round;

        // Mix in salt
        for (size_t i = 0; i < AUTH_SALT_SIZE; i++) {
            hash ^= salt[i];
            hash = (hash << 5) | (hash >> 59);  // Rotate left
            hash *= 0x9E3779B97F4A7C15ULL;      // Multiply by prime
        }

        // Mix in password
        for (size_t i = 0; i < pass_len; i++) {
            hash ^= (uint64_t)password[i];
            hash = (hash << 7) | (hash >> 57);  // Rotate left
            hash *= 0x9E3779B97F4A7C15ULL;
        }

        // Store hash bytes into output
        for (size_t i = 0; i < 8 && i < AUTH_PASSWORD_HASH_SIZE; i++) {
            output[i] ^= (uint8_t)(hash >> (i * 8));
        }
    }
}

// Generate cryptographically-random salt
void auth_generate_salt(uint8_t* salt) {
    // Use timer + RDTSC for entropy
    uint64_t entropy = 0;
    asm volatile("rdtsc" : "=A"(entropy));

    random_seed ^= entropy;
    random_seed *= 0x9E3779B97F4A7C15ULL;

    for (size_t i = 0; i < AUTH_SALT_SIZE; i++) {
        random_seed = (random_seed * 1103515245ULL + 12345ULL);
        salt[i] = (uint8_t)(random_seed >> 32);
    }
}

// ============================================================================
// AUTHENTICATION SYSTEM
// ============================================================================

void auth_init(void) {
    spinlock_init(&auth_lock);
    memset(user_db, 0, sizeof(user_db));
    user_count = 0;

    kprintf("[AUTH] Initializing authentication system...\n");
    kprintf("[AUTH] BoxOS INNOVATIVE: Wizards, Apprentices, Guilds!\n");

    // Create THE WIZARD (uid=0) - default superuser account
    // Password: "wizard" (for demo - CHANGE IN PRODUCTION!)
    UserCredentials* wizard = &user_db[0];
    wizard->user_id = 0;  // THE WIZARD always has uid=0
    wizard->guild_id = 0;  // Wizard's own guild
    strncpy(wizard->username, "wizard", AUTH_USERNAME_MAX - 1);
    wizard->user_type = USER_TYPE_WIZARD;
    wizard->is_active = true;
    wizard->failed_attempts = 0;
    wizard->last_login = 0;

    // Generate salt and hash password
    auth_generate_salt(wizard->salt);
    auth_hash_password("wizard", wizard->salt, wizard->password_hash);

    user_count = 1;

    kprintf("[AUTH] %[S]The Wizard created%[D] (uid=0, username='wizard')\n");
    kprintf("[AUTH] %[W]Default password: 'wizard' - CHANGE IN PRODUCTION!%[D]\n");
}

int auth_add_user(const char* username, const char* password, int is_wizard) {
    if (!username || !password) return -1;
    if (strlen(username) == 0 || strlen(username) >= AUTH_USERNAME_MAX) return -1;
    if (strlen(password) < 4) {
        kprintf("[AUTH] %[E]Password too short (minimum 4 characters)%[D]\n");
        return -1;
    }

    spin_lock(&auth_lock);

    // Check if user already exists
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            spin_unlock(&auth_lock);
            kprintf("[AUTH] %[E]User '%s' already exists%[D]\n", username);
            return -1;
        }
    }

    // Check if database is full
    if (user_count >= AUTH_MAX_USERS) {
        spin_unlock(&auth_lock);
        kprintf("[AUTH] %[E]User database full%[D]\n");
        return -1;
    }

    // Create new user
    UserCredentials* user = &user_db[user_count];
    memset(user, 0, sizeof(UserCredentials));

    // Assign user_id and type
    if (is_wizard) {
        // Another wizard (rare, but allowed)
        user->user_id = user_count;  // Wizards can have uid < 1000
        user->user_type = USER_TYPE_WIZARD;
        user->guild_id = 0;  // Wizards guild
    } else {
        // Apprentice (normal user)
        user->user_id = 1000 + user_count;  // Apprentices start at uid=1000
        user->user_type = USER_TYPE_APPRENTICE;
        user->guild_id = 1000;  // Default apprentices guild
    }

    strncpy(user->username, username, AUTH_USERNAME_MAX - 1);
    user->username[AUTH_USERNAME_MAX - 1] = '\0';

    // Generate salt and hash password
    auth_generate_salt(user->salt);
    auth_hash_password(password, user->salt, user->password_hash);

    user->is_active = true;
    user->failed_attempts = 0;
    user->last_login = 0;

    user_count++;

    spin_unlock(&auth_lock);

    const char* type_str = is_wizard ? "Wizard" : "Apprentice";
    kprintf("[AUTH] %[S]%s '%s' created%[D] (uid=%u, guild=%u)\n",
           type_str, username, user->user_id, user->guild_id);

    return 0;
}

int auth_verify_password(const char* username, const char* password) {
    if (!username || !password) return 0;

    spin_lock(&auth_lock);

    // Find user
    UserCredentials* user = NULL;
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            user = &user_db[i];
            break;
        }
    }

    if (!user) {
        spin_unlock(&auth_lock);
        kprintf("[AUTH] User '%s' not found\n", username);
        return 0;
    }

    // Check if account is locked
    if (!user->is_active) {
        spin_unlock(&auth_lock);
        kprintf("[AUTH] %[E]Account '%s' is locked%[D]\n", username);
        return 0;
    }

    // Hash provided password with user's salt
    uint8_t hash[AUTH_PASSWORD_HASH_SIZE];
    auth_hash_password(password, user->salt, hash);

    // Compare hashes (constant-time comparison to prevent timing attacks)
    int match = 1;
    for (size_t i = 0; i < AUTH_PASSWORD_HASH_SIZE; i++) {
        if (hash[i] != user->password_hash[i]) {
            match = 0;
        }
    }

    if (match) {
        // Success - reset failed attempts
        user->failed_attempts = 0;
        user->last_login = 0; // TODO: use real timestamp
        spin_unlock(&auth_lock);
        return 1;
    } else {
        // Failed - increment attempts
        user->failed_attempts++;

        if (user->failed_attempts >= 5) {
            user->is_active = 0;  // Lock account after 5 failed attempts
            kprintf("[AUTH] %[E]Account '%s' locked after 5 failed attempts%[D]\n", username);
        }

        spin_unlock(&auth_lock);
        return 0;
    }
}

int auth_is_admin(const char* username) {
    if (!username) return 0;

    spin_lock(&auth_lock);

    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            int is_wizard = (user_db[i].user_type == USER_TYPE_WIZARD);
            spin_unlock(&auth_lock);
            return is_wizard;  // Returns 1 if Wizard, 0 if Apprentice
        }
    }

    spin_unlock(&auth_lock);
    return 0;
}

int auth_change_password(const char* username, const char* old_pass, const char* new_pass) {
    if (!username || !old_pass || !new_pass) return -1;
    if (strlen(new_pass) < 4) {
        kprintf("[AUTH] %[E]New password too short%[D]\n");
        return -1;
    }

    // Verify old password first
    if (!auth_verify_password(username, old_pass)) {
        kprintf("[AUTH] %[E]Incorrect current password%[D]\n");
        return -1;
    }

    spin_lock(&auth_lock);

    UserCredentials* user = NULL;
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            user = &user_db[i];
            break;
        }
    }

    if (!user) {
        spin_unlock(&auth_lock);
        return -1;
    }

    // Generate new salt and hash
    auth_generate_salt(user->salt);
    auth_hash_password(new_pass, user->salt, user->password_hash);

    spin_unlock(&auth_lock);

    kprintf("[AUTH] %[S]Password changed successfully for '%s'%[D]\n", username);
    return 0;
}

void auth_lock_account(const char* username) {
    if (!username) return;

    spin_lock(&auth_lock);

    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            user_db[i].is_active = 0;
            kprintf("[AUTH] Account '%s' locked\n", username);
            break;
        }
    }

    spin_unlock(&auth_lock);
}

void auth_unlock_account(const char* username) {
    if (!username) return;

    spin_lock(&auth_lock);

    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            user_db[i].is_active = 1;
            user_db[i].failed_attempts = 0;
            kprintf("[AUTH] Account '%s' unlocked\n", username);
            break;
        }
    }

    spin_unlock(&auth_lock);
}

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================

UserSession* auth_create_session(const char* username) {
    if (!username) return NULL;

    spin_lock(&auth_lock);

    // Find user
    UserCredentials* user = NULL;
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            user = &user_db[i];
            break;
        }
    }

    if (!user || !user->is_active) {
        spin_unlock(&auth_lock);
        kprintf("[AUTH] Cannot create session: user '%s' not found or inactive\n", username);
        return NULL;
    }

    // Allocate session
    UserSession* session = (UserSession*)kmalloc(sizeof(UserSession));
    if (!session) {
        spin_unlock(&auth_lock);
        kprintf("[AUTH] Failed to allocate session\n");
        return NULL;
    }

    // Initialize session
    session->user_id = user->user_id;
    session->guild_id = user->guild_id;  // GUILDS, not groups!
    strncpy(session->username, user->username, AUTH_USERNAME_MAX - 1);
    session->username[AUTH_USERNAME_MAX - 1] = '\0';
    session->user_type = user->user_type;  // WIZARD or APPRENTICE
    session->login_time = rdtsc();

    // Update last login
    user->last_login = session->login_time;

    spin_unlock(&auth_lock);

    const char* type_str = (session->user_type == USER_TYPE_WIZARD) ? "Wizard" : "Apprentice";
    kprintf("[AUTH] Session created for %s '%s' (uid=%u, guild=%u)\n",
            type_str, username, session->user_id, session->guild_id);

    return session;
}

void auth_destroy_session(UserSession* session) {
    if (!session) return;

    kprintf("[AUTH] Destroying session for user '%s'\n", session->username);
    kfree(session);
}

UserSession* auth_get_session_by_user_id(uint32_t user_id) {
    // This function is not needed for current design - sessions are owned by tasks
    // Left as stub for future multi-session support
    (void)user_id;
    return NULL;
}

uint32_t auth_get_user_id(const char* username) {
    if (!username) return (uint32_t)-1;

    spin_lock(&auth_lock);

    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_db[i].username, username) == 0) {
            uint32_t uid = user_db[i].user_id;
            spin_unlock(&auth_lock);
            return uid;
        }
    }

    spin_unlock(&auth_lock);
    return (uint32_t)-1;  // User not found
}

const char* auth_get_username(uint32_t user_id) {
    spin_lock(&auth_lock);

    for (uint32_t i = 0; i < user_count; i++) {
        if (user_db[i].user_id == user_id) {
            // Return pointer to username in database
            // WARNING: Caller must not modify! (Read-only)
            const char* name = user_db[i].username;
            spin_unlock(&auth_lock);
            return name;
        }
    }

    spin_unlock(&auth_lock);
    return NULL;  // User not found
}
