#include "plm_audio.h"

plm_audio_t *plm_audio_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done)
{
	plm_audio_t *self = (plm_audio_t *)PLM_MALLOC(sizeof(plm_audio_t));
	memset(self, 0, sizeof(plm_audio_t));

	self->samples.count = PLM_AUDIO_SAMPLES_PER_FRAME;
	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;
	self->samplerate_index = 3; // Indicates 0

	memcpy(self->D, PLM_AUDIO_SYNTHESIS_WINDOW, 512 * sizeof(float));
	memcpy(self->D + 512, PLM_AUDIO_SYNTHESIS_WINDOW, 512 * sizeof(float));

	// Attempt to decode first header
	self->next_frame_data_size = plm_audio_decode_header(self);

	return self;
}

void plm_audio_destroy(plm_audio_t *self)
{
	if (self->destroy_buffer_when_done)
	{
		plm_buffer_destroy(self->buffer);
	}
	PLM_FREE(self);
}

int plm_audio_has_header(plm_audio_t *self)
{
	if (self->has_header)
	{
		return TRUE;
	}

	self->next_frame_data_size = plm_audio_decode_header(self);
	return self->has_header;
}

int plm_audio_get_samplerate(plm_audio_t *self)
{
	return plm_audio_has_header(self)
						 ? PLM_AUDIO_SAMPLE_RATE[self->samplerate_index]
						 : 0;
}

double plm_audio_get_time(plm_audio_t *self)
{
	return self->time;
}

void plm_audio_set_time(plm_audio_t *self, double time)
{
	self->samples_decoded = time *
													(double)PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
	self->time = time;
}

void plm_audio_rewind(plm_audio_t *self)
{
	plm_buffer_rewind(self->buffer);
	self->time = 0;
	self->samples_decoded = 0;
	self->next_frame_data_size = 0;
}

int plm_audio_has_ended(plm_audio_t *self)
{
	return plm_buffer_has_ended(self->buffer);
}

plm_samples_t *plm_audio_decode(plm_audio_t *self)
{
	// Do we have at least enough information to decode the frame header?
	if (!self->next_frame_data_size)
	{
		if (!plm_buffer_has(self->buffer, 48))
		{
			return NULL;
		}
		self->next_frame_data_size = plm_audio_decode_header(self);
	}

	if (
			self->next_frame_data_size == 0 ||
			!plm_buffer_has(self->buffer, self->next_frame_data_size << 3))
	{
		return NULL;
	}

	plm_audio_decode_frame(self);
	self->next_frame_data_size = 0;

	self->samples.time = self->time;

	self->samples_decoded += PLM_AUDIO_SAMPLES_PER_FRAME;
	self->time = (double)self->samples_decoded /
							 (double)PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];

	return &self->samples;
}

int plm_audio_find_frame_sync(plm_audio_t *self)
{
	uint32_t i;
	for (i = self->buffer->bit_index >> 3; i < self->buffer->length - 1; i++)
	{
		if (
				self->buffer->bytes[i] == 0xFF &&
				(self->buffer->bytes[i + 1] & 0xFE) == 0xFC)
		{
			self->buffer->bit_index = ((i + 1) << 3) + 3;
			return TRUE;
		}
	}
	self->buffer->bit_index = (i + 1) << 3;
	return FALSE;
}

