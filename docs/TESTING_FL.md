# Checklist de pruebas manuales en FL Studio

Marca cada punto en **Windows** y en **macOS** (en Mac, idealmente en Apple Silicon nativo **y** con FL bajo Rosetta / en un Intel).
La ventana de NovaPitch muestra los datos de diagnóstico necesarios (formato, host, sample rate, bloques, canales, latencia, MIDI).

Versión de FL Studio probada: ________  ·  Sistema: ________  ·  Fecha: ________

## Fase 1 — Esqueleto (passthrough)

### Escaneo y carga
| # | Prueba | Win VST3 | Win CLAP | Mac VST3 | Mac CLAP | Mac AU |
|---|---|---|---|---|---|---|
| 1 | *Manage plugins → Find more plugins* detecta NovaPitch sin errores | | | | | |
| 2 | Aparece como **efecto** (no como generador) | | | | | |
| 3 | Carga en un insert del Mixer; la ventana muestra "FL Studio detectado" y el formato correcto | | | | | |
| 4 | El audio pasa sin cambios (A/B con el slot desactivado: idéntico, sin clics) | | | | | |
| 5 | "Canales" muestra Estéreo → Estéreo en un insert del Mixer | | | | | |
| 6 | "Latencia reportada" = 0 y no hay desfase con otras pistas | | | | | |

### Robustez
| # | Prueba | Windows | macOS |
|---|---|---|---|
| 7 | Cambiar sample rate (44.1 → 48 → 96 kHz) en *Audio settings* con el plugin abierto: sigue funcionando y la ventana se actualiza | | |
| 8 | Cambiar tamaño de buffer del driver (ASIO / CoreAudio): "Bloques recibidos" refleja tamaños irregulares sin problemas | | |
| 9 | Render/exportar a WAV con el plugin en el Mixer: resultado idéntico a sin plugin | | |
| 10 | 8+ instancias simultáneas en distintos inserts sin problemas | | |
| 11 | Guardar proyecto, cerrar FL, reabrir: todas las instancias se recuperan | | |
| 12 | Abrir y cerrar la ventana del plugin muchas veces durante la reproducción: el audio no se interrumpe | | |
| 13 | Desactivar/activar el slot (bypass) durante la reproducción: sin clics | | |

### Interfaz / DPI
| # | Prueba | Windows | macOS |
|---|---|---|---|
| 14 | Texto nítido a escala de Windows 100 %, 150 % y 200 % (Configuración → Pantalla) | | n/a |
| 15 | Mover la ventana entre monitores con DPI distinto: se reescala correctamente | | |
| 16 | Redimensionar desde la esquina (75 %–200 %) mantiene proporción y nitidez | | |
| 17 | Retina: texto nítido | n/a | |

### MIDI hacia el efecto (preparación fase 7)
| # | Prueba | Win VST3 | Win CLAP | Mac VST3 | Mac AU |
|---|---|---|---|---|---|
| 18 | Enrutar MIDI al efecto (ver abajo) y tocar notas: "Eventos MIDI recibidos" aumenta | | | | |

**Cómo enrutar MIDI a un efecto en FL Studio:** en la ventana del wrapper del plugin (icono de engranaje → *Settings*), asigna un número de puerto en *MIDI → Input port*. Luego, en un canal *MIDI Out* del Channel Rack, selecciona el mismo puerto y escribe notas en su Piano roll.

### Apple Silicon / Rosetta (solo macOS)
| # | Prueba | Resultado |
|---|---|---|
| 19 | FL Studio nativo arm64: la ventana muestra "(arm64)" | |
| 20 | FL Studio con "Abrir con Rosetta" (o Mac Intel): la ventana muestra "(x86_64)" | |
| 21 | Binario descargado de CI: tras `xattr -dr com.apple.quarantine` carga sin aviso de Gatekeeper | |

## Fase 2 — Detección y medidor

