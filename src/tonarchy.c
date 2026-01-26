#include "tonarchy.h"
#include <string.h>
#include <ctype.h>

static FILE *log_file = NULL;
static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR"};
static struct termios orig_termios;

static void part_path(char *out, size_t size, const char *disk, int part) {
    if (isdigit(disk[strlen(disk) - 1])) {
        snprintf(out, size, "/dev/%sp%d", disk, part);
    } else {
        snprintf(out, size, "/dev/%s%d", disk, part);
    }
}

enum Install_Option {
    BEGINNER = 0,
    OXIDIZED = 1
};

static const char *XFCE_PACKAGES = "base base-devel linux linux-firmware linux-headers networkmanager git vim neovim curl wget htop btop man-db man-pages openssh sudo xorg-server xorg-xinit xorg-xrandr xorg-xset xfce4 xfce4-goodies xfce4-session xfce4-whiskermenu-plugin thunar thunar-archive-plugin file-roller firefox alacritty vlc evince eog fastfetch rofi ripgrep fd ttf-iosevka-nerd ttf-jetbrains-mono-nerd pavucontrol";

static const char *OXWM_PACKAGES = "base base-devel linux linux-firmware linux-headers networkmanager git vim neovim curl wget htop btop man-db man-pages openssh sudo xorg-server xorg-xinit xorg-xsetroot xorg-xrandr xorg-xset libx11 libxft freetype2 fontconfig pkg-config lua firefox alacritty vlc evince eog cargo ttf-iosevka-nerd ttf-jetbrains-mono-nerd picom xclip xwallpaper maim rofi pulseaudio pulseaudio-alsa pavucontrol alsa-utils fastfetch ripgrep fd pcmanfm lxappearance papirus-icon-theme gnome-themes-extra";

static int is_uefi_system(void) {
    struct stat st;
    return stat("/sys/firmware/efi", &st) == 0;
}

void logger_init(const char *log_path) {
    log_file = fopen(log_path, "a");
    if (log_file) {
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        timestamp[strlen(timestamp) - 1] = '\0';
        fprintf(log_file, "\n=== Tonarchy Installation Log - %s ===\n", timestamp);
        fflush(log_file);
    }
}

void logger_close(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_msg(Log_Level level, const char *fmt, ...) {
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(
        log_file,
        "[%02d:%02d:%02d] [%s] ",
        t->tm_hour,
        t->tm_min,
        t->tm_sec,
        level_strings[level]
    );

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}

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
    if (strncmp(path, CHROOT_PATH, strlen(CHROOT_PATH)) == 0) {
        const char *chroot_path = path + strlen(CHROOT_PATH);
        snprintf(chown_cmd, sizeof(chown_cmd),
            "arch-chroot %s chown %s:%s %s 2>> /tmp/tonarchy-install.log",
            CHROOT_PATH, owner, group, chroot_path);
    } else {
        snprintf(chown_cmd, sizeof(chown_cmd), "chown %s:%s %s", owner, group, path);
    }

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
    snprintf(full_cmd, sizeof(full_cmd),
        "arch-chroot %s /bin/bash -c '%s' >> /tmp/tonarchy-install.log 2>&1",
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

int create_user_dotfile(const char *username, const Dotfile *dotfile) {
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

int setup_systemd_override(const Systemd_Override *override) {
    char dir_path[1024];
    char file_path[2048];

    snprintf(dir_path, sizeof(dir_path), "%s/etc/systemd/system/%s", CHROOT_PATH, override->drop_in_dir);
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
        if (override->entries[i].value[0] == '\0') {
            fprintf(fp, "%s\n", override->entries[i].key);
        } else {
            fprintf(fp, "%s=%s\n", override->entries[i].key, override->entries[i].value);
        }
    }

    fclose(fp);
    LOG_INFO("Successfully created systemd override");
    return 1;
}


static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void draw_logo(int cols) {
    const char *logo[] = {
        "████████╗ ██████╗ ███╗   ██╗ █████╗ ██████╗  ██████╗██╗  ██╗██╗   ██╗",
        "╚══██╔══╝██╔═══██╗████╗  ██║██╔══██╗██╔══██╗██╔════╝██║  ██║╚██╗ ██╔╝",
        "   ██║   ██║   ██║██╔██╗ ██║███████║██████╔╝██║     ███████║ ╚████╔╝ ",
        "   ██║   ██║   ██║██║╚██╗██║██╔══██║██╔══██╗██║     ██╔══██║  ╚██╔╝  ",
        "   ██║   ╚██████╔╝██║ ╚████║██║  ██║██║  ██║╚██████╗██║  ██║   ██║   ",
        "   ╚═╝    ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝   ╚═╝   "
    };

    int logo_height = 6;
    int logo_start = (cols - 70) / 2;

    printf("\033[1;32m");
    for (int i = 0; i < logo_height; i++) {
        printf("\033[%d;%dH%s", i + 2, logo_start, logo[i]);
    }
    printf("\033[0m");
}

static int draw_menu(const char **items, int count, int selected) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    int menu_start_row = 10;

    for (int i = 0; i < count; i++) {
        printf("\033[%d;%dH", menu_start_row + i, logo_start + 2);
        if (i == selected) {
            printf("\033[1;34m> %s\033[0m", items[i]);
        } else {
            printf("\033[37m  %s\033[0m", items[i]);
        }
    }

    printf("\033[%d;%dH", menu_start_row + count + 2, logo_start);
    printf("\033[33mj/k Navigate  Enter Select\033[0m");

    fflush(stdout);
    return 0;
}

static int select_from_menu(const char **items, int count) {
    int selected = 0;

    enable_raw_mode();
    draw_menu(items, count, selected);

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q' || c == 27) {
            disable_raw_mode();
            return -1;
        }

        if (c == 'j' || c == 66) {
            if (selected < count - 1) {
                selected++;
                draw_menu(items, count, selected);
            }
        }

        if (c == 'k' || c == 65) {
            if (selected > 0) {
                selected--;
                draw_menu(items, count, selected);
            }
        }

        if (c == '\r' || c == '\n') {
            disable_raw_mode();
            return selected;
        }
    }

    disable_raw_mode();
    return -1;
}

