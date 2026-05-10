/*
 * =============================================================================
 * JagOs - kernel.c
 * Core Kernel: VGA Driver, IDT, Keyboard, ruby_panic, NuboGuard, PacketX
 * Compiled with: gcc -m32 -ffreestanding -nostdlib -O2 -c kernel.c -o kernel.o
 * =============================================================================
 */

/* ============================================================================
 * TYPE DEFINITIONS (no libc)
 * ============================================================================ */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef unsigned int       size_t;

#define NULL ((void*)0)
#define true  1
#define false 0

/* ============================================================================
 * VGA DRIVER
 * ============================================================================ */
#define VGA_BASE        ((uint16_t*)0xB8000)
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

/* VGA color codes */
typedef enum {
    VGA_BLACK=0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_LIGHT_BROWN, VGA_WHITE
} vga_color_t;

static int vga_col = 0;
static int vga_row = 0;
static uint8_t vga_fg = VGA_WHITE;
static uint8_t vga_bg = VGA_BLACK;

static inline uint16_t vga_entry(char c, uint8_t fg, uint8_t bg) {
    return (uint16_t)(unsigned char)c | ((uint16_t)((bg << 4) | (fg & 0x0F)) << 8);
}

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Update hardware cursor position */
static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 14); outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_fg = fg;
    vga_bg = bg;
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_fg, vga_bg);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BASE[i] = blank;
    vga_col = 0; vga_row = 0;
    vga_update_cursor();
}

static void vga_scroll(void) {
    for (int r = 1; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA_BASE[(r-1)*VGA_WIDTH + c] = VGA_BASE[r*VGA_WIDTH + c];
    uint16_t blank = vga_entry(' ', vga_fg, vga_bg);
    for (int c = 0; c < VGA_WIDTH; c++)
        VGA_BASE[(VGA_HEIGHT-1)*VGA_WIDTH + c] = blank;
    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) vga_scroll();
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_BASE[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_fg, vga_bg);
        }
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            if (++vga_row >= VGA_HEIGHT) vga_scroll();
        }
    } else {
        VGA_BASE[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_fg, vga_bg);
        if (++vga_col >= VGA_WIDTH) {
            vga_col = 0;
            if (++vga_row >= VGA_HEIGHT) vga_scroll();
        }
    }
    vga_update_cursor();
}

void vga_print(const char* s) {
    while (*s) vga_putchar(*s++);
}

void vga_println(const char* s) {
    vga_print(s);
    vga_putchar('\n');
}

/* Print hex number */
void vga_print_hex(uint32_t val) {
    const char* hex = "0123456789ABCDEF";
    char buf[11];
    buf[0]='0'; buf[1]='x';
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
    vga_print(buf);
}

/* Print decimal number */
void vga_print_dec(uint32_t val) {
    if (val == 0) { vga_putchar('0'); return; }
    char buf[12]; int i = 11;
    buf[i] = '\0';
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_print(buf + i);
}

/* ============================================================================
 * STRING UTILITIES (no libc)
 * ============================================================================ */
static size_t k_strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int k_strncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static char* k_strcpy(char* dst, const char* src) {
    char* d = dst; while ((*d++ = *src++)); return dst;
}
static void k_memset(void* ptr, uint8_t val, size_t n) {
    uint8_t* p = (uint8_t*)ptr; while (n--) *p++ = val;
}

/* ============================================================================
 * IDT - INTERRUPT DESCRIPTOR TABLE
 * ============================================================================ */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;    /* 0x8E = interrupt gate, ring 0, present */
    uint16_t offset_high;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

#define IDT_SIZE 256
static idt_entry_t idt[IDT_SIZE];
static idt_ptr_t   idt_ptr;

static void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
}

/* Generic ISR stub - we just EOI and return */
static void __attribute__((interrupt)) isr_default(void* frame) {
    (void)frame;
    outb(0x20, 0x20); /* EOI to PIC */
}

