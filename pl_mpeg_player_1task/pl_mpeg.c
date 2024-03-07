// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// IMPLEMENTATION

#include <string.h>
#include <stdlib.h>

#include "pl_mpeg.h"

// -----------------------------------------------------------------------------
// plm (high-level interface) implementation

struct plm_t
{
	plm_demux_t *demux;
	double time;
	int has_ended;
	int loop;
	int has_decoders;

	int video_enabled;
	int video_packet_type;
	plm_buffer_t *video_buffer;
	plm_video_t *video_decoder;

	int audio_enabled;
	int audio_stream_index;
	int audio_packet_type;
	double audio_lead_time;
	plm_buffer_t *audio_buffer;
	plm_audio_t *audio_decoder;

	plm_video_decode_callback video_decode_callback;
	void *video_decode_callback_user_data;

	plm_audio_decode_callback audio_decode_callback;
	void *audio_decode_callback_user_data;
};

int plm_init_decoders(plm_t *self);
void plm_handle_end(plm_t *self);
void plm_read_video_packet(plm_buffer_t *buffer, void *user);
void plm_read_audio_packet(plm_buffer_t *buffer, void *user);
void plm_read_packets(plm_t *self, int requested_type);

plm_t *plm_create_with_filename(const char *filename)
{
// printf("plm_create_with_filename\n");
	plm_buffer_t *buffer = plm_buffer_create_with_filename(filename);
	if (!buffer)
	{
		return NULL;
	}
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_file(FILE *fh, int close_when_done)
{
// printf("plm_create_with_file\n");
	plm_buffer_t *buffer = plm_buffer_create_with_file(fh, close_when_done);
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_memory(uint8_t *bytes, size_t length, int free_when_done)
{
// printf("plm_create_with_memory\n");
	plm_buffer_t *buffer = plm_buffer_create_with_memory(bytes, length, free_when_done);
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done)
{
// printf("plm_create_with_buffer\n");
	plm_t *self = (plm_t *)PLM_MALLOC(sizeof(plm_t));
	memset(self, 0, sizeof(plm_t));

	self->demux = plm_demux_create(buffer, destroy_when_done);
	self->video_enabled = TRUE;
	self->audio_enabled = TRUE;
	plm_init_decoders(self);

	return self;
}

int plm_init_decoders(plm_t *self)
{
// printf("plm_init_decoders\n");
	if (self->has_decoders)
	{
		return TRUE;
	}

	if (!plm_demux_has_headers(self->demux))
	{
		return FALSE;
	}

	if (plm_demux_get_num_video_streams(self->demux) > 0)
	{
		if (self->video_enabled)
		{
			self->video_packet_type = PLM_DEMUX_PACKET_VIDEO_1;
		}
		if (!self->video_decoder)
		{
			self->video_buffer = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
			plm_buffer_set_load_callback(self->video_buffer, plm_read_video_packet, self);
			self->video_decoder = plm_video_create_with_buffer(self->video_buffer, TRUE);
		}
	}

	if (plm_demux_get_num_audio_streams(self->demux) > 0)
	{
		if (self->audio_enabled)
		{
			self->audio_packet_type = PLM_DEMUX_PACKET_AUDIO_1 + self->audio_stream_index;
		}
		if (!self->audio_decoder)
		{
			self->audio_buffer = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
			plm_buffer_set_load_callback(self->audio_buffer, plm_read_audio_packet, self);
			self->audio_decoder = plm_audio_create_with_buffer(self->audio_buffer, TRUE);
		}
	}

	self->has_decoders = TRUE;
	return TRUE;
}

void plm_destroy(plm_t *self)
{
// printf("plm_destroy\n");
	if (self->video_decoder)
	{
		plm_video_destroy(self->video_decoder);
	}
	if (self->audio_decoder)
	{
		plm_audio_destroy(self->audio_decoder);
	}

	plm_demux_destroy(self->demux);
	PLM_FREE(self);
}

int plm_get_audio_enabled(plm_t *self)
{
// printf("plm_get_audio_enabled\n");
	return self->audio_enabled;
}

int plm_has_headers(plm_t *self)
{
// printf("plm_has_headers\n");
	if (!plm_demux_has_headers(self->demux))
	{
		return FALSE;
	}

	if (!plm_init_decoders(self))
	{
		return FALSE;
	}

	if (
			(self->video_decoder && !plm_video_has_header(self->video_decoder)) ||
			(self->audio_decoder && !plm_audio_has_header(self->audio_decoder)))
	{
		return FALSE;
	}

	return TRUE;
}

int plm_probe(plm_t *self, size_t probesize)
{
// printf("plm_probe\n");
	int found_streams = plm_demux_probe(self->demux, probesize);
	if (!found_streams)
	{
		return FALSE;
	}

	// Re-init decoders
	self->has_decoders = FALSE;
	self->video_packet_type = 0;
	self->audio_packet_type = 0;
	return plm_init_decoders(self);
}

void plm_set_audio_enabled(plm_t *self, int enabled)
{
// printf("plm_set_audio_enabled\n");
	self->audio_enabled = enabled;

	if (!enabled)
	{
		self->audio_packet_type = 0;
		return;
	}

	self->audio_packet_type = (plm_init_decoders(self) && self->audio_decoder)
																? PLM_DEMUX_PACKET_AUDIO_1 + self->audio_stream_index
																: 0;
}

void plm_set_audio_stream(plm_t *self, int stream_index)
{
// printf("plm_set_audio_stream\n");
	if (stream_index < 0 || stream_index > 3)
	{
		return;
	}
	self->audio_stream_index = stream_index;

	// Set the correct audio_packet_type
	plm_set_audio_enabled(self, self->audio_enabled);
}

int plm_get_video_enabled(plm_t *self)
{
// printf("plm_get_video_enabled\n");
	return self->video_enabled;
}

void plm_set_video_enabled(plm_t *self, int enabled)
{
// printf("plm_set_video_enabled\n");
	self->video_enabled = enabled;

	if (!enabled)
	{
		self->video_packet_type = 0;
		return;
	}

	self->video_packet_type = (plm_init_decoders(self) && self->video_decoder)
																? PLM_DEMUX_PACKET_VIDEO_1
																: 0;
}

int plm_get_num_video_streams(plm_t *self)
{
// printf("plm_get_num_video_streams\n");
	return plm_demux_get_num_video_streams(self->demux);
}

int plm_get_width(plm_t *self)
{
// printf("plm_get_width\n");
	return (plm_init_decoders(self) && self->video_decoder)
						 ? plm_video_get_width(self->video_decoder)
						 : 0;
}

int plm_get_height(plm_t *self)
{
// printf("plm_get_height\n");
	return (plm_init_decoders(self) && self->video_decoder)
						 ? plm_video_get_height(self->video_decoder)
						 : 0;
}

double plm_get_framerate(plm_t *self)
{
// printf("plm_get_framerate\n");
	return (plm_init_decoders(self) && self->video_decoder)
						 ? plm_video_get_framerate(self->video_decoder)
						 : 0;
}

int plm_get_num_audio_streams(plm_t *self)
{
// printf("plm_get_num_audio_streams\n");
	return plm_demux_get_num_audio_streams(self->demux);
}

int plm_get_samplerate(plm_t *self)
{
// printf("plm_get_samplerate\n");
	return (plm_init_decoders(self) && self->audio_decoder)
						 ? plm_audio_get_samplerate(self->audio_decoder)
						 : 0;
}

double plm_get_audio_lead_time(plm_t *self)
{
// printf("plm_get_audio_lead_time\n");
	return self->audio_lead_time;
}

void plm_set_audio_lead_time(plm_t *self, double lead_time)
{
// printf("plm_set_audio_lead_time\n");
	self->audio_lead_time = lead_time;
}

double plm_get_time(plm_t *self)
{
// printf("plm_get_time\n");
	return self->time;
}

double plm_get_duration(plm_t *self)
{
// printf("plm_get_duration\n");
	return plm_demux_get_duration(self->demux, PLM_DEMUX_PACKET_VIDEO_1);
}

void plm_rewind(plm_t *self)
{
// printf("plm_rewind\n");
	if (self->video_decoder)
	{
		plm_video_rewind(self->video_decoder);
	}

	if (self->audio_decoder)
	{
		plm_audio_rewind(self->audio_decoder);
	}

	plm_demux_rewind(self->demux);
	self->time = 0;
}

int plm_get_loop(plm_t *self)
{
// printf("plm_get_loop\n");
	return self->loop;
}

void plm_set_loop(plm_t *self, int loop)
{
// printf("plm_set_loop\n");
	self->loop = loop;
}

int plm_has_ended(plm_t *self)
{
// printf("plm_has_ended\n");
	return self->has_ended;
}

void plm_set_video_decode_callback(plm_t *self, plm_video_decode_callback fp, void *user)
{
// printf("plm_set_video_decode_callback\n");
	self->video_decode_callback = fp;
	self->video_decode_callback_user_data = user;
}

void plm_set_audio_decode_callback(plm_t *self, plm_audio_decode_callback fp, void *user)
{
// printf("plm_set_audio_decode_callback\n");
	self->audio_decode_callback = fp;
	self->audio_decode_callback_user_data = user;
}

void plm_decode(plm_t *self, double tick)
{
// printf("plm_decode\n");
	if (!plm_init_decoders(self))
	{
		return;
	}

	int decode_video = (self->video_decode_callback && self->video_packet_type);
	int decode_audio = (self->audio_decode_callback && self->audio_packet_type);

	if (!decode_video && !decode_audio)
	{
		// Nothing to do here
		return;
	}

	int did_decode = FALSE;
	int decode_video_failed = FALSE;
	int decode_audio_failed = FALSE;

	double video_target_time = self->time + tick;
	double audio_target_time = self->time + tick + self->audio_lead_time;

	do
	{
		did_decode = FALSE;

		if (decode_video && plm_video_get_time(self->video_decoder) < video_target_time)
		{
			plm_frame_t *frame = plm_video_decode(self->video_decoder);
			if (frame)
			{
				self->video_decode_callback(self, frame, self->video_decode_callback_user_data);
				did_decode = TRUE;
			}
			else
			{
				decode_video_failed = TRUE;
			}
		}

		if (decode_audio && plm_audio_get_time(self->audio_decoder) < audio_target_time)
		{
			plm_samples_t *samples = plm_audio_decode(self->audio_decoder);
			if (samples)
			{
				self->audio_decode_callback(self, samples, self->audio_decode_callback_user_data);
				did_decode = TRUE;
			}
			else
			{
				decode_audio_failed = TRUE;
			}
		}
	} while (did_decode);

	// Did all sources we wanted to decode fail and the demuxer is at the end?
	if (
			(!decode_video || decode_video_failed) &&
			(!decode_audio || decode_audio_failed) &&
			plm_demux_has_ended(self->demux))
	{
		plm_handle_end(self);
		return;
	}

	self->time += tick;
}

plm_frame_t *plm_decode_video(plm_t *self)
{
// printf("plm_decode_video\n");
	if (!plm_init_decoders(self))
	{
		return NULL;
	}

	if (!self->video_packet_type)
	{
		return NULL;
	}

	plm_frame_t *frame = plm_video_decode(self->video_decoder);
	if (frame)
	{
		self->time = frame->time;
	}
	else if (plm_demux_has_ended(self->demux))
	{
		plm_handle_end(self);
	}
	return frame;
}

plm_samples_t *plm_decode_audio(plm_t *self)
{
// printf("plm_decode_audio\n");
	if (!plm_init_decoders(self))
	{
		return NULL;
	}

	if (!self->audio_packet_type)
	{
		return NULL;
	}

	plm_samples_t *samples = plm_audio_decode(self->audio_decoder);
	if (samples)
	{
		self->time = samples->time;
	}
	else if (plm_demux_has_ended(self->demux))
	{
		plm_handle_end(self);
	}
	return samples;
}

void plm_handle_end(plm_t *self)
{
// printf("plm_handle_end\n");
	if (self->loop)
	{
		plm_rewind(self);
	}
	else
	{
		self->has_ended = TRUE;
	}
}

void plm_read_video_packet(plm_buffer_t *buffer, void *user)
{
// printf("plm_read_video_packet\n");
	PLM_UNUSED(buffer);
	plm_t *self = (plm_t *)user;
	plm_read_packets(self, self->video_packet_type);
}

void plm_read_audio_packet(plm_buffer_t *buffer, void *user)
{
// printf("plm_read_audio_packet\n");
	PLM_UNUSED(buffer);
	plm_t *self = (plm_t *)user;
	plm_read_packets(self, self->audio_packet_type);
}

void plm_read_packets(plm_t *self, int requested_type)
{
// printf("plm_read_packets\n");
	plm_packet_t *packet;
	while ((packet = plm_demux_decode(self->demux)))
	{
		if (packet->type == self->video_packet_type)
		{
			plm_buffer_write(self->video_buffer, packet->data, packet->length);
		}
		else if (packet->type == self->audio_packet_type)
		{
			plm_buffer_write(self->audio_buffer, packet->data, packet->length);
		}

		if (packet->type == requested_type)
		{
			return;
		}
	}

	if (plm_demux_has_ended(self->demux))
	{
		if (self->video_buffer)
		{
			plm_buffer_signal_end(self->video_buffer);
		}
		if (self->audio_buffer)
		{
			plm_buffer_signal_end(self->audio_buffer);
		}
	}
}

plm_frame_t *plm_seek_frame(plm_t *self, double time, int seek_exact)
{
// printf("plm_seek_frame\n");
	if (!plm_init_decoders(self))
	{
		return NULL;
	}

	if (!self->video_packet_type)
	{
		return NULL;
	}

	int type = self->video_packet_type;

	double start_time = plm_demux_get_start_time(self->demux, type);
	double duration = plm_demux_get_duration(self->demux, type);

	if (time < 0)
	{
		time = 0;
	}
	else if (time > duration)
	{
		time = duration;
	}

	plm_packet_t *packet = plm_demux_seek(self->demux, time, type, TRUE);
	if (!packet)
	{
		return NULL;
	}

	// Disable writing to the audio buffer while decoding video
	int previous_audio_packet_type = self->audio_packet_type;
	self->audio_packet_type = 0;

	// Clear video buffer and decode the found packet
	plm_video_rewind(self->video_decoder);
	plm_video_set_time(self->video_decoder, packet->pts - start_time);
	plm_buffer_write(self->video_buffer, packet->data, packet->length);
	plm_frame_t *frame = plm_video_decode(self->video_decoder);

	// If we want to seek to an exact frame, we have to decode all frames
	// on top of the intra frame we just jumped to.
	if (seek_exact)
	{
		while (frame && frame->time < time)
		{
			frame = plm_video_decode(self->video_decoder);
		}
	}

	// Enable writing to the audio buffer again?
	self->audio_packet_type = previous_audio_packet_type;

	if (frame)
	{
		self->time = frame->time;
	}

	self->has_ended = FALSE;
	return frame;
}

int plm_seek(plm_t *self, double time, int seek_exact)
{
// printf("plm_seek\n");
	plm_frame_t *frame = plm_seek_frame(self, time, seek_exact);

	if (!frame)
	{
		return FALSE;
	}

	if (self->video_decode_callback)
	{
		self->video_decode_callback(self, frame, self->video_decode_callback_user_data);
	}

	// If audio is not enabled we are done here.
	if (!self->audio_packet_type)
	{
		return TRUE;
	}

	// Sync up Audio. This demuxes more packets until the first audio packet
	// with a PTS greater than the current time is found. plm_decode() is then
	// called to decode enough audio data to satisfy the audio_lead_time.

	double start_time = plm_demux_get_start_time(self->demux, self->video_packet_type);
	plm_audio_rewind(self->audio_decoder);

	plm_packet_t *packet = NULL;
	while ((packet = plm_demux_decode(self->demux)))
	{
		if (packet->type == self->video_packet_type)
		{
			plm_buffer_write(self->video_buffer, packet->data, packet->length);
		}
		else if (
				packet->type == self->audio_packet_type &&
				packet->pts - start_time > self->time)
		{
			plm_audio_set_time(self->audio_decoder, packet->pts - start_time);
			plm_buffer_write(self->audio_buffer, packet->data, packet->length);
			plm_decode(self, 0);
			break;
		}
	}

	return TRUE;
}

// -----------------------------------------------------------------------------
// plm_buffer implementation

plm_buffer_t *plm_buffer_create_with_filename(const char *filename)
{
// printf("plm_buffer_create_with_filename\n");
	FILE *fh = fopen(filename, "rb");
	if (!fh)
	{
		return NULL;
	}
	return plm_buffer_create_with_file(fh, TRUE);
}

plm_buffer_t *plm_buffer_create_with_file(FILE *fh, int close_when_done)
{
// printf("plm_buffer_create_with_file\n");
	plm_buffer_t *self = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
	self->fh = fh;
	self->close_when_done = close_when_done;
	self->mode = PLM_BUFFER_MODE_FILE;
	self->discard_read_bytes = TRUE;

	fseek(self->fh, 0, SEEK_END);
	self->total_size = ftell(self->fh);
	fseek(self->fh, 0, SEEK_SET);

	plm_buffer_set_load_callback(self, plm_buffer_load_file_callback, NULL);
	return self;
}

plm_buffer_t *plm_buffer_create_with_memory(uint8_t *bytes, size_t length, int free_when_done)
{
// printf("plm_buffer_create_with_memory\n");
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	memset(self, 0, sizeof(plm_buffer_t));
	self->capacity = length;
	self->length = length;
	self->total_size = length;
	self->free_when_done = free_when_done;
	self->bytes = bytes;
	self->mode = PLM_BUFFER_MODE_FIXED_MEM;
	self->discard_read_bytes = FALSE;
	return self;
}

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity)
{
// printf("plm_buffer_create_with_capacity\n");
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	memset(self, 0, sizeof(plm_buffer_t));
	self->capacity = capacity;
	self->free_when_done = TRUE;
	self->bytes = (uint8_t *)PLM_MALLOC(capacity);
	self->mode = PLM_BUFFER_MODE_RING;
	self->discard_read_bytes = TRUE;
	return self;
}

plm_buffer_t *plm_buffer_create_for_appending(size_t initial_capacity)
{
// printf("plm_buffer_create_for_appending\n");
	plm_buffer_t *self = plm_buffer_create_with_capacity(initial_capacity);
	self->mode = PLM_BUFFER_MODE_APPEND;
	self->discard_read_bytes = FALSE;
	return self;
}

void plm_buffer_destroy(plm_buffer_t *self)
{
// printf("plm_buffer_destroy\n");
	if (self->fh && self->close_when_done)
	{
		fclose(self->fh);
	}
	if (self->free_when_done)
	{
		PLM_FREE(self->bytes);
	}
	PLM_FREE(self);
}

size_t plm_buffer_get_size(plm_buffer_t *self)
{
// printf("plm_buffer_get_size\n");
	return (self->mode == PLM_BUFFER_MODE_FILE)
						 ? self->total_size
						 : self->length;
}

size_t plm_buffer_get_remaining(plm_buffer_t *self)
{
// printf("plm_buffer_get_remaining\n");
	return self->length - (self->bit_index >> 3);
}

size_t plm_buffer_write(plm_buffer_t *self, uint8_t *bytes, size_t length)
{
// printf("plm_buffer_write\n");
	if (self->mode == PLM_BUFFER_MODE_FIXED_MEM)
	{
		return 0;
	}

	if (self->discard_read_bytes)
	{
		// This should be a ring buffer, but instead it just shifts all unread
		// data to the beginning of the buffer and appends new data at the end.
		// Seems to be good enough.

		plm_buffer_discard_read_bytes(self);
		if (self->mode == PLM_BUFFER_MODE_RING)
		{
			self->total_size = 0;
		}
	}

	// Do we have to resize to fit the new data?
	size_t bytes_available = self->capacity - self->length;
	if (bytes_available < length)
	{
		size_t new_size = self->capacity;
		do
		{
			new_size *= 2;
		} while (new_size - self->length < length);
		self->bytes = (uint8_t *)PLM_REALLOC(self->bytes, new_size);
		self->capacity = new_size;
	}

	memcpy(self->bytes + self->length, bytes, length);
	self->length += length;
	self->has_ended = FALSE;
	return length;
}

void plm_buffer_signal_end(plm_buffer_t *self)
{
// printf("plm_buffer_signal_end\n");
	self->total_size = self->length;
}

void plm_buffer_set_load_callback(plm_buffer_t *self, plm_buffer_load_callback fp, void *user)
{
// printf("plm_buffer_set_load_callback\n");
	self->load_callback = fp;
	self->load_callback_user_data = user;
}

void plm_buffer_rewind(plm_buffer_t *self)
{
// printf("plm_buffer_rewind\n");
	plm_buffer_seek(self, 0);
}

void plm_buffer_seek(plm_buffer_t *self, size_t pos)
{
// printf("plm_buffer_seek\n");
	self->has_ended = FALSE;

	if (self->mode == PLM_BUFFER_MODE_FILE)
	{
		fseek(self->fh, pos, SEEK_SET);
		self->bit_index = 0;
		self->length = 0;
	}
	else if (self->mode == PLM_BUFFER_MODE_RING)
	{
		if (pos != 0)
		{
			// Seeking to non-0 is forbidden for dynamic-mem buffers
			return;
		}
		self->bit_index = 0;
		self->length = 0;
		self->total_size = 0;
	}
	else if (pos < self->length)
	{
		self->bit_index = pos << 3;
	}
}

size_t plm_buffer_tell(plm_buffer_t *self)
{
// printf("plm_buffer_tell\n");
	return self->mode == PLM_BUFFER_MODE_FILE
						 ? ftell(self->fh) + (self->bit_index >> 3) - self->length
						 : self->bit_index >> 3;
}

void plm_buffer_discard_read_bytes(plm_buffer_t *self)
{
// printf("plm_buffer_discard_read_bytes\n");
	size_t byte_pos = self->bit_index >> 3;
	if (byte_pos == self->length)
	{
		self->bit_index = 0;
		self->length = 0;
	}
	else if (byte_pos > 0)
	{
		memmove(self->bytes, self->bytes + byte_pos, self->length - byte_pos);
		self->bit_index -= byte_pos << 3;
		self->length -= byte_pos;
	}
}

void plm_buffer_load_file_callback(plm_buffer_t *self, void *user)
{
// printf("plm_buffer_load_file_callback\n");
	PLM_UNUSED(user);

	if (self->discard_read_bytes)
	{
		plm_buffer_discard_read_bytes(self);
	}

	size_t bytes_available = self->capacity - self->length;
	size_t bytes_read = fread(self->bytes + self->length, 1, bytes_available, self->fh);
	self->length += bytes_read;

	if (bytes_read == 0)
	{
		self->has_ended = TRUE;
	}
}

int plm_buffer_has_ended(plm_buffer_t *self)
{
// printf("plm_buffer_has_ended\n");
	return self->has_ended;
}

int plm_buffer_has(plm_buffer_t *self, size_t count)
{
// // printf("plm_buffer_has\n");
	if (((self->length << 3) - self->bit_index) >= count)
	{
		return TRUE;
	}

	if (self->load_callback)
	{
		self->load_callback(self, self->load_callback_user_data);

		if (((self->length << 3) - self->bit_index) >= count)
		{
			return TRUE;
		}
	}

	if (self->total_size != 0 && self->length == self->total_size)
	{
		self->has_ended = TRUE;
	}
	return FALSE;
}

int plm_buffer_read(plm_buffer_t *self, int count)
{
// // printf("plm_buffer_read\n");
	if (!plm_buffer_has(self, count))
	{
		return 0;
	}

	int value = 0;
	while (count)
	{
		int current_byte = self->bytes[self->bit_index >> 3];

		int remaining = 8 - (self->bit_index & 7);				// Remaining bits in byte
		int read = remaining < count ? remaining : count; // Bits in self run
		int shift = remaining - read;
		int mask = (0xff >> (8 - read));

		value = (value << read) | ((current_byte & (mask << shift)) >> shift);

		self->bit_index += read;
		count -= read;
	}

	return value;
}

void plm_buffer_align(plm_buffer_t *self)
{
// printf("plm_buffer_align\n");
	if (self->bit_index & 0b111)
	{
		self->bit_index = ((self->bit_index + 7) >> 3) << 3; // Align to next byte
	}
}

void plm_buffer_skip(plm_buffer_t *self, size_t count)
{
// printf("plm_buffer_skip(%d)\n", count);
	if (plm_buffer_has(self, count))
	{
		self->bit_index += count;
	}
}

int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v)
{
	plm_buffer_align(self);
	if ((((self->length << 3) - self->bit_index) >= 8) && (self->bytes[self->bit_index >> 3] != v))
	{
// printf("plm_buffer_skip_bytes(%d) = 0\n", v);
		return 0;
	}
	else
	{
		int skipped = 0;
		while (plm_buffer_has(self, 8) && self->bytes[self->bit_index >> 3] == v)
		{
			self->bit_index += 8;
			skipped++;
		}
// printf("plm_buffer_skip_bytes(%d) = %d\n", v, skipped);
		return skipped;
	}
}

