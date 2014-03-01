/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

/*******************************************************************************#
#                                                                               #
#  Audio library                                                                #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>

#include "../config.h"
#include "gviewaudio.h"
#include "gview.h"
#include "audio_portaudio.h"
#if HAS_PULSEAUDIO
  #include "audio_pulseaudio.h"
#endif

/*audio device data mutex*/
static __MUTEX_TYPE mutex;
#define __PMUTEX &mutex

#define AUDBUFF_NUM     80    /*number of audio buffers*/
#define AUDBUFF_FRAMES  1152  /*number of audio frames per buffer*/
static audio_buff_t *audio_buffers = NULL; /*pointer to buffers list*/
static int buffer_read_index = 0; /*current read index of buffer list*/
static int buffer_write_index = 0;/*current write index of buffer list*/

int verbosity = 0;
static int audio_api = AUDIO_PORTAUDIO;

/*
 * set verbosity
 * args:
 *   value - verbosity value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void audio_set_verbosity(int value)
{
	verbosity = value;
}

/*
 * Lock the mutex
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void audio_lock_mutex()
{
	__LOCK_MUTEX( __PMUTEX );
}

/*
 * Unlock the mutex
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void audio_unlock_mutex()
{
	__UNLOCK_MUTEX( __PMUTEX );
}

/*
 * free audio buffers
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int audio_free_buffers()
{
	int i = 0;

	for(i = 0; i < AUDBUFF_NUM; ++i)
	{
		free(audio_buffers[i].data);
	}

	free(audio_buffers);
	audio_buffers = NULL;
}

/*
 * alloc audio buffers
 * args:
 *    audio_ctx - pointer to audio context data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int audio_init_buffers(audio_context_t *audio_ctx)
{
	if(!audio_ctx)
		return -1;

	int i = 0;

	/*set the buffers size*/
	audio_ctx->capture_buff_size = audio_ctx->channels * AUDBUFF_FRAMES;

	if(audio_ctx->capture_buff)
		free(audio_ctx->capture_buff);

	audio_ctx->capture_buff = calloc(
		audio_ctx->capture_buff_size, sizeof(sample_t));

	if(audio_buffers != NULL)
		audio_free_buffers;

	audio_buffers = calloc(AUDBUFF_NUM, sizeof(audio_buff_t));

	for(i = 0; i < AUDBUFF_NUM; ++i)
	{
		audio_buffers[i].data = calloc(
			audio_ctx->capture_buff_size, sizeof(sample_t));
		audio_buffers[i].flag = AUDIO_BUFF_FREE;
	}

	return 0;
}

/*
 * fill a audio buffer data and move write index to next one
 * args:
 *   audio_ctx - pointer to audio context data
 *   ts - timestamp for end of data
 *
 * asserts:
 *   audio_ctx is not null
 *
 * returns: none
 */
void audio_fill_buffer(audio_context_t *audio_ctx, int64_t ts)
{
	/*in nanosec*/
	uint64_t frame_length = NSEC_PER_SEC / audio_ctx->samprate;
	uint64_t buffer_length = frame_length * (audio_ctx->capture_buff_size / audio_ctx->channels);

	audio_ctx->current_ts += buffer_length; /*buffer end time*/

	audio_ctx->ts_drift = audio_ctx->current_ts - ts;

	/*get the current write indexed buffer flag*/
	audio_lock_mutex();
	int flag = audio_buffers[buffer_write_index].flag;
	audio_unlock_mutex();

	if(flag == AUDIO_BUFF_USED)
	{
		fprintf(stderr, "AUDIO: write buffer(%i) is still in use - dropping data\n", buffer_write_index);
		return;
	}

	/*write max_frames and fill a buffer*/
	memcpy(audio_buffers[buffer_write_index].data,
		audio_ctx->capture_buff,
		audio_ctx->capture_buff_size * sizeof(sample_t));
	/*buffer begin time*/
	audio_buffers[buffer_write_index].timestamp = audio_ctx->current_ts - buffer_length;

	audio_lock_mutex();
	audio_buffers[buffer_write_index].flag = AUDIO_BUFF_USED;
	NEXT_IND(buffer_write_index, AUDBUFF_NUM);
	audio_unlock_mutex();



}

/* saturate float samples to int16 limits*/
static int16_t clip_int16 (float in)
{
	in = (in < -32768) ? -32768 : (in > 32767) ? 32767 : in;

	return ((int16_t) in);
}

