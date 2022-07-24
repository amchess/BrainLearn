REM x32 builds begin
SET PATH=C:\msys64\mingw32\bin;C:\msys64\usr\bin;%PATH%

Title "x86-32"
make clean
mingw32-make -f MakeFile profile-build ARCH=x86-32 COMP=mingw
strip stockfish.exe
ren stockfish.exe "BrainLearn18-x86-32.exe"

Title "x86-32-old"
make clean
mingw32-make -f MakeFile profile-build ARCH=x86-32-old COMP=mingw
strip stockfish.exe
ren stockfish.exe "BrainLearn18-x86-32-old.exe"

Title "general-32"
make clean
mingw32-make -f MakeFile profile-build ARCH=general-32 COMP=mingw
strip stockfish.exe
ren stockfish.exe "BrainLearn18-general-32.exe"

make clean
ren C:\MinGW\mingw32 mingw32-730-pd
REM x32 builds end

pause
