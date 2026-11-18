# NegxusOS - Documentação

Este documento cobre: como compilar o sistema, como transformar em `.img` e gravar em pendrive,
e como criar aplicativos `.nxe`.

## 1. Estado atual do sistema (versão 1.0)

Veja também o README.md. Esta seção resume o estado técnico.

Funciona no QEMU (testado): animação de boot, área de trabalho no estilo Windows 95, barra de
tarefas, menu Iniciar, Explorador de Arquivos (com colunas Nome/Tipo/Tamanho), Terminal,
Cubos (clone de Minecraft em voxel), Cobrinha, Campo Minado e Paint.

Renderização: por software (CPU), direto no framebuffer VESA obtido do BIOS. O Cubos usa
rasterização de triângulos com z-buffer em ponto fixo, sem FPU.

Não implementado nesta versão:
- Driver de GPU Intel com aproveitamento total da placa. Requer documentação de registradores
  (PRM) e testes em hardware real. Não foi feito, e um driver sem isso poderia travar a máquina.
- Execução em anel 3 (apps rodam no mesmo espaço de endereços do kernel).
- Rede, navegador, bloco de notas, gerenciador de tarefas, Doom, camada para `.exe`.
- FAT16 persistente implementado com driver ATA PIO (canal primário/master). FAT32, exFAT e NTFS ainda não são suportados; USB Mass Storage também não.
- Gamepad USB e pilha USB, instalador e live boot persistente.

Teste em hardware real: NÃO feito. Só foi testado no QEMU.

Controles do Cubos: W/A/S/D andar, setas girar, espaço subir, X descer, 1-4 escolher bloco,
clique esquerdo quebrar, clique direito colocar. Não há gravidade nem colisão (modo voo).

## 1a. Arquitetura do sistema

- boot/mbr.asm e boot/stage2.asm: MBR carrega o estágio 2 (LBA via INT 13h). O estágio 2
  lê o kernel em blocos (INT 13h para 0x10000, INT 15h/AH=87h para copiar acima de 1 MiB),
  escolhe o modo VESA e entra em modo longo (64 bits) com paginação identidade de 4 GiB.
