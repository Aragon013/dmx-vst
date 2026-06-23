@echo off
REM PowerShell script launcher for GitHub setup
REM This batch file allows easy execution on Windows without Git Bash

setlocal enabledelayedexpansion

echo.
echo ======================================
echo LuxSync DMX - GitHub Setup (Windows)
echo ======================================
echo.

REM Check if Git is installed
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git not found. Please install Git from https://git-scm.com/
    exit /b 1
)

REM Check if already a git repo
if exist ".git" (
    echo √ Already a git repository
    echo.
    echo Current remote:
    git remote -v
    echo.
) else (
    echo × Not a git repository yet
    echo.
    set /p GITHUB_USER="Enter your GitHub username: "
    set /p REPO_NAME="Enter repository name [dmx-vst]: "
    
    if "!REPO_NAME!"=="" (
        set REPO_NAME=dmx-vst
    )
    
    set REPO_URL=https://github.com/!GITHUB_USER!/!REPO_NAME!.git
    
    echo.
    echo Repository URL: !REPO_URL!
    echo.
    echo Make sure this repository exists on GitHub before continuing.
    pause
    
    REM Initialize git
    echo Initializing git repository...
    git init
    git config user.name "LuxSync Developer"
    git config user.email "dev@luxsync.local"
    
    REM Add all files
    echo Adding files...
    git add .
    
    echo Creating initial commit...
    git commit -m "Initial commit: LuxSync DMX VST3 + AI Automator"
    
    REM Setup branch and remote
    git branch -M main
    git remote add origin !REPO_URL!
)

echo.
echo ======================================
echo Ready to push!
echo ======================================
echo.
echo Next steps:
echo.
echo 1. Push main branch:
echo    git push -u origin main
echo.
echo 2. Create release tag:
echo    git tag -a v0.1.0 -m "Initial release: LuxSync DMX VST3 + AI Automator"
echo.
echo 3. Push tag (triggers GitHub Actions):
echo    git push origin v0.1.0
echo.
echo Then GitHub Actions will automatically:
echo   - Build on Windows (VST3 + Standalone)
echo   - Build on macOS (AU/VST3 + Standalone for arm64 + x86_64)
echo   - Create a GitHub Release with all installers
echo.
echo ======================================
echo.

pause
