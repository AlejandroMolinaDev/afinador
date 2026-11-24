# Pruebas automáticas: qué cubren y qué queda para FL Studio

Mientras no se pueda probar en FL Studio, NovaPitch se verifica con varias capas de pruebas automáticas que la CI ejecuta en **Windows x64, macOS Universal y Linux x64** en cada cambio. Este documento recorre los 114 puntos de [`TESTING_FL.md`](TESTING_FL.md) e indica, para cada uno, qué prueba automática lo cubre y qué parte depende de verdad de FL Studio o de un equipo real.

## Las capas de prueba

| Capa | Qué hace | Cómo se ejecuta |
|---|---|---|
| **Unitarias y DSP** (`NovaPitchTests`, 103 casos) | Detección, corrección, formantes, vibrato, armonías, MIDI, presets, A/B, estado, modo gráfico, tiempo real sin reservas de memoria | `ctest --test-dir build` |
| **Interfaz** (`NovaPitchUiTests`, 23 casos, ~5000 comprobaciones) | El editor real en una ventana del escritorio, manejado con ratón y teclado: cada control, cada pestaña, menús, diálogos, el modo gráfico completo; maquetación en 75/100/150/200 %; capturas de cada escenario | igual (en Linux bajo `xvfb-run -a`) |
| **Host test** (`NovaPitchHostTest`) | Carga el **binario** VST3 / AU como un DAW: 6 frecuencias × 4 tamaños de bloque con bloques irregulares y vacíos, automatización, MIDI, bypass, latencia medida = informada, guardar/reabrir, 8 instancias, mono/estéreo, editor abierto/cerrado con audio, modo gráfico con transporte | `NovaPitchHostTest <plugin> --editor` |
| **Ventana real de host** | El editor que dibuja el propio binario VST3/AU, incrustado en una ventana de host al 100 % y al 200 % (redimensionado por `checkSizeConstraint`/`onSize`, como FL) con audio sonando; en Linux se captura la pantalla y se comprueba que se ve el plugin | `NovaPitchHostTest <plugin> --show-editor <carpeta>` |
| **Validadores** | pluginval (nivel 10), clap-validator, auval estricto | CI |
| **Instaladores** | Se construyen, se instalan como un usuario, se prueba el plugin instalado y se desinstala (Windows y macOS) | CI |
| **Rendimiento y audio** | CPU por perfil y frecuencia, render offline y gráfica de afinación de una frase sintética | `NovaPitchBench`, `tools/demo/run_demo.sh` |

### Cómo funcionan las pruebas de interfaz

- El editor se abre en una **ventana real** y los eventos de ratón, rueda y teclado se inyectan por el *peer* de la ventana, el mismo punto de entrada que usa el sistema operativo. JUCE hace la detección de clics (también a través del escalado 75–200 %), el arrastre, el doble clic y el bloqueo de los diálogos modales igual que con un ratón real.
- Cada clic comprueba además que el punto cae **sobre el control buscado**: si otro componente o ventana lo tapara, la prueba falla.
- La **maquetación** se comprueba automáticamente en todas las pestañas, en cada voz de armonía, en la vista gráfica vacía y con selección, y en 4 escalas:
  - ningún control se sale de la ventana ni queda recortado;
  - no hay controles solapados;
  - el centro de cada control recibe el clic;
  - todos los textos caben sin comprimirse, medidos con la fuente real de cada sistema: botones, interruptores, rótulos, los valores de cada knob en mínimo, máximo y por defecto, y todos los elementos de cada desplegable;
  - los controles tienen un tamaño mínimo para poder pulsarlos.
- Las **capturas** de cada escenario (al 100 % y al 200 %) se suben como artefactos `UI-test-shots-*`, y en Linux también las capturas de pantalla de la ventana real del host.

## Defectos encontrados y corregidos con estas pruebas

