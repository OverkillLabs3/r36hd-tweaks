/* direct present: hand a finished RGB565 frame straight to the display driver picoarch loaded
 * (video_driver_disp_frame, any size), then tell picoarch "same frame" with video_cb(NULL, ...), so picoarch does not
 * copy and scale it again. Off switch: <port dir>/direct_off.txt.
 * Usage: if (direct_present(px, w, h)) video_cb(NULL, w, h, w * 2); else video_cb(px, w, h, w * 2);
 * Call direct_present_setup(port_dir, "tag") once first. */
#ifndef DIRECT_PRESENT_H
#define DIRECT_PRESENT_H
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)   /* glibc value; dlfcn.h only defines it with _GNU_SOURCE */
#endif

typedef void (*direct_disp_frame_fn)(const void *pixels, int w, int h, int pitch);
static direct_disp_frame_fn direct_fn;
static int direct_state = -1, direct_frames;
static char direct_dir[512] = ".";
static const char *direct_tag = "core";

static void direct_present_setup(const char *port_dir, const char *tag)
{
    if (port_dir) snprintf(direct_dir, sizeof direct_dir, "%s", port_dir);
    if (tag) direct_tag = tag;
}

static void direct_init(void)
{
    direct_state = 0;
    char off[600]; struct stat st;
    snprintf(off, sizeof off, "%s/direct_off.txt", direct_dir);
    if (stat(off, &st) == 0) { fprintf(stderr, "[%s] direct present: off (direct_off.txt)\n", direct_tag); return; }
    static const char *names[] = { "/mnt/sdcard/cubegm/driver.so", "/mnt/sdcard/cubegm/driver_r36sx.so",
        "/mnt/sdcard/cubegm/driver_r36sx27.so", "/mnt/sdcard/cubegm/driver_sf3000.so",
        "/mnt/sdcard/cubegm/driver_sf3500.so", "/mnt/sdcard/cubegm/driver_gb350.so" };
    direct_fn = (direct_disp_frame_fn)dlsym(RTLD_DEFAULT, "video_driver_disp_frame");
    for (unsigned i = 0; !direct_fn && i < sizeof names / sizeof names[0]; i++) {
        void *h = dlopen(names[i], RTLD_NOW | RTLD_NOLOAD);
        if (h) direct_fn = (direct_disp_frame_fn)dlsym(h, "video_driver_disp_frame");
    }
    direct_state = direct_fn ? 1 : 0;
    fprintf(stderr, "[%s] direct present: %s\n", direct_tag, direct_fn ? "on" : "off (no display driver loaded)");
}

/* 1 = shown by the driver (send video_cb(NULL, ...)); 0 = send the frame to picoarch as usual */
static int direct_present(const uint16_t *f, int w, int h)
{
    if (direct_state < 0) direct_init();
    if (direct_state <= 0) return 0;
    if (direct_frames < 30) { direct_frames++; return 0; }   /* let picoarch set the display up first */
    direct_fn(f, w, h, w * 2);
    return 1;
}
#endif
