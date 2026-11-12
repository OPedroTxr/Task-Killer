@echo off
rem ==========================================================
rem  The Method - compila o TheMethod.exe (2 cliques aqui)
rem  Usa o Visual Studio (MSVC) se existir; senao, MinGW (gcc).
rem ==========================================================
setlocal
cd /d "%~dp0"
rem fecha o app se estiver aberto (versao antiga "Risca" ou a nova)
taskkill /im Risca.exe /f >nul 2>nul
taskkill /im TheMethod.exe /f >nul 2>nul

where cl >nul 2>nul
if %errorlevel%==0 goto msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto try_mingw
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto try_mingw
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" goto try_mingw
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
goto msvc

:try_mingw
where gcc >nul 2>nul
if %errorlevel%==0 goto mingw
goto nocompiler

:msvc
echo [The Method] Compilando com MSVC...
rc /nologo /fo themethod.res themethod.rc
if errorlevel 1 goto fail
cl /nologo /utf-8 /O1 /Os /GS- /MT /W3 /DNDEBUG themethod.c themethod.res /Fe:TheMethod.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF
if errorlevel 1 goto fail
del /q themethod.obj themethod.res 2>nul
goto ok

:mingw
echo [The Method] Compilando com MinGW (gcc)...
windres -O coff themethod.rc -o themethod_res.o
if errorlevel 1 goto fail
gcc -Os -s -municode -mwindows -o TheMethod.exe themethod.c themethod_res.o -luser32 -lgdi32 -lshell32 -lcomctl32 -lcomdlg32 -ladvapi32 -lwinmm
if errorlevel 1 goto fail
del /q themethod_res.o 2>nul
goto ok

:nocompiler
echo.
echo  Nenhum compilador C foi encontrado.
echo  Instale UM destes (gratuitos) e rode este arquivo de novo:
echo.
echo   1) Visual Studio Build Tools - carga "Desenvolvimento para desktop com C++"
echo      winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo   2) MinGW-w64 (WinLibs), mais leve:
echo      winget install BrechtSanders.WinLibs.POSIX.UCRT
echo      (depois feche e abra o terminal)
echo.
goto end

:fail
echo.
echo [The Method] A compilacao falhou. Veja as mensagens acima.
goto end

:ok
echo.
echo [The Method] Pronto: TheMethod.exe criado nesta pasta.
echo              Abra o TheMethod.exe. Seus dados e o "Iniciar com o Windows"
echo              da versao Risca sao migrados automaticamente.
echo              Depois voce pode apagar o Risca.exe antigo.

:end
echo.
pause
