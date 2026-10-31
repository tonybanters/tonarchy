# Tonarchy Codebase Guide

This document explains how the project is structured, what the major code paths do, and why the installer calls each subsystem in the order it does.

## 1. Project at a glance

Tonarchy is an opinionated Arch installer. The main installer logic lives in [src/tonarchy.c](../src/tonarchy.c), and the ISO creation helper lives in [src/build_iso.c](../src/build_iso.c). The top-level driver is the Makefile, which builds the static installer and the ISO toolkit.

At a high level:

- [src/tonarchy.c](../src/tonarchy.c): interactive TUI installer for a live Arch environment
- [src/build_iso.c](../src/build_iso.c): ISO builder that prepares the `archiso` profile and output artifacts
- [iso/](../iso): Arch ISO profile with packages, bootloader config, and airootfs structure
- [assets/](../assets): GTK, rofi, firefox, alacritty, wallpaper, and theme assets copied into the installed system
- [walls/](../walls): packndles
- [Makefile](../Makefile): compile and testing commands

## 2. Why this project is split this way

Tonarchy has two operational modes:

1. The installer runs on a booted ISO/live system and does the actual OS setup.
2. The ISO build tool packages the static installer and profile into a bootable Arch ISO.

That separation matters because the installer is meant to be a single static binary that can run inside the live environment, while the build helper is responsible for copying the correct assets and invoking `mkarchiso`.

## 3. The live installer: main execution flow

The main logic starts in [src/tonarchy.c](../src/tonarchy.c).
age groups used by the installer as “preset” install bu
### 3.1 Startup

`main()` does the following in order:

1. `logger_init("/tmp/tonarchy-install.log")`
   - Creates a log file for the install session.
   - This is used for debugging and artifact review after installation.

2. `LOG_INFO("Tonarchy installer started")`
   - Writes a boot log entry.

3. `setup_wifi_if_needed()`
   - Checks whether the system has internet access.
   - If not, it scans for available Wi-Fi SSIDs and asks the user to connect.
   - This is required because the installer must fetch package data and clone external Git repos such as OXWM.

4. `get_form_input(...)`
   - Prompts for the user account, password, hostname, keyboard layout, and timezone.
   - It renders a terminal form using custom ANSI cursor positioning and simple validation.

5. `select_from_menu(...)` for installation tier
   - User chooses between `BEGINNER` and `OXIDIZED`.
   - `BEGINNER` installs XFCE; `OXIDIZED` installs OXWM.

6. `select_disk(...)`
   - Shows the candidate block devices.
   - Confirms destruction with the message `Type 'yes' to confirm` before proceeding.

### 3.2 Why this order matters

This ordering is intentionally serial:

- internet before packages
- user identity before system config
- disk choice before partitioning
- install mode before desktop configuration

If any of the earlier steps fail, the installer stops before it touches the disk.

## 4. Disk preparation and install path

After the form is complete, the actual installation flow is:

- `partition_disk(disk)`
  - Wipes the selected disk.
  - Creates UEFI or BIOS layout: EFI, swap, and root partitions.
  - Formats them with `mkfs`/`mkswap`.
  - Mounts them under `/mnt`.

- `install_packages_impl(package_list)`
  - Runs `pacstrap -K /mnt ...` with the chosen package bundle.
  - The package set is hard-coded as `XFCE_PACKAGES` or `OXWM_PACKAGES`.

- `configure_system_impl(...)`
  - Uses `arch-chroot /mnt` to configure locale, timezone, hostname, users, sudoers, networking, and boot settings.
  - Creates the target user and sets its password.
  - Enables the display manager or autologin depending on the desktop mode.

- `install_bootloader(disk)`
  - Installs either `systemd-boot` (UEFI) or `GRUB` (BIOS).
  - Writes the necessary loader configuration and boot entry.

- `setup_common_configs(username)`
  - Copies the theme files, wallpapers, Firefox profile, terminal config, rofi config, picom config, and custom dotfiles into the target system.

- `configure_xfce(username)` or `configure_oxwm(username)`
  - Finishes the desktop-specific setup.

## 5. The desktop-specific configuration code

### 5.1 XFCE flow

The beginner path is handled by `configure_xfce()`.

What it does:

- calls `setup_common_configs(username)`
- copies the XFCE config directory from the live ISO assets into `/home/$user/.config/xfce4`
- installs dotfiles like `.bashrc`, `.bash_profile`, and other user config files
- enables autologin via a systemd drop-in

This path is optimized for easy first-time Linux use: a full desktop environment with a minimal amount of user choice.

### 5.2 OXWM flow

The advanced path is `configure_oxwm()`.

That function performs the following:

1. `ensure_compatible_zig_version()`
   - Checks the Zig version inside the target chroot.
   - This is the compatibility guard added to prevent `zig build` failures caused by unsupported toolchains.

2. `git_clone_as_user(username, "https://github.com/tonybanters/oxwm", ... )`
   - Clones the upstream OXWM project into the user’s home directory.
   - This is the actual source build step.

3. `chroot_exec_fmt("cd %s && zig build -Doptimize=ReleaseSmall", oxwm_path)`
   - Compiles OXWM from source using Zig.
   - This is the key “advanced mode” behavior: OXWM is not installed via a package manager; it is built from source to match the local desktop and configuration expectations.

