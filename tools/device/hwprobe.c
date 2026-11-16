/* Read-only hardware probe for the handheld: ELF auxv, sysconf, framebuffer ioctls, memory limit, SD speed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <linux/fb.h>
#include <signal.h>
#include <setjmp.h>

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static void auxv(void)
{
    printf("== ELF auxiliary vector\n");
    printf("AT_PLATFORM=%s\n", (const char *)getauxval(AT_PLATFORM));
    printf("AT_BASE_PLATFORM=%s\n", getauxval(AT_BASE_PLATFORM) ? (const char *)getauxval(AT_BASE_PLATFORM) : "(none)");
    printf("AT_HWCAP=0x%lx AT_HWCAP2=0x%lx\n", getauxval(AT_HWCAP), getauxval(AT_HWCAP2));
    printf("AT_PAGESZ=%lu AT_CLKTCK=%lu AT_SECURE=%lu\n", getauxval(AT_PAGESZ), getauxval(AT_CLKTCK), getauxval(AT_SECURE));
}

static void conf(void)
{
    struct utsname u; cpu_set_t set; struct rlimit rl; int i, n = 0;
    printf("== sysconf / limits\n");
    uname(&u);
    printf("uname: %s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
    printf("page size %ld, phys pages %ld (%.1f MB), avail pages %ld (%.1f MB)\n", sysconf(_SC_PAGESIZE), sysconf(_SC_PHYS_PAGES),
           sysconf(_SC_PHYS_PAGES) * (double)sysconf(_SC_PAGESIZE) / 1048576.0, sysconf(_SC_AVPHYS_PAGES), sysconf(_SC_AVPHYS_PAGES) * (double)sysconf(_SC_PAGESIZE) / 1048576.0);
    printf("cpus configured %ld online %ld, clock ticks %ld\n", sysconf(_SC_NPROCESSORS_CONF), sysconf(_SC_NPROCESSORS_ONLN), sysconf(_SC_CLK_TCK));
#ifdef _SC_LEVEL1_DCACHE_SIZE
    printf("L1d size %ld line %ld assoc %ld | L1i size %ld | L2 size %ld line %ld\n", sysconf(_SC_LEVEL1_DCACHE_SIZE), sysconf(_SC_LEVEL1_DCACHE_LINESIZE),
           sysconf(_SC_LEVEL1_DCACHE_ASSOC), sysconf(_SC_LEVEL1_ICACHE_SIZE), sysconf(_SC_LEVEL2_CACHE_SIZE), sysconf(_SC_LEVEL2_CACHE_LINESIZE));
#endif
    if (sched_getaffinity(0, sizeof(set), &set) == 0) { for (i = 0; i < CPU_SETSIZE; i++) if (CPU_ISSET(i, &set)) n++; printf("schedulable cpus (affinity): %d\n", n); }
    if (getrlimit(RLIMIT_AS, &rl) == 0) printf("RLIMIT_AS cur=%ld max=%ld\n", (long)rl.rlim_cur, (long)rl.rlim_max);
    if (getrlimit(RLIMIT_STACK, &rl) == 0) printf("RLIMIT_STACK cur=%ld\n", (long)rl.rlim_cur);
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) printf("RLIMIT_NOFILE cur=%ld\n", (long)rl.rlim_cur);
    printf("sizeof(long)=%zu sizeof(void*)=%zu sizeof(double)=%zu\n", sizeof(long), sizeof(void *), sizeof(double));
}

static void framebuffers(void)
{
    const char *devs[] = {"/dev/fb0", "/dev/fb1", "/dev/fb2", "/dev/graphics/fb0", 0};
    int i;
    printf("== framebuffer devices\n");
    for (i = 0; devs[i]; i++) {
        struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
        int fd = open(devs[i], O_RDONLY);
        if (fd < 0) { printf("%s: %s\n", devs[i], strerror(errno)); continue; }
        memset(&v, 0, sizeof(v)); memset(&f, 0, sizeof(f));
        if (ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0)
            printf("%s fix: id='%.16s' smem_len=%u line_length=%u type=%u visual=%u accel=%u\n", devs[i], f.id, f.smem_len, f.line_length, f.type, f.visual, f.accel);
        if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0)
            printf("%s var: %ux%u virtual %ux%u offset %u,%u bpp %u grayscale %u rotate %u | R off %u len %u | G off %u len %u | B off %u len %u | A off %u len %u | pixclock %u | margins l%u r%u u%u lo%u | sync h%u v%u | width_mm %u height_mm %u\n",
                   devs[i], v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset, v.bits_per_pixel, v.grayscale, v.rotate,
                   v.red.offset, v.red.length, v.green.offset, v.green.length, v.blue.offset, v.blue.length, v.transp.offset, v.transp.length,
                   v.pixclock, v.left_margin, v.right_margin, v.upper_margin, v.lower_margin, v.hsync_len, v.vsync_len, v.width, v.height);
        close(fd);
    }
}

static void memlimit(void)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    char line[256]; long avail_kb = 0, total_kb = 0, free_kb = 0;
    size_t step = 4u << 20, got = 0, cap; void **blocks; int nb = 0;
    printf("== memory a normal process can really use\n");
    if (fp) { while (fgets(line, sizeof(line), fp)) { sscanf(line, "MemTotal: %ld", &total_kb); sscanf(line, "MemFree: %ld", &free_kb); sscanf(line, "MemAvailable: %ld", &avail_kb); } fclose(fp); }
    if (!avail_kb) avail_kb = free_kb;
    printf("MemTotal %ld kB, MemFree %ld kB, MemAvailable %ld kB\n", total_kb, free_kb, avail_kb);
    cap = (size_t)(avail_kb * 1024.0 * 0.6);            /* never push the system toward the OOM killer */
    blocks = (void **)calloc(512, sizeof(void *));
    while (got + step <= cap && nb < 512) {
        void *p = malloc(step);
        if (!p) break;
        memset(p, 0xA5, step);
        blocks[nb++] = p; got += step;
    }
    printf("allocated and touched %zu MB before stopping (cap 60%% of available = %zu MB)\n", got >> 20, cap >> 20);
    while (nb) free(blocks[--nb]);
    free(blocks);
}