> Lo que ya se prueba automáticamente en cada CI (sin FL): `tools/hosttest` carga el binario VST3 (y AU en Mac) y simula al host: 6 sample rates × 4 tamaños de buffer con bloques irregulares y vacíos, automatización de parámetros, MIDI, bypass, latencia medida = reportada, guardar/reabrir estado, 8 instancias, layouts mono/estéreo y abrir/cerrar el editor mientras suena. Además: pluginval 10, auval, clap-validator, benchmark de CPU y capturas de la UI (artefacto `UI-snapshots-*`).
> Lo que **solo** se puede comprobar en FL Studio es lo de esta tabla.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 22 | Micrófono → insert del Mixer con NovaPitch: el medidor muestra la nota cantada, estable (sin saltar de octava) | | |
| 23 | Notas sostenidas: los cents se mueven suavemente y "afinado" aparece cerca de 0 ¢ | | |
| 24 | Sibilantes (s, sh) y respiraciones: el medidor muestra "—" (sin señal afinable), no notas falsas | | |
| 25 | Cambiar el **Perfil de entrada** mientras se canta: sin clics, el medidor se recupera | | |
| 26 | **Sensibilidad** baja en una toma ruidosa → menos detecciones falsas; alta en voz susurrada → detecta | | |
| 27 | Gráfico inferior: la curva sigue la melodía de los últimos 5 s | | |
| 28 | Automatizar *Perfil de entrada* y *Sensibilidad* desde FL (clic derecho → *Create automation clip*) | | |
| 29 | La lista de parámetros del plugin en FL muestra solo los parámetros de NovaPitch (sin cientos de "MIDI CC") | | |
| 30 | CPU del plugin en el Mixer (*Performance monitor*) a 48 kHz / buffer 128: anotar % | | |

## Fase 3 — Corrección automática

> Automático en CI: +35 cents corregidos a < 5 cents (Live/Tracking/Vintage, 44.1–96 kHz), hard tune en escalones, tolerancia, notas ignoradas/eliminadas, referencia A4, transposición ±12, sin clicks en transiciones, latencia medida = reportada en los 4 modos, demo de audio (`Audio-demo`).

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 31 | Voz real en un insert, **Velocidad 0**: efecto hard tune claro, notas en escalones, sin glitches | | |
| 32 | **Velocidad 40–80 ms**: corrección natural, el vibrato se conserva | | |
| 33 | **Tonalidad/Escala** correctas para la canción: no salta a notas fuera de escala | | |
| 34 | **Tolerancia** 20–40 ¢: los glissandos y adornos pasan sin corregir | | |
| 35 | **Sostenido** 50–100 %: notas largas más naturales, ataques igual de precisos | | |
| 36 | **Transposición** ±12: la voz sube/baja sin efecto "ardilla" (formantes preservados) | | |
| 37 | **Mezcla** 50 %: sin flanger/phasing en modo Tracking | | |
| 38 | Cambiar **Modo** Live ↔ Tracking durante la reproducción: FL reajusta la compensación (PDC) sin desfase con otras pistas | | |
| 39 | Exportar (render) el proyecto: el resultado suena igual que en tiempo real y alineado | | |
| 40 | Consonantes (s, t, k) y respiraciones: sin artefactos ni "zumbidos" | | |
| 41 | Automatizar Velocidad/Tonalidad/Escala desde FL: los cambios se oyen sin clics | | |
| 42 | CPU (*Performance monitor*) a 48 kHz / buffer 128 en modo Live y Tracking: anotar % | | |

## Fase 4 — Formantes, HQ y Vintage

> Automático en CI: formantes preservados/desplazados/carácter (PSOLA y HQ, centroide espectral), +35 ¢ corregidos en HQ, transposición ±12 en HQ (< 5 ¢), latencia de los 4 modos en el binario (HQ = 100 ms), 0 asignaciones de memoria en el hilo de audio en los 4 modos, demo de audio con renders HQ/formantes/Vintage.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 43 | **Modo HQ** en una pista ya grabada: FL compensa los 100 ms (la voz sigue alineada con el beat) | | |
| 44 | HQ vs Tracking con Transposición ±7: HQ suena más limpio en voces con aire / ruido | | |
| 45 | Exportar el proyecto con HQ: el resultado coincide con la reproducción | | |
| 46 | **Preservar formantes** on/off con Transposición +7: on = voz natural, off = voz de "ardilla" | | |
| 47 | **Desplazar formantes** ±4 st sin transposición: cambia el timbre, no la afinación | | |
| 48 | **Carácter** ±30 %: voz más "pequeña" / más "grande" | | |
| 49 | **Enlazar a transposición**: con Transposición −12, voz de "monstruo" | | |
| 50 | **Vintage**: efecto hard tune clásico, más seco que Live con velocidad 0 | | |
| 51 | CPU en HQ a 48 kHz (*Performance monitor*): anotar % (Mac Apple Silicon debería ser menor) | | |

## Fase 5 — Vibrato

