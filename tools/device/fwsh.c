/* fwsh: send commands to the second core's firmware shell (/dev/virtuart) and print the replies.
 *   fwsh "<command>" ["<command>" ...]
 * Each command gets the reply that arrives until the shell is quiet for 1.2 s. One descriptor for read and write
 * (the virtuart driver keeps one global channel) and 512-byte reads (its read ignores the length asked for).
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

static void drain(int fd, int quiet_polls)
{
    char buf[1024];
    for (int idle = 0; idle < quiet_polls;) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 150) > 0 && (p.revents & POLLIN)) {
            ssize_t n = read(fd, buf, 512);
            if (n > 0) { fwrite(buf, 1, (size_t)n, stdout); idle = 0; continue; }
        }
        idle++;
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int fd = open("/dev/virtuart", O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("/dev/virtuart"); return 1; }
    drain(fd, 2);
    for (int i = 1; i < argc; i++) {
        printf("\n>>> %s\n", argv[i]);
        if (write(fd, argv[i], strlen(argv[i])) < 0 || write(fd, "\r\n", 2) < 0) { perror("write"); return 1; }
        drain(fd, 8);
    }
    printf("\n");
    close(fd);
    return 0;
}
