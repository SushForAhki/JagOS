/*
 * =============================================================================
 * JagOs - shell.cpp
 * Interactive Shell + .jagu Script Engine
 * Compiled with: g++ -m32 -ffreestanding -nostdlib -fno-rtti -fno-exceptions
 *                    -fno-threadsafe-statics -O2 -c shell.cpp -o shell.o
 * =============================================================================
 */

/* ============================================================================
 * KERNEL API IMPORTS
 * ============================================================================ */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned int       size_t;

#define NULL ((void*)0)

extern "C" {
    void   kernel_print(const char* s);
    void   kernel_println(const char* s);
    void   kernel_putchar(char c);
    void   kernel_clear(void);
    void   kernel_set_color(uint8_t fg, uint8_t bg);
    char   kernel_getchar(void);
    void   kernel_readline(char* buf, int len);
    void   kernel_panic(const char* msg);
    void   kernel_nubo(const char* msg);
    int    kernel_strcmp(const char* a, const char* b);
    int    kernel_strncmp(const char* a, const char* b, size_t n);
    size_t kernel_strlen(const char* s);
    void   kernel_packet_demo(void);
    uint32_t kernel_get_ticks(void);
    void   kernel_get_uptime(uint32_t* h, uint32_t* m, uint32_t* s);
    void   kernel_set_layout(int layout);
    int    kernel_get_layout(void);
    void   kernel_klog(const char* msg);
    void   kernel_klog_dump(void);
    void   kernel_reboot(void);
    void   kernel_beep(uint32_t freq, uint32_t ms);
    uint32_t kernel_rand(void);
    void   kernel_srand(uint32_t seed);
}

/* VGA colors (must match kernel.c) */
enum Color : uint8_t {
    BLACK=0, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHT_GREY,
    DARK_GREY, LIGHT_BLUE, LIGHT_GREEN, LIGHT_CYAN,
    LIGHT_RED, LIGHT_MAGENTA, YELLOW, WHITE
};

/* ============================================================================
 * SHELL UTILITIES
 * ============================================================================ */
#define SHELL_BUF 256
#define MAX_ARGS  16
#define HISTORY_SIZE 8

/* Minimal busy-wait delay (approximate, platform-dependent) */
static void delay_ms(uint32_t ms) {
    /* Each outer iteration ≈ ~1ms at typical QEMU speed */
    for (uint32_t i = 0; i < ms; i++)
        for (volatile uint32_t j = 0; j < 10000; j++)
            __asm__ volatile("nop");
}

static size_t sh_strlen(const char* s) { return kernel_strlen(s); }
static int sh_strcmp(const char* a, const char* b) { return kernel_strcmp(a, b); }
static int sh_strncmp(const char* a, const char* b, size_t n) { return kernel_strncmp(a, b, n); }

static void sh_strcpy(char* dst, const char* src) {
    while ((*dst++ = *src++));
}

