/* utilitarios comuns dos aplicativos */
#pragma once
#include "nxe.h"
extern const nx_api *A;
#define CW(s) A->text_w((s), NF_UI)
#define UIH   A->text_h(NF_UI)
static inline int nx_inside(int x, int y, int w, int h, int px, int py) {
    return px >= x && py >= y && px < x + w && py < y + h;
}
static inline int nx_iabs(int v) { return v < 0 ? -v : v; }
static inline int nx_imin(int a, int b) { return a < b ? a : b; }
static inline int nx_imax(int a, int b) { return a > b ? a : b; }
