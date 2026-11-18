@echo off
setlocal
cd /d "%~dp0"

if exist "%~dp0toolchain\bin" set "PATH=%~dp0toolchain\bin;%PATH%"

where x86_64-elf-gcc >nul 2>nul
if errorlevel 1 goto no_gcc
where x86_64-elf-ld >nul 2>nul
if errorlevel 1 goto no_ld
where x86_64-elf-objcopy >nul 2>nul
if errorlevel 1 goto no_objcopy
where x86_64-elf-nm >nul 2>nul
if errorlevel 1 goto no_nm
where x86_64-elf-readelf >nul 2>nul
if errorlevel 1 goto no_readelf
where nasm >nul 2>nul
if errorlevel 1 goto no_nasm
where python >nul 2>nul
if errorlevel 1 goto no_python

set "CC=x86_64-elf-gcc"
set "LD=x86_64-elf-ld"
set "OBJCOPY=x86_64-elf-objcopy"
set "NM=x86_64-elf-nm"
set "READELF=x86_64-elf-readelf"
set "NASM=nasm"
set "PYTHON=python"

python -c "import sys; import PIL; print('Python',sys.version.split()[0],'- Pillow OK')"
if errorlevel 1 goto no_pillow

python tools\build_windows.py
if errorlevel 1 goto failed

echo.
echo Imagem criada: build\negxusos.img
echo QEMU: qemu-system-x86_64 -m 512 -drive file=build\negxusos.img,format=raw,if=ide,index=0 -serial stdio
pause
exit /b 0

:no_gcc
echo ERRO: x86_64-elf-gcc nao encontrado.
goto failed
:no_ld
echo ERRO: x86_64-elf-ld nao encontrado.
goto failed
:no_objcopy
echo ERRO: x86_64-elf-objcopy nao encontrado.
goto failed
:no_nm
echo ERRO: x86_64-elf-nm nao encontrado.
goto failed
:no_readelf
echo ERRO: x86_64-elf-readelf nao encontrado.
goto failed
:no_nasm
echo ERRO: nasm.exe nao encontrado.
goto failed
:no_python
echo ERRO: Python nao encontrado.
goto failed
:no_pillow
echo ERRO: Pillow ausente. Execute: python -m pip install Pillow==10.4.0
goto failed
:failed
echo.
echo A compilacao falhou. Leia a mensagem de erro acima.
pause
exit /b 1