int plm_buffer_next_start_code(plm_buffer_t *self)
{
// printf("plm_buffer_next_start_code\n");
	plm_buffer_align(self);

	while (plm_buffer_has(self, (5 << 3)))
	{
		size_t byte_index = self->bit_index >> 3;
		if (
				self->bytes[byte_index] == 0x00 &&
				self->bytes[byte_index + 1] == 0x00 &&
				self->bytes[byte_index + 2] == 0x01)
		{
			self->bit_index = (byte_index + 4) << 3;
			return self->bytes[byte_index + 3];
		}
		self->bit_index += 8;
	}
	return -1;
}

int plm_buffer_find_start_code(plm_buffer_t *self, int code)
{
// printf("plm_buffer_find_start_code\n");
	int current = 0;
	while (TRUE)
	{
		current = plm_buffer_next_start_code(self);
		if (current == code || current == -1)
		{
			return current;
		}
	}
	return -1;
}

int plm_buffer_has_start_code(plm_buffer_t *self, int code)
{
// printf("plm_buffer_has_start_code\n");
	size_t previous_bit_index = self->bit_index;
	int previous_discard_read_bytes = self->discard_read_bytes;

	self->discard_read_bytes = FALSE;
	int current = plm_buffer_find_start_code(self, code);

	self->bit_index = previous_bit_index;
	self->discard_read_bytes = previous_discard_read_bytes;
	return current;
}

