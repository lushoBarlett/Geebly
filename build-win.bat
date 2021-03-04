@echo off

set SDL2_INCLUDE_DIR=""
set SDL2_LIB_DIR=""
set LGW_INCLUDE_DIR=""

mkdir build
c++ -c src/geebly.cpp -o build/geebly.o -I%SDL2_INCLUDE_DIR% -I%LGW_INCLUDE_DIR% -std=c++2a -Ofast -Wno-format -Wno-narrowing -D GEEBLY_VERSION_MAJOR=0 -D GEEBLY_VERSION_MINOR=7 -D GEEBLY_VERSION_CLASS=b -D GEEBLY_COMMIT_ID=50e4056

c++ build/geebly.o -o build/geebly.exe -L%SDL2_LIB_DIR% -lSDL2main -lSDL2

del "build\geebly.o"