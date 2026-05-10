/*
 * JagOs shell.cpp - v0.2 Enhanced
 * CLI + .jagu script engine + RAM Disk + Env Vars + Calculator
 */

typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
typedef unsigned int    size_t;
typedef signed int      int32_t;

#define NULL ((void*)0)

/* datetime_t from kernel */
typedef struct {
    uint8_t second, minute, hour;
    uint8_t day, month;
    uint16_t year;
} datetime_t;

extern "C" {
    void   kernel_print(const char*);
    void   kernel_println(const char*);
    void   kernel_putchar(char);
    void   kernel_clear(void);
    void   kernel_setcolor(uint8_t,uint8_t);
    char   kernel_getchar(void);
    void   kernel_readline(char*,int);
    void   kernel_panic(const char*);
    void   kernel_nubo(const char*);
    int    kernel_strcmp(const char*,const char*);
    int    kernel_strncmp(const char*,const char*,size_t);
    size_t kernel_strlen(const char*);
    void   kernel_packet_demo(void);
    /* v0.2 API additions */
    void   kernel_kb_set_layout(int);
    int    kernel_kb_get_layout(void);
    void   kernel_rtc_read(datetime_t*);
    void   kernel_serial_puts(const char*);
    void   kernel_reboot(void);
    void   kernel_shutdown(void);
    void   kernel_meminfo(size_t*,size_t*,size_t*);
    void*  kernel_kmalloc(size_t);
    void   kernel_kfree(void*);
}

enum Color : uint8_t {
    BLACK=0,BLUE,GREEN,CYAN,RED,MAGENTA,BROWN,LGREY,
    DGREY,LBLUE,LGREEN,LCYAN,LRED,LMAGENTA,YELLOW,WHITE
};

/* ---- utils ---- */

static int scmp(const char*a,const char*b){return kernel_strcmp(a,b);}
static int sncmp(const char*a,const char*b,size_t n){return kernel_strncmp(a,b,n);}

static void sncpy(char*d,const char*s,size_t n){
    size_t i=0;
    for(;i<n-1&&s[i];i++) d[i]=s[i];
    d[i]=0;
}

static int issp(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}

static void print_int(int32_t v){
    if(v<0){kernel_putchar('-');v=-v;}
    if(v==0){kernel_putchar('0');return;}
    char b[12]; int i=11; b[i]=0;
    while(v){b[--i]='0'+(v%10);v/=10;}
    kernel_print(b+i);
}

static int split(char*cmd,char*argv[],int max){
    int argc=0; char*p=cmd;
    while(*p&&argc<max){
        while(*p&&issp(*p))p++;
        if(!*p) break;
        argv[argc++]=p;
        while(*p&&!issp(*p))p++;
        if(*p)*p++=0;
    }
    return argc;
}

/* ---- history ---- */
#define HSIZE 8
#define BSIZE 256
static char hist[HSIZE][BSIZE];
static int  hcnt=0;

static void hist_add(const char*s){
    if(!s[0]) return;
    if(hcnt>0&&scmp(hist[(hcnt-1)%HSIZE],s)==0) return;
    sncpy(hist[hcnt%HSIZE],s,BSIZE);
    hcnt++;
}

/* ---- RAM Disk File System ---- */
#define MAX_FILES 16
#define MAX_FILE_SIZE 1024
#define MAX_PATH 64
#define MAX_NAME 16

typedef struct {
    char name[MAX_NAME];
    char path[MAX_PATH];
    uint8_t data[MAX_FILE_SIZE];
    size_t size;
    int used;
} File;

static File files[MAX_FILES];
static char current_dir[MAX_PATH] = "/";
static int fs_init=0;

static void fs_init_fs(void){
    if(fs_init) return;
    for(int i=0;i<MAX_FILES;i++) files[i].used=0;
    fs_init=1;
}

static int fs_find(const char*name){
    for(int i=0;i<MAX_FILES;i++)
        if(files[i].used && scmp(files[i].name,name)==0)
            return i;
    return -1;
}

