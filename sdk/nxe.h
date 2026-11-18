/* NegxusOS SDK - contrato entre o kernel e os programas .nxe */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define NX_API_VERSION 1

/* ------------------------------------------------------------------ */
/* Formato executavel NXE ("NXE1")                                      */
/* ------------------------------------------------------------------ */
#define NXE_MAGIC     0x3145584Eu          /* 'N','X','E','1' */
#define NXE_F_DESKTOP 1                    /* mostrar atalho na area de trabalho */
#define NXE_F_HIDDEN  2                    /* ocultar do menu Iniciar */
#define NXE_HDR_SIZE  68
#define NXE_ICON_PX   48

typedef struct {
    uint32_t magic;
    uint16_t version, flags;
    uint32_t entry;                        /* offset da funcao nxe_main dentro da imagem */
    uint32_t image_off, image_size, mem_size;
    uint32_t reloc_off, reloc_count;       /* tabela de uint32: offsets de ponteiros de 64 bits */
    uint32_t order;                        /* ordem no menu */
    char     name[32];
} __attribute__((packed)) nxe_header;      /* 68 bytes; depois vem o icone 48x48 ARGB */

/* ------------------------------------------------------------------ */
/* Graficos                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t *px;
    int w, h, pitch;                       /* pitch em pixels */
    int cx0, cy0, cx1, cy1;                /* retangulo de recorte [cx0,cx1) x [cy0,cy1) */
} nx_canvas;

#define RGB(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))

enum { NF_UI = 0, NF_UIB, NF_SM, NF_H1, NF_H2, NF_H3, NF_MONO, NF_COUNT };

/* ------------------------------------------------------------------ */
/* Entrada                                                              */
/* ------------------------------------------------------------------ */
enum {
    NK_BACKSPACE = 8, NK_TAB = 9, NK_ENTER = 13, NK_ESC = 27,
    NK_UP = 0x100, NK_DOWN, NK_LEFT, NK_RIGHT, NK_HOME, NK_END,
    NK_PGUP, NK_PGDN, NK_INSERT, NK_DELETE,
    NK_F1 = 0x120, NK_F2, NK_F3, NK_F4, NK_F5, NK_F6, NK_F7, NK_F8, NK_F9, NK_F10, NK_F11, NK_F12
};
#define NM_SHIFT 1
#define NM_CTRL  2
#define NM_ALT   4
#define NM_ALTGR 8
#define NM_CAPS  16

enum { NMOUSE_MOVE = 0, NMOUSE_DOWN, NMOUSE_UP, NMOUSE_WHEEL, NMOUSE_DBL };
#define NB_LEFT  1
#define NB_RIGHT 2
#define NB_MID   4

enum { NC_ARROW = 0, NC_IBEAM, NC_HAND, NC_SIZEH, NC_SIZEV, NC_SIZEBR, NC_CROSS };

#define NW_FIXED 1                         /* janela sem redimensionar */

typedef struct nx_win nx_win;
struct nx_win {
    nx_canvas cv;                          /* area cliente (desenhe aqui) */
    void *user;                            /* livre para o aplicativo */
    int cursor;                            /* NC_*: formato do cursor sobre a janela */
    void (*on_paint)(nx_win *);            /* repintar tudo */
    void (*on_key)(nx_win *, int key, int mods);
    void (*on_mouse)(nx_win *, int type, int x, int y, int buttons, int arg);
    void (*on_timer)(nx_win *);
    void (*on_resize)(nx_win *);
    int  (*on_close)(nx_win *);            /* retorne 1 para permitir fechar */
};

/* ------------------------------------------------------------------ */
/* Tipos de servicos                                                    */
/* ------------------------------------------------------------------ */
typedef struct { char name[60]; uint32_t size; uint8_t is_dir, pad[3]; } nx_dirent;
typedef struct { uint32_t size; uint8_t is_dir; } nx_stat;
typedef struct { int year, month, day, hour, min, sec; } nx_time;
typedef struct {
    uint32_t ram_mb; int sw, sh; char cpu[52];
    uint32_t uptime_s;
} nx_sysinfo;

typedef struct nx_api {
    uint32_t version;
    /* memoria (liberada automaticamente quando o app termina) */
    void *(*malloc)(size_t); void (*free)(void *); void *(*realloc)(void *, size_t);
    /* janelas */
    nx_win *(*win_create)(const char *title, int cw, int ch, int flags);
    void (*win_title)(nx_win *, const char *);
    void (*win_invalidate)(nx_win *);
    void (*win_invalidate_rect)(nx_win *, int x, int y, int w, int h);
    void (*win_close)(nx_win *);
    void (*win_timer)(nx_win *, int ms);
    void (*win_size)(nx_win *, int cw, int ch);
    void (*win_minsize)(nx_win *, int cw, int ch);
    /* desenho */
    void (*fill)(nx_canvas *, int x, int y, int w, int h, uint32_t c);
    void (*fill_a)(nx_canvas *, int x, int y, int w, int h, uint32_t c, int alpha);
    void (*rect)(nx_canvas *, int x, int y, int w, int h, uint32_t c);
    void (*rrect)(nx_canvas *, int x, int y, int w, int h, int r, uint32_t c);
    void (*rrect_line)(nx_canvas *, int x, int y, int w, int h, int r, uint32_t c);
    void (*line)(nx_canvas *, int x0, int y0, int x1, int y1, uint32_t c);
    void (*circle)(nx_canvas *, int cx, int cy, int r, uint32_t c);
    void (*text)(nx_canvas *, int x, int y, const char *s, int font, uint32_t c);
    int  (*text_w)(const char *s, int font);
    int  (*text_h)(int font);
    void (*blit)(nx_canvas *dst, int x, int y, const nx_canvas *src, int sx, int sy, int w, int h);
    void (*icon)(nx_canvas *, const uint32_t *icon48, int x, int y, int size);
    /* arquivos */
    int (*fs_read)(const char *path, void *buf, int max);
    int (*fs_write)(const char *path, const void *buf, int n);
    int (*fs_list)(const char *path, nx_dirent *out, int max);
    int (*fs_stat)(const char *path, nx_stat *st);
    int (*fs_mkdir)(const char *path);
    int (*fs_remove)(const char *path);
    int (*fs_rename)(const char *from, const char *to);
    /* sistema */
    uint32_t (*ticks)(void);               /* ms desde o boot */
    void (*time)(nx_time *);
    uint32_t (*rand)(void);
    int (*exec)(const char *path, const char *arg);
    int (*open)(const char *path);         /* abre pelo tipo de arquivo */
    void (*clip_set)(const char *s, int n);
    int (*clip_get)(char *buf, int max);
    void (*sysinfo)(nx_sysinfo *);
    void (*power)(int reboot);
    void (*log)(const char *s);
    int (*app_icon)(const char *nxe_path, uint32_t *out48);
    void (*set_wallpaper)(int id);          /* 0 azul, 1 cinza, 2 verde-azulado, 3 roxo */
} nx_api;

typedef int (*nxe_main_fn)(const nx_api *api, const char *arg);

/* ------------------------------------------------------------------ */
/* libc minima (implementada em common/str.c, usada por kernel e apps)  */
/* ------------------------------------------------------------------ */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strncat(char *d, const char *s, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
int    atoi(const char *s);
int    nx_snprintf(char *buf, size_t n, const char *fmt, ...);
int    nx_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
