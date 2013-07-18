/*
 * Copyright (c) 2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * OpenAL audio output driver.
 *
 * This file is part of the libspotify examples suite.
 */

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>

#include "audio.h"

#define NUM_BUFFERS 3

static void error_exit(const char *msg)
{
	syslog(LOG_ERR, "OpenAL error: %s", msg);
	exit(1);
}

static int queue_buffer(ALuint source, audio_fifo_t *af, ALuint buffer)
{
	audio_fifo_data_t *afd = audio_get(af) /* blocks until data available */;

	alBufferData(buffer,
		 afd->channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
		 afd->samples,
		 afd->nsamples * afd->channels * sizeof(short),
		 afd->rate);
	alSourceQueueBuffers(source, 1, &buffer);

	free(afd);

	return 1;
}

static void* audio_start(void *aux)
{
	audio_fifo_t *af = aux;
	audio_fifo_data_t *afd;
	unsigned int frame = 0;
	ALCdevice *device = NULL;
	ALCcontext *context = NULL;
	ALuint buffers[NUM_BUFFERS];
	ALuint source;
	ALint processed;
	ALenum error;
	ALint rate;
	ALint channels;
	ALint val;

	device = alcOpenDevice(NULL); /* Use the default device */
	if(!device)
		error_exit("failed to open device");

	context = alcCreateContext(device, NULL);
	alcMakeContextCurrent(context);
	alListenerf(AL_GAIN, 1.0f);
	alDistanceModel(AL_NONE);
	alGenBuffers((ALsizei)NUM_BUFFERS, buffers);
	alGenSources(1, &source);

	/* First prebuffer some audio */
	for(val = 0; val < NUM_BUFFERS; val++)
		queue_buffer(source, af, buffers[val]);

	for (;;) {

		alSourcePlay(source);
		for (;;) {
			/* Wait for some audio to play */
			do {
				alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
				usleep(100);
			} while (!processed);

			/* Remove old audio from the queue.. */
			alSourceUnqueueBuffers(source, 1, &buffers[frame % NUM_BUFFERS]);

			/* and queue some more audio */
			afd = audio_get(af);
			alGetBufferi(buffers[frame % NUM_BUFFERS], AL_FREQUENCY, &rate);
			alGetBufferi(buffers[frame % NUM_BUFFERS], AL_CHANNELS, &channels);
			if (afd->rate != rate || afd->channels != channels) {
				syslog(LOG_NOTICE, "OpenAL: rate or channel count changed, resetting");
				break;
			}

			/* If a buffer underrun occured, restart playback */
			alGetSourcei(source, AL_SOURCE_STATE, &val);
			if(val != AL_PLAYING) {
				syslog(LOG_NOTICE, "OpenAL: Audio playback stopped (buffer underrun?), restarting");
				break;
			}

			alBufferData(buffers[frame % NUM_BUFFERS],
					 afd->channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
					 afd->samples,
					 afd->nsamples * afd->channels * sizeof(short),
					 afd->rate);
			free(afd);
			alSourceQueueBuffers(source, 1, &buffers[frame % NUM_BUFFERS]);

			if ((error = alcGetError(device)) != AL_NO_ERROR) {
				syslog(LOG_ERR, "OpenAL error: alcGetError() returned %d, killing audio thread", error);
				exit(1);
			}

			frame++;
		}

		/* Format or rate changed, so we need to reset all buffers */
		alSourcei(source, AL_BUFFER, 0);
		alSourceStop(source);

		/* Make sure we don't lose the audio packet that caused the change */
		alBufferData(buffers[0],
				 afd->channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
				 afd->samples,
				 afd->nsamples * afd->channels * sizeof(short),
				 afd->rate);
		free(afd);

		alSourceQueueBuffers(source, 1, &buffers[0]);
		for(val = 1; val < NUM_BUFFERS; val++)
			queue_buffer(source, af, buffers[val]);

		frame = 0;
	}
}

void audio_init(audio_fifo_t *af)
{
	pthread_t tid;

	TAILQ_INIT(&af->q);
	af->qlen = 0;

	pthread_mutex_init(&af->mutex, NULL);
	pthread_cond_init(&af->cond, NULL);

	pthread_create(&tid, NULL, audio_start, af);
}