static int fs_create(const char*name){
    if(fs_find(name)>=0) return -1; /* exists */
    for(int i=0;i<MAX_FILES;i++){
        if(!files[i].used){
            files[i].used=1;
            sncpy(files[i].name,name,MAX_NAME);
            sncpy(files[i].path,current_dir,MAX_PATH);
            files[i].size=0;
            return i;
        }
    }
    return -1; /* full */
}

static int fs_delete(const char*name){
    int i=fs_find(name);
    if(i<0) return -1;
    files[i].used=0;
    return 0;
}

static int fs_write(int idx,const char*data,size_t n){
    if(idx<0||idx>=MAX_FILES||!files[idx].used) return -1;
    if(n>MAX_FILE_SIZE) n=MAX_FILE_SIZE;
    for(size_t i=0;i<n;i++) files[idx].data[i]=data[i];
    files[idx].size=n;
    return 0;
}

static void fs_list(void){
    int cnt=0;
    kernel_setcolor(LCYAN,BLACK);
    kernel_print("Contents of "); kernel_print(current_dir); kernel_println(":");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("NAME             SIZE");
    kernel_setcolor(WHITE,BLACK);
    for(int i=0;i<MAX_FILES;i++){
        if(files[i].used && scmp(files[i].path,current_dir)==0){
            cnt++;
            /* Name padding */
            kernel_print(files[i].name);
            int pad=16-kernel_strlen(files[i].name);
            while(pad-->0) kernel_putchar(' ');
            print_int((int)files[i].size);
            kernel_println(" bytes");
        }
    }
    if(cnt==0) kernel_println("(empty)");
}

/* ---- Environment Variables ---- */
#define MAX_ENVS 16
#define MAX_ENV_NAME 16
#define MAX_ENV_VAL 64

typedef struct {
    char name[MAX_ENV_NAME];
    char value[MAX_ENV_VAL];
    int used;
} EnvVar;

static EnvVar envs[MAX_ENVS];

static void env_init(void){
    for(int i=0;i<MAX_ENVS;i++) envs[i].used=0;
}

static int env_find(const char*name){
    for(int i=0;i<MAX_ENVS;i++)
        if(envs[i].used && scmp(envs[i].name,name)==0)
            return i;
    return -1;
}

static void env_set(const char*name,const char*val){
    int i=env_find(name);
    if(i<0){ /* new */
        for(i=0;i<MAX_ENVS;i++) if(!envs[i].used) break;
        if(i>=MAX_ENVS) return; /* full */
        envs[i].used=1;
        sncpy(envs[i].name,name,MAX_ENV_NAME);
    }
    sncpy(envs[i].value,val,MAX_ENV_VAL);
}

static void env_unset(const char*name){
    int i=env_find(name);
    if(i>=0) envs[i].used=0;
}

static const char* env_get(const char*name){
    int i=env_find(name);
    return (i>=0) ? envs[i].value : (const char*)NULL;
}

static void env_list(void){
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("Environment Variables:");
    kernel_setcolor(WHITE,BLACK);
    int cnt=0;
    for(int i=0;i<MAX_ENVS;i++){
        if(envs[i].used){
            cnt++;
            kernel_print(envs[i].name);
            kernel_print("=");
            kernel_println(envs[i].value);
        }
    }
    if(cnt==0) kernel_println("(none set)");
}

/* ---- .jagu engine ---- */
static void delay(uint32_t ms){
    for(uint32_t i=0;i<ms;i++)
        for(volatile uint32_t j=0;j<8000;j++)
            __asm__ volatile("nop");
}

