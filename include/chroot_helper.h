#ifndef CHROOT_HELPER_H
#define CHROOT_HELPER_H

#include "types.h"

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

int create_user_dotfile(const char *username, const DotFile *dotfile);
int setup_systemd_override(const SystemdOverride *override);

#define CHECK_OR_FAIL(expr, user_msg) \
    do { \
        if (!(expr)) { \
            LOG_ERROR("%s", #expr); \
            show_message(user_msg); \
            return 0; \
        } \
    } while(0)

#endif
