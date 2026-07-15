# esp32-cam-display

Proyecto con una ESP32-CAM: captura video y lo muestra en tiempo real en una
pantalla LCD Nokia 5110 (monocromo, 84x48, controlador PCD8544), en blanco y
negro con dithering Floyd-Steinberg. Standalone: no depende de WiFi ni de
una PC, solo necesita alimentacion de 5V.

## Hardware

- ESP32-CAM (AI-Thinker)
- ESP32-CAM-MB (placa programadora USB-serial, solo se usa para subir el firmware)
- LCD Nokia 5110 (PCD8544)

## Estado actual

- [x] Conexion de la LCD Nokia 5110
- [x] Conversion de frames a escala de grises / dithering para la LCD
- [x] Mostrar la imagen de la camara en la LCD en tiempo real
- [x] Compensacion de rotacion (la LCD va montada a 90 grados respecto a la camara)
- [x] Funciona standalone, sin WiFi ni PC

## Wiring (LCD Nokia 5110)

La mayoria de los GPIOs de la ESP32-CAM estan ocupados por la camara, asi
que la LCD se conecta usando los pines que normalmente usaria la ranura
microSD (no se usa en este proyecto):

| Pin LCD 5110 | GPIO ESP32-CAM |
|---|---|
| RST | GPIO 2 |
| CE (CS) | GPIO 14 |
| DC | GPIO 15 |
| DIN (MOSI) | GPIO 13 |
| CLK (SCLK) | GPIO 12 |
| VCC | 3V3 (NO 5V) |
| GND | GND |
| BL | 3V3 |

Si la LCD queda fisicamente montada rotada 90 grados respecto a la camara
(borde largo vertical en vez de horizontal), cambia `LCD_ROTATE_CW` en
`src/main.cpp` (1 o 0) hasta que la imagen se vea derecha.

## Setup

1. Instalar [PlatformIO](https://platformio.org/).
2. Conectar la ESP32-CAM por USB (via la ESP32-CAM-MB, insertando el modulo
   en su socket) y ajustar el puerto en `platformio.ini` si no es `COM4`.
3. Compilar y subir:

   ```
   pio run -e esp32cam -t upload
   ```

4. Sacar la ESP32-CAM de la MB y alimentarla de forma independiente por el
   pin 5V (por ejemplo, con un cable USB cortado a un cargador o power
   bank). La imagen de la camara deberia aparecer en la LCD en unos segundos.

## Alimentacion independiente

- Usar el pin **5V** y cualquier **GND** de la placa (no el pin 3V3).
- Se recomienda una fuente de al menos 1A: la ESP32-CAM tiene picos de
  consumo que pueden causar reinicios con fuentes debiles.