int plm_audio_decode_header(plm_audio_t *self)
{
	if (!plm_buffer_has(self->buffer, 48))
	{
		return 0;
	}

	plm_buffer_skip_bytes(self->buffer, 0x00);
	int sync = plm_buffer_read(self->buffer, 11);

	// Attempt to resync if no syncword was found. This sucks balls. The MP2
	// stream contains a syncword just before every frame (11 bits set to 1).
	// However, this syncword is not guaranteed to not occur elsewhere in the
	// stream. So, if we have to resync, we also have to check if the header
	// (samplerate, bitrate) differs from the one we had before. This all
	// may still lead to garbage data being decoded :/

	if (sync != PLM_AUDIO_FRAME_SYNC && !plm_audio_find_frame_sync(self))
	{
		return 0;
	}

	self->version = plm_buffer_read(self->buffer, 2);
	self->layer = plm_buffer_read(self->buffer, 2);
	int hasCRC = !plm_buffer_read(self->buffer, 1);

	if (
			self->version != PLM_AUDIO_MPEG_1 ||
			self->layer != PLM_AUDIO_LAYER_II)
	{
		return 0;
	}

	int bitrate_index = plm_buffer_read(self->buffer, 4) - 1;
	if (bitrate_index > 13)
	{
		return 0;
	}

	int samplerate_index = plm_buffer_read(self->buffer, 2);
	if (samplerate_index == 3)
	{
		return 0;
	}

	int padding = plm_buffer_read(self->buffer, 1);
	plm_buffer_skip(self->buffer, 1); // f_private
	int mode = plm_buffer_read(self->buffer, 2);

	// If we already have a header, make sure the samplerate, bitrate and mode
	// are still the same, otherwise we might have missed sync.
	if (
			self->has_header && (self->bitrate_index != bitrate_index ||
													 self->samplerate_index != samplerate_index ||
													 self->mode != mode))
	{
		return 0;
	}

	self->bitrate_index = bitrate_index;
	self->samplerate_index = samplerate_index;
	self->mode = mode;
	self->has_header = TRUE;

	// Parse the mode_extension, set up the stereo bound
	if (mode == PLM_AUDIO_MODE_JOINT_STEREO)
	{
		self->bound = (plm_buffer_read(self->buffer, 2) + 1) << 2;
	}
	else
	{
		plm_buffer_skip(self->buffer, 2);
		self->bound = (mode == PLM_AUDIO_MODE_MONO) ? 0 : 32;
	}

	// Discard the last 4 bits of the header and the CRC value, if present
	plm_buffer_skip(self->buffer, 4); // copyright(1), original(1), emphasis(2)
	if (hasCRC)
	{
		plm_buffer_skip(self->buffer, 16);
	}

	// Compute frame size, check if we have enough data to decode the whole
	// frame.
	int bitrate = PLM_AUDIO_BIT_RATE[self->bitrate_index];
	int samplerate = PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
	int frame_size = (144000 * bitrate / samplerate) + padding;
	return frame_size - (hasCRC ? 6 : 4);
}

