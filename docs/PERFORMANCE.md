# Rendimiento y robustez: objetivos, método y resultados

Fecha de la medición: 5 de noviembre de 2026.
Entorno: contenedor Linux x86-64 sin GPU, compilación Release (`-O3`), JUCE 9.0.3, servidor X virtual (Xvfb) con Openbox.
**No es Windows.** Los números absolutos de Windows/MSVC/FL Studio están por medir; los objetivos y las pruebas sirven tal cual.

## Objetivos (se comprueban en `Tests/PluginTests.cpp`)

| Qué | Objetivo | Resultado medido |
|---|---|---|
| Carga media del hilo de audio | < 2 % del tiempo del bloque | 0,14 % – 0,27 % |
| Percentil 99 del bloque | < 25 % del plazo del bloque | 8,6 µs (64 muestras) – 162 µs (4096 muestras) |
| Peor bloque observado | sin desbordar el plazo | 0,2 % – 3,2 % del bloque |
| Asignaciones de memoria en `processBlock` | 0 | **0** en 4000 bloques y en 1 hora de audio |
| Entrada con números subnormales | sin pico de CPU | p99 ≈ 60 µs (bloque de 10 667 µs) |
| Una hora de audio continuo | lecturas exactas, sin deriva | −23,0 LUFS ±0,1; procesada en 5,5 s |
| Interfaz, por fotograma (760×420) | < 20 ms (plazo de 33 ms a 30 Hz) | 0,45 – 1,5 ms (9 paneles) |
| Interfaz, por fotograma (1600×900) | < 20 ms | 1,2 – 4,8 ms (9 paneles; el más caro es Spectrogram) |
| Abrir/cerrar interfaz 60 veces | sin pérdida de vistas | ≈ 1 MB en total; Valgrind: 0 bytes perdidos |

Configuraciones de audio medidas: 44,1 kHz/64, 48 kHz/512, 48 kHz/4096, 96 kHz/512, 192 kHz/1024, y bloques de 1 muestra.

## Cómo se mide

- **CPU de audio:** se cronometra solo `processBlock` con `steady_clock`, 10 s de audio por configuración; se informa media, p99 y máximo.
- **Asignaciones:** en glibc se interponen `malloc/calloc/realloc/free` y se cuentan las llamadas del hilo que ejecuta `processBlock`. `operator new` pasa por `malloc`, así que queda cubierto. En otras plataformas la prueba se omite y lo dice.
- **Interfaz:** `refreshNow()` (lo que hace el temporizador) más una captura de software de todo el editor. En Windows, JUCE puede dibujar con Direct2D, normalmente más rápido que este renderizado por CPU.
- **Fugas:** `mallinfo2` antes y después de 60 ciclos; y Valgrind (`--leak-check=full --show-leak-kinds=all`) sobre una escena reducida (`VV_LEAK_SCENARIO=1` con el binario compilado con `-DVV_NO_ALLOCATION_HOOKS`).
- **Diagnóstico de crecimiento:** `VV_LEAK_DIAG=1` imprime los KB por ciclo de cada escenario.
- **Capturas:** `VV_SNAPSHOT_DIR=/ruta` escribe PNG de cada panel y skin.

## Hallazgos

1. **Hilo de audio:** sin hallazgos. El coste del histograma de gating (cada 100 ms) no se nota en el peor bloque.
2. **Un contador podía desbordarse** tras ~6,8 años seguidos (`stepsSeen` en `LoudnessMeter`). Corregido: ahora se satura en 30.
3. **Crecimiento de memoria al abrir/cerrar la ventana flotante** (≈ 5 KB por ciclo, algo más con PIN/DOCK). Una ventana vacía de JUCE crece ≈ 3 KB por ciclo en esta plataforma, y Valgrind no encuentra pérdidas definitivas ni errores. Los registros que se repiten en cada ciclo no incluyen funciones del plugin. Se interpreta como memoria retenida por la capa de JUCE sobre X11; **no está confirmado en Windows**.
4. Un valor aislado de ~1 ms en la prueba de subnormales (frente a p99 de 60 µs) se atribuye a planificación del sistema. Se vigila el p99, no el máximo.

5. **Spectrogram a 1600×900 costaba 9,2 ms por fotograma** (28 % de un núcleo a 30 Hz), 4–5 veces más que el resto de paneles. Causa: reescalado suave de la imagen con mezcla alfa. Corregido con imagen opaca (RGB) y reescalado rápido cuando el panel es grande: **4,8 ms**. Lo detectó la prueba de interfaz sin cambios, al añadirse el panel.
6. **Tuner:** el detector trabaja sobre una copia diezmada (~24 kHz), así que su coste no crece con la frecuencia de muestreo; se ejecuta ~10 veces por segundo.

## Pendiente

- Repetir todo en Windows 11 con MSVC y dentro de FL Studio 2026 (CPU del DAW, no solo del plugin).
- Prueba de asignaciones en Windows (Application Verifier o un contador propio).
- Medir con ventana flotante y editor abiertos a la vez, y con el modo FULL cuando haya cuadrícula multipanel.
- Medir la latencia de interfaz «lo que oyes es lo que ves». Hoy el plugin declara latencia 0 y el audio pasa sin tocarse.
- Cuando existan Spectrogram y los visualizadores generativos, añadirlos a `testUiPerformance` (se recorren todos los paneles automáticamente).

## Añadido: Spectrogram y Tuner (5 de noviembre de 2026, mismo entorno)

| Panel | 760×420 | 1600×900 |
|---|---|---|
| Spectrogram | 1,8 ms por fotograma | 5,5 ms por fotograma (el panel más caro) |
| Tuner | 0,9 ms | 1,9 ms |

Ambos quedan dentro del objetivo de 20 ms. Spectrogram escala la imagen con calidad reducida a partir de cierto tamaño (decisión ya tomada en el código por su coste).

## Actualización: 15 paneles, cuadrícula y rejilla FULL (mismo entorno)

Interfaz por fotograma a 1600×900 (actualizar + pintar), un panel a la vez:

| Panel | ms | Panel | ms | Panel | ms |
|---|---|---|---|---|---|
| LUFS | 1,65 | Tuner | 1,67 | Pulsar | 1,68 |
| Spectrum | 1,92 | Constellation | 2,22 | Orb | 2,92 |
| Waveform | 2,36 | Mandala | 2,92 | Attractor | 1,83 |
| Levels | 1,84 | Signal Map | 1,77 | Scope | 2,67 |
| VU | 2,03 | Goniometer | 1,92 | Spectrogram | 4,87 |

**Rejilla FULL con 5 módulos a la vez a 1600×900: 3,8 ms por fotograma** (11,5 % de un núcleo a 30 Hz).
Límite de las pruebas: 20 ms por panel y 25 ms para la rejilla completa.
Audio: sin cambios (0,15 % del bloque a 48 kHz/512; cero asignaciones; una hora de audio en 5,6 s).
Memoria al abrir y cerrar: ≈ 1,3 MB tras 60 ciclos (7 editores + ventana flotante cada uno); límite 2 MB.
