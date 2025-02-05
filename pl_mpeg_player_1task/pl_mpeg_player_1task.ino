// #pragma GCC optimize("O3")
#pragma GCC optimize("Ofast")
// #pragma GCC optimize("O2")
// #pragma GCC optimize("O1")

const char *root = "/root";
const char *mpeg_file = "/root/AVSEQ02.DAT";

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
#include "PINS_JC4827W543.h"
// #include "PINS_JC1060P470.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <FFat.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "esp32_audio.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "plm_audio.h"
plm_t *plm;
plm_frame_t *frame = NULL;
double plm_frame_interval;
uint16_t frame_interval_ms;
int plm_w;
int plm_h;
int y_size;
int cbcr_size;
uint8_t *y_buffer;
uint8_t *cb_buffer;
uint8_t *cr_buffer;
TaskHandle_t video_task_handle;
QueueHandle_t video_queue_handle;

uint16_t disp_w;
uint16_t disp_h;
uint16_t ys_offset = 0;
uint16_t cbcrs_offset = 0;
uint16_t x_skip = 0;
uint16_t ys_skip;
uint16_t yt_skip;
uint16_t cbcrs_skip;
uint16_t frame_count;

unsigned long next_frame_ms;
unsigned long cur_ms;
unsigned long remain_ms = 0;
unsigned long total_remain_ms = 0;
int decode_video_count = 0;
int display_video_count = 0;
int decode_audio_count = 0;

typedef struct
{
  volatile uint_fast8_t queue = false;
} queue_t;

queue_t *q = NULL;

static void convert_video_task(void *arg)
{
  queue_t *q1 = NULL;

  while (xQueueReceive(video_queue_handle, &q1, portMAX_DELAY))
  {
    gfx->drawYCbCrBitmap(0, 0, y_buffer, cb_buffer, cr_buffer, disp_w, disp_h);
    ++display_video_count;
  }
}

// This function gets called for each decoded video frame
void my_video_callback(plm_t *plm, plm_frame_t *frame, void *user)
{
  // if (cur_ms < next_frame_ms)
  // {
    uint32_t *y_src = (uint32_t *)(frame->y.data + ys_offset);
    uint32_t *y_src2 = y_src + (plm_w >> 2);
    uint32_t *y_trgt = (uint32_t *)y_buffer;
    uint32_t *y_trgt2 = y_trgt + (disp_w >> 2);
    uint32_t *cb_src = (uint32_t *)(frame->cb.data + cbcrs_offset);
    uint32_t *cb_trgt = (uint32_t *)cb_buffer;
    uint32_t *cr_src = (uint32_t *)(frame->cr.data + cbcrs_offset);
    uint32_t *cr_trgt = (uint32_t *)cr_buffer;

    uint16_t w = disp_w >> 3;
    uint16_t h = disp_h >> 1;
    while (h--)
    {
      uint16_t i = w;
      while (i--)
      {
        *y_trgt++ = *y_src++;
        *y_trgt++ = *y_src++;
        *y_trgt2++ = *y_src2++;
        *y_trgt2++ = *y_src2++;
        *cb_trgt++ = *cb_src++;
        *cr_trgt++ = *cr_src++;
      }
      y_src += ys_skip;
      y_src2 += ys_skip;
      y_trgt += yt_skip;
      y_trgt2 += yt_skip;
      cb_src += cbcrs_skip;
      cr_src += cbcrs_skip;
    }

    xQueueSend(video_queue_handle, &q, 0);
  // }
  // else
  // {
  //   // Serial.printf("skip display frame #%d\n", decode_video_count);
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
    disp_w = gfx->width();
    disp_h = gfx->height();
    if (disp_w < plm_w)
    {
      x_skip = plm_w - disp_w;
      ys_offset += x_skip / 2;
      cbcrs_offset += x_skip / 2 / 2;
    }
    else
    {
      disp_w = plm_w;
    }
    if (disp_h < plm_h)
    {
      ys_offset += ((plm_h - disp_h) / 2) * plm_w;
      cbcrs_offset += ((plm_h - disp_h) / 2) * plm_w / 4;
    }
    else
    {
      disp_h = plm_h;
    }
    ys_skip = (plm_w >> 2) + (x_skip >> 2);
    yt_skip = disp_w >> 2;
    cbcrs_skip = x_skip >> 3;

    y_size = disp_w * disp_h;
    cbcr_size = y_size >> 2;
    y_buffer = (uint8_t *)malloc(y_size);
    cb_buffer = (uint8_t *)malloc(cbcr_size);
    cr_buffer = (uint8_t *)malloc(cbcr_size);

    frame_count = 0;

    video_queue_handle = xQueueCreate(1, sizeof(queue_t *));

    xTaskCreatePinnedToCore(convert_video_task, "convert_video_task", 1600, NULL, 1, &video_task_handle, 0);

    Serial.printf("plm_frame_interval: %f, frame_interval_ms: %d, plm_w: %d, plm_h: %d\n", plm_frame_interval, frame_interval_ms, plm_w, plm_h);
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

  vQueueDelete(video_queue_handle);

  delay(LONG_MAX);
}
