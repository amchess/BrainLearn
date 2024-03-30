@echo off
REM make -j profile-build
make profile-build ARCH=native COMP=gcc
strip brainlearn
ren brainlearn Brainlearn28-native
make clean
pause