- kernel/entry.asm: ponto de entrada; zera o BSS e chama kmain.
- kernel/arch.c: serial COM1, GDT, IDT, PIC, PIT (1 kHz), RTC, energia.
- kernel/mem.c: heap first-fit, com etiqueta de dono (cada app tem sua etiqueta, liberada junto).
- kernel/gfx.c: canvas 32 bits, retângulos, texto antialiased, ícones, blit.
- kernel/input.c: teclado PS/2 (layout ABNT2) e mouse PS/2 (com roda).
- kernel/disk.c: driver ATA PIO LBA28 para o dispositivo master do canal IDE primário.
- kernel/fs.c: FAT16 persistente com nomes longos VFAT; importa os aplicativos do initrd NXAR no primeiro boot.
- kernel/wm.c: janelas, foco, arrastar, redimensionar, eventos e repintura.
- kernel/desktop.c: papel de parede, barra de tarefas, menu Iniciar, ícones, cursor.
- kernel/nxe.c: carregador de .nxe (relocações) e tabela de API para os apps.
- apps/*.c: aplicativos, cada um compilado como .nxe independente.
- tools/mkinitrd.py: compila os apps, gera ícones e monta o initrd.
- tools/mkimage.py: monta a imagem de disco MBR com partição FAT16 de 64 MiB.
- tools/build_windows.py e build-windows.bat: compilação da imagem no Windows 7.

## 1a-2. Anel 3 (modo usuário)

Já existe em `kernel/ring3.c` e `kernel/arch.c`:
- GDT com segmentos de usuário: dados 0x1B e código 0x23 (DPL 3); TSS em 0x28.
- TSS com pilha de interrupção própria (`ring0_stack`), separada da pilha do kernel.
- Porta `int 0x80` com DPL 3 (`isr128` em `kernel/isr.asm`). `eax` = número da syscall,
  `rdi` = primeiro argumento, retorno em `rax`.
- Syscalls atuais: `1` = escreve um caractere na serial; `2` = sai do processo e volta ao kernel.
- Páginas de usuário: bits U/S definidos em PML4, PDPT e na entrada de 2 MiB (0x400000).
- Teste de boot: `ring3_selftest()` executa um código de usuário que imprime `RI` e sai.

O que falta para os apps rodarem em anel 3:
1. Mapear o código e a memória de cada app em páginas de usuário, e tirar o acesso do usuário
   à memória do kernel (hoje só a página do teste é de usuário).
2. Trocar as chamadas `A->funcao(...)` por stubs que executam `int 0x80`.
3. Copiar dados entre usuário e kernel na fronteira (ex.: buffers de tela e de arquivo).
4. Isolar a memória por processo (tabelas de páginas separadas).

## 1b. Limites técnicos conhecidos

- Sem FPU nem SSE no kernel e nos apps (compilados com -mgeneral-regs-only), por isso a
  matemática do Cubos é em ponto fixo.
- O kernel tem paginação identidade. Não há isolamento de memória entre apps.
- Não há driver Intel i915 nem camada Wine. Eles dependem de firmware proprietário (i915)
  e de um processo isolado com API Win32 (Wine), que ainda não existem.
- A resolução é a escolhida pelo bootloader (prefere 1280x720 quando disponível).
- O driver de teclado não trata todas as teclas de forma completa (ex.: teclado numérico
  parcial).


No Windows 7, veja `README-WINDOWS.md` e execute `build-windows.bat`.

## 2. Compilar o sistema

Requisitos (Ubuntu/Debian): `sudo apt install gcc nasm binutils python3 python3-pil qemu-system-x86`

```
make            # gera build/negxusos.img
make clean      # apaga build/
```

O `Makefile` faz, nesta ordem: compila o kernel (`kernel/*.c`, `kernel/*.asm`), compila os
aplicativos (`apps/*.c`) e gera os ícones (`tools/mkinitrd.py`), monta o initrd, e monta a imagem
de disco (`tools/mkimage.py`) com o bootloader (`boot/mbr.asm`, `boot/stage2.asm`).

Para testar no QEMU: `qemu-system-x86_64 -m 512 -drive file=build/negxusos.img,format=raw,if=ide,index=0`

## 3. Transformar em .img e gravar em pendrive

A imagem `negxusos.img` já é um disco completo (MBR + bootloader + kernel). Para gravar:

- Linux: confira o pendrive com `lsblk`, depois
  `sudo dd if=negxusos.img of=/dev/sdX bs=4M conv=fsync status=progress`
  (troque `sdX` pelo dispositivo certo; isso apaga o pendrive inteiro)
- Windows: use o Rufus, selecione a imagem e escolha o modo "Imagem DD" (não "ISO")
- Teste: escolha o pendrive como primeiro dispositivo de boot na BIOS/UEFI (modo Legacy/CSM)

Observação: a imagem contém o MBR, o kernel e uma partição FAT16 de 64 MiB. Ela inicia em BIOS Legacy. Em UEFI puro pode não iniciar (esse bootloader não tem suporte a UEFI). O boot pelo pendrive pode carregar o kernel, mas o kernel ainda não possui driver USB Mass Storage; nesse caso a partição do pendrive não fica disponível aos aplicativos.

## 4. Criar aplicativos (.nxe)

Um aplicativo é um programa em C compilado sem biblioteca padrão e carregado pelo kernel.
Ele recebe uma tabela de funções (`nx_api`, abreviada como `A->`) e desenha dentro de uma janela.

### 4.1 Estrutura do projeto

```
apps/meu_app.c        seu código
sdk/nxe.h             contrato com o sistema (tipos, eventos, API)
sdk/apputil.h         macros úteis
sdk/app_rt.c          runtime (chama app_main)
sdk/crt.ld            layout do binário
tools/mkinitrd.py     compila todos os apps e gera ícones
```

### 4.2 Primeiro app (passo a passo)

1. Crie `apps/ola.c`:

```c
#include "../sdk/apputil.h"

static nx_win *win;
static int contador;

static void on_paint(nx_win *w) {
    nx_canvas *c = &w->cv;                          /* área cliente da janela */
    A->fill(c, 0, 0, c->w, c->h, RGB(255, 255, 255));
    A->text(c, 20, 20, "Olá, NegxusOS!", NF_H2, RGB(20, 20, 20));
    char t[40];
    nx_snprintf(t, sizeof t, "Cliques: %d", contador);
    A->text(c, 20, 60, t, NF_UI, RGB(80, 80, 90));
}
static void on_mouse(nx_win *w, int type, int x, int y, int btn, int arg) {
    (void)x; (void)y; (void)btn; (void)arg;
    if (type == NMOUSE_DOWN) { contador++; A->win_invalidate(w); }
}