| Defecto | Corrección |
|---|---|
| Al cerrar la ventana del plugin con un menú (Escuchar, MIDI Learn) o un diálogo (Guardar preset, desafinación exacta) abierto, el programa usaba memoria ya liberada: posible cierre inesperado de FL | Los callbacks comprueban si el editor sigue existiendo. Lo cubre el test «Closing the window with a menu or a dialog open is safe» (verificado también con AddressSanitizer) |
| Diez textos comprimidos o cortados: A/B, «Guardar», «Seleccionar», «Deshacer», «Rehacer», «Importar desde automático», «Limpiar…», «Ajustar», «Profundidad» y el rótulo de MIDI Learn | Las barras del modo gráfico y de presets calculan el ancho con la fuente real de cada sistema operativo. La asignación MIDI se muestra como una etiqueta «CCn» aparte y el rótulo de aprendizaje es más corto |
| Los menús propios (`···`, Escuchar, MIDI Learn, Importar, Limpiar, menú de cada nota) no usaban el aspecto de NovaPitch | Cada menú usa ahora el aspecto de NovaPitch |
| Tras guardar o cargar un preset, el «*» de modificado seguía visible un momento; elegir de nuevo el mismo preset justo después de tocar un knob podía no revertirlo | El estado se recalcula al refrescar la lista y en cada actualización de la interfaz |
| La captura del modo gráfico guardaba fotogramas antes del 0:00 de la canción, y con ellos la herramienta **Tiempo** no podía mover la primera nota | No se capturan tiempos negativos y la importación nunca crea objetos antes del 0. Una nota pegada al inicio se estira desde su final |

## Mapa de cobertura de `TESTING_FL.md`

Leyenda: ✅ cubierto por pruebas automáticas · 🟡 cubierto en parte (se indica qué falta) · 🎧 solo en FL Studio o con un equipo real.

### Fase 1 — Esqueleto

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 1 | FL detecta NovaPitch | 🟡 | El host test y pluginval escanean el binario con el mismo mecanismo de los DAW. Falta el escáner propio de FL |
| 2 | Aparece como efecto | ✅ | Host test «is an effect (not a synth)», identificadores congelados (AU `aumf`) |
| 3 | Pie: host y formato | 🟡 | UI «Meter, footer…» (versión, frecuencia, bloques, canales, latencia, MIDI); la ventana real muestra «VST3». Falta ver el nombre «FL Studio» |
| 4 | El audio pasa sin cambios | ✅ | «Dry path (mix 0) is the input delayed by the reported latency», host test bypass |
| 5 | Estéreo → Estéreo | ✅ | Host test de disposiciones, pie en la prueba de UI |
| 6 | Latencia sin desfase | 🟡 | Latencia medida (impulso) = informada en los 4 modos. Falta que FL aplique su compensación (PDC) |
| 7 | Cambiar frecuencia de muestreo | ✅ | Host test: 6 frecuencias × 4 bloques; «Re-preparing at a new sample rate» |
| 8 | Buffer irregular | ✅ | Host test con bloques irregulares y vacíos; el pie muestra mínimo y máximo |
| 9 | Exportar = sin plugin | ✅ | Ruta seca y render offline (mismo código y resultado determinista) |
| 10 | 8+ instancias | ✅ | Host test «8 instances»; UI: dos instancias con editor y audio en hilos propios |
| 11 | Guardar / reabrir | ✅ | Host test «save / reopen project restores state», pruebas de estado |
| 12 | Abrir/cerrar la ventana tocando | ✅ | Host test (editor ×4 con audio); UI: cerrar y reabrir con audio, y con menús o diálogos abiertos |
| 13 | Bypass sin clics | ✅ | Host test «bypass on/off while playing» |
| 14 | Texto nítido a 100/150/200 % de Windows | 🟡 | Maquetación y capturas a 1× y 2×, ventana real al 200 %. Falta el escalado de Windows dentro de FL |
| 15 | Monitores con DPI distinto | 🎧 | Requiere dos monitores |
| 16 | Redimensionar desde la esquina | ✅ | UI «Resize: the corner…» (arrastre real, límites y proporción); ventana real al 200 % por la ruta VST3 |
| 17 | Retina nítido | 🟡 | Render a 2× y CI de macOS. Falta una pantalla Retina física |
| 18 | MIDI hacia el efecto | 🟡 | Host test con MIDI; UI: contador «MIDI n» y notas recibidas. Falta el enrutado de puertos de FL |
| 19 | arm64 nativo | 🟡 | La CI verifica el binario universal y el pie muestra la arquitectura. Falta FL nativo |
| 20 | Rosetta / Intel | 🎧 | Requiere FL bajo Rosetta o un Mac Intel |
| 21 | Gatekeeper | 🎧 | Requiere un Mac limpio con el binario descargado |

