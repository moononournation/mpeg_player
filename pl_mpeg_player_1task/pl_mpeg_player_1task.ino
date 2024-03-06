const char *root = "/root";
const char *mpeg_file = "/root/320x240.mpg";

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <WiFi.h>

#include <FFat.h>
#include <LittleFS.h>

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
Arduino_DataBus *bus = new Arduino_ESP32SPIDMA(TDECK_TFT_DC, TDECK_TFT_CS, TDECK_SPI_SCK, TDECK_SPI_MOSI, GFX_NOT_DEFINED);
Arduino_TFT *gfx = new Arduino_ST7789(bus, GFX_NOT_DEFINED /* RST */, 1 /* rotation */, true /* IPS */);
/*******************************************************************************
   End of Arduino_GFX setting
 ******************************************************************************/

#include "esp32_audio.h"
// I2S
#define I2S_DOUT TDECK_I2S_DOUT
#define I2S_BCLK TDECK_I2S_BCK
#define I2S_LRCK TDECK_I2S_WS

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

int decode_video_count = 0;
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
    ++decode_video_count;
  }
}

// This function gets called for each decoded video frame
void my_video_callback(plm_t *plm, plm_frame_t *frame, void *user)
{
  // Do something with frame->y.data, frame->cr.data, frame->cb.data
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
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("MPEG Player");

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

  if (!FFat.begin(false, root))
  // if (!LittleFS.begin(false, root))
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
  unsigned long next_frame_ms = start_ms;
  unsigned long cur_ms;
  unsigned long remain_ms = 0;
  unsigned long total_remain_ms = 0;
  do
  {
    cur_ms = millis();
    if (next_frame_ms > cur_ms)
    {
      remain_ms = next_frame_ms - cur_ms;
      delay(remain_ms >> 1);
      total_remain_ms += remain_ms;
    }
    else
    {
      // Serial.printf("Excess: %lu\n", cur_ms - next_frame_ms);
    }

    plm_decode(plm, plm_frame_interval);

    next_frame_ms += frame_interval_ms;
  } while (!plm_has_ended(plm));

  Serial.printf("Time used: %lu, decode_video_count: %d, decode_audio_count: %d, remain: %lu\n", millis() - start_ms, decode_video_count, decode_audio_count, total_remain_ms);

  vQueueDelete(video_queue_handle);

  delay(LONG_MAX);
}
