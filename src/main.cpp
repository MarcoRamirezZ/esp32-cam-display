#include <Arduino.h>
#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include "img_converters.h"  // fmt2rgb888(): decodes a JPEG/other camera frame into RGB888
#include "esp_heap_caps.h"   // heap_caps_malloc(): lets us request PSRAM specifically

// Nokia 5110 (PCD8544) wiring, using software (bit-banged) SPI.
// Hardware SPI pins are already used by the camera, and almost every other
// GPIO on the ESP32-CAM is taken too. These are the pins normally reserved
// for the microSD slot, which this project doesn't use.
#define LCD_CLK  12
#define LCD_DIN  13
#define LCD_DC   15
#define LCD_CS   14
#define LCD_RST   2

// Adafruit_PCD8544(clk, din, dc, cs, rst) — this constructor overload picks
// software SPI (plain digitalWrite on each pin) instead of the hardware SPI
// peripheral, since the hardware SPI pins aren't available here.
Adafruit_PCD8544 lcd(LCD_CLK, LCD_DIN, LCD_DC, LCD_CS, LCD_RST);

// Nokia 5110 panel resolution: 84x48, 1 bit per pixel (on/off, no grayscale).
#define LCD_W 84
#define LCD_H 48

// The LCD module is physically mounted rotated 90 degrees relative to the
// camera on this build (its long edge runs vertical instead of horizontal),
// so the image needs to be rotated back in software to appear upright.
// Set to 0 if it ends up rotated the wrong way after testing.
#define LCD_ROTATE_CW 1

static int camFrameW = 0;      // actual camera frame width, set in initCamera()
static int camFrameH = 0;      // actual camera frame height, set in initCamera()
static uint8_t *rgbBuf = nullptr;       // scratch buffer for one decoded RGB888 frame
static float grayBuf[LCD_H][LCD_W];     // 84x48 grayscale working buffer, reused by the dithering step

// ESP32-CAM (AI-Thinker) camera module pinout. Fixed by the board's PCB
// layout, not something we get to choose.
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Configures and starts the OV2640 camera sensor, and allocates the scratch
// RGB buffer used later to convert frames for the LCD.
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Deliberately using a low resolution: every frame gets JPEG-decoded to
  // RGB888 and manually downscaled to the LCD's 84x48, so a VGA frame would
  // only add decoding work without adding any usable detail on a screen
  // this small.
  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 10;            // lower number = higher quality/larger file
    config.fb_count = 2;                 // double-buffer: capture next frame while sending the current one
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QQVGA;  // 160x120
    config.jpeg_quality = 12;
    config.fb_count = 1;                  // no PSRAM: only room for a single frame buffer
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error al iniciar la camara: 0x%x\n", err);
    while (true) {
      delay(1000);
    }
  }

  camFrameW = (config.frame_size == FRAMESIZE_QVGA) ? 320 : 160;
  camFrameH = (config.frame_size == FRAMESIZE_QVGA) ? 240 : 120;

  // heap_caps_malloc() with MALLOC_CAP_SPIRAM forces the allocation into
  // external PSRAM instead of the ESP32's small internal RAM — needed
  // because an RGB888 buffer (3 bytes/pixel) is too big for internal RAM
  // at QVGA. Without PSRAM we fall back to MALLOC_CAP_8BIT (regular
  // byte-addressable internal RAM), sized for the smaller QQVGA frame.
  rgbBuf = (uint8_t *)heap_caps_malloc((size_t)camFrameW * camFrameH * 3,
                                        psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_8BIT);
}

// Initializes the LCD controller and leaves the screen blank.
void initLcd() {
  lcd.begin();
  lcd.setContrast(60);   // tune this per-panel if the display looks too light/dark
  lcd.clearDisplay();    // clears the library's internal 84x48 framebuffer
  lcd.display();         // pushes that (blank) framebuffer to the physical screen
}