static void sh_strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int sh_isspace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Split a command string into argv, return argc */
static int sh_split(char* cmd, char* argv[], int maxargs) {
    int argc = 0;
    char* p = cmd;
    while (*p && argc < maxargs) {
        while (*p && sh_isspace(*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !sh_isspace(*p)) p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static void sh_print_int(int32_t val) {
    if (val < 0) { kernel_putchar('-'); val = -val; }
    if (val == 0) { kernel_putchar('0'); return; }
    char buf[12]; int i = 11;
    buf[i] = '\0';
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    kernel_print(buf + i);
}

/* ============================================================================
 * COMMAND HISTORY
 * ============================================================================ */
static char history[HISTORY_SIZE][SHELL_BUF];
static int  history_count = 0;

static void history_add(const char* cmd) {
    if (!cmd[0]) return;
    /* Don't add duplicate of last */
    if (history_count > 0 &&
        sh_strcmp(history[(history_count-1) % HISTORY_SIZE], cmd) == 0) return;
    sh_strcpy(history[history_count % HISTORY_SIZE], cmd);
    history_count++;
}

/* ============================================================================
 * .JAGU SCRIPT ENGINE
 * ============================================================================ */
#define JAGU_MAX_LINES 64
#define JAGU_LINE_LEN  128

/*
 * Supported statements:
 *   print <text>    - print text followed by newline
 *   clear           - clear screen
 *   wait <ms>       - busy wait milliseconds
 *   # ...           - comment (ignored)
 *   (empty line)    - ignored
 */

static void jagu_trim(char* s) {
    /* Trim trailing whitespace */
    int len = (int)sh_strlen(s);
    while (len > 0 && sh_isspace(s[len-1])) s[--len] = '\0';
    /* Trim leading whitespace in-place by returning pointer (we copy instead) */
}

static void jagu_exec_line(const char* line) {
    /* Skip leading spaces */
    while (*line && sh_isspace(*line)) line++;

    /* Comment or empty */
    if (!*line || *line == '#') return;

    /* print <text> */
    if (sh_strncmp(line, "print", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
        const char* text = line + 5;
        while (*text && sh_isspace(*text)) text++;
        kernel_println(text);
        return;
    }

    /* clear */
    if (sh_strcmp(line, "clear") == 0) {
        kernel_clear();
        return;
    }

    /* wait <ms> */
    if (sh_strncmp(line, "wait", 4) == 0 && (line[4] == ' ' || line[4] == '\t')) {
        const char* numstr = line + 4;
        while (*numstr && sh_isspace(*numstr)) numstr++;
        uint32_t ms = 0;
        while (*numstr >= '0' && *numstr <= '9')
            ms = ms * 10 + (uint32_t)(*numstr++ - '0');
        delay_ms(ms);
        return;
    }

    /* color <fg> */
    if (sh_strncmp(line, "color", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
        const char* arg = line + 5;
        while (*arg && sh_isspace(*arg)) arg++;
        if (sh_strcmp(arg, "red")   == 0) kernel_set_color(LIGHT_RED, BLACK);
        else if (sh_strcmp(arg, "green") == 0) kernel_set_color(LIGHT_GREEN, BLACK);
        else if (sh_strcmp(arg, "blue")  == 0) kernel_set_color(LIGHT_BLUE, BLACK);
        else if (sh_strcmp(arg, "white") == 0) kernel_set_color(WHITE, BLACK);
        else if (sh_strcmp(arg, "yellow")== 0) kernel_set_color(YELLOW, BLACK);
        else if (sh_strcmp(arg, "cyan")  == 0) kernel_set_color(LIGHT_CYAN, BLACK);
        else kernel_set_color(WHITE, BLACK);
        return;
    }

    /* beep <freq> */
    if (sh_strncmp(line, "beep", 4) == 0 && (line[4] == ' ' || line[4] == '\t')) {
        const char* numstr = line + 4;
        while (*numstr && sh_isspace(*numstr)) numstr++;
        uint32_t freq = 1000;
        if (*numstr) {
            freq = 0;
            while (*numstr >= '0' && *numstr <= '9')
                freq = freq * 10 + (uint32_t)(*numstr++ - '0');
        }
        kernel_beep(freq, 200);
        return;
    }

    /* uptime */
    if (sh_strcmp(line, "uptime") == 0) {
        uint32_t h, m, s;
        kernel_get_uptime(&h, &m, &s);
        kernel_print("Uptime: ");
        sh_print_int((int)h); kernel_print("h ");
        sh_print_int((int)m); kernel_print("m ");
        sh_print_int((int)s); kernel_println("s");
        return;
    }

    /* Unknown statement */
    kernel_set_color(LIGHT_RED, BLACK);
    kernel_print("[jagu] Unknown: ");
    kernel_println(line);
    kernel_set_color(WHITE, BLACK);
}

/*
 * Run a .jagu script embedded as a string (newline-separated).
 * In a real OS you'd read from a filesystem; here we run inline scripts.
 */
static void jagu_run_string(const char* script) {
    char line[JAGU_LINE_LEN];
    int li = 0;
    const char* p = script;

    while (*p) {
        if (*p == '\n' || *p == '\r') {
            line[li] = '\0';
            jagu_trim(line);
            jagu_exec_line(line);
            li = 0;
        } else if (li < JAGU_LINE_LEN - 1) {
            line[li++] = *p;
        }
        p++;
    }
    if (li > 0) {
        line[li] = '\0';
        jagu_trim(line);
        jagu_exec_line(line);
    }
}

/* ============================================================================
 * BUILT-IN COMMANDS
 * ============================================================================ */

static void cmd_help(void) {
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("JagOs Shell Commands:");
    kernel_println("---------------------");
    kernel_set_color(WHITE, BLACK);
    kernel_println("  help           - Show this help");
    kernel_println("  clear          - Clear the screen");
    kernel_println("  sysinfo        - Show system information");
    kernel_println("  uptime         - Show system uptime");
    kernel_println("  nubo [msg]     - Summon NuboGuard");
    kernel_println("  packet         - PacketX demo");
    kernel_println("  jagu           - Run a .jagu script demo");
    kernel_println("  echo <text>    - Echo text to screen");
    kernel_println("  color <name>   - Set text color");
    kernel_println("  keymap <tr|us> - Set keyboard layout");
    kernel_println("  calc <expr>    - Simple calculator (+ - * /)");
    kernel_println("  beep [freq]    - PC speaker beep");
    kernel_println("  timer <sec>    - Countdown timer");
    kernel_println("  random [max]   - Random number");
    kernel_println("  version        - OS version");
    kernel_println("  klogs          - Show kernel logs");
    kernel_println("  history        - Command history");
    kernel_println("  panic          - Trigger kernel panic (test)");
    kernel_println("  reboot         - Reboot the system");
    kernel_println("  halt           - Halt the system");
}

static void cmd_clear(void) {
    kernel_set_color(WHITE, BLACK);
    kernel_clear();
}

static void cmd_sysinfo(void) {
    uint32_t h, m, s;
    kernel_get_uptime(&h, &m, &s);
    kernel_set_color(LIGHT_GREEN, BLACK);
    kernel_println("=== JagOs System Information ===");
    kernel_set_color(WHITE, BLACK);
    kernel_println("  OS:          JagOs v0.2");
    kernel_println("  Arch:        x86 32-bit Protected Mode");
    kernel_println("  Boot:        BIOS + Custom Bootloader");
    kernel_println("  Memory:      640KB conventional (no MM yet)");
    kernel_println("  Video:       VGA text mode 80x25");
    kernel_print  ("  Keyboard:    PS/2 polling (");
    kernel_print  (kernel_get_layout() ? "Turkish Q" : "US QWERTY");
    kernel_println(")");
    kernel_println("  IDT:         256 entries, PIC remapped");
    kernel_println("  Shell:       JagOs Shell v2.0");
    kernel_println("  Scripting:   .jagu script engine");
    kernel_println("  Subsystems:  NuboGuard, PacketX stub, PIT Timer");
    kernel_print  ("  Uptime:      ");
    sh_print_int((int)h); kernel_print("h ");
    sh_print_int((int)m); kernel_print("m ");
    sh_print_int((int)s); kernel_println("s");
    kernel_set_color(LIGHT_GREEN, BLACK);
    kernel_println("================================");
    kernel_set_color(WHITE, BLACK);
}

static void cmd_nubo(int argc, char* argv[]) {
    const char* msg = "Manual invocation from shell";
    if (argc > 1) msg = argv[1];
    kernel_nubo(msg);
}

static void cmd_packet(void) {
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("[PacketX] Running demo...");
    kernel_set_color(WHITE, BLACK);
    kernel_packet_demo();
    kernel_set_color(LIGHT_GREEN, BLACK);
    kernel_println("[PacketX] Demo complete.");
    kernel_set_color(WHITE, BLACK);
}

/* Demo .jagu script */
static const char DEMO_JAGU[] =
    "# JagOs Demo Script (.jagu)\n"
    "color cyan\n"
    "print === .jagu Script Engine Demo ===\n"
    "color white\n"
    "print This is a .jagu script running inside JagOs!\n"
    "wait 200\n"
    "color yellow\n"
    "print Lines are parsed top to bottom.\n"
    "wait 200\n"
    "color green\n"
    "print Supported: print, clear, wait, color, comments\n"
    "wait 200\n"
    "color white\n"
    "print Script complete!\n";

static void cmd_jagu(void) {
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("[jagu] Running embedded demo script...\n");
    kernel_set_color(WHITE, BLACK);
    jagu_run_string(DEMO_JAGU);
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        kernel_print(argv[i]);
        if (i < argc - 1) kernel_putchar(' ');
    }
    kernel_putchar('\n');
}

static void cmd_color(int argc, char* argv[]) {
    if (argc < 2) {
        kernel_println("Usage: color <red|green|blue|cyan|yellow|white>");
        return;
    }
    const char* c = argv[1];
    if (sh_strcmp(c, "red")    == 0) kernel_set_color(LIGHT_RED, BLACK);
    else if (sh_strcmp(c, "green")  == 0) kernel_set_color(LIGHT_GREEN, BLACK);
    else if (sh_strcmp(c, "blue")   == 0) kernel_set_color(LIGHT_BLUE, BLACK);
    else if (sh_strcmp(c, "cyan")   == 0) kernel_set_color(LIGHT_CYAN, BLACK);
    else if (sh_strcmp(c, "yellow") == 0) kernel_set_color(YELLOW, BLACK);
    else if (sh_strcmp(c, "white")  == 0) kernel_set_color(WHITE, BLACK);
    else { kernel_print("Unknown color: "); kernel_println(c); }
}

static void cmd_history(void) {
    if (history_count == 0) {
        kernel_println("(no history)");
        return;
    }
    int start = (history_count > HISTORY_SIZE) ? history_count - HISTORY_SIZE : 0;
    for (int i = start; i < history_count; i++) {
        sh_print_int(i + 1);
        kernel_print("  ");
        kernel_println(history[i % HISTORY_SIZE]);
    }
}

static void cmd_uptime(void) {
    uint32_t h, m, s;
    kernel_get_uptime(&h, &m, &s);
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_print("Uptime: ");
    sh_print_int((int)h); kernel_print("h ");
    sh_print_int((int)m); kernel_print("m ");
    sh_print_int((int)s); kernel_println("s");
    kernel_set_color(WHITE, BLACK);
}

static void cmd_version(void) {
    kernel_set_color(LIGHT_GREEN, BLACK);
    kernel_println("JagOs v0.2 - Developed by SushForAhkI");
    kernel_set_color(WHITE, BLACK);
    kernel_println("Features: Turkish Q Keyboard, PIT Timer, Kernel Logs,");
    kernel_println("           Basic Calculator, Random, PC Speaker Beep");
}

static void cmd_keymap(int argc, char* argv[]) {
    if (argc < 2) {
        kernel_print("Current layout: ");
        kernel_println(kernel_get_layout() ? "Turkish Q" : "US QWERTY");
        return;
    }
    if (sh_strcmp(argv[1], "tr") == 0) {
        kernel_set_layout(1);
        kernel_println("Keyboard layout set to Turkish Q");
    } else if (sh_strcmp(argv[1], "us") == 0) {
        kernel_set_layout(0);
        kernel_println("Keyboard layout set to US QWERTY");
    } else {
        kernel_println("Usage: keymap <tr|us>");
    }
}

static void cmd_calc(int argc, char* argv[]) {
    if (argc < 4) {
        kernel_println("Usage: calc <num1> <+|-|*|/> <num2>");
        return;
    }
    int a = 0, b = 0;
    const char* p = argv[1];
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    a *= sign;
    p = argv[3];
    sign = 1;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    b *= sign;
    int res = 0;
    if (sh_strcmp(argv[2], "+") == 0) res = a + b;
    else if (sh_strcmp(argv[2], "-") == 0) res = a - b;
    else if (sh_strcmp(argv[2], "*") == 0) res = a * b;
    else if (sh_strcmp(argv[2], "/") == 0) { if (b != 0) res = a / b; else { kernel_println("Division by zero!"); return; } }
    else { kernel_println("Unknown operator. Use + - * /"); return; }
    kernel_print(argv[1]); kernel_print(" "); kernel_print(argv[2]); kernel_print(" "); kernel_print(argv[3]);
    kernel_print(" = "); sh_print_int(res); kernel_putchar('\n');
}

static void cmd_beep(int argc, char* argv[]) {
    uint32_t freq = 1000;
    if (argc > 1) {
        const char* p = argv[1];
        freq = 0;
        while (*p >= '0' && *p <= '9') freq = freq * 10 + (*p++ - '0');
        if (freq < 20) freq = 20;
        if (freq > 20000) freq = 20000;
    }
    kernel_beep(freq, 200);
    kernel_print("Beep at "); sh_print_int((int)freq); kernel_println(" Hz");
}

static void cmd_timer(int argc, char* argv[]) {
    if (argc < 2) { kernel_println("Usage: timer <seconds>"); return; }
    int sec = 0;
    const char* p = argv[1];
    while (*p >= '0' && *p <= '9') sec = sec * 10 + (*p++ - '0');
    if (sec <= 0 || sec > 300) { kernel_println("Range: 1-300 seconds"); return; }
    kernel_print("Timer started: "); sh_print_int(sec); kernel_println("s");
    for (int i = sec; i > 0; i--) {
        sh_print_int(i); kernel_print("... ");
        delay_ms(1000);
    }
    kernel_println("Time's up!");
    kernel_beep(800, 300);
}

static void cmd_random(int argc, char* argv[]) {
    uint32_t r = kernel_rand();
    uint32_t max = 100;
    if (argc > 1) {
        const char* p = argv[1];
        max = 0;
        while (*p >= '0' && *p <= '9') max = max * 10 + (*p++ - '0');
        if (max == 0) max = 100;
    }
    kernel_print("Random: "); sh_print_int((int)(r % max));
    kernel_print(" (max "); sh_print_int((int)max); kernel_println(")");
}

static void cmd_klogs(void) {
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("--- Kernel Logs ---");
    kernel_set_color(WHITE, BLACK);
    kernel_klog_dump();
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("--- End Logs ---");
    kernel_set_color(WHITE, BLACK);
}

static void cmd_reboot(void) {
    kernel_println("Rebooting system...");
    kernel_reboot();
}

static void cmd_halt(void) {
    kernel_set_color(YELLOW, BLACK);
    kernel_println("System halting. Goodbye!");
    __asm__ volatile("cli; hlt");
    while (1) __asm__ volatile("hlt");
}

/* ============================================================================
 * SHELL PROMPT AND MAIN LOOP
 * ============================================================================ */

static void shell_prompt(void) {
    kernel_set_color(LIGHT_GREEN, BLACK);
    kernel_print("jag");
    kernel_set_color(WHITE, BLACK);
    kernel_print("@os");
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_print(":~$ ");
    kernel_set_color(WHITE, BLACK);
}

static void shell_dispatch(char* cmd_line) {
    if (!cmd_line[0]) return;

    history_add(cmd_line);

    char buf[SHELL_BUF];
    sh_strncpy(buf, cmd_line, SHELL_BUF);

    char* argv[MAX_ARGS];
    int argc = sh_split(buf, argv, MAX_ARGS);
    if (argc == 0) return;

    const char* cmd = argv[0];

    if (sh_strcmp(cmd, "help")    == 0) cmd_help();
    else if (sh_strcmp(cmd, "clear")   == 0) cmd_clear();
    else if (sh_strcmp(cmd, "sysinfo") == 0) cmd_sysinfo();
    else if (sh_strcmp(cmd, "uptime")  == 0) cmd_uptime();
    else if (sh_strcmp(cmd, "nubo")    == 0) cmd_nubo(argc, argv);
    else if (sh_strcmp(cmd, "packet")  == 0) cmd_packet();
    else if (sh_strcmp(cmd, "jagu")    == 0) cmd_jagu();
    else if (sh_strcmp(cmd, "echo")    == 0) cmd_echo(argc, argv);
    else if (sh_strcmp(cmd, "color")   == 0) cmd_color(argc, argv);
    else if (sh_strcmp(cmd, "keymap")  == 0) cmd_keymap(argc, argv);
    else if (sh_strcmp(cmd, "calc")    == 0) cmd_calc(argc, argv);
    else if (sh_strcmp(cmd, "beep")    == 0) cmd_beep(argc, argv);
    else if (sh_strcmp(cmd, "timer")   == 0) cmd_timer(argc, argv);
    else if (sh_strcmp(cmd, "random")  == 0) cmd_random(argc, argv);
    else if (sh_strcmp(cmd, "version") == 0) cmd_version();
    else if (sh_strcmp(cmd, "klogs")   == 0) cmd_klogs();
    else if (sh_strcmp(cmd, "history") == 0) cmd_history();
    else if (sh_strcmp(cmd, "panic")   == 0) kernel_panic("User-triggered panic from shell");
    else if (sh_strcmp(cmd, "reboot")  == 0) cmd_reboot();
    else if (sh_strcmp(cmd, "halt")    == 0) cmd_halt();
    else {
        kernel_set_color(LIGHT_RED, BLACK);
        kernel_print("Unknown command: '");
        kernel_print(cmd);
        kernel_println("'  (type 'help' for commands)");
        kernel_set_color(WHITE, BLACK);
    }
}

/* ============================================================================
 * SHELL ENTRY POINT (called from kernel_main)
 * ============================================================================ */
extern "C" void shell_main(void) {
    kernel_set_color(LIGHT_CYAN, BLACK);
    kernel_println("Welcome to JagOs Shell! Type 'help' for commands.");
    kernel_set_color(WHITE, BLACK);
    kernel_putchar('\n');

    static char input[SHELL_BUF];

    while (1) {
        shell_prompt();
        kernel_readline(input, SHELL_BUF);
        shell_dispatch(input);
    }
}
