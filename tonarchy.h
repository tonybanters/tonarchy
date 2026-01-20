#ifndef TONARCHY_H
#define TONARCHY_H

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#define CHROOT_PATH "/mnt"
#define MAX_CMD_SIZE 4096

#define ANSI_ESC           "\033["
#define ANSI_RESET         ANSI_ESC "0m"
#define ANSI_BOLD          ANSI_ESC "1m"
#define ANSI_WHITE         ANSI_ESC "37m"
#define ANSI_GREEN         ANSI_ESC "32m"
#define ANSI_YELLOW        ANSI_ESC "33m"
#define ANSI_GRAY          ANSI_ESC "90m"
#define ANSI_BLUE          ANSI_ESC "34m"
#define ANSI_BLUE_BOLD     ANSI_ESC "1;34m"
#define ANSI_CURSOR_POS    ANSI_ESC "%d;%dH"

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} Log_Level;

typedef struct {
    const char *repo_url;
    const char *name;
    const char *build_dir;
} Git_Repo;

typedef struct {
    const char *filename;
    const char *content;
    mode_t permissions;
} Dotfile;

typedef struct {
    const char *key;
    const char *value;
} Config_Entry;

typedef struct {
    const char *service_name;
    const char *drop_in_dir;
    const char *drop_in_file;
    Config_Entry *entries;
    size_t entry_count;
} Systemd_Override;

typedef struct {
    const char *label;
    const char *value;
    const char *default_display;
    int is_password;
} Tui_Field;

typedef enum {
    INPUT_TEXT,
    INPUT_PASSWORD,
    INPUT_FZF_KEYMAP,
    INPUT_FZF_TIMEZONE
} Input_Type;

typedef struct {
    char *dest;
    const char *default_val;
    Input_Type type;
    int cursor_offset;
    const char *error_msg;
} Form_Field;

void logger_init(const char *log_path);
void logger_close(void);
void log_msg(Log_Level level, const char *fmt, ...);

#define LOG_DEBUG(...) log_msg(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_msg(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_LEVEL_ERROR, __VA_ARGS__)

int write_file(const char *path, const char *content);
int write_file_fmt(const char *path, const char *fmt, ...);
int set_file_perms(const char *path, mode_t mode, const char *owner, const char *group);
int create_directory(const char *path, mode_t mode);
int chroot_exec(const char *cmd);
int chroot_exec_fmt(const char *fmt, ...);
int chroot_exec_as_user(const char *username, const char *cmd);
int chroot_exec_as_user_fmt(const char *username, const char *fmt, ...);
int git_clone_as_user(const char *username, const char *repo_url, const char *dest_path);
int make_clean_install(const char *build_dir);
int create_user_dotfile(const char *username, const Dotfile *dotfile);
int setup_systemd_override(const Systemd_Override *override);

void show_message(const char *message);

#define CHECK_OR_FAIL(expr, user_msg) \
    do { \
        if (!(expr)) { \
            LOG_ERROR("%s", #expr); \
            show_message(user_msg); \
            return 0; \
        } \
    } while(0)

#endif