static void jagu_line(const char*ln){
    while(*ln&&issp(*ln))ln++;
    if(!*ln||*ln=='#') return;

    if(sncmp(ln,"print",5)==0&&(ln[5]==' '||ln[5]=='\t')){
        const char*t=ln+5; while(*t&&issp(*t))t++;
        kernel_println(t); return;
    }
    if(scmp(ln,"clear")==0){ kernel_clear(); return; }
    if(sncmp(ln,"wait",4)==0&&(ln[4]==' '||ln[4]=='\t')){
        const char*n=ln+4; while(*n&&issp(*n))n++;
        uint32_t ms=0;
        while(*n>='0'&&*n<='9') ms=ms*10+(uint32_t)(*n++)-'0';
        delay(ms); return;
    }
    if(sncmp(ln,"color",5)==0&&(ln[5]==' '||ln[5]=='\t')){
        const char*c=ln+5; while(*c&&issp(*c))c++;
        if(scmp(c,"red")==0)       kernel_setcolor(LRED,BLACK);
        else if(scmp(c,"green")==0) kernel_setcolor(LGREEN,BLACK);
        else if(scmp(c,"blue")==0)  kernel_setcolor(LBLUE,BLACK);
        else if(scmp(c,"cyan")==0)  kernel_setcolor(LCYAN,BLACK);
        else if(scmp(c,"yellow")==0)kernel_setcolor(YELLOW,BLACK);
        else if(scmp(c,"white")==0) kernel_setcolor(WHITE,BLACK);
        return;
    }
    kernel_setcolor(LRED,BLACK);
    kernel_print("[jagu] unknown: "); kernel_println(ln);
    kernel_setcolor(WHITE,BLACK);
}

static void jagu_run(const char*src){
    char line[128]; int li=0;
    while(*src){
        if(*src=='\n'||*src=='\r'){
            line[li]=0; jagu_line(line); li=0;
        } else if(li<127){ line[li++]=*src; }
        src++;
    }
    if(li){line[li]=0;jagu_line(line);}
}

static const char DEMO_JAGU[]=
    "# JagOs .jagu demo\n"
    "color cyan\n"
    "print === .jagu Script Engine ===\n"
    "color white\n"
    "print Running inside JagOs!\n"
    "wait 300\n"
    "color yellow\n"
    "print Supports: print clear wait color\n"
    "wait 300\n"
    "color green\n"
    "print Script done!\n"
    "color white\n";

/* ---- Calculator ---- */
static int calc_eval(const char*expr,int32_t*result){
    /* Simple calculator: supports + - * / with integers */
    int32_t a=0,b=0;
    char op=0;
    /* Parse first number */
    if(expr[0]=='0'&&(expr[1]=='x'||expr[1]=='X')){
        /* Hex */
        expr+=2;
        while(*expr){
            if(*expr>='0'&&*expr<='9') a=a*16+(*expr-'0');
            else if(*expr>='a'&&*expr<='f') a=a*16+(*expr-'a'+10);
            else if(*expr>='A'&&*expr<='F') a=a*16+(*expr-'A'+10);
            else break;
            expr++;
        }
    } else {
        while(*expr>='0'&&*expr<='9'){ a=a*10+(*expr-'0'); expr++; }
    }
    while(issp(*expr)) expr++;
    op=*expr++;
    if(!op) return -1;
    while(issp(*expr)) expr++;
    /* Parse second number */
    if(expr[0]=='0'&&(expr[1]=='x'||expr[1]=='X')){
        expr+=2;
        while(*expr){
            if(*expr>='0'&&*expr<='9') b=b*16+(*expr-'0');
            else if(*expr>='a'&&*expr<='f') b=b*16+(*expr-'a'+10);
            else if(*expr>='A'&&*expr<='F') b=b*16+(*expr-'A'+10);
            else break;
            expr++;
        }
    } else {
        while(*expr>='0'&&*expr<='9'){ b=b*10+(*expr-'0'); expr++; }
    }
    switch(op){
        case '+': *result=a+b; break;
        case '-': *result=a-b; break;
        case '*': *result=a*b; break;
        case '/': if(b==0) return -1; *result=a/b; break;
        default: return -1;
    }
    return 0;
}

static void print_hex(uint32_t v){
    kernel_print("0x");
    char buf[9]; buf[8]=0;
    for(int i=7;i>=0;i--){
        int d=v&0xF;
        buf[i]=(char)((d<10)?('0'+d):('A'+d-10));
        v>>=4;
    }
    kernel_print(buf);
}

