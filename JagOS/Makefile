# =============================================================================
# JagOs - Makefile
# Build pipeline: boot.asm + kernel.c + shell.cpp → jagos.img
#
# Requirements (WSL/Ubuntu):
#   sudo apt install nasm gcc g++ binutils qemu-system-x86
#   (All tools must support 32-bit / i686 targets)
#
# Usage:
#   make          - Build everything
#   make run      - Build and launch in QEMU
#   make clean    - Remove all build artifacts
#   make debug    - Build and run with QEMU debug output
# =============================================================================

# --- Tools -------------------------------------------------------------------
ASM      := nasm
CC       := gcc
CXX      := g++
LD       := ld

# --- Flags -------------------------------------------------------------------

# Assembler: flat binary for bootloader
ASM_FLAGS := -f bin

# C compiler: 32-bit, freestanding, no stdlib
CC_FLAGS  := -m32 \
             -ffreestanding \
             -nostdlib \
             -nostdinc \
             -fno-stack-protector \
             -fno-pic \
             -fno-pie \
             -mno-80387 \
             -O2 \
             -Wall \
             -Wextra \
             -std=c11

# C++ compiler: same + no exceptions/RTTI (kernel doesn't support them)
CXX_FLAGS := -m32 \
             -ffreestanding \
             -nostdlib \
             -nostdinc \
             -fno-stack-protector \
             -fno-pic \
             -fno-pie \
             -fno-rtti \
             -fno-exceptions \
             -fno-threadsafe-statics \
             -fno-use-cxa-atexit \
             -O2 \
             -Wall \
             -Wextra \
             -std=c++14

# Linker: 32-bit ELF, use our script, no stdlib
LD_FLAGS  := -m elf_i386 \
             -T linker.ld \
             --oformat binary \
             -nostdlib

# --- Targets -----------------------------------------------------------------
BOOT_SRC   := boot.asm
KERNEL_SRC := kernel.c
SHELL_SRC  := shell.cpp

BOOT_BIN   := boot.bin
KERNEL_OBJ := kernel.o
SHELL_OBJ  := shell.o
KERNEL_BIN := kernel.bin
IMG        := jagos.img

# --- Default target ----------------------------------------------------------
.PHONY: all
all: $(IMG)
	@echo ""
	@echo "┌─────────────────────────────────────────┐"
	@echo "│     JagOs build complete!               │"
	@echo "│                                         │"
	@echo "│  Run: make run                          │"
	@echo "│  Or:  qemu-system-x86_64 \\              │"
	@echo "│         -drive format=raw,file=jagos.img│"
	@echo "└─────────────────────────────────────────┘"

# --- Step 1: Assemble bootloader (512-byte flat binary) ----------------------
$(BOOT_BIN): $(BOOT_SRC)
	@echo "[ASM]  $< → $@"
	$(ASM) $(ASM_FLAGS) $< -o $@
	@# Verify boot signature
	@python3 -c "data=open('$@','rb').read(); \
	  assert len(data)==512, f'boot.bin must be 512 bytes, got {len(data)}'; \
	  assert data[510]==0x55 and data[511]==0xAA, 'Missing boot signature 0xAA55'; \
	  print('[CHK]  boot.bin: 512 bytes, signature OK')"

# --- Step 2: Compile kernel (C) ----------------------------------------------
$(KERNEL_OBJ): $(KERNEL_SRC)
	@echo "[CC]   $< → $@"
	$(CC) $(CC_FLAGS) -c $< -o $@

# --- Step 3: Compile shell (C++) ---------------------------------------------
$(SHELL_OBJ): $(SHELL_SRC)
	@echo "[CXX]  $< → $@"
	$(CXX) $(CXX_FLAGS) -c $< -o $@

