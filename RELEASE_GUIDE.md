# LuxSync DMX - Release & Distribution Guide

Este documento explica cómo compilar, empaquetar y distribuir LuxSync DMX a través de GitHub Releases.

## Estructura de Releases

Se generan **4 instaladores principales**:

| Plataforma | Tipo | Archivo |
|---|---|---|
| Windows | VST3 Plugin | `LuxSync-DMX-VST3-v*.exe` (NSIS installer) |
| Windows | Standalone | `LuxSync-AIAutomator-v*-Windows-x64.zip` |
| macOS | AU + VST3 Plugins | `LuxSync-DMX-AU-v*.dmg` + `LuxSync-DMX-VST3-v*.dmg` |
| macOS | Standalone | `LuxSync-AIAutomator-v*.dmg` |

---

## Compilación Local

### Windows

#### Requisitos:
- **Visual Studio 2022** o **MSVC Build Tools**
- **CMake** (>= 3.22)
- **NSIS** (para generar instalador)

#### Build:
```bash
# Ejecutar build-windows.sh desde bash (Git Bash, WSL o similar)
bash scripts/build-windows.sh 0.1.0 Release

# O manualmente:
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DmxVst
cmake --build build --config Release --target LuxAutomator
```

#### Packaging:
```powershell
# Ejecutar script PowerShell para generar instaladores
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -Version 0.1.0
```

**Salida**: `release/` con instalador VST3 (.exe) y Standalone (.zip)

### macOS

#### Requisitos:
- **Xcode** (include Command Line Tools)
- **CMake** (>= 3.22)

#### Build (arm64 - Apple Silicon):
```bash
bash scripts/build-macos.sh 0.1.0 Release arm64
```

#### Build (x86_64 - Intel):
```bash
bash scripts/build-macos.sh 0.1.0 Release x86_64
```

**Salida**: `dist/macos/` con `.component` (AU), `.vst3`, y `.app` (Standalone)

#### Crear DMG (manual, si no usas GitHub Actions):
```bash
# AU
mkdir -p dmg/LuxSync-AU
cp -r dist/macos/au/*.component dmg/LuxSync-AU/
ln -s /Library/Audio/Plug-Ins/Components dmg/LuxSync-AU/
hdiutil create -volname "LuxSync DMX AU" -srcfolder dmg -ov -format UDZO LuxSync-DMX-AU-v0.1.0-macOS-arm64.dmg

# VST3
mkdir -p dmg/LuxSync-VST3
cp -r dist/macos/vst3/*.vst3 dmg/LuxSync-VST3/
ln -s /Library/Audio/Plug-Ins/VST3 dmg/LuxSync-VST3/
hdiutil create -volname "LuxSync DMX VST3" -srcfolder dmg -ov -format UDZO LuxSync-DMX-VST3-v0.1.0-macOS-arm64.dmg

# Standalone
mkdir -p dmg/LuxSync-Automator
cp -r dist/macos/standalone/*.app dmg/LuxSync-Automator/
hdiutil create -volname "LuxSync AI Automator" -srcfolder dmg -ov -format UDZO LuxSync-AIAutomator-v0.1.0-macOS-arm64.dmg
```

---

## Releases Automatizadas vía GitHub Actions

### 1. Push a GitHub

Primero, sube el código a un repositorio en GitHub:

```bash
# Si aún no está inicializado
git init
git add .
git commit -m "Initial commit: LuxSync DMX VST3 and Automator"
git branch -M main
git remote add origin https://github.com/TuUsuario/dmx-vst.git
git push -u origin main
```

### 2. Crear un Release

Los releases se disparan automáticamente cuando haces un **tag de versión**:

```bash
git tag -a v0.1.0 -m "Release version 0.1.0: Initial VST3 + Automator build"
git push origin v0.1.0
```

**GitHub Actions automáticamente:**
1. Compila en Windows (VST3 + Standalone)
2. Compila en macOS arm64 y x86_64 (AU + VST3 + Standalone)
3. Empaqueta todos los artefactos
4. Crea un GitHub Release con todos los instaladores