void show_message(const char *message) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH", 10, logo_start);
    printf("\033[37m%s\033[0m", message);
    fflush(stdout);

    sleep(2);
}

static int check_internet_connection(void) {
    int result = system("ping -c 1 -W 2 1.1.1.1 > /dev/null 2>&1");
    return result == 0;
}

static int list_wifi_networks(char networks[][256], char ssids[][128], int max_networks) {
    FILE *fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY device wifi list | head -20", "r");
    if (!fp) {
        return 0;
    }

    int count = 0;
    char line[512];
    while (count < max_networks && fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = '\0';

        char ssid[128], signal[32], security[64];
        char *token = strtok(line, ":");
        if (token) strncpy(ssid, token, sizeof(ssid) - 1);
        token = strtok(NULL, ":");
        if (token) strncpy(signal, token, sizeof(signal) - 1);
        token = strtok(NULL, ":");
        if (token) strncpy(security, token, sizeof(security) - 1);

        if (strlen(ssid) > 0 && strcmp(ssid, "--") != 0) {
            snprintf(ssids[count], 128, "%s", ssid);
            snprintf(networks[count], 255, "%s (%s%%) [%s]", ssid, signal,
                     strlen(security) > 0 ? security : "Open");
            count++;
        }
    }
    pclose(fp);
    return count;
}

static int connect_to_wifi(const char *ssid) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mConnecting to: %s\033[0m", 10, logo_start, ssid);
    printf("\033[%d;%dH\033[37mEnter password (leave empty if open): \033[0m", 12, logo_start);
    fflush(stdout);

    char password[256] = "";
    struct termios old_term;
    tcgetattr(STDIN_FILENO, &old_term);
    struct termios new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    new_term.c_lflag |= ICANON;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term);

    if (fgets(password, sizeof(password), stdin) != NULL) {
        password[strcspn(password, "\n")] = '\0';
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);

    clear_screen();
    draw_logo(cols);
    printf("\033[%d;%dH\033[37mConnecting...\033[0m", 10, logo_start);
    fflush(stdout);

    char cmd[512];
    if (strlen(password) > 0) {
        snprintf(cmd, sizeof(cmd), "nmcli device wifi connect '%s' password '%s' > /dev/null 2>&1", ssid, password);
    } else {
        snprintf(cmd, sizeof(cmd), "nmcli device wifi connect '%s' > /dev/null 2>&1", ssid);
    }

    int result = system(cmd);
    sleep(2);

    if (result == 0 && check_internet_connection()) {
        show_message("Connected successfully!");
        return 1;
    } else {
        show_message("Connection failed. Please try again.");
        return 0;
    }
}

static int setup_wifi_if_needed(void) {
    if (check_internet_connection()) {
        return 1;
    }

    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mNo internet connection detected.\033[0m", 10, logo_start);
    printf("\033[%d;%dH\033[37mScanning for WiFi networks...\033[0m", 11, logo_start);
    fflush(stdout);

    system("nmcli radio wifi on > /dev/null 2>&1");
    sleep(1);

    char networks[32][256];
    char ssids[32][128];
    int network_count = list_wifi_networks(networks, ssids, 32);

    if (network_count == 0) {
        show_message("No WiFi networks found. Please check your connection.");
        return 0;
    }

    const char *network_ptrs[32];
    for (int i = 0; i < network_count; i++) {
        network_ptrs[i] = networks[i];
    }

    int selected = select_from_menu(network_ptrs, network_count);
    if (selected < 0) {
        show_message("WiFi setup cancelled. Installation requires internet.");
        return 0;
    }

    return connect_to_wifi(ssids[selected]);
}

static void draw_form(
        const char *username,
        const char *password,
        const char *confirmed_password,
        const char *hostname,
        const char *keyboard,
        const char *timezone,
        int current_field
    ) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    int form_row = 10;

    printf(ANSI_CURSOR_POS ANSI_WHITE "Setup your system:" ANSI_RESET, form_row, logo_start);
    form_row += 2;

    Tui_Field fields[] = {
        {"Username",         username,           NULL,       0},
        {"Password",         password,           NULL,       1},
        {"Confirm Password", confirmed_password, NULL,       1},
        {"Hostname",         hostname,           "tonarchy", 0},
        {"Keyboard",         keyboard,           "us",       0},
        {"Timezone",         timezone,           NULL,       0},
    };
    int num_fields = (int)(sizeof(fields) / sizeof(fields[0]));

    for (int i = 0; i < num_fields; i++) {
        printf(ANSI_CURSOR_POS, form_row + i, logo_start);

        if (current_field == i) {
            printf(ANSI_BLUE_BOLD ">" ANSI_RESET " ");
        } else {
            printf("  ");
        }

        printf(ANSI_WHITE "%s: " ANSI_RESET, fields[i].label);

        if (strlen(fields[i].value) > 0) {
            printf(ANSI_GREEN "%s" ANSI_RESET, fields[i].is_password ? "********" : fields[i].value);
        } else if (current_field != i) {
            if (fields[i].default_display) {
                printf(ANSI_GRAY "%s" ANSI_RESET, fields[i].default_display);
            } else {
                printf(ANSI_GRAY "[not set]" ANSI_RESET);
            }
        }
    }

    fflush(stdout);
}

static int validate_alphanumeric(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (!isalnum(s[i]) && s[i] != '-' && s[i] != '_')
            return 0;
    }
    return 1;
}

