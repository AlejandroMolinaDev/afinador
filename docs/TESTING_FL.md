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

## Notas / incidencias encontradas
- 
