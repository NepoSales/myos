/* shell.c - the interactive command shell
 *
 * The shell is an ordinary (system) task. It reads a line from the
 * keyboard, splits it into arguments, and runs the matching built-in
 * command. "cmd > file" and "cmd >> file" redirect the command's output by
 * attaching a memory buffer as the task's stdout and writing it to the
 * file afterwards.
 */

#include "shell/shell.h"
#include "kernel.h"
#include "fs/fs.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "proc/task.h"
#include "proc/programs.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "arch/io.h"

#define LINE_MAX      256
#define MAX_ARGS      16
#define HISTORY_SIZE  16

typedef struct {
    const char* name;
    const char* usage;
    const char* help;
    int         group;
    int       (*fn)(int argc, char** argv);
} command_t;

enum { GROUP_SYSTEM, GROUP_PROCESS, GROUP_FILES, NUM_GROUPS };
static const char* group_names[NUM_GROUPS] = { "System", "Processes", "Files" };

static char cwd[FS_PATH_MAX] = "/";
static char history[HISTORY_SIZE][LINE_MAX];
static int  history_count = 0;              /* total lines ever added */

extern const command_t commands[];
static const command_t* find_command(const char* name);

/* ---------- small helpers ---------- */

static void set_fg(enum vga_color c) {
    /* colors only make sense on the screen, not in redirected output */
    if (!task_stdout()) console_set_fg(c);
}

static void fs_error(const char* cmd, const char* path, int err) {
    kprintf("%s: %s: %s\n", cmd, path, fs_strerror(err));
}

/* Resolve a user-supplied path against the current directory. */
static int resolve(const char* cmd, const char* path, char* out) {
    int err = path_resolve(cwd, path, out, FS_PATH_MAX);
    if (err) fs_error(cmd, path, err);
    return err;
}

static void join_path(char* out, const char* dir, const char* name) {
    strlcpy(out, dir, FS_PATH_MAX);
    if (strcmp(dir, "/") != 0) strlcat(out, "/", FS_PATH_MAX);
    strlcat(out, name, FS_PATH_MAX);
}

static const char* basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---------- line editing ---------- */

static void print_prompt(void) {
    console_set_fg(VGA_LIGHT_GREEN);
    console_write("myos");
    console_set_fg(VGA_LIGHT_GREY);
    console_write(":");
    console_set_fg(VGA_LIGHT_BLUE);
    console_write(cwd);
    console_set_fg(VGA_LIGHT_GREY);
    console_write("$ ");
}

static void erase_input(int len) {
    while (len-- > 0) console_putc('\b');
}

/* State of the line being edited, used to redraw it after background output */
static const char* edit_buf;
static int*        edit_len;
static int         edit_interactive;

static void redraw_input(void) {
    char tmp[LINE_MAX];
    int n = *edit_len < LINE_MAX ? *edit_len : LINE_MAX - 1;
    memcpy(tmp, edit_buf, n);
    tmp[n] = '\0';
    if (edit_interactive) print_prompt();
    console_write(tmp);
}

static int read_line_raw(char* buf, int max, int interactive);

/* Read a line into buf. Returns its length, or -1 if cancelled (Ctrl+C). */
static int read_line(char* buf, int max, int interactive) {
    int len = read_line_raw(buf, max, interactive);
    console_set_input_hook(NULL);
    return len;
}