static int read_line(char *buf, int size, int echo) {
    struct termios old_term;
    tcgetattr(STDIN_FILENO, &old_term);
    struct termios new_term = old_term;
    if (echo)
        new_term.c_lflag |= ECHO;
    else
        new_term.c_lflag &= ~ECHO;
    new_term.c_lflag |= ICANON;
    new_term.c_lflag &= ~ISIG;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term);

    int result = (fgets(buf, size, stdin) != NULL);
    if (result)
        buf[strcspn(buf, "\n")] = '\0';

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
    return result;
}

static int fzf_select(char *dest, const char *cmd, const char *default_val) {
    clear_screen();
    FILE *fp = popen(cmd, "r");
    if (fp == NULL)
        return 0;

    char buf[256];
    if (fgets(buf, sizeof(buf), fp) != NULL) {
        buf[strcspn(buf, "\n")] = '\0';
        if (strlen(buf) > 0)
            strcpy(dest, buf);
    }
    pclose(fp);

    if (strlen(dest) == 0 && default_val)
        strcpy(dest, default_val);

    return 1;
}

static int handle_password_entry(
        char *password,
        char *confirmed_password,
        int form_row,
        int logo_start,
        char *username,
        char *hostname,
        char *keyboard,
        char *timezone
    ) {
    char temp_input[256];
    char password_confirm[256];

    printf(ANSI_CURSOR_POS, form_row + 1, logo_start + 13);
    fflush(stdout);

    if (!read_line(temp_input, sizeof(temp_input), 0))
        return -1;

    if (strlen(temp_input) == 0) {
        show_message("Password cannot be empty");
        return 0;
    }

    strcpy(password, temp_input);

    draw_form(username, password, confirmed_password, hostname, keyboard, timezone, 2);
    printf(ANSI_CURSOR_POS, form_row + 2, logo_start + 20);
    fflush(stdout);

    if (!read_line(password_confirm, sizeof(password_confirm), 0))
        return -1;

    if (strcmp(password, password_confirm) == 0) {
        strcpy(confirmed_password, password_confirm);
        return 1;
    } else {
        show_message("Passwords do not match");
        return 0;
    }
}

static int get_form_input(
        char *username,
        char *password,
        char *confirmed_password,
        char *hostname,
        char *keyboard,
        char *timezone
    ) {
    char temp_input[256];
    int rows, cols;
    get_terminal_size(&rows, &cols);
    int logo_start = (cols - 70) / 2;
    int form_row = 12;

    Form_Field fields[] = {
        {username, NULL,       INPUT_TEXT,         13, "Username must be alphanumeric"},
        {password, NULL,       INPUT_PASSWORD,     13, NULL},
        {confirmed_password, NULL, INPUT_PASSWORD, 20, NULL},
        {hostname, "tonarchy", INPUT_TEXT,         13, "Hostname must be alphanumeric"},
        {keyboard, "us",       INPUT_FZF_KEYMAP,   0,  NULL},
        {timezone, NULL,       INPUT_FZF_TIMEZONE, 0,  "Timezone is required"},
    };
    int num_fields = (int)(sizeof(fields) / sizeof(fields[0]));

    int current_field = 0;
    while (current_field < num_fields) {
        draw_form(username, password, confirmed_password, hostname, keyboard, timezone, current_field);
        Form_Field *f = &fields[current_field];

        if (f->type == INPUT_FZF_KEYMAP) {
            fzf_select(keyboard, "localectl list-keymaps | fzf --height=40% --reverse --prompt='Keyboard: ' --header='Start typing to filter, Enter to select' --query='us'", "us");
            current_field++;
        } else if (f->type == INPUT_FZF_TIMEZONE) {
            fzf_select(timezone, "timedatectl list-timezones | fzf --height=40% --reverse --prompt='Timezone: ' --header='Type your city/timezone, Enter to select'", NULL);
            if (strlen(timezone) == 0) {
                show_message("Timezone is required");
            } else {
                current_field++;
            }
        } else if (current_field == 1) {
            int result = handle_password_entry(
                password,
                confirmed_password,
                form_row,
                logo_start,
                username,
                hostname,
                keyboard,
                timezone
            );
            if (result == -1) return 0;
            if (result == 1) current_field = 3;
        } else if (current_field == 2) {
            current_field = 1;
        } else {
            printf(ANSI_CURSOR_POS, form_row + current_field, logo_start + f->cursor_offset);
            fflush(stdout);

            if (!read_line(temp_input, sizeof(temp_input), 1))
                return 0;

            if (strlen(temp_input) == 0) {
                if (f->default_val) {
                    strcpy(f->dest, f->default_val);
                    current_field++;
                }
            } else if (validate_alphanumeric(temp_input)) {
                strcpy(f->dest, temp_input);
                current_field++;
            } else {
                show_message(f->error_msg);
            }
        }
    }

    while (1) {
        draw_form(username, password, confirmed_password, hostname, keyboard, timezone, 6);

        get_terminal_size(&rows, &cols);
        logo_start = (cols - 70) / 2;

        printf(ANSI_CURSOR_POS ANSI_YELLOW "Press Enter to continue, or field number to edit (0-5)" ANSI_RESET, 20, logo_start);
        fflush(stdout);

        enable_raw_mode();
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == '\r' || c == '\n') {
                disable_raw_mode();
                return 1;
            }
            if (c == 'q' || c == 27) {
                disable_raw_mode();
                return 0;
            }
            if (c >= '0' && c <= '5') {
                disable_raw_mode();
                int edit_field = c - '0';
                Form_Field *f = &fields[edit_field];

                if (f->type == INPUT_FZF_KEYMAP) {
                    fzf_select(keyboard, "localectl list-keymaps | fzf --height=40% --reverse --prompt='Keyboard: ' --header='Start typing to filter, Enter to select' --query='us'", "us");
                } else if (f->type == INPUT_FZF_TIMEZONE) {
                    fzf_select(timezone, "timedatectl list-timezones | fzf --height=40% --reverse --prompt='Timezone: ' --header='Type your city/timezone, Enter to select'", NULL);
                    if (strlen(timezone) == 0)
                        show_message("Timezone is required");
                } else if (edit_field == 1 || edit_field == 2) {
                    draw_form(username, password, confirmed_password, hostname, keyboard, timezone, 1);
                    handle_password_entry(password, confirmed_password,
                                          form_row, logo_start,
                                          username, hostname, keyboard, timezone);
                } else {
                    draw_form(username, password, confirmed_password, hostname, keyboard, timezone, edit_field);
                    printf(ANSI_CURSOR_POS, form_row + edit_field, logo_start + f->cursor_offset);
                    fflush(stdout);

                    if (read_line(temp_input, sizeof(temp_input), 1)) {
                        if (strlen(temp_input) == 0 && f->default_val) {
                            strcpy(f->dest, f->default_val);
                        } else if (strlen(temp_input) > 0) {
                            if (validate_alphanumeric(temp_input))
                                strcpy(f->dest, temp_input);
                            else
                                show_message(f->error_msg);
                        }
                    }
                }
                continue;
            }
        }
        disable_raw_mode();
    }

    return 1;
}

