@echo off
REM make -j profile-build
make profile-build ARCH=native COMP=gcc
strip brailearn
ren brainlearn Brainlearn26.4-native
make clean
pause