static int read_line_raw(char* buf, int max, int interactive) {
    int len = 0;
    edit_buf = buf;
    edit_len = &len;
    edit_interactive = interactive;
    console_set_input_hook(redraw_input);

    int hist_pos = history_count;
    int oldest = history_count > HISTORY_SIZE ? history_count - HISTORY_SIZE : 0;

    for (;;) {
        int c = keyboard_getchar();

        if (c == '\n') {
            console_putc('\n');
            buf[len] = '\0';
            return len;
        } else if (c == '\b') {
            if (len > 0) { len--; console_putc('\b'); }
        } else if (c == KEY_CTRL('c')) {
            console_write("^C\n");
            buf[0] = '\0';
            return -1;
        } else if (c == KEY_CTRL('l') && interactive) {
            console_clear();
            print_prompt();
            buf[len] = '\0';
            console_write(buf);
        } else if ((c == KEY_UP || c == KEY_DOWN) && interactive) {
            if (c == KEY_UP && hist_pos > oldest) hist_pos--;
            else if (c == KEY_DOWN && hist_pos < history_count) hist_pos++;
            else continue;
            erase_input(len);
            if (hist_pos < history_count) strlcpy(buf, history[hist_pos % HISTORY_SIZE], max);
            else buf[0] = '\0';
            len = (int)strlen(buf);
            console_write(buf);
        } else if (c >= 32 && c < 127 && len + 1 < max) {
            buf[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

static void history_add(const char* line) {
    if (history_count > 0 && strcmp(history[(history_count - 1) % HISTORY_SIZE], line) == 0)
        return;     /* skip consecutive duplicates */
    strlcpy(history[history_count % HISTORY_SIZE], line, LINE_MAX);
    history_count++;
}

/* ---------- parsing ---------- */

typedef struct {
    int   argc;
    char* argv[MAX_ARGS];
    char* redirect;         /* target file of > or >>, or NULL */
    int   append;           /* 1 for >> */
} cmdline_t;

/* Split a line into words. Double quotes group words ("hello world"), and
 * an unquoted > or >> marks the next word as the output file. Words are
 * copied into `buf`, so the original line is left untouched. */
static const char* parse_line(const char* s, cmdline_t* c, char* buf, size_t size) {
    size_t w = 0;
    int want_target = 0;
    memset(c, 0, sizeof(*c));

    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;

        if (*s == '>') {
            if (c->redirect || want_target) return "unexpected '>'";
            s++;
            if (*s == '>') { c->append = 1; s++; }
            want_target = 1;
            continue;
        }

        char* word = buf + w;
        int quoted = 0;
        while (*s && (quoted || (*s != ' ' && *s != '\t' && *s != '>'))) {
            if (*s == '"') { quoted = !quoted; s++; continue; }
            if (w + 1 >= size) return "line too long";
            buf[w++] = *s++;
        }
        if (quoted) return "unterminated quote";
        buf[w++] = '\0';

        if (want_target) {
            c->redirect = word;
            want_target = 0;
        } else {
            if (c->argc == MAX_ARGS) return "too many arguments";
            c->argv[c->argc++] = word;
        }
    }
    if (want_target) return "missing file name after '>'";
    return NULL;
}

static void execute(const char* line) {
    static char words[LINE_MAX + MAX_ARGS];
    cmdline_t c;

    const char* err = parse_line(line, &c, words, sizeof(words));
    if (err) { kprintf("syntax error: %s\n", err); return; }
    if (c.argc == 0) return;

    const command_t* cmd = find_command(c.argv[0]);
    if (!cmd) {
        kprintf("%s: command not found (type 'help')\n", c.argv[0]);
        return;
    }

    if (!c.redirect) {
        cmd->fn(c.argc, c.argv);
        return;
    }

    char path[FS_PATH_MAX];
    if (resolve(c.argv[0], c.redirect, path)) return;

    outbuf_t out = { 0 };
    task_set_stdout(&out);
    cmd->fn(c.argc, c.argv);
    task_set_stdout(NULL);

    int res = fs_write(path, out.data ? out.data : "", out.len, c.append);
    if (res < 0) fs_error(c.argv[0], c.redirect, res);
    outbuf_free(&out);
}

/* =====================================================================
 *  System commands
 * ===================================================================== */

static int cmd_help(int argc, char** argv) {
    if (argc > 1) {
        const command_t* cmd = find_command(argv[1]);
        if (!cmd) { kprintf("help: no such command: %s\n", argv[1]); return 1; }
        kprintf("usage: %s\n  %s\n", cmd->usage, cmd->help);
        return 0;
    }

    for (int g = 0; g < NUM_GROUPS; g++) {
        set_fg(VGA_YELLOW);
        kprintf("%-11s", group_names[g]);
        set_fg(VGA_LIGHT_GREY);
        int col = 11;
        for (const command_t* cmd = commands; cmd->name; cmd++) {
            if (cmd->group != g) continue;
            int len = (int)strlen(cmd->name) + 1;
            if (col + len > 78) { kprintf("\n%-11s", ""); col = 11; }
            kprintf("%s ", cmd->name);
            col += len;
        }
        kputc('\n');
    }
    kprintf("\n'help <command>' shows details. Redirect output with > or >>.\n");
    kprintf("Keys: Up/Down = history, Ctrl+C = cancel line, Ctrl+L = clear.\n");
    return 0;
}

static int cmd_about(int argc, char** argv) {
    (void)argc; (void)argv;
    fs_info_t fs;
    fs_get_info(&fs);
    set_fg(VGA_LIGHT_CYAN);
    kprintf("%s %s - a minimal educational operating system\n", MYOS_NAME, MYOS_VERSION);
    set_fg(VGA_LIGHT_GREY);
    kprintf("  arch        i386, 32-bit protected mode, flat memory model\n");
    kprintf("  boot        Multiboot (%s)\n", kernel_bootloader());
    kprintf("  memory      bitmap page allocator + first-fit kernel heap\n");
    kprintf("  processes   kernel threads, preemptive round-robin scheduler\n");
    kprintf("  files       hierarchical file system (%s)\n", fs.backend);
    kprintf("  drivers     VGA text, PS/2 keyboard, PIT timer, CMOS clock, serial\n");
    return 0;
}

static int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++)
        kprintf(i > 1 ? " %s" : "%s", argv[i]);
    kputc('\n');
    return 0;
}

static int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    console_clear();
    return 0;
}

