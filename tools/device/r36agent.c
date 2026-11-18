/* r36agent: a small file and command service for the USB network link (no SD card moves while developing).
 * Listens on TCP port 7777 (all interfaces; the console has only loopback and the USB link). One request per
 * connection; the first line is the request, the second line its argument:
 *   PING\n                         -> "PONG <uptime>\n"
 *   EXEC\n<shell command>\n        -> the command's stdout and stderr, then "\n#R36EXIT <status>\n"
 *   PUT <size> <mode octal>\n<path>\n<size bytes> -> "OK\n" or "ERR <text>\n" (written to <path>.r36tmp, then renamed)
 *   GET\n<path>\n                  -> "SIZE <n>\n" and the bytes, or "ERR <text>\n"
 * Usage: r36agent [port]. It runs until killed.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <termios.h>

static int read_line(int fd, char *buf, int max)
{
    int n = 0;
    while (n < max - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = 0;
    if (n && buf[n - 1] == '\r') buf[--n] = 0;
    return n;
}

static int write_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        c += w; n -= (size_t)w;
    }
    return 0;
}

static void reply(int fd, const char *fmt, const char *a)
{
    char b[600]; snprintf(b, sizeof b, fmt, a); write_all(fd, b, strlen(b));
}

static void do_exec(int fd, const char *cmd)
{
    int p[2];
    if (pipe(p)) { reply(fd, "ERR pipe %s\n", strerror(errno)); return; }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 1); dup2(p[1], 2); close(p[0]); close(p[1]); close(fd);
        int nul = open("/dev/null", O_RDONLY); if (nul >= 0) { dup2(nul, 0); close(nul); }
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    char buf[16384];
    ssize_t r;
    while ((r = read(p[0], buf, sizeof buf)) > 0) if (write_all(fd, buf, (size_t)r)) break;
    close(p[0]);
    int st = 0; waitpid(pid, &st, 0);
    char t[64]; snprintf(t, sizeof t, "\n#R36EXIT %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    write_all(fd, t, strlen(t));
}

static void do_put(int fd, long size, int mode, const char *path)
{
    char tmp[600]; snprintf(tmp, sizeof tmp, "%s.r36tmp", path);
    int o = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0644);
    if (o < 0) { reply(fd, "ERR open %s\n", strerror(errno)); return; }
    char buf[65536];
    long left = size;
    while (left > 0) {
        ssize_t r = read(fd, buf, left > (long)sizeof buf ? sizeof buf : (size_t)left);
        if (r <= 0) { close(o); unlink(tmp); reply(fd, "ERR short %s\n", "read"); return; }
        if (write_all(o, buf, (size_t)r)) { close(o); unlink(tmp); reply(fd, "ERR write %s\n", strerror(errno)); return; }
        left -= r;
    }
    fsync(o); close(o);
    if (mode) chmod(tmp, mode);
    if (rename(tmp, path)) { unlink(tmp); reply(fd, "ERR rename %s\n", strerror(errno)); return; }
    write_all(fd, "OK\n", 3);
}

static void do_get(int fd, const char *path)
{
    int i = open(path, O_RDONLY);
    if (i < 0) { reply(fd, "ERR open %s\n", strerror(errno)); return; }
    struct stat st; fstat(i, &st);
    char h[64]; snprintf(h, sizeof h, "SIZE %lld\n", (long long)st.st_size);
    write_all(fd, h, strlen(h));
    char buf[65536]; ssize_t r;
    while ((r = read(i, buf, sizeof buf)) > 0) if (write_all(fd, buf, (size_t)r)) break;
    close(i);
    /* a trailing newline ends the transfer with a short USB packet: without it the host driver holds the last full
       packets of a transfer whose size is a multiple of 512 until more data arrives */
    write_all(fd, "\n", 1);
}

static void serve_line(int fd, const char *l1)
{
    char l2[4096];
    if (!strcmp(l1, "PING")) {
        char u[64] = "?"; int f = open("/proc/uptime", O_RDONLY);
        if (f >= 0) { ssize_t n = read(f, u, sizeof u - 1); if (n > 0) u[n] = 0; close(f); char *e = strchr(u, ' '); if (e) *e = 0; }
        reply(fd, "PONG %s\n", u);
        return;
    }
    if (read_line(fd, l2, sizeof l2) < 0) return;
    if (!strcmp(l1, "EXEC")) do_exec(fd, l2);
    else if (!strncmp(l1, "PUT ", 4)) { long size = 0; int mode = 0; sscanf(l1 + 4, "%ld %o", &size, &mode); do_put(fd, size, mode, l2); }
    else if (!strcmp(l1, "GET")) do_get(fd, l2);
    else reply(fd, "ERR unknown request %s\n", l1);
}

static void serve(int fd)
{
    char l1[256];
    if (read_line(fd, l1, sizeof l1) < 0) return;
    serve_line(fd, l1);
}

/* serial mode: the same requests one after another on a tty (the USB serial function, /dev/ttyGS0), raw mode */
static int serve_tty(const char *dev)
{
    for (;;) {
        int fd = open(dev, O_RDWR | O_NOCTTY);
        if (fd < 0) { sleep(1); continue; }
        struct termios t;
        if (tcgetattr(fd, &t) == 0) { cfmakeraw(&t); t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0; tcsetattr(fd, TCSANOW, &t); }
        for (;;) {
            char l1[256];
            int n = read_line(fd, l1, sizeof l1);
            if (n < 0) break;               /* cable gone: reopen */
            if (n == 0) continue;           /* empty lines resynchronise */
            serve_line(fd, l1);
        }
        close(fd);
        sleep(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    if (argc > 2 && !strcmp(argv[1], "tty")) return serve_tty(argv[2]);
    int port = argc > 1 ? atoi(argv[1]) : 7777;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) { perror("r36agent"); return 1; }
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        pid_t pid = fork();
        if (pid == 0) { close(s); serve(c); shutdown(c, SHUT_RDWR); close(c); _exit(0); }
        close(c);
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }
    return 0;
}
