# Roadmap

Estado por fases (ver especificación original, sección 11).

| Fase | Entregable | Estado |
|---|---|---|
| 1 | Esqueleto: CMake + JUCE 8, VST3/AU/CLAP/Standalone passthrough, CI Win/Mac/Linux, pluginval | ✅ Hecho (prueba manual en FL pendiente) |
| 2 | PitchDetector (YIN/MPM) + perfiles + voicing + medidor de afinación | ✅ Hecho (prueba manual en FL pendiente) |
| 3 | NoteMapper + CorrectionCurve + PSOLA (Live/Tracking) + transposición + mezcla + latencia | ✅ Hecho (prueba manual en FL pendiente) |
| 4 | FormantProcessor + motor HQ (signalsmith-stretch) + modo Vintage | ✅ Hecho (prueba manual en FL pendiente) |
| 5 | VibratoAnalyzer / Conservar vibrato / VibratoSynth / visualizador | ✅ Hecho (prueba manual en FL pendiente) |
| 6 | UI completa + presets + redimensionado + MIDI Learn | ✅ Hecho (pendiente tu revisión visual y prueba en FL) |
| 7 | HarmonyEngine (4 voces) + MIDI Target + aprender escala + acordes MIDI | ✅ Hecho (prueba manual en FL pendiente) |
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

### Fase 2
- [ ] Prueba manual con voz real en FL Studio (Windows y Mac): el medidor debe seguir la voz de forma estable (`docs/TESTING_FL.md`, sección Fase 2).
- [ ] Afinar umbrales de voicing con grabaciones reales de voz (hasta ahora solo señales sintéticas).
- [ ] Referencia A4 (400–480 Hz) para el medidor: llega con el parámetro de la Fase 3.
- [ ] MIDI Learn (Fase 6) en VST3: sin emulación de CC, el aprendizaje interno solo verá CC en CLAP/AU; en VST3 se documentará "Link to controller" de FL.

### Fase 3
- [ ] Prueba de escucha con voz real en FL Studio (Windows y Mac) — `docs/TESTING_FL.md`, sección Fase 3. También se puede usar `tools/render` con tomas propias.
- [ ] Compensar el retardo de detección (≈ media ventana + mediana) alineando el control con el audio en Tracking/HQ (hoy el control llega ~5–10 ms tarde: suaviza ligeramente los ataques con velocidad 0).
- [ ] Modo Live: retardo extra de hasta ~1 periodo en partes con voz (documentado en DSP_NOTES); valorar ventanas asimétricas más cortas.
- [ ] Teclado de 12 notas en la UI (estados Activa/Ignorar/Eliminar): los parámetros `noteState0..11` ya existen y son automatizables; el componente visual llega en la Fase 6.
- [x] HQ (signalsmith-stretch) y Vintage reales (Fase 4).

### Fase 4
- [x] Submódulos `signalsmith-stretch` 1.4.0 y `signalsmith-linear` 0.6.4 (sin FetchContent).
- [ ] Prueba de escucha en FL (Windows y Mac) de HQ, Vintage y formantes — `docs/TESTING_FL.md`, sección Fase 4.
- [ ] Cambio de modo a/desde HQ sin hueco: hoy el motor nuevo arranca desde cero (~100 ms de silencio y fundido). Opción: alimentar ambos motores durante un crossfade corto.
- [ ] CPU de HQ a 96/192 kHz (11–23 % de un núcleo en Linux): procesar HQ a una frecuencia interna de 48 kHz con re-muestreo, o bloque/intervalo escalados.
- [ ] Sesgo residual de HQ en transposiciones grandes hacia arriba (~+2 ¢ a +12 st). Investigar con signalsmith (peak mapping) o compensar con la frecuencia detectada.
- [ ] Formant shift en PSOLA con factores > 2 (p. ej. +12 st + carácter): los granos dejan de solaparse y el sonido se vuelve "zumbante"; documentar o limitar en la UI.

### Fase 5
- [ ] Prueba con voz real en FL: vibrato conservado con corrección rápida, eliminado al 0 %, exagerado al 200 %, vibrato artificial — `docs/TESTING_FL.md`, sección Fase 5.
- [ ] El analizador necesita ~0,6 s de oscilación para reconocer el vibrato: con velocidad rápida el inicio del vibrato de cada nota se aplana. Opción: ventana adaptativa (más corta para velocidades altas) o detección provisional con menos confianza.
- [ ] Preset "Hard tune" (Fase 6) con Conservar vibrato = 0 % (el valor por defecto 100 % deja pasar el vibrato natural incluso con velocidad 0).

### Fase 6
- [x] Pestañas Corrección / Formantes / Vibrato (adelantado en la Fase 5).
- [x] Presets de fábrica (Por defecto, Corrección natural, Pop moderno, Hard tune, Trap / urbano latino, Directo, Voz grave) y de usuario (`Documentos/NovaPitch/Presets/*.novapreset`).
- [x] MIDI Learn (clic derecho en cualquier knob), mapeos guardados en el proyecto.
- [x] Color de acento configurable (guardado en el proyecto).
- [ ] **Revisión visual por tu parte** (criterio de aceptación de la Fase 6): capturas en el artefacto `UI-snapshots-*` de cada ejecución de CI.
- [x] Preset "Armonías 3as" (Fase 7).
- [ ] Indicador de preset modificado ("*") y comparación A/B.
- [ ] Archivo global de usuario para mapeos MIDI por defecto (opcional en la especificación; hoy se guardan en cada proyecto).
- [ ] Teclado de 12 notas en la UI (estados Activa/Ignorar/Eliminar): los parámetros `noteState0..11` existen y son automatizables; falta el componente visual.
- [ ] MIDI Learn en VST3 dentro de FL: FL no envía CC a efectos VST3 sin parámetros CC emulados (desactivados a propósito). Se documenta "Link to controller" de FL; en CLAP/AU/Standalone funciona directo.
- [ ] Guardar el tamaño de la ventana del editor en el estado del plugin.
- [ ] Color de acento configurable desde la UI (la infraestructura `NovaLookAndFeel::setAccent` ya existe).

### Fase 7
- [ ] Prueba en FL: armonías por acordes desde el piano roll (canal MIDI Out → puerto del plugin), MIDI Target y aprender escala — `docs/TESTING_FL.md`, sección Fase 7.
- [ ] CPU: las voces de armonía comparten el análisis de f0 pero cada una marca sus propias épocas; compartir las épocas de la voz principal ahorraría ~30 % por voz.
- [ ] Armonías en modo HQ usan PSOLA (alineado con la latencia de HQ); un modo HQ para las voces costaría ~5 % de CPU por voz.
- [ ] Asignación de voces en modo MIDI: hoy voz i = i-ésima nota más grave; opción de "nota más cercana" para voice leading.

### Fase 9
- [ ] ARA SDK 2.3.0 (compatible con JUCE 8.0.13+).
- [ ] AAX: requiere AAX SDK, cuenta de desarrollador Avid y firma PACE/wraptool (opción `NOVAPITCH_ENABLE_AAX`).

### Fase 10
- [ ] `docs/USER_GUIDE_es.md` completa.
- [ ] Firma Developer ID + notarización en macOS; firma Authenticode opcional en Windows.
