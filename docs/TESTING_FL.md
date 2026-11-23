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

## Notas / incidencias encontradas
- 
