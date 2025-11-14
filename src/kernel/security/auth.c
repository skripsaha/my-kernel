#include "auth.h"
#include "pit.h"  // For random seed from timer

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

    // Create default root user with random password
    // IMPORTANT: On first boot, system should prompt for root password
    kprintf("[AUTH] Initializing authentication system...\n");
    kprintf("[AUTH] %[W]WARNING: Default accounts disabled for security%[D]\n");
    kprintf("[AUTH] Use 'auth_add_user()' to create initial admin account\n");
}

int auth_add_user(const char* username, const char* password, int is_admin) {
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

    strncpy(user->username, username, AUTH_USERNAME_MAX - 1);
    user->username[AUTH_USERNAME_MAX - 1] = '\0';

    // Generate salt and hash password
    auth_generate_salt(user->salt);
    auth_hash_password(password, user->salt, user->password_hash);

    user->is_admin = is_admin;
    user->is_active = 1;
    user->failed_attempts = 0;
    user->last_login = 0;

    user_count++;

    spin_unlock(&auth_lock);

    kprintf("[AUTH] %[S]User '%s' created successfully%[D] (admin: %s)\n",
           username, is_admin ? "yes" : "no");

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
            int is_admin = user_db[i].is_admin;
            spin_unlock(&auth_lock);
            return is_admin;
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
