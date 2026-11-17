# MiniScopeS3

**Mini osciloscopio digital basado en ESP32-S3 y una pantalla OLED.**

MiniScopeS3 adquiere una señal analógica con el ADC del ESP32-S3, representa la forma de onda en una pantalla OLED SSD1306 de 128 × 64 y muestra su voltaje pico a pico, frecuencia y escala temporal.

[![Plataforma](https://img.shields.io/badge/Plataforma-ESP32--S3-blue)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF-red)](https://docs.espressif.com/projects/esp-idf/)
[![Lenguaje](https://img.shields.io/badge/Lenguaje-C-blue)](main/hello_world_main.c)

## Funciones

- Visualización gráfica de la señal en una OLED de 128 × 64.
- Entrada analógica mediante ADC1.
- Medición calibrada de voltaje.
- Cálculo de voltaje pico a pico.
- Detección de frecuencia por flancos ascendentes.
- Dos modos de operación:
  - **Rollo:** muestreo continuo para señales lentas.
  - **Disparo:** captura, detección de flancos y medición para señales rápidas.
- Cambio de escala temporal con el botón BOOT.
- Escalas disponibles desde `100 µs` hasta `100 ms` por muestra.
- Malla y cursor visual de la forma de onda en la pantalla.

## Hardware

| Componente | Configuración |
|---|---|
| Microcontrolador | ESP32-S3 |
| Pantalla | OLED monocromática SSD1306, 128 × 64 |
| Protocolo de pantalla | I²C |
| Entrada analógica | ADC1, canal 3 (GPIO4) |
| Voltaje máximo recomendado | 3,3 V |

### Conexiones

| Señal | GPIO ESP32-S3 |
|---|---:|
| OLED SDA | 8 |
| OLED SCL | 9 |
| Entrada ADC | 4 |
| Cambio de escala | 0 (BOOT) |

> [!IMPORTANT]
> La señal de entrada debe mantenerse dentro del rango seguro del ADC: **0 a 3,3 V**. No conectes directamente tensiones superiores ni señales provenientes de la red eléctrica. Para medir señales adicionales utiliza división resistiva, acoplamiento y protección adecuados.

La dirección I²C predeterminada es `0x3C`; el firmware también contempla `0x3D` como referencia, aunque el código realiza la comunicación con `0x3C`.

## Uso

1. Conecta la pantalla OLED a los pines SDA/SCL indicados.
2. Conecta la señal a medir al GPIO4.
3. Compila y programa el proyecto en un ESP32-S3.
4. Presiona el botón **BOOT** para recorrer las escalas temporales.

El firmware selecciona automáticamente el modo de disparo para escalas menores a `30 ms` y el modo rollo a partir de `30 ms`.

## Compilación

Requisitos:

- ESP-IDF configurado con soporte para tu plataforma.
- Python y CMake incluidos en el entorno de ESP-IDF.
- Un ESP32-S3 conectado por USB.

Desde la raíz del proyecto:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Ejemplo sustituyendo `PORT` por el puerto serie correspondiente:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

En Windows puede ser, por ejemplo:

```powershell
idf.py -p COM5 flash monitor
```

Para salir del monitor serie, presiona `Ctrl+]`.

## Entorno de desarrollo

El repositorio incluye configuración para VS Code y un contenedor de desarrollo basado en la imagen oficial `espressif/idf`. Para usarlo:

1. Instala Docker y la extensión **Dev Containers** de VS Code.
2. Abre el repositorio en VS Code.
3. Selecciona **Dev Containers: Reopen in Container**.
4. Compila con:

```bash
idf.py set-target esp32s3
idf.py build
```

## Estructura

```text
.
├── CMakeLists.txt          Configuración principal del proyecto ESP-IDF
├── main/
│   ├── CMakeLists.txt      Registro del componente y sus dependencias
│   └── hello_world_main.c  Inicialización, ADC, I²C, OLED y procesamiento
├── .devcontainer/          Entorno ESP-IDF reproducible
└── .vscode/                Configuración del entorno de desarrollo
```

## Arquitectura del firmware

`app_main()` inicializa el bus I²C, la pantalla OLED, el ADC y el botón BOO​T. Después ejecuta un bucle continuo que:

1. Detecta cambios de escala con el botón.
2. Realiza muestreo continuo o captura disparada.
3. Convierte las lecturas del ADC a milivoltios.
4. Mide amplitud y frecuencia cuando corresponde.
5. Dibuja la cuadrícula y la forma de onda en el framebuffer SSD1306.
6. Envía el contenido completo de la pantalla mediante I²C.

Las escalas mayores o iguales a `30 ms` usan `roll_step()`; las menores usan `triggered_step()` con muestreo temporal activo y detección de flancos ascendentes.

## Solución de problemas

### No se reconoce el ESP32-S3

- Comprueba el cable USB y prueba con otro puerto.
- Sustituye `PORT` por el puerto serie real.
- Mantén presionado BOOT mientras conectas o reinicias si la placa lo requiere.

### Fallo al cargar el firmware

- Verifica que no haya otro programa usando el puerto serie.
- Baja la velocidad de transferencia o reinicia la placa.
- Ejecuta `idf.py set-target esp32s3` antes de construir.

### No aparece imagen en la OLED

- Confirma que SDA esté en GPIO8 y SCL en GPIO9.
- Comprueba la alimentación y masa compartida.
- Verifica que la pantalla use dirección `0x3C`.
- Prueba una velocidad I²C inferior si hay problemas de cableado.

## Créditos y licencias

Este proyecto es una aplicación basada en **ESP-IDF** y utiliza componentes proporcionados por Espressif. El repositorio no declara actualmente una licencia; se recomienda añadir una antes de redistribuir o reutilizar el código con fines comerciales.
