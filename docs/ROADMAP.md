# Roadmap

Estado por fases (ver especificación original, sección 11).

| Fase | Entregable | Estado |
|---|---|---|
| 1 | Esqueleto: CMake + JUCE 8, VST3/AU/CLAP/Standalone passthrough, CI Win/Mac/Linux, pluginval | ✅ Hecho (pendiente prueba manual en FL Studio) |
| 2 | PitchDetector (YIN/MPM) + perfiles + voicing + medidor de afinación | ⏳ |
| 3 | NoteMapper + CorrectionCurve + PSOLA (Live/Tracking) + transposición + mezcla + latencia | ⏳ |
| 4 | FormantProcessor + motor HQ (signalsmith-stretch) + modo Vintage | ⏳ |
| 5 | VibratoAnalyzer / Conservar vibrato / VibratoSynth / visualizador | ⏳ |
| 6 | UI completa + presets + redimensionado + MIDI Learn | ⏳ |
| 7 | HarmonyEngine (4 voces) + MIDI Target + aprender escala + acordes MIDI | ⏳ |
| 8 | Modo gráfico sin ARA (captura por reproducción, objetos, undo/redo) | ⏳ |
| 9 | Corrección temporal, detector de tonalidad + sync de instancias, ARA2, AAX, Linux | ⏳ |
| 10 | Instaladores (Inno Setup / .pkg firmado y notarizado), guía de usuario | ⏳ |

## Pendientes registrados

### Fase 1
- [ ] **Prueba manual en FL Studio** Windows y macOS (VST3, CLAP, AU) según `docs/TESTING_FL.md` — requiere tu máquina.
- [ ] Validador VST3 de Steinberg (`validator` del SDK) como paso independiente en CI. Hoy la cobertura viene de pluginval (que ejecuta los tests de conformidad VST3 en strictness ≥ 5).
- [ ] Caché de compilación en CI (ccache / sccache) para acelerar los jobs.
- [ ] clap-validator 0.3.2 es la última versión con binarios publicados detectables; revisar versiones nuevas (existe el tag 0.4.1).
- [ ] Evaluar migración a JUCE 9.x (existe 9.0.6). La especificación exige JUCE 8; revisar licencia, cambios de API y compatibilidad con clap-juce-extensions y ARA antes de migrar.
- [ ] Sustituir URLs `example.com` (web, manual, soporte) y `COMPANY_NAME` por los definitivos antes de publicar.

### Fase 4
- [ ] Añadir submódulo `signalsmith-stretch`.

### Fase 6
- [ ] Guardar el tamaño de la ventana del editor en el estado del plugin.
- [ ] Color de acento configurable desde la UI (la infraestructura `NovaLookAndFeel::setAccent` ya existe).

### Fase 9
- [ ] ARA SDK 2.3.0 (compatible con JUCE 8.0.13+).
- [ ] AAX: requiere AAX SDK, cuenta de desarrollador Avid y firma PACE/wraptool (opción `NOVAPITCH_ENABLE_AAX`).

### Fase 10
- [ ] `docs/USER_GUIDE_es.md` completa.
- [ ] Firma Developer ID + notarización en macOS; firma Authenticode opcional en Windows.
