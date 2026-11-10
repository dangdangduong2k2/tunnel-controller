@echo off
setlocal
if defined VSDEVCMD goto vs_found
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
:vs_found
if not exist "%VSDEVCMD%" (
  echo Visual Studio C++ tools missing. Set VSDEVCMD to the full VsDevCmd.bat path.
  exit /b 1
)
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
if defined PYTHON_EXE (
  "%PYTHON_EXE%" "%~dp0tests\run_modbus_regressions.py" %*
) else (
  py -3 "%~dp0tests\run_modbus_regressions.py" %*
)
exit /b %errorlevel%