static int cmd_color(int argc, char** argv) {
    int n;
    if (argc != 2 || !parse_int(argv[1], &n) || n < 1 || n > 15) {
        kprintf("usage: color <1-15>\n");
        return 1;
    }
    console_set_color(VGA_COLOR(n, VGA_BLACK));
    return 0;
}

static int cmd_history(int argc, char** argv) {
    (void)argc; (void)argv;
    int first = history_count > HISTORY_SIZE ? history_count - HISTORY_SIZE : 0;
    for (int i = first; i < history_count; i++)
        kprintf("%4d  %s\n", i + 1, history[i % HISTORY_SIZE]);
    return 0;
}

static int cmd_date(int argc, char** argv) {
    (void)argc; (void)argv;
    char buf[24];
    rtc_format(rtc_now(), buf, sizeof(buf));
    kprintf("%s UTC\n", buf);
    return 0;
}

static int cmd_uptime(int argc, char** argv) {
    (void)argc; (void)argv;
    char buf[16];
    timer_format(timer_ticks(), buf, sizeof(buf));
    kprintf("up %s (%u ticks at %d Hz), %d tasks\n", buf, timer_ticks(), TIMER_HZ, task_count());
    return 0;
}

static int cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    uint32_t total = pmm_total_pages(), used = pmm_used_pages();
    heap_stats_t hs;
    heap_get_stats(&hs);

    set_fg(VGA_YELLOW);
    kprintf("Physical memory (4 KiB pages)\n");
    set_fg(VGA_LIGHT_GREY);
    kprintf("  total  %7u KiB  %6u pages\n", total * 4, total);
    kprintf("  used   %7u KiB  %6u pages  (kernel, heap, task stacks)\n", used * 4, used);
    kprintf("  free   %7u KiB  %6u pages\n", (total - used) * 4, total - used);
    set_fg(VGA_YELLOW);
    kprintf("Kernel heap\n");
    set_fg(VGA_LIGHT_GREY);
    kprintf("  size   %7u KiB  in %u region(s)\n", hs.total / 1024, hs.regions);
    kprintf("  used   %7u B    in %u blocks total\n", hs.used, hs.blocks);
    kprintf("  free   %7u B\n", hs.free);
    return 0;
}

static int cmd_sleep(int argc, char** argv) {
    int secs;
    if (argc != 2 || !parse_int(argv[1], &secs) || secs < 0) {
        kprintf("usage: sleep <seconds>\n");
        return 1;
    }
    task_sleep((uint32_t)secs * 1000);
    return 0;
}

static int cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    kprintf("Rebooting...\n");
    __asm__ volatile ("cli");
    while (inb(0x64) & 0x02) ;          /* wait for the keyboard controller */
    outb(0x64, 0xFE);                   /* pulse the CPU reset line */
    for (;;) cpu_halt();
    return 0;
}

static int cmd_halt(int argc, char** argv) {
    (void)argc; (void)argv;
    kprintf("System halted. It is now safe to turn off the machine.\n");
    outw(0x604, 0x2000);                /* QEMU / Bochs ACPI power-off */
    for (;;) __asm__ volatile ("cli; hlt");
    return 0;
}

