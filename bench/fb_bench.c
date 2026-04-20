#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static double now_monotonic(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void fill_pattern(uint16_t *dst, int width, int height, int stride_pixels,
                         unsigned frame) {
  const uint16_t bg_a = rgb565(0x11, 0x1e, 0x2a);
  const uint16_t bg_b = rgb565(0xe0, 0x4f, 0x5f);
  const uint16_t fg_a = rgb565(0xf3, 0xd3, 0x4a);
  const uint16_t fg_b = rgb565(0x4e, 0xcd, 0xc4);
  const int bar_w = width / 5;
  const int bar_h = height / 5;
  const int x = (frame * 17) % (width + bar_w) - bar_w;
  const int y = (frame * 11) % (height + bar_h) - bar_h;

  for (int row = 0; row < height; ++row) {
    uint16_t *line = dst + row * stride_pixels;
    const int band = ((row / 16) + frame) & 1;
    const uint16_t bg = band ? bg_a : bg_b;
    for (int col = 0; col < width; ++col) {
      line[col] = bg;
    }
  }

  for (int row = 0; row < bar_h; ++row) {
    const int py = y + row;
    if (py < 0 || py >= height) continue;
    uint16_t *line = dst + py * stride_pixels;
    for (int col = 0; col < bar_w; ++col) {
      const int px = x + col;
      if (px < 0 || px >= width) continue;
      line[px] = ((row / 8) ^ (col / 8)) & 1 ? fg_a : fg_b;
    }
  }
}

int main(int argc, char **argv) {
  const char *fb_path = argc > 1 ? argv[1] : "/dev/fb1";
  double duration = argc > 2 ? atof(argv[2]) : 8.0;
  int fd = open(fb_path, O_RDWR);
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  size_t map_len;
  uint8_t *map;
  double start, next_report, end;
  unsigned frames = 0;
  unsigned total_frames = 0;

  if (fd < 0) {
    fprintf(stderr, "open(%s): %s\n", fb_path, strerror(errno));
    return 1;
  }

  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
    fprintf(stderr, "FBIOGET_VSCREENINFO: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
    fprintf(stderr, "FBIOGET_FSCREENINFO: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  if (vinfo.bits_per_pixel != 16) {
    fprintf(stderr, "expected 16bpp framebuffer, got %u\n", vinfo.bits_per_pixel);
    close(fd);
    return 1;
  }

  map_len = finfo.line_length * vinfo.yres;
  map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    fprintf(stderr, "mmap: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  printf("fb=%s width=%u height=%u stride=%u bytes duration=%.1fs\n",
         fb_path, vinfo.xres, vinfo.yres, finfo.line_length, duration);
  fflush(stdout);

  start = now_monotonic();
  next_report = start + 1.0;
  end = start + duration;

  while (now_monotonic() < end) {
    fill_pattern((uint16_t *)map, (int)vinfo.xres, (int)vinfo.yres,
                 (int)(finfo.line_length / 2), total_frames);
    msync(map, map_len, MS_SYNC);
    frames++;
    total_frames++;

    if (now_monotonic() >= next_report) {
      const double elapsed = now_monotonic() - start;
      printf("elapsed=%.2fs loop_fps=%.2f\n", elapsed, total_frames / elapsed);
      fflush(stdout);
      next_report += 1.0;
    }
  }

  {
    const double elapsed = now_monotonic() - start;
    const double full_frame_mbit = (double)(vinfo.xres * vinfo.yres * vinfo.bits_per_pixel) / 1000000.0;
    printf("summary: frames=%u elapsed=%.3fs loop_fps=%.2f frame_mbit=%.3f estimated_required_mbit_s=%.2f\n",
           total_frames, elapsed, total_frames / elapsed, full_frame_mbit,
           full_frame_mbit * (total_frames / elapsed));
  }

  munmap(map, map_len);
  close(fd);
  return 0;
}