/* Keyboard ISR stub - handled by polling, but we need to ACK */
static void __attribute__((interrupt)) isr_keyboard(void* frame) {
    (void)frame;
    /* Read scancode to clear buffer */
    inb(0x60);
    outb(0x20, 0x20);
}

/* Timer ISR forward declaration (defined later in PIT section) */
static void __attribute__((interrupt)) isr_timer(void* frame);

void idt_init(void) {
    k_memset(idt, 0, sizeof(idt));

    /* Set all gates to default handler */
    for (int i = 0; i < IDT_SIZE; i++)
        idt_set_gate(i, (uint32_t)isr_default, 0x08, 0x8E);

    /* IRQ0 = timer, IRQ1 = keyboard */
    idt_set_gate(0x20, (uint32_t)isr_timer, 0x08, 0x8E);
    idt_set_gate(0x21, (uint32_t)isr_keyboard, 0x08, 0x8E);

    /* Initialize PIC (8259A) - remap IRQs 0-7 to INT 0x20-0x27 */
    outb(0x20, 0x11); outb(0xA0, 0x11);    /* Init command */
    outb(0x21, 0x20); outb(0xA1, 0x28);    /* Vector offsets */
    outb(0x21, 0x04); outb(0xA1, 0x02);    /* Cascade */
    outb(0x21, 0x01); outb(0xA1, 0x01);    /* 8086 mode */
    outb(0x21, 0xFC); outb(0xA1, 0xFF);    /* Mask: timer+keyboard enabled */

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)idt;
    __asm__ volatile("lidt %0" :: "m"(idt_ptr));
    __asm__ volatile("sti");
}

/* ============================================================================
 * PIT TIMER + SYSTEM TICKS
 * ============================================================================ */
static volatile uint32_t system_ticks = 0;
static volatile uint32_t uptime_sec   = 0;
static volatile uint32_t uptime_min   = 0;
static volatile uint32_t uptime_hour  = 0;

static void __attribute__((interrupt)) isr_timer(void* frame) {
    (void)frame;
    system_ticks++;
    if (system_ticks % 100 == 0) {
        uptime_sec++;
        if (uptime_sec >= 60) { uptime_sec = 0; uptime_min++; }
        if (uptime_min >= 60) { uptime_min = 0; uptime_hour++; }
    }
    outb(0x20, 0x20);
}

static void pit_init(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

/* ============================================================================
 * KEYBOARD DRIVER (Polling mode)
 * ============================================================================ */

/* US QWERTY scancode → ASCII (set 1) */
static const char scancode_ascii[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0
};
static const char scancode_shift[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0
};

/* Turkish Q layout scancode tables (CP437 where available) */
static const char scancode_tr_ascii[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','*','-','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','g','u','\n',
    0,   'a','s','d','f','g','h','j','k','l','s','i',
    0,   ',','z','x','c','v','b','n','m','o','c','.',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0
};
static const char scancode_tr_shift[128] = {
    0,   27, '!','\"','^','+','%','&','/','(','=','?','_','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','G',(char)0x9A,'\n',
    0,   'A','S','D','F','G','H','J','K','L','S','I',
    0,   ';','Z','X','C','V','B','N','M',(char)0x99,(char)0x80,':',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0
};
static const char scancode_tr_altgr[128] = {
    0,   27, '>',(char)0x9C,'#','$','1','{','[',']','}','\\','|','\b',
    '\t','@', 0, 0, 0, 0, 0, 0, 0, 0, 0,(char)0x22,(char)0x7E,'\n',
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0,(char)0x27,(char)0x60,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0
};

static int kb_shift = 0;
static int kb_caps  = 0;
static int kb_layout = 0; /* 0 = US, 1 = TR */
static int kb_altgr  = 0;
static int kb_e0_prefix = 0;

/* Non-blocking: returns 0 if no key, else ASCII char */
char kb_getchar_nb(void) {
    if (!(inb(0x64) & 1)) return 0;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { kb_e0_prefix = 1; return 0; }

    if (kb_e0_prefix) {
        kb_e0_prefix = 0;
        if (sc == 0x38) { kb_altgr = 1; return 0; }
        if (sc == 0xB8) { kb_altgr = 0; return 0; }
        return 0;
    }

    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { kb_shift = 0; return 0; }
    if (sc == 0x3A) { kb_caps ^= 1; return 0; }
    if (sc == 0x38 || sc == 0xB8) return 0;
    if (sc & 0x80) return 0;
    if (sc >= 128) return 0;

    char c;
    if (kb_layout == 1) {
        if (kb_altgr) c = scancode_tr_altgr[sc];
        else c = kb_shift ? scancode_tr_shift[sc] : scancode_tr_ascii[sc];
    } else {
        c = kb_shift ? scancode_shift[sc] : scancode_ascii[sc];
    }

    if (!c) return 0;

    if (kb_caps) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    return c;
}

/* Blocking read: waits for a character */
char kb_getchar(void) {
    char c;
    do { c = kb_getchar_nb(); } while (!c);
    return c;
}

/* Read a line into buf (max len-1 chars), echo to screen */
void kb_readline(char* buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        char c = kb_getchar();
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            vga_putchar('\n');
            return;
        } else if (c == '\b') {
            if (i > 0) { i--; vga_putchar('\b'); }
        } else if ((unsigned char)c >= 32 && (unsigned char)c != 127) {
            buf[i++] = c;
            vga_putchar(c);
        }
    }
    buf[i] = '\0';
    vga_putchar('\n');
}

