#   EntropyOS
#   Copyright (C) 2025  Gabriel Sîrbu

#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; version 2 of the License.

#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.

#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.



CC = x86_64-w64-mingw32-gcc

# ISO name
ISO_OUT = EntropyOS-alpha1-x86_64.iso
# Path used by headless installer runner
ISO_INSTALLER := $(ISO_OUT)

IMG = EntropyOS.img
IMGSIZE_MB = 256

# Virtual SATA disk for testing
SATA_DISK = sata-disk.img
SATA_DISK_SIZE_MB = 1024

.PHONY: all run run-no-recompile clean iso
.PHONY: run-PCIe run-bios build-bios

# Default: build EFI then create a UEFI-bootable ISO from the `iso/` tree
all: clean $(ISO_OUT)

# Build main system as iso/BOOT.EFI (everything except installer)
iso/BOOT.EFI: fs/gpt/gpt.c fs/fat32/fat32.c fs/exfat/exfat.c kernel/serial.c init/main.c gui/helpers.c gui/console.c kernel/time.c kernel/mem.c kernel/boot.c kernel/utils.c kernel/chkstk.c gui/graphics_init.c gui/fonts.c gui/button.c drivers/poweroffreboot.c drivers/pci/pci.c drivers/pcie/pcie.c drivers/usb/xhci.c drivers/usb/xhci_core.c drivers/usb/usb_host_xhci.c drivers/thumbdrive/mass_storage.c drivers/usb/touchpad_adapter.c drivers/hid/hid.c drivers/hid/keyboard.c drivers/ps2/ps2.c drivers/ps2/ps2_diag.c gui/icons.c scheduler/scheduler.c drivers/block/block.c drivers/sata/sata.c generated/graphics_blob.o
	@mkdir -p iso
	@echo "CC iso/BOOT.EFI"
	@$(MAKE) generated/graphics_blob.o >/dev/null 2>&1 || true
	@$(CC) -Iinclude \
		fs/gpt/gpt.c \
		fs/fat32/fat32.c \
		fs/exfat/exfat.c \
		fs/exfat/io.c \
		fs/exfat/mount.c \
		fs/exfat/lookup.c \
		fs/exfat/cluster.c \
		fs/exfat/node.c \
		fs/exfat/repair.c \
		fs/exfat/log.c \
		fs/exfat/utils.c \
		fs/exfat/utf.c \
		fs/exfat/time.c \
		fs/exfat/mkfs/main.c \
		fs/exfat/mkfs/mkexfat.c \
		fs/exfat/mkfs/cbm.c \
		fs/exfat/mkfs/fat.c \
		fs/exfat/mkfs/vbr.c \
		fs/exfat/mkfs/rootdir.c \
		fs/exfat/mkfs/uct.c \
		fs/exfat/mkfs/uctc.c \
		fs/exfat/compat.c \
		boot/Uefi/boot.c \
		init/main.c \
		gui/helpers.c \
		gui/console.c \
		kernel/time.c \
		kernel/mem.c \
		kernel/boot.c \
		kernel/utils.c \
		kernel/chkstk.c \
		gui/graphics_init.c \
		gui/fonts.c \
		gui/button.c \
		drivers/poweroffreboot.c \
		drivers/pci/pci.c \
		drivers/pcie/pcie.c \
		drivers/usb/xhci.c \
		drivers/usb/xhci_core.c \
		drivers/usb/usb_host_xhci.c \
		drivers/thumbdrive/mass_storage.c \
		drivers/usb/touchpad_adapter.c \
		drivers/hid/hid.c \
		drivers/hid/keyboard.c \
		drivers/ps2/ps2.c \
		drivers/ps2/ps2_diag.c \
		gui/icons.c \
		generated/graphics_blob.o \
		scheduler/scheduler.c \
		drivers/block/block.c \
		drivers/sata/sata.c \
		-std=c17 -Wall -Wextra -Wpedantic -D__UEFI__ \
		-mno-red-zone -ffreestanding -fno-builtin -nostdlib \
		-Wl,-subsystem,10 -Wl,-entry,efi_main \
		-fno-builtin -O2 \
		-o $@ || (echo "Building iso/BOOT.EFI failed"; exit 1)