int plm_buffer_peek_non_zero(plm_buffer_t *self, int bit_count)
{
// printf("plm_buffer_peek_non_zero\n");
	if (!plm_buffer_has(self, bit_count))
	{
		return FALSE;
	}

	int val = plm_buffer_read(self, bit_count);
	self->bit_index -= bit_count;
	return val != 0;
}

int16_t plm_buffer_read_vlc(plm_buffer_t *self, const plm_vlc_t *table)
{
// printf("plm_buffer_read_vlc\n");
	plm_vlc_t state = {0, 0};
	do
	{
		state = table[state.index + plm_buffer_read(self, 1)];
	} while (state.index > 0);
	return state.value;
}

uint16_t plm_buffer_read_vlc_uint(plm_buffer_t *self, const plm_vlc_uint_t *table)
{
// printf("plm_buffer_read_vlc_uint\n");
	return (uint16_t)plm_buffer_read_vlc(self, (const plm_vlc_t *)table);
}

// ----------------------------------------------------------------------------
// plm_demux implementation

#define PLM_START_PACK 0xBA
#define PLM_START_END 0xB9
#define PLM_START_SYSTEM 0xBB

struct plm_demux_t
{
	plm_buffer_t *buffer;
	int destroy_buffer_when_done;
	double system_clock_ref;

	size_t last_file_size;
	double last_decoded_pts;
	double start_time;
	double duration;

	int start_code;
	int has_pack_header;
	int has_system_header;
	int has_headers;

	int num_audio_streams;
	int num_video_streams;
	plm_packet_t current_packet;
	plm_packet_t next_packet;
};

void plm_demux_buffer_seek(plm_demux_t *self, size_t pos);
double plm_demux_decode_time(plm_demux_t *self);
plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type);
plm_packet_t *plm_demux_get_packet(plm_demux_t *self);

plm_demux_t *plm_demux_create(plm_buffer_t *buffer, int destroy_when_done)
{
// printf("plm_demux_create\n");
	plm_demux_t *self = (plm_demux_t *)PLM_MALLOC(sizeof(plm_demux_t));
	memset(self, 0, sizeof(plm_demux_t));

	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	self->start_time = PLM_PACKET_INVALID_TS;
	self->duration = PLM_PACKET_INVALID_TS;
	self->start_code = -1;

	plm_demux_has_headers(self);
	return self;
}

void plm_demux_destroy(plm_demux_t *self)
{
// printf("plm_demux_destroy\n");
	if (self->destroy_buffer_when_done)
	{
		plm_buffer_destroy(self->buffer);
	}
	PLM_FREE(self);
}

int plm_demux_has_headers(plm_demux_t *self)
{
// printf("plm_demux_has_headers\n");
	if (self->has_headers)
	{
		return TRUE;
	}

	// Decode pack header
	if (!self->has_pack_header)
	{
		if (
				self->start_code != PLM_START_PACK &&
				plm_buffer_find_start_code(self->buffer, PLM_START_PACK) == -1)
		{
			return FALSE;
		}

		self->start_code = PLM_START_PACK;
		if (!plm_buffer_has(self->buffer, 64))
		{
			return FALSE;
		}
		self->start_code = -1;

		if (plm_buffer_read(self->buffer, 4) != 0x02)
		{
			return FALSE;
		}

		self->system_clock_ref = plm_demux_decode_time(self);
		plm_buffer_skip(self->buffer, 1);
		plm_buffer_skip(self->buffer, 22); // mux_rate * 50
		plm_buffer_skip(self->buffer, 1);

		self->has_pack_header = TRUE;
	}

	// Decode system header
	if (!self->has_system_header)
	{
		if (
				self->start_code != PLM_START_SYSTEM &&
				plm_buffer_find_start_code(self->buffer, PLM_START_SYSTEM) == -1)
		{
			return FALSE;
		}

		self->start_code = PLM_START_SYSTEM;
		if (!plm_buffer_has(self->buffer, 56))
		{
			return FALSE;
		}
		self->start_code = -1;

		plm_buffer_skip(self->buffer, 16); // header_length
		plm_buffer_skip(self->buffer, 24); // rate bound
		self->num_audio_streams = plm_buffer_read(self->buffer, 6);
		plm_buffer_skip(self->buffer, 5); // misc flags
		self->num_video_streams = plm_buffer_read(self->buffer, 5);

		self->has_system_header = TRUE;
	}

	self->has_headers = TRUE;
	return TRUE;
}

int plm_demux_probe(plm_demux_t *self, size_t probesize)
{
// printf("plm_demux_probe\n");
	int previous_pos = plm_buffer_tell(self->buffer);

	int video_stream = FALSE;
	int audio_streams[4] = {FALSE, FALSE, FALSE, FALSE};
	do
	{
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (self->start_code == PLM_DEMUX_PACKET_VIDEO_1)
		{
			video_stream = TRUE;
		}
		else if (
				self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 &&
				self->start_code <= PLM_DEMUX_PACKET_AUDIO_4)
		{
			audio_streams[self->start_code - PLM_DEMUX_PACKET_AUDIO_1] = TRUE;
		}
	} while (
			self->start_code != -1 &&
			plm_buffer_tell(self->buffer) - previous_pos < probesize);

	self->num_video_streams = video_stream ? 1 : 0;
	self->num_audio_streams = 0;
	for (int i = 0; i < 4; i++)
	{
		if (audio_streams[i])
		{
			self->num_audio_streams++;
		}
	}

	plm_demux_buffer_seek(self, previous_pos);
	return (self->num_video_streams || self->num_audio_streams);
}

int plm_demux_get_num_video_streams(plm_demux_t *self)
{
// printf("plm_demux_get_num_video_streams\n");
	return plm_demux_has_headers(self)
						 ? self->num_video_streams
						 : 0;
}

int plm_demux_get_num_audio_streams(plm_demux_t *self)
{
// printf("plm_demux_get_num_audio_streams\n");
	return plm_demux_has_headers(self)
						 ? self->num_audio_streams
						 : 0;
}

void plm_demux_rewind(plm_demux_t *self)
{
// printf("plm_demux_rewind\n");
	plm_buffer_rewind(self->buffer);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

int plm_demux_has_ended(plm_demux_t *self)
{
// printf("plm_demux_has_ended\n");
	return plm_buffer_has_ended(self->buffer);
}

void plm_demux_buffer_seek(plm_demux_t *self, size_t pos)
{
// printf("plm_demux_buffer_seek\n");
	plm_buffer_seek(self->buffer, pos);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

double plm_demux_get_start_time(plm_demux_t *self, int type)
{
// printf("plm_demux_get_start_time\n");
	if (self->start_time != PLM_PACKET_INVALID_TS)
	{
		return self->start_time;
	}

	int previous_pos = plm_buffer_tell(self->buffer);
	int previous_start_code = self->start_code;

	// Find first video PTS
	plm_demux_rewind(self);
	do
	{
		plm_packet_t *packet = plm_demux_decode(self);
		if (!packet)
		{
			break;
		}
		if (packet->type == type)
		{
			self->start_time = packet->pts;
		}
	} while (self->start_time == PLM_PACKET_INVALID_TS);

	plm_demux_buffer_seek(self, previous_pos);
	self->start_code = previous_start_code;
	return self->start_time;
}

double plm_demux_get_duration(plm_demux_t *self, int type)
{
// printf("plm_demux_get_duration\n");
	size_t file_size = plm_buffer_get_size(self->buffer);

	if (
			self->duration != PLM_PACKET_INVALID_TS &&
			self->last_file_size == file_size)
	{
		return self->duration;
	}

	size_t previous_pos = plm_buffer_tell(self->buffer);
	int previous_start_code = self->start_code;

	// Find last video PTS. Start searching 64kb from the end and go further
	// back if needed.
	long start_range = 64 * 1024;
	long max_range = 4096 * 1024;
	for (long range = start_range; range <= max_range; range *= 2)
	{
		long seek_pos = file_size - range;
		if (seek_pos < 0)
		{
			seek_pos = 0;
			range = max_range; // Make sure to bail after this round
		}
		plm_demux_buffer_seek(self, seek_pos);
		self->current_packet.length = 0;

		double last_pts = PLM_PACKET_INVALID_TS;
		plm_packet_t *packet = NULL;
		while ((packet = plm_demux_decode(self)))
		{
			if (packet->pts != PLM_PACKET_INVALID_TS && packet->type == type)
			{
				last_pts = packet->pts;
			}
		}
		if (last_pts != PLM_PACKET_INVALID_TS)
		{
			self->duration = last_pts - plm_demux_get_start_time(self, type);
			break;
		}
	}

	plm_demux_buffer_seek(self, previous_pos);
	self->start_code = previous_start_code;
	self->last_file_size = file_size;
	return self->duration;
}

plm_packet_t *plm_demux_seek(plm_demux_t *self, double seek_time, int type, int force_intra)
{
// printf("plm_demux_seek\n");
	if (!plm_demux_has_headers(self))
	{
		return NULL;
	}

	// Using the current time, current byte position and the average bytes per
	// second for this file, try to jump to a byte position that hopefully has
	// packets containing timestamps within one second before to the desired
	// seek_time.

	// If we hit close to the seek_time scan through all packets to find the
	// last one (just before the seek_time) containing an intra frame.
	// Otherwise we should at least be closer than before. Calculate the bytes
	// per second for the jumped range and jump again.

	// The number of retries here is hard-limited to a generous amount. Usually
	// the correct range is found after 1--5 jumps, even for files with very
	// variable bitrates. If significantly more jumps are needed, there's
	// probably something wrong with the file and we just avoid getting into an
	// infinite loop. 32 retries should be enough for anybody.

	double duration = plm_demux_get_duration(self, type);
	long file_size = plm_buffer_get_size(self->buffer);
	long byterate = file_size / duration;

	double cur_time = self->last_decoded_pts;
	double scan_span = 1;

	if (seek_time > duration)
	{
		seek_time = duration;
	}
	else if (seek_time < 0)
	{
		seek_time = 0;
	}
	seek_time += self->start_time;

	for (int retry = 0; retry < 32; retry++)
	{
		int found_packet_with_pts = FALSE;
		int found_packet_in_range = FALSE;
		long last_valid_packet_start = -1;
		double first_packet_time = PLM_PACKET_INVALID_TS;

		long cur_pos = plm_buffer_tell(self->buffer);

		// Estimate byte offset and jump to it.
		long offset = (seek_time - cur_time - scan_span) * byterate;
		long seek_pos = cur_pos + offset;
		if (seek_pos < 0)
		{
			seek_pos = 0;
		}
		else if (seek_pos > file_size - 256)
		{
			seek_pos = file_size - 256;
		}

		plm_demux_buffer_seek(self, seek_pos);

		// Scan through all packets up to the seek_time to find the last packet
		// containing an intra frame.
		while (plm_buffer_find_start_code(self->buffer, type) != -1)
		{
			long packet_start = plm_buffer_tell(self->buffer);
			plm_packet_t *packet = plm_demux_decode_packet(self, type);

			// Skip packet if it has no PTS
			if (!packet || packet->pts == PLM_PACKET_INVALID_TS)
			{
				continue;
			}

			// Bail scanning through packets if we hit one that is outside
			// seek_time - scan_span.
			// We also adjust the cur_time and byterate values here so the next
			// iteration can be a bit more precise.
			if (packet->pts > seek_time || packet->pts < seek_time - scan_span)
			{
				found_packet_with_pts = TRUE;
				byterate = (seek_pos - cur_pos) / (packet->pts - cur_time);
				cur_time = packet->pts;
				break;
			}

			// If we are still here, it means this packet is in close range to
			// the seek_time. If this is the first packet for this jump position
			// record the PTS. If we later have to back off, when there was no
			// intra frame in this range, we can lower the seek_time to not scan
			// this range again.
			if (!found_packet_in_range)
			{
				found_packet_in_range = TRUE;
				first_packet_time = packet->pts;
			}

			// Check if this is an intra frame packet. If so, record the buffer
			// position of the start of this packet. We want to jump back to it
			// later, when we know it's the last intra frame before desired
			// seek time.
			if (force_intra)
			{
				for (size_t i = 0; i < packet->length - 6; i++)
				{
					// Find the START_PICTURE code
					if (
							packet->data[i] == 0x00 &&
							packet->data[i + 1] == 0x00 &&
							packet->data[i + 2] == 0x01 &&
							packet->data[i + 3] == 0x00)
					{
						// Bits 11--13 in the picture header contain the frame
						// type, where 1=Intra
						if ((packet->data[i + 5] & 0x38) == 8)
						{
							last_valid_packet_start = packet_start;
						}
						break;
					}
				}
			}

			// If we don't want intra frames, just use the last PTS found.
			else
			{
				last_valid_packet_start = packet_start;
			}
		}

		// If there was at least one intra frame in the range scanned above,
		// our search is over. Jump back to the packet and decode it again.
		if (last_valid_packet_start != -1)
		{
			plm_demux_buffer_seek(self, last_valid_packet_start);
			return plm_demux_decode_packet(self, type);
		}

		// If we hit the right range, but still found no intra frame, we have
		// to increases the scan_span. This is done exponentially to also handle
		// video files with very few intra frames.
		else if (found_packet_in_range)
		{
			scan_span *= 2;
			seek_time = first_packet_time;
		}

		// If we didn't find any packet with a PTS, it probably means we reached
		// the end of the file. Estimate byterate and cur_time accordingly.
		else if (!found_packet_with_pts)
		{
			byterate = (seek_pos - cur_pos) / (duration - cur_time);
			cur_time = duration;
		}
	}

	return NULL;
}

plm_packet_t *plm_demux_decode(plm_demux_t *self)
{
// printf("plm_demux_decode\n");
	if (!plm_demux_has_headers(self))
	{
		return NULL;
	}

	if (self->current_packet.length)
	{
		size_t bits_till_next_packet = self->current_packet.length << 3;
		if (!plm_buffer_has(self->buffer, bits_till_next_packet))
		{
			return NULL;
		}
		plm_buffer_skip(self->buffer, bits_till_next_packet);
		self->current_packet.length = 0;
	}

	// Pending packet waiting for data?
	if (self->next_packet.length)
	{
		return plm_demux_get_packet(self);
	}

	// Pending packet waiting for header?
	if (self->start_code != -1)
	{
		return plm_demux_decode_packet(self, self->start_code);
	}

	do
	{
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (
				self->start_code == PLM_DEMUX_PACKET_VIDEO_1 ||
				self->start_code == PLM_DEMUX_PACKET_PRIVATE || (self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 && self->start_code <= PLM_DEMUX_PACKET_AUDIO_4))
		{
			return plm_demux_decode_packet(self, self->start_code);
		}
	} while (self->start_code != -1);

	return NULL;
}

double plm_demux_decode_time(plm_demux_t *self)
{
// printf("plm_demux_decode_time\n");
	int64_t clock = plm_buffer_read(self->buffer, 3) << 30;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15) << 15;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15);
	plm_buffer_skip(self->buffer, 1);
	return (double)clock / 90000.0;
}

plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type)
{
// printf("plm_demux_decode_packet\n");
	if (!plm_buffer_has(self->buffer, 16 << 3))
	{
		return NULL;
	}

	self->start_code = -1;

	self->next_packet.type = type;
	self->next_packet.length = plm_buffer_read(self->buffer, 16);
	self->next_packet.length -= plm_buffer_skip_bytes(self->buffer, 0xff); // stuffing

	// skip P-STD
	if (plm_buffer_read(self->buffer, 2) == 0x01)
	{
		plm_buffer_skip(self->buffer, 16);
		self->next_packet.length -= 2;
	}

	int pts_dts_marker = plm_buffer_read(self->buffer, 2);
	if (pts_dts_marker == 0x03)
	{
		self->next_packet.pts = plm_demux_decode_time(self);
		self->last_decoded_pts = self->next_packet.pts;
		plm_buffer_skip(self->buffer, 40); // skip dts
		self->next_packet.length -= 10;
	}
	else if (pts_dts_marker == 0x02)
	{
		self->next_packet.pts = plm_demux_decode_time(self);
		self->last_decoded_pts = self->next_packet.pts;
		self->next_packet.length -= 5;
	}
	else if (pts_dts_marker == 0x00)
	{
		self->next_packet.pts = PLM_PACKET_INVALID_TS;
		plm_buffer_skip(self->buffer, 4);
		self->next_packet.length -= 1;
	}
	else
	{
		return NULL; // invalid
	}

	return plm_demux_get_packet(self);
}

plm_packet_t *plm_demux_get_packet(plm_demux_t *self)
{
// printf("plm_demux_get_packet\n");
	if (!plm_buffer_has(self->buffer, self->next_packet.length << 3))
	{
		return NULL;
	}

	self->current_packet.data = self->buffer->bytes + (self->buffer->bit_index >> 3);
	self->current_packet.length = self->next_packet.length;
	self->current_packet.type = self->next_packet.type;
	self->current_packet.pts = self->next_packet.pts;

	self->next_packet.length = 0;
	return &self->current_packet;
}

// -----------------------------------------------------------------------------
// plm_video implementation

// Inspired by Java MPEG-1 Video Decoder and Player by Zoltan Korandi
// https://sourceforge.net/projects/javampeg1video/

#define PLM_VIDEO_PICTURE_TYPE_INTRA 1
#define PLM_VIDEO_PICTURE_TYPE_PREDICTIVE 2
#define PLM_VIDEO_PICTURE_TYPE_B 3

#define PLM_START_SEQUENCE 0xB3
#define PLM_START_SLICE_FIRST 0x01
#define PLM_START_SLICE_LAST 0xAF
#define PLM_START_PICTURE 0x00
#define PLM_START_EXTENSION 0xB5
#define PLM_START_USER_DATA 0xB2

#define PLM_START_IS_SLICE(c) \
	(c >= PLM_START_SLICE_FIRST && c <= PLM_START_SLICE_LAST)

static const double PLM_VIDEO_PICTURE_RATE[] = {
		0.000, 23.976, 24.000, 25.000, 29.970, 30.000, 50.000, 59.940,
		60.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000};

static const uint8_t PLM_VIDEO_ZIG_ZAG[] = {
		0, 1, 8, 16, 9, 2, 3, 10,
		17, 24, 32, 25, 18, 11, 4, 5,
		12, 19, 26, 33, 40, 48, 41, 34,
		27, 20, 13, 6, 7, 14, 21, 28,
		35, 42, 49, 56, 57, 50, 43, 36,
		29, 22, 15, 23, 30, 37, 44, 51,
		58, 59, 52, 45, 38, 31, 39, 46,
		53, 60, 61, 54, 47, 55, 62, 63};

static const uint8_t PLM_VIDEO_INTRA_QUANT_MATRIX[] = {
		8, 16, 19, 22, 26, 27, 29, 34,
		16, 16, 22, 24, 27, 29, 34, 37,
		19, 22, 26, 27, 29, 34, 34, 38,
		22, 22, 26, 27, 29, 34, 37, 40,
		22, 26, 27, 29, 32, 35, 40, 48,
		26, 27, 29, 32, 35, 40, 48, 58,
		26, 27, 29, 34, 38, 46, 56, 69,
		27, 29, 35, 38, 46, 56, 69, 83};

static const uint8_t PLM_VIDEO_NON_INTRA_QUANT_MATRIX[] = {
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16,
		16, 16, 16, 16, 16, 16, 16, 16};

static const uint8_t PLM_VIDEO_PREMULTIPLIER_MATRIX[] = {
		32, 44, 42, 38, 32, 25, 17, 9,
		44, 62, 58, 52, 44, 35, 24, 12,
		42, 58, 55, 49, 42, 33, 23, 12,
		38, 52, 49, 44, 38, 30, 20, 10,
		32, 44, 42, 38, 32, 25, 17, 9,
		25, 35, 33, 30, 25, 20, 14, 7,
		17, 24, 23, 20, 17, 14, 9, 5,
		9, 12, 12, 10, 9, 7, 5, 2};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT[] = {
		{1 << 1, 0}, {0, 1}, //   0: x
		{2 << 1, 0},
		{3 << 1, 0}, //   1: 0x
		{4 << 1, 0},
		{5 << 1, 0}, //   2: 00x
		{0, 3},
		{0, 2}, //   3: 01x
		{6 << 1, 0},
		{7 << 1, 0}, //   4: 000x
		{0, 5},
		{0, 4}, //   5: 001x
		{8 << 1, 0},
		{9 << 1, 0}, //   6: 0000x
		{0, 7},
		{0, 6}, //   7: 0001x
		{10 << 1, 0},
		{11 << 1, 0}, //   8: 0000 0x
		{12 << 1, 0},
		{13 << 1, 0}, //   9: 0000 1x
		{14 << 1, 0},
		{15 << 1, 0}, //  10: 0000 00x
		{16 << 1, 0},
		{17 << 1, 0}, //  11: 0000 01x
		{18 << 1, 0},
		{19 << 1, 0}, //  12: 0000 10x
		{0, 9},
		{0, 8}, //  13: 0000 11x
		{-1, 0},
		{20 << 1, 0}, //  14: 0000 000x
		{-1, 0},
		{21 << 1, 0}, //  15: 0000 001x
		{22 << 1, 0},
		{23 << 1, 0}, //  16: 0000 010x
		{0, 15},
		{0, 14}, //  17: 0000 011x
		{0, 13},
		{0, 12}, //  18: 0000 100x
		{0, 11},
		{0, 10}, //  19: 0000 101x
		{24 << 1, 0},
		{25 << 1, 0}, //  20: 0000 0001x
		{26 << 1, 0},
		{27 << 1, 0}, //  21: 0000 0011x
		{28 << 1, 0},
		{29 << 1, 0}, //  22: 0000 0100x
		{30 << 1, 0},
		{31 << 1, 0}, //  23: 0000 0101x
		{32 << 1, 0},
		{-1, 0}, //  24: 0000 0001 0x
		{-1, 0},
		{33 << 1, 0}, //  25: 0000 0001 1x
		{34 << 1, 0},
		{35 << 1, 0}, //  26: 0000 0011 0x
		{36 << 1, 0},
		{37 << 1, 0}, //  27: 0000 0011 1x
		{38 << 1, 0},
		{39 << 1, 0}, //  28: 0000 0100 0x
		{0, 21},
		{0, 20}, //  29: 0000 0100 1x
		{0, 19},
		{0, 18}, //  30: 0000 0101 0x
		{0, 17},
		{0, 16}, //  31: 0000 0101 1x
		{0, 35},
		{-1, 0}, //  32: 0000 0001 00x
		{-1, 0},
		{0, 34}, //  33: 0000 0001 11x
		{0, 33},
		{0, 32}, //  34: 0000 0011 00x
		{0, 31},
		{0, 30}, //  35: 0000 0011 01x
		{0, 29},
		{0, 28}, //  36: 0000 0011 10x
		{0, 27},
		{0, 26}, //  37: 0000 0011 11x
		{0, 25},
		{0, 24}, //  38: 0000 0100 00x
		{0, 23},
		{0, 22}, //  39: 0000 0100 01x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_INTRA[] = {
		{1 << 1, 0}, {0, 0x01}, //   0: x
		{-1, 0},
		{0, 0x11}, //   1: 0x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE[] = {
		{1 << 1, 0}, {0, 0x0a}, //   0: x
		{2 << 1, 0},
		{0, 0x02}, //   1: 0x
		{3 << 1, 0},
		{0, 0x08}, //   2: 00x
		{4 << 1, 0},
		{5 << 1, 0}, //   3: 000x
		{6 << 1, 0},
		{0, 0x12}, //   4: 0000x
		{0, 0x1a},
		{0, 0x01}, //   5: 0001x
		{-1, 0},
		{0, 0x11}, //   6: 0000 0x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_B[] = {
		{1 << 1, 0}, {2 << 1, 0}, //   0: x
		{3 << 1, 0},
		{4 << 1, 0}, //   1: 0x
		{0, 0x0c},
		{0, 0x0e}, //   2: 1x
		{5 << 1, 0},
		{6 << 1, 0}, //   3: 00x
		{0, 0x04},
		{0, 0x06}, //   4: 01x
		{7 << 1, 0},
		{8 << 1, 0}, //   5: 000x
		{0, 0x08},
		{0, 0x0a}, //   6: 001x
		{9 << 1, 0},
		{10 << 1, 0}, //   7: 0000x
		{0, 0x1e},
		{0, 0x01}, //   8: 0001x
		{-1, 0},
		{0, 0x11}, //   9: 0000 0x
		{0, 0x16},
		{0, 0x1a}, //  10: 0000 1x
};

