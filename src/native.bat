@echo off
REM x64 builds begin
SET PATH=C:\tools\msys64\mingw64\bin;C:\tools\msys64\usr\bin;%PATH%
REM make -j profile-build
mingw32-make profile-build ARCH=native COMP=mingw -j %Number_Of_Processors%
strip brainlearn.exe
ren brainlearn.exe Brainlearn30-native.exe
make clean
pause