4. `cp %s/zig-out/bin/oxwm /usr/bin/oxwm`
   - Installs the built binary into the system path.

5. `setup_common_configs(username)`
   - Adds common theming, config, and app assets.

6. Copies GTK theme assets and default config files
   - This sets up the X session, `.xinitrc`, and OXWM configuration.

7. `setup_autologin(username)`
   - Enables autologin so the user lands directly into the window manager.

## 6. Important helper layers

### 6.1 Logging

The installer has a logger system with `logger_init`, `logger_close`, and `log_msg` in [src/tonarchy.c](../src/tonarchy.c).

This provides a structured installation log for debugging and postmortem troubleshooting.

### 6.2 File manipulation helpers

`write_file`, `write_file_fmt`, `create_directory`, and `set_file_perms` wrap common shell operations in C functions so the installer can reliably create files and config content without depending on a big library stack.

### 6.3 Chroot helpers

The installer is built to operate on a live system that later becomes the installed system. That is why helpers like `chroot_exec`, `chroot_exec_fmt`, and `chroot_exec_as_user` exist.

These functions run commands inside the target root, usually `/mnt`:

- `pacman` package configuration
- `useradd` / `usermod`
- `hwclock --systohc`
- `systemctl` equivalent through rootfs setup
- `git clone` and source builds done as a specific user

## 7. Terminal UI flow

The installer uses raw terminal control instead of ncurses. This gives the project a very lightweight, dependency-free TUI.

Key routines:

- `enable_raw_mode()` / `disable_raw_mode()`
  - Switch the terminal to single-character input mode.

- `draw_logo()` and `draw_form()`
  - Render the custom Tonarchy splash and form screens.

- `select_from_menu()`
  - Implements the key-driven menu for choosing options.

- `read_line()`
  - Reads user input without echo or with a configurable password mode.

The project intentionally avoids heavy dependencies; the UI is custom terminal code and ANSI cursor movement.

## 8. ISO build flow

The second major subsystem is [src/build_iso.c](../src/build_iso.c).

### 8.1 What it does

`main()` in the ISO builder:

1. Sets up the source and output directories.
2. Parses CLI flags like `--iso-profile`, `--out-dir`, and `--container`.
3. Builds a static copy of the installer binary.
4. Cleans and prepares the `airootfs` tree.
5. Runs `mkarchiso` to produce a bootable image.
6. Finds the newest `.iso` file in the output directory.

### 8.2 Key functions

- `build_tonarchy_static()`
  - Produces a static `tonarchy` binary with musl.

- `prepare_airootfs()`
  - Copies the static binary and the assets directory into the `airootfs` tree for the live ISO.

- `run_mkarchiso()` / `run_mkarchiso_in_container()`
  - Build the actual `.iso` with `mkarchiso`.

- `find_latest_iso()`
  - Locates the newest ISO built by the process.

## 9. Why the installer is structured this way

The architecture is intentionally linear and conservative:

- no hidden side effects before warning
- no package installs before confirmation of disk and mode
- no system changes before user identity is known
- dependencies are installed only once the target environment is ready

This reduces the chance of an irreversible mistake during installation.

## 10. OXWM compatibility note and Zig guard

The OXWM project has a declared minimum Zig version in its upstream configuration. The Tonarchy installer now checks the Zig version inside the chroot before compiling OXWM from source.

The check works like this:

- run `arch-chroot /mnt zig version`
- parse the reported version string
- require a minimum of `0.16.0`
- if the version is too old, run `pacman -S --noconfirm --needed zig` and re-check

This prevents the installer from getting several minutes into the OXWM build only to fail because the toolchain is too old.

## 11. Call chain summary

A simplified call chain for a normal beginner install is:

`main` -> `setup_wifi_if_needed` -> `get_form_input` -> `select_disk` -> `partition_disk` -> `install_packages_impl` -> `configure_system_impl` -> `install_bootloader` -> `configure_xfce` -> `setup_common_configs` -> `setup_autologin` -> reboot

A simplified call chain for OXWM install is:

`main` -> `setup_wifi_if_needed` -> `get_form_input` -> `select_disk` -> `partition_disk` -> `install_packages_impl` -> `configure_system_impl` -> `install_bootloader` -> `configure_oxwm` -> `ensure_compatible_zig_version` -> `git_clone_as_user` -> `zig build` -> install binary -> `setup_common_configs` -> `setup_autologin` -> reboot

## 12. What to read next

The best next reads are:

- [src/tonarchy.c](../src/tonarchy.c) for the end-to-end installer logic
- [src/build_iso.c](../src/build_iso.c) for the ISO packaging process
- [iso/](../iso) for the Arch profile used when building the bootable image
- [Makefile](../Makefile) for control flow and test commands

## 13. Short takeaway

Tonarchy is a custom, dependency-light Arch installer that intentionally keeps its logic in a single C installer and a companion ISO builder. The structure is simple but effective: collect user input, prepare the disk, install a curated package set, configure the system, inject one desktop profile, and install the bootloader. The OXWM path adds a source-build step that depends on a recent Zig toolchain, which is why the compatibility guard exists.