### Fase 2 — Detección y medidor

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 22 | Medidor estable con micrófono | 🟡 | Detector: senos, sierras, fundamental débil, saltos de octava, ruido; UI: el medidor muestra la nota en vivo; ventana real. Falta una voz real |
| 23 | Cents suaves, «afinado» | 🟡 | Mediana e histéresis del detector. Falta valorarlo a ojo con voz real |
| 24 | Sibilantes y respiraciones | 🟡 | «Noise and silence are unvoiced», sensibilidad. Falta voz real |
| 25 | Cambiar perfil cantando | ✅ | «Profile switch at runtime…», combo enlazado en ambos sentidos |
| 26 | Sensibilidad | ✅ | «Higher sensitivity accepts noisier signals» |
| 27 | Gráfico inferior | ✅ | Capturas de la UI y de la ventana real con audio |
| 28 | Automatizar desde FL | ✅ | Host test con automatización; UI: host → control y control → host con gestos (lo que graba FL) |
| 29 | Sin parámetros «MIDI CC» | ✅ | Host test: 97 parámetros (96 + bypass) |
| 30 | CPU en FL | 🟡 | `NovaPitchBench` en la CI. Falta el monitor de FL |

### Fase 3 — Corrección automática

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 31 | Velocidad 0 | ✅ | «Retune speed 0 gives a stepwise glissando», «Hard tune follows a sung note change at once» |
| 32 | Velocidad 40–80 ms | ✅ | «Natural vibrato is kept…», constante de tiempo |
| 33 | Tonalidad / escala | ✅ | Pruebas de NoteMapper y escalas |
| 34 | Tolerancia | ✅ | «Tolerance lets a slow glissando through…» |
| 35 | Sostenido | ✅ | «Natural hold slows correction on long notes only» |
| 36 | Transposición sin «ardilla» | ✅ | «Formants are preserved under transposition…» |
| 37 | Mezcla sin phasing | ✅ | Ruta seca alineada con la latencia informada |
| 38 | Cambio de modo y PDC | 🟡 | Latencia por modo informada = medida. Falta que FL reajuste la compensación |
| 39 | Exportar = tiempo real | ✅ | Proceso determinista; render offline |
| 40 | Consonantes sin artefactos | 🟡 | «No clicks at voiced / unvoiced transitions». Falta escucharlo con voz real |
| 41 | Automatizar sin clics | ✅ | Host test con automatización; sin clics |
| 42 | CPU Live / Tracking | 🟡 | `NovaPitchBench`. Falta el monitor de FL |

### Fase 4 — Formantes, HQ y Vintage

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 43 | HQ: FL compensa 100 ms | 🟡 | HQ informa = mide 4800 muestras. Falta la PDC de FL |
| 44 | HQ más limpio | 🎧 | Valoración de escucha |
| 45 | Exportar con HQ | ✅ | Render offline con latencia compensada |
| 46 | Preservar formantes | ✅ | Prueba de formantes (Tracking y HQ) |
| 47 | Desplazar formantes | ✅ | «Formant shift moves the spectral envelope…» |
| 48 | Carácter | ✅ | «Character scales the envelope…» |
| 49 | Enlazar a transposición | ✅ | «Formant link: the envelope follows the transposition…» (nueva) |
| 50 | Vintage | ✅ | «Vintage mode flattens vibrato…» |
| 51 | CPU HQ | 🟡 | `NovaPitchBench` |

### Fase 5 — Vibrato

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 52 | Conservar 100 % | ✅ | «Natural vibrato is kept (or scaled)…» |
| 53 | Conservar 0 % / 200 % | ✅ | Ídem |
| 54 | Visualizador del vibrato | ✅ | Analizador (velocidad y profundidad ±10 %); captura de la pestaña con vibrato |
| 55 | Vibrato artificial | ✅ | «Artificial vibrato is added to sustained notes after its delay» |
| 56 | Variación, amplitud, formante | 🟡 | Variación acotada, sin reservas de memoria. Falta escucharlo |
| 57 | Cambiar de pestaña tocando | ✅ | UI: pestañas, presets, A/B y modo gráfico con audio en otros hilos |

### Fase 6 — Presets, MIDI Learn, apariencia

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 58 | Recorrer presets con `<` `>` | ✅ | UI (clics reales, vuelta completa); la tonalidad no cambia |
| 59 | Guardar preset de usuario | ✅ | UI: diálogo, nombre con «ñ», archivo, lista, recarga, cancelar, borrar |
| 60 | Abrir carpeta de presets | 🟡 | El elemento del menú existe. No se abre el explorador en la CI |
| 61 | MIDI Learn (CLAP) | 🟡 | UI: aprender por el menú, un CC mueve el knob, etiqueta «CC21», quitar. Falta que FL envíe CC al CLAP |
| 62 | VST3: Link to controller | 🎧 | Función de FL (el menú lo explica) |
| 63 | Mapeos guardados | ✅ | Pruebas de MIDI Learn y estado |
| 64 | Color de acento guardado | ✅ | UI: cambia y se recupera al reabrir el proyecto |
| 65 | Redimensionar en HiDPI | 🟡 | Ver 14 y 17 |

