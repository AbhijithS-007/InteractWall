call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd C:\My_Proj\InteractWall\renderer
cmake -B build
cmake --build build --target StonePress --config Release