static void sdspeed(const char *dir)
{
    char path[512]; int fd, i; size_t n = 1u << 20; char *buf = (char *)malloc(n); double t; const int mb = 24;
    printf("== storage speed\n");
    snprintf(path, sizeof(path), "%s/probe_speed.tmp", dir);
    memset(buf, 0x5A, n);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        t = now_s();
        for (i = 0; i < mb; i++) if (write(fd, buf, n) != (ssize_t)n) break;
        fsync(fd); close(fd);
        t = now_s() - t;
        printf("write %d MB + fsync: %.2f s = %.1f MB/s (%s)\n", mb, t, mb / t, dir);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            posix_fadvise(fd, 0, 0, 4 /* POSIX_FADV_DONTNEED */);
            t = now_s();
            for (i = 0; i < mb; i++) if (read(fd, buf, n) != (ssize_t)n) break;
            t = now_s() - t; close(fd);
            printf("read back %d MB: %.2f s = %.1f MB/s (may be partly cached)\n", mb, t, mb / t);
        }
        unlink(path);
    } else printf("cannot write in %s: %s\n", dir, strerror(errno));
    {   /* raw card read, read-only */
        const char *raw[] = {"/dev/mmcblk0", "/dev/mmcblk1", 0};
        for (i = 0; raw[i]; i++) {
            int r = open(raw[i], O_RDONLY);
            if (r < 0) { printf("%s: %s\n", raw[i], strerror(errno)); continue; }
            lseek(r, (off_t)64 << 20, SEEK_SET);
            t = now_s();
            for (n = 0; n < 48; n++) if (read(r, buf, 1u << 20) != (ssize_t)(1u << 20)) break;
            t = now_s() - t;
            printf("%s raw sequential read of %zu MB: %.1f MB/s\n", raw[i], n, n / t);
            close(r);
        }
    }
    free(buf);
}

static sigjmp_buf jb;
static void on_ill(int sig) { (void)sig; siglongjmp(jb, 1); }