void plm_audio_decode_frame(plm_audio_t *self)
{
	// Prepare the quantizer table lookups
	int tab3 = 0;
	int sblimit = 0;

	int tab1 = (self->mode == PLM_AUDIO_MODE_MONO) ? 0 : 1;
	int tab2 = PLM_AUDIO_QUANT_LUT_STEP_1[tab1][self->bitrate_index];
	tab3 = QUANT_LUT_STEP_2[tab2][self->samplerate_index];
	sblimit = tab3 & 63;
	tab3 >>= 6;

	if (self->bound > sblimit)
	{
		self->bound = sblimit;
	}

	// Read the allocation information
	for (int sb = 0; sb < self->bound; sb++)
	{
		self->allocation[0][sb] = plm_audio_read_allocation(self, sb, tab3);
		self->allocation[1][sb] = plm_audio_read_allocation(self, sb, tab3);
	}

	for (int sb = self->bound; sb < sblimit; sb++)
	{
		self->allocation[0][sb] =
				self->allocation[1][sb] =
						plm_audio_read_allocation(self, sb, tab3);
	}

	// Read scale factor selector information
	int channels = (self->mode == PLM_AUDIO_MODE_MONO) ? 1 : 2;
	for (int sb = 0; sb < sblimit; sb++)
	{
		for (int ch = 0; ch < channels; ch++)
		{
			if (self->allocation[ch][sb])
			{
				self->scale_factor_info[ch][sb] = plm_buffer_read(self->buffer, 2);
			}
		}
		if (self->mode == PLM_AUDIO_MODE_MONO)
		{
			self->scale_factor_info[1][sb] = self->scale_factor_info[0][sb];
		}
	}

	// Read scale factors
	for (int sb = 0; sb < sblimit; sb++)
	{
		for (int ch = 0; ch < channels; ch++)
		{
			if (self->allocation[ch][sb])
			{
				int *sf = self->scale_factor[ch][sb];
				switch (self->scale_factor_info[ch][sb])
				{
				case 0:
					sf[0] = plm_buffer_read(self->buffer, 6);
					sf[1] = plm_buffer_read(self->buffer, 6);
					sf[2] = plm_buffer_read(self->buffer, 6);
					break;
				case 1:
					sf[0] =
							sf[1] = plm_buffer_read(self->buffer, 6);
					sf[2] = plm_buffer_read(self->buffer, 6);
					break;
				case 2:
					sf[0] =
							sf[1] =
									sf[2] = plm_buffer_read(self->buffer, 6);
					break;
				case 3:
					sf[0] = plm_buffer_read(self->buffer, 6);
					sf[1] =
							sf[2] = plm_buffer_read(self->buffer, 6);
					break;
				}
			}
		}
		if (self->mode == PLM_AUDIO_MODE_MONO)
		{
			self->scale_factor[1][sb][0] = self->scale_factor[0][sb][0];
			self->scale_factor[1][sb][1] = self->scale_factor[0][sb][1];
			self->scale_factor[1][sb][2] = self->scale_factor[0][sb][2];
		}
	}

	// Coefficient input and reconstruction
	int out_pos = 0;
	for (int part = 0; part < 3; part++)
	{
		for (int granule = 0; granule < 4; granule++)
		{

			// Read the samples
			for (int sb = 0; sb < self->bound; sb++)
			{
				plm_audio_read_samples(self, 0, sb, part);
				plm_audio_read_samples(self, 1, sb, part);
			}
			for (int sb = self->bound; sb < sblimit; sb++)
			{
				plm_audio_read_samples(self, 0, sb, part);
				self->sample[1][sb][0] = self->sample[0][sb][0];
				self->sample[1][sb][1] = self->sample[0][sb][1];
				self->sample[1][sb][2] = self->sample[0][sb][2];
			}
			for (int sb = sblimit; sb < 32; sb++)
			{
				self->sample[0][sb][0] = 0;
				self->sample[0][sb][1] = 0;
				self->sample[0][sb][2] = 0;
				self->sample[1][sb][0] = 0;
				self->sample[1][sb][1] = 0;
				self->sample[1][sb][2] = 0;
			}

			// Synthesis loop
			for (int p = 0; p < 3; p++)
			{
				// Shifting step
				self->v_pos = (self->v_pos - 64) & 1023;

				for (int ch = 0; ch < 2; ch++)
				{
					plm_audio_idct36(self->sample[ch], p, self->V[ch], self->v_pos);

					// Build U, windowing, calculate output
					memset(self->U, 0, sizeof(self->U));

					int d_index = 512 - (self->v_pos >> 1);
					int v_index = (self->v_pos % 128) >> 1;
					while (v_index < 1024)
					{
						for (int i = 0; i < 32; ++i)
						{
							self->U[i] += self->D[d_index++] * self->V[ch][v_index++];
						}

						v_index += 128 - 32;
						d_index += 64 - 32;
					}

					d_index -= (512 - 32);
					v_index = (128 - 32 + 1024) - v_index;
					while (v_index < 1024)
					{
						for (int i = 0; i < 32; ++i)
						{
							self->U[i] += self->D[d_index++] * self->V[ch][v_index++];
						}

						v_index += 128 - 32;
						d_index += 64 - 32;
					}

// Output samples
#ifdef PLM_AUDIO_SEPARATE_CHANNELS
					float *out_channel = ch == 0
																	 ? self->samples.left
																	 : self->samples.right;
					for (int j = 0; j < 32; j++)
					{
						out_channel[out_pos + j] = self->U[j] / 2147418112.0f;
					}
#else
					for (int j = 0; j < 32; j++)
					{
						self->samples.interleaved[((out_pos + j) << 1) + ch] =
								self->U[j] / 2147418112.0f;
					}
#endif
				} // End of synthesis channel loop
				out_pos += 32;
			} // End of synthesis sub-block loop

		} // Decoding of the granule finished
	}

	plm_buffer_align(self->buffer);
}

const plm_quantizer_spec_t *plm_audio_read_allocation(plm_audio_t *self, int sb, int tab3)
{
	int tab4 = PLM_AUDIO_QUANT_LUT_STEP_3[tab3][sb];
	int qtab = PLM_AUDIO_QUANT_LUT_STEP_4[tab4 & 15][plm_buffer_read(self->buffer, tab4 >> 4)];
	return qtab ? (&PLM_AUDIO_QUANT_TAB[qtab - 1]) : 0;
}

