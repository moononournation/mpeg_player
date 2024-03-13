/*
 * http://andrewduncan.net/mpeg/mpeg-1.html
 */

#define PRINT_DEBUG_MSG

#define MPEG_START_CODE_PACK_32 0xBA010000
#define MPEG_START_CODE_PACK 0x000001BA
#define MPEG_START_CODE_SYSTEM_HEADER 0x000001BB
#define MPEG_PACKET_MASK 0x000001C0
#define MPEG_AUDIO_RANGE_START 0x000001C0
#define MPEG_AUDIO_RANGE_END 0x000001CF
#define MPEG_VIDEO_RANGE_START 0x000001E0
#define MPEG_VIDEO_RANGE_END 0x000001EF
#define MPEG_STD_BUFFER_SIZE_MASK 0b11000000
#define MPEG_STD_BUFFER_SIZE_PREFIX 0b01000000

char *buf;
size_t buf_read;
size_t file_index = 0;

#ifdef PRINT_DEBUG_MSG
size_t start_code_offset;
size_t cnt_read = 0;
size_t cnt_BA = 0;
size_t cnt_BB = 0;
size_t cnt_ap = 0;
size_t cnt_vp = 0;
size_t cnt_unknown = 0;
size_t advance_bytes = 0;
#endif

uint32_t start_code = 0;
size_t first_pack_offset = 0;
size_t pack_size = 0;

