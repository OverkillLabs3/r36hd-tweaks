/* crashlog: when a core dies with SIGSEGV, SIGBUS, SIGILL, SIGFPE or SIGABRT, write what is needed to find the
 * place: signal, fault address, pc, ra, sp, the registers, a few stack words and /proc/self/maps (so the pc can be
 * matched to a function in the unstripped .so). Append-only text file, written with plain write() calls.
 * Include this file in the core's main source and call crashlog_install("name") once. Then the handler restores the
 * default action and re-raises, so the exit code and the frontend behaviour stay the same.
 * File: /mnt/sdcard/cubegm/logs/crash_<name>.txt   (env R36_CRASHLOG=path overrides)
 */
#ifndef CRASHLOG_C
#define CRASHLOG_C
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <ucontext.h>

static char crashlog_path[256];
static char crashlog_altstack[64 * 1024];

static void crashlog_puts(int fd, const char *s) { size_t n = strlen(s); if (write(fd, s, n) < 0) {} }
static void crashlog_hex(int fd, const char *tag, unsigned long v)
{
    char b[48]; int i = 0; const char *h = "0123456789abcdef";
    b[i++] = ' ';
    for (const char *t = tag; *t && i < 30; t++) b[i++] = *t;
    b[i++] = '=';
    for (int s = (int)sizeof(unsigned long) * 8 - 4; s >= 0; s -= 4) b[i++] = h[(v >> s) & 15];
    b[i] = 0;
    crashlog_puts(fd, b);
}

static void crashlog_handler(int sig, siginfo_t *si, void *ctx)
{
    int fd = open(crashlog_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ucontext_t *uc = (ucontext_t *)ctx;
        crashlog_puts(fd, "\n=== crash");
        crashlog_hex(fd, "signal", (unsigned long)sig);
        crashlog_hex(fd, "code", (unsigned long)si->si_code);
        crashlog_hex(fd, "addr", (unsigned long)si->si_addr);
#if defined(__mips__)
        crashlog_hex(fd, "pc", (unsigned long)uc->uc_mcontext.pc);
        crashlog_hex(fd, "ra", (unsigned long)uc->uc_mcontext.gregs[31]);
        crashlog_hex(fd, "sp", (unsigned long)uc->uc_mcontext.gregs[29]);
        crashlog_puts(fd, "\nregs:");
        for (int i = 1; i < 32; i++) crashlog_hex(fd, "r", (unsigned long)uc->uc_mcontext.gregs[i]);
        crashlog_puts(fd, "\nstack:");
        unsigned long *sp = (unsigned long *)(uintptr_t)uc->uc_mcontext.gregs[29];
        for (int i = 0; i < 96 && sp; i++) { if (i % 6 == 0) crashlog_puts(fd, "\n "); crashlog_hex(fd, "w", sp[i]); }
#elif defined(__i386__)
        crashlog_hex(fd, "pc", (unsigned long)uc->uc_mcontext.gregs[REG_EIP]);
#else
        (void)uc;
#endif
        crashlog_puts(fd, "\nmaps:\n");
        int m = open("/proc/self/maps", O_RDONLY);
        if (m >= 0) { char buf[1024]; ssize_t n; while ((n = read(m, buf, sizeof buf)) > 0) if (write(fd, buf, (size_t)n) < 0) break; close(m); }
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void crashlog_install(const char *name)
{
    const char *p = getenv("R36_CRASHLOG");
    if (p) snprintf(crashlog_path, sizeof crashlog_path, "%s", p);
    else snprintf(crashlog_path, sizeof crashlog_path, "/mnt/sdcard/cubegm/logs/crash_%s.txt", name);
    stack_t ss; ss.ss_sp = crashlog_altstack; ss.ss_size = sizeof crashlog_altstack; ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crashlog_handler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}
#endif