/* ============================================================================
 * KERNEL LOG RING BUFFER
 * ============================================================================ */
#define KLOG_SIZE 4096
static char klog_buffer[KLOG_SIZE];
static volatile uint32_t klog_write_pos = 0;
static volatile uint32_t klog_read_pos  = 0;

static void klog_put(const char* msg) {
    while (*msg) {
        klog_buffer[klog_write_pos % KLOG_SIZE] = *msg++;
        klog_write_pos++;
    }
    if (klog_write_pos - klog_read_pos > KLOG_SIZE) {
        klog_read_pos = klog_write_pos - KLOG_SIZE;
    }
}

static void klog_dump(void) {
    uint32_t start = klog_read_pos;
    uint32_t end   = klog_write_pos;
    for (uint32_t i = start; i < end; i++) {
        vga_putchar(klog_buffer[i % KLOG_SIZE]);
    }
}

/* ============================================================================
 * RUBY PANIC SYSTEM
 * ============================================================================ */

static const char* ruby_art[] = {
    "    ____        __          ",
    "   / __ \\__  __/ /_  __  __",
    "  / /_/ / / / / __ \\/ / / /",
    " / _, _/ /_/ / /_/ / /_/ / ",
    "/_/ |_|\\__,_/_.___/\\__, /  ",
    "                   /____/   ",
    NULL
};

void __attribute__((noreturn)) ruby_panic(const char* msg) {
    __asm__ volatile("cli");

    /* Red screen */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();

    /* Header bar */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_println("================================================================================");
    vga_println("                        *** KERNEL PANIC - JagOs ***                           ");
    vga_println("================================================================================");
    vga_putchar('\n');

    /* Ruby ASCII art */
    vga_set_color(VGA_LIGHT_BROWN, VGA_RED);
    for (int i = 0; ruby_art[i]; i++)
        vga_println(ruby_art[i]);

    vga_putchar('\n');
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_print("  PANIC: ");
    vga_set_color(VGA_LIGHT_BROWN, VGA_RED);
    vga_println(msg);
    vga_putchar('\n');
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_println("  System halted. Please reboot.");
    vga_println("================================================================================");

    /* Halt forever */
    while (1) __asm__ volatile("hlt");
}

/* ============================================================================
 * NUBO GUARD
 * ============================================================================ */