/*
 * get the next used buffer from the ring buffer
 * args:
 *   audio_ctx - pointer to audio context
 *   buff - pointer to an allocated audio buffer
 *   type - type of data (SAMPLE_TYPE_[INT16|FLOAT])
 *
 * asserts:
 *   none
 *
 * returns: error code
 */
int audio_get_next_buffer(audio_context_t *audio_ctx, audio_buff_t *buff, int type)
{
	audio_lock_mutex();
	int flag = audio_buffers[buffer_read_index].flag;
	audio_unlock_mutex();

	if(flag == AUDIO_BUFF_FREE)
		return 1; /*all done*/

	int i = 0;
	/*copy pcm data*/
	if(type == SAMPLE_TYPE_FLOAT)
	{
		float *my_data = (float *) buff->data;
		memcpy( my_data, audio_buffers[buffer_read_index].data,
			audio_ctx->capture_buff_size * sizeof(sample_t));
	}
	else
	{
		int16_t *my_data = (int16_t *) buff->data;
		for(i = 0; i < audio_ctx->capture_buff_size; ++i)
			my_data[i] = clip_int16( audio_buffers[buffer_read_index].data[i] * 32767.0);
	}
	buff->timestamp = audio_buffers[buffer_read_index].timestamp;

	audio_lock_mutex();
	audio_buffers[buffer_read_index].flag = AUDIO_BUFF_FREE;
	NEXT_IND(buffer_read_index, AUDBUFF_NUM);
	audio_unlock_mutex();

	return 0;
}

/*
 * audio initialization
 * args:
 *   api - audio API to use
 *           (AUDIO_NONE, AUDIO_PORTAUDIO, AUDIO_PULSE, ...)
 *
 * asserts:
 *   none
 *
 * returns: pointer to audio context
 */
audio_context_t *audio_init(int api)
{

	audio_context_t *audio_ctx = NULL;

	audio_api = api;

	switch(audio_api)
	{
		case AUDIO_NONE:
			break;

#if HAS_PULSEAUDIO
		case AUDIO_PULSE:
			audio_ctx = audio_init_pulseaudio();
			break;
#endif
		case AUDIO_PORTAUDIO:
		default:
			audio_ctx = audio_init_portaudio();
			break;
	}

	return audio_ctx;
}

/*
 * start audio stream capture
 * args:
 *   audio_ctx - pointer to audio context data
 *
 * asserts:
 *   audio_ctx is not null
 *
 * returns: error code
 */
int audio_start(audio_context_t *audio_ctx)
{
	/*assertions*/
	assert(audio_ctx != NULL);

	/*alloc the ring buffer*/
	audio_init_buffers(audio_ctx);

	int err = 0;

	switch(audio_api)
	{
		case AUDIO_NONE:
			break;

#if HAS_PULSEAUDIO
		case AUDIO_PULSE:
			err = audio_start_pulseaudio(audio_ctx);
			break;
#endif
		case AUDIO_PORTAUDIO:
		default:
			err = audio_start_portaudio(audio_ctx);
			break;
	}

	return err;
}

/*
 * stop audio stream capture
 * args:
 *   audio_ctx - pointer to audio context data
 *
 * asserts:
 *   audio_ctx is not null
 *
 * returns: error code
 */
int audio_stop(audio_context_t *audio_ctx)
{
	int err =0;

	switch(audio_api)
	{
		case AUDIO_NONE:
			break;

#if HAS_PULSEAUDIO
		case AUDIO_PULSE:
			err = audio_stop_pulseaudio(audio_ctx);
			break;
#endif
		case AUDIO_PORTAUDIO:
		default:
			err = audio_stop_portaudio(audio_ctx);
			break;
	}

	return err;
}

/*
 * close and clean audio context
 * args:
 *   audio_ctx - pointer to audio context data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void audio_close(audio_context_t *audio_ctx)
{
	switch(audio_api)
	{
		case AUDIO_NONE:
			break;

#if HAS_PULSEAUDIO
		case AUDIO_PULSE:
			audio_close_pulseaudio(audio_ctx);
			break;
#endif
		case AUDIO_PORTAUDIO:
		default:
			audio_close_portaudio(audio_ctx);
			break;
	}

	if(audio_buffers != NULL)
		audio_free_buffers;
}