/* =====================================================================
 *  Process commands
 * ===================================================================== */

static int cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    static task_info_t before[MAX_TASKS], after[MAX_TASKS];

    /* sample CPU usage over a short window, like `top` does */
    const uint32_t window_ms = 500;
    int n0 = task_list(before, MAX_TASKS);
    task_sleep(window_ms);
    int n1 = task_list(after, MAX_TASKS);
    uint32_t window_ticks = window_ms * TIMER_HZ / 1000;

    set_fg(VGA_YELLOW);
    kprintf("  PID  NAME        STATE     %%CPU   CPU TIME  ARGS\n");
    set_fg(VGA_LIGHT_GREY);
    for (int i = 0; i < n1; i++) {
        task_info_t* t = &after[i];
        uint32_t prev = 0;
        for (int j = 0; j < n0; j++)
            if (before[j].pid == t->pid) prev = before[j].cpu_ticks;
        uint32_t pct = (t->cpu_ticks - prev) * 100 / window_ticks;
        if (pct > 100) pct = 100;
        uint32_t ms = t->cpu_ticks * (1000 / TIMER_HZ);

        kprintf("  %3d  %-10s  %-8s  %3u%%  %6u.%02us  %s%s\n",
                t->pid, t->name, task_state_name(t->state), pct,
                ms / 1000, (ms % 1000) / 10, t->args, t->system ? "[system]" : "");
    }
    return 0;
}

static int cmd_run(int argc, char** argv) {
    if (argc < 2) {
        kprintf("usage: run <program> [args]   (runs in the background)\n\nPrograms:\n");
        for (int i = 0; program_get(i); i++) {
            const program_t* p = program_get(i);
            kprintf("  %-18s %s\n", p->usage, p->description);
        }
        return 1;
    }

    const program_t* p = program_find(argv[1]);
    if (!p) { kprintf("run: no such program: %s (type 'run' for a list)\n", argv[1]); return 1; }

    char args[TASK_ARGS_MAX] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strlcat(args, " ", sizeof(args));
        strlcat(args, argv[i], sizeof(args));
    }

    int pid = task_spawn(p->name, p->entry, args, 0);
    if (pid < 0) { kprintf("run: cannot create task (task table or memory full)\n"); return 1; }
    kprintf("[%d] %s started\n", pid, p->name);
    return 0;
}

static int cmd_kill(int argc, char** argv) {
    if (argc < 2) { kprintf("usage: kill <pid>...\n"); return 1; }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int pid;
        if (!parse_int(argv[i], &pid)) { kprintf("kill: invalid pid: %s\n", argv[i]); status = 1; continue; }
        int res = task_kill(pid);
        if (res == -1)      { kprintf("kill: (%d): no such task\n", pid); status = 1; }
        else if (res == -2) { kprintf("kill: (%d): system tasks cannot be killed\n", pid); status = 1; }
        else                kprintf("[%d] killed\n", pid);
    }
    return status;
}

/* =====================================================================
 *  File commands
 * ===================================================================== */

static int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    kprintf("%s\n", cwd);
    return 0;
}

static int cmd_cd(int argc, char** argv) {
    const char* arg = argc > 1 ? argv[1] : "/home";
    char path[FS_PATH_MAX];
    if (resolve("cd", arg, path)) return 1;

    fs_stat_t st;
    int err = fs_stat(path, &st);
    if (!err && st.type != FS_DIR) err = FS_ENOTDIR;
    if (err) { fs_error("cd", arg, err); return 1; }
    strlcpy(cwd, path, sizeof(cwd));
    return 0;
}

static void print_entry(const fs_stat_t* st) {
    if (st->type == FS_DIR) {
        kprintf("  %8s  ", "<dir>");
        set_fg(VGA_LIGHT_BLUE);
        kprintf("%s/\n", st->name);
        set_fg(VGA_LIGHT_GREY);
    } else {
        kprintf("  %8u  %s\n", st->size, st->name);
    }
}

static int cmd_ls(int argc, char** argv) {
    const char* arg = argc > 1 ? argv[1] : ".";
    char path[FS_PATH_MAX];
    if (resolve("ls", arg, path)) return 1;

    fs_stat_t st;
    int err = fs_stat(path, &st);
    if (err) { fs_error("ls", arg, err); return 1; }
    if (st.type == FS_FILE) { print_entry(&st); return 0; }

    for (uint32_t i = 0; fs_readdir(path, i, &st) == FS_OK; i++)
        print_entry(&st);
    return 0;
}

