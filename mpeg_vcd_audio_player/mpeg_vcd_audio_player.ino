// #pragma GCC optimize("O3")
#pragma GCC optimize("Ofast")

const char *root = "/root";
// const char *mpeg_file = "/root/224x128.mpg";
// const char *mpeg_file = "/root/320x240.mpg";
const char *mpeg_file = "/root/AVSEQ02.DAT";
// const char *mpeg_file = "/root/VCD.DAT";

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <WiFi.h>

#include <FFat.h>
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "TDECK_PINS.h"

/*******************************************************************************
   Start of Arduino_GFX setting
 ******************************************************************************/
#include <Arduino_GFX_Library.h>
#define GFX_DEV_DEVICE LILYGO_T_DECK
#define GFX_EXTRA_PRE_INIT()                \
  {                                         \
    pinMode(TDECK_SDCARD_CS, OUTPUT);       \
    digitalWrite(TDECK_SDCARD_CS, HIGH);    \
    pinMode(TDECK_RADIO_CS, OUTPUT);        \
    digitalWrite(TDECK_RADIO_CS, HIGH);     \
    pinMode(TDECK_PERI_POWERON, OUTPUT);    \
    digitalWrite(TDECK_PERI_POWERON, HIGH); \
    delay(500);                             \
  }
#define GFX_BL TDECK_TFT_BACKLIGHT
Arduino_DataBus *bus = new Arduino_ESP32SPI(TDECK_TFT_DC, TDECK_TFT_CS, TDECK_SPI_SCK, TDECK_SPI_MOSI, TDECK_SPI_MISO);
Arduino_TFT *gfx = new Arduino_ST7789(bus, GFX_NOT_DEFINED /* RST */, 1 /* rotation */, true /* IPS */);
/*******************************************************************************
   End of Arduino_GFX setting
 ******************************************************************************/

// microSD card
#define SD_SCK TDECK_SPI_SCK
#define SD_MISO TDECK_SPI_MISO
#define SD_MOSI TDECK_SPI_MOSI
#define SD_CS TDECK_SDCARD_CS

unsigned long decode_start_ms;

#include "esp32_audio.h"
// I2S
#define I2S_DOUT TDECK_I2S_DOUT
#define I2S_BCLK TDECK_I2S_BCK
#define I2S_LRCK TDECK_I2S_WS

#include "mpeg.h"

void setup(void)
{
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while (!Serial);
  Serial.println("MPEG VCD Audio Player");

  // If display and SD shared same interface, init SPI first
  SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  // Init Display
  if (!gfx->begin(80000000))
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  // if (!FFat.begin(false, root))
  // if (!LittleFS.begin(false, root))
  if (!SD.begin(SD_CS, SPI, 80000000, "/root"))
  // pinMode(SD_CS, OUTPUT);
  // digitalWrite(SD_CS, HIGH);
  // SD_MMC.setPins(SD_SCK, SD_MOSI, SD_MISO);
  // if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
  {
    Serial.println("ERROR: File system mount failed!");
  }
  else
  {
    i2s_init(
        I2S_NUM_0,
        44100 /* sample_rate */,
        -1 /* mck_io_num */, /*!< MCK in out pin. Note that ESP32 supports setting MCK on GPIO0/GPIO1/GPIO3 only*/
        I2S_BCLK,            /*!< BCK in out pin*/
        I2S_LRCK,            /*!< WS in out pin*/
        I2S_DOUT,            /*!< DATA out pin*/
        -1 /* data_in_num */ /*!< DATA in pin*/
    );
    i2s_zero_dma_buffer(I2S_NUM_0);

    FILE *f = fopen(mpeg_file, "r");
    if (!f)
    {
      printf("Couldn't open file %s\n", mpeg_file);
    }
    else
    {

      mp2_player_task_start();

      mpeg_init(f);

      decode_start_ms = millis();
      mpeg_packet_scan(f);
      fclose(f);

      Serial.printf("duration: %ld, total_decode_audio_ms: %u, total_play_audio_ms: %u\n", millis() - decode_start_ms, total_decode_audio_ms, total_play_audio_ms);
    }
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("done");
}

void loop()
{
}
