@echo off
REM x64 builds begin
SET PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
make -j profile-build
ren brainlearn.exe Brainlearn26.1-native.exe
make clean
pause
