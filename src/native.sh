@echo off
REM make -j profile-build
make profile-build ARCH=native COMP=gcc
strip brainlearn
ren brainlearn Brainlearn28.1-native
make clean
pause
