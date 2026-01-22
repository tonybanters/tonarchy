#ifndef BUILD_ISO_H
#define BUILD_ISO_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>

#define PATH_MAX_LEN 1024
#define CMD_MAX_LEN 4096

typedef enum {
    CONTAINER_NONE,
    CONTAINER_PODMAN,
    CONTAINER_DISTROBOX
} Container_Type;

typedef struct {
    char tonarchy_src[PATH_MAX_LEN];
    char iso_profile[PATH_MAX_LEN];
    char out_dir[PATH_MAX_LEN];
    char work_dir[PATH_MAX_LEN];
    char distrobox_name[128];
    Container_Type container_type;
    bool use_container;
} Build_Config;

void logger_init(const char *log_path);
void logger_close(void);

void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_warn(const char *fmt, ...);

int run_command(const char *cmd);
int run_command_in_container(const char *cmd, const Build_Config *config);
int create_directory(const char *path, mode_t mode);

int build_tonarchy_static(const Build_Config *config);
int clean_airootfs(const Build_Config *config);
int clean_work_dir(const Build_Config *config);
int prepare_airootfs(const Build_Config *config);
int run_mkarchiso(const Build_Config *config);
int run_mkarchiso_in_container(const Build_Config *config);

const char *find_latest_iso(const char *out_dir);

int detect_container_runtime(void);
int check_distrobox_exists(const char *name);

#endif
