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

uint8_t aBuf[1152 * 4];
union
{
  uint16_t v16;
  uint8_t v8[2];
} iSample;
static void i2s_play_float(float *sample, uint16_t len)
{
  uint16_t i = len << 1;
  uint8_t *p = aBuf;
  while (i--)
  {
#ifdef I2S_DEFAULT_GAIN_LEVEL
    iSample.v16 = (uint16_t)((*sample++ * (32767.0f * 0.01f * I2S_DEFAULT_GAIN_LEVEL)) + 32768);
    // iSample.v16 = (uint16_t)((*sample++ * (32767.0f / 2147418112.0f * I2S_DEFAULT_GAIN_LEVEL)) + 32768);
#else
    iSample.v16 = (uint16_t)(*sample++ * (32767.0f)) + 32768;
#endif

    *p++ = iSample.v8[1];
    *p++ = iSample.v8[0];
  }
  size_t i2s_bytes_written = 0;
  i2s_write(I2S_OUTPUT_NUM, aBuf, len << 2, &i2s_bytes_written, portMAX_DELAY);
}
