/*
 * JagOs kernel.c
 * VGA, IDT, Keyboard, ruby_panic, NuboGuard, PacketX
 */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned int       size_t;
typedef signed int         int32_t;

#define NULL ((void*)0)

/* =========================================================
 * VGA
 * ========================================================= */
#define VGA_BASE   ((volatile uint16_t*)0xB8000)
#define VGA_W      80
#define VGA_H      25

enum {
    CL_BLACK=0,CL_BLUE,CL_GREEN,CL_CYAN,CL_RED,CL_MAGENTA,
    CL_BROWN,CL_LGREY,CL_DGREY,CL_LBLUE,CL_LGREEN,CL_LCYAN,
    CL_LRED,CL_LMAGENTA,CL_YELLOW,CL_WHITE
};

static int vga_col=0, vga_row=0;
static uint8_t vga_fg=CL_WHITE, vga_bg=CL_BLACK;

static inline void outb(uint16_t p, uint8_t v){
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static inline uint8_t inb(uint16_t p){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));
    return v;
}

static void vga_cursor(void){
    uint16_t pos=(uint16_t)(vga_row*VGA_W+vga_col);
    outb(0x3D4,14); outb(0x3D5,(uint8_t)(pos>>8));
    outb(0x3D4,15); outb(0x3D5,(uint8_t)(pos&0xFF));
}

void vga_setcolor(uint8_t fg, uint8_t bg){ vga_fg=fg; vga_bg=bg; }

void vga_clear(void){
    uint16_t blank=(uint16_t)(' '|((uint16_t)((vga_bg<<4)|vga_fg)<<8));
    for(int i=0;i<VGA_W*VGA_H;i++) VGA_BASE[i]=blank;
    vga_col=0; vga_row=0; vga_cursor();
}

static void vga_scroll(void){
    for(int r=1;r<VGA_H;r++)
        for(int c=0;c<VGA_W;c++)
            VGA_BASE[(r-1)*VGA_W+c]=VGA_BASE[r*VGA_W+c];
    uint16_t blank=(uint16_t)(' '|((uint16_t)((vga_bg<<4)|vga_fg)<<8));
    for(int c=0;c<VGA_W;c++) VGA_BASE[(VGA_H-1)*VGA_W+c]=blank;
    vga_row=VGA_H-1;
}

void vga_putc(char c){
    if(c=='\n'){ vga_col=0; if(++vga_row>=VGA_H) vga_scroll(); }
    else if(c=='\r'){ vga_col=0; }
    else if(c=='\b'){ if(vga_col>0){ vga_col--;
        VGA_BASE[vga_row*VGA_W+vga_col]=(uint16_t)(' '|((uint16_t)((vga_bg<<4)|vga_fg)<<8)); }}
    else{
        VGA_BASE[vga_row*VGA_W+vga_col]=(uint16_t)((unsigned char)c|((uint16_t)((vga_bg<<4)|vga_fg)<<8));
        if(++vga_col>=VGA_W){ vga_col=0; if(++vga_row>=VGA_H) vga_scroll(); }
    }
    vga_cursor();
}

void vga_puts(const char*s){ while(*s) vga_putc(*s++); }
void vga_putln(const char*s){ vga_puts(s); vga_putc('\n'); }

void vga_putdec(uint32_t v){
    if(!v){vga_putc('0');return;}
    char b[12]; int i=11; b[i]=0;
    while(v){b[--i]='0'+(v%10);v/=10;}
    vga_puts(b+i);
}

/* =========================================================
 * STRING UTILS
 * ========================================================= */
