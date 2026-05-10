ASM  := nasm
CC   := gcc
CXX  := g++
LD   := ld

CFLAGS   := -m32 -ffreestanding -nostdlib -nostdinc \
            -fno-stack-protector -fno-pic -fno-pie \
            -fno-builtin -O0 -Wall -std=c11

CXXFLAGS := -m32 -ffreestanding -nostdlib -nostdinc \
            -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
            -fno-rtti -fno-exceptions -fno-threadsafe-statics \
            -fno-use-cxa-atexit -O0 -Wall -std=c++14

LDFLAGS  := -m elf_i386 -T linker.ld --oformat binary -nostdlib

all: jagos.img
	@echo ""
	@echo "  Build complete! Run: make run"
	@echo ""

boot.bin: boot.asm
	$(ASM) -f bin $< -o $@
	@python3 -c "d=open('$@','rb').read();\
	assert len(d)==512,'boot must be 512 bytes';\
	assert d[510]==0x55 and d[511]==0xAA,'bad signature';\
	print('[boot.bin] 512 bytes, AA55 OK')"

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

shell.o: shell.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

kernel.bin: kernel.o shell.o linker.ld
	$(LD) $(LDFLAGS) kernel.o shell.o -o $@
	@SZ=$$(wc -c < $@); SECS=$$(( ($$SZ+511)/512 ));\
	echo "[kernel.bin] $$SZ bytes ($$SECS sectors)";\
	if [ $$SECS -gt 48 ]; then echo "WARNING: kernel > 48 sectors, increase KERNEL_SECTORS in boot.asm"; fi

jagos.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > $@
	@python3 -c "\
import os; f=open('$@','ab');\
sz=os.path.getsize('$@');\
pad=1474560-sz;\
f.write(b'\\x00'*pad) if pad>0 else None;\
f.close();\
print('[jagos.img]',os.path.getsize('$@'),'bytes - ready!')"

run: jagos.img
	qemu-system-x86_64 \
	  -drive format=raw,file=jagos.img,index=0,media=disk \
	  -m 32M -display sdl -no-reboot

clean:
	rm -f boot.bin kernel.o shell.o kernel.bin jagos.img
	@echo "Cleaned."
