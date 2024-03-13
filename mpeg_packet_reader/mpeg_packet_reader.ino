const char *root = "/root";
// const char *mpeg_file = "/root/224x128.mpg";
// const char *mpeg_file = "/root/320x240.mpg";
const char *mpeg_file = "/root/AVSEQ02.DAT";
// const char *mpeg_file = "/root/VCD.DAT";

#include <WiFi.h>

#include <FFat.h>
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>

// microSD card
#define SD_SCK TDECK_SPI_SCK
#define SD_MISO TDECK_SPI_MISO
#define SD_MOSI TDECK_SPI_MOSI
#define SD_CS TDECK_SDCARD_CS

#include "mpeg.h"

void setup(void)
{
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("MPEG Packet Reader");

  // If display and SD shared same interface, init SPI first
  // SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI);

  if (!FFat.begin(false, root))
  // if (!LittleFS.begin(false, root))
  // if (!SD.begin(SD_CS, SPI, 80000000, "/root"))
  // pinMode(SD_CS, OUTPUT);
  // digitalWrite(SD_CS, HIGH);
  // SD_MMC.setPins(SD_SCK, SD_MOSI, SD_MISO);
  // if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
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
      unsigned long start_ms = millis();

      mpeg_init(f);
      mpeg_packet_scan(f);
      fclose(f);

      Serial.printf("duration: %ld\n", millis() - start_ms);
    }
  }
  Serial.println("done");
}

void loop()
{
}