static int select_disk(char *disk_name) {
    clear_screen();

    FILE *fp = popen("lsblk -d -n -o NAME,SIZE,MODEL,TYPE | grep -E '(disk|nvme)' | grep -v -E '(loop|rom|airoot)' | awk '{printf \"%s (%s) %s\\n\", $1, $2, substr($0, index($0,$3))}'", "r");
    if (fp == NULL) {
        show_message("Failed to list disks");
        return 0;
    }

    char disks[32][256];
    char names[32][64];
    int disk_count = 0;

    while (disk_count < 32 && fgets(disks[disk_count], sizeof(disks[0]), fp) != NULL) {
        disks[disk_count][strcspn(disks[disk_count], "\n")] = '\0';
        sscanf(disks[disk_count], "%s", names[disk_count]);
        disk_count++;
    }
    pclose(fp);

    if (disk_count == 0) {
        show_message("No disks found");
        return 0;
    }

    const char *disk_ptrs[32];
    for (int i = 0; i < disk_count; i++) {
        disk_ptrs[i] = disks[i];
    }

    int selected = select_from_menu(disk_ptrs, disk_count);
    if (selected < 0) {
        return 0;
    }

    strcpy(disk_name, names[selected]);

    int rows, cols;
    get_terminal_size(&rows, &cols);
    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mWARNING: All data on \033[31m/dev/%s\033[37m will be destroyed!\033[0m", 10, logo_start, disk_name);
    printf("\033[%d;%dH\033[37mType 'yes' to confirm: \033[0m", 12, logo_start);
    fflush(stdout);

    char confirm[256];
    struct termios old_term;
    tcgetattr(STDIN_FILENO, &old_term);
    struct termios new_term = old_term;
    new_term.c_lflag |= (ECHO | ICANON);
    new_term.c_lflag &= ~ISIG;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term);

    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
        return 0;
    }
    confirm[strcspn(confirm, "\n")] = '\0';
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);

    if (strcmp(confirm, "yes") != 0) {
        show_message("Installation cancelled");
        return 0;
    }

    return 1;
}