static const plm_vlc_t *PLM_VIDEO_MACROBLOCK_TYPE[] = {
		NULL,
		PLM_VIDEO_MACROBLOCK_TYPE_INTRA,
		PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE,
		PLM_VIDEO_MACROBLOCK_TYPE_B};

static const plm_vlc_t PLM_VIDEO_CODE_BLOCK_PATTERN[] = {
		{1 << 1, 0}, {2 << 1, 0}, //   0: x
		{3 << 1, 0},
		{4 << 1, 0}, //   1: 0x
		{5 << 1, 0},
		{6 << 1, 0}, //   2: 1x
		{7 << 1, 0},
		{8 << 1, 0}, //   3: 00x
		{9 << 1, 0},
		{10 << 1, 0}, //   4: 01x
		{11 << 1, 0},
		{12 << 1, 0}, //   5: 10x
		{13 << 1, 0},
		{0, 60}, //   6: 11x
		{14 << 1, 0},
		{15 << 1, 0}, //   7: 000x
		{16 << 1, 0},
		{17 << 1, 0}, //   8: 001x
		{18 << 1, 0},
		{19 << 1, 0}, //   9: 010x
		{20 << 1, 0},
		{21 << 1, 0}, //  10: 011x
		{22 << 1, 0},
		{23 << 1, 0}, //  11: 100x
		{0, 32},
		{0, 16}, //  12: 101x
		{0, 8},
		{0, 4}, //  13: 110x
		{24 << 1, 0},
		{25 << 1, 0}, //  14: 0000x
		{26 << 1, 0},
		{27 << 1, 0}, //  15: 0001x
		{28 << 1, 0},
		{29 << 1, 0}, //  16: 0010x
		{30 << 1, 0},
		{31 << 1, 0}, //  17: 0011x
		{0, 62},
		{0, 2}, //  18: 0100x
		{0, 61},
		{0, 1}, //  19: 0101x
		{0, 56},
		{0, 52}, //  20: 0110x
		{0, 44},
		{0, 28}, //  21: 0111x
		{0, 40},
		{0, 20}, //  22: 1000x
		{0, 48},
		{0, 12}, //  23: 1001x
		{32 << 1, 0},
		{33 << 1, 0}, //  24: 0000 0x
		{34 << 1, 0},
		{35 << 1, 0}, //  25: 0000 1x
		{36 << 1, 0},
		{37 << 1, 0}, //  26: 0001 0x
		{38 << 1, 0},
		{39 << 1, 0}, //  27: 0001 1x
		{40 << 1, 0},
		{41 << 1, 0}, //  28: 0010 0x
		{42 << 1, 0},
		{43 << 1, 0}, //  29: 0010 1x
		{0, 63},
		{0, 3}, //  30: 0011 0x
		{0, 36},
		{0, 24}, //  31: 0011 1x
		{44 << 1, 0},
		{45 << 1, 0}, //  32: 0000 00x
		{46 << 1, 0},
		{47 << 1, 0}, //  33: 0000 01x
		{48 << 1, 0},
		{49 << 1, 0}, //  34: 0000 10x
		{50 << 1, 0},
		{51 << 1, 0}, //  35: 0000 11x
		{52 << 1, 0},
		{53 << 1, 0}, //  36: 0001 00x
		{54 << 1, 0},
		{55 << 1, 0}, //  37: 0001 01x
		{56 << 1, 0},
		{57 << 1, 0}, //  38: 0001 10x
		{58 << 1, 0},
		{59 << 1, 0}, //  39: 0001 11x
		{0, 34},
		{0, 18}, //  40: 0010 00x
		{0, 10},
		{0, 6}, //  41: 0010 01x
		{0, 33},
		{0, 17}, //  42: 0010 10x
		{0, 9},
		{0, 5}, //  43: 0010 11x
		{-1, 0},
		{60 << 1, 0}, //  44: 0000 000x
		{61 << 1, 0},
		{62 << 1, 0}, //  45: 0000 001x
		{0, 58},
		{0, 54}, //  46: 0000 010x
		{0, 46},
		{0, 30}, //  47: 0000 011x
		{0, 57},
		{0, 53}, //  48: 0000 100x
		{0, 45},
		{0, 29}, //  49: 0000 101x
		{0, 38},
		{0, 26}, //  50: 0000 110x
		{0, 37},
		{0, 25}, //  51: 0000 111x
		{0, 43},
		{0, 23}, //  52: 0001 000x
		{0, 51},
		{0, 15}, //  53: 0001 001x
		{0, 42},
		{0, 22}, //  54: 0001 010x
		{0, 50},
		{0, 14}, //  55: 0001 011x
		{0, 41},
		{0, 21}, //  56: 0001 100x
		{0, 49},
		{0, 13}, //  57: 0001 101x
		{0, 35},
		{0, 19}, //  58: 0001 110x
		{0, 11},
		{0, 7}, //  59: 0001 111x
		{0, 39},
		{0, 27}, //  60: 0000 0001x
		{0, 59},
		{0, 55}, //  61: 0000 0010x
		{0, 47},
		{0, 31}, //  62: 0000 0011x
};

static const plm_vlc_t PLM_VIDEO_MOTION[] = {
		{1 << 1, 0}, {0, 0}, //   0: x
		{2 << 1, 0},
		{3 << 1, 0}, //   1: 0x
		{4 << 1, 0},
		{5 << 1, 0}, //   2: 00x
		{0, 1},
		{0, -1}, //   3: 01x
		{6 << 1, 0},
		{7 << 1, 0}, //   4: 000x
		{0, 2},
		{0, -2}, //   5: 001x
		{8 << 1, 0},
		{9 << 1, 0}, //   6: 0000x
		{0, 3},
		{0, -3}, //   7: 0001x
		{10 << 1, 0},
		{11 << 1, 0}, //   8: 0000 0x
		{12 << 1, 0},
		{13 << 1, 0}, //   9: 0000 1x
		{-1, 0},
		{14 << 1, 0}, //  10: 0000 00x
		{15 << 1, 0},
		{16 << 1, 0}, //  11: 0000 01x
		{17 << 1, 0},
		{18 << 1, 0}, //  12: 0000 10x
		{0, 4},
		{0, -4}, //  13: 0000 11x
		{-1, 0},
		{19 << 1, 0}, //  14: 0000 001x
		{20 << 1, 0},
		{21 << 1, 0}, //  15: 0000 010x
		{0, 7},
		{0, -7}, //  16: 0000 011x
		{0, 6},
		{0, -6}, //  17: 0000 100x
		{0, 5},
		{0, -5}, //  18: 0000 101x
		{22 << 1, 0},
		{23 << 1, 0}, //  19: 0000 0011x
		{24 << 1, 0},
		{25 << 1, 0}, //  20: 0000 0100x
		{26 << 1, 0},
		{27 << 1, 0}, //  21: 0000 0101x
		{28 << 1, 0},
		{29 << 1, 0}, //  22: 0000 0011 0x
		{30 << 1, 0},
		{31 << 1, 0}, //  23: 0000 0011 1x
		{32 << 1, 0},
		{33 << 1, 0}, //  24: 0000 0100 0x
		{0, 10},
		{0, -10}, //  25: 0000 0100 1x
		{0, 9},
		{0, -9}, //  26: 0000 0101 0x
		{0, 8},
		{0, -8}, //  27: 0000 0101 1x
		{0, 16},
		{0, -16}, //  28: 0000 0011 00x
		{0, 15},
		{0, -15}, //  29: 0000 0011 01x
		{0, 14},
		{0, -14}, //  30: 0000 0011 10x
		{0, 13},
		{0, -13}, //  31: 0000 0011 11x
		{0, 12},
		{0, -12}, //  32: 0000 0100 00x
		{0, 11},
		{0, -11}, //  33: 0000 0100 01x
};

static const plm_vlc_t PLM_VIDEO_DCT_SIZE_LUMINANCE[] = {
		{1 << 1, 0}, {2 << 1, 0}, //   0: x
		{0, 1},
		{0, 2}, //   1: 0x
		{3 << 1, 0},
		{4 << 1, 0}, //   2: 1x
		{0, 0},
		{0, 3}, //   3: 10x
		{0, 4},
		{5 << 1, 0}, //   4: 11x
		{0, 5},
		{6 << 1, 0}, //   5: 111x
		{0, 6},
		{7 << 1, 0}, //   6: 1111x
		{0, 7},
		{8 << 1, 0}, //   7: 1111 1x
		{0, 8},
		{-1, 0}, //   8: 1111 11x
};

static const plm_vlc_t PLM_VIDEO_DCT_SIZE_CHROMINANCE[] = {
		{1 << 1, 0}, {2 << 1, 0}, //   0: x
		{0, 0},
		{0, 1}, //   1: 0x
		{0, 2},
		{3 << 1, 0}, //   2: 1x
		{0, 3},
		{4 << 1, 0}, //   3: 11x
		{0, 4},
		{5 << 1, 0}, //   4: 111x
		{0, 5},
		{6 << 1, 0}, //   5: 1111x
		{0, 6},
		{7 << 1, 0}, //   6: 1111 1x
		{0, 7},
		{8 << 1, 0}, //   7: 1111 11x
		{0, 8},
		{-1, 0}, //   8: 1111 111x
};

static const plm_vlc_t *PLM_VIDEO_DCT_SIZE[] = {
		PLM_VIDEO_DCT_SIZE_LUMINANCE,
		PLM_VIDEO_DCT_SIZE_CHROMINANCE,
		PLM_VIDEO_DCT_SIZE_CHROMINANCE};

//  dct_coeff bitmap:
//    0xff00  run
//    0x00ff  level

//  Decoded values are unsigned. Sign bit follows in the stream.

