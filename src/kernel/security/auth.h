#ifndef AUTH_H
#define AUTH_H

#include "klib.h"

// ============================================================================
// AUTHENTICATION & USER MANAGEMENT - INNOVATIVE BOXOS APPROACH!
// ============================================================================
// PHILOSOPHY:
// - NO Unix-style users! We have WIZARDS and APPRENTICES
// - NO root! We have THE WIZARD (uid=0)
// - Users belong to GUILDS (not groups)
// - Tag-based permissions (not rwx)
// ============================================================================

#define AUTH_USERNAME_MAX 32
#define AUTH_PASSWORD_HASH_SIZE 32  // SHA-256
#define AUTH_SALT_SIZE 16
#define AUTH_MAX_USERS 64

// User types
#define USER_TYPE_WIZARD    0  // The all-powerful wizard (uid=0)
#define USER_TYPE_APPRENTICE 1  // Regular users (uid=1000+)

// User credentials structure (NO PLAINTEXT PASSWORDS!)
typedef struct {
    uint32_t user_id;                        // Unique user ID (0 = wizard, 1000+ = apprentices)
    uint32_t guild_id;                       // Guild (не group!)
    char username[AUTH_USERNAME_MAX];
    uint8_t password_hash[AUTH_PASSWORD_HASH_SIZE];
    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t user_type;                       // USER_TYPE_WIZARD or USER_TYPE_APPRENTICE
    bool is_active;
    uint64_t last_login;
    uint32_t failed_attempts;
} UserCredentials;

// User session - связывает task с пользователем
typedef struct {
    uint32_t user_id;                        // ID пользователя (0 = wizard)
    uint32_t guild_id;                       // Guild ID (гильдия, не группа!)
    char username[AUTH_USERNAME_MAX];        // Имя пользователя
    uint8_t user_type;                       // WIZARD or APPRENTICE
    uint64_t login_time;                     // Время входа
} UserSession;

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

// Session management (NEW!)
UserSession* auth_create_session(const char* username);
void auth_destroy_session(UserSession* session);
UserSession* auth_get_session_by_user_id(uint32_t user_id);
uint32_t auth_get_user_id(const char* username);
const char* auth_get_username(uint32_t user_id);

#endif // AUTH_H
