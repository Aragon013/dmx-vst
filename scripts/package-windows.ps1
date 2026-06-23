# PowerShell script to build and package Windows installers for LuxSync DMX

param(
    [string]$Version = "0.1.0",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================="
Write-Host "LuxSync DMX - Windows Packaging"
Write-Host "Version: $Version"
Write-Host "Build Type: $BuildType"
Write-Host "=========================================="

# Create distribution folder
Write-Host "[1/4] Creating distribution folders..."
$distPath = "dist\windows"
New-Item -ItemType Directory -Path "$distPath\vst3" -Force | Out-Null
New-Item -ItemType Directory -Path "$distPath\standalone" -Force | Out-Null
New-Item -ItemType Directory -Path "release" -Force | Out-Null

# Copy built artifacts
Write-Host "[2/4] Copying artifacts..."
Copy-Item -Path "build\DmxVst_artefacts\$BuildType\VST3\*" -Destination "$distPath\vst3\" -Recurse -Force
Copy-Item -Path "build\LuxAutomator_artefacts\$BuildType\Standalone\*" -Destination "$distPath\standalone\" -Recurse -Force

# Check if NSIS is installed, if not offer to install
Write-Host "[3/4] Creating NSIS installer for VST3..."
$nsisPath = "C:\Program Files (x86)\NSIS\makensis.exe"

if (-not (Test-Path $nsisPath)) {
    Write-Host "NSIS not found. Installing via chocolatey..."
    try {
        choco install nsis -y
    } catch {
        Write-Host "Error: Chocolatey not available. Please install NSIS manually from https://nsis.sourceforge.io/"
        exit 1
    }
}

# Create NSIS script with version substitution
$nsisScript = Get-Content "installer\LuxSync-VST3-Windows.nsi" -Raw
$nsisScript = $nsisScript -replace "0\.1\.0", $Version
$nsisScript | Out-File -Encoding ASCII "installer\LuxSync-VST3-temp.nsi"

# Build installer
& $nsisPath "installer\LuxSync-VST3-temp.nsi"

if ($LastExitCode -ne 0) {
    Write-Host "ERROR: NSIS compilation failed!"
    exit 1
}

# Move generated installer to release folder
Move-Item -Path "LuxSync-DMX-VST3-v*-Windows-x64.exe" -Destination "release\" -Force

Write-Host "[4/4] Creating Standalone package..."
$standaloneZip = "LuxSync-AIAutomator-v$Version-Windows-x64.zip"
Compress-Archive -Path "$distPath\standalone\*" -DestinationPath "release\$standaloneZip" -Force

# Clean up
Remove-Item "installer\LuxSync-VST3-temp.nsi" -Force
Remove-Item "$distPath\*" -Recurse -Force

Write-Host "=========================================="
Write-Host "Packaging completed successfully!"
Write-Host "Release artifacts in: release\"
Get-ChildItem "release\" | ForEach-Object { Write-Host "  - $($_.Name)" }
Write-Host "=========================================="
