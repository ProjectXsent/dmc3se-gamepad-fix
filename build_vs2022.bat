mkdir build86
cd build86
cmake .. -G "Visual Studio 17 2022" -A "Win32" -DSDL2_DIR=".\ThirdParty\SDL2\cmake"
cmake --build . --config Release
cd ..
pause