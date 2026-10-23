#include "OfeRgbTileCopy.h"
// Compile/disassemble only: keep the hot helper inline in an IRAM caller.
extern "C" __attribute__((noinline, section(".iram1"))) void ofe_rgb_copy_codegen(
    uint16_t* framebuffer, const uint16_t* pixels, int x, int y, int w, int h) {
  ofe_rgb_tile::copy180Direct(framebuffer, 800, pixels, x, y, w, h, 0);
}
