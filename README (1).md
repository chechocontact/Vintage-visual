# Vintage Visual

Medidor de loudness y visualizadores generativos modulares, en formato **VST3**, para colocar en el **master** de tu DAW.
El audio pasa por el plugin **sin modificarse**: solo lo mide y lo dibuja.

## Qué incluye

**Medición de loudness (ITU-R BS.1770-4 / EBU R128)**
- LUFS *Momentary*, *Short-term* e *Integrated* con *gating*, rango de loudness (LRA) y *True Peak* con sobremuestreo (dBTP).
- El filtro K-weighting se recalcula para la frecuencia de muestreo real del proyecto.
- Objetivos configurables: −14 (streaming), −16 (Apple), −23 (EBU), −9 (club).

**15 visualizadores, combinables**
Spectrum · Spectrogram (dos resoluciones de análisis) · Waveform · VU · LUFS · Levels · Goniometer · Scope · Tuner · Constellation · Mandala · Signal Map · Pulsar · Orb · Attractor.

**Interfaz**
- Ventana del DAW redimensionable, o **ventana flotante sin bordes** (botón *Float*).
- **PIN**: siempre encima de todo.
- **DOCK**: franja en la parte superior de la pantalla.
- **FULL**: pantalla completa con una cuadrícula de módulos (espectrograma, loudness, forma de onda, estéreo y niveles).
- Paneles múltiples: 1, 2, 3 o 4 a elección; cada hueco elige su visualizador y se pueden **arrastrar unos sobre otros para intercambiarlos**.
- 9 skins, con un menú que muestra cada una en sus colores: Vintage (por defecto), Night, Day, Nebula, Phosphor, Blueprint, Synthwave, Pink y Celeste.

## Uso

Inserta **Vintage Visual** como efecto en el canal **master**. No añade latencia ni cambia el sonido.

## Instalar (Windows 11 x64)

El instalador `VintageVisual-<versión>-Setup.exe` lo genera GitHub Actions (artefacto `VintageVisual-Setup-windows`).
Instala en `C:\Program Files\Common Files\VST3\Vintage Visual.vst3`. Después reabre tu DAW y busca plugins nuevos
(FL Studio: *Options > Manage plugins > Find installed plugins*).

> El instalador **no está firmado**: Windows SmartScreen puede mostrar un aviso la primera vez.

## Compilar

Requisitos: CMake ≥ 3.25, compilador C++20 (Visual Studio 2022 en Windows). JUCE 9.0.3 se descarga solo.
Compila desde una ruta corta (por ejemplo `C:\dev\VintageVisual`) para evitar el límite de 260 caracteres de Windows.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

El plugin queda en `build/VintageVisual_artefacts/Release/VST3/Vintage Visual.vst3`.
Para el instalador: Inno Setup 6 y `installer/VintageVisual.iss` (el CI lo hace en cada push).

## Pruebas

| Suite | Qué comprueba |
|---|---|
| `CoreTests` | Medición (valores de referencia EBU/ITU), FFT, niveles, afinador, geometría de cuadrículas y contraste WCAG de cada skin. Sin JUCE. |
| `PluginTests` | Procesador, estado guardado, editor, ventana flotante (PIN/DOCK/FULL), paneles, intercambio de paneles, rendimiento, ausencia de asignaciones de memoria en el hilo de audio y fugas. |

Variables de entorno de `PluginTests` (solo Linux/X11 con gestor de ventanas para las que abren ventanas):
`VV_SNAPSHOT_DIR=/ruta` escribe capturas PNG; `VV_LEAK_DIAG=1` y `VV_LEAK_SCENARIO=1` ayudan a localizar fugas.
Resultados de rendimiento y método: [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).

## Estado de compatibilidad

| Elemento | Estado |
|---|---|
| Compilación y pruebas en Linux (GCC, JUCE 9.0.3) | **Verificado** (todas las pruebas pasan) |
| Compilación con MSVC / Visual Studio 2022 | **Sin verificar** (lo hace el CI de GitHub) |
| Instalador de Inno Setup | **Sin verificar** (escrito, no ejecutado) |
| FL Studio 2026 | **Sin probar** |
| Otros DAW (REAPER, Cubase, Studio One, Bitwig) | Sin probar |
| Windows 10 | No es objetivo (el instalador exige Windows 11) |
| macOS, Linux como formato de distribución, AU, AAX, CLAP | No incluidos |

## Limitaciones conocidas

- **DOCK** es una franja siempre encima; no reserva espacio en el escritorio (las ventanas maximizadas pasan por debajo).
- El plugin mide el audio que recibe en el canal donde se inserta; no captura el audio del sistema.
- Solo estéreo.
- Interfaz solo en inglés.

## Licencia

**GNU AGPL-3.0-or-later** (ver `LICENSE`). El plugin enlaza JUCE, que se usa aquí bajo AGPLv3; distribuir el instalador
obliga a ofrecer el código fuente. Para distribuir con código cerrado haría falta una licencia comercial de JUCE y
revisar el resto de dependencias. *Esta nota no es asesoría legal.*

VST es una marca registrada de Steinberg Media Technologies GmbH.
