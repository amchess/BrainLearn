@echo off
REM make -j profile-build
make profile-build ARCH=native COMP=gcc
strip brainlearn
ren brainlearn Brainlearn27-native
make clean
pause