static void tree(const char* path, char* prefix, int depth) {
    fs_stat_t dir, st;
    if (fs_stat(path, &dir) || depth > 8) return;

    for (uint32_t i = 0; i < dir.size && fs_readdir(path, i, &st) == FS_OK; i++) {
        int last = i + 1 == dir.size;
        kprintf("%s%s", prefix, last ? "`-- " : "|-- ");
        if (st.type == FS_DIR) {
            set_fg(VGA_LIGHT_BLUE);
            kprintf("%s/\n", st.name);
            set_fg(VGA_LIGHT_GREY);

            char child[FS_PATH_MAX];
            size_t plen = strlen(prefix);
            join_path(child, path, st.name);
            strlcat(prefix, last ? "    " : "|   ", 64);
            tree(child, prefix, depth + 1);
            prefix[plen] = '\0';
        } else {
            kprintf("%s (%u bytes)\n", st.name, st.size);
        }
    }
}

static int cmd_tree(int argc, char** argv) {
    const char* arg = argc > 1 ? argv[1] : ".";
    char path[FS_PATH_MAX];
    if (resolve("tree", arg, path)) return 1;

    fs_stat_t st;
    int err = fs_stat(path, &st);
    if (!err && st.type != FS_DIR) err = FS_ENOTDIR;
    if (err) { fs_error("tree", arg, err); return 1; }

    char prefix[64] = "";
    set_fg(VGA_LIGHT_BLUE);
    kprintf("%s\n", path);
    set_fg(VGA_LIGHT_GREY);
    tree(path, prefix, 0);
    return 0;
}

static int cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { kprintf("usage: mkdir <dir>...\n"); return 1; }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[FS_PATH_MAX];
        if (resolve("mkdir", argv[i], path)) { status = 1; continue; }
        int err = fs_mkdir(path);
        if (err) { fs_error("mkdir", argv[i], err); status = 1; }
    }
    return status;
}

static int cmd_touch(int argc, char** argv) {
    if (argc < 2) { kprintf("usage: touch <file>...\n"); return 1; }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[FS_PATH_MAX];
        if (resolve("touch", argv[i], path)) { status = 1; continue; }
        int err = fs_create(path);
        if (err) { fs_error("touch", argv[i], err); status = 1; }
    }
    return status;
}

static int cmd_cat(int argc, char** argv) {
    if (argc < 2) { kprintf("usage: cat <file>...\n"); return 1; }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[FS_PATH_MAX], chunk[257];
        if (resolve("cat", argv[i], path)) { status = 1; continue; }

        uint32_t off = 0;
        int n;
        while ((n = fs_read(path, off, chunk, 256)) > 0) {
            chunk[n] = '\0';
            kputs(chunk);
            off += (uint32_t)n;
        }
        if (n < 0) { fs_error("cat", argv[i], n); status = 1; }
    }
    return status;
}

static int cmd_write(int argc, char** argv) {
    if (argc != 2) { kprintf("usage: write <file>\n"); return 1; }
    char path[FS_PATH_MAX];
    if (resolve("write", argv[1], path)) return 1;

    console_write("Enter text. Finish with a line containing only '.', cancel with Ctrl+C.\n");
    outbuf_t text = { 0 };
    char line[LINE_MAX];
    for (;;) {
        int len = read_line(line, sizeof(line), 0);
        if (len < 0) { outbuf_free(&text); console_write("write: cancelled\n"); return 1; }
        if (strcmp(line, ".") == 0) break;
        outbuf_append(&text, line, (uint32_t)len);
        outbuf_append(&text, "\n", 1);
    }

    int res = fs_write(path, text.data ? text.data : "", text.len, 0);
    if (res < 0) fs_error("write", argv[1], res);
    else kprintf("%u bytes written to %s\n", text.len, path);
    outbuf_free(&text);
    return res < 0;
}

static int remove_tree(const char* path) {
    fs_stat_t st, child;
    int err = fs_stat(path, &st);
    if (err) return err;
    if (st.type == FS_DIR) {
        while (fs_readdir(path, 0, &child) == FS_OK) {
            char sub[FS_PATH_MAX];
            join_path(sub, path, child.name);
            if ((err = remove_tree(sub))) return err;
        }
    }
    return fs_remove(path);
}