### 3. Verificar Workflow

Ir a **GitHub** → **Actions** → Ver el workflow `Build & Release` en ejecución.

Cuando termine, los artefactos estarán en **Releases** → **[v0.1.0]** → **Assets**.

---

## Instalación de los Plugins

### Windows - VST3

1. Descargar `LuxSync-DMX-VST3-v*.exe`
2. Ejecutar el instalador
3. Seleccionar carpeta de instalación (por defecto: `C:\Program Files\Common Files\VST3`)
4. Reiniciar el DAW (Ableton, FL Studio, Reaper, etc.)
5. El plugin aparecerá en la lista de VST3 bajo "Aragon"

### Windows - Standalone

1. Descargar `LuxSync-AIAutomator-v*-Windows-x64.zip`
2. Extraer en la carpeta deseada
3. Ejecutar `LuxAutomator.exe`

### macOS - AU / VST3

1. Descargar el `.dmg` correspondiente (AU o VST3)
2. Abrir el DMG
3. Arrastrar el plugin al destino sugerido
4. Reiniciar el DAW
5. El plugin aparecerá en Logic Pro, GarageBand, etc.

### macOS - Standalone

1. Descargar `LuxSync-AIAutomator-v*-macOS-arm64.dmg` (o x86_64)
2. Abrir el DMG
3. Arrastrar `LuxAutomator.app` a `Applications`
4. Abrir desde Launchpad o Spotlight

---

## Versionado Semántico

Usar **Semantic Versioning**: `v[MAJOR].[MINOR].[PATCH]`

Ejemplos:
- `v0.1.0` — Primera release (beta)
- `v0.2.0` — Nueva funcionalidad menor
- `v1.0.0` — Release estable
- `v1.0.1` — Bugfix

---

## Configuración Post-Instalación

### Windows DAW

- **FL Studio**: Plugins → Scan VST3 folder
- **Reaper**: Options → Preferences → VST → Re-scan plug-ins
- **Bitwig**: Settings → Plug-ins → Rescan
- **Ableton Live**: Options → Plug-in Browser → Rescan

### macOS DAW

- **Logic Pro**: Preferences → Plug-in Manager → Rescan
- **GarageBand**: Editar → Preferencias → Plug-ins → Rescan
- **Ableton Live**: Options → Preferences → Plug-ins → Rescan

---

## Troubleshooting

### Windows: "Plugin not found"
- Verificar que el installer puso la carpeta en `C:\Program Files\Common Files\VST3\Aragon\`
- Reiniciar el DAW
- Si aún no aparece, ejecutar el comando en Reaper: `?extensions="C:\Program Files\Common Files\VST3"`

### macOS: "Developer cannot be verified"
- Aceptar la ventana de seguridad del sistema
- O en Terminal: `sudo spctl --add /Library/Audio/Plug-Ins/VST3/Aragon/`

### macOS: Architecture mismatch
- Descargar la versión correcta:
  - M1/M2/M3 (Apple Silicon) → `arm64`
  - Intel Mac → `x86_64`

---

## Actualizaciones Futuras

Para lanzar una nueva versión:

```bash
# Editar código...
git add .
git commit -m "Add feature X"

# Crear nuevo tag
git tag -a v0.2.0 -m "Release version 0.2.0: Added sACN output + new motion figures"
git push origin main
git push origin v0.2.0

# GitHub Actions compilará y creará la release automáticamente
```

---

## Archivos Relevantes

- `.github/workflows/release.yml` — Configuración de CI/CD
- `scripts/build-windows.sh` — Script de compilación Windows
- `scripts/build-macos.sh` — Script de compilación macOS
- `scripts/package-windows.ps1` — Generador de instaladores Windows
- `installer/LuxSync-VST3-Windows.nsi` — Template NSIS para Windows

---

## Contacto & Soporte

Para reportar bugs o solicitar features: [GitHub Issues](https://github.com/TuUsuario/dmx-vst/issues)

---

**Última actualización**: 2026-06-22
**Versión**: v0.1.0 (Initial Release)
