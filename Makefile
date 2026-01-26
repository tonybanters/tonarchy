CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -O2 -Wno-format-truncation
LDFLAGS =
STATIC_LDFLAGS = -static

TARGET = tonarchy
SRC = src/tonarchy.c
LATEST_ISO = $(shell ls -t out/*.iso 2>/dev/null | head -1)
TEST_DISK = test-disk.qcow2

.PHONY: all clean static build build-container test test-nix test-disk test-nvme release clean-iso clean-vm

all: $(TARGET)

static: $(TARGET)-static

build_iso: src/build_iso.c src/build_iso.h
	$(CC) $(CFLAGS) src/build_iso.c -o build_iso

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

$(TARGET)-static: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)-static $(STATIC_LDFLAGS)

build: build_iso
	./build_iso --iso-profile ./iso --out-dir ./out

build-container: build_iso
	./build_iso --iso-profile ./iso --out-dir ./out --container podman

test-nix:
	@if [ -z "$(LATEST_ISO)" ]; then echo "No ISO found. Run 'nix run .#build_iso -- --container podman' first"; exit 1; fi
	./vm-test "$(LATEST_ISO)"

test:
	@if [ -z "$(LATEST_ISO)" ]; then echo "No ISO found. Run 'make build' first"; exit 1; fi
	@if [ ! -f "$(TEST_DISK)" ]; then \
		echo "Creating test disk..."; \
		qemu-img create -f qcow2 "$(TEST_DISK)" 20G; \
	fi
	@OVMF_CODE=$$(find /usr/share/edk2 /usr/share/OVMF -name "OVMF_CODE*.fd" 2>/dev/null | grep x64 | head -1); \
	if [ -z "$$OVMF_CODE" ]; then \
		echo "Error: OVMF not found. Install with: sudo pacman -S edk2-ovmf"; \
		exit 1; \
	fi; \
	OVMF_VARS=$$(find /usr/share/edk2 /usr/share/OVMF -name "OVMF_VARS*.fd" 2>/dev/null | grep x64 | head -1); \
	if [ ! -f ./OVMF_VARS.fd ]; then \
		cp "$$OVMF_VARS" ./OVMF_VARS.fd; \
	fi; \
	echo "Starting UEFI VM with ISO: $(LATEST_ISO)"; \
	qemu-system-x86_64 \
		-cpu host -enable-kvm -machine q35,accel=kvm \
		-smp $$(nproc) \
		-m 8192 \
		-drive file=$(TEST_DISK),format=qcow2,if=virtio \
		-drive if=pflash,format=raw,readonly=on,file=$$OVMF_CODE \
		-drive if=pflash,format=raw,file=./OVMF_VARS.fd \
		-drive file="$(LATEST_ISO)",media=cdrom,readonly=on,cache=none \
		-boot order=d \
		-vga virtio \
		-display gtk \
		-usb -device usb-tablet \
		-netdev user,id=net0,hostfwd=tcp::2222-:22 \
		-device virtio-net-pci,netdev=net0 \
		-boot menu=on

test-nvme:
	@if [ -z "$(LATEST_ISO)" ]; then echo "No ISO found. Run 'make build' first"; exit 1; fi
	@if [ ! -f "$(TEST_DISK)" ]; then \
		echo "Creating test disk..."; \
		qemu-img create -f qcow2 "$(TEST_DISK)" 20G; \
	fi
	@OVMF_CODE=$$(find /usr/share/edk2 /usr/share/OVMF -name "OVMF_CODE*.fd" 2>/dev/null | grep x64 | head -1); \
	if [ -z "$$OVMF_CODE" ]; then \
		echo "Error: OVMF not found. Install with: sudo pacman -S edk2-ovmf"; \
		exit 1; \
	fi; \
	OVMF_VARS=$$(find /usr/share/edk2 /usr/share/OVMF -name "OVMF_VARS*.fd" 2>/dev/null | grep x64 | head -1); \
	if [ ! -f ./OVMF_VARS.fd ]; then \
		cp "$$OVMF_VARS" ./OVMF_VARS.fd; \
	fi; \
	echo "Starting UEFI VM with NVMe disk: $(LATEST_ISO)"; \
	qemu-system-x86_64 \
		-cpu host -enable-kvm -machine q35,accel=kvm \
		-smp $$(nproc) \
		-m 8192 \
		-drive file=$(TEST_DISK),format=qcow2,if=none,id=nvme0 \
		-device nvme,serial=deadbeef,drive=nvme0 \
		-drive if=pflash,format=raw,readonly=on,file=$$OVMF_CODE \
		-drive if=pflash,format=raw,file=./OVMF_VARS.fd \
		-drive file="$(LATEST_ISO)",media=cdrom,readonly=on,cache=none \
		-boot order=d \
		-vga virtio \
		-display gtk \
		-usb -device usb-tablet \
		-netdev user,id=net0,hostfwd=tcp::2222-:22 \
		-device virtio-net-pci,netdev=net0 \
		-boot menu=on

test-disk:
	@if [ ! -f "$(TEST_DISK)" ]; then \
		echo "No test disk found. Run 'make test' first to install."; \
		exit 1; \
	fi
	@OVMF_CODE=$$(find /usr/share/edk2 /usr/share/OVMF -name "OVMF_CODE*.fd" 2>/dev/null | grep x64 | head -1); \
	if [ -z "$$OVMF_CODE" ]; then \
		echo "Error: OVMF not found. Install with: sudo pacman -S edk2-ovmf"; \
		exit 1; \
	fi; \
	echo "Booting from installed disk: $(TEST_DISK)"; \
	echo "SSH available at: ssh -p 2222 user@localhost"; \
	qemu-system-x86_64 \
		-cpu host -enable-kvm -machine q35,accel=kvm \
		-smp $$(nproc) \
		-m 8192 \
		-drive file=$(TEST_DISK),format=qcow2,if=virtio \
		-drive if=pflash,format=raw,readonly=on,file=$$OVMF_CODE \
		-drive if=pflash,format=raw,file=./OVMF_VARS.fd \
		-vga virtio \
		-display gtk \
		-usb -device usb-tablet \
		-netdev user,id=net0,hostfwd=tcp::2222-:22 \
		-device virtio-net-pci,netdev=net0 \
		-boot menu=on

release: build
	@if [ -z "$(LATEST_ISO)" ]; then echo "No ISO found after build"; exit 1; fi
	@echo "Generating checksums for $(LATEST_ISO)..."
	@cd out && sha256sum $(notdir $(LATEST_ISO)) > $(notdir $(LATEST_ISO)).sha256
	@cd out && md5sum $(notdir $(LATEST_ISO)) > $(notdir $(LATEST_ISO)).md5
	@echo "Release files ready in out/:"
	@ls -lh out/$(notdir $(LATEST_ISO))*

clean-vm:
	rm -f $(TEST_DISK) OVMF_VARS.fd

clean-iso:
	rm -rf out/*.iso out/*.sha256 out/*.md5
	sudo rm -rf /tmp/tonarchy_iso_work

clean: clean-iso clean-vm
	rm -f $(TARGET) $(TARGET)-static build_iso