static int cmd_rm(int argc, char** argv) {
    int recursive = argc > 1 && strcmp(argv[1], "-r") == 0;
    int first = recursive ? 2 : 1;
    if (argc <= first) { kprintf("usage: rm [-r] <path>...\n"); return 1; }

    int status = 0;
    for (int i = first; i < argc; i++) {
        char path[FS_PATH_MAX];
        if (resolve("rm", argv[i], path)) { status = 1; continue; }

        size_t len = strlen(path);
        if (strncmp(cwd, path, len) == 0 && (cwd[len] == '/' || cwd[len] == '\0' || len == 1)) {
            kprintf("rm: %s: refusing to remove the current directory or its parents\n", argv[i]);
            status = 1;
            continue;
        }
        int err = recursive ? remove_tree(path) : fs_remove(path);
        if (err == FS_ENOTEMPTY && !recursive) {
            kprintf("rm: %s: directory not empty (use rm -r)\n", argv[i]);
            status = 1;
        } else if (err) {
            fs_error("rm", argv[i], err);
            status = 1;
        }
    }
    return status;
}

/* copy one file; `dst` may be an existing directory */
static int copy_file(const char* cmd, const char* src_arg, const char* dst_arg, char* src, char* dst) {
    fs_stat_t st;
    if (resolve(cmd, src_arg, src) || resolve(cmd, dst_arg, dst)) return 1;

    int err = fs_stat(src, &st);
    if (!err && st.type == FS_DIR) err = FS_EISDIR;
    if (err) { fs_error(cmd, src_arg, err); return 1; }

    fs_stat_t dst_st;
    if (fs_stat(dst, &dst_st) == FS_OK && dst_st.type == FS_DIR) {
        char tmp[FS_PATH_MAX];
        join_path(tmp, dst, basename(src));
        strlcpy(dst, tmp, FS_PATH_MAX);
    }
    if (strcmp(src, dst) == 0) { kprintf("%s: '%s' and '%s' are the same file\n", cmd, src, dst); return 1; }

    char* data = kmalloc(st.size + 1);
    if (!data) { fs_error(cmd, src_arg, FS_ENOMEM); return 1; }
    int n = fs_read(src, 0, data, st.size);
    int res = n < 0 ? n : fs_write(dst, data, (uint32_t)n, 0);
    kfree(data);
    if (res < 0) { fs_error(cmd, dst_arg, res); return 1; }
    return 0;
}

static int cmd_cp(int argc, char** argv) {
    if (argc != 3) { kprintf("usage: cp <source> <destination>\n"); return 1; }
    char src[FS_PATH_MAX], dst[FS_PATH_MAX];
    return copy_file("cp", argv[1], argv[2], src, dst);
}

static int cmd_mv(int argc, char** argv) {
    if (argc != 3) { kprintf("usage: mv <source> <destination>\n"); return 1; }
    char src[FS_PATH_MAX], dst[FS_PATH_MAX];
    if (copy_file("mv", argv[1], argv[2], src, dst)) return 1;
    int err = fs_remove(src);
    if (err) { fs_error("mv", argv[1], err); return 1; }
    return 0;
}

static int cmd_stat(int argc, char** argv) {
    if (argc != 2) { kprintf("usage: stat <path>\n"); return 1; }
    char path[FS_PATH_MAX], created[24], modified[24];
    if (resolve("stat", argv[1], path)) return 1;

    fs_stat_t st;
    int err = fs_stat(path, &st);
    if (err) { fs_error("stat", argv[1], err); return 1; }

    rtc_format(st.ctime, created, sizeof(created));
    rtc_format(st.mtime, modified, sizeof(modified));
    kprintf("  path      %s\n", path);
    kprintf("  type      %s\n", st.type == FS_DIR ? "directory" : "file");
    if (st.type == FS_DIR) kprintf("  entries   %u\n", st.size);
    else kprintf("  size      %u bytes\n", st.size);
    kprintf("  created   %s\n", created);
    kprintf("  modified  %s\n", modified);
    return 0;
}