static const plm_vlc_uint_t PLM_VIDEO_DCT_COEFF[] = {
		{1 << 1, 0}, {0, 0x0001}, //   0: x
		{2 << 1, 0},
		{3 << 1, 0}, //   1: 0x
		{4 << 1, 0},
		{5 << 1, 0}, //   2: 00x
		{6 << 1, 0},
		{0, 0x0101}, //   3: 01x
		{7 << 1, 0},
		{8 << 1, 0}, //   4: 000x
		{9 << 1, 0},
		{10 << 1, 0}, //   5: 001x
		{0, 0x0002},
		{0, 0x0201}, //   6: 010x
		{11 << 1, 0},
		{12 << 1, 0}, //   7: 0000x
		{13 << 1, 0},
		{14 << 1, 0}, //   8: 0001x
		{15 << 1, 0},
		{0, 0x0003}, //   9: 0010x
		{0, 0x0401},
		{0, 0x0301}, //  10: 0011x
		{16 << 1, 0},
		{0, 0xffff}, //  11: 0000 0x
		{17 << 1, 0},
		{18 << 1, 0}, //  12: 0000 1x
		{0, 0x0701},
		{0, 0x0601}, //  13: 0001 0x
		{0, 0x0102},
		{0, 0x0501}, //  14: 0001 1x
		{19 << 1, 0},
		{20 << 1, 0}, //  15: 0010 0x
		{21 << 1, 0},
		{22 << 1, 0}, //  16: 0000 00x
		{0, 0x0202},
		{0, 0x0901}, //  17: 0000 10x
		{0, 0x0004},
		{0, 0x0801}, //  18: 0000 11x
		{23 << 1, 0},
		{24 << 1, 0}, //  19: 0010 00x
		{25 << 1, 0},
		{26 << 1, 0}, //  20: 0010 01x
		{27 << 1, 0},
		{28 << 1, 0}, //  21: 0000 000x
		{29 << 1, 0},
		{30 << 1, 0}, //  22: 0000 001x
		{0, 0x0d01},
		{0, 0x0006}, //  23: 0010 000x
		{0, 0x0c01},
		{0, 0x0b01}, //  24: 0010 001x
		{0, 0x0302},
		{0, 0x0103}, //  25: 0010 010x
		{0, 0x0005},
		{0, 0x0a01}, //  26: 0010 011x
		{31 << 1, 0},
		{32 << 1, 0}, //  27: 0000 0000x
		{33 << 1, 0},
		{34 << 1, 0}, //  28: 0000 0001x
		{35 << 1, 0},
		{36 << 1, 0}, //  29: 0000 0010x
		{37 << 1, 0},
		{38 << 1, 0}, //  30: 0000 0011x
		{39 << 1, 0},
		{40 << 1, 0}, //  31: 0000 0000 0x
		{41 << 1, 0},
		{42 << 1, 0}, //  32: 0000 0000 1x
		{43 << 1, 0},
		{44 << 1, 0}, //  33: 0000 0001 0x
		{45 << 1, 0},
		{46 << 1, 0}, //  34: 0000 0001 1x
		{0, 0x1001},
		{0, 0x0502}, //  35: 0000 0010 0x
		{0, 0x0007},
		{0, 0x0203}, //  36: 0000 0010 1x
		{0, 0x0104},
		{0, 0x0f01}, //  37: 0000 0011 0x
		{0, 0x0e01},
		{0, 0x0402}, //  38: 0000 0011 1x
		{47 << 1, 0},
		{48 << 1, 0}, //  39: 0000 0000 00x
		{49 << 1, 0},
		{50 << 1, 0}, //  40: 0000 0000 01x
		{51 << 1, 0},
		{52 << 1, 0}, //  41: 0000 0000 10x
		{53 << 1, 0},
		{54 << 1, 0}, //  42: 0000 0000 11x
		{55 << 1, 0},
		{56 << 1, 0}, //  43: 0000 0001 00x
		{57 << 1, 0},
		{58 << 1, 0}, //  44: 0000 0001 01x
		{59 << 1, 0},
		{60 << 1, 0}, //  45: 0000 0001 10x
		{61 << 1, 0},
		{62 << 1, 0}, //  46: 0000 0001 11x
		{-1, 0},
		{63 << 1, 0}, //  47: 0000 0000 000x
		{64 << 1, 0},
		{65 << 1, 0}, //  48: 0000 0000 001x
		{66 << 1, 0},
		{67 << 1, 0}, //  49: 0000 0000 010x
		{68 << 1, 0},
		{69 << 1, 0}, //  50: 0000 0000 011x
		{70 << 1, 0},
		{71 << 1, 0}, //  51: 0000 0000 100x
		{72 << 1, 0},
		{73 << 1, 0}, //  52: 0000 0000 101x
		{74 << 1, 0},
		{75 << 1, 0}, //  53: 0000 0000 110x
		{76 << 1, 0},
		{77 << 1, 0}, //  54: 0000 0000 111x
		{0, 0x000b},
		{0, 0x0802}, //  55: 0000 0001 000x
		{0, 0x0403},
		{0, 0x000a}, //  56: 0000 0001 001x
		{0, 0x0204},
		{0, 0x0702}, //  57: 0000 0001 010x
		{0, 0x1501},
		{0, 0x1401}, //  58: 0000 0001 011x
		{0, 0x0009},
		{0, 0x1301}, //  59: 0000 0001 100x
		{0, 0x1201},
		{0, 0x0105}, //  60: 0000 0001 101x
		{0, 0x0303},
		{0, 0x0008}, //  61: 0000 0001 110x
		{0, 0x0602},
		{0, 0x1101}, //  62: 0000 0001 111x
		{78 << 1, 0},
		{79 << 1, 0}, //  63: 0000 0000 0001x
		{80 << 1, 0},
		{81 << 1, 0}, //  64: 0000 0000 0010x
		{82 << 1, 0},
		{83 << 1, 0}, //  65: 0000 0000 0011x
		{84 << 1, 0},
		{85 << 1, 0}, //  66: 0000 0000 0100x
		{86 << 1, 0},
		{87 << 1, 0}, //  67: 0000 0000 0101x
		{88 << 1, 0},
		{89 << 1, 0}, //  68: 0000 0000 0110x
		{90 << 1, 0},
		{91 << 1, 0}, //  69: 0000 0000 0111x
		{0, 0x0a02},
		{0, 0x0902}, //  70: 0000 0000 1000x
		{0, 0x0503},
		{0, 0x0304}, //  71: 0000 0000 1001x
		{0, 0x0205},
		{0, 0x0107}, //  72: 0000 0000 1010x
		{0, 0x0106},
		{0, 0x000f}, //  73: 0000 0000 1011x
		{0, 0x000e},
		{0, 0x000d}, //  74: 0000 0000 1100x
		{0, 0x000c},
		{0, 0x1a01}, //  75: 0000 0000 1101x
		{0, 0x1901},
		{0, 0x1801}, //  76: 0000 0000 1110x
		{0, 0x1701},
		{0, 0x1601}, //  77: 0000 0000 1111x
		{92 << 1, 0},
		{93 << 1, 0}, //  78: 0000 0000 0001 0x
		{94 << 1, 0},
		{95 << 1, 0}, //  79: 0000 0000 0001 1x
		{96 << 1, 0},
		{97 << 1, 0}, //  80: 0000 0000 0010 0x
		{98 << 1, 0},
		{99 << 1, 0}, //  81: 0000 0000 0010 1x
		{100 << 1, 0},
		{101 << 1, 0}, //  82: 0000 0000 0011 0x
		{102 << 1, 0},
		{103 << 1, 0}, //  83: 0000 0000 0011 1x
		{0, 0x001f},
		{0, 0x001e}, //  84: 0000 0000 0100 0x
		{0, 0x001d},
		{0, 0x001c}, //  85: 0000 0000 0100 1x
		{0, 0x001b},
		{0, 0x001a}, //  86: 0000 0000 0101 0x
		{0, 0x0019},
		{0, 0x0018}, //  87: 0000 0000 0101 1x
		{0, 0x0017},
		{0, 0x0016}, //  88: 0000 0000 0110 0x
		{0, 0x0015},
		{0, 0x0014}, //  89: 0000 0000 0110 1x
		{0, 0x0013},
		{0, 0x0012}, //  90: 0000 0000 0111 0x
		{0, 0x0011},
		{0, 0x0010}, //  91: 0000 0000 0111 1x
		{104 << 1, 0},
		{105 << 1, 0}, //  92: 0000 0000 0001 00x
		{106 << 1, 0},
		{107 << 1, 0}, //  93: 0000 0000 0001 01x
		{108 << 1, 0},
		{109 << 1, 0}, //  94: 0000 0000 0001 10x
		{110 << 1, 0},
		{111 << 1, 0}, //  95: 0000 0000 0001 11x
		{0, 0x0028},
		{0, 0x0027}, //  96: 0000 0000 0010 00x
		{0, 0x0026},
		{0, 0x0025}, //  97: 0000 0000 0010 01x
		{0, 0x0024},
		{0, 0x0023}, //  98: 0000 0000 0010 10x
		{0, 0x0022},
		{0, 0x0021}, //  99: 0000 0000 0010 11x
		{0, 0x0020},
		{0, 0x010e}, // 100: 0000 0000 0011 00x
		{0, 0x010d},
		{0, 0x010c}, // 101: 0000 0000 0011 01x
		{0, 0x010b},
		{0, 0x010a}, // 102: 0000 0000 0011 10x
		{0, 0x0109},
		{0, 0x0108}, // 103: 0000 0000 0011 11x
		{0, 0x0112},
		{0, 0x0111}, // 104: 0000 0000 0001 000x
		{0, 0x0110},
		{0, 0x010f}, // 105: 0000 0000 0001 001x
		{0, 0x0603},
		{0, 0x1002}, // 106: 0000 0000 0001 010x
		{0, 0x0f02},
		{0, 0x0e02}, // 107: 0000 0000 0001 011x
		{0, 0x0d02},
		{0, 0x0c02}, // 108: 0000 0000 0001 100x
		{0, 0x0b02},
		{0, 0x1f01}, // 109: 0000 0000 0001 101x
		{0, 0x1e01},
		{0, 0x1d01}, // 110: 0000 0000 0001 110x
		{0, 0x1c01},
		{0, 0x1b01}, // 111: 0000 0000 0001 111x
};

typedef struct
{
	int full_px;
	int is_set;
	int r_size;
	int h;
	int v;
} plm_video_motion_t;

struct plm_video_t
{
	double framerate;
	double time;
	int frames_decoded;
	int width;
	int height;
	int mb_width;
	int mb_height;
	int mb_size;

	int luma_width;
	int luma_height;

	int chroma_width;
	int chroma_height;

	int start_code;
	int picture_type;

	plm_video_motion_t motion_forward;
	plm_video_motion_t motion_backward;

	int has_sequence_header;

	int quantizer_scale;
	int slice_begin;
	int macroblock_address;

	int mb_row;
	int mb_col;

	int macroblock_type;
	int macroblock_intra;

	int dc_predictor[3];

	plm_buffer_t *buffer;
	int destroy_buffer_when_done;

	plm_frame_t frame_current;
	plm_frame_t frame_forward;
	plm_frame_t frame_backward;

	uint8_t *frames_data;

	int block_data[64];
	uint8_t intra_quant_matrix[64];
	uint8_t non_intra_quant_matrix[64];

	int has_reference_frame;
	int assume_no_b_frames;
};

static inline uint8_t plm_clamp(int n)
{
	if (n > 255)
	{
		n = 255;
	}
	else if (n < 0)
	{
		n = 0;
	}
	return n;
}

int plm_video_decode_sequence_header(plm_video_t *self);
void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base);
void plm_video_decode_picture(plm_video_t *self);
void plm_video_decode_slice(plm_video_t *self, int slice);
void plm_video_decode_macroblock(plm_video_t *self);
void plm_video_decode_motion_vectors(plm_video_t *self);
int plm_video_decode_motion_vector(plm_video_t *self, int r_size, int motion);
void plm_video_predict_macroblock(plm_video_t *self);
void plm_video_copy_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v);
void plm_video_interpolate_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v);
void plm_video_process_macroblock(plm_video_t *self, uint8_t *s, uint8_t *d, int mh, int mb, int bs, int interp);
void plm_video_decode_block(plm_video_t *self, int block);
void plm_video_idct(int *block);

plm_video_t *plm_video_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done)
{
// printf("plm_video_create_with_buffer\n");
	plm_video_t *self = (plm_video_t *)PLM_MALLOC(sizeof(plm_video_t));
	memset(self, 0, sizeof(plm_video_t));

	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	// Attempt to decode the sequence header
	self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	if (self->start_code != -1)
	{
		plm_video_decode_sequence_header(self);
	}
	return self;
}

void plm_video_destroy(plm_video_t *self)
{
// printf("plm_video_destroy\n");
	if (self->destroy_buffer_when_done)
	{
		plm_buffer_destroy(self->buffer);
	}

	if (self->has_sequence_header)
	{
		PLM_FREE(self->frames_data);
	}

	PLM_FREE(self);
}

double plm_video_get_framerate(plm_video_t *self)
{
// printf("plm_video_get_framerate\n");
	return plm_video_has_header(self)
						 ? self->framerate
						 : 0;
}

int plm_video_get_width(plm_video_t *self)
{
// printf("plm_video_get_width\n");
	return plm_video_has_header(self)
						 ? self->width
						 : 0;
}

int plm_video_get_height(plm_video_t *self)
{
// printf("plm_video_get_height\n");
	return plm_video_has_header(self)
						 ? self->height
						 : 0;
}

void plm_video_set_no_delay(plm_video_t *self, int no_delay)
{
// printf("plm_video_set_no_delay\n");
	self->assume_no_b_frames = no_delay;
}

double plm_video_get_time(plm_video_t *self)
{
// printf("plm_video_get_time\n");
	return self->time;
}

void plm_video_set_time(plm_video_t *self, double time)
{
// printf("plm_video_set_time\n");
	self->frames_decoded = self->framerate * time;
	self->time = time;
}

void plm_video_rewind(plm_video_t *self)
{
// printf("plm_video_rewind\n");
	plm_buffer_rewind(self->buffer);
	self->time = 0;
	self->frames_decoded = 0;
	self->has_reference_frame = FALSE;
	self->start_code = -1;
}

int plm_video_has_ended(plm_video_t *self)
{
// printf("plm_video_has_ended\n");
	return plm_buffer_has_ended(self->buffer);
}

