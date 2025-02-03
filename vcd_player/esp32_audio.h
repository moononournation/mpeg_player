#include "driver/i2s.h"

#define I2S_DEFAULT_SAMPLE_RATE 44100

extern unsigned long total_decode_audio_ms;
extern unsigned long total_play_audio_ms;

uint32_t i2s_curr_sample_rate = I2S_DEFAULT_SAMPLE_RATE;
void i2s_set_sample_rate(uint32_t sample_rate)
{
  Serial.printf("i2s_set_sample_rate: %lu\n", sample_rate);
  i2s_curr_sample_rate = sample_rate;
  i2s_set_clk(I2S_OUTPUT_NUM, i2s_curr_sample_rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

esp_err_t i2s_init()
{
  esp_err_t ret_val = ESP_OK;

  i2s_config_t i2s_config;
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = i2s_curr_sample_rate;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 32;
  i2s_config.dma_buf_len = 576;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = true;
  i2s_config.fixed_mclk = 0;
  i2s_config.mclk_multiple = I2S_MCLK_MULTIPLE_128;
  i2s_config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  i2s_pin_config_t pin_config;
  pin_config.mck_io_num = I2S_MCLK;
  pin_config.bck_io_num = I2S_BCLK;
  pin_config.ws_io_num = I2S_LRCK;
  pin_config.data_out_num = I2S_DOUT;
  pin_config.data_in_num = I2S_DIN;

  ret_val |= i2s_driver_install(I2S_OUTPUT_NUM, &i2s_config, 0, NULL);
  ret_val |= i2s_set_pin(I2S_OUTPUT_NUM, &pin_config);

  i2s_zero_dma_buffer(I2S_OUTPUT_NUM);

  return ret_val;
}

#include "kjmp2.h"
kjmp2_context_t *audio_context;
unsigned char *audio_buf;
uint32_t audio_buf_read = 0;
volatile uint32_t audio_buf_remain = 0;
signed short *pcm_out;

const uint32_t audio_buf_size = KJMP2_MAX_FRAME_SIZE * 4;

static void fill_audio_frame(uint32_t presentation_ts, char *a_buf, uint32_t len)
{
  // Serial.printf("[fill_audio_frame] presentation_ts: %u, audio_buf_remain: %u, len: %u\n", presentation_ts, audio_buf_remain, len);
  // Serial.flush();
  while (len)
  {
    if ((audio_buf_remain + len) > audio_buf_size)
    {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    else
    {
      memcpy(&audio_buf[audio_buf_remain], a_buf, len);
      audio_buf_remain += len;
      len = 0;
    }
  }
}

static void mp2_player_task(void *pvParam)
{
  unsigned long ms;
  int16_t decoded = -1;
  do
  {
    if (audio_buf_remain > 0)
    {
      ms = millis();
      decoded = kjmp2_decode_frame(audio_context, (const unsigned char *)audio_buf, pcm_out);
      total_decode_audio_ms += millis() - ms;
      // Serial.printf("[mp2_player_task] audio_buf_remain: %u, decoded: %u\n", audio_buf_remain, decoded);
      // Serial.flush();

      if (decoded > 0)
      {
        ms = millis();
        memmove(audio_buf, &audio_buf[decoded], audio_buf_remain - decoded);
        audio_buf_remain -= decoded;

        int16_t *pwm = (int16_t *)pcm_out;
#ifdef I2S_DEFAULT_GAIN_LEVEL
        for (int i = 0; i < KJMP2_SAMPLES_PER_FRAME * 2; i++)
        {
          pwm[i] = pwm[i] * 0.01f * I2S_DEFAULT_GAIN_LEVEL;
        }
#endif
        size_t i2s_bytes_written;
        i2s_write(I2S_OUTPUT_NUM, pcm_out, KJMP2_SAMPLES_PER_FRAME * 4, &i2s_bytes_written, portMAX_DELAY);
        total_play_audio_ms += millis() - ms;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  } while ((decoded != 0) || (audio_buf_remain > 0));

  Serial.printf("==================== MP2 stop ====================\n");
  Serial.flush();

  i2s_zero_dma_buffer(I2S_NUM_0);
  vTaskDelete(NULL);
}

static BaseType_t mp2_player_task_start()
{
  audio_context = (kjmp2_context_t *)calloc(1, sizeof(kjmp2_context_t));
  kjmp2_init(audio_context);
  audio_buf = (unsigned char *)calloc(1, audio_buf_size);
  pcm_out = (signed short *)calloc(1, KJMP2_SAMPLES_PER_FRAME * 4);

  return xTaskCreatePinnedToCore(
      mp2_player_task,
      "MP2 Player Task",
      2000,
      NULL,
      configMAX_PRIORITIES - 1,
      NULL,
      0);
}
