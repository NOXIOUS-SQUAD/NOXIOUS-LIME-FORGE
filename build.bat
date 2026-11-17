@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo  ============================================
echo   LimeForge - compilacion Release
echo  ============================================
echo.

set "BUILD_DIR=build"
set "VS_DIR="

rem --- Localizar Visual Studio con herramientas de C++ ----------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%I"
)
if not defined VS_DIR if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community" set "VS_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
if not defined VS_DIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community" set "VS_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VS_DIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools" set "VS_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined VS_DIR (
  echo [ERROR] No se encontro Visual Studio con "Desktop development with C++".
  echo         Instala los Tools for C++ y volve a intentar.
  goto :error
)

echo [1/4] Preparando entorno de MSVC...
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [ERROR] No se pudo inicializar vcvars64.bat.
  goto :error
)

rem --- Localizar CMake y Ninja ---------------------------------------------
set "CMAKE="
for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE if exist "%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE (
  echo [ERROR] No se encontro CMake. Instala CMake 3.24 o superior.
  goto :error
)

set "NINJA="
for /f "delims=" %%I in ('where ninja 2^>nul') do if not defined NINJA set "NINJA=%%I"
if not defined NINJA if exist "%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if defined NINJA for %%I in ("%NINJA%") do set "PATH=%%~dpI;%PATH%"

set "NINJA_ARG="
if defined NINJA set NINJA_ARG=-DCMAKE_MAKE_PROGRAM="%NINJA%"

echo [2/4] Configurando el proyecto...
"%CMAKE%" -S "%CD%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %NINJA_ARG%
if errorlevel 1 goto :error

echo [3/4] Compilando...
"%CMAKE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto :error

echo [4/4] Copiando LimeForge.exe a la raiz...
if not exist "%BUILD_DIR%\LimeForge.exe" (
  echo [ERROR] La compilacion no genero "%BUILD_DIR%\LimeForge.exe".
  goto :error
)
copy /y "%BUILD_DIR%\LimeForge.exe" "LimeForge.exe" >nul
if errorlevel 1 (
  echo [ERROR] No se pudo copiar el ejecutable. Cerra LimeForge si esta abierto.
  goto :error
)

echo.
echo  Listo. Ejecutable: %CD%\LimeForge.exe
echo.
if /i not "%~1"=="--no-pause" pause
exit /b 0

:error
echo.
echo  La compilacion fallo. Revisa el mensaje anterior.
echo.
if /i not "%~1"=="--no-pause" pause
exit /b 1
