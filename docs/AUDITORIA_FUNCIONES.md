# Auditoría de funciones — NovaPitch frente a un afinador vocal profesional

Comparación de NovaPitch con el conjunto de funciones habitual de los afinadores vocales profesionales en tiempo real (corrección automática, modo gráfico, armonías, MIDI). Se describen las **funciones**, no productos concretos: NovaPitch no usa nombres ni marcas de terceros.

Estado: ✅ ya estaba · 🔧 corregido o añadido en esta auditoría · ⏳ pendiente (ver `docs/ROADMAP.md`).

## Corrección automática

| Función | Estado | En NovaPitch |
|---|---|---|
| Tonalidad + escalas (mayor, menores, modos, pentatónicas, blues, cromática, personalizada) | ✅ | Tonalidad / Escala (14 escalas) |
| **Teclado de notas**: activar, ignorar o eliminar cada nota | 🔧 | Nueva pestaña **Notas** (antes solo como parámetros del host). Muestra la nota detectada y la nota destino en vivo |
| Velocidad de corrección (de transparente a efecto robótico) | ✅ | Velocidad 0–400 ms |
| **Cambio de nota inmediato con velocidad 0** | 🔧 | La salida seguía en la nota anterior ~20 ms tras cada cambio. Ahora el PSOLA sigue el periodo real de la onda en las transiciones: llega a la nota nueva en 0–6 ms según el modo (antes 17–24 ms) |
| Tolerancia (dejar pasar adornos cerca de la nota) | ✅ | Tolerancia 0–100 ¢ |
| Conservar micro-variaciones en notas largas | ✅ | Sostenido natural |
| Conservar / quitar / exagerar el vibrato natural | ✅ | Conservar vibrato 0–200 % |
| Vibrato artificial (velocidad, profundidad, retardo, variación, amplitud, formante) | ✅ | Pestaña Vibrato |
| **Forma del vibrato artificial** | 🔧 | Nueva: Seno, Triángulo, Cuadrada, Sierra |
| Tipos de entrada (soprano, tenor, grave, instrumento, bajo) | ✅ | Perfil de entrada |
| Sensibilidad de detección | ✅ | Sensibilidad |
| Referencia de afinación (detune) | ✅ | Referencia A4 400–480 Hz |
| **Desafinación por nota (escalas microtonales, entonación justa, cuartos de tono)** | 🔧 | ±50 ¢ por nota en la pestaña Notas; decide y corrige hacia la nota desafinada; llega a las armonías, al objetivo MIDI, a la importación del modo gráfico y a Sincronizar |
| Transposición | ✅ | ±12 st |
| Formantes: preservar, desplazar, longitud del tracto | ✅ | Pestaña Formantes (Carácter = longitud del tracto) |
| Modo clásico de corrección dura | ✅ | Modo Vintage |
| Modos de baja latencia / alta calidad | ✅ | Live 3 ms, Tracking 10 ms, HQ 100 ms |
| Mezcla dry/wet con latencia compensada | ✅ | Mezcla |

## Tonalidad y varias pistas

| Función | Estado | En NovaPitch |
|---|---|---|
| Detección automática de tonalidad | ✅ | Escuchar (10/20/30 s) o desde la captura |
| Compartir tonalidad entre instancias | ✅ | Sincronizar |

## MIDI

| Función | Estado | En NovaPitch |
|---|---|---|
| Nota destino desde MIDI | ✅ | MIDI: nota objetivo |
| **Nota destino en la octava tocada** | 🔧 | Nueva opción «Octava exacta» (antes siempre la octava más cercana a la voz) |
| Aprender la escala tocando | ✅ | Aprender escala por MIDI |
| **Enviar las notas corregidas como MIDI** | 🔧 | Nuevo «Enviar notas MIDI» (para doblar la voz con un sintetizador) |
| MIDI Learn de controles | ✅ | Clic derecho en un knob (CLAP/AU); en VST3 de FL, *Link to controller* |

## Modo gráfico

| Función | Estado | En NovaPitch |
|---|---|---|
| Captura de la voz y su afinación | ✅ | Capturar (por reproducción; FL no admite ARA) |
| Curva original + curva corregida | ✅ | Gris + color de acento |
| Notas, líneas, curvas a mano | ✅ | Herramientas Nota, Línea, Curva |
| Importar notas desde el modo automático | ✅ | Importar desde automático |
| **Nivel de detalle al importar** | 🔧 | Menos / normal / más detalle |
| **Importar como curvas editables** | 🔧 | Nuevo: curvas que siguen la afinación capturada |
| **Escalar el vibrato de una curva** | 🔧 | El inspector «Vibrato» también actúa sobre curvas |
| Velocidad, vibrato y formante por nota | ✅ | Inspector |
| Mover, estirar, dividir, unir, borrar, deshacer ilimitado | ✅ | Herramientas + Deshacer/Rehacer |
| **Mover con el teclado** | 🔧 | Flechas: ±1 semitono / ±10 ms (Mayús: 10 ¢ / 100 ms) |
| Corrección de tiempo no destructiva | ✅ | Herramienta Tiempo |

## Armonías

| Función | Estado | En NovaPitch |
|---|---|---|
| Voces por intervalo de escala, por semitonos o por acordes MIDI | ✅ | 4 voces |
| Formante, panorama, nivel, humanizar, mute/solo por voz | ✅ | |
| Bus con EQ, envolvente, gate y ancho | ✅ | Pestaña Bus |

## Presets

| Función | Estado | En NovaPitch |
|---|---|---|
| Presets de fábrica y de usuario | ✅ | Barra de presets |
| **Comparación A/B** de dos sonidos | 🔧 | Botones A y B en la cabecera; copiar uno en otro en `···`; se guarda con el proyecto; nunca toca la tonalidad ni las notas |
| **Indicador de preset modificado** | 🔧 | «*» junto al nombre; elegir el mismo preset descarta los cambios |

## Pendientes conocidos (no bloquean el uso)

- ⏳ Integración ARA2 para DAW que la soportan (pospuesta por decisión del usuario; FL Studio no la usa).
- ⏳ Saltos de más de ~4 semitonos: la salida sigue el cambio cuando el detector lo confirma (~20 ms).

## Cómo se verificó

Cada punto 🔧 tiene su test automático: `tests/unit/TransitionTests.cpp` (cambio de nota en Live, Tracking y Vintage, con la misma y con distinta desafinación), `tests/unit/AuditTests.cpp` (salida MIDI, octava exacta, formas de vibrato, importación con detalle y como curvas, escala de vibrato de curvas), `tests/unit/DetuneAbTests.cpp` (desafinación por nota en el mapeo, el procesador, las armonías, el modo gráfico y Sincronizar; A/B y marcador de modificado) y la captura de la pestaña Notas en `NovaPitchSnapshot`. Todo el conjunto (102 casos), el host test sobre el binario, pluginval 10 y clap-validator pasan.
