# JagOs v0.2 - 32-bit Protected Mode Operating System

JagOs is a minimal x86 operating system with a custom bootloader, VGA driver, Turkish Q keyboard support, PIT timer, kernel logging, shell, and script engine.

## Requirements

- NASM
- GCC/G++ (32-bit or i686-elf cross-compiler)
- GNU LD
- QEMU (recommended)
- Make (optional)

## Build

Using Makefile:
make          # build everything
make run      # build and run in QEMU
make clean    # remove build artifacts
make debug    # run with GDB stub

Manual build:
nasm -f bin boot.asm -o boot.bin
gcc -m32 -ffreestanding -nostdlib -O2 -c kernel.c -o kernel.o
g++ -m32 -ffreestanding -nostdlib -fno-rtti -fno-exceptions -fno-threadsafe-statics -O2 -c shell.cpp -o shell.o
ld -m elf_i386 -T linker.ld -o kernel.elf kernel.o shell.o
objcopy -O binary kernel.elf kernel.bin
cat boot.bin kernel.bin > jagos.img

## Run

qemu-system-x86_64 -drive format=raw,file=jagos.img

## Shell Commands

help           - show commands
clear          - clear screen
sysinfo        - system info (shows uptime + layout)
uptime         - show system uptime
nubo [msg]     - NuboGuard (panda)
packet         - PacketX demo
jagu           - run embedded .jagu script
echo <text>    - print text
color <name>   - set color (red/green/blue/cyan/yellow/white)
keymap <tr|us> - switch keyboard layout
calc <expr>    - simple calculator (+ - * /)
beep [freq]    - PC speaker beep (default 1000 Hz)
timer <sec>    - countdown timer
random [max]   - random number (default max 100)
version        - OS version
klogs          - show kernel logs
history        - command history
panic          - trigger kernel panic
reboot         - reboot system
halt           - halt system

## .jagu Script Engine

Supported commands:
print <text>
clear
wait <ms>
color <color>
beep <freq>
uptime
# comment

## New Features in v0.2

- **Turkish Q Keyboard Layout**: Full TR-Q scancode mapping with Shift and AltGr support.
- **PIT Timer**: 100 Hz system timer with uptime tracking.
- **Kernel Log Ring Buffer**: Stores up to 4KB of kernel messages, viewable via `klogs`.
- **PC Speaker Beep**: Programmable beep via `beep` command.
- **Calculator**: `calc 10 + 5` style basic arithmetic.
- **Countdown Timer**: `timer 10` counts down and beeps.
- **Random Number Generator**: Simple LCG-based `random` command.
- **Reboot**: Soft reboot via keyboard controller.
- **Keyboard Layout Switching**: `keymap tr` / `keymap us` on the fly.

## Files

boot.asm     - bootloader
kernel.c     - core kernel (VGA, IDT, PIT timer, keyboard, panic, NuboGuard, PacketX, klog)
shell.cpp    - shell and .jagu engine
linker.ld    - linker script
Makefile     - build automation

## License

MIT License
Author: SushForAhkİ