static size_t kstrlen(const char*s){size_t n=0;while(s[n])n++;return n;}
static int kstrcmp(const char*a,const char*b){
    while(*a&&*a==*b){a++;b++;}
    return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
static int kstrncmp(const char*a,const char*b,size_t n){
    while(n--&&*a&&*a==*b){a++;b++;}
    if(n==(size_t)-1)return 0;
    return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
static void kmemset(void*p,uint8_t v,size_t n){
    uint8_t*q=(uint8_t*)p; while(n--)*q++=v;
}

/* =========================================================
 * IDT  (naked stubs — no FPU issues)
 * ========================================================= */
typedef struct __attribute__((packed)){
    uint16_t off_lo, sel;
    uint8_t  zero, flags;
    uint16_t off_hi;
} idt_entry_t;

typedef struct __attribute__((packed)){
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t   idtp;

static void __attribute__((naked)) isr_stub(void){
    __asm__ volatile(
        "pusha\n"
        "movb $0x20,%al\n"
        "outb %al,$0x20\n"
        "popa\n"
        "iret\n"
    );
}

static void __attribute__((naked)) isr_kbd(void){
    __asm__ volatile(
        "pusha\n"
        "inb $0x60,%al\n"
        "movb $0x20,%al\n"
        "outb %al,$0x20\n"
        "popa\n"
        "iret\n"
    );
}

static void idt_gate(uint8_t n, uint32_t h){
    idt[n].off_lo=(uint16_t)(h&0xFFFF);
    idt[n].off_hi=(uint16_t)(h>>16);
    idt[n].sel=0x08; idt[n].zero=0; idt[n].flags=0x8E;
}

static void idt_init(void){
    kmemset(idt,0,sizeof(idt));
    for(int i=0;i<256;i++) idt_gate((uint8_t)i,(uint32_t)isr_stub);
    idt_gate(0x21,(uint32_t)isr_kbd);

    /* Remap PIC */
    outb(0x20,0x11); outb(0xA0,0x11);
    outb(0x21,0x20); outb(0xA1,0x28);
    outb(0x21,0x04); outb(0xA1,0x02);
    outb(0x21,0x01); outb(0xA1,0x01);
    outb(0x21,0xFD); outb(0xA1,0xFF);

    idtp.limit=sizeof(idt)-1;
    idtp.base=(uint32_t)idt;
    __asm__ volatile("lidt %0"::"m"(idtp));
    __asm__ volatile("sti");
}

/* =========================================================
 * KEYBOARD (polling)
 * ========================================================= */
/* =========================================================
 * KEYBOARD LAYOUTS
 * ========================================================= */

/* US QWERTY */
static const char sc_norm[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};
static const char sc_shift[128]={
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

/* Turkish Q Keyboard Layout (single byte extended ASCII for terminal compatibility) */
static const char tr_q_norm[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','*','-','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','i',',',
    0,'\'','z','x','c','v','b','n','m','o','c','.',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};
static const char tr_q_shift[128]={
    0,27,'!','"','#','$','%','^','&','\'','(',')','?','_','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','I',';',
    0,'>','Z','X','C','V','B','N','M','O','C',':',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

/* Turkish Q with ASCII fallbacks for non-UTF8 display */
static const char tr_q_ascii_norm[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','*','-','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','i',',',
    0,'\'','z','x','c','v','b','n','m','o','c','.',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};
static const char tr_q_ascii_shift[128]={
    0,27,'!','"','#','$','%','^','&','\'','(',')','?','_','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','I',';',
    0,'>','Z','X','C','V','B','N','M','O','C',':',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

static int kb_layout=0; /* 0=US, 1=TR-Q */
static int kb_shift=0, kb_caps=0, kb_altgr=0;

void kb_set_layout(int layout){ kb_layout=layout; }
int kb_get_layout(void){ return kb_layout; }

static char kb_nb(void){
    if(!(inb(0x64)&1)) return 0;
    uint8_t sc=inb(0x60);
    /* Shift keys */
    if(sc==0x2A||sc==0x36){kb_shift=1;return 0;}
    if(sc==0xAA||sc==0xB6){kb_shift=0;return 0;}
    /* Caps Lock */
    if(sc==0x3A){kb_caps^=1;return 0;}
    /* AltGr (Right Alt) */
    if(sc==0x38){} /* left alt - ignore for now */
    if(sc==0xE0){} /* extended prefix */
    if(sc&0x80||sc>=128) return 0;
    
    char c=0;
    const char* tbl_norm=sc_norm;
    const char* tbl_shift=sc_shift;
    
    if(kb_layout==1){ /* TR-Q */
        tbl_norm=tr_q_norm;
        tbl_shift=tr_q_shift;
    }
    
    c=kb_shift?tbl_shift[sc]:tbl_norm[sc];
    if(kb_caps && !kb_altgr){
        if(c>='a'&&c<='z') c=(char)(c-32);
        else if(c>='A'&&c<='Z') c=(char)(c+32);
    }
    return c;
}

char kb_getc(void){ char c; do{c=kb_nb();}while(!c); return c; }

void kb_readline(char*buf, int max){
    int i=0;
    while(i<max-1){
        char c=kb_getc();
        if(c=='\n'||c=='\r'){buf[i]=0;vga_putc('\n');return;}
        if(c=='\b'&&i>0){i--;vga_putc('\b');}
        else if(c>=' '&&c<127){buf[i++]=c;vga_putc(c);}
    }
    buf[i]=0; vga_putc('\n');
}

/* =========================================================
 * RUBY PANIC
 * ========================================================= */
static const char*ruby_art[]={
    "    ____        __          ",
    "   / __ \\__  __/ /_  __  __",
    "  / /_/ / / / / __ \\/ / / /",
    " / _, _/ /_/ / /_/ / /_/ / ",
    "/_/ |_|\\__,_/_.___/\\__, /  ",
    "                   /____/  ",
    NULL
};

void __attribute__((noreturn)) ruby_panic(const char*msg){
    __asm__ volatile("cli");
    vga_setcolor(CL_WHITE,CL_RED);
    vga_clear();
    vga_putln("================================================================================");
    vga_putln("                      *** KERNEL PANIC - JagOs ***");
    vga_putln("================================================================================");
    vga_putc('\n');
    vga_setcolor(CL_YELLOW,CL_RED);
    for(int i=0;ruby_art[i];i++) vga_putln(ruby_art[i]);
    vga_putc('\n');
    vga_setcolor(CL_WHITE,CL_RED);
    vga_puts("  PANIC: ");
    vga_setcolor(CL_YELLOW,CL_RED);
    vga_putln(msg);
    vga_putc('\n');
    vga_setcolor(CL_WHITE,CL_RED);
    vga_putln("  System halted. Reboot required.");
    vga_putln("================================================================================");
    while(1) __asm__ volatile("hlt");
}

/* =========================================================
 * NUBO GUARD
 * ========================================================= */
static const char*panda[]={
    "  /\\_____/\\ ",
    " (  o   o  )",
    "  \\  ~~~  / ",
    "   )     (  ",
    "  (_______) ",
    NULL
};

void nubo_guard(const char*msg){
    uint8_t fg=vga_fg, bg=vga_bg;
    vga_setcolor(CL_BLACK,CL_CYAN);
    vga_putln("\n[NUBO GUARD]");
    vga_setcolor(CL_WHITE,CL_BLACK);
    for(int i=0;panda[i];i++) vga_putln(panda[i]);
    vga_setcolor(CL_LRED,CL_BLACK);
    vga_puts("  Caught: "); vga_putln(msg);
    vga_setcolor(CL_LGREY,CL_BLACK);
    vga_putln("  System protected. Continuing.\n");
    vga_setcolor(fg,bg);
}

/* =========================================================
 * PACKETX STUB
 * ========================================================= */
#define PKT_MAGIC 0x4A414700u
#define PKT_DMAX  128

typedef struct {
    uint32_t magic;
    uint16_t src, dst;
    uint8_t  proto, flags;
    uint16_t len;
    uint32_t csum;
    uint8_t  data[PKT_DMAX];
} packet_t;

static uint32_t pkt_csum(const packet_t*p){
    uint32_t s=p->magic;
    for(uint16_t i=0;i<p->len&&i<PKT_DMAX;i++)
        s^=(uint32_t)p->data[i]<<(8*(i&3));
    return s;
}

int packetx_send(const packet_t*p){
    if(!p||p->magic!=PKT_MAGIC) return -1;
    vga_setcolor(CL_LGREEN,CL_BLACK);
    vga_puts("[PacketX] SEND port:"); vga_putdec(p->dst);
    vga_puts(" len:"); vga_putdec(p->len);
    vga_puts(" proto:"); vga_putdec(p->proto);
    vga_putc('\n');
    vga_setcolor(CL_WHITE,CL_BLACK);
    return 0;
}

int packetx_recv(packet_t*out){
    if(!out) return -1;
    out->magic=PKT_MAGIC; out->src=9000; out->dst=8000;
    out->proto=1; out->flags=0x01; out->len=5;
    out->data[0]='H';out->data[1]='E';out->data[2]='L';
    out->data[3]='L';out->data[4]='O';
    out->csum=pkt_csum(out);
    vga_setcolor(CL_LCYAN,CL_BLACK);
    vga_puts("[PacketX] RECV from port:"); vga_putdec(out->src);
    vga_putc('\n');
    vga_setcolor(CL_WHITE,CL_BLACK);
    return 0;
}

void kernel_packet_demo(void){
    packet_t p; kmemset(&p,0,sizeof(p));
    p.magic=PKT_MAGIC; p.src=8000; p.dst=9000;
    p.proto=1; p.len=4;
    p.data[0]='P';p.data[1]='I';p.data[2]='N';p.data[3]='G';
    p.csum=pkt_csum(&p);
    packetx_send(&p);
    packetx_recv(&p);
}

/* =========================================================
 * CMOS / REAL-TIME CLOCK
 * ========================================================= */
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg){
    outb(CMOS_ADDR, reg);
    for(volatile int i=0;i<100;i++) __asm__ volatile("nop");
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t val){ return ((val>>4)*10)+(val&0x0F); }

typedef struct {
    uint8_t second, minute, hour;
    uint8_t day, month;
    uint16_t year;
} datetime_t;

void rtc_read(datetime_t*dt){
    /* Read RTC registers (BCD format) */
    dt->second = bcd_to_bin(cmos_read(0x00));
    dt->minute = bcd_to_bin(cmos_read(0x02));
    dt->hour   = bcd_to_bin(cmos_read(0x04) & 0x7F); /* 24-hour format */
    dt->day    = bcd_to_bin(cmos_read(0x07));
    dt->month  = bcd_to_bin(cmos_read(0x08));
    dt->year   = (uint16_t)(bcd_to_bin(cmos_read(0x09)) + 2000);
}

/* =========================================================
 * SERIAL PORT (COM1 - 0x3F8)
 * ========================================================= */
#define COM1 0x3F8

static int serial_init_done=0;

static void serial_init(void){
    if(serial_init_done) return;
    outb(COM1+1, 0x00); /* Disable interrupts */
    outb(COM1+3, 0x80); /* Enable DLAB */
    outb(COM1+0, 0x03); /* 38400 baud (divisor 3) */
    outb(COM1+1, 0x00);
    outb(COM1+3, 0x03); /* 8 bits, no parity, 1 stop */
    outb(COM1+2, 0xC7); /* FIFO enable */
    outb(COM1+4, 0x0B); /* RTS/DTR */
    serial_init_done=1;
}

static int serial_tx_empty(void){
    return inb(COM1+5) & 0x20;
}

void serial_putc(char c){
    serial_init();
    while(!serial_tx_empty()) __asm__ volatile("pause");
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char*s){
    while(*s) serial_putc(*s++);
}

/* =========================================================
 * SYSTEM CONTROL
 * ========================================================= */
void system_reboot(void){
    vga_setcolor(CL_YELLOW,CL_BLACK);
    vga_putln("\n[SYSTEM] Rebooting...");
    vga_setcolor(CL_WHITE,CL_BLACK);
    /* Try 8042 keyboard controller reboot */
    uint8_t good=0x02;
    while(good&0x02) good=inb(0x64);
    outb(0x64, 0xFE);
    /* Fallback: triple fault */
    __asm__ volatile("cli; lidt %0; int $0x03"::"m"((uint16_t[]){0,0}));
    while(1) __asm__ volatile("hlt");
}

void system_shutdown(void){
    vga_setcolor(CL_YELLOW,CL_BLACK);
    vga_putln("\n[SYSTEM] Shutdown requested.");
    vga_putln("  (Use QEMU monitor to quit: Ctrl+A then X)");
    vga_setcolor(CL_WHITE,CL_BLACK);
    __asm__ volatile("cli; hlt");
    while(1) __asm__ volatile("hlt");
}

/* =========================================================
 * MEMORY INFO (Simple heap tracker)
 * ========================================================= */
#define HEAP_START 0x100000  /* 1MB */
#define HEAP_SIZE  0x800000  /* 8MB heap */

static uint8_t* heap_ptr=(uint8_t*)HEAP_START;
static size_t heap_used=0;

void* kmalloc(size_t n){
    if(heap_used+n>HEAP_SIZE) return NULL;
    void* p=heap_ptr;
    heap_ptr+=n;
    heap_used+=n;
    return p;
}

void kfree(void*p){ (void)p; /* simple bump allocator, no free */ }

void meminfo(size_t* total, size_t* used, size_t* free){
    if(total) *total=HEAP_SIZE;
    if(used) *used=heap_used;
    if(free) *free=HEAP_SIZE-heap_used;
}

/* =========================================================
 * KERNEL API (used by shell.cpp)
 * ========================================================= */
void kernel_print(const char*s)              { vga_puts(s); }
void kernel_println(const char*s)            { vga_putln(s); }
void kernel_putchar(char c)                  { vga_putc(c); }
void kernel_clear(void)                      { vga_clear(); }
void kernel_setcolor(uint8_t fg,uint8_t bg)  { vga_setcolor(fg,bg); }
char kernel_getchar(void)                    { return kb_getc(); }
void kernel_readline(char*b,int n)           { kb_readline(b,n); }
void kernel_panic(const char*m)              { ruby_panic(m); }
void kernel_nubo(const char*m)               { nubo_guard(m); }
int  kernel_strcmp(const char*a,const char*b){ return kstrcmp(a,b); }
int  kernel_strncmp(const char*a,const char*b,size_t n){ return kstrncmp(a,b,n); }
size_t kernel_strlen(const char*s)           { return kstrlen(s); }

/* New API exports for v0.2 */
void kernel_kb_set_layout(int layout)        { kb_set_layout(layout); }
int  kernel_kb_get_layout(void)               { return kb_get_layout(); }
void kernel_rtc_read(datetime_t*dt)           { rtc_read(dt); }
void kernel_serial_puts(const char*s)        { serial_puts(s); }
void kernel_reboot(void)                     { system_reboot(); }
void kernel_shutdown(void)                   { system_shutdown(); }
void kernel_meminfo(size_t*t,size_t*u,size_t*f){ meminfo(t,u,f); }
void* kernel_kmalloc(size_t n)               { return kmalloc(n); }
void kernel_kfree(void*p)                    { kfree(p); }

/* C++ stubs */
void __cxa_pure_virtual(void){ ruby_panic("pure virtual"); }
int  __cxa_atexit(void(*f)(void*),void*a,void*d){(void)f;(void)a;(void)d;return 0;}
void*__dso_handle=NULL;

/* =========================================================
 * KERNEL MAIN
 * ========================================================= */
extern void shell_main(void);

__attribute__((section(".text.boot")))
void kernel_main(void){
    vga_setcolor(CL_WHITE,CL_BLACK);
    vga_clear();
    idt_init();

    /* Banner */
    vga_setcolor(CL_LGREEN,CL_BLACK);
    vga_putln("================================================================================");
    vga_putln("       _____   ____      ____    _____");
    vga_putln("      |_   _| |  _ \\    / __ \\  / ____|");
    vga_putln("        | |   | |_) |  | |  | || (___");
    vga_putln("        | |   |  _ <   | |  | | \\___ \\ ");
    vga_putln("       _| |_  | |_) |  | |__| | ____) |");
    vga_putln("      |_____| |____/    \\____/ |_____/   JagOs v0.2");
    vga_putln("================================================================================");
    vga_setcolor(CL_WHITE,CL_BLACK);
    vga_putln("  Arch: x86 32-bit PM | VGA: 80x25 | KBD: PS/2 (US/TR-Q) | COM1: 38400");
    vga_putln("  FS: RAM Disk (16 files) | RTC: CMOS | Shell: v2.0");
    vga_setcolor(CL_LGREEN,CL_BLACK);
    vga_putln("  [IDT OK] [VGA OK] [KBD OK] [RTC OK] [SERIAL OK]");
    vga_setcolor(CL_WHITE,CL_BLACK);
    vga_putln("================================================================================\n");

    shell_main();
    ruby_panic("kernel_main returned");
}
