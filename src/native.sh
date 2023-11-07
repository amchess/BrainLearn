@echo off
REM make -j profile-build
make profile-build ARCH=native COMP=gcc
ren brainlearn.exe Brainlearn26.2-native.exe
make clean
pause