/* ---- commands ---- */
static void cmd_help(void);

static void cmd_help(void){
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("================================================================================");
    kernel_println("                         JagOs Shell v2.0 - Commands");
    kernel_println("================================================================================");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("[System]");
    kernel_setcolor(WHITE,BLACK);
    kernel_println("  help        - Show this help message");
    kernel_println("  clear       - Clear the screen");
    kernel_println("  sysinfo     - System information");
    kernel_println("  reboot      - Reboot the system");
    kernel_println("  shutdown    - Shutdown the system");
    kernel_println("  meminfo     - Show memory usage");
    kernel_println("  keyboard    - Set keyboard layout (us/tr)");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("[File System]");
    kernel_setcolor(WHITE,BLACK);
    kernel_println("  ls          - List files in current directory");
    kernel_println("  pwd         - Print current directory");
    kernel_println("  mkdir <dir> - Create directory (stub)");
    kernel_println("  rm <file>   - Remove a file");
    kernel_println("  cat <file>  - Display file contents");
    kernel_println("  write <file> <text> - Create/write a file");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("[Utilities]");
    kernel_setcolor(WHITE,BLACK);
    kernel_println("  echo <text> - Echo text to screen");
    kernel_println("  color <c>   - Set text color (red/green/blue/cyan/yellow/white)");
    kernel_println("  date        - Show current date");
    kernel_println("  time        - Show current time");
    kernel_println("  calc <expr> - Calculator (e.g., calc 10+5, calc 0xFF*2)");
    kernel_println("  serial <t>  - Send text to serial port (COM1)");
    kernel_println("  env         - List environment variables");
    kernel_println("  set <k>=<v> - Set environment variable");
    kernel_println("  unset <k>   - Remove environment variable");
    kernel_println("  history     - Show command history");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("[Demo/Advanced]");
    kernel_setcolor(WHITE,BLACK);
    kernel_println("  nubo        - NuboGuard security demo");
    kernel_println("  packet      - PacketX network stub demo");
    kernel_println("  jagu        - Run .jagu script demo");
    kernel_println("  panic       - Trigger kernel panic (test)");
    kernel_println("  halt        - Halt system (stop)");
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("================================================================================");
    kernel_setcolor(WHITE,BLACK);
}

static void cmd_sysinfo(void){
    datetime_t dt;
    kernel_rtc_read(&dt);
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("================================================================================");
    kernel_println("                        JagOs System Information v0.2");
    kernel_println("================================================================================");
    kernel_setcolor(WHITE,BLACK);
    kernel_println("  Operating System : JagOs v0.2 (Enhanced)");
    kernel_println("  Architecture     : x86 32-bit Protected Mode");
    kernel_println("  Video            : VGA Text Mode 80x25");
    kernel_println("  Keyboard         : PS/2 Polling (US/TR-Q Layouts)");
    kernel_println("  Shell            : JagOs Shell v2.0");
    kernel_println("  Script Engine    : .jagu v1.0");
    kernel_println("  File System      : RAM Disk (16 files max)");
    kernel_println("  Serial Port      : COM1 @ 38400 baud");
    kernel_print("  Current Date     : "); print_int(dt.day); kernel_print("/");
    print_int(dt.month); kernel_print("/"); print_int((int)dt.year); kernel_println("");
    kernel_print("  Current Time     : "); print_int(dt.hour); kernel_print(":");
    if(dt.minute<10){ kernel_print("0"); } print_int(dt.minute); kernel_print(":");
    if(dt.second<10){ kernel_print("0"); } print_int(dt.second); kernel_println("");
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("================================================================================");
    kernel_setcolor(WHITE,BLACK);
}

static void cmd_date(void){
    datetime_t dt;
    kernel_rtc_read(&dt);
    kernel_setcolor(LCYAN,BLACK);
    kernel_print("Date: ");
    kernel_setcolor(WHITE,BLACK);
    print_int(dt.day); kernel_putchar('/');
    print_int(dt.month); kernel_putchar('/');
    print_int((int)dt.year); kernel_putchar('\n');
}