plm_frame_t *plm_video_decode(plm_video_t *self)
{
// printf("plm_video_decode\n");
	if (!plm_video_has_header(self))
	{
		return NULL;
	}

	plm_frame_t *frame = NULL;
	do
	{
		if (self->start_code != PLM_START_PICTURE)
		{
			self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_PICTURE);

			if (self->start_code == -1)
			{
				// If we reached the end of the file and the previously decoded
				// frame was a reference frame, we still have to return it.
				if (
						self->has_reference_frame &&
						!self->assume_no_b_frames &&
						plm_buffer_has_ended(self->buffer) && (self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA || self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE))
				{
					self->has_reference_frame = FALSE;
					frame = &self->frame_backward;
					break;
				}

				return NULL;
			}
		}

		// Make sure we have a full picture in the buffer before attempting to
		// decode it. Sadly, this can only be done by seeking for the start code
		// of the next picture. Also, if we didn't find the start code for the
		// next picture, but the source has ended, we assume that this last
		// picture is in the buffer.
		if (
				plm_buffer_has_start_code(self->buffer, PLM_START_PICTURE) == -1 &&
				!plm_buffer_has_ended(self->buffer))
		{
			return NULL;
		}
		plm_buffer_discard_read_bytes(self->buffer);

		plm_video_decode_picture(self);

		if (self->assume_no_b_frames)
		{
			frame = &self->frame_backward;
		}
		else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B)
		{
			frame = &self->frame_current;
		}
		else if (self->has_reference_frame)
		{
			frame = &self->frame_forward;
		}
		else
		{
			self->has_reference_frame = TRUE;
		}
	} while (!frame);

	frame->time = self->time;
	self->frames_decoded++;
	self->time = (double)self->frames_decoded / self->framerate;

	return frame;
}

int plm_video_has_header(plm_video_t *self)
{
// printf("plm_video_has_header\n");
	if (self->has_sequence_header)
	{
		return TRUE;
	}

	if (self->start_code != PLM_START_SEQUENCE)
	{
		self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	}
	if (self->start_code == -1)
	{
		return FALSE;
	}

	if (!plm_video_decode_sequence_header(self))
	{
		return FALSE;
	}

	return TRUE;
}

int plm_video_decode_sequence_header(plm_video_t *self)
{
// printf("plm_video_decode_sequence_header\n");
	int max_header_size = 64 + 2 * 64 * 8; // 64 bit header + 2x 64 byte matrix
	if (!plm_buffer_has(self->buffer, max_header_size))
	{
		return FALSE;
	}

	self->width = plm_buffer_read(self->buffer, 12);
	self->height = plm_buffer_read(self->buffer, 12);

	if (self->width <= 0 || self->height <= 0)
	{
		return FALSE;
	}

	// Skip pixel aspect ratio
	plm_buffer_skip(self->buffer, 4);

	self->framerate = PLM_VIDEO_PICTURE_RATE[plm_buffer_read(self->buffer, 4)];

	// Skip bit_rate, marker, buffer_size and constrained bit
	plm_buffer_skip(self->buffer, 18 + 1 + 10 + 1);

	// Load custom intra quant matrix?
	if (plm_buffer_read(self->buffer, 1))
	{
		for (int i = 0; i < 64; i++)
		{
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else
	{
		memcpy(self->intra_quant_matrix, PLM_VIDEO_INTRA_QUANT_MATRIX, 64);
	}

	// Load custom non intra quant matrix?
	if (plm_buffer_read(self->buffer, 1))
	{
		for (int i = 0; i < 64; i++)
		{
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->non_intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else
	{
		memcpy(self->non_intra_quant_matrix, PLM_VIDEO_NON_INTRA_QUANT_MATRIX, 64);
	}

	self->mb_width = (self->width + 15) >> 4;
	self->mb_height = (self->height + 15) >> 4;
	self->mb_size = self->mb_width * self->mb_height;

	self->luma_width = self->mb_width << 4;
	self->luma_height = self->mb_height << 4;

	self->chroma_width = self->mb_width << 3;
	self->chroma_height = self->mb_height << 3;

	// Allocate one big chunk of data for all 3 frames = 9 planes
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;
	size_t frame_data_size = (luma_plane_size + 2 * chroma_plane_size);

	self->frames_data = (uint8_t *)PLM_MALLOC(frame_data_size * 3);
	plm_video_init_frame(self, &self->frame_current, self->frames_data + frame_data_size * 0);
	plm_video_init_frame(self, &self->frame_forward, self->frames_data + frame_data_size * 1);
	plm_video_init_frame(self, &self->frame_backward, self->frames_data + frame_data_size * 2);

	self->has_sequence_header = TRUE;
	return TRUE;
}

void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base)
{
// printf("plm_video_init_frame\n");
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;

	frame->width = self->width;
	frame->height = self->height;
	frame->y.width = self->luma_width;
	frame->y.height = self->luma_height;
	frame->y.data = base;

	frame->cr.width = self->chroma_width;
	frame->cr.height = self->chroma_height;
	frame->cr.data = base + luma_plane_size;

	frame->cb.width = self->chroma_width;
	frame->cb.height = self->chroma_height;
	frame->cb.data = base + luma_plane_size + chroma_plane_size;
}

void plm_video_decode_picture(plm_video_t *self)
{
// printf("plm_video_decode_picture\n");
	plm_buffer_skip(self->buffer, 10); // skip temporalReference
	self->picture_type = plm_buffer_read(self->buffer, 3);
	plm_buffer_skip(self->buffer, 16); // skip vbv_delay

	// D frames or unknown coding type
	if (self->picture_type <= 0 || self->picture_type > PLM_VIDEO_PICTURE_TYPE_B)
	{
		return;
	}

	// Forward full_px, f_code
	if (
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE ||
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_B)
	{
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0)
		{
			// Ignore picture with zero f_code
			return;
		}
		self->motion_forward.r_size = f_code - 1;
	}

	// Backward full_px, f_code
	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B)
	{
		self->motion_backward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0)
		{
			// Ignore picture with zero f_code
			return;
		}
		self->motion_backward.r_size = f_code - 1;
	}

	plm_frame_t frame_temp = self->frame_forward;
	if (
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE)
	{
		self->frame_forward = self->frame_backward;
	}

	// Find first slice start code; skip extension and user data
	do
	{
		self->start_code = plm_buffer_next_start_code(self->buffer);
	} while (
			self->start_code == PLM_START_EXTENSION ||
			self->start_code == PLM_START_USER_DATA);

	// Decode all slices
	while (PLM_START_IS_SLICE(self->start_code))
	{
		plm_video_decode_slice(self, self->start_code & 0x000000FF);
		if (self->macroblock_address >= self->mb_size - 2)
		{
			break;
		}
		self->start_code = plm_buffer_next_start_code(self->buffer);
	}

	// If this is a reference picture rotate the prediction pointers
	if (
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
			self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE)
	{
		self->frame_backward = self->frame_current;
		self->frame_current = frame_temp;
	}
}

void plm_video_decode_slice(plm_video_t *self, int slice)
{
// printf("plm_video_decode_slice\n");
	self->slice_begin = TRUE;
	self->macroblock_address = (slice - 1) * self->mb_width - 1;

	// Reset motion vectors and DC predictors
	self->motion_backward.h = self->motion_forward.h = 0;
	self->motion_backward.v = self->motion_forward.v = 0;
	self->dc_predictor[0] = 128;
	self->dc_predictor[1] = 128;
	self->dc_predictor[2] = 128;

	self->quantizer_scale = plm_buffer_read(self->buffer, 5);

	// Skip extra
	while (plm_buffer_read(self->buffer, 1))
	{
		plm_buffer_skip(self->buffer, 8);
	}

	do
	{
		plm_video_decode_macroblock(self);
	} while (
			self->macroblock_address < self->mb_size - 1 &&
			plm_buffer_peek_non_zero(self->buffer, 23));
}

void plm_video_decode_macroblock(plm_video_t *self)
{
// printf("plm_video_decode_macroblock\n");
	// Decode increment
	int increment = 0;
	int t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);

	while (t == 34)
	{
		// macroblock_stuffing
		t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);
	}
	while (t == 35)
	{
		// macroblock_escape
		increment += 33;
		t = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT);
	}
	increment += t;

	// Process any skipped macroblocks
	if (self->slice_begin)
	{
		// The first increment of each slice is relative to beginning of the
		// previous row, not the previous macroblock
		self->slice_begin = FALSE;
		self->macroblock_address += increment;
	}
	else
	{
		if (self->macroblock_address + increment >= self->mb_size)
		{
			return; // invalid
		}
		if (increment > 1)
		{
			// Skipped macroblocks reset DC predictors
			self->dc_predictor[0] = 128;
			self->dc_predictor[1] = 128;
			self->dc_predictor[2] = 128;

			// Skipped macroblocks in P-pictures reset motion vectors
			if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE)
			{
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
			}
		}

		// Predict skipped macroblocks
		while (increment > 1)
		{
			self->macroblock_address++;
			self->mb_row = self->macroblock_address / self->mb_width;
			self->mb_col = self->macroblock_address % self->mb_width;

			plm_video_predict_macroblock(self);
			increment--;
		}
		self->macroblock_address++;
	}

	self->mb_row = self->macroblock_address / self->mb_width;
	self->mb_col = self->macroblock_address % self->mb_width;

	if (self->mb_col >= self->mb_width || self->mb_row >= self->mb_height)
	{
		return; // corrupt stream;
	}

	// Process the current macroblock
	const plm_vlc_t *table = PLM_VIDEO_MACROBLOCK_TYPE[self->picture_type];
	self->macroblock_type = plm_buffer_read_vlc(self->buffer, table);

	self->macroblock_intra = (self->macroblock_type & 0x01);
	self->motion_forward.is_set = (self->macroblock_type & 0x08);
	self->motion_backward.is_set = (self->macroblock_type & 0x04);

	// Quantizer scale
	if ((self->macroblock_type & 0x10) != 0)
	{
		self->quantizer_scale = plm_buffer_read(self->buffer, 5);
	}

	if (self->macroblock_intra)
	{
		// Intra-coded macroblocks reset motion vectors
		self->motion_backward.h = self->motion_forward.h = 0;
		self->motion_backward.v = self->motion_forward.v = 0;
	}
	else
	{
		// Non-intra macroblocks reset DC predictors
		self->dc_predictor[0] = 128;
		self->dc_predictor[1] = 128;
		self->dc_predictor[2] = 128;

		plm_video_decode_motion_vectors(self);
		plm_video_predict_macroblock(self);
	}

	// Decode blocks
	int cbp = ((self->macroblock_type & 0x02) != 0)
								? plm_buffer_read_vlc(self->buffer, PLM_VIDEO_CODE_BLOCK_PATTERN)
								: (self->macroblock_intra ? 0x3f : 0);

	for (int block = 0, mask = 0x20; block < 6; block++)
	{
		if ((cbp & mask) != 0)
		{
			plm_video_decode_block(self, block);
		}
		mask >>= 1;
	}
}

void plm_video_decode_motion_vectors(plm_video_t *self)
{
// printf("plm_video_decode_motion_vectors\n");

	// Forward
	if (self->motion_forward.is_set)
	{
		int r_size = self->motion_forward.r_size;
		self->motion_forward.h = plm_video_decode_motion_vector(self, r_size, self->motion_forward.h);
		self->motion_forward.v = plm_video_decode_motion_vector(self, r_size, self->motion_forward.v);
	}
	else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE)
	{
		// No motion information in P-picture, reset vectors
		self->motion_forward.h = 0;
		self->motion_forward.v = 0;
	}

	if (self->motion_backward.is_set)
	{
		int r_size = self->motion_backward.r_size;
		self->motion_backward.h = plm_video_decode_motion_vector(self, r_size, self->motion_backward.h);
		self->motion_backward.v = plm_video_decode_motion_vector(self, r_size, self->motion_backward.v);
	}
}

int plm_video_decode_motion_vector(plm_video_t *self, int r_size, int motion)
{
// printf("plm_video_decode_motion_vector\n");
	int fscale = 1 << r_size;
	int m_code = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_MOTION);
	int r = 0;
	int d;

	if ((m_code != 0) && (fscale != 1))
	{
		r = plm_buffer_read(self->buffer, r_size);
		d = ((abs(m_code) - 1) << r_size) + r + 1;
		if (m_code < 0)
		{
			d = -d;
		}
	}
	else
	{
		d = m_code;
	}

	motion += d;
	if (motion > (fscale << 4) - 1)
	{
		motion -= fscale << 5;
	}
	else if (motion < ((-fscale) << 4))
	{
		motion += fscale << 5;
	}

	return motion;
}