static int cmd_df(int argc, char** argv) {
    (void)argc; (void)argv;
    fs_info_t info;
    fs_get_info(&info);
    kprintf("  backend      %s\n", info.backend);
    kprintf("  directories  %u\n", info.dirs);
    kprintf("  files        %u\n", info.files);
    if (info.bytes_total)
        kprintf("  used         %u of %u KiB\n", info.bytes_used / 1024, info.bytes_total / 1024);
    else
        kprintf("  used         %u bytes (capacity limited only by free RAM)\n", info.bytes_used);
    return 0;
}

/* =====================================================================
 *  Command table
 * ===================================================================== */

const command_t commands[] = {
    { "help",    "help [command]",       "list commands, or show help for one",          GROUP_SYSTEM,  cmd_help    },
    { "about",   "about",                "show information about the system",            GROUP_SYSTEM,  cmd_about   },
    { "echo",    "echo [text...]",       "print text",                                   GROUP_SYSTEM,  cmd_echo    },
    { "clear",   "clear",                "clear the screen",                             GROUP_SYSTEM,  cmd_clear   },
    { "color",   "color <1-15>",         "change the text color",                        GROUP_SYSTEM,  cmd_color   },
    { "history", "history",              "show the last commands",                       GROUP_SYSTEM,  cmd_history },
    { "date",    "date",                 "show the current date and time (CMOS clock)", GROUP_SYSTEM,  cmd_date    },
    { "uptime",  "uptime",               "show how long the system has been running",   GROUP_SYSTEM,  cmd_uptime  },
    { "mem",     "mem",                  "show physical memory and heap usage",          GROUP_SYSTEM,  cmd_mem     },
    { "reboot",  "reboot",               "restart the machine",                          GROUP_SYSTEM,  cmd_reboot  },
    { "halt",    "halt",                 "stop the machine",                             GROUP_SYSTEM,  cmd_halt    },
    { "ps",      "ps",                   "list tasks with CPU usage over the last 0.5 s", GROUP_PROCESS, cmd_ps      },
    { "run",     "run <program> [args]", "start a program as a background task",         GROUP_PROCESS, cmd_run     },
    { "kill",    "kill <pid>...",        "terminate tasks",                              GROUP_PROCESS, cmd_kill    },
    { "sleep",   "sleep <seconds>",      "put the shell task to sleep",                  GROUP_PROCESS, cmd_sleep   },
    { "pwd",     "pwd",                  "print the current directory",                  GROUP_FILES,   cmd_pwd     },
    { "cd",      "cd [dir]",             "change directory (default: /home)",            GROUP_FILES,   cmd_cd      },
    { "ls",      "ls [path]",            "list a directory",                             GROUP_FILES,   cmd_ls      },
    { "tree",    "tree [dir]",           "show a directory tree",                        GROUP_FILES,   cmd_tree    },
    { "mkdir",   "mkdir <dir>...",       "create directories",                           GROUP_FILES,   cmd_mkdir   },
    { "touch",   "touch <file>...",      "create empty files",                           GROUP_FILES,   cmd_touch   },
    { "cat",     "cat <file>...",        "print file contents",                          GROUP_FILES,   cmd_cat     },
    { "write",   "write <file>",         "type text into a file (ends with a '.' line)", GROUP_FILES,   cmd_write   },
    { "rm",      "rm [-r] <path>...",    "remove files or directories",                  GROUP_FILES,   cmd_rm      },
    { "cp",      "cp <src> <dst>",       "copy a file",                                  GROUP_FILES,   cmd_cp      },
    { "mv",      "mv <src> <dst>",       "move or rename a file",                        GROUP_FILES,   cmd_mv      },
    { "stat",    "stat <path>",          "show file details",                            GROUP_FILES,   cmd_stat    },
    { "df",      "df",                   "show file system usage",                       GROUP_FILES,   cmd_df      },
    { NULL, NULL, NULL, 0, NULL },
};

static const command_t* find_command(const char* name) {
    for (const command_t* c = commands; c->name; c++)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

/* ---------- main loop ---------- */

void shell_main(const char* args) {
    (void)args;
    char line[LINE_MAX];

    cmd_cd(1, NULL);                            /* start in /home */
    char* motd[] = { "cat", "/etc/motd" };
    cmd_cat(2, motd);

    for (;;) {
        print_prompt();
        if (read_line(line, sizeof(line), 1) <= 0) continue;
        history_add(line);
        execute(line);
    }
}
