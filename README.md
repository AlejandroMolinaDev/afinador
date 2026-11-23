# NovaPitch

Plugin de **corrección de afinación vocal en tiempo real** (monofónico) para **FL Studio en Windows y macOS**, también compatible con cualquier DAW que cargue VST3, AU o CLAP.

> Nombre de trabajo. Verificar disponibilidad de la marca antes de publicar.

![NovaPitch — Fase 9, modo gráfico con corrección temporal](docs/img/fase9-tiempo.png)

**Estado:** Fase 9 — corrección automática (tonalidad y 14 escalas, velocidad, tolerancia, sostenido natural, transposición, mezcla), 4 modos (Live 3 ms, Tracking 10 ms, HQ 100 ms, Vintage), formantes, vibrato, presets, MIDI Learn, 4 voces de armonía, MIDI Target, aprender escala por MIDI, **modo gráfico sin ARA** (captura reproduciendo en FL Studio, notas / líneas / curvas, deshacer ilimitado), **corrección temporal no destructiva**, **detector de tonalidad** («Escuchar») y **sincronización entre instancias**. ARA2 queda pospuesto (FL no lo soporta). **Fase 10:** instaladores (Windows Inno Setup, macOS `.pkg` Universal) y [guía de usuario en español](docs/USER_GUIDE_es.md). Versión **0.9.0 (beta)**: falta tu prueba en FL Studio para llegar a 1.0.0. Ver `docs/ROADMAP.md`.

## Instalar (usuarios)
Descarga el instalador de la última ejecución de CI (pestaña *Actions* → artefactos) o de *Releases*:
- **Windows**: `NovaPitch-<versión>-Windows-x64-Setup.exe` (artefacto `NovaPitch-Windows-x64-Setup`) → VST3 y CLAP en `C:\Program Files\Common Files\{VST3,CLAP}`, aplicación y guía en `C:\Program Files\NovaPitch`.
- **macOS**: `NovaPitch-<versión>-macOS.pkg` (artefacto `NovaPitch-macOS-Installer`) → `/Library/Audio/Plug-Ins/{VST3,Components,CLAP}`, `/Applications/NovaPitch.app` y la guía en `/Library/Application Support/NovaPitch`. Las compilaciones de CI sin certificados de Apple no van firmadas: clic derecho → *Abrir*.
- Después, en FL Studio: *Options → Manage plugins → Find installed plugins*.

La [guía de usuario](docs/USER_GUIDE_es.md) explica cada control, el modo gráfico, la corrección temporal y el enrutado MIDI en FL.

### Modo gráfico en FL Studio (sin ARA)
1. Botón **Gráfico** en la cabecera → **● Capturar** → reproduce la canción: la curva de afinación se dibuja.
2. **Importar desde automático**: crea notas con la tonalidad y escala actuales.
3. Edita con **Seleccionar / Nota / Línea / Curva / Borrar / Dividir** (clic derecho borra, Ctrl/Cmd+Z deshace). El inspector ajusta velocidad, vibrato y formante por nota.
4. Reproduce: la voz sigue lo dibujado; fuera de los objetos o con el transporte parado pasa la voz original.
5. **Tiempo**: arrastra una nota para mover la sílaba o sus bordes para estirarla (las vecinas se ajustan). Es no destructivo: el audio capturado se guarda en tu carpeta de usuario (`···` → *Abrir carpeta de capturas de audio*), nunca se modifica el audio del proyecto.

### Tonalidad automática y varias pistas
- **Escuchar** (junto a *Tonalidad*): escucha 10–30 s de voz y ajusta Tonalidad y Escala (también desde la captura del modo gráfico).
- **Sincronizar** (junto a *Escala*): envía tonalidad, escala, notas y A4 a las demás instancias de NovaPitch del proyecto.

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
- Visual Studio 2022 o posterior con la carga de trabajo *Desarrollo para el escritorio con C++* (CMake usa automáticamente la versión más nueva instalada)
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
cmake -B build -A x64
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

## Instalar y probar en FL Studio (desarrollo)