int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    win = A->win_create("Olá", 320, 140, 0);
    if (!win) return -1;
    win->on_paint = on_paint;
    win->on_mouse = on_mouse;
    return 0;
}
```

2. Registre o app em `tools/mkinitrd.py`, na lista `APPS`:

```python
("Olá", "ola", 1, 6, "terminal"),   # nome, arquivo, flags, ordem, ícone
```

`flags`: 1 = aparece na área de trabalho; 2 = oculto no menu Iniciar.
`icon`: `folder`, `globe`, `snake`, `mine`, `paint` ou `terminal` (ou crie um novo em `icon_pixels()`).

3. Rode `make` e teste com `qemu-system-x86_64 -m 512 -drive format=raw,file=build/negxusos.img`.

### 4.3 Ciclo de vida

- `app_main` roda uma vez, ao abrir o app. Crie as janelas aqui e retorne 0 (ou -1 em erro).
- Os callbacks são chamados pelo sistema depois disso. Não há laço infinito no app.
- Dados globais (`static`) são próprios de cada instância do app.
- Memória de `A->malloc` é liberada quando a última janela do app fecha.
- Depois de criar a janela, o sistema chama `on_paint` uma vez.

### 4.4 Callbacks

| Callback | Quando é chamado |
|---|---|
| `on_paint(w)` | primeira exibição e sempre que você chama `A->win_invalidate(w)` |
| `on_key(w, key, mods)` | tecla, com a janela em foco |
| `on_mouse(w, tipo, x, y, botoes, arg)` | mouse; `x,y` relativos à área cliente |
| `on_timer(w)` | a cada N ms, definido por `A->win_timer(w, N)` |
| `on_resize(w)` | após redimensionar |
| `on_close(w)` | ao fechar; retorne 1 para permitir o fechamento |

Regras:
- Desenhe **só** dentro de `on_paint`. Para atualizar, chame `A->win_invalidate(w)`.
- Não faça alocação pesada dentro de `on_paint`.
- Para animação use `A->win_timer` (ex.: 30 ms), não laços infinitos.

### 4.5 Teclas e mouse

Teclas especiais: `NK_UP, NK_DOWN, NK_LEFT, NK_RIGHT, NK_ENTER, NK_ESC, NK_BACKSPACE, NK_TAB,
NK_HOME, NK_END, NK_PGUP, NK_PGDN, NK_F1..NK_F12`. Caracteres comuns chegam como código
(`'a'`, `'A'`, `'ç'`). Modificadores em `mods`: `NM_SHIFT, NM_CTRL, NM_ALT, NM_ALTGR`.

Mouse: `NMOUSE_MOVE, NMOUSE_DOWN, NMOUSE_UP, NMOUSE_WHEEL, NMOUSE_DBL`; botões `NB_LEFT,
NB_RIGHT, NB_MID`. No evento de roda, `arg` é o sentido do giro.

### 4.6 API (`A->`)

- Janelas: `win_create(titulo, largura, altura, flags)`, `win_title`, `win_invalidate`,
  `win_invalidate_rect`, `win_size`, `win_minsize`, `win_timer`, `win_close`.
  Flag `NW_FIXED` = tamanho fixo.
- Desenho: `fill`, `fill_a` (com alfa), `rect`, `rrect`, `rrect_line`, `line`, `circle`,
  `text(c, x, y, s, fonte, cor)`, `text_w`, `text_h`, `blit`, `icon`.
- Fontes: `NF_UI`, `NF_UIB` (negrito), `NF_SM` (pequena), `NF_H1`, `NF_H2`, `NF_H3`, `NF_MONO`.
- Cor: `RGB(r, g, b)`.
- Arquivos: `fs_read`, `fs_write`, `fs_list`, `fs_stat`, `fs_mkdir`, `fs_remove`, `fs_rename`.
  Caminhos começam com `/`. Pastas: `/Apps`, `/Documentos`, `/Imagens`.
- Sistema: `ticks()` (ms desde o boot), `time()`, `rand()`, `exec(caminho, arg)`,
  `open(caminho)`, `clip_set/clip_get`, `sysinfo`, `power`.
- Memória: `malloc`, `realloc`, `free`.

Não há acesso direto ao hardware nem à rede. Um app só usa o que está na tabela `nx_api`.

### 4.7 Exemplo completo: o Terminal

O terminal está em `apps/terminal.c`. Ele mostra como fazer: entrada de texto por teclado
(`on_key`), lista de linhas com rolagem, leitura de pastas com `fs_list`, leitura de arquivos
com `fs_read`, e comandos internos. Leia esse arquivo como referência antes de criar um app de
texto ou de interface.

### 4.8 Boas práticas

- Valide tamanhos de arquivo antes de alocar (`fs_stat` antes de `fs_read`).
- Use `nx_snprintf` (não existe `printf`).
- Não use funções da libc que não estão em `common/str.c`.
- Trate `NULL` de `win_create` e de `malloc`.
- Não confie em dados de arquivos: verifique limites de arrays.

## 5. Estrutura do código

```
boot/        bootloader (MBR e estágio 2)
kernel/      kernel: arch, disco ATA, memória, gráficos, entrada, FAT16,
             janelas (wm), área de trabalho (desktop), carregador de .nxe (nxe), main
common/      libc mínima (str.c)
sdk/         contrato com os apps
apps/        aplicativos do sistema
tools/       geração de fontes, ícones, initrd e imagem
```