# Embed iso/BOOT.EFI into an object (linkable) using objcopy
generated/boot_blob.o: iso/BOOT.EFI
	@mkdir -p generated
	@command -v objcopy >/dev/null 2>&1 || (echo "objcopy is required to embed binary blobs; install binutils."; exit 1)
	@objcopy -I binary -O elf64-x86-64 -B i386:x86-64 iso/BOOT.EFI $@

# Build the graphics subsystem as a separate binary and embed it
# The graphics code is compiled into an object with only the entry at
# `graphics_start`. We convert the object to a raw binary and then
# embed that binary into the kernel image as `generated/graphics_blob.o`.
generated/graphics.o: gui/graphics/graphics.S
	@mkdir -p generated
	@echo "AS gui/graphics/graphics.S"
	@$(CC) -Iinclude -ffreestanding -fno-builtin -nostdlib -c gui/graphics/graphics.S -o $@

generated/graphics.bin: generated/graphics.o
	@mkdir -p generated
	@echo "OBJCOPY -> binary: generated/graphics.bin"
	@objcopy -O binary --only-section=.text generated/graphics.o $@

generated/graphics_blob.o: generated/graphics.bin
	@mkdir -p generated
	@command -v objcopy >/dev/null 2>&1 || (echo "objcopy is required to embed binary blobs; install binutils."; exit 1)
	@objcopy -I binary -O elf64-x86-64 -B i386:x86-64 generated/graphics.bin $@

# Ensure ISO contains a UEFI startup script so the Internal Shell auto-runs the
# installer when booting the ISO. The file is generated at build time so `make`
# never fails if `iso/` is cleaned.
iso/startup.nsh:
	@mkdir -p iso
	@printf '%s\n' '\EFI\BOOT\INSTALLER.EFI' 'FS0:\EFI\BOOT\INSTALLER.EFI' 'FS1:\EFI\BOOT\INSTALLER.EFI' 'FS2:\EFI\BOOT\INSTALLER.EFI' 'exit' > iso/startup.nsh


# Build installer as iso/EFI/BOOT/INSTALLER.EFI (contains embedded BOOT.EFI blob)
iso/EFI/BOOT/INSTALLER.EFI: generated/boot_blob.o src/installer/installer.c fs/gpt/gpt.c fs/fat32/fat32.c fs/exfat/exfat.c kernel/chkstk.c drivers/block/block.c drivers/usb/usb_host_xhci.c drivers/thumbdrive/mass_storage.c
	@mkdir -p iso/EFI/BOOT
	@echo "CC iso/EFI/BOOT/INSTALLER.EFI (installer)"
	@$(CC) -Iinclude \
		generated/boot_blob.o \
		src/installer/installer.c \
		fs/gpt/gpt.c \
		fs/fat32/fat32.c \
		fs/exfat/exfat.c \
		fs/exfat/io.c \
		fs/exfat/mount.c \
		fs/exfat/lookup.c \
		fs/exfat/cluster.c \
		fs/exfat/node.c \
		fs/exfat/repair.c \
		fs/exfat/log.c \
		fs/exfat/utils.c \
		fs/exfat/utf.c \
		fs/exfat/time.c \
		fs/exfat/mkfs/main.c \
		fs/exfat/mkfs/mkexfat.c \
		fs/exfat/mkfs/cbm.c \
		fs/exfat/mkfs/fat.c \
		fs/exfat/mkfs/vbr.c \
		fs/exfat/mkfs/rootdir.c \
		fs/exfat/mkfs/uct.c \
		fs/exfat/mkfs/uctc.c \
		fs/exfat/compat.c \
		kernel/mem.c \
		kernel/chkstk.c \
		drivers/block/block.c \
		drivers/usb/usb_host_xhci.c \
		drivers/usb/xhci.c \
		drivers/usb/xhci_core.c \
		drivers/pci/pci.c \
		drivers/pcie/pcie.c \
		drivers/sata/sata.c \
		drivers/thumbdrive/mass_storage.c \
		-std=c17 -Wall -Wextra -Wpedantic -D__UEFI__ \
		-mno-red-zone -ffreestanding -fno-builtin -nostdlib \
		-Wl,-subsystem,10 -Wl,-entry,efi_main \
		-fno-builtin -O2 \
		-o $@ || (echo "Building installer failed"; exit 1)