static void cmd_time(void){
    datetime_t dt;
    kernel_rtc_read(&dt);
    kernel_setcolor(LCYAN,BLACK);
    kernel_print("Time: ");
    kernel_setcolor(WHITE,BLACK);
    print_int(dt.hour); kernel_putchar(':');
    if(dt.minute<10){ kernel_putchar('0'); } print_int(dt.minute); kernel_putchar(':');
    if(dt.second<10){ kernel_putchar('0'); } print_int(dt.second); kernel_putchar('\n');
}

static void cmd_meminfo(void){
    size_t total,used,free;
    kernel_meminfo(&total,&used,&free);
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("=== Memory Information ===");
    kernel_setcolor(WHITE,BLACK);
    kernel_print("  Total Heap: "); print_int((int)total);
    kernel_println(" bytes (8MB)");
    kernel_print("  Used:       "); print_int((int)used);
    kernel_println(" bytes");
    kernel_print("  Free:       "); print_int((int)free);
    kernel_println(" bytes");
    kernel_print("  Usage:      ");
    if(total>0) print_int((int)(used*100/total)); else kernel_print("0");
    kernel_println("%");
}

static void cmd_keyboard(int argc,char*argv[]){
    if(argc<2){
        int layout=kernel_kb_get_layout();
        kernel_setcolor(LCYAN,BLACK);
        kernel_print("Current layout: ");
        kernel_setcolor(WHITE,BLACK);
        if(layout==0) kernel_println("US (QWERTY)");
        else if(layout==1) kernel_println("TR-Q");
        kernel_println("Usage: keyboard <us|tr>");
        return;
    }
    const char*layout=argv[1];
    if(scmp(layout,"us")==0){
        kernel_kb_set_layout(0);
        kernel_println("Keyboard layout set to: US (QWERTY)");
    } else if(scmp(layout,"tr")==0){
        kernel_kb_set_layout(1);
        kernel_println("Keyboard layout set to: TR-Q");
    } else {
        kernel_setcolor(LRED,BLACK);
        kernel_print("Unknown layout: "); kernel_println(layout);
        kernel_setcolor(WHITE,BLACK);
    }
}

static void cmd_calc(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: calc <expression> (e.g., calc 10+5, calc 0xFF*2)");return;}
    /* Join all args into one expression string */
    char expr[128]; int ei=0;
    for(int i=1;i<argc&&ei<120;i++){
        const char*p=argv[i];
        while(*p&&ei<120) expr[ei++]=*p++;
    }
    expr[ei]=0;
    int32_t result;
    if(calc_eval(expr,&result)==0){
        kernel_setcolor(LGREEN,BLACK);
        kernel_print("Result: ");
        kernel_setcolor(WHITE,BLACK);
        print_int(result);
        kernel_print(" (");
        print_hex((uint32_t)result);
        kernel_println(")");
    } else {
        kernel_setcolor(LRED,BLACK);
        kernel_println("Invalid expression. Use: number op number (+ - * /)");
        kernel_setcolor(WHITE,BLACK);
    }
}

static void cmd_serial(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: serial <text>");return;}
    for(int i=1;i<argc;i++){
        kernel_serial_puts(argv[i]);
        if(i<argc-1) kernel_serial_puts(" ");
    }
    kernel_serial_puts("\r\n");
    kernel_println("[Sent to COM1]");
}

/* ---- File System Commands ---- */
static void cmd_ls(void){
    fs_init_fs();
    fs_list();
}

static void cmd_pwd(void){
    kernel_setcolor(LCYAN,BLACK);
    kernel_print("Current directory: ");
    kernel_setcolor(WHITE,BLACK);
    kernel_println(current_dir);
}

static void cmd_mkdir(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: mkdir <directory>");return;}
    kernel_println("Note: Directory support is stubbed in RAM disk.");
    kernel_print("Would create: "); kernel_println(argv[1]);
}

