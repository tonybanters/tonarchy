#define _POSIX_C_SOURCE 200809L
#include "chroot_helper.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define CHROOT_PATH "/mnt"
#define MAX_CMD_SIZE 4096

int write_file(const char *path, const char *content) {
    LOG_INFO("Writing file: %s", path);
    FILE *fp = fopen(path, "w");
    if (!fp) {
        LOG_ERROR("Failed to open file for writing: %s", path);
        return 0;
    }

    if (fprintf(fp, "%s", content) < 0) {
        LOG_ERROR("Failed to write to file: %s", path);
        fclose(fp);
        return 0;
    }

    fclose(fp);
    LOG_DEBUG("Successfully wrote file: %s", path);
    return 1;
}

int write_file_fmt(const char *path, const char *fmt, ...) {
    char content[MAX_CMD_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(content, sizeof(content), fmt, args);
    va_end(args);

    return write_file(path, content);
}

int set_file_perms(const char *path, mode_t mode, const char *owner, const char *group) {
    LOG_INFO("Setting permissions for %s: mode=%o owner=%s group=%s", path, mode, owner, group);

    if (chmod(path, mode) != 0) {
        LOG_ERROR("Failed to chmod %s", path);
        return 0;
    }

    char chown_cmd[512];
    snprintf(chown_cmd, sizeof(chown_cmd), "chown %s:%s %s", owner, group, path);

    if (system(chown_cmd) != 0) {
        LOG_ERROR("Failed to chown %s", path);
        return 0;
    }

    return 1;
}

int create_directory(const char *path, mode_t mode) {
    LOG_INFO("Creating directory: %s", path);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);

    if (system(cmd) != 0) {
        LOG_ERROR("Failed to create directory: %s", path);
        return 0;
    }

    if (chmod(path, mode) != 0) {
        LOG_WARN("Failed to set permissions on directory: %s", path);
    }

    return 1;
}

int chroot_exec(const char *cmd) {
    char full_cmd[MAX_CMD_SIZE];
    snprintf(full_cmd, sizeof(full_cmd), "arch-chroot %s /bin/bash -c '%s' >> /tmp/tonarchy-install.log 2>&1",
             CHROOT_PATH, cmd);

    LOG_INFO("Executing in chroot: %s", cmd);
    LOG_DEBUG("Full command: %s", full_cmd);

    int result = system(full_cmd);
    if (result != 0) {
        LOG_ERROR("Chroot command failed (exit %d): %s", result, cmd);
        return 0;
    }

    LOG_DEBUG("Chroot command succeeded: %s", cmd);
    return 1;
}

int chroot_exec_fmt(const char *fmt, ...) {
    char cmd[MAX_CMD_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    return chroot_exec(cmd);
}

int chroot_exec_as_user(const char *username, const char *cmd) {
    char full_cmd[MAX_CMD_SIZE * 2];
    snprintf(full_cmd, sizeof(full_cmd), "sudo -u %s %s", username, cmd);
    return chroot_exec(full_cmd);
}

int chroot_exec_as_user_fmt(const char *username, const char *fmt, ...) {
    char cmd[MAX_CMD_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    return chroot_exec_as_user(username, cmd);
}

int git_clone_as_user(const char *username, const char *repo_url, const char *dest_path) {
    LOG_INFO("Cloning %s to %s as user %s", repo_url, dest_path, username);
    return chroot_exec_as_user_fmt(username, "git clone %s %s", repo_url, dest_path);
}

int make_clean_install(const char *build_dir) {
    LOG_INFO("Building and installing from %s", build_dir);
    return chroot_exec_fmt("cd %s && make clean install", build_dir);
}

int create_user_dotfile(const char *username, const DotFile *dotfile) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/home/%s/%s", CHROOT_PATH, username, dotfile->filename);

    LOG_INFO("Creating dotfile %s for user %s", dotfile->filename, username);

    if (!write_file(full_path, dotfile->content)) {
        return 0;
    }

    if (!set_file_perms(full_path, dotfile->permissions, username, username)) {
        return 0;
    }

    return 1;
}

int setup_systemd_override(const SystemdOverride *override) {
    char dir_path[1024];
    char file_path[2048];

    snprintf(dir_path, sizeof(dir_path), "%s/etc/systemd/system/%s",
             CHROOT_PATH, override->drop_in_dir);
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, override->drop_in_file);

    LOG_INFO("Setting up systemd override: %s/%s", override->drop_in_dir, override->drop_in_file);

    if (!create_directory(dir_path, 0755)) {
        return 0;
    }

    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        LOG_ERROR("Failed to create systemd override file: %s", file_path);
        return 0;
    }

    for (size_t i = 0; i < override->entry_count; i++) {
        fprintf(fp, "%s=%s\n", override->entries[i].key, override->entries[i].value);
    }

    fclose(fp);
    LOG_INFO("Successfully created systemd override");
    return 1;
}