# Build UEFI bootloader (reads ENTROPY.OS from ExFAT DATA partition and boots it)
iso/EFI/BOOT/BOOTX64.EFI: generated/boot_blob.o boot/Uefi/bootloader.c fs/gpt/gpt.c fs/fat32/fat32.c fs/exfat/exfat.c kernel/chkstk.c drivers/block/block.c drivers/thumbdrive/mass_storage.c drivers/usb/usb_host_xhci.c drivers/usb/xhci.c drivers/usb/xhci_core.c drivers/pci/pci.c drivers/pcie/pcie.c drivers/sata/sata.c
	@mkdir -p iso/EFI/BOOT
	@echo "CC iso/EFI/BOOT/BOOTX64.EFI (bootloader)"
	@$(CC) -Iinclude \
		generated/boot_blob.o \
		boot/Uefi/bootloader.c \
		fs/gpt/gpt.c \
		fs/fat32/fat32.c \
		fs/exfat/exfat.c \
		fs/exfat/io.c \
		fs/exfat/mount.c \
		fs/exfat/lookup.c \
		fs/exfat/cluster.c \
		fs/exfat/node.c \
		fs/exfat/repair.c \
		fs/exfat/log.c \
		fs/exfat/utils.c \
		fs/exfat/utf.c \
		fs/exfat/time.c \
		fs/exfat/mkfs/main.c \
		fs/exfat/mkfs/mkexfat.c \
		fs/exfat/mkfs/cbm.c \
		fs/exfat/mkfs/fat.c \
		fs/exfat/mkfs/vbr.c \
		fs/exfat/mkfs/rootdir.c \
		fs/exfat/mkfs/uct.c \
		fs/exfat/mkfs/uctc.c \
		fs/exfat/compat.c \
		kernel/mem.c \
		kernel/chkstk.c \
		drivers/block/block.c \
		drivers/thumbdrive/mass_storage.c \
		drivers/usb/usb_host_xhci.c \
		drivers/usb/xhci.c \
		drivers/usb/xhci_core.c \
		drivers/pci/pci.c \
		drivers/pcie/pcie.c \
		drivers/sata/sata.c \
		-std=c17 -Wall -Wextra -Wpedantic -D__UEFI__ \
		-mno-red-zone -ffreestanding -fno-builtin -nostdlib \
		-Wl,-subsystem,10 -Wl,-entry,efi_main \
		-fno-builtin -O2 \
		-o $@ || (echo "Building bootloader failed"; exit 1)

# Create a UEFI El Torito ISO from the `iso/` tree
$(ISO_OUT): iso/EFI/BOOT/BOOTX64.EFI iso/EFI/BOOT/INSTALLER.EFI iso/startup.nsh iso/EntropyOS.img boot/LegacyBios/boot.img
	@echo "Creating UEFI-bootable ISO: $@"
	@command -v xorriso >/dev/null 2>&1 || (echo "xorriso is required to create a UEFI El Torito ISO. Install xorriso and retry."; exit 1)
	@MBR=; \
	for p in /usr/lib/ISOLINUX/isohdpfx.bin /usr/lib/syslinux/isohdpfx.bin /usr/lib/syslinux/bios/isohdpfx.bin /usr/lib/syslinux/modules/bios/isohdpfx.bin /usr/share/syslinux/isohdpfx.bin; do \
		if [ -f $$p ]; then MBR=$$p; break; fi; \
	done; \
	if [ -n "$$MBR" ]; then \
		MBRARG="-isohybrid-mbr $$MBR"; \
	else \
		MBRARG=""; \
	fi; \
	mkdir -p iso/boot/LegacyBios; cp -f boot/LegacyBios/boot.img iso/boot/LegacyBios/boot.img; \
	xorriso -as mkisofs \
		-o $@ \
		-V "EntropyOS-1-intel64" \
		-c boot.catalog \
			-eltorito-boot boot/LegacyBios/boot.img -no-emul-boot -boot-load-size 4 -boot-info-table \
		-eltorito-alt-boot \
			-e EntropyOS.img -no-emul-boot \
			-isohybrid-gpt-basdat $$MBRARG \
			-append_partition 2 0xef EntropyOS.img \
		-allow-lowercase -allow-multidot \
		iso

# Build legacy BIOS boot sector
boot/LegacyBios/boot.bin: boot/LegacyBios/boot.asm
	@command -v nasm >/dev/null 2>&1 || (echo "nasm is required to assemble legacy BIOS image. Install nasm."; exit 1)
	@mkdir -p boot/LegacyBios
	nasm -f bin -o $@ $<