void mpeg_init(FILE *f)
{
  fseek(f, 0, SEEK_SET);
  file_index = 0;

  int found_pack_count = 0;
  size_t thrid_pack_offset = 0;
  while (fread(&start_code, 4, 1, f) && (found_pack_count < 4))
  {
    if (start_code == MPEG_START_CODE_PACK_32)
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
#ifdef PRINT_DEBUG_MSG
  Serial.printf(
      "first_pack_offset: 0x%08X, pack_size: 0x%08X\n",
      first_pack_offset, pack_size);
  Serial.flush();
#endif

  if (buf)
  {
    buf = (char *)realloc(buf, pack_size);
  }
  else
  {
    buf = (char *)malloc(pack_size);
  }

  fseek(f, first_pack_offset, SEEK_SET);
  file_index = first_pack_offset;
}

void mpeg_packet_scan(FILE *f)
{
  buf_read = fread(buf, 1, pack_size, f);
#ifdef PRINT_DEBUG_MSG
  // Serial.printf("[%08X] read: %d\n", file_index, buf_read);
  // ++cnt_read;
#endif

  int i = 0;
  while (i < buf_read)
  {
    start_code <<= 8;
    start_code |= buf[i++];

    if (start_code == MPEG_START_CODE_PACK)
    {
#ifdef PRINT_DEBUG_MSG
      start_code_offset = file_index + i - 4;
#endif
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

#ifdef PRINT_DEBUG_MSG
      Serial.printf(
          "[%08X] 000001BA PACK, SCR: %u, multiplex_rate: %u\n",
          start_code_offset, system_clock_reference, multiplex_rate);
      Serial.flush();
      ++cnt_BA;
#endif

      start_code = (buf[i++]);
      start_code <<= 8;
      start_code |= (buf[i++]);
      start_code <<= 8;
      start_code |= (buf[i++]);
      start_code <<= 8;
      start_code |= (buf[i++]);
      if (start_code == MPEG_START_CODE_SYSTEM_HEADER)
      {
#ifdef PRINT_DEBUG_MSG
        start_code_offset = file_index + i - 4;
#endif
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

        uint8_t stream_id = 0;
        uint16_t STD_buffer_size_bound = 0;
        bool bound_scale = false;
        if (header_length == 9)
        {
          stream_id = (buf[i++]);
          STD_buffer_size_bound = (buf[i++]);
          bound_scale = (STD_buffer_size_bound & 0b00100000) > 0;
          STD_buffer_size_bound &= 0b00011111;
          STD_buffer_size_bound <<= 8;
          STD_buffer_size_bound |= (buf[i++]);
          STD_buffer_size_bound *= bound_scale ? 1024 : 128;
        }
#ifdef PRINT_DEBUG_MSG
        Serial.printf(
            "[%08X] 000001BB SYSTEM HEADER, header_length: %u, rate_bound: %u, audio_bound: %u, %c, %c, %c, %c, video_bound: %u, stream_id: %u, STD_buffer_size_bound: %u, %s\n",
            start_code_offset, header_length, rate_bound, audio_bound, fixed_bitrate ? 'Y' : 'N', constrained ? 'Y' : 'N', sa_loc ? 'Y' : 'N', sv_loc ? 'Y' : 'N', video_bound, stream_id, STD_buffer_size_bound, bound_scale ? "video" : "audio");
        Serial.flush();
        ++cnt_BB;
#endif
      }
      else if ((start_code & MPEG_PACKET_MASK) > 0)
      {
#ifdef PRINT_DEBUG_MSG
        start_code_offset = file_index + i - 4;
#endif
        uint8_t stream_id = start_code & 0xFF;
        uint16_t packet_length = (buf[i++]);
        packet_length <<= 8;
        packet_length |= (buf[i++]);

        // stuffing byte
        while (buf[i] == 0xFF)
        {
          ++i;
          --packet_length;
        }

        if ((buf[i] & MPEG_STD_BUFFER_SIZE_MASK) == MPEG_STD_BUFFER_SIZE_PREFIX)
        {
          // skip STD_BUFFER_SIZE
          i += 2;
          packet_length -= 2;
        }

        uint32_t presentation_ts = (buf[i++]);
        uint32_t decoding_ts = 0;
        bool pts = (presentation_ts & 0b00100000) > 0;
        bool dts = (presentation_ts & 0b00010000) > 0;

        if (pts)
        {
          presentation_ts &= 0b1110;
          presentation_ts <<= 7;
          presentation_ts |= (buf[i++]);
          presentation_ts <<= 8;
          presentation_ts |= (buf[i++] & 0b11111110);
          presentation_ts <<= 7;
          presentation_ts |= (buf[i++]);
          presentation_ts <<= 7;
          presentation_ts |= (buf[i++]) >> 1;
          presentation_ts /= 90;
          packet_length -= 5;
        }
        else
        {
          presentation_ts = 0;
          --packet_length;
        }

        if (dts)
        {
          decoding_ts = (buf[i++] & 0b1110);
          decoding_ts <<= 7;
          decoding_ts |= (buf[i++]);
          decoding_ts <<= 8;
          decoding_ts |= (buf[i++] & 0b11111110);
          decoding_ts <<= 7;
          decoding_ts |= (buf[i++]);
          decoding_ts <<= 7;
          decoding_ts |= (buf[i++]) >> 1;
          decoding_ts /= 90;
          packet_length -= 5;
        }
        if ((start_code >= MPEG_AUDIO_RANGE_START) && (start_code <= MPEG_AUDIO_RANGE_END)) // audio
        {
#ifdef PRINT_DEBUG_MSG
          Serial.printf(
              "[%08X] %08X Audio Packet, stream_id: %u, packet_length: %u, pts: %c, dts: %c, presentation_ts: %u, decoding_ts: %u\n",
              start_code_offset, start_code, stream_id, packet_length, pts ? 'Y' : 'N', dts ? 'Y' : 'N', presentation_ts, decoding_ts);
          Serial.flush();
          ++cnt_ap;
#endif
        }
        else if ((start_code >= MPEG_VIDEO_RANGE_START) && (start_code <= MPEG_VIDEO_RANGE_END)) // video
        {
#ifdef PRINT_DEBUG_MSG
          Serial.printf(
              "[%08X] %08X Video Packet, stream_id: %u, packet_length: %u, pts: %c, dts: %c, presentation_ts: %u, decoding_ts: %u\n",
              start_code_offset, start_code, stream_id, packet_length, pts ? 'Y' : 'N', dts ? 'Y' : 'N', presentation_ts, decoding_ts);
          Serial.flush();
          ++cnt_vp;
#endif
        }
        i += packet_length;
      }
      else
      {
#ifdef PRINT_DEBUG_MSG
        start_code_offset = file_index + i - 4;
        Serial.printf(
            "[%08X] %08X\n",
            start_code_offset, start_code);
        ++cnt_unknown;
#endif
      }
      start_code = 0;
    }

    if (i >= buf_read)
    {
      file_index += buf_read;

      if (i > buf_read)
      {
        size_t advanced = i - buf_read;
#ifdef PRINT_DEBUG_MSG
        Serial.printf("advanced: %u\n", advanced);
        advance_bytes += advanced;
#endif
        while (advanced)
        {
          buf_read = fread(buf, 1, advanced, f);
          advanced -= buf_read;
          file_index += buf_read;
        }
      }

      buf_read = fread(buf, 1, pack_size, f);
#ifdef PRINT_DEBUG_MSG
      // Serial.printf("[%08X] read: %d\n", file_index, buf_read);
      // ++cnt_read;
#endif
      i = 0;
    }
  }
#ifdef PRINT_DEBUG_MSG
  Serial.printf(
      "BA: %d, BB: %d,ap: %d, vp: %d, unknown: %d, advance_bytes: %u, cnt_read: %d\n",
      cnt_BA, cnt_BB, cnt_ap, cnt_vp, cnt_unknown, advance_bytes, cnt_read);
#endif
}