void plm_audio_read_samples(plm_audio_t *self, int ch, int sb, int part)
{
	const plm_quantizer_spec_t *q = self->allocation[ch][sb];
	int sf = self->scale_factor[ch][sb][part];
	int *sample = self->sample[ch][sb];
	int val = 0;

	if (!q)
	{
		// No bits allocated for this subband
		sample[0] = sample[1] = sample[2] = 0;
		return;
	}

	// Resolve scalefactor
	if (sf == 63)
	{
		sf = 0;
	}
	else
	{
		int shift = (sf / 3) | 0;
		sf = (PLM_AUDIO_SCALEFACTOR_BASE[sf % 3] + ((1 << shift) >> 1)) >> shift;
	}

	// Decode samples
	int adj = q->levels;
	if (q->group)
	{
		// Decode grouped samples
		val = plm_buffer_read(self->buffer, q->bits);
		sample[0] = val % adj;
		val /= adj;
		sample[1] = val % adj;
		sample[2] = val / adj;
	}
	else
	{
		// Decode direct samples
		sample[0] = plm_buffer_read(self->buffer, q->bits);
		sample[1] = plm_buffer_read(self->buffer, q->bits);
		sample[2] = plm_buffer_read(self->buffer, q->bits);
	}

	// Postmultiply samples
	int scale = 65536 / (adj + 1);
	adj = ((adj + 1) >> 1) - 1;

	val = (adj - sample[0]) * scale;
	sample[0] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;

	val = (adj - sample[1]) * scale;
	sample[1] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;

	val = (adj - sample[2]) * scale;
	sample[2] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;
}