static void cmd_rm(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: rm <file>");return;}
    fs_init_fs();
    if(fs_delete(argv[1])==0){
        kernel_setcolor(LGREEN,BLACK);
        kernel_print("Removed: "); kernel_println(argv[1]);
        kernel_setcolor(WHITE,BLACK);
    } else {
        kernel_setcolor(LRED,BLACK);
        kernel_print("File not found: "); kernel_println(argv[1]);
        kernel_setcolor(WHITE,BLACK);
    }
}

static void cmd_cat(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: cat <file>");return;}
    fs_init_fs();
    int idx=fs_find(argv[1]);
    if(idx<0){
        kernel_setcolor(LRED,BLACK);
        kernel_print("File not found: "); kernel_println(argv[1]);
        kernel_setcolor(WHITE,BLACK);
        return;
    }
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("=== File Contents ===");
    kernel_setcolor(WHITE,BLACK);
    for(size_t i=0;i<files[idx].size;i++){
        kernel_putchar((char)files[idx].data[i]);
    }
    if(files[idx].size>0 && files[idx].data[files[idx].size-1]!='\n')
        kernel_putchar('\n');
}

static void cmd_write(int argc,char*argv[]){
    if(argc<3){kernel_println("Usage: write <file> <text>");return;}
    fs_init_fs();
    int idx=fs_create(argv[1]);
    if(idx<0){
        /* Try to find existing */
        idx=fs_find(argv[1]);
        if(idx<0){
            kernel_setcolor(LRED,BLACK);
            kernel_println("Cannot create file (FS full or error)");
            kernel_setcolor(WHITE,BLACK);
            return;
        }
    }
    /* Join remaining args as content */
    char buf[256]; int bi=0;
    for(int i=2;i<argc&&bi<250;i++){
        const char*p=argv[i];
        while(*p&&bi<250) buf[bi++]=*p++;
        if(i<argc-1&&bi<250) buf[bi++]=' ';
    }
    buf[bi]=0;
    fs_write(idx,buf,bi);
    kernel_setcolor(LGREEN,BLACK);
    kernel_print("Wrote "); print_int(bi);
    kernel_print(" bytes to: "); kernel_println(argv[1]);
    kernel_setcolor(WHITE,BLACK);
}

/* ---- Environment Commands ---- */
static void cmd_env(void){
    env_list();
}

static void cmd_set(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: set VAR=value or set VAR value");return;}
    /* Check for VAR=value format */
    char*eq=argv[1];
    while(*eq&&*eq!='=') eq++;
    if(*eq=='='){
        *eq=0;
        env_set(argv[1],eq+1);
    } else if(argc>=3){
        env_set(argv[1],argv[2]);
    } else {
        kernel_println("Usage: set VAR=value or set VAR value");
        return;
    }
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("Variable set.");
    kernel_setcolor(WHITE,BLACK);
}

static void cmd_unset(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: unset <variable>");return;}
    env_unset(argv[1]);
    kernel_setcolor(LGREEN,BLACK);
    kernel_println("Variable removed.");
    kernel_setcolor(WHITE,BLACK);
}

static void cmd_color(int argc,char*argv[]){
    if(argc<2){kernel_println("Usage: color <name>");return;}
    const char*c=argv[1];
    if(scmp(c,"red")==0)        kernel_setcolor(LRED,BLACK);
    else if(scmp(c,"green")==0)  kernel_setcolor(LGREEN,BLACK);
    else if(scmp(c,"blue")==0)   kernel_setcolor(LBLUE,BLACK);
    else if(scmp(c,"cyan")==0)   kernel_setcolor(LCYAN,BLACK);
    else if(scmp(c,"yellow")==0) kernel_setcolor(YELLOW,BLACK);
    else if(scmp(c,"white")==0)  kernel_setcolor(WHITE,BLACK);
    else{ kernel_print("Unknown: ");kernel_println(c); }
}

static void cmd_echo(int argc,char*argv[]){
    for(int i=1;i<argc;i++){
        kernel_print(argv[i]);
        if(i<argc-1) kernel_putchar(' ');
    }
    kernel_putchar('\n');
}

