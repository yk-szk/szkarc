SETLOCAL enabledelayedexpansion
mkdir build
cd build
set G="Visual Studio 15 2017"
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019" set G="Visual Studio 16 2019"
cmake -G !G! -A x64 ..
cmake --build . --config Release --parallel 2
ENDLOCAL