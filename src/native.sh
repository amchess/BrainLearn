# make -j profile-build
make profile-build ARCH=native COMP=gcc  -j$(nproc)
strip brainlearn
mv brainlearn Brainlearn28-native
make clean