static const char* panda_art[] = {
    "  /\\_____/\\  ",
    " (  o   o  ) ",
    "  \\  ~~~  /  ",
    "   )     (   ",
    "  (_______) ",
    NULL
};

void nubo_guard(const char* error_msg) {
    /* Save colors */
    uint8_t old_fg = vga_fg, old_bg = vga_bg;

    vga_set_color(VGA_BLACK, VGA_CYAN);
    vga_println("\n[NUBO GUARD ACTIVATED]");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    for (int i = 0; panda_art[i]; i++)
        vga_println(panda_art[i]);

    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_print("  Nubo caught: ");
    vga_println(error_msg);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_println("  System protected. Continuing safely.\n");

    /* Restore colors */
    vga_set_color(old_fg, old_bg);
}

/* ============================================================================
 * PACKETX STUB
 * ============================================================================ */
#define PACKET_MAX_DATA 128
#define PACKET_MAGIC    0x4A414700  /* 'JAG\0' */

typedef struct {
    uint32_t magic;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;     /* 0=raw, 1=cmd, 2=data */
    uint8_t  flags;        /* bit0=ACK, bit1=ERR, bit2=FIN */
    uint16_t length;
    uint32_t checksum;
    uint8_t  data[PACKET_MAX_DATA];
} packet_t;

/* Simple XOR checksum */
static uint32_t packet_checksum(const packet_t* p) {
    uint32_t sum = 0;
    const uint8_t* b = (const uint8_t*)p->data;
    for (uint16_t i = 0; i < p->length && i < PACKET_MAX_DATA; i++)
        sum ^= (uint32_t)b[i] << (8 * (i & 3));
    return sum ^ p->magic;
}

/* "Send" a packet (stub - prints to screen) */
int packetx_send(const packet_t* p) {
    if (!p || p->magic != PACKET_MAGIC) return -1;

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[PacketX] SEND -> port:");
    vga_print_dec(p->dst_port);
    vga_print(" len:");
    vga_print_dec(p->length);
    vga_print(" proto:");
    vga_print_dec(p->protocol);
    vga_putchar('\n');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    return 0;
}

/* "Receive" a packet (stub - returns fake echo) */
int packetx_recv(packet_t* out) {
    if (!out) return -1;

    /* Fake loopback response */
    out->magic    = PACKET_MAGIC;
    out->src_port = 9000;
    out->dst_port = 8000;
    out->protocol = 1;
    out->flags    = 0x01;  /* ACK */
    out->length   = 5;
    out->data[0]  = 'H';
    out->data[1]  = 'E';
    out->data[2]  = 'L';
    out->data[3]  = 'L';
    out->data[4]  = 'O';
    out->checksum = packet_checksum(out);

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[PacketX] RECV <- port:");
    vga_print_dec(out->src_port);
    vga_print(" flags:");
    vga_print_hex(out->flags);
    vga_putchar('\n');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    return 0;
}

/* ============================================================================
 * EXPORTED KERNEL API (called from shell.cpp)
 * ============================================================================ */

/* These are the symbols shell.cpp will link against */
void kernel_print(const char* s)               { vga_print(s); }
void kernel_println(const char* s)             { vga_println(s); }
void kernel_putchar(char c)                    { vga_putchar(c); }
void kernel_clear(void)                        { vga_clear(); }
void kernel_set_color(uint8_t fg, uint8_t bg)  { vga_set_color(fg, bg); }
char kernel_getchar(void)                      { return kb_getchar(); }
void kernel_readline(char* buf, int len)       { kb_readline(buf, len); }
void kernel_panic(const char* msg)             { ruby_panic(msg); }
void kernel_nubo(const char* msg)              { nubo_guard(msg); }
int  kernel_strcmp(const char* a, const char* b)  { return k_strcmp(a, b); }
int  kernel_strncmp(const char* a, const char* b, size_t n) { return k_strncmp(a, b, n); }
size_t kernel_strlen(const char* s)            { return k_strlen(s); }

