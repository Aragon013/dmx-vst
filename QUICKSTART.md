# 🚀 LuxSync DMX — Guía Rápida de GitHub + Releases

## ⏱️ TL;DR (5 minutos)

```bash
# 1. Crea repo vacío en https://github.com/new (Sin README)
# 2. En PowerShell, desde la carpeta del proyecto:

git init
git add .
git commit -m "Initial commit: LuxSync DMX VST3 + AI Automator"
git branch -M main
git remote add origin https://github.com/TuUsuario/dmx-vst.git
git push -u origin main

# 3. Crea tag para disparar releases automáticas
git tag -a v0.1.0 -m "Initial release"
git push origin v0.1.0

# 4. Espera 15-30 minutos mientras GitHub Actions compila...
# 5. Abre https://github.com/TuUsuario/dmx-vst/releases
# 6. ¡Descarga los 4+ instaladores automáticos!
```

---

## 📦 Qué se genera automáticamente

| Archivo | Plataforma | Uso |
|---------|-----------|-----|
| `LuxSync-DMX-VST3-v0.1.0-Windows-x64.exe` | Windows | Instalar VST3 en DAW |
| `LuxSync-AIAutomator-v0.1.0-Windows-x64.zip` | Windows | Ejecutable Automator |
| `LuxSync-DMX-AU-v0.1.0-macOS-arm64.dmg` | macOS (M1/M2/M3) | Logic Pro, GarageBand |
| `LuxSync-DMX-VST3-v0.1.0-macOS-arm64.dmg` | macOS (M1/M2/M3) | DAWs genéricos |
| `LuxSync-AIAutomator-v0.1.0-macOS-arm64.dmg` | macOS (M1/M2/M3) | Ejecutable Automator |
| `LuxSync-DMX-AU-v0.1.0-macOS-x86_64.dmg` | macOS (Intel) | Logic Pro, GarageBand |
| `LuxSync-DMX-VST3-v0.1.0-macOS-x86_64.dmg` | macOS (Intel) | DAWs genéricos |
| `LuxSync-AIAutomator-v0.1.0-macOS-x86_64.dmg` | macOS (Intel) | Ejecutable Automator |

---

## 🔄 Futuras Releases

```bash
# Edita lo que quieras...
git add .
git commit -m "Add feature X"
git push origin main

# Crea nuevo tag
git tag -a v0.2.0 -m "v0.2.0: Added sACN output"
git push origin v0.2.0

# GitHub Actions compila automáticamente → release en /releases
```

---

## ⚙️ Cómo Funciona

```
TÚ: git push origin v0.1.0
    ↓
GITHUB: Detecta nuevo tag matching v*
    ↓
GITHUB ACTIONS: Inicia 3 jobs en paralelo
    ├─ build-windows: Compila VST3 + Standalone
    ├─ build-macos: Compila AU/VST3 + Standalone (arm64 + x86_64)
    └─ create-release: Descarga todos los artefactos y crea Release
    ↓
USERS: Descargan desde /releases
    ├─ Windows Users: Ejecutan .exe (NSIS installer)
    ├─ macOS Users: Abren .dmg y arrastran plugin/app
    └─ DJs: Descargan Standalone ZIP/DMG
```

---

## 🆘 Problemas Comunes

### "fatal: 'origin' does not appear to be a 'git' repository"
```bash
git remote remove origin
git remote add origin https://github.com/TuUsuario/dmx-vst.git
git push -u origin main
```

### GitHub Actions falla compilando
1. Abre https://github.com/TuUsuario/dmx-vst/actions
2. Haz click en el workflow fallido
3. Mira el log del error
4. Problemas típicos:
   - **NSIS no instalado**: Los runners de GitHub lo instalan automáticamente (puede tardar)
   - **CMake error**: Rara vez, reintentar el workflow
   - **Timeout**: Aumenta timeout en `.github/workflows/release.yml`

### macOS: "Developer cannot be verified"
- Es normal en macOS Sonoma+
- Los usuarios pueden: `sudo spctl --add /Library/Audio/Plug-Ins/VST3/...`
- O aceptar en System Settings → Security & Privacy

---

## 📚 Documentación Completa

- [GITHUB_SETUP_SPANISH.md](GITHUB_SETUP_SPANISH.md) — Guía paso-a-paso detallada
- [RELEASE_GUIDE.md](RELEASE_GUIDE.md) — Build local, empaquetado, troubleshooting
- [README.md](README.md) — Portada del proyecto

---

## ✅ Checklist

- [ ] Repo creado en GitHub
- [ ] Código pusheado: `git push -u origin main`
- [ ] Tag creado y pusheado: `git push origin v0.1.0`
- [ ] GitHub Actions ejecutándose (chequea en /actions)
- [ ] Release aparecido en /releases con instaladores
- [ ] Descargaste y testeaste los instaladores
- [ ] ¡Compartiste con amigos/usuarios!

---

**¡Listo para rockear! 🎛️**

¿Necesitas más detalles? Ve a [GITHUB_SETUP_SPANISH.md](GITHUB_SETUP_SPANISH.md)