### Fase 7 — Armonías y MIDI

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 66 | Preset Armonías 3as | ✅ | Pasos diatónicos y afinación de las voces |
| 67 | Voces MIDI con acordes | 🟡 | Armonías por el procesador. Falta el Piano roll de FL |
| 68 | MIDI: nota objetivo | ✅ | «MIDI Target: the held note is the target» |
| 69 | Aprender escala | ✅ | «Learn scale from MIDI writes a custom scale» |
| 70 | La pestaña MIDI muestra notas | ✅ | UI: «C4  E4» al tocar y vacío al soltar |
| 71 | Humanizar, panorama, mute/solo, bus | 🟡 | Gate, panorama, mute; controles enlazados. Falta escuchar EQ y ancho |
| 72 | CPU 4 voces | 🟡 | `NovaPitchBench` |

### Fase 8 — Modo gráfico

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 73 | Auto / Gráfico y automatización | ✅ | UI: el botón cambia el parámetro con gesto y la vista sigue a la automatización |
| 74 | Capturar y Seguir | ✅ | UI: captura con transporte, texto «Capturando…», la vista avanza con el cursor |
| 75 | Importar, Deshacer / Rehacer | ✅ | UI: tres niveles de detalle, curvas, botones de deshacer y rehacer |
| 76 | Mover, bordes, Dividir, Unir, clic derecho | ✅ | UI con ratón (semitonos, Alt libre, borde derecho, recuadro, Mayús+clic, Unir) |
| 77 | Línea y Curva | ✅ | UI dibujando; «Graphical mode end to end» |
| 78 | Inspector por nota | ✅ | UI: velocidad por nota; en curvas solo vibrato |
| 79 | Bucle alineado | ✅ | Host test: reproducir, bucle, parar, reabrir (desvío 0,2–0,4 ¢) |
| 80 | Capturar en Tracking y reproducir en HQ | 🎧 | Depende de cómo aplica FL la PDC a la posición (en `ROADMAP.md`) |
| 81 | Recapturar un tramo | ✅ | «Capture passes merge by time range»; UI con segunda pasada |
| 82 | Guardar / reabrir; tamaño del .flp | 🟡 | Prueba de punta a punta con reapertura. El tamaño del .flp, en FL |
| 83 | Atajos de teclado | 🟡 | UI: Supr, Ctrl+Z, Ctrl+Mayús+Z, Ctrl+A, flechas, 1–7, Esc. Falta ver si FL intercepta alguno |
| 84 | Rueda y zoom | 🟡 | UI: Ctrl+rueda (zoom), botones de zoom, Ajustar. Falta el gesto de pinza del trackpad |
| 85 | Varias pistas en modo gráfico | ✅ | UI: dos instancias capturando con audio en hilos propios |

### Fase 9 — Tonalidad, sincronización y tiempo

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 86 | Escuchar 10 s | ✅ | UI: contador en el botón, «Detener y aplicar», E mayor detectada y aplicada, mensaje en el pie |
| 87 | Escuchar desde la captura | ✅ | Prueba unitaria y menú habilitado tras capturar |
| 88 | Sincronizar dos pistas | ✅ | UI: desactivado con una sola instancia; con dos, envía tonalidad, escala y desafinación |
| 89 | Sincronizar en modo *bridged* | 🟡 | UI: desactivado con su explicación cuando no hay otra instancia. Falta el modo *bridged* real |
| 90 | Carpeta de capturas | 🟡 | WAV por pasada verificados. Abrir la carpeta, en el equipo |
| 91 | Herramienta Tiempo | ✅ | UI con ratón (mover la sílaba, nota al inicio); render y reproducción de punta a punta |
| 92 | Alt+clic, Limpiar, Deshacer | ✅ | UI |
| 93 | Reabrir con ediciones de tiempo | ✅ | «Time edit end to end… reopen» |
| 94 | Falta el audio capturado | ✅ | Prueba de tiempo con audio ausente |
| 95 | CPU con ediciones de tiempo | 🟡 | `NovaPitchBench` |