> Automático en CI: analizador (velocidad/profundidad ±10 % con audio real a través del detector), glissandos/cambios de nota no confundidos con vibrato, Conservar 0/100/200 % en PSOLA y HQ, Vintage aplana, vibrato artificial con retardo, 0 asignaciones en tiempo real, renders de demo 12–14.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 52 | Voz con vibrato, Velocidad 0, **Conservar 100 %**: notas afinadas y vibrato natural intacto | | |
| 53 | **Conservar 0 %**: vibrato eliminado (efecto "plano") · **200 %**: vibrato exagerado | | |
| 54 | Pestaña Vibrato: el visualizador muestra Hz y ± cents del vibrato cantado | | |
| 55 | **Vibrato artificial** (Profundidad 30 ¢, Retardo 300 ms) en notas largas de una voz plana | | |
| 56 | Variación, Amplitud y Formante del vibrato artificial: cambios audibles y sin clics | | |
| 57 | Cambiar de pestaña con la reproducción en marcha: sin cortes de audio | | |

## Fase 6 — Presets, MIDI Learn, apariencia

> Automático en CI: presets de fábrica (no tocan tonalidad/escala/notas/A4), presets de usuario guardar/listar/cargar/borrar (incluye nombres con "ñ"), nombre del preset guardado en el proyecto, MIDI Learn (CC aprendido, valor aplicado, mapeo restaurado al reabrir), capturas de las 3 pestañas.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 58 | Recorrer los presets de fábrica con `<` `>`: el sonido cambia, la tonalidad/escala no | | |
| 59 | **Guardar** un preset de usuario, cerrar FL, reabrir: aparece en la lista y carga igual | | |
| 60 | `···` → *Abrir carpeta de presets* abre `Documentos/NovaPitch/Presets` | | |
| 61 | **CLAP**: clic derecho en un knob → *Asignar a controlador MIDI* → mover un knob del controlador: queda "· CC n" y lo controla | | |
| 62 | **VST3**: el menú indica usar *Link to controller* de FL; verificar que *Link to controller* funciona con los parámetros de NovaPitch | | |
| 63 | Guardar el proyecto con mapeos MIDI y reabrir: mapeos intactos | | |
| 64 | `···` → *Color de acento*: cambia el color y se mantiene al reabrir el proyecto | | |
| 65 | Redimensionar 75 %–200 % en pantallas HiDPI (Windows 150 %/200 %, Retina) | | |

## Fase 7 — Armonías y MIDI

> Automático en CI: pasos diatónicos, voces diatónica/semitonos/MIDI a la afinación correcta (< 5 ¢), gate/pan/mute, MIDI Target (nota sostenida en otra octava), aprender escala (parámetros escritos), armonías en Live/Tracking/HQ sin NaN, 0 asignaciones con 4 voces y acordes, ida y vuelta texto↔valor de los 80 parámetros.

**Enrutar MIDI a NovaPitch en FL Studio:** ventana del plugin → ⚙ (Settings) → *MIDI* → *Input port* = 1. Crea un canal *MIDI Out* en el Channel Rack con *Port* = 1 y escribe notas/acordes en su Piano roll.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 66 | Preset **Armonías 3as**: dos voces (3ª arriba y abajo) que siguen la escala de la canción | | |
| 67 | Voz en modo **MIDI** + acordes en el Piano roll: las voces cantan las notas del acorde | | |
| 68 | **MIDI: nota objetivo** con una melodía en el Piano roll: la voz sigue exactamente esas notas | | |
| 69 | **Aprender escala**: tocar las notas de la escala → Escala = Personalizada con esas notas; desactivar y comprobar que se conserva al reabrir | | |
| 70 | Pestaña MIDI: muestra las notas que llegan desde FL | | |
| 71 | Humanizar, panorama, mute/solo y bus (EQ, gate, ancho) suenan como se espera | | |
| 72 | CPU con 4 voces a 48 kHz (*Performance monitor*): anotar % | | |

## Fase 8 — Modo gráfico (sin ARA)

> Automático en CI: pista compilada (notas, líneas, curvas, huecos), edición con deshacer/rehacer, capturas que se sustituyen por tramos, importación desde automático (vibrato no parte notas), liberación segura de pistas, prueba de extremo a extremo con un transporte simulado (capturar → importar → editar → reproducir → guardar → reabrir) y, sobre el binario VST3, una nota guardada en el proyecto que se aplica al reproducir, en bucle, con el transporte parado y tras reabrir. 0 asignaciones en tiempo real con el modo gráfico.

