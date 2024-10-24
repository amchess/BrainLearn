@echo off
REM x64 builds begin
SET PATH=C:\tools\msys64\mingw64\bin;C:\tools\msys64\usr\bin;%PATH%
Title "x86-64-sse41-popcnt"
mingw32-make profile-build ARCH=x86-64-sse41-popcnt COMP=mingw CXX=x86_64-w64-mingw32-g++ -j %Number_Of_Processors%
strip brainlearn.exe
ren brainlearn.exe "BrainlearnSantiago-x86-64-sse41-popcnt.exe"
make clean