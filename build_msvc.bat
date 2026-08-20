@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Commun~1\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cmake -S . -B cmake-build-release-visual-studio >nul 2>&1
cmake --build cmake-build-release-visual-studio %*
