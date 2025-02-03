// #pragma GCC optimize("O3")
#pragma GCC optimize("Ofast")
// #pragma GCC optimize("O2")
// #pragma GCC optimize("O1")

const char *root = "/root";
const char *mpeg_file = "/root/AVSEQ02.DAT";
// const char *mpeg_file = "/root/VCD.DAT";

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
// #include "PINS_T-DECK.h"
#include "PINS_JC1060P470.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <FFat.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SD_MMC.h>

unsigned long decode_start_ms;
unsigned long total_decode_audio_ms;
unsigned long total_play_audio_ms;

#include "esp32_audio.h"

#include "mpeg.h"

void setup(void)
{
#ifdef DEV_DEVICE_INIT
  DEV_DEVICE_INIT();
#endif

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("VCD Player");

  // If display and SD shared same interface, init SPI first
#ifdef SPI_SCK
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
#endif

  // Init Display
  if (!gfx->begin(GFX_SPEED))
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);

#ifdef GFX_BL
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR < 3)
  ledcSetup(0, 1000, 8);
  ledcAttachPin(GFX_BL, 0);
  ledcWrite(0, 204);
#else  // ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(GFX_BL, 1000, 8, 1);
  ledcWrite(GFX_BL, 204);
#endif // ESP_ARDUINO_VERSION_MAJOR >= 3
#endif // GFX_BL

#ifdef AUDIO_EXTRA_PRE_INIT
  AUDIO_EXTRA_PRE_INIT();
#endif

  i2s_init();

#ifdef AUDIO_MUTE
  pinMode(AUDIO_MUTE, OUTPUT);
  digitalWrite(AUDIO_MUTE, HIGH);
#endif

#if defined(SD_D1)
#define FILESYSTEM SD_MMC
  SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */, SD_D1, SD_D2, SD_CS /* D3 */);
  if (!SD_MMC.begin(root, false /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_HIGHSPEED))
#elif defined(SD_SCK)
#define FILESYSTEM SD_MMC
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */);
  if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_HIGHSPEED))
#elif defined(SD_CS)
#define FILESYSTEM SD
  if (!SD.begin(SD_CS, SPI, 80000000, "/root"))
#else
#define FILESYSTEM FFat
  // if (!FFat.begin(false, root))
  if (!LittleFS.begin(false, root))
  // if (!SPIFFS.begin(false, root))
#endif
  {
    Serial.println("ERROR: File system mount failed!");
  }
  else
  {
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

      Serial.printf("duration: %ld, total_decode_audio_ms: %lu, total_play_audio_ms: %lu\n", millis() - decode_start_ms, total_decode_audio_ms, total_play_audio_ms);
    }
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("done");
}

void loop()
{
}