Para crear los instaladores en local:
```bash
python3 tools/docs/build_guide.py docs/USER_GUIDE_es.md build/Guia_de_usuario.html   # requiere: pip install markdown
```
```bash
iscc /DAppVersion=0.9.0 /DArtefactsDir=%CD%\build\NovaPitch_artefacts\Release /DDocsDir=%CD%\build /DOutputDir=%CD%\dist installer\windows\NovaPitch.iss
```
```bash
installer/macos/build_pkg.sh build/NovaPitch_artefacts/Release 0.9.0 build/Guia_de_usuario.html dist
```
(En macOS, `DEVELOPER_ID_APP`, `DEVELOPER_ID_INSTALLER` y `NOTARY_PROFILE` activan la firma y la notarización.)

Copiar los plugins sin instalador:

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

## Presets y MIDI Learn
- **Presets**: selector en la cabecera (`<` `>` para recorrerlos, **Guardar** para crear uno de usuario). Los presets cambian el sonido, nunca la tonalidad, la escala, las notas ni la referencia A4. Los de usuario se guardan en `Documentos/NovaPitch/Presets` (archivos `.novapreset`, se pueden copiar entre equipos).
- **MIDI Learn**: clic derecho sobre cualquier knob → *Asignar a controlador MIDI (aprender)* y mueve un control de tu teclado/controlador. Funciona en CLAP, AU y Standalone. En **VST3 dentro de FL Studio** usa *Link to controller* de FL (FL no envía CC a efectos VST3).
- **Color de acento**: botón `···` de la cabecera.

## Armonías y MIDI en FL Studio
- Pestaña **Armonías**: 4 voces; modo *Diatónica* (intervalo en la escala: 3ª arriba, 6ª abajo...), *Semitonos* o *MIDI* (cada voz canta una nota del acorde que tocas). Formante, panorama, nivel, humanizar, mute/solo por voz. Pestaña **Bus**: EQ, gate, ataque/liberación y ancho de todas las armonías.
- Pestaña **MIDI**: *nota objetivo* (la nota que tocas es la nota corregida) y *aprender escala*.
- Para enviar MIDI a NovaPitch en FL: ventana del plugin → ⚙ → *MIDI* → *Input port* = 1; luego un canal *MIDI Out* con *Port* = 1 y escribe las notas en su Piano roll.

## Probar sin DAW: render offline
`NovaPitchRender` procesa un WAV/AIFF/FLAC con los parámetros que quieras y compensa la latencia, para comparar antes/después con tus propias tomas:
```bash
build/tools/NovaPitchRender mi_voz.wav mi_voz_hardtune.wav key=9 scale=1 retuneSpeed=0
```
Sin argumentos lista todos los parámetros (IDs y valores por defecto). `tools/demo/run_demo.sh` genera una frase sintética desafinada, la procesa con varios ajustes y dibuja el pitch antes/después (requiere Python con numpy y matplotlib); la CI publica ese resultado en el artefacto `Audio-demo`.

## Integración continua y pruebas automáticas
GitHub Actions compila en Windows x64, macOS Universal y Linux y ejecuta:
- tests unitarios (Catch2) con señales sintéticas (precisión de detección < 5 cents);
- **host test** (`tools/hosttest`): carga el binario VST3 (y AU en Mac) como lo haría un DAW y simula el comportamiento de FL Studio (bloques irregulares y vacíos, cambios de sample rate, automatización, MIDI, bypass, guardar/reabrir, varias instancias, latencia, abrir/cerrar editor mientras suena);
- **pluginval (strictness 10)**, `auval` (Mac) y **clap-validator**;
- benchmark de CPU (aparece en el resumen de la ejecución) y capturas de la interfaz (`UI-snapshots-*`).

Los binarios quedan como artefactos descargables de cada ejecución (pestaña *Actions*). FL Studio no puede ejecutarse en CI, por eso la prueba final con voz real sigue en `docs/TESTING_FL.md`.

## Licencias
- **JUCE 8**: licencia comercial (plan *Starter* gratuito hasta el límite de ingresos de JUCE) **o** AGPLv3. Para distribuir NovaPitch como software cerrado necesitas una licencia JUCE adecuada a tus ingresos.
- **VST3 SDK** (incluido en JUCE): MIT desde la versión 3.8.
- **CLAP** y **clap-juce-extensions**: MIT.
- **signalsmith-stretch** y **signalsmith-linear** (motor HQ): MIT.
- **Catch2**: Boost Software License 1.0 (solo tests).

Detalle en [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

NovaPitch es una implementación original. No usa código, nombres ni diseño de productos comerciales de terceros.
