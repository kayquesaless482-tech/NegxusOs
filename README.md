# NegxusOS

Sistema operacional experimental para x86-64, escrito do zero em C e assembly, sem Linux e sem
nenhum código de outro sistema. Roda a partir de uma imagem de disco (`.img`) no QEMU com disco IDE.
O bootloader Legacy BIOS pode iniciar a imagem gravada em pendrive, mas esta versão ainda não tem
driver USB Mass Storage: os arquivos persistentes dependem do volume NegxusOS FAT16 no ATA primário/master.

Status: **versão 1.0 de teste**. Testado apenas no QEMU. Ainda não foi validado em hardware real.

## O que funciona

- Bootloader próprio (MBR + estágio 2) com modo gráfico VESA de 32 bits
- Animação de inicialização com a logo do sistema
- Área de trabalho no estilo Windows 10: barra de tarefas escura, menu Iniciar e janelas com
  barra de título clara
- Troca de papel de parede (app **Personalizar**)
- Explorador de Arquivos com colunas Nome, Tipo e Tamanho
- Sistema de arquivos FAT16 persistente, com nomes longos VFAT e leitura/escrita no disco (driver ATA PIO primário/master)
- Terminal com comandos internos (`help`, `ls`, `cd`, `cat`, `mkdir`, `echo`, `date`)
- **Cubos**: jogo de blocos em 3D real (triângulos com perspectiva e z-buffer)
- Cobrinha, Campo Minado e Paint (salva desenhos em `.npt`)
- Teclado com layout ABNT2 e mouse PS/2

## O que não funciona (ainda)

- Aceleração de GPU: a renderização é feita pela CPU, sobre o framebuffer do BIOS. Não há driver
  de GPU (Intel ou outro).
- Troca de resolução em tempo real (a resolução é fixa, escolhida no boot).
- Execução de `.exe` e Direct3D.
- Rede, navegador, bloco de notas, gerenciador de tarefas, Doom.
- Driver Intel i915 e Wine: não portados (ver DOCUMENTACAO-APPS.md, seção 1b).
- Apps em anel 3: o kernel já suporta anel 3 (segmentos de usuário, TSS, porta de syscall `int 0x80`,
  página de usuário e retorno ao kernel). Um teste de anel 3 roda no boot e imprime `RI` na serial.
  Os aplicativos ainda rodam em anel 0 (mesmo espaço de endereços do kernel).
- FAT32, exFAT e NTFS ainda não são suportados. O FAT16 usa ATA PIO no canal primário/master; USB Mass Storage ainda não está implementado.
- Gamepad USB, instalador e live boot persistente (a escolha instalar/live ainda não existe).
- UEFI puro (só BIOS Legacy).

## Como compilar

```
sudo apt install gcc nasm binutils python3 python3-pil qemu-system-x86
make
```

O resultado é `build/negxusos.img`.

## Como testar

```
qemu-system-x86_64 -m 512 -drive file=build/negxusos.img,format=raw,if=ide,index=0
```

## Como gravar em pendrive

Confira o nome do dispositivo antes (`lsblk`). Gravar apaga o pendrive inteiro.

- Linux: `sudo dd if=build/negxusos.img of=/dev/sdX bs=4M conv=fsync status=progress`
- Windows: Rufus, modo "Imagem DD"

Inicie em modo Legacy/CSM na BIOS.

## Compilar no Windows 7

Consulte `README-WINDOWS.md` e execute `build-windows.bat`. É necessária uma cross-toolchain `x86_64-elf`, Python 3.8, Pillow e NASM.

## Documentação

Veja `DOCUMENTACAO-APPS.md` para criar aplicativos e entender o sistema.

## Licença

Código próprio deste projeto. As fontes Inter e DejaVu Sans Mono usadas nas fontes do sistema
têm licenças próprias (SIL OFL e Bitstream Vera), e o código de fontes é gerado por
`tools/mkfont.py`.