# --- Step 4: Link kernel + shell into flat binary ----------------------------
$(KERNEL_BIN): $(KERNEL_OBJ) $(SHELL_OBJ) linker.ld
	@echo "[LD]   $(KERNEL_OBJ) + $(SHELL_OBJ) → $@"
	$(LD) $(LD_FLAGS) $(KERNEL_OBJ) $(SHELL_OBJ) -o $@
	@KSIZE=$$(wc -c < $@); \
	 KSECTORS=$$(( ($$KSIZE + 511) / 512 )); \
	 echo "[SZ]   kernel: $$KSIZE bytes ($$KSECTORS sectors)"; \
	 if [ $$KSECTORS -gt 64 ]; then \
	   echo "[WARN] Kernel > 64 sectors! Increase KERNEL_SECTORS in boot.asm"; \
	 fi

# --- Step 5: Combine into disk image -----------------------------------------
$(IMG): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG]  Creating disk image..."
	@# Boot sector (512 bytes) + kernel
	cat $(BOOT_BIN) $(KERNEL_BIN) > $(IMG)
	@# Pad to at least 1.44MB (floppy size) for QEMU compatibility
	@IMGSIZE=$$(wc -c < $(IMG)); \
	 PADSIZE=$$((1474560 - $$IMGSIZE)); \
	 if [ $$PADSIZE -gt 0 ]; then \
	   dd if=/dev/zero bs=1 count=$$PADSIZE >> $(IMG) 2>/dev/null; \
	 fi
	@echo "[OK]   $(IMG) ready ($$(wc -c < $(IMG)) bytes)"

# --- Run in QEMU (headless terminal window) ----------------------------------
.PHONY: run
run: $(IMG)
	@echo "[QEMU] Launching JagOs..."
	qemu-system-x86_64 \
	  -drive format=raw,file=$(IMG),index=0,media=disk \
	  -m 32M \
	  -display sdl \
	  -no-reboot

# --- Run with SDL display (same as above, explicit) --------------------------
.PHONY: run-sdl
run-sdl: $(IMG)
	qemu-system-x86_64 \
	  -drive format=raw,file=$(IMG),index=0,media=disk \
	  -m 32M \
	  -display sdl \
	  -no-reboot

# --- Debug: QEMU with GDB stub + serial log ----------------------------------
.PHONY: debug
debug: $(IMG)
	@echo "[DBG]  Launching QEMU with GDB stub on :1234"
	@echo "[DBG]  Connect: gdb -ex 'target remote :1234' -ex 'set arch i386'"
	qemu-system-x86_64 \
	  -drive format=raw,file=$(IMG),index=0,media=disk \
	  -m 32M \
	  -display sdl \
	  -s -S \
	  -no-reboot \
	  -serial stdio

# --- Debug: boot only (just the bootloader, useful to test disk load) --------
.PHONY: debug-boot
debug-boot: $(IMG)
	qemu-system-x86_64 \
	  -drive format=raw,file=$(IMG),index=0,media=disk \
	  -m 32M \
	  -display sdl \
	  -d int,cpu_reset \
	  -no-reboot \
	  -no-shutdown

# --- Clean build artifacts ---------------------------------------------------
.PHONY: clean
clean:
	@echo "[CLN]  Removing build artifacts..."
	rm -f $(BOOT_BIN) $(KERNEL_OBJ) $(SHELL_OBJ) $(KERNEL_BIN) $(IMG)
	@echo "[CLN]  Done."

# --- Show sizes of all components --------------------------------------------
.PHONY: size
size: $(BOOT_BIN) $(KERNEL_BIN) $(IMG)
	@echo "Component sizes:"
	@echo "  boot.bin   : $$(wc -c < $(BOOT_BIN)) bytes"
	@echo "  kernel.bin : $$(wc -c < $(KERNEL_BIN)) bytes"
	@echo "  jagos.img  : $$(wc -c < $(IMG)) bytes"

# --- Help --------------------------------------------------------------------
.PHONY: help
help:
	@echo "JagOs Makefile targets:"
	@echo "  make         - Build jagos.img"
	@echo "  make run     - Build and run in QEMU"
	@echo "  make debug   - Run with GDB stub on :1234"
	@echo "  make clean   - Remove build artifacts"
	@echo "  make size    - Show component sizes"