static int partition_disk(const char *disk) {
    char cmd[1024];
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    int uefi = is_uefi_system();

    char part1[64], part2[64], part3[64];
    part_path(part1, sizeof(part1), disk, 1);
    part_path(part2, sizeof(part2), disk, 2);
    part_path(part3, sizeof(part3), disk, 3);

    printf("\033[%d;%dH\033[37mPartitioning /dev/%s (%s mode)...\033[0m", 10, logo_start, disk, uefi ? "UEFI" : "BIOS");
    fflush(stdout);

    LOG_INFO("Starting disk partitioning: /dev/%s (mode: %s)", disk, uefi ? "UEFI" : "BIOS");

    snprintf(cmd, sizeof(cmd), "wipefs -af /dev/%s 2>> /tmp/tonarchy-install.log", disk);
    if (system(cmd) != 0) {
        LOG_ERROR("Failed to wipe disk: /dev/%s", disk);
        show_message("Failed to wipe disk");
        return 0;
    }
    LOG_INFO("Wiped disk");

    if (uefi) {
        snprintf(cmd, sizeof(cmd), "sgdisk --zap-all /dev/%s 2>> /tmp/tonarchy-install.log", disk);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to zap disk: /dev/%s", disk);
            show_message("Failed to zap disk");
            return 0;
        }
        LOG_INFO("Zapped disk (GPT)");

        snprintf(cmd, sizeof(cmd),
            "sgdisk --clear "
            "--new=1:0:+1G --typecode=1:ef00 --change-name=1:EFI "
            "--new=2:0:+4G --typecode=2:8200 --change-name=2:swap "
            "--new=3:0:0 --typecode=3:8300 --change-name=3:root "
            "/dev/%s 2>> /tmp/tonarchy-install.log", disk);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to create partitions on /dev/%s", disk);
            show_message("Failed to create partitions");
            return 0;
        }
        LOG_INFO("Created partitions (EFI, swap, root)");
    } else {
        snprintf(cmd, sizeof(cmd),
            "parted -s /dev/%s mklabel msdos "
            "mkpart primary linux-swap 1MiB 4GiB "
            "mkpart primary ext4 4GiB 100%% "
            "set 2 boot on 2>> /tmp/tonarchy-install.log", disk);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to create MBR partitions on /dev/%s", disk);
            show_message("Failed to create partitions");
            return 0;
        }
        LOG_INFO("Created MBR partitions (swap, root)");
    }

    printf("\033[%d;%dH\033[37mFormatting partitions...\033[0m", 11, logo_start);
    fflush(stdout);

    if (uefi) {
        snprintf(cmd, sizeof(cmd), "mkfs.fat -F32 %s 2>> /tmp/tonarchy-install.log", part1);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to format EFI partition: %s", part1);
            show_message("Failed to format EFI partition");
            return 0;
        }
        LOG_INFO("Formatted EFI partition");

        snprintf(cmd, sizeof(cmd), "mkswap %s 2>> /tmp/tonarchy-install.log", part2);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to format swap: %s", part2);
            show_message("Failed to format swap partition");
            return 0;
        }
        LOG_INFO("Formatted swap partition");

        snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F %s 2>> /tmp/tonarchy-install.log", part3);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to format root: %s", part3);
            show_message("Failed to format root partition");
            return 0;
        }
        LOG_INFO("Formatted root partition");
    } else {
        snprintf(cmd, sizeof(cmd), "mkswap %s 2>> /tmp/tonarchy-install.log", part1);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to format swap: %s", part1);
            show_message("Failed to format swap partition");
            return 0;
        }
        LOG_INFO("Formatted swap partition");

        snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F %s 2>> /tmp/tonarchy-install.log", part2);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to format root: %s", part2);
            show_message("Failed to format root partition");
            return 0;
        }
        LOG_INFO("Formatted root partition");
    }

    printf("\033[%d;%dH\033[37mMounting partitions...\033[0m", 12, logo_start);
    fflush(stdout);

    if (uefi) {
        snprintf(cmd, sizeof(cmd), "mount %s /mnt 2>> /tmp/tonarchy-install.log", part3);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to mount root: %s", part3);
            show_message("Failed to mount root partition");
            return 0;
        }
        LOG_INFO("Mounted root partition");

        snprintf(cmd, sizeof(cmd), "mkdir -p /mnt/boot 2>> /tmp/tonarchy-install.log");
        system(cmd);

        snprintf(cmd, sizeof(cmd), "mount %s /mnt/boot 2>> /tmp/tonarchy-install.log", part1);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to mount EFI: %s", part1);
            show_message("Failed to mount EFI partition");
            return 0;
        }
        LOG_INFO("Mounted EFI partition");

        snprintf(cmd, sizeof(cmd), "swapon %s 2>> /tmp/tonarchy-install.log", part2);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to enable swap: %s", part2);
            show_message("Failed to enable swap");
            return 0;
        }
    } else {
        snprintf(cmd, sizeof(cmd), "mount %s /mnt 2>> /tmp/tonarchy-install.log", part2);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to mount root: %s", part2);
            show_message("Failed to mount root partition");
            return 0;
        }
        LOG_INFO("Mounted root partition");

        snprintf(cmd, sizeof(cmd), "swapon %s 2>> /tmp/tonarchy-install.log", part1);
        if (system(cmd) != 0) {
            LOG_ERROR("Failed to enable swap: %s", part1);
            show_message("Failed to enable swap");
            return 0;
        }
    }
    LOG_INFO("Enabled swap");
    LOG_INFO("Disk partitioning completed successfully");

    show_message("Disk prepared successfully!");
    return 1;
}

static int install_packages_impl(const char *package_list) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mInstalling system packages...\033[0m", 10, logo_start);
    printf("\033[%d;%dH\033[37mThis will take several minutes.\033[0m", 11, logo_start);
    fflush(stdout);

    LOG_INFO("Starting package installation");
    LOG_INFO("Packages: %s", package_list);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "pacstrap -K /mnt %s 2>> /tmp/tonarchy-install.log", package_list);

    int result = system(cmd);
    if (result != 0) {
        LOG_ERROR("pacstrap failed with exit code %d", result);
        show_message("Failed to install packages");
        return 0;
    }

    LOG_INFO("Package installation completed successfully");
    show_message("Packages installed successfully!");
    return 1;
}

