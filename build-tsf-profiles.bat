@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /utf-8 /std:c++20 tools\tsf-activate.cpp /Fe:build\tsf-activate.exe /link ole32.lib oleaut32.lib