void kernel_packet_demo(void) {
    packet_t pkt;
    k_memset(&pkt, 0, sizeof(pkt));
    pkt.magic    = PACKET_MAGIC;
    pkt.src_port = 8000;
    pkt.dst_port = 9000;
    pkt.protocol = 1;
    pkt.length   = 4;
    pkt.data[0]  = 'P'; pkt.data[1]='I';
    pkt.data[2]  = 'N'; pkt.data[3]='G';
    pkt.checksum = packet_checksum(&pkt);
    packetx_send(&pkt);
    packetx_recv(&pkt);
}

uint32_t kernel_get_ticks(void)                { return system_ticks; }
void kernel_get_uptime(uint32_t* h, uint32_t* m, uint32_t* s) {
    *h = uptime_hour; *m = uptime_min; *s = uptime_sec;
}
void kernel_set_layout(int layout)             { kb_layout = layout; }
int  kernel_get_layout(void)                   { return kb_layout; }
void kernel_klog(const char* msg)              { klog_put(msg); }
void kernel_klog_dump(void)                    { klog_dump(); }
void kernel_reboot(void) {
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    while (1) __asm__ volatile("hlt");
}
void kernel_beep(uint32_t freq_hz, uint32_t ms) {
    if (freq_hz == 0) return;
    uint32_t div = 1193180 / freq_hz;
    outb(0x43, 0xB6);
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 3);
    for (volatile uint32_t i = 0; i < ms * 3000; i++) __asm__ volatile("nop");
    outb(0x61, tmp & ~3);
}
static uint32_t rng_seed = 12345;
uint32_t kernel_rand(void) {
    rng_seed = rng_seed * 1103515245 + 12345;
    return rng_seed;
}
void kernel_srand(uint32_t seed) { rng_seed = seed; }

/* ============================================================================
 * KERNEL ENTRY POINT
 * ============================================================================ */

/* Forward declaration - shell is C++ */
extern void shell_main(void);

__attribute__((section(".text.kernel_main")))
void kernel_main(void) {
    /* Zero BSS */
    extern uint32_t __bss_start[];
    extern uint32_t __bss_end[];
    uint32_t* p = __bss_start;
    while (p < __bss_end) *p++ = 0;

    /* Note: C++ global constructors intentionally skipped.
     * All static data is zero-initialized; no .init_array is needed. */
    /* Initialize VGA */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    /* Initialize IDT + PIC */
    idt_init();

    /* Start PIT timer at ~100 Hz */
    pit_init(100);

    /* Seed RNG from uptime ticks */
    rng_seed = system_ticks + 12345;

    klog_put("JagOs v0.2 boot sequence started\n");

    /* Boot banner */
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("================================================================================");
    vga_println("           ___                ____           ____  _____                        ");
    vga_println("          |_  |              / __ \\         / __ \\/ ___/                       ");
    vga_println("           / / __ _  __ _  / / / /__  ____/ / / /\\__ \\                        ");
    vga_println("          / / / _` |/ _` |/ /_/ / __|/___/ /_/ /___/ /                        ");
    vga_println("         /_/  \\__,_|\\__, /\\____/\\__)    \\____//____/                         ");
    vga_println("                   /____/                                                       ");
    vga_println("================================================================================");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("  JagOs v0.2 - Booted Successfully!");
    vga_print  ("  IDT: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("OK");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print  ("  VGA: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("OK");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print  ("  KBD: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("POLLING + TURKISH-Q");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print  ("  TIMER: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("100HZ PIT");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("================================================================================");
    vga_putchar('\n');

    /* Hand off to shell */
    shell_main();

    /* Should never reach here */
    ruby_panic("kernel_main returned unexpectedly");
}

/* C++ needs these; provide minimal stubs */
void __cxa_pure_virtual(void) { ruby_panic("pure virtual call"); }
int  __cxa_atexit(void(*f)(void*), void* a, void* d) { (void)f;(void)a;(void)d; return 0; }
void* __dso_handle = NULL;