/* executes single instructions to see which ISA features the CPU really implements (SIGILL = absent) */
static void isa_tests(void)
{
    struct sigaction sa; uint32_t a = 0x01020304u, b = 0x10203040u, r = 0; int k;
    const char *names[] = {"MIPS32r2 ext (bit-field extract)", "MIPS32r2 wsbh (byte swap)", "MIPS32r2 seb", "MIPS32 clz", "MIPS32r2 rotr",
                           "DSP rev1 addu.qb (4x8-bit add)", "DSP rev1 muleu_s.ph.qbl", "DSP rev2 mul.ph (2x16-bit multiply)", "DSP rev2 addu.ph", "DSP rev2 precrq.qb.ph", "FPU double add.d", "FPU single madd.s (MIPS32r2 fused)", "FPU sqrt.d", 0};
    printf("== which instructions this CPU executes (evidence for ISA level and DSP ASE)\n");
    memset(&sa, 0, sizeof(sa)); sa.sa_handler = on_ill; sigaction(SIGILL, &sa, 0);
    for (k = 0; names[k]; k++) {
        int ok;
        if (sigsetjmp(jb, 1) == 0) {
            r = 0;
            switch (k) {
            case 0: __asm__ volatile("ext %0, %1, 4, 8" : "=r"(r) : "r"(a)); break;
            case 1: __asm__ volatile("wsbh %0, %1" : "=r"(r) : "r"(a)); break;
            case 2: __asm__ volatile("seb %0, %1" : "=r"(r) : "r"(a)); break;
            case 3: __asm__ volatile("clz %0, %1" : "=r"(r) : "r"(a)); break;
            case 4: __asm__ volatile("rotr %0, %1, 8" : "=r"(r) : "r"(a)); break;
            case 5: __asm__ volatile(".set push\n.set dsp\naddu.qb %0, %1, %2\n.set pop" : "=r"(r) : "r"(a), "r"(b)); break;
            case 6: __asm__ volatile(".set push\n.set dsp\nmuleu_s.ph.qbl %0, %1, %2\n.set pop" : "=r"(r) : "r"(a), "r"(b)); break;
            case 7: __asm__ volatile(".set push\n.set dspr2\nmul.ph %0, %1, %2\n.set pop" : "=r"(r) : "r"(a), "r"(b)); break;
            case 8: __asm__ volatile(".set push\n.set dspr2\naddu.ph %0, %1, %2\n.set pop" : "=r"(r) : "r"(a), "r"(b)); break;
            case 9: __asm__ volatile(".set push\n.set dspr2\nprecrq.qb.ph %0, %1, %2\n.set pop" : "=r"(r) : "r"(a), "r"(b)); break;
            case 10: { double x = 1.5, y = 2.5, z; __asm__ volatile("add.d %0, %1, %2" : "=f"(z) : "f"(x), "f"(y)); r = (uint32_t)z; } break;
            case 11: { float x = 1.5f, y = 2.5f, z = 3.0f, o; __asm__ volatile("madd.s %0, %1, %2, %3" : "=f"(o) : "f"(z), "f"(x), "f"(y)); r = (uint32_t)o; } break;
            case 12: { double x = 9.0, z; __asm__ volatile("sqrt.d %0, %1" : "=f"(z) : "f"(x)); r = (uint32_t)z; } break;
            }
            ok = 1;
        } else ok = 0;
        printf("%-44s %s%s\n", names[k], ok ? "EXECUTES" : "ILLEGAL INSTRUCTION", ok ? "" : " (not implemented)");
        fflush(stdout);
    }
    signal(SIGILL, SIG_DFL);
}

/* ns per instruction for dependent chains; if FP ops cost ~100x an integer add, the kernel is emulating the FPU */
static void fp_timing(void)
{
    uint32_t xi = 1; double x = 1.0, y = 1e-9, t; float xf = 1.0f, yf = 1e-6f; int i;
    const int ni = 4000000, nf = 200000;
    double ti, tad, tmd, tdd, tsd, tas;
    printf("== floating point speed vs integer speed (direct evidence for hardware FPU or kernel emulation)\n");
    t = now_s(); for (i = 0; i < ni; i++) __asm__ volatile("addu %0, %0, %1" : "+r"(xi) : "r"(i)); ti = (now_s() - t) / ni * 1e9;
    t = now_s(); for (i = 0; i < nf; i++) __asm__ volatile("add.d %0, %0, %1" : "+f"(x) : "f"(y)); tad = (now_s() - t) / nf * 1e9;
    t = now_s(); for (i = 0; i < nf; i++) __asm__ volatile("mul.d %0, %0, %1" : "+f"(x) : "f"(y)); tmd = (now_s() - t) / nf * 1e9;
    x = 1e300;
    t = now_s(); for (i = 0; i < nf; i++) __asm__ volatile("div.d %0, %0, %1" : "+f"(x) : "f"(y)); tdd = (now_s() - t) / nf * 1e9;
    x = 2.0;
    t = now_s(); for (i = 0; i < nf; i++) __asm__ volatile("sqrt.d %0, %0" : "+f"(x)); tsd = (now_s() - t) / nf * 1e9;
    t = now_s(); for (i = 0; i < nf; i++) __asm__ volatile("add.s %0, %0, %1" : "+f"(xf) : "f"(yf)); tas = (now_s() - t) / nf * 1e9;
    printf("integer addu: %.2f ns | add.d: %.2f ns | mul.d: %.2f ns | div.d: %.2f ns | sqrt.d: %.2f ns | add.s: %.2f ns\n", ti, tad, tmd, tdd, tsd, tas);
    printf("add.d costs %.1fx an integer add -> %s\n", tad / (ti > 0 ? ti : 1),
           tad > ti * 40 ? "FPU IS EMULATED IN SOFTWARE (each FP instruction traps to the kernel)" : "hardware FPU");
    (void)xi;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/mnt/sdcard";
    if (argc > 2 && strcmp(argv[2], "fp") == 0) { fp_timing(); return 0; }
    auxv(); conf(); isa_tests(); fp_timing(); framebuffers(); memlimit(); sdspeed(dir);
    return 0;
}
