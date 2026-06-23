# Guía: Cómo Subir LuxSync DMX a GitHub

Esta guía paso-a-paso explica cómo subir el proyecto a GitHub y configurar releases automáticas.

---

## 📋 Resumen Rápido

1. **Crear repo en GitHub** (vacío, SIN README)
2. **Subir código** con `git push`
3. **Crear tag de versión** (`git tag -a v0.1.0`)
4. **GitHub Actions compila automáticamente** en Windows + macOS
5. **Release con 4 instaladores** aparece en GitHub

---

## 🚀 Paso 1: Crear Repositorio en GitHub

1. Abre https://github.com/new
2. **Repository name**: `dmx-vst`
3. **Description** (opcional):
   ```
   Real-time audio-reactive DMX lighting control VST3/AU plugin + AI choreography engine
   ```
4. **Visibility**: `Public` (o `Private` si prefieres)
5. **NO inicializar** con README, .gitignore, o LICENSE (lo haremos localmente)
6. Click **Create repository**

Copia la URL que aparece (ej: `https://github.com/TuUsuario/dmx-vst.git`)

---

## 🖥️ Paso 2: Inicializar Git Localmente

### Opción A: Desde PowerShell (RECOMENDADO en Windows)

```powershell
cd C:\Users\Aragonlu\dmx-vst
git init
git config user.name "Aragon"
git config user.email "tu.email@example.com"
git add .
git commit -m "Initial commit: LuxSync DMX VST3 + AI Automator"
git branch -M main
git remote add origin https://github.com/TuUsuario/dmx-vst.git
```

### Opción B: Usar Script Proporcionado

**Windows (CMD):**
```batch
cd C:\Users\Aragonlu\dmx-vst
scripts\setup-github.bat
```

Sigue las instrucciones interactivas.

---

## ⬆️ Paso 3: Push al Repositorio

```bash
git push -u origin main
```

**Si falla con error de autenticación:**
- Git Portable usa OAuth browser automáticamente
- Se abrirá el navegador para autorizar
- Verifica que tengas `credential.helper=manager` en git config

**Verificar:**
```bash
git remote -v
# Deberías ver:
# origin  https://github.com/TuUsuario/dmx-vst.git (fetch)
# origin  https://github.com/TuUsuario/dmx-vst.git (push)
```

---

## 🏷️ Paso 4: Crear Tag de Release

Cada tag de versión (`v*`) dispara automáticamente GitHub Actions.

```bash
git tag -a v0.1.0 -m "Initial release: LuxSync DMX VST3 + AI Automator"
git push origin v0.1.0
```

**Formato de versión**: `v[MAJOR].[MINOR].[PATCH]`
- `v0.1.0` → Primera versión (beta)
- `v0.2.0` → Nueva funcionalidad
- `v1.0.0` → Release estable

---

## 🤖 Paso 5: GitHub Actions Compila Automáticamente

1. Abre https://github.com/TuUsuario/dmx-vst/actions
2. Verás un workflow `Build & Release` **en ejecución**
3. Espera a que termine (~15-30 minutos):
   - ✅ **build-windows** (compila VST3 + Standalone en Windows)
   - ✅ **build-macos** (compila AU/VST3 + Standalone en arm64 y x86_64)
   - ✅ **create-release** (empaqueta y crea Release con todos los archivos)

---

## 📦 Paso 6: Verificar Release

1. Abre https://github.com/TuUsuario/dmx-vst/releases
2. Verás **v0.1.0** con 4 archivos:
   - `LuxSync-DMX-VST3-v0.1.0-Windows-x64.exe` (Instalador VST3 Windows)
   - `LuxSync-AIAutomator-v0.1.0-Windows-x64.zip` (Standalone Windows)
   - `LuxSync-DMX-AU-v0.1.0-macOS-arm64.dmg` (AU Plugin macOS)
   - `LuxSync-DMX-VST3-v0.1.0-macOS-arm64.dmg` (VST3 Plugin macOS)
   - `LuxSync-AIAutomator-v0.1.0-macOS-arm64.dmg` (Standalone macOS)
   - (+ versiones x86_64 para macOS)

**¡Listo!** Ya puedes compartir los links de descarga.

---

## 🔄 Futuras Actualizaciones

Cuando hagas cambios y quieras una nueva release:

```bash
# 1. Hacer cambios
# ... editar archivos ...

# 2. Commit
git add .
git commit -m "Describe your changes"
git push origin main

# 3. Crear nuevo tag
git tag -a v0.2.0 -m "Release v0.2.0: Added sACN output + new motion figures"
git push origin v0.2.0

# GitHub Actions automáticamente compila y crea la release
```

---

## 🆘 Troubleshooting

### Error: "fatal: not a git repository"
```bash
cd C:\Users\Aragonlu\dmx-vst
git init
```

### Error: "remote origin already exists"
```bash
git remote set-url origin https://github.com/TuUsuario/dmx-vst.git
```

### Error: "fatal: 'origin' does not appear to be a 'git' repository"
```bash
git remote remove origin
git remote add origin https://github.com/TuUsuario/dmx-vst.git
```

### GitHub Actions falla en compilación
1. Abre https://github.com/TuUsuario/dmx-vst/actions
2. Click en el workflow fallido
3. Mira el log (scroll hacia abajo)
4. Errores comunes:
   - **"CMake not found"** → GitHub la instala automáticamente, puede haber timeout
   - **"NSIS not found"** → El instalador Windows necesita NSIS, actualmente se intenta instalar vía chocolatey en el runner
   - **"macOS compilation error"** → Verifica Xcode version en el runner

### Push rechazado por tamaño
Si hay archivos de compilación:
```bash
git reset HEAD~1  # Undo commit
git status
rm -r build/  # Remove build artifacts
git add .
git commit -m "Initial commit (without build artifacts)"
git push origin main
```

---

## 📱 Instalación de Usuarios

Una vez que tengas los releases, los usuarios pueden:

1. **Windows**: Descargar `.exe` y ejecutar el instalador
2. **macOS**: Descargar `.dmg` y arrastrar plugin/app a la carpeta sugerida
3. **Standalone**: Descargar `.zip` / `.dmg` y ejecutar el `.exe` / `.app`

Instrucciones completas en [README.md](../README.md) o [RELEASE_GUIDE.md](../RELEASE_GUIDE.md).

---

## 🎯 Checklist Final

- [ ] Repositorio creado en GitHub (`https://github.com/TuUsuario/dmx-vst`)
- [ ] Código pusheado: `git push -u origin main`
- [ ] Tag creado: `git tag -a v0.1.0 -m "..."`
- [ ] Tag pusheado: `git push origin v0.1.0`
- [ ] GitHub Actions compila (chequea en /actions)
- [ ] Release aparece en /releases con 4+ instaladores
- [ ] Descargas funcionan y plugins instalan correctamente

---

## 📚 Documentación Adicional

- [README.md](../README.md) — Portada del proyecto
- [RELEASE_GUIDE.md](../RELEASE_GUIDE.md) — Guía detallada de build & release
- [PROJECT_OVERVIEW.md](../PROJECT_OVERVIEW.md) — Arquitectura del proyecto

---

**¡Listo para subir!** 🚀
