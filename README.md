# esp32-cam-display

Proyecto con una ESP32-CAM: captura video y lo transmite por WiFi, con el
objetivo de mostrarlo en una pantalla LCD Nokia 5110 (monocromo, 84x48,
controlador PCD8544).

## Hardware

- ESP32-CAM (AI-Thinker)
- ESP32-CAM-MB (placa programadora USB-serial)
- LCD Nokia 5110 (PCD8544)

## Estado actual

- [x] Streaming de video en vivo por WiFi (MJPEG), visible desde el navegador
- [ ] Conexion de la LCD Nokia 5110
- [ ] Conversion de frames a escala de grises / dithering para la LCD
- [ ] Mostrar la imagen de la camara en la LCD

## Wiring planeado (LCD Nokia 5110)

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

## Setup

1. Instalar [PlatformIO](https://platformio.org/).
2. Copiar `include/secrets.h.example` a `include/secrets.h` y poner tu SSID
   y password de WiFi (red de 2.4GHz).
3. Conectar la ESP32-CAM por USB (via la ESP32-CAM-MB) y ajustar el puerto
   en `platformio.ini` si no es `COM4`.
4. Compilar y subir:

   ```
   pio run -e esp32cam -t upload
   ```

5. Abrir el monitor serial para ver la IP asignada:

   ```
   pio device monitor -p COM4 -b 115200
   ```

6. Abrir esa IP en el navegador (misma red WiFi) para ver el video en vivo.
