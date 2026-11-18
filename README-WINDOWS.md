# Compilar o NegxusOS no Windows 7

Este procedimento gera `build\\negxusos.img` diretamente no Windows 7, sem GNU Make ou Linux instalado no computador.

## 1. Requisitos

Recomendado: Windows 7 SP1 de 64 bits.

1. **Python 3.8.x**. O Python 3.8 e a ultima linha que suporta Windows 7.
2. **Pillow 7.0.0**. Essa versao tem wheel CPython 3.8 para Windows e aparece como versao testada no Windows 7. Depois de instalar Python, abra o Prompt de Comando e execute:
   `python -m pip install Pillow==7.0.0`
3. **NASM para Windows**: baixe em https://www.nasm.us/ e adicione a pasta que contem `nasm.exe` ao PATH.
4. **Cross-compiler x86_64-elf**. O projeto precisa de ferramentas que gerem objetos ELF para o kernel. O GCC normal do MinGW gera executaveis Windows (PE) e nao substitui essa toolchain.

A toolchain deve fornecer estes programas:
- `x86_64-elf-gcc.exe`
- `x86_64-elf-ld.exe`
- `x86_64-elf-objcopy.exe`
- `x86_64-elf-nm.exe`
- `x86_64-elf-readelf.exe`

Consulte a secao de toolchains precompiladas para Windows no OSDev Wiki:
https://wiki.osdev.org/GCC_Cross_Compiler

Toolchains precompiladas para Windows sao mantidas pela comunidade. Confira a origem e teste os executaveis no seu Windows antes de compilar o sistema.

## 2. Organizar os arquivos

Opcao recomendada: extraia a cross-toolchain para a pasta `toolchain\\bin` dentro do repositorio:

```text
NegxusOs/
  toolchain/
    bin/
      x86_64-elf-gcc.exe
      x86_64-elf-ld.exe
      x86_64-elf-objcopy.exe
      x86_64-elf-nm.exe
      x86_64-elf-readelf.exe
```

O arquivo `build-windows.bat` adiciona essa pasta ao PATH automaticamente, se ela existir. O NASM tambem precisa estar no PATH.

## 3. Gerar a imagem IMG

Abra o Prompt de Comando na pasta do repositorio e execute:

```bat
build-windows.bat
```

Ao terminar sem erros, o arquivo estara em:

```text
build\\negxusos.img
```

O script compila o kernel, os aplicativos NXE, o initrd e a imagem de disco.

## 4. Testar no QEMU

Com QEMU instalado para Windows, execute:

```bat
qemu-system-x86_64 -m 512 -drive file=build\\negxusos.img,format=raw,if=ide,index=0 -serial stdio
```

O disco deve ser apresentado como IDE primario master, que e o dispositivo suportado pelo driver ATA desta versao.

## 5. Gravar em pendrive

Use Rufus e selecione o modo de gravacao **DD** quando solicitado. A gravacao apaga o dispositivo selecionado. Confira cuidadosamente a unidade antes de confirmar.

A imagem possui uma particao FAT16 de 64 MiB que o Windows 7 pode reconhecer e montar.

## 6. Sistema de arquivos persistente

O kernel agora usa FAT16 no disco em vez de manter os arquivos somente na RAM. O initrd instala os aplicativos em `/Apps` no primeiro boot, e os arquivos criados pelo sistema permanecem no volume depois de reiniciar, desde que o disco seja acessivel.

Operacoes previstas na API atual:
- listar diretorios e consultar atributos;
- criar diretorios;
- ler e sobrescrever arquivos;
- renomear arquivos e diretorios;
- remover arquivos e diretorios.

## 7. Limitacoes de hardware desta versao

O driver implementado e ATA PIO, canal IDE primario, dispositivo master, LBA28. No QEMU, a imagem e configurada como IDE.

Em computador fisico, o acesso persistente requer uma controladora IDE ou SATA configurada em modo IDE/Compatibility e o disco ligado como master do canal primario.

**Importante:** o bootloader pode iniciar o kernel a partir de um pendrive USB via BIOS, mas o kernel ainda nao inclui USB Mass Storage. Portanto, o sistema ainda nao consegue ler/escrever a particao do proprio pendrive depois que termina o boot. USB Mass Storage precisa de um driver proprio.
