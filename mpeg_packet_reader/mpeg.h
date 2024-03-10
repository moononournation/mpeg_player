/*
 * http://andrewduncan.net/mpeg/mpeg-1.html
 */

#define VCD_SECTOR_SIZE 2352
#define MULTIPLEX_RATE 176400

#define MPEG_START_CODE_PACK 0x000001BA
#define MPEG_START_CODE_SYSTEM_HEADER 0x000001BB

char *buf;
size_t buf_read;
int read_count = 0;
size_t file_index = 0;
int cnt_BA = 0;
int cnt_BB = 0;
int cnt_ap = 0;
int cnt_vp = 0;
int cnt_unknown = 0;
int cnt_advanced = 0;

unsigned long start_ms;

uint32_t code = 0;
size_t first_pack_offset = 0;
size_t pack_size = 0;

void mpeg_init(FILE *f)
{
  fseek(f, 0, SEEK_SET);
  file_index = 0;

  int found_pack_count = 0;
  size_t thrid_pack_offset = 0;
  while (fread(&code, 4, 1, f) && (found_pack_count < 4))
  {
    if (code == 0xBA010000)
    {
      ++found_pack_count;
      if (found_pack_count == 1)
      {
        first_pack_offset = file_index;
      }
      else if (found_pack_count == 3)
      {
        thrid_pack_offset = file_index;
      }
      else if (found_pack_count == 4)
      {
        pack_size = file_index - thrid_pack_offset;
      }
    }
    file_index += 4;
  }
  Serial.printf(
      "first_pack_offset: 0x%08X, pack_size: 0x%08X\n",
      first_pack_offset, pack_size);
  Serial.flush();
  buf = (char *)malloc(pack_size);

  fseek(f, first_pack_offset, SEEK_SET);
  file_index = first_pack_offset;
}

void mpeg_packet_scan(FILE *f)
{
  start_ms = millis();

  buf_read = fread(buf, 1, pack_size, f);
  // Serial.printf("[%08X] read: %d\n", file_index, buf_read);

  ++read_count;
  int i = 0;
  while (i < buf_read)
  {
    code <<= 8;
    code |= buf[i++];

    if (code == MPEG_START_CODE_PACK)
    {
      uint32_t system_clock_reference = (buf[i++] & 0b1110);
      system_clock_reference <<= 7;
      system_clock_reference |= (buf[i++]);
      system_clock_reference <<= 8;
      system_clock_reference |= (buf[i++] & 0b11111110);
      system_clock_reference <<= 7;
      system_clock_reference |= (buf[i++]);
      system_clock_reference <<= 7;
      system_clock_reference |= (buf[i++]) >> 1;
      system_clock_reference /= 90;
      uint32_t multiplex_rate = (buf[i++] & 0b01111111);
      multiplex_rate <<= 8;
      multiplex_rate |= (buf[i++]);
      multiplex_rate <<= 7;
      multiplex_rate |= (buf[i++]) >> 1;
      multiplex_rate *= (400 / 8);

      Serial.printf(
          "[%08X] 000001BA PACK, SCR: %u, multiplex_rate: %u\n",
          file_index + i - 12, system_clock_reference, multiplex_rate);
      Serial.flush();

      ++cnt_BA;

      code = (buf[i++]);
      code <<= 8;
      code |= (buf[i++]);
      code <<= 8;
      code |= (buf[i++]);
      code <<= 8;
      code |= (buf[i++]);
      if (code == MPEG_START_CODE_SYSTEM_HEADER)
      {
        uint32_t header_length = (buf[i++]);
        header_length <<= 8;
        header_length |= (buf[i++]);
        uint32_t rate_bound = (buf[i++] & 0b01111111);
        rate_bound <<= 7;
        rate_bound |= (buf[i++]);
        rate_bound <<= 7;
        rate_bound |= (buf[i++]) >> 1;
        uint8_t audio_bound = (buf[i++]);
        bool fixed_bitrate = (audio_bound & 0b10) > 0;
        bool constrained = (audio_bound & 0b1) > 0;
        audio_bound >>= 2;
        uint8_t video_bound = (buf[i++]);
        bool sa_loc = (video_bound & 0b10000000) > 0;
        bool sv_loc = (video_bound & 0b01000000) > 0;
        video_bound &= 0b00011111;
        ++i; // reserved

        Serial.printf(
            "[%08X] 000001BB SYSTEM HEADER, header_length: %u, rate_bound: %u, audio_bound: %u, %c, %c, %c, %c, video_bound: %u",
            file_index + i - 12, header_length, rate_bound, audio_bound, fixed_bitrate ? 'Y' : 'N', constrained ? 'Y' : 'N', sa_loc ? 'Y' : 'N', sv_loc ? 'Y' : 'N', video_bound);
        if (header_length == 9)
        {
          uint8_t stream_id = (buf[i++]);
          uint16_t STD_buffer_size_bound = (buf[i++]);
          bool bound_scale = (STD_buffer_size_bound & 0b00100000) > 0;
          STD_buffer_size_bound &= 0b00011111;
          STD_buffer_size_bound <<= 8;
          STD_buffer_size_bound |= (buf[i++]);
          STD_buffer_size_bound *= bound_scale ? 1024 : 128;

          Serial.printf(
              ", stream_id: %u, STD_buffer_size_bound: %u, %s",
              stream_id, STD_buffer_size_bound, bound_scale ? "video" : "audio");
        }
        Serial.println();
        Serial.flush();

        ++cnt_BB;
      }
      else if ((code >= 0x000001C0) && (code <= 0x000001DF))
      {
        uint8_t stream_id = code & 0xFF;
        uint16_t packet_length = (buf[i++]);
        packet_length <<= 8;
        packet_length |= (buf[i++]);

        Serial.printf(
            "[%08X] %08X Audio Packet, stream_id: %u, packet_length: %u\n",
            file_index + i - 6, code, stream_id, packet_length);
        Serial.flush();

        i += packet_length;

        ++cnt_ap;
      }
      else if ((code >= 0x000001E0) && (code <= 0x000001EF))
      {
        uint8_t stream_id = code & 0xFF;
        uint16_t packet_length = (buf[i++]);
        packet_length <<= 8;
        packet_length |= (buf[i++]);

        Serial.printf(
            "[%08X] %08X Video Packet, stream_id: %u, packet_length: %u\n",
            file_index + i - 6, code, stream_id, packet_length);
        i += packet_length;

        ++cnt_vp;
      }
      else
      {
        Serial.printf("[%08X] %08X\n", file_index + i - 4, code);
        ++cnt_unknown;
      }
      code = 0;
    }

    if (i >= buf_read)
    {
      file_index += buf_read;

      if (i > buf_read)
      {
        size_t advanced = i - buf_read;
        Serial.printf("advanced: %u\n", advanced);
        ++cnt_advanced;

        while (advanced)
        {
          buf_read = fread(buf, 1, advanced, f);
          advanced -= buf_read;
          file_index += buf_read;
        }
      }

      buf_read = fread(buf, 1, pack_size, f);
      // Serial.printf("[%08X] read: %d\n", file_index, buf_read);
      ++read_count;
      i = 0;
    }
  }
  Serial.printf(
      "BA: %d, BB: %d,ap: %d, vp: %d, unknown: %d, advanced: %d, duration: %ld, read_count: %d\n",
      cnt_BA, cnt_BB, cnt_ap, cnt_vp, cnt_unknown, cnt_advanced, millis() - start_ms, read_count);

  free(buf);
}