static void cmd_history(void){
    if(!hcnt){kernel_println("(empty)");return;}
    int st=hcnt>HSIZE?hcnt-HSIZE:0;
    for(int i=st;i<hcnt;i++){
        print_int(i+1); kernel_print("  ");
        kernel_println(hist[i%HSIZE]);
    }
}

static void cmd_halt(void){
    kernel_setcolor(YELLOW,BLACK);
    kernel_println("Halting. Goodbye!");
    __asm__ volatile("cli; hlt");
    while(1) __asm__ volatile("hlt");
}

/* ---- dispatch ---- */
#define MARGS 16
static void dispatch(char*line){
    if(!line[0]) return;
    hist_add(line);
    char buf[BSIZE]; sncpy(buf,line,BSIZE);
    char*argv[MARGS]; int argc=split(buf,argv,MARGS);
    if(!argc) return;
    const char*cmd=argv[0];

    if(scmp(cmd,"help")==0)         cmd_help();
    else if(scmp(cmd,"clear")==0)   kernel_clear();
    else if(scmp(cmd,"sysinfo")==0) cmd_sysinfo();
    else if(scmp(cmd,"reboot")==0)  kernel_reboot();
    else if(scmp(cmd,"shutdown")==0) kernel_shutdown();
    else if(scmp(cmd,"meminfo")==0) cmd_meminfo();
    else if(scmp(cmd,"keyboard")==0) cmd_keyboard(argc,argv);
    else if(scmp(cmd,"nubo")==0)    kernel_nubo(argc>1?argv[1]:"manual");
    else if(scmp(cmd,"packet")==0)  kernel_packet_demo();
    else if(scmp(cmd,"jagu")==0)    jagu_run(DEMO_JAGU);
    else if(scmp(cmd,"echo")==0)    cmd_echo(argc,argv);
    else if(scmp(cmd,"color")==0)   cmd_color(argc,argv);
    else if(scmp(cmd,"date")==0)    cmd_date();
    else if(scmp(cmd,"time")==0)    cmd_time();
    else if(scmp(cmd,"calc")==0)    cmd_calc(argc,argv);
    else if(scmp(cmd,"serial")==0)  cmd_serial(argc,argv);
    /* File System */
    else if(scmp(cmd,"ls")==0)      cmd_ls();
    else if(scmp(cmd,"pwd")==0)     cmd_pwd();
    else if(scmp(cmd,"mkdir")==0)   cmd_mkdir(argc,argv);
    else if(scmp(cmd,"rm")==0)      cmd_rm(argc,argv);
    else if(scmp(cmd,"cat")==0)     cmd_cat(argc,argv);
    else if(scmp(cmd,"write")==0)   cmd_write(argc,argv);
    /* Environment */
    else if(scmp(cmd,"env")==0)     cmd_env();
    else if(scmp(cmd,"set")==0)     cmd_set(argc,argv);
    else if(scmp(cmd,"unset")==0)   cmd_unset(argc,argv);
    /* History & System */
    else if(scmp(cmd,"history")==0) cmd_history();
    else if(scmp(cmd,"panic")==0)   kernel_panic("user triggered");
    else if(scmp(cmd,"halt")==0)    cmd_halt();
    else{
        kernel_setcolor(LRED,BLACK);
        kernel_print("Unknown: "); kernel_println(cmd);
        kernel_setcolor(WHITE,BLACK);
    }
}

/* ---- prompt ---- */
static void prompt(void){
    kernel_setcolor(LGREEN,BLACK); kernel_print("jag");
    kernel_setcolor(WHITE,BLACK);  kernel_print("@os");
    kernel_setcolor(LCYAN,BLACK);  kernel_print(":~$ ");
    kernel_setcolor(WHITE,BLACK);
}

/* ---- entry ---- */
extern "C" void shell_main(void){
    kernel_setcolor(LCYAN,BLACK);
    kernel_println("Type 'help' for commands.\n");
    kernel_setcolor(WHITE,BLACK);

    static char input[BSIZE];
    while(1){
        prompt();
        kernel_readline(input,BSIZE);
        dispatch(input);
    }
}
