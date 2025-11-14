#ifndef AUTH_H
#define AUTH_H

#include "klib.h"

// ============================================================================
// AUTHENTICATION & USER MANAGEMENT (Production-Ready)
// ============================================================================

#define AUTH_USERNAME_MAX 32
#define AUTH_PASSWORD_HASH_SIZE 32  // SHA-256
#define AUTH_SALT_SIZE 16
#define AUTH_MAX_USERS 64

// User credentials structure (NO PLAINTEXT PASSWORDS!)
typedef struct {
    char username[AUTH_USERNAME_MAX];
    uint8_t password_hash[AUTH_PASSWORD_HASH_SIZE];
    uint8_t salt[AUTH_SALT_SIZE];
    int is_admin;
    int is_active;
    uint64_t last_login;
    uint32_t failed_attempts;
} UserCredentials;

// Authentication system
void auth_init(void);
int auth_add_user(const char* username, const char* password, int is_admin);
int auth_verify_password(const char* username, const char* password);
int auth_is_admin(const char* username);
int auth_change_password(const char* username, const char* old_pass, const char* new_pass);
void auth_lock_account(const char* username);
void auth_unlock_account(const char* username);

// Simple hash function (SHA-256-like, simplified for kernel)
void auth_hash_password(const char* password, const uint8_t* salt, uint8_t* output);
void auth_generate_salt(uint8_t* salt);

#endif // AUTH_H