# Build 64-bit legacy kernel binary
kernel/legacymain.bin: kernel/legacymain.c boot/LegacyBios/legacylink.ld
	@echo "Building legacy 64-bit kernel: $@"
	@mkdir -p kernel
	@if command -v x86_64-elf-gcc >/dev/null 2>&1; then \
		x86_64-elf-gcc -m64 -ffreestanding -fno-builtin -nostdlib -Iinclude -c kernel/legacymain.c -o kernel/legacymain.o; \
		x86_64-elf-ld -m elf_x86_64 -T boot/LegacyBios/legacylink.ld -o kernel/legacymain.elf kernel/legacymain.o; \
		objcopy -O binary kernel/legacymain.elf kernel/legacymain.bin; \
	else \
		gcc -m64 -ffreestanding -fno-builtin -nostdlib -Iinclude -c kernel/legacymain.c -o kernel/legacymain.o; \
		ld -m elf_x86_64 -T boot/LegacyBios/legacylink.ld -o kernel/legacymain.elf kernel/legacymain.o; \
		objcopy -O binary kernel/legacymain.elf kernel/legacymain.bin; \
	fi

# Make boot image (boot sector + header + kernel)
boot/LegacyBios/boot.img: boot/LegacyBios/boot.bin kernel/legacymain.bin
	@echo "Creating boot image: $@"
	@mkdir -p boot/LegacyBios
	@size=$$(stat -c%s kernel/legacymain.bin); ksectors=$$(( (size + 511) / 512 )); \
	python3 -c "import sys,struct; n=int(sys.argv[1]); open('boot/LegacyBios/header.bin','wb').write(struct.pack('<H',n)+b'\x00'*(512-2))" $$ksectors; \
	dd if=boot/LegacyBios/boot.bin of=boot/LegacyBios/boot_sector.bin bs=512 count=1 2>/dev/null || true; \
	cat boot/LegacyBios/boot_sector.bin boot/LegacyBios/header.bin kernel/legacymain.bin > boot/LegacyBios/boot.img; \
	rm -f boot/LegacyBios/header.bin boot/LegacyBios/boot_sector.bin

.PHONY: build-bios
build-bios: boot/LegacyBios/boot.bin
	@echo "Built legacy BIOS boot sector: boot/LegacyBios/boot.bin"

# Run BIOS target using AHCI SATA disk
.PHONY: run-bios
run-bios: $(ISO_OUT) $(SATA_DISK)
	@echo "Running ISO in legacy BIOS mode (AHCI SATA disk)."
	qemu-system-x86_64 -machine q35 -m 1024 \
		-cdrom $(ISO_OUT) -boot d \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet -serial stdio