**Flujo en FL Studio:** botón **Gráfico** (cabecera) → **● Capturar** → reproduce la canción (o el patrón) → la curva aparece en rojo y queda en gris al parar → **Importar desde automático** → edita → reproduce: manda lo dibujado; fuera de los objetos y con el transporte parado pasa la voz original.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 73 | **Auto / Gráfico** cambia la vista; el parámetro *Modo gráfico* es automatizable y la vista lo sigue | | |
| 74 | **Capturar** + reproducir: la curva se dibuja durante la reproducción y la vista sigue al cursor (*Seguir*) | | |
| 75 | **Importar desde automático**: notas en la tonalidad/escala actual; *Deshacer* / *Rehacer* | | |
| 76 | Editar: mover una nota un semitono (se oye), cambiar su duración por los bordes, *Dividir*, *Unir*, borrar con clic derecho | | |
| 77 | Herramientas **Línea** y **Curva**: el tono de salida sigue lo dibujado | | |
| 78 | Inspector: velocidad, vibrato y formante por nota (punto amarillo en la nota) | | |
| 79 | **Bucle** de patrón/canción: la corrección queda alineada en cada vuelta | | |
| 80 | **Alineación**: capturar en Tracking y reproducir en HQ (y al revés). ¿Se desplazan los objetos? Anotar el desfase si lo hay | | |
| 81 | Volver a capturar solo un tramo (punch): se sustituye solo ese tramo | | |
| 82 | Guardar el proyecto, cerrar FL, reabrir: objetos y curva intactos. Anotar el tamaño del .flp | | |
| 83 | Atajos con la ventana del plugin enfocada: Supr, Ctrl/Cmd+Z, Ctrl/Cmd+Mayús+Z, Ctrl/Cmd+A, 1–6. ¿FL intercepta alguno? | | |
| 84 | Rueda: tono · Mayús+rueda: tiempo · Ctrl/Cmd+rueda: zoom · pinza en trackpad (Mac) | | |
| 85 | Varias pistas de voz con NovaPitch en modo gráfico a la vez | | |

## Fase 9 — Tonalidad, sincronización y corrección temporal

> Automático en CI: detección de tonalidad en melodías mayores/menores y en una frase cantada a través del plugin, sincronización entre 3 instancias, mapa de tiempo, render (ataque movido a ± 0,4 ms, audio sin editar idéntico, cambio a la voz en vivo en la pausa, tono intacto), captura de audio a WAV por pasadas, extremo a extremo (capturar → mover una sílaba 100 ms → oírla movida y afinada → reabrir) y 0 asignaciones en tiempo real.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 86 | **Escuchar** (junto a Tonalidad) 10 s mientras suena la voz: Tonalidad y Escala cambian solas; el pie muestra la tonalidad detectada | | |
| 87 | Escuchar → *Desde la captura del modo gráfico* da la misma tonalidad | | |
| 88 | Dos pistas con NovaPitch: cambiar la tonalidad en una y pulsar **Sincronizar** → la otra cambia | | |
| 89 | Con el plugin en modo *bridged* (proceso aparte), Sincronizar aparece desactivado con su explicación | | |
| 90 | Modo gráfico: Capturar + reproducir; `···` → *Abrir carpeta de capturas* muestra los WAV | | |
| 91 | Herramienta **Tiempo**: arrastrar una nota (mover la sílaba) y un borde (estirarla); al reproducir se oye movida, sin clics en los bordes | | |
| 92 | Alt+clic en la nota editada deshace su edición; *Limpiar → Quitar ediciones de tiempo*; *Deshacer* | | |
| 93 | Guardar, cerrar FL y reabrir el proyecto: las ediciones de tiempo siguen sonando | | |
| 94 | Mover el proyecto a otro equipo (o borrar la carpeta de capturas): aviso «Falta el audio capturado», el resto del modo gráfico funciona | | |
| 95 | CPU con ediciones de tiempo sonando (*Performance monitor*) | | |

## Fase 10 — Instaladores y guía

> Automático en CI: el instalador de Windows se instala en silencio, se comprueban las rutas, el host test pasa sobre el VST3 instalado y la desinstalación lo borra todo; el `.pkg` de macOS se instala con `installer`, se verifican las firmas, `auval` y el host test pasan sobre lo instalado en `/Library`.

| # | Prueba | Windows | macOS |
|---|---|---|---|
| 96 | Instalar en una máquina **sin herramientas de desarrollo** (sin Visual Studio / Xcode) | | |
| 97 | FL Studio encuentra NovaPitch tras *Find installed plugins* (VST3, CLAP y AU en Mac) | | |
| 98 | Abrir un proyecto guardado con una versión anterior: el plugin y sus ajustes se cargan igual | | |
| 99 | Actualizar (instalar encima) con FL abierto: el instalador pide cerrarlo; después todo funciona | | |
| 100 | La guía se abre desde el menú Inicio (Windows) / `/Library/Application Support/NovaPitch` (Mac) | | |
| 101 | Desinstalar: desaparecen los plugins; presets y capturas del usuario se conservan | | |
| 102 | macOS sin firma: el aviso de Gatekeeper se salta con clic derecho → Abrir | | |

## Notas / incidencias encontradas
- 
