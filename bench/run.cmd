@echo off
rem Run stupix-bench on Windows: picks the native build for x86, x64 or ARM64.
setlocal
set ARCH=x86_64
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set ARCH=aarch64
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set ARCH=aarch64
"%~dp0bin\stupix-bench-windows-%ARCH%.exe" %*
