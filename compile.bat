@echo off
set "MINGW_BIN=C:\msys64\mingw64\bin"
set "PATH=%MINGW_BIN%;%PATH%"

echo --- Compilando NovaISO ---

echo [1/3] Compilando recursos...
windres resources.rc -O coff -o src/resources.res

echo [2/3] Compilando codigo fuente...
g++ -c src/iso_logic.cpp -o src/iso_logic.o -I"C:\msys64\mingw64\include" -O3
g++ -c src/cso_logic.cpp -o src/cso_logic.o -I"C:\msys64\mingw64\include" -O3
g++ -c src/cso_metadata.cpp -o src/cso_metadata.o -I"C:\msys64\mingw64\include" -O3
g++ -c src/main.cpp -o src/main.o -I"C:\msys64\mingw64\include" -O3

echo [3/3] Enlazando ejecutable...
g++ src/*.o src/resources.res -o NovaISO.exe ^
    -L"C:\msys64\mingw64\lib" ^
    -mwindows ^
    -lgdiplus ^
    -lz ^
    -lcomctl32 ^
    -lshlwapi ^
    -lcomdlg32 ^
    -lole32 ^
    -O3 ^
    -static

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [OK] NovaISO.exe generado con exito.
    del src\*.o src\resources.res 2>nul
) else (
    echo.
    echo [ERROR] La compilacion fallo.
)
pause
