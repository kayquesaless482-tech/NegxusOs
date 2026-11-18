/* Terminal do NegxusOS: shell simples com comandos internos */
#include "../sdk/apputil.h"
#define COLS 100
#define MAXL 300
#define PAD 10

static nx_win *win;
static char lines[MAXL][COLS];
static int nlines;
static char inp[COLS];
static int ilen;
static char cwd[128] = "/";
static int lh, cw_;

static void push_line(const char *s) {
    if (nlines == MAXL) {                         /* descarta a linha mais antiga */
        for (int i = 1; i < MAXL; i++) memcpy(lines[i - 1], lines[i], COLS);
        nlines--;
    }
    strncpy(lines[nlines], s, COLS - 1);
    lines[nlines][COLS - 1] = 0;
    nlines++;
}
static void out(const char *s) {
    /* quebra em linhas no '\n' e em COLS-1 caracteres */
    char tmp[COLS];
    while (*s) {
        int k = 0;
        while (*s && *s != '\n' && k < COLS - 1) tmp[k++] = *s++;
        tmp[k] = 0;
        push_line(tmp);
        if (*s == '\n') s++;
        else if (*s && k == COLS - 1) { /* continua a linha */ }
    }
}
static void outf(const char *fmt, const char *a, const char *b) {
    char t[400];
    nx_snprintf(t, sizeof t, fmt, a, b);
    out(t);
}

/* monta caminho absoluto a partir de cwd */
static void resolve(const char *p, char *dst, int cap) {
    if (!p || !*p) { strncpy(dst, cwd, cap - 1); dst[cap - 1] = 0; return; }
    char base[128];
    if (p[0] == '/') strncpy(base, "", sizeof base);
    else strncpy(base, cwd, sizeof base - 1);
    base[sizeof base - 1] = 0;
    char acc[256];
    strncpy(acc, base, sizeof acc - 1); acc[sizeof acc - 1] = 0;
    const char *s = p;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = s; while (*e && *e != '/') e++;
        int L = (int)(e - s);
        if (L == 1 && s[0] == '.') { /* nada */ }
        else if (L == 2 && s[0] == '.' && s[1] == '.') {
            char *slash = strrchr(acc, '/');
            if (slash && slash != acc) *slash = 0; else acc[0] = 0;
        } else {
            char comp[120]; if (L > 100) L = 100;
            memcpy(comp, s, L); comp[L] = 0;
            strcat(acc, "/"); strcat(acc, comp);
        }
        s = e;
    }
    if (!acc[0]) strcpy(acc, "/");
    strncpy(dst, acc, cap - 1); dst[cap - 1] = 0;
}

static void cmd_ls(const char *arg) {
    char path[256]; resolve(arg, path, sizeof path);
    nx_dirent ents[96];
    int n = A->fs_list(path, ents, 96);
    if (n < 0) { outf("ls: nao e uma pasta: %s", path, 0); return; }
    if (n == 0) { out("(pasta vazia)"); return; }
    for (int i = 0; i < n; i++) {
        char l[COLS];
        if (ents[i].is_dir) nx_snprintf(l, sizeof l, "%s/", ents[i].name);
        else nx_snprintf(l, sizeof l, "%-30s %u bytes", ents[i].name, ents[i].size);
        out(l);
    }
}
static void cmd_cat(const char *arg) {
    if (!arg || !*arg) { out("uso: cat <arquivo>"); return; }
    char path[256]; resolve(arg, path, sizeof path);
    nx_stat st;
    if (A->fs_stat(path, &st) < 0 || st.is_dir) { outf("cat: nao encontrado: %s", path, 0); return; }
    if (st.size > 16384) { out("cat: arquivo grande demais para o terminal"); return; }
    char *b = A->malloc(st.size + 1);
    if (!b) { out("cat: sem memoria"); return; }
    int n = A->fs_read(path, b, st.size);
    b[n > 0 ? n : 0] = 0;
    out(b);
    A->free(b);
}
static void cmd_cd(const char *arg) {
    char path[256]; resolve(arg && *arg ? arg : "/", path, sizeof path);
    nx_stat st;
    if (A->fs_stat(path, &st) < 0 || !st.is_dir) { outf("cd: nao e uma pasta: %s", path, 0); return; }
    strncpy(cwd, path, sizeof cwd - 1); cwd[sizeof cwd - 1] = 0;
}
static void cmd_mkdir(const char *arg) {
    if (!arg || !*arg) { out("uso: mkdir <pasta>"); return; }
    char path[256]; resolve(arg, path, sizeof path);
    if (A->fs_mkdir(path) < 0) outf("mkdir: falhou: %s", path, 0);
}
static void cmd_echo(const char *arg) { out(arg ? arg : ""); }