void plm_video_predict_macroblock(plm_video_t *self)
{
// printf("plm_video_predict_macroblock\n");
	int fw_h = self->motion_forward.h;
	int fw_v = self->motion_forward.v;

	if (self->motion_forward.full_px)
	{
		fw_h <<= 1;
		fw_v <<= 1;
	}

	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B)
	{
		int bw_h = self->motion_backward.h;
		int bw_v = self->motion_backward.v;

		if (self->motion_backward.full_px)
		{
			bw_h <<= 1;
			bw_v <<= 1;
		}

		if (self->motion_forward.is_set)
		{
			plm_video_copy_macroblock(self, &self->frame_forward, fw_h, fw_v);
			if (self->motion_backward.is_set)
			{
				plm_video_interpolate_macroblock(self, &self->frame_backward, bw_h, bw_v);
			}
		}
		else
		{
			plm_video_copy_macroblock(self, &self->frame_backward, bw_h, bw_v);
		}
	}
	else
	{
		plm_video_copy_macroblock(self, &self->frame_forward, fw_h, fw_v);
	}
}

void plm_video_copy_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v)
{
// printf("plm_video_copy_macroblock\n");
	plm_frame_t *d = &self->frame_current;
	plm_video_process_macroblock(self, s->y.data, d->y.data, motion_h, motion_v, 16, FALSE);
	plm_video_process_macroblock(self, s->cr.data, d->cr.data, motion_h / 2, motion_v / 2, 8, FALSE);
	plm_video_process_macroblock(self, s->cb.data, d->cb.data, motion_h / 2, motion_v / 2, 8, FALSE);
}

void plm_video_interpolate_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v)
{
// printf("plm_video_interpolate_macroblock\n");
	plm_frame_t *d = &self->frame_current;
	plm_video_process_macroblock(self, s->y.data, d->y.data, motion_h, motion_v, 16, TRUE);
	plm_video_process_macroblock(self, s->cr.data, d->cr.data, motion_h / 2, motion_v / 2, 8, TRUE);
	plm_video_process_macroblock(self, s->cb.data, d->cb.data, motion_h / 2, motion_v / 2, 8, TRUE);
}

#define PLM_BLOCK_SET(DEST, DEST_INDEX, DEST_WIDTH, SOURCE_INDEX, SOURCE_WIDTH, BLOCK_SIZE, OP) \
	do                                                                                            \
	{                                                                                             \
		int dest_scan = DEST_WIDTH - BLOCK_SIZE;                                                    \
		int source_scan = SOURCE_WIDTH - BLOCK_SIZE;                                                \
		for (int y = 0; y < BLOCK_SIZE; y++)                                                        \
		{                                                                                           \
			for (int x = 0; x < BLOCK_SIZE; x++)                                                      \
			{                                                                                         \
				DEST[DEST_INDEX] = OP;                                                                  \
				SOURCE_INDEX++;                                                                         \
				DEST_INDEX++;                                                                           \
			}                                                                                         \
			SOURCE_INDEX += source_scan;                                                              \
			DEST_INDEX += dest_scan;                                                                  \
		}                                                                                           \
	} while (FALSE)

void plm_video_process_macroblock(
		plm_video_t *self, uint8_t *s, uint8_t *d,
		int motion_h, int motion_v, int block_size, int interpolate)
{
// printf("plm_video_process_macroblock\n");
	int dw = self->mb_width * block_size;

	int hp = motion_h >> 1;
	int vp = motion_v >> 1;
	int odd_h = (motion_h & 1) == 1;
	int odd_v = (motion_v & 1) == 1;

	unsigned int si = ((self->mb_row * block_size) + vp) * dw + (self->mb_col * block_size) + hp;
	unsigned int di = (self->mb_row * dw + self->mb_col) * block_size;

	unsigned int max_address = (dw * (self->mb_height * block_size - block_size + 1) - block_size);
	if (si > max_address || di > max_address)
	{
		return; // corrupt video
	}

#define PLM_MB_CASE(INTERPOLATE, ODD_H, ODD_V, OP)    \
	case ((INTERPOLATE << 2) | (ODD_H << 1) | (ODD_V)): \
		PLM_BLOCK_SET(d, di, dw, si, dw, block_size, OP); \
		break

	switch ((interpolate << 2) | (odd_h << 1) | (odd_v))
	{
		PLM_MB_CASE(0, 0, 0, (s[si]));
		PLM_MB_CASE(0, 0, 1, (s[si] + s[si + dw] + 1) >> 1);
		PLM_MB_CASE(0, 1, 0, (s[si] + s[si + 1] + 1) >> 1);
		PLM_MB_CASE(0, 1, 1, (s[si] + s[si + 1] + s[si + dw] + s[si + dw + 1] + 2) >> 2);

		PLM_MB_CASE(1, 0, 0, (d[di] + (s[si]) + 1) >> 1);
		PLM_MB_CASE(1, 0, 1, (d[di] + ((s[si] + s[si + dw] + 1) >> 1) + 1) >> 1);
		PLM_MB_CASE(1, 1, 0, (d[di] + ((s[si] + s[si + 1] + 1) >> 1) + 1) >> 1);
		PLM_MB_CASE(1, 1, 1, (d[di] + ((s[si] + s[si + 1] + s[si + dw] + s[si + dw + 1] + 2) >> 2) + 1) >> 1);
	}

#undef PLM_MB_CASE
}

void plm_video_decode_block(plm_video_t *self, int block)
{
// printf("plm_video_decode_block\n");

	int n = 0;
	uint8_t *quant_matrix;

	// Decode DC coefficient of intra-coded blocks
	if (self->macroblock_intra)
	{
		int predictor;
		int dct_size;

		// DC prediction
		int plane_index = block > 3 ? block - 3 : 0;
		predictor = self->dc_predictor[plane_index];
		dct_size = plm_buffer_read_vlc(self->buffer, PLM_VIDEO_DCT_SIZE[plane_index]);

		// Read DC coeff
		if (dct_size > 0)
		{
			int differential = plm_buffer_read(self->buffer, dct_size);
			if ((differential & (1 << (dct_size - 1))) != 0)
			{
				self->block_data[0] = predictor + differential;
			}
			else
			{
				self->block_data[0] = predictor + (-(1 << dct_size) | (differential + 1));
			}
		}
		else
		{
			self->block_data[0] = predictor;
		}

		// Save predictor value
		self->dc_predictor[plane_index] = self->block_data[0];

		// Dequantize + premultiply
		self->block_data[0] <<= (3 + 5);

		quant_matrix = self->intra_quant_matrix;
		n = 1;
	}
	else
	{
		quant_matrix = self->non_intra_quant_matrix;
	}

	// Decode AC coefficients (+DC for non-intra)
	int level = 0;
	while (TRUE)
	{
		int run = 0;
		uint16_t coeff = plm_buffer_read_vlc_uint(self->buffer, PLM_VIDEO_DCT_COEFF);

		if ((coeff == 0x0001) && (n > 0) && (plm_buffer_read(self->buffer, 1) == 0))
		{
			// end_of_block
			break;
		}
		if (coeff == 0xffff)
		{
			// escape
			run = plm_buffer_read(self->buffer, 6);
			level = plm_buffer_read(self->buffer, 8);
			if (level == 0)
			{
				level = plm_buffer_read(self->buffer, 8);
			}
			else if (level == 128)
			{
				level = plm_buffer_read(self->buffer, 8) - 256;
			}
			else if (level > 128)
			{
				level = level - 256;
			}
		}
		else
		{
			run = coeff >> 8;
			level = coeff & 0xff;
			if (plm_buffer_read(self->buffer, 1))
			{
				level = -level;
			}
		}

		n += run;
		if (n < 0 || n >= 64)
		{
			return; // invalid
		}

		int de_zig_zagged = PLM_VIDEO_ZIG_ZAG[n];
		n++;

		// Dequantize, oddify, clip
		level <<= 1;
		if (!self->macroblock_intra)
		{
			level += (level < 0 ? -1 : 1);
		}
		level = (level * self->quantizer_scale * quant_matrix[de_zig_zagged]) >> 4;
		if ((level & 1) == 0)
		{
			level -= level > 0 ? 1 : -1;
		}
		if (level > 2047)
		{
			level = 2047;
		}
		else if (level < -2048)
		{
			level = -2048;
		}

		// Save premultiplied coefficient
		self->block_data[de_zig_zagged] = level * PLM_VIDEO_PREMULTIPLIER_MATRIX[de_zig_zagged];
	}

	// Move block to its place
	uint8_t *d;
	int dw;
	int di;

	if (block < 4)
	{
		d = self->frame_current.y.data;
		dw = self->luma_width;
		di = (self->mb_row * self->luma_width + self->mb_col) << 4;
		if ((block & 1) != 0)
		{
			di += 8;
		}
		if ((block & 2) != 0)
		{
			di += self->luma_width << 3;
		}
	}
	else
	{
		d = (block == 4) ? self->frame_current.cb.data : self->frame_current.cr.data;
		dw = self->chroma_width;
		di = ((self->mb_row * self->luma_width) << 2) + (self->mb_col << 3);
	}

	int *s = self->block_data;
	int si = 0;
	if (self->macroblock_intra)
	{
		// Overwrite (no prediction)
		if (n == 1)
		{
			int clamped = plm_clamp((s[0] + 128) >> 8);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, clamped);
			s[0] = 0;
		}
		else
		{
			plm_video_idct(s);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(s[si]));
			memset(self->block_data, 0, sizeof(self->block_data));
		}
	}
	else
	{
		// Add data to the predicted macroblock
		if (n == 1)
		{
			int value = (s[0] + 128) >> 8;
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(d[di] + value));
			s[0] = 0;
		}
		else
		{
			plm_video_idct(s);
			PLM_BLOCK_SET(d, di, dw, si, 8, 8, plm_clamp(d[di] + s[si]));
			memset(self->block_data, 0, sizeof(self->block_data));
		}
	}
}

void plm_video_idct(int *block)
{
// printf("plm_video_idct\n");
	int
			b1,
			b3, b4, b6, b7, tmp1, tmp2, m0,
			x0, x1, x2, x3, x4, y3, y4, y5, y6, y7;

	// Transform columns
	for (int i = 0; i < 8; ++i)
	{
		b1 = block[4 * 8 + i];
		b3 = block[2 * 8 + i] + block[6 * 8 + i];
		b4 = block[5 * 8 + i] - block[3 * 8 + i];
		tmp1 = block[1 * 8 + i] + block[7 * 8 + i];
		tmp2 = block[3 * 8 + i] + block[5 * 8 + i];
		b6 = block[1 * 8 + i] - block[7 * 8 + i];
		b7 = tmp1 + tmp2;
		m0 = block[0 * 8 + i];
		x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
		x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
		x1 = m0 - b1;
		x2 = (((block[2 * 8 + i] - block[6 * 8 + i]) * 362 + 128) >> 8) - b3;
		x3 = m0 + b1;
		y3 = x1 + x2;
		y4 = x3 + b3;
		y5 = x1 - x2;
		y6 = x3 - b3;
		y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
		block[0 * 8 + i] = b7 + y4;
		block[1 * 8 + i] = x4 + y3;
		block[2 * 8 + i] = y5 - x0;
		block[3 * 8 + i] = y6 - y7;
		block[4 * 8 + i] = y6 + y7;
		block[5 * 8 + i] = x0 + y5;
		block[6 * 8 + i] = y3 - x4;
		block[7 * 8 + i] = y4 - b7;
	}

	// Transform rows
	for (int i = 0; i < 64; i += 8)
	{
		b1 = block[4 + i];
		b3 = block[2 + i] + block[6 + i];
		b4 = block[5 + i] - block[3 + i];
		tmp1 = block[1 + i] + block[7 + i];
		tmp2 = block[3 + i] + block[5 + i];
		b6 = block[1 + i] - block[7 + i];
		b7 = tmp1 + tmp2;
		m0 = block[0 + i];
		x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
		x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
		x1 = m0 - b1;
		x2 = (((block[2 + i] - block[6 + i]) * 362 + 128) >> 8) - b3;
		x3 = m0 + b1;
		y3 = x1 + x2;
		y4 = x3 + b3;
		y5 = x1 - x2;
		y6 = x3 - b3;
		y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
		block[0 + i] = (b7 + y4 + 128) >> 8;
		block[1 + i] = (x4 + y3 + 128) >> 8;
		block[2 + i] = (y5 - x0 + 128) >> 8;
		block[3 + i] = (y6 - y7 + 128) >> 8;
		block[4 + i] = (y6 + y7 + 128) >> 8;
		block[5 + i] = (x0 + y5 + 128) >> 8;
		block[6 + i] = (y3 - x4 + 128) >> 8;
		block[7 + i] = (y4 - b7 + 128) >> 8;
	}
}