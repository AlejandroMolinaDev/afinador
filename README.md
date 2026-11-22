# NovaPitch

Plugin de **corrección de afinación vocal en tiempo real** (monofónico) para **FL Studio en Windows y macOS**, también compatible con cualquier DAW que cargue VST3, AU o CLAP.

> Nombre de trabajo. Verificar disponibilidad de la marca antes de publicar.

**Estado:** Fase 1 — esqueleto *passthrough* (el audio pasa sin cambios). Ver `docs/ROADMAP.md`.

| Formato | Windows x64 | macOS Universal (Apple Silicon + Intel) | Linux (opcional) |
|---|---|---|---|
| VST3 | ✅ | ✅ | ✅ |
| CLAP | ✅ | ✅ | ✅ |
| AU | — | ✅ | — |
| Standalone | ✅ | ✅ | ✅ |
| AAX | preparado, desactivado | preparado, desactivado | — |

FL Studio carga VST3 y CLAP (Windows y Mac) y AU (Mac). FL Studio **no** soporta ARA.

---

## Requisitos para compilar

**Windows**
- Visual Studio 2022 con la carga de trabajo *Desarrollo para el escritorio con C++*
- CMake ≥ 3.25 y Git

**macOS**
- Xcode ≥ 15 + Command Line Tools (`xcode-select --install`)
- CMake ≥ 3.25 (`brew install cmake`) y Git

**Linux (opcional)**: GCC ≥ 11 o Clang ≥ 14, CMake, Ninja y las dependencias de JUCE (`libasound2-dev libx11-dev libxrandr-dev libxcursor-dev libxinerama-dev libxext-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`).

## Compilar

```bash
git clone --recursive https://github.com/AlejandroMolinaDev/afinador.git
cd afinador
```
(Si ya lo clonaste sin `--recursive`: `git submodule update --init --recursive`.)

**Windows** (PowerShell o "Developer Command Prompt"):
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

**macOS**:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
```

Los plugins quedan en `build/NovaPitch_artefacts/Release/` (`VST3/`, `CLAP/`, `AU/`, `Standalone/`).

## Instalar y probar en FL Studio

### Windows
En una PowerShell **como administrador**:
```bash
powershell -ExecutionPolicy Bypass -File tools\install_plugins.ps1
```
Copia a `C:\Program Files\Common Files\VST3\NovaPitch.vst3` y `C:\Program Files\Common Files\CLAP\NovaPitch.clap`.

Sin permisos de administrador: añade `-UserFolder` (copia a `Documentos\NovaPitch-dev\`) y agrega esa carpeta en FL Studio → *Options → Manage plugins → Plugin search paths*.

Cierra FL Studio antes de reinstalar (mantiene el DLL bloqueado).

### macOS
```bash
./tools/install_plugins.sh
```
Copia a `~/Library/Audio/Plug-Ins/{VST3,Components,CLAP}`, firma ad-hoc y refresca la caché de Audio Units.

Si usas el zip descargado de GitHub Actions, quita la cuarentena antes de copiar:
```bash
xattr -dr com.apple.quarantine NovaPitch.vst3 NovaPitch.component NovaPitch.clap
```

### En FL Studio
1. *Options → Manage plugins → Find more plugins* (o *Find installed plugins*).
2. Busca **NovaPitch** (aparecerá como VST3, CLAP y, en Mac, AU; son entradas separadas).
3. Mixer → un slot de insert → NovaPitch.
4. La ventana muestra "FL Studio detectado", el formato cargado, frecuencia de muestreo, tamaños de bloque, canales y eventos MIDI recibidos.

Checklist completo de pruebas manuales: [`docs/TESTING_FL.md`](docs/TESTING_FL.md).

## Integración continua
GitHub Actions compila en Windows x64, macOS Universal y Linux; ejecuta los tests unitarios, **pluginval (strictness 10)**, `auval` (Mac) y **clap-validator**, y publica los binarios como artefactos descargables de cada ejecución (pestaña *Actions*).

## Licencias
- **JUCE 8**: licencia comercial (plan *Starter* gratuito hasta el límite de ingresos de JUCE) **o** AGPLv3. Para distribuir NovaPitch como software cerrado necesitas una licencia JUCE adecuada a tus ingresos.
- **VST3 SDK** (incluido en JUCE): MIT desde la versión 3.8.
- **CLAP** y **clap-juce-extensions**: MIT.
- **Catch2**: Boost Software License 1.0 (solo tests).

Detalle en [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

NovaPitch es una implementación original. No usa código, nombres ni diseño de productos comerciales de terceros.