# Create FAT EFI image
$(IMG): iso/EFI/BOOT/BOOTX64.EFI
	@echo "Creating FAT image: $@ (size=$(IMGSIZE_MB)MB)"
	dd if=/dev/zero of=$@ bs=1M count=$(IMGSIZE_MB)
	@if command -v mkfs.vfat >/dev/null 2>&1; then \
		mkfs.vfat -F 32 $@; \
	elif command -v mkfs.fat >/dev/null 2>&1; then \
		mkfs.fat -F 32 $@; \
	elif [ -x /usr/sbin/mkfs.fat ]; then \
		/usr/sbin/mkfs.fat -F 32 $@; \
	elif [ -x /sbin/mkfs.fat ]; then \
		/sbin/mkfs.fat -F 32 $@; \
	else \
		echo "mkfs.vfat or mkfs.fat is required."; exit 1; \
	fi
	@echo "Copying iso/ contents into $@ using mtools"
	mmd -i $@ ::/EFI || true
	mmd -i $@ ::/EFI/BOOT || true
	mcopy -i $@ -s iso/* ::

iso/EntropyOS.img: $(IMG)
	@mkdir -p iso
	cp $(IMG) iso/EntropyOS.img

# Create virtual SATA disk
$(SATA_DISK):
	@echo "Creating virtual SATA disk: $@ (size=$(SATA_DISK_SIZE_MB)MB)"
	@if [ ! -f $(SATA_DISK) ]; then \
		dd if=/dev/zero of=$(SATA_DISK) bs=1M count=$(SATA_DISK_SIZE_MB) 2>/dev/null; \
		echo "Virtual SATA disk created"; \
	else \
		echo "SATA disk exists, skipping"; \
	fi

# Run targets
.PHONY: run
run: all $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-cdrom $(ISO_OUT) \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio

.PHONY: run-installer
run-installer: $(ISO_INSTALLER) $(SATA_DISK)
	@echo "Starting headless installer ISO; serial -> installer-serial.log"
	@rm -f installer-serial.log || true
	@qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-drive file=$(ISO_INSTALLER),if=ide,media=cdrom,format=raw \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw,cache=directsync \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet -display none -serial file:installer-serial.log & \
	QEMU_PID=$$!; \
	# wait for installer app to finish (ONLY accept the installer app signal)
	# increase timeout to accommodate slower hosts/CI
	TIMEOUT=120; i=0; until grep -q "Installer: Done\." installer-serial.log 2>/dev/null; do \
		sleep 1; i=$$((i+1)); if [ $$i -ge $$TIMEOUT ]; then echo "run-installer: timeout waiting for installer log"; break; fi; \
	done; \
	sleep 3; kill $$QEMU_PID >/dev/null 2>&1 || true; sleep 1; wait $$QEMU_PID 2>/dev/null || true; \
	sync; sleep 1; \
	echo "installer serial (last 200 lines):"; tail -n 200 installer-serial.log || true

.PHONY: run-virt
run-virt: all $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-enable-kvm -cpu host \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-cdrom $(ISO_OUT) \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio

		

.PHONY: run-virt-gdb
run-virt-gdb: all $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-enable-kvm -cpu host \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-cdrom $(ISO_OUT) \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio -S -s

.PHONY: run-sata-virt-gdb
run-sata-virt-gdb: $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-enable-kvm -cpu host \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0,bootindex=1 \
		-usb -device usb-tablet \
		-serial stdio -S -s

.PHONY: check-sata
check-sata: $(SATA_DISK)
	@echo "Checking SATA disk image for ExFAT integrity (non-destructive)"
	@command -v losetup >/dev/null 2>&1 || (echo "losetup required"; exit 1)
	@LOOP=$$(losetup --show -f -P $(SATA_DISK)); echo "loop device: $$LOOP"; \
	# prefer absolute /usr/sbin/fsck.exfat if present (some distros install it there)
	if [ -x /usr/sbin/fsck.exfat ]; then \
		echo "running /usr/sbin/fsck.exfat -n"; sudo /usr/sbin/fsck.exfat -n $${LOOP}p2 || true; \
	elif command -v fsck.exfat >/dev/null 2>&1; then \
		echo "running fsck.exfat -n"; sudo fsck.exfat -n $${LOOP}p2 || true; \
	elif command -v exfatfsck >/dev/null 2>&1; then \
		echo "running exfatfsck -n"; sudo exfatfsck -n $${LOOP}p2 || true; \
	else \
		echo "no exfat fsck utility found; please install 'exfatprogs' or 'exfat-utils' (Debian/Ubuntu: sudo apt install exfatprogs)"; \
	fi; \
	losetup -d $${LOOP}

.PHONY: check-deps
check-deps:
	@echo "Checking host tools required for build & tests"; \
	missing=0; \
	for cmd in losetup guestfish xorriso objcopy; do \
		if ! command -v $$cmd >/dev/null 2>&1; then echo " MISSING: $$cmd"; missing=1; else echo " OK: $$cmd"; fi; \
	done; \
	if [ -x /usr/sbin/fsck.exfat ]; then echo " OK: /usr/sbin/fsck.exfat"; elif command -v fsck.exfat >/dev/null 2>&1; then echo " OK: fsck.exfat"; elif command -v exfatfsck >/dev/null 2>&1; then echo " OK: exfatfsck"; else echo " MISSING: exFAT fsck (install exfatprogs/exfat-utils)"; missing=1; fi; \
	if [ $$missing -eq 0 ]; then echo "check-deps: all good"; else echo ""; echo "To fix on Debian/Ubuntu: sudo apt install exfatprogs libguestfs-tools xorriso dosfstools util-linux"; false; fi

.PHONY: test-install
test-install: $(SATA_DISK)
	@echo "Running automated install smoke-test (fsck + file checks)"
	@command -v /usr/sbin/losetup >/dev/null 2>&1 || (echo "/usr/sbin/losetup required"; exit 1)
	@sudo bash tools/run-install-check.sh $(SATA_DISK)

# CI-friendly verifier (no sudo/mount required if guestfish is available)
.PHONY: test-install-ci
# run installer first to ensure sata-disk.img is populated, then verify with guestfish
test-install-ci: $(SATA_DISK)
	@command -v guestfish >/dev/null 2>&1 || (echo "guestfish required for test-install-ci; install libguestfs-tools"; exit 1)
	@echo "Ensuring SATA disk is populated (running installer)..."
	@$(MAKE) run-installer >/dev/null 2>&1 || true
	@echo "Running CI-friendly smoke-test via guestfish"
	@# Mount partition 2 (ExFAT data partition) manually instead of using -i
	guestfish --ro -a $(SATA_DISK) run : mount-ro /dev/sda2 / : ls /sys/DoNotTouch/Entropy.OS >/dev/null 2>&1 || (echo "MISSING: /sys/DoNotTouch/Entropy.OS"; exit 1)
	@guestfish --ro -a $(SATA_DISK) run : mount-ro /dev/sda2 / : ls /sys/fonts >/dev/null 2>&1 || (echo "MISSING: /sys/fonts"; exit 1)
	@guestfish --ro -a $(SATA_DISK) run : mount-ro /dev/sda2 / : ls /freedom/user/README.txt >/dev/null 2>&1 || (echo "MISSING: /freedom/user/README.txt"; exit 1)
	@echo "test-install-ci: PASS"

run-disk: all $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio \

run-gdb: all $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-cdrom $(ISO_OUT) \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio \
		-S -s

.PHONY: run-no-recompile
run-no-recompile: $(ISO_OUT) $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-cdrom $(ISO_OUT) \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-usb -device usb-tablet \
		-serial stdio

.PHONY: run-PCIe
run-PCIe: $(ISO_OUT) $(SATA_DISK)
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-device qemu-xhci,id=xhci \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0 \
		-cdrom $(ISO_OUT) \
		-usb -device usb-mouse \
		-serial stdio

.PHONY: run-auto
run-auto: $(ISO_OUT) $(SATA_DISK)
	@command -v expect >/dev/null 2>&1 || (echo "expect is required for run-auto; install package 'expect' and retry."; exit 1)
	expect ./scripts/run-uefi-auto.exp $(ISO_OUT)

clean:
	@echo "\e[31mCLEAN\e[0m"
	rm -rf iso $(IMG) $(ISO_OUT) $(SATA_DISK) generated

.PHONY: burn
burn:
	@clear
	@echo "=========================== will execute lsblk to locate needed drive:"
	@lsblk
	@echo "=========================== WARNING: this will DESTROY all data on the target drive!"
	@echo "===================================================================================="
	@read -p "Drive name (e.g., sdb) goes here: " DRIVE; \
	echo "You selected /dev/$$DRIVE"; \
	echo "Proceeding in 5 seconds... press Ctrl-C to abort"; \
	sleep 5; \
	echo "Flashing EntropyOS to /dev/$$DRIVE"; \
	sudo dd if=$(ISO_OUT) of=/dev/$$DRIVE bs=4M status=progress conv=fsync; \
	sync; \
	echo "Done."

loc: LOC
LOC:
	find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.asm" -o -name "Makefile" \) -exec wc -l {} + | sort -n

.PHONY: run-sata-only
run-sata-only: $(SATA_DISK)
	@echo "Booting directly from SATA disk (no ISO)"
	qemu-system-x86_64 -machine q35 -m 1G \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0,bootindex=1 \
		-usb -device usb-tablet \
		-serial stdio

.PHONY: run-sata-virt
run-sata-virt: $(SATA_DISK)
	@echo "Booting directly from SATA disk (no ISO, KVM vibes)"
	qemu-system-x86_64 -machine q35 -m 1G \
		-enable-kvm -cpu host \
		-drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=OVMF/OVMF_VARS.fd \
		-device ahci,id=ahci0 \
		-drive id=satadisk,file=$(SATA_DISK),if=none,format=raw \
		-device ide-hd,drive=satadisk,bus=ahci0.0,bootindex=1 \
		-usb -device usb-tablet \
		-serial stdio