void plm_audio_idct36(int s[32][3], int ss, float *d, int dp)
{
	float t01, t02, t03, t04, t05, t06, t07, t08, t09, t10, t11, t12,
			t13, t14, t15, t16, t17, t18, t19, t20, t21, t22, t23, t24,
			t25, t26, t27, t28, t29, t30, t31, t32, t33;

	t01 = (float)(s[0][ss] + s[31][ss]);
	t02 = (float)(s[0][ss] - s[31][ss]) * 0.500602998235f;
	t03 = (float)(s[1][ss] + s[30][ss]);
	t04 = (float)(s[1][ss] - s[30][ss]) * 0.505470959898f;
	t05 = (float)(s[2][ss] + s[29][ss]);
	t06 = (float)(s[2][ss] - s[29][ss]) * 0.515447309923f;
	t07 = (float)(s[3][ss] + s[28][ss]);
	t08 = (float)(s[3][ss] - s[28][ss]) * 0.53104259109f;
	t09 = (float)(s[4][ss] + s[27][ss]);
	t10 = (float)(s[4][ss] - s[27][ss]) * 0.553103896034f;
	t11 = (float)(s[5][ss] + s[26][ss]);
	t12 = (float)(s[5][ss] - s[26][ss]) * 0.582934968206f;
	t13 = (float)(s[6][ss] + s[25][ss]);
	t14 = (float)(s[6][ss] - s[25][ss]) * 0.622504123036f;
	t15 = (float)(s[7][ss] + s[24][ss]);
	t16 = (float)(s[7][ss] - s[24][ss]) * 0.674808341455f;
	t17 = (float)(s[8][ss] + s[23][ss]);
	t18 = (float)(s[8][ss] - s[23][ss]) * 0.744536271002f;
	t19 = (float)(s[9][ss] + s[22][ss]);
	t20 = (float)(s[9][ss] - s[22][ss]) * 0.839349645416f;
	t21 = (float)(s[10][ss] + s[21][ss]);
	t22 = (float)(s[10][ss] - s[21][ss]) * 0.972568237862f;
	t23 = (float)(s[11][ss] + s[20][ss]);
	t24 = (float)(s[11][ss] - s[20][ss]) * 1.16943993343f;
	t25 = (float)(s[12][ss] + s[19][ss]);
	t26 = (float)(s[12][ss] - s[19][ss]) * 1.48416461631f;
	t27 = (float)(s[13][ss] + s[18][ss]);
	t28 = (float)(s[13][ss] - s[18][ss]) * 2.05778100995f;
	t29 = (float)(s[14][ss] + s[17][ss]);
	t30 = (float)(s[14][ss] - s[17][ss]) * 3.40760841847f;
	t31 = (float)(s[15][ss] + s[16][ss]);
	t32 = (float)(s[15][ss] - s[16][ss]) * 10.1900081235f;

	t33 = t01 + t31;
	t31 = (t01 - t31) * 0.502419286188f;
	t01 = t03 + t29;
	t29 = (t03 - t29) * 0.52249861494f;
	t03 = t05 + t27;
	t27 = (t05 - t27) * 0.566944034816f;
	t05 = t07 + t25;
	t25 = (t07 - t25) * 0.64682178336f;
	t07 = t09 + t23;
	t23 = (t09 - t23) * 0.788154623451f;
	t09 = t11 + t21;
	t21 = (t11 - t21) * 1.06067768599f;
	t11 = t13 + t19;
	t19 = (t13 - t19) * 1.72244709824f;
	t13 = t15 + t17;
	t17 = (t15 - t17) * 5.10114861869f;
	t15 = t33 + t13;
	t13 = (t33 - t13) * 0.509795579104f;
	t33 = t01 + t11;
	t01 = (t01 - t11) * 0.601344886935f;
	t11 = t03 + t09;
	t09 = (t03 - t09) * 0.899976223136f;
	t03 = t05 + t07;
	t07 = (t05 - t07) * 2.56291544774f;
	t05 = t15 + t03;
	t15 = (t15 - t03) * 0.541196100146f;
	t03 = t33 + t11;
	t11 = (t33 - t11) * 1.30656296488f;
	t33 = t05 + t03;
	t05 = (t05 - t03) * 0.707106781187f;
	t03 = t15 + t11;
	t15 = (t15 - t11) * 0.707106781187f;
	t03 += t15;
	t11 = t13 + t07;
	t13 = (t13 - t07) * 0.541196100146f;
	t07 = t01 + t09;
	t09 = (t01 - t09) * 1.30656296488f;
	t01 = t11 + t07;
	t07 = (t11 - t07) * 0.707106781187f;
	t11 = t13 + t09;
	t13 = (t13 - t09) * 0.707106781187f;
	t11 += t13;
	t01 += t11;
	t11 += t07;
	t07 += t13;
	t09 = t31 + t17;
	t31 = (t31 - t17) * 0.509795579104f;
	t17 = t29 + t19;
	t29 = (t29 - t19) * 0.601344886935f;
	t19 = t27 + t21;
	t21 = (t27 - t21) * 0.899976223136f;
	t27 = t25 + t23;
	t23 = (t25 - t23) * 2.56291544774f;
	t25 = t09 + t27;
	t09 = (t09 - t27) * 0.541196100146f;
	t27 = t17 + t19;
	t19 = (t17 - t19) * 1.30656296488f;
	t17 = t25 + t27;
	t27 = (t25 - t27) * 0.707106781187f;
	t25 = t09 + t19;
	t19 = (t09 - t19) * 0.707106781187f;
	t25 += t19;
	t09 = t31 + t23;
	t31 = (t31 - t23) * 0.541196100146f;
	t23 = t29 + t21;
	t21 = (t29 - t21) * 1.30656296488f;
	t29 = t09 + t23;
	t23 = (t09 - t23) * 0.707106781187f;
	t09 = t31 + t21;
	t31 = (t31 - t21) * 0.707106781187f;
	t09 += t31;
	t29 += t09;
	t09 += t23;
	t23 += t31;
	t17 += t29;
	t29 += t25;
	t25 += t09;
	t09 += t27;
	t27 += t23;
	t23 += t19;
	t19 += t31;
	t21 = t02 + t32;
	t02 = (t02 - t32) * 0.502419286188f;
	t32 = t04 + t30;
	t04 = (t04 - t30) * 0.52249861494f;
	t30 = t06 + t28;
	t28 = (t06 - t28) * 0.566944034816f;
	t06 = t08 + t26;
	t08 = (t08 - t26) * 0.64682178336f;
	t26 = t10 + t24;
	t10 = (t10 - t24) * 0.788154623451f;
	t24 = t12 + t22;
	t22 = (t12 - t22) * 1.06067768599f;
	t12 = t14 + t20;
	t20 = (t14 - t20) * 1.72244709824f;
	t14 = t16 + t18;
	t16 = (t16 - t18) * 5.10114861869f;
	t18 = t21 + t14;
	t14 = (t21 - t14) * 0.509795579104f;
	t21 = t32 + t12;
	t32 = (t32 - t12) * 0.601344886935f;
	t12 = t30 + t24;
	t24 = (t30 - t24) * 0.899976223136f;
	t30 = t06 + t26;
	t26 = (t06 - t26) * 2.56291544774f;
	t06 = t18 + t30;
	t18 = (t18 - t30) * 0.541196100146f;
	t30 = t21 + t12;
	t12 = (t21 - t12) * 1.30656296488f;
	t21 = t06 + t30;
	t30 = (t06 - t30) * 0.707106781187f;
	t06 = t18 + t12;
	t12 = (t18 - t12) * 0.707106781187f;
	t06 += t12;
	t18 = t14 + t26;
	t26 = (t14 - t26) * 0.541196100146f;
	t14 = t32 + t24;
	t24 = (t32 - t24) * 1.30656296488f;
	t32 = t18 + t14;
	t14 = (t18 - t14) * 0.707106781187f;
	t18 = t26 + t24;
	t24 = (t26 - t24) * 0.707106781187f;
	t18 += t24;
	t32 += t18;
	t18 += t14;
	t26 = t14 + t24;
	t14 = t02 + t16;
	t02 = (t02 - t16) * 0.509795579104f;
	t16 = t04 + t20;
	t04 = (t04 - t20) * 0.601344886935f;
	t20 = t28 + t22;
	t22 = (t28 - t22) * 0.899976223136f;
	t28 = t08 + t10;
	t10 = (t08 - t10) * 2.56291544774f;
	t08 = t14 + t28;
	t14 = (t14 - t28) * 0.541196100146f;
	t28 = t16 + t20;
	t20 = (t16 - t20) * 1.30656296488f;
	t16 = t08 + t28;
	t28 = (t08 - t28) * 0.707106781187f;
	t08 = t14 + t20;
	t20 = (t14 - t20) * 0.707106781187f;
	t08 += t20;
	t14 = t02 + t10;
	t02 = (t02 - t10) * 0.541196100146f;
	t10 = t04 + t22;
	t22 = (t04 - t22) * 1.30656296488f;
	t04 = t14 + t10;
	t10 = (t14 - t10) * 0.707106781187f;
	t14 = t02 + t22;
	t02 = (t02 - t22) * 0.707106781187f;
	t14 += t02;
	t04 += t14;
	t14 += t10;
	t10 += t02;
	t16 += t04;
	t04 += t08;
	t08 += t14;
	t14 += t28;
	t28 += t10;
	t10 += t20;
	t20 += t02;
	t21 += t16;
	t16 += t32;
	t32 += t04;
	t04 += t06;
	t06 += t08;
	t08 += t18;
	t18 += t14;
	t14 += t30;
	t30 += t28;
	t28 += t26;
	t26 += t10;
	t10 += t12;
	t12 += t20;
	t20 += t24;
	t24 += t02;

	d[dp + 48] = -t33;
	d[dp + 49] = d[dp + 47] = -t21;
	d[dp + 50] = d[dp + 46] = -t17;
	d[dp + 51] = d[dp + 45] = -t16;
	d[dp + 52] = d[dp + 44] = -t01;
	d[dp + 53] = d[dp + 43] = -t32;
	d[dp + 54] = d[dp + 42] = -t29;
	d[dp + 55] = d[dp + 41] = -t04;
	d[dp + 56] = d[dp + 40] = -t03;
	d[dp + 57] = d[dp + 39] = -t06;
	d[dp + 58] = d[dp + 38] = -t25;
	d[dp + 59] = d[dp + 37] = -t08;
	d[dp + 60] = d[dp + 36] = -t11;
	d[dp + 61] = d[dp + 35] = -t18;
	d[dp + 62] = d[dp + 34] = -t09;
	d[dp + 63] = d[dp + 33] = -t14;
	d[dp + 32] = -t05;
	d[dp + 0] = t05;
	d[dp + 31] = -t30;
	d[dp + 1] = t30;
	d[dp + 30] = -t27;
	d[dp + 2] = t27;
	d[dp + 29] = -t28;
	d[dp + 3] = t28;
	d[dp + 28] = -t07;
	d[dp + 4] = t07;
	d[dp + 27] = -t26;
	d[dp + 5] = t26;
	d[dp + 26] = -t23;
	d[dp + 6] = t23;
	d[dp + 25] = -t10;
	d[dp + 7] = t10;
	d[dp + 24] = -t15;
	d[dp + 8] = t15;
	d[dp + 23] = -t12;
	d[dp + 9] = t12;
	d[dp + 22] = -t19;
	d[dp + 10] = t19;
	d[dp + 21] = -t20;
	d[dp + 11] = t20;
	d[dp + 20] = -t13;
	d[dp + 12] = t13;
	d[dp + 19] = -t24;
	d[dp + 13] = t24;
	d[dp + 18] = -t31;
	d[dp + 14] = t31;
	d[dp + 17] = -t02;
	d[dp + 15] = t02;
	d[dp + 16] = 0.0;
}