### Fase 10 — Instaladores

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 96 | Equipo sin herramientas de desarrollo | 🟡 | Runtime estático (`/MT`) e instalación en la CI. Falta una máquina limpia |
| 97 | FL encuentra el plugin instalado | 🟡 | La CI prueba el plugin desde las carpetas del sistema. Falta el escáner de FL |
| 98 | Proyecto de una versión anterior | ✅ | «Older state versions are migrated…» |
| 99 | Actualizar con FL abierto | 🎧 | Diálogo del instalador con FL en marcha |
| 100 | Guía instalada | ✅ | La CI comprueba los archivos instalados |
| 101 | Desinstalar conserva datos | 🟡 | La CI desinstala; los datos de usuario están fuera de las carpetas del instalador |
| 102 | Gatekeeper sin firma | 🎧 | Mac limpio |

### Auditoría, A/B y desafinación

| # | Punto | Estado | Prueba automática / qué falta |
|---|---|---|---|
| 103 | Pestaña Notas en vivo | ✅ | UI: clic en cada tecla (negras y blancas), ciclo de estados, nota detectada y destino resaltadas |
| 104 | Hard tune inmediato | ✅ | «Hard tune follows a sung note change at once» |
| 105 | Enviar notas MIDI | 🟡 | «MIDI out sends the corrected notes…». Falta el enrutado de puertos de FL |
| 106 | Octava exacta | ✅ | «MIDI target exact octave…» |
| 107 | Forma del vibrato | ✅ | Formas medidas; combo enlazado |
| 108 | Importar con detalle, curvas, flechas | ✅ | Pruebas unitarias y UI (menú y flechas) |
| 109 | A/B | ✅ | UI: A ↔ B, también con audio en marcha |
| 110 | Copiar A en B y reabrir | ✅ | UI y prueba unitaria |
| 111 | «*» de modificado | ✅ | UI (tonalidad no marca; elegir el mismo preset revierte) |
| 112 | Rueda, menú y «Otro valor…» | ✅ | UI: rueda ±1/±10 ¢, límite ±50, menú, diálogo con coma y signo «−» |
| 113 | E a −14 ¢ | ✅ | «Per-note detune through the processor…» |
| 114 | Armonía en escala desafinada | ✅ | «Harmony voices land on their own note's detune» |

## Resumen

- **73 de 114** puntos están cubiertos por completo por pruebas automáticas.
- **33** están cubiertos en parte: la lógica y el binario están probados, y falta la parte que pone FL o el equipo (PDC de FL, escáner, enrutado de puertos MIDI, escala de Windows, escucha con voz real).
- **8** dependen solo de FL o de hardware: monitores con DPI distinto, Rosetta, Gatekeeper (dos puntos), *Link to controller*, valoración de HQ, alineación HQ/Tracking en el modo gráfico y actualizar con FL abierto.

### Lo que queda para la prueba en FL

Cuando se pueda usar FL Studio, la prueba manual puede reducirse a estos puntos:

1. **Escaneo y carga.** Ver el nombre «FL Studio» en el pie (puntos 1, 3, 97).
2. **Compensación de latencia (PDC) de FL** al cambiar de modo y en HQ, y la alineación HQ/Tracking del modo gráfico (puntos 6, 38, 43, 80).
3. **Enrutado de MIDI de FL**: entrada al efecto, *Link to controller*, CC al CLAP y salida a un sintetizador (puntos 18, 61, 62, 67, 105).
4. **Atajos** que FL pudiera interceptar (punto 83) y modo *bridged* (punto 89).
5. **Escala de pantalla** de Windows 150/200 %, Retina y monitores con DPI distinto (puntos 14, 15, 17, 65).
6. **Escucha con voz real**: naturalidad, consonantes, HQ frente a Tracking (puntos 22–24, 40, 44, 56, 71).
7. **CPU** en el monitor de FL (puntos 30, 42, 51, 72, 95).
8. **Instalación en equipos limpios** y Gatekeeper (puntos 21, 96, 99, 101, 102).

## Ejecutar las pruebas

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
xvfb-run -a -s "-screen 0 1920x1600x24" ctest --test-dir build --output-on-failure   # Linux (en Windows/macOS sin xvfb-run)
ctest --test-dir build -L ui --output-on-failure                                     # solo la interfaz
build/tests/NovaPitchUiTests "[graph]"                                               # un grupo (etiquetas: layout, controls, presets, midi, key, sync, live, notes, harmony, graph, lifetime)
build/tools/NovaPitchHostTest_artefacts/Release/NovaPitchHostTest build/NovaPitch_artefacts/Release/VST3/NovaPitch.vst3 --show-editor host-window
```

Las capturas quedan en `build/ui-shots` (o en la carpeta de la variable `NOVAPITCH_UI_SHOTS`) y en `host-window/`.
