@echo off
REM x32 builds begin
SET "ORIGINAL_PATH=%PATH%"
SET PATH=C:\tools\msys64\mingw32\bin;C:\tools\msys64\usr\bin;%PATH%
Title "x86-32"
make -f MakeFile profile-build ARCH=x86-32 COMP=mingw
strip brainlearn.exe
ren brainlearn.exe "Brainlearn30-x86-32.exe"

Title "x86-32-old"
make clean
make -f MakeFile profile-build ARCH=x86-32-old COMP=mingw
strip brainlearn.exe
ren brainlearn.exe "Brainlearn30-x86-32-old.exe"

Title "general-32"
make clean
make -f MakeFile profile-build ARCH=general-32 COMP=mingw
strip brainlearn.exe
ren brainlearn.exe "Brainlearn30-general-32.exe"

make clean
REM x32 builds end
SET "PATH=%ORIGINAL_PATH%"

pause
