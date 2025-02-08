// #pragma GCC optimize("O3")
#pragma GCC optimize("Ofast")
// #pragma GCC optimize("O2")
// #pragma GCC optimize("O1")

const char *root = "/root";
const char *mpeg_file = "/root/AVSEQ02.DAT";

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
// #include "PINS_T-DECK.h"
#include "PINS_JC1060P470.h"

#include <FFat.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "esp32_audio.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
plm_t *plm;
plm_frame_t *frame = NULL;
double plm_frame_interval;
uint16_t frame_interval_ms;
int plm_w;
int plm_h;
uint16_t *plm_buffer;

unsigned long next_frame_ms;
unsigned long cur_ms;
unsigned long remain_ms = 0;
unsigned long total_remain_ms = 0;
int decode_video_count = 0;
int display_video_count = 0;
int decode_audio_count = 0;

void YCbCr2RGB565Be(uint8_t *yData, uint8_t *cbData, uint8_t *crData, uint16_t w, uint16_t h, uint16_t *dest)
{
  int cols = w >> 1;
  int rows = h >> 1;
  uint8_t *yData2 = yData + w;
  uint16_t *dest2 = dest + w;
  for (int row = 0; row < rows; ++row)
  {
    for (int col = 0; col < cols; ++col)
    {
      uint8_t cr = *crData++;
      uint8_t cb = *cbData++;
      int16_t r = CR2R16[cr];
      int16_t g = -CB2G16[cb] - CR2G16[cr];
      int16_t b = CB2B16[cb];
      int16_t y;

      y = Y2I16[*yData++];
      *dest++ = CLIPRBE[y + r] | CLIPGBE[y + g] | CLIPBBE[y + b];
      y = Y2I16[*yData++];
      *dest++ = CLIPRBE[y + r] | CLIPGBE[y + g] | CLIPBBE[y + b];
      y = Y2I16[*yData2++];
      *dest2++ = CLIPRBE[y + r] | CLIPGBE[y + g] | CLIPBBE[y + b];
      y = Y2I16[*yData2++];
      *dest2++ = CLIPRBE[y + r] | CLIPGBE[y + g] | CLIPBBE[y + b];
    }
    yData += w;
    yData2 += w;
    dest += w;
    dest2 += w;
  }
}

// This function gets called for each decoded video frame
void my_video_callback(plm_t *plm, plm_frame_t *frame, void *user)
{
  // if (cur_ms < next_frame_ms)
  // if (decode_video_count % 2)
  {
    YCbCr2RGB565Be(frame->y.data, frame->cb.data, frame->cr.data, frame->width, frame->height, plm_buffer);
#if defined(SPI_SCK) && defined(SD_CS)
    // explicit disable SD before use display
    digitalWrite(SD_CS, HIGH);
#endif
    gfx->draw16bitBeRGBBitmap(0, 0, plm_buffer, plm_w, plm_h);
    ++display_video_count;
  }
  // else
  // {
  //   Serial.printf("skip display frame #%d\n", decode_video_count);
  // }
  ++decode_video_count;
}

// This function gets called for each decoded audio frame
void my_audio_callback(plm_t *plm, plm_samples_t *frame, void *user)
{
  // Do something with samples->interleaved
  i2s_play_float(frame->interleaved, frame->count);
  ++decode_audio_count;
}

void setup(void)
{
#ifdef DEV_DEVICE_INIT
  DEV_DEVICE_INIT();
#endif

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("MPEG Player");

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
    plm = plm_create_with_filename(mpeg_file);
    if (!plm)
    {
      printf("Couldn't open file %s\n", mpeg_file);
    }

    // Probe the MPEG-PS data to find the actual number of video and audio streams
    plm_probe(plm, PLM_BUFFER_DEFAULT_SIZE);

    // Install the video & audio decode callbacks
    plm_set_video_decode_callback(plm, my_video_callback, NULL);
    plm_set_audio_decode_callback(plm, my_audio_callback, NULL);

    // plm_video_set_no_delay(plm->video_decoder, true);
    // plm_set_video_enabled(plm, false);
    // plm_set_audio_enabled(plm, false);

    plm_frame_interval = 1.0 / plm_get_framerate(plm);
    frame_interval_ms = (uint16_t)(plm_frame_interval * 1000);
    plm_w = plm_get_width(plm);
    plm_h = plm_get_height(plm);
    plm_buffer = (uint16_t *)malloc(plm_w * plm_h * 2);
  }
}

void loop()
{
  unsigned long start_ms = millis();
  next_frame_ms = start_ms;
  do
  {
    cur_ms = millis();
    if (next_frame_ms > cur_ms)
    {
      remain_ms = next_frame_ms - cur_ms;
      if (remain_ms > 200)
      {
        // Serial.printf("Remain %d ms\n", remain_ms);
        remain_ms -= 200;
        delay(remain_ms);
        total_remain_ms += remain_ms;
      }
    }
    else
    {
      // Serial.printf("Excess: %lu\n", cur_ms - next_frame_ms);
    }

    plm_decode(plm, plm_frame_interval);

    next_frame_ms += frame_interval_ms;
  } while (!plm_has_ended(plm));

  Serial.printf("Time used: %lu, decode_video_count: %d, display_video_count: %d, decode_audio_count: %d, remain: %lu\n", millis() - start_ms, decode_video_count, display_video_count, decode_audio_count, total_remain_ms);
  delay(LONG_MAX);
}