static void run(char *line) {
    char l[COLS]; strncpy(l, line, COLS - 1); l[COLS - 1] = 0;
    char *p = l; while (*p == ' ') p++;
    if (!*p) return;
    char *sp = p; while (*sp && *sp != ' ') sp++;
    char *arg = sp; if (*arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }
    if (!strcmp(p, "help")) {
        out("Comandos: help, ver, clear, pwd, cd <pasta>, ls [pasta], cat <arquivo>,");
        out("          mkdir <pasta>, echo <texto>, date, exit");
    } else if (!strcmp(p, "ver")) out("NegxusOS 1.0 (terminal)");
    else if (!strcmp(p, "clear")) nlines = 0;
    else if (!strcmp(p, "pwd")) out(cwd);
    else if (!strcmp(p, "cd")) cmd_cd(arg);
    else if (!strcmp(p, "ls")) cmd_ls(arg);
    else if (!strcmp(p, "cat")) cmd_cat(arg);
    else if (!strcmp(p, "mkdir")) cmd_mkdir(arg);
    else if (!strcmp(p, "echo")) cmd_echo(arg);
    else if (!strcmp(p, "date")) {
        nx_time t; A->time(&t);
        char d[64]; nx_snprintf(d, sizeof d, "%02d/%02d/%04d %02d:%02d:%02d", t.day, t.month, t.year, t.hour, t.min, t.sec);
        out(d);
    }
    else if (!strcmp(p, "exit")) A->win_close(win);
    else outf("comando nao encontrado: %s", p, 0);
}

static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    A->fill(c, 0, 0, c->w, c->h, RGB(12, 14, 20));
    int rows = (c->h - 2 * PAD) / lh - 1;     /* deixa a ultima linha para o prompt */
    int first = nlines - rows; if (first < 0) first = 0;
    int y = PAD;
    for (int i = first; i < nlines; i++) {
        A->text(c, PAD, y, lines[i], NF_MONO, RGB(204, 230, 204));
        y += lh;
    }
    char pr[COLS + 40];
    nx_snprintf(pr, sizeof pr, "%s> %s_", cwd, inp);
    A->text(c, PAD, c->h - PAD - lh + 2, pr, NF_MONO, RGB(120, 255, 150));
}
static void on_paint(nx_win *w) { draw(w); }
static void on_key(nx_win *w, int key, int mods) {
    (void)mods;
    if (key == NK_ENTER) {
        char echo[COLS + 40];
        nx_snprintf(echo, sizeof echo, "%s> %s", cwd, inp);
        push_line(echo);
        run(inp);
        ilen = 0; inp[0] = 0;
    } else if (key == NK_BACKSPACE) {
        if (ilen > 0) inp[--ilen] = 0;
    } else if (key == 'l' && (mods & NM_CTRL)) {
        nlines = 0;
    } else if (key >= 32 && key < 256 && ilen < COLS - 2) {
        inp[ilen++] = (char)key; inp[ilen] = 0;
    } else return;
    A->win_invalidate(w);
}
int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    win = A->win_create("Terminal", 760, 440, 0);
    if (!win) return -1;
    lh = A->text_h(NF_MONO) + 4;
    cw_ = A->text_w("M", NF_MONO);
    (void)cw_;
    win->on_paint = on_paint; win->on_key = on_key;
    out("NegxusOS Terminal - digite 'help' para ver os comandos.");
    A->win_invalidate(win);
    return 0;
}