static int configure_system_impl(
        const char *username,
        const char *password,
        const char *hostname,
        const char *keyboard,
        const char *timezone,
        const char *disk,
        int use_dm
    ) {
    (void)disk;
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mConfiguring system...\033[0m", 10, logo_start);
    printf("\033[%d;%dH\033[90m(Logging to /tmp/tonarchy-install.log)\033[0m", 11, logo_start);
    fflush(stdout);

    LOG_INFO("Starting system configuration");
    LOG_INFO("User: %s, Hostname: %s, Timezone: %s, Keyboard: %s", username, hostname, timezone, keyboard);

    CHECK_OR_FAIL(
        system("genfstab -U /mnt >> /mnt/etc/fstab 2>> /tmp/tonarchy-install.log") == 0,
        "Failed to generate fstab - check /tmp/tonarchy-install.log"
    );

    CHECK_OR_FAIL(
        chroot_exec_fmt("ln -sf /usr/share/zoneinfo/%s /etc/localtime", timezone),
        "Failed to configure timezone"
    );

    if (!chroot_exec("hwclock --systohc")) {
        LOG_WARN("Failed to set hardware clock");
    }

    CHECK_OR_FAIL(
        write_file("/mnt/etc/locale.gen", "en_US.UTF-8 UTF-8\n"),
        "Failed to write locale.gen"
    );

    CHECK_OR_FAIL(
        chroot_exec("locale-gen"),
        "Failed to generate locales"
    );

    CHECK_OR_FAIL(
        write_file("/mnt/etc/locale.conf", "LANG=en_US.UTF-8\n"),
        "Failed to write locale.conf"
    );

    CHECK_OR_FAIL(
        write_file_fmt("/mnt/etc/vconsole.conf", "KEYMAP=%s\n", keyboard),
        "Failed to write vconsole.conf"
    );

    CHECK_OR_FAIL(
        write_file_fmt("/mnt/etc/hostname", "%s\n", hostname),
        "Failed to write hostname"
    );

    char hosts_content[512];
    snprintf(hosts_content, sizeof(hosts_content),
             "127.0.0.1   localhost\n"
             "::1         localhost\n"
             "127.0.1.1   %s.localdomain %s\n",
             hostname, hostname);

    CHECK_OR_FAIL(
        write_file("/mnt/etc/hosts", hosts_content),
        "Failed to write hosts file"
    );

    CHECK_OR_FAIL(
        chroot_exec_fmt("useradd -m -G wheel -s /bin/bash %s", username),
        "Failed to create user"
    );

    FILE *chpasswd_pipe = popen("arch-chroot /mnt chpasswd 2>> /tmp/tonarchy-install.log", "w");
    if (!chpasswd_pipe) {
        show_message("Failed to open chpasswd");
        return 0;
    }
    fprintf(chpasswd_pipe, "%s:%s\n", username, password);
    fprintf(chpasswd_pipe, "root:%s\n", password);
    int chpasswd_status = pclose(chpasswd_pipe);
    CHECK_OR_FAIL(
        chpasswd_status == 0,
        "Failed to set passwords"
    );

    create_directory("/mnt/etc/sudoers.d", 0750);
    CHECK_OR_FAIL(
        write_file("/mnt/etc/sudoers.d/wheel", "%wheel ALL=(ALL:ALL) ALL\n"),
        "Failed to configure sudo"
    );
    chmod("/mnt/etc/sudoers.d/wheel", 0440);

    CHECK_OR_FAIL(
        chroot_exec("systemctl enable NetworkManager"),
        "Failed to enable NetworkManager"
    );

    CHECK_OR_FAIL(
        chroot_exec("systemctl enable dbus"),
        "Failed to enable dbus"
    );

    if (use_dm) {
        CHECK_OR_FAIL(
            chroot_exec("systemctl enable lightdm"),
            "Failed to enable display manager"
        );
    }

    LOG_INFO("System configuration completed successfully");
    show_message("System configured successfully!");
    return 1;
}

static int get_root_uuid(const char *disk, char *uuid_out, size_t uuid_size) {
    char cmd[512];
    char root_part[64];

    part_path(root_part, sizeof(root_part), disk, 3);
    snprintf(cmd, sizeof(cmd), "blkid -s UUID -o value %s", root_part);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOG_ERROR("Failed to get UUID for %s", root_part);
        return 0;
    }

    if (fgets(uuid_out, uuid_size, fp) == NULL) {
        pclose(fp);
        LOG_ERROR("Failed to read UUID for %s", root_part);
        return 0;
    }

    uuid_out[strcspn(uuid_out, "\n")] = '\0';
    pclose(fp);

    if (strlen(uuid_out) == 0) {
        LOG_ERROR("Empty UUID for %s", root_part);
        return 0;
    }

    LOG_INFO("Root partition UUID: %s", uuid_out);
    return 1;
}

static int install_bootloader(const char *disk) {
    char cmd[2048];
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    int uefi = is_uefi_system();

    printf("\033[%d;%dH\033[37mInstalling bootloader (%s)...\033[0m", 10, logo_start, uefi ? "systemd-boot" : "GRUB");
    fflush(stdout);

    if (uefi) {
        LOG_INFO("Installing systemd-boot");

        if (!chroot_exec("bootctl install")) {
            LOG_ERROR("bootctl install failed");
            show_message("Failed to install bootloader");
            return 0;
        }

        char uuid[128];
        if (!get_root_uuid(disk, uuid, sizeof(uuid))) {
            show_message("Failed to get root partition UUID");
            return 0;
        }

        LOG_INFO("Creating loader.conf");
        if (!write_file("/mnt/boot/loader/loader.conf",
            "default arch.conf\n"
            "timeout 3\n"
            "console-mode max\n"
            "editor no\n")) {
            LOG_ERROR("Failed to write loader.conf");
            show_message("Failed to create loader config");
            return 0;
        }

        if (!create_directory("/mnt/boot/loader/entries", 0755)) {
            LOG_ERROR("Failed to create boot entries directory");
            return 0;
        }

        char boot_entry[512];
        snprintf(boot_entry, sizeof(boot_entry),
            "title   Tonarchy\n"
            "linux   /vmlinuz-linux\n"
            "initrd  /initramfs-linux.img\n"
            "options root=UUID=%s rw\n",
            uuid);

        LOG_INFO("Creating boot entry");
        if (!write_file("/mnt/boot/loader/entries/arch.conf", boot_entry)) {
            LOG_ERROR("Failed to write boot entry");
            show_message("Failed to create boot entry");
            return 0;
        }

        struct stat st;
        if (stat("/mnt/boot/loader/entries/arch.conf", &st) != 0) {
            LOG_ERROR("Boot entry file missing after creation");
            show_message("Boot entry verification failed");
            return 0;
        }

        LOG_INFO("Syncing filesystem");
        sync();
        sleep(1);

        if (!chroot_exec("sync")) {
            LOG_WARN("Failed to sync in chroot");
        }

        LOG_INFO("systemd-boot installation completed");
    } else {
        snprintf(cmd, sizeof(cmd),
            "arch-chroot /mnt pacman -S --noconfirm grub 2>> /tmp/tonarchy-install.log");
        if (system(cmd) != 0) {
            show_message("Failed to install GRUB package");
            return 0;
        }

        snprintf(cmd, sizeof(cmd),
            "arch-chroot /mnt grub-install --target=i386-pc /dev/%s 2>> /tmp/tonarchy-install.log",
            disk);
        if (system(cmd) != 0) {
            show_message("Failed to install GRUB");
            return 0;
        }

        snprintf(cmd, sizeof(cmd),
            "arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>> /tmp/tonarchy-install.log");
        if (system(cmd) != 0) {
            show_message("Failed to generate GRUB config");
            return 0;
        }
    }

    show_message("Bootloader installed successfully!");
    return 1;
}

