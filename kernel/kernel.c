/* kernel.c - kernel entry point and initialization sequence
 *
 * Boot order matters: each subsystem only depends on the ones above it.
 *   serial, VGA      -> we can print
 *   GDT, IDT, PIC    -> we can handle interrupts and exceptions
 *   PMM, heap        -> we can allocate memory
 *   tasking          -> the boot context becomes the idle task
 *   timer, keyboard  -> time slices and input (IRQ 0 and 1)
 *   file system      -> populated with a few default files
 * Then the system tasks are started and interrupts are enabled; from that
 * point on the scheduler is in charge and kernel_main() just idles.
 */

#include <stdint.h>
#include "kernel.h"
#include "multiboot.h"
#include "panic.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/pic.h"
#include "arch/io.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "drivers/keyboard.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "proc/task.h"
#include "proc/programs.h"
#include "fs/fs.h"
#include "shell/shell.h"
#include "lib/printf.h"
#include "lib/string.h"

static char bootloader[48] = "unknown";

const char* kernel_bootloader(void) { return bootloader; }

static void boot_ok(const char* what) {
    console_write("[ ");
    console_set_fg(VGA_LIGHT_GREEN);
    console_write("OK");
    console_set_fg(VGA_LIGHT_GREY);
    console_write(" ] ");
    console_write(what);
    console_write("\n");
}

/* ---------- default file system contents ---------- */

static void put_file(const char* path, const char* text) {
    fs_write(path, text, strlen(text), 0);
}

static void populate_fs(void) {
    fs_mkdir("/home");
    fs_mkdir("/tmp");
    fs_mkdir("/etc");
    fs_mkdir("/var");
    fs_mkdir("/var/log");

    put_file("/etc/version", MYOS_NAME " " MYOS_VERSION "\n");
    put_file("/etc/motd",
        "\nWelcome to " MYOS_NAME " " MYOS_VERSION "!\n"
        "Type 'help' for commands, or 'cat readme.txt' for a short tour.\n\n");
    put_file("/home/readme.txt",
        "A short tour of myOS\n"
        "--------------------\n"
        "Files:      ls, tree /, mkdir docs, echo hi > docs/a.txt, cat docs/a.txt\n"
        "Processes:  run spinner, run logger 2, run primes, ps, kill <pid>\n"
        "Faults:     run crash   (the kernel kills the task, the system survives)\n"
        "Memory:     mem, df\n"
        "Logs:       cat /var/log/boot.log, or /var/log/system.log after 'run logger'\n");
}

/* ---------- system tasks ---------- */

/* Draws the status bar on row 0 four times per second. */
static void statusd(const char* args) {
    (void)args;
    const uint8_t bar = VGA_COLOR(VGA_WHITE, VGA_BLUE);
    task_info_t list[MAX_TASKS];

    for (;;) {
        char up[16], now[24], line[SPINNER_COL + 1];
        timer_format(timer_ticks(), up, sizeof(up));
        rtc_format(rtc_now(), now, sizeof(now));
        now[16] = '\0';                    /* "YYYY-MM-DD hh:mm" */
        uint32_t used_kb = pmm_used_pages() * 4, total_kb = pmm_total_pages() * 4;
        int n = task_list(list, MAX_TASKS);

        ksnprintf(line, sizeof(line), " %s %s | %s | up %s | tasks %d | mem %u.%u/%uM",
                  MYOS_NAME, MYOS_VERSION, now, up, n,
                  used_kb / 1024, (used_kb % 1024) * 10 / 1024, total_kb / 1024);
        status_fill(0, SPINNER_COL, bar);
        status_write(0, line, bar);

        /* clear spinner slots whose task is gone */
        int owned[SPINNER_SLOTS] = { 0 };
        for (int i = 0; i < n; i++)
            if (strcmp(list[i].name, "spinner") == 0) owned[list[i].pid % SPINNER_SLOTS] = 1;
        for (int s = 0; s < SPINNER_SLOTS; s++)
            if (!owned[s]) status_fill(SPINNER_COL + s, 1, bar);

        task_sleep(250);
    }
}

/* ---------- entry point ---------- */

void kernel_main(uint32_t magic, multiboot_info_t* mbi) {
    serial_init();
    console_init();
    status_fill(0, VGA_WIDTH, VGA_COLOR(VGA_WHITE, VGA_BLUE));

    console_set_fg(VGA_LIGHT_CYAN);
    console_write(MYOS_NAME " " MYOS_VERSION " is booting...\n\n");
    console_set_fg(VGA_LIGHT_GREY);

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        kpanic("not loaded by a Multiboot-compliant bootloader", NULL);
    if (mbi->flags & MULTIBOOT_INFO_LOADER_NAME)
        strlcpy(bootloader, (const char*)mbi->boot_loader_name, sizeof(bootloader));

    gdt_init();
    boot_ok("GDT: flat 4 GiB code and data segments");

    pic_init();
    idt_init();
    boot_ok("IDT: CPU exception handlers installed, PIC remapped to 0x20");

    pmm_init(mbi);
    char msg[80];
    ksnprintf(msg, sizeof(msg), "PMM: %u MiB of usable RAM in 4 KiB pages",
              pmm_total_pages() * 4 / 1024);
    boot_ok(msg);

    heap_init();
    boot_ok("Heap: kmalloc/kfree ready");

    tasking_init();
    boot_ok("Scheduler: preemptive round-robin, 50 ms time slice");

    timer_init();
    boot_ok("Timer: PIT at 100 Hz on IRQ 0");

    rtc_init();
    char now[24];
    rtc_format(rtc_now(), now, sizeof(now));
    ksnprintf(msg, sizeof(msg), "Clock: %s UTC (CMOS RTC)", now);
    boot_ok(msg);

    keyboard_init();
    boot_ok("Keyboard: PS/2 on IRQ 1");

    if (fs_init() != FS_OK) kpanic("cannot initialize the file system", NULL);
    populate_fs();
    ksnprintf(msg, sizeof(msg), "%s  boot\n", now);
    fs_write("/var/log/boot.log", msg, strlen(msg), 1);
    fs_info_t info;
    fs_get_info(&info);
    ksnprintf(msg, sizeof(msg), "File system: %s mounted at /", info.backend);
    boot_ok(msg);

    if (task_spawn("statusd", statusd, "", 1) < 0 || task_spawn("shell", shell_main, "", 1) < 0)
        kpanic("cannot start the system tasks", NULL);
    boot_ok("Tasks: statusd and shell started");
    klog("kernel: boot complete, bootloader: %s\n", bootloader);

    /* From here on, this is the idle task: it runs only when nothing else
     * is ready, and halts the CPU until the next interrupt. */
    __asm__ volatile ("sti");
    for (;;) cpu_halt();
}