// Grabs one camera frame and renders it on the Nokia 5110:
//   1. JPEG-decode the frame to RGB888.
//   2. Downscale it to 84x48 while converting to grayscale (nearest-pixel
//      sampling per target pixel, using the standard luminance weights),
//      also compensating for the panel's physical rotation.
//   3. Floyd-Steinberg dither the grayscale image down to 1 bit per pixel,
//      since the LCD can only show fully on/off pixels.
void updateLcdFromCamera() {
  if (!rgbBuf) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    return;
  }

  // fmt2rgb888() decodes the camera's JPEG frame into a flat RGB888 buffer
  // (3 bytes per pixel: R, G, B), matching the camera's own resolution.
  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgbBuf);
  esp_camera_fb_return(fb);
  if (!ok) {
    return;
  }

  // Downscale + grayscale in one pass. For each of the 84x48 output
  // pixels, pick the nearest matching source pixel (srcX, srcY) and convert
  // it to grayscale using the standard luminance formula, which weights
  // green highest because the human eye is most sensitive to it.
  //
  // The srcX/srcY formulas below also bake in the 90-degree rotation
  // compensation: instead of mapping panel x -> camera x and panel y ->
  // camera y (which would produce a sideways image on this physically
  // rotated panel), panel x maps to the camera's vertical axis and panel y
  // maps to the camera's horizontal axis, with one of them reversed
  // depending on rotation direction.
  for (int y = 0; y < LCD_H; y++) {
    for (int x = 0; x < LCD_W; x++) {
#if LCD_ROTATE_CW
      int srcX = y * camFrameW / LCD_H;
      int srcY = (LCD_W - 1 - x) * camFrameH / LCD_W;
#else
      int srcX = (LCD_H - 1 - y) * camFrameW / LCD_H;
      int srcY = x * camFrameH / LCD_W;
#endif
      int idx = (srcY * camFrameW + srcX) * 3;
      uint8_t r = rgbBuf[idx];
      uint8_t g = rgbBuf[idx + 1];
      uint8_t b = rgbBuf[idx + 2];
      grayBuf[y][x] = 0.299f * r + 0.587f * g + 0.114f * b;
    }
  }

  lcd.clearDisplay();

  // Floyd-Steinberg dithering: visit pixels left-to-right, top-to-bottom.
  // Each pixel is rounded to pure black or white, and the rounding error
  // (how far off that guess was) is pushed onto the neighboring pixels that
  // haven't been processed yet, weighted by these classic fractions:
  //
  //        *   7/16
  //   3/16 5/16 1/16
  //
  // This spreads the "mistake" around instead of concentrating it, which is
  // what makes dithered 1-bit images still read as having shades of gray.
  for (int y = 0; y < LCD_H; y++) {
    for (int x = 0; x < LCD_W; x++) {
      float oldPixel = grayBuf[y][x];
      float newPixel = (oldPixel < 128.0f) ? 0.0f : 255.0f;
      float error = oldPixel - newPixel;

      if (newPixel == 0.0f) {
        lcd.drawPixel(x, y, BLACK);  // draws into the library's internal framebuffer only
      }

      if (x + 1 < LCD_W) {
        grayBuf[y][x + 1] += error * 7.0f / 16.0f;
      }
      if (x - 1 >= 0 && y + 1 < LCD_H) {
        grayBuf[y + 1][x - 1] += error * 3.0f / 16.0f;
      }
      if (y + 1 < LCD_H) {
        grayBuf[y + 1][x] += error * 5.0f / 16.0f;
      }
      if (x + 1 < LCD_W && y + 1 < LCD_H) {
        grayBuf[y + 1][x + 1] += error * 1.0f / 16.0f;
      }
    }
  }

  // lcd.display() sends the whole internal framebuffer to the panel over
  // SPI. Nothing drawn above is actually visible until this call.
  lcd.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  initLcd();
  initCamera();

  Serial.println("Listo: camara -> LCD, standalone.");
}

void loop() {
  updateLcdFromCamera();
}