static int setup_common_configs(const char *username) {
    char cmd[4096];

    create_directory("/mnt/usr/share/wallpapers", 0755);
    system("cp /usr/share/wallpapers/wall1.jpg /mnt/usr/share/wallpapers/wall1.jpg");

    create_directory("/mnt/usr/share/tonarchy", 0755);
    system("cp /usr/share/tonarchy/favicon.png /mnt/usr/share/tonarchy/favicon.png");

    create_directory("/mnt/usr/share/themes", 0755);
    system("cp -r /usr/share/tonarchy/Tokyonight-Dark /mnt/usr/share/themes/");

    LOG_INFO("Setting up Firefox profile");
    snprintf(cmd, sizeof(cmd), "/mnt/home/%s/.config/firefox", username);
    create_directory(cmd, 0755);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/firefox/default-release/* /mnt/home/%s/.config/firefox/", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "arch-chroot /mnt chown -R %s:%s /home/%s/.config/firefox", username, username, username);
    system(cmd);

    create_directory("/mnt/usr/lib/firefox/distribution", 0755);
    system("cp /usr/share/tonarchy/firefox-policies/policies.json /mnt/usr/lib/firefox/distribution/");

    create_directory("/mnt/usr/share/applications", 0755);
    write_file("/mnt/usr/share/applications/firefox.desktop",
        "[Desktop Entry]\n"
        "Name=Firefox\n"
        "GenericName=Web Browser\n"
        "Exec=sh -c 'firefox --profile $HOME/.config/firefox'\n"
        "Type=Application\n"
        "Icon=firefox\n"
        "Categories=Network;WebBrowser;\n"
        "MimeType=text/html;text/xml;application/xhtml+xml;application/vnd.mozilla.xul+xml;\n"
    );

    snprintf(cmd, sizeof(cmd), "/mnt/home/%s/.config", username);
    create_directory(cmd, 0755);

    snprintf(cmd, sizeof(cmd), "arch-chroot /mnt chown -R %s:%s /home/%s/.config", username, username, username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/alacritty /mnt/home/%s/.config/alacritty", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/rofi /mnt/home/%s/.config/rofi", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/fastfetch /mnt/home/%s/.config/fastfetch", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/picom /mnt/home/%s/.config/picom", username);
    system(cmd);

    char nvim_path[256];
    snprintf(nvim_path, sizeof(nvim_path), "/home/%s/.config/nvim", username);
    git_clone_as_user(username, "https://github.com/tonybanters/nvim", nvim_path);

    snprintf(cmd, sizeof(cmd), "arch-chroot /mnt chown -R %s:%s /home/%s/.config", username, username, username);
    system(cmd);

    return 1;
}

static int setup_autologin(const char *username) {
    char autologin_exec[512];
    snprintf(autologin_exec, sizeof(autologin_exec),
             "ExecStart=-/sbin/agetty -o \"-p -f -- \\\\u\" --noclear --autologin %s %%I $TERM",
             username);

    Config_Entry autologin_entries[] = {
        {"[Service]", ""},
        {"ExecStart=", ""},
        {autologin_exec, ""}
    };

    Systemd_Override autologin = {
        "getty@tty1.service",
        "getty@tty1.service.d",
        "autologin.conf",
        autologin_entries,
        3
    };

    if (!setup_systemd_override(&autologin)) {
        LOG_ERROR("Failed to setup autologin");
        return 0;
    }

    return 1;
}

static const char *BASHRC_CONTENT =
    "export PATH=\"$HOME/.local/bin:$PATH\"\n"
    "export EDITOR=\"nvim\"\n"
    "\n"
    "alias ls='ls --color=auto'\n"
    "alias la='ls -a'\n"
    "alias ll='ls -la'\n"
    "alias ..='cd ..'\n"
    "alias ...='cd ../..'\n"
    "alias grep='grep --color=auto'\n"
    "\n"
    "export PS1=\"\\[\\e[38;5;75m\\]\\u@\\h \\[\\e[38;5;113m\\]\\w \\[\\e[38;5;189m\\]\\$ \\[\\e[0m\\]\"\n"
    "\n"
    "fastfetch\n";

static const char *BASH_PROFILE_CONTENT =
    "if [ -z $DISPLAY ] && [ $XDG_VTNR = 1 ]; then\n"
    "  exec startx\n"
    "fi\n";

static int configure_xfce(const char *username) {
    char cmd[4096];
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mConfiguring XFCE...\033[0m", 10, logo_start);
    fflush(stdout);

    setup_common_configs(username);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/xfce4 /mnt/home/%s/.config/xfce4", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "arch-chroot /mnt chown -R %s:%s /home/%s/.config/xfce4", username, username, username);
    system(cmd);

    Dotfile dotfiles[] = {
        { ".xinitrc", "exec startxfce4\n", 0755 },
        { ".bash_profile", BASH_PROFILE_CONTENT, 0644 },
        { ".bashrc", BASHRC_CONTENT, 0644 }
    };

    for (size_t i = 0; i < sizeof(dotfiles) / sizeof(dotfiles[0]); i++) {
        if (!create_user_dotfile(username, &dotfiles[i])) {
            LOG_ERROR("Failed to create dotfile: %s", dotfiles[i].filename);
            return 0;
        }
    }

    if (!setup_autologin(username)) {
        return 0;
    }

    return 1;
}

static int configure_oxwm(const char *username) {
    char cmd[4096];
    int rows, cols;
    get_terminal_size(&rows, &cols);

    clear_screen();
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[37mConfiguring OXWM...\033[0m", 10, logo_start);
    printf("\033[%d;%dH\033[37mCloning and building from source...\033[0m", 11, logo_start);
    fflush(stdout);

    LOG_INFO("Starting OXWM installation for user: %s", username);

    char oxwm_path[256];
    snprintf(oxwm_path, sizeof(oxwm_path), "/home/%s/oxwm", username);

    if (!git_clone_as_user(username, "https://github.com/tonybanters/oxwm", oxwm_path)) {
        LOG_ERROR("Failed to clone oxwm");
        show_message("Failed to clone OXWM");
        return 0;
    }

    if (!chroot_exec_fmt("cd %s && cargo build --release", oxwm_path)) {
        LOG_ERROR("Failed to build oxwm");
        show_message("Failed to build OXWM");
        return 0;
    }

    if (!chroot_exec_fmt("cp %s/target/release/oxwm /usr/bin/oxwm", oxwm_path)) {
        LOG_ERROR("Failed to install oxwm binary");
        show_message("Failed to install OXWM");
        return 0;
    }

    chroot_exec("chmod 755 /usr/bin/oxwm");

    setup_common_configs(username);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/gtk-3.0 /mnt/home/%s/.config/gtk-3.0", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/tonarchy/gtk-4.0 /mnt/home/%s/.config/gtk-4.0", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp /usr/share/tonarchy/gtkrc-2.0 /mnt/home/%s/.gtkrc-2.0", username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "/mnt/home/%s/.config/oxwm", username);
    create_directory(cmd, 0755);

    snprintf(cmd, sizeof(cmd), "cp /mnt%s/templates/tonarchy-config.lua /mnt/home/%s/.config/oxwm/config.lua", oxwm_path, username);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "arch-chroot /mnt chown -R %s:%s /home/%s/.config", username, username, username);
    system(cmd);

    Dotfile dotfiles[] = {
        { ".xinitrc", "export GTK_THEME=Adwaita-dark\nxset r rate 200 35 &\npicom --config ~/.config/picom/picom.conf &\nxwallpaper --zoom /usr/share/wallpapers/wall1.jpg &\nexec oxwm\n", 0755 },
        { ".bash_profile", BASH_PROFILE_CONTENT, 0644 },
        { ".bashrc", BASHRC_CONTENT, 0644 }
    };

    for (size_t i = 0; i < sizeof(dotfiles) / sizeof(dotfiles[0]); i++) {
        if (!create_user_dotfile(username, &dotfiles[i])) {
            LOG_ERROR("Failed to create dotfile: %s", dotfiles[i].filename);
            return 0;
        }
    }

    if (!setup_autologin(username)) {
        return 0;
    }

    LOG_INFO("OXWM installation completed successfully");
    show_message("OXWM installed successfully!");
    return 1;
}

int main(void) {
    logger_init("/tmp/tonarchy-install.log");
    LOG_INFO("Tonarchy installer started");

    if (!setup_wifi_if_needed()) {
        logger_close();
        return 1;
    }

    char username[256] = "";
    char password[256] = "";
    char confirmed_password[256] = "";
    char hostname[256] = "";
    char keyboard[256] = "";
    char timezone[256] = "";

    if (!get_form_input(username, password, confirmed_password, hostname, keyboard, timezone)) {
        logger_close();
        return 1;
    }

    const char *levels[] = {
        "Beginner (XFCE desktop - perfect for starters)",
        "Oxidized (OXWM Beta)"
    };

    int level = select_from_menu(levels, 2);
    if (level < 0) {
        LOG_INFO("Installation cancelled by user at level selection");
        logger_close();
        return 1;
    }

    LOG_INFO("Installation level selected: %d", level);

    char disk[64] = "";
    if (!select_disk(disk)) {
        LOG_INFO("Installation cancelled by user at disk selection");
        logger_close();
        return 1;
    }

    LOG_INFO("Selected disk: %s", disk);

    if (level == BEGINNER) {
        CHECK_OR_FAIL(partition_disk(disk), "Failed to partition disk");
        CHECK_OR_FAIL(install_packages_impl(XFCE_PACKAGES), "Failed to install packages");
        CHECK_OR_FAIL(configure_system_impl(username, password, hostname, keyboard, timezone, disk, 0), "Failed to configure system");
        CHECK_OR_FAIL(install_bootloader(disk), "Failed to install bootloader");
        configure_xfce(username);
    } else {
        CHECK_OR_FAIL(partition_disk(disk), "Failed to partition disk");
        CHECK_OR_FAIL(install_packages_impl(OXWM_PACKAGES), "Failed to install packages");
        CHECK_OR_FAIL(configure_system_impl(username, password, hostname, keyboard, timezone, disk, 0), "Failed to configure system");
        CHECK_OR_FAIL(install_bootloader(disk), "Failed to install bootloader");
        configure_oxwm(username);
    }

    system("cp /tmp/tonarchy-install.log /mnt/var/log/tonarchy-install.log");

    clear_screen();
    int rows, cols;
    get_terminal_size(&rows, &cols);
    draw_logo(cols);

    int logo_start = (cols - 70) / 2;
    printf("\033[%d;%dH\033[1;32mInstallation complete!\033[0m\n", 10, logo_start);
    printf("\033[%d;%dH\033[37mPress Enter to reboot...\033[0m\n", 12, logo_start);
    fflush(stdout);

    char c;
    enable_raw_mode();
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\r' || c == '\n') {
            break;
        }
    }
    disable_raw_mode();

    LOG_INFO("Tonarchy installer finished - scheduling reboot");
    logger_close();

    sync();
    system("sleep 2 && reboot &");

    exit(0);
}
