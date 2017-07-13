/* tinycap.c
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <tinyalsa/asoundlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <limits.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

int capturing = 1;
int prinfo = 1;

unsigned int capture_sample(FILE *file, unsigned int card, unsigned int device,
                            unsigned int channels, unsigned int rate,
                            enum pcm_format format, unsigned int period_size,
                            unsigned int period_count, unsigned int capture_time);

void sigint_handler(int sig)
{
    if (sig == SIGINT){
        capturing = 0;
    }
}

void tinycap_print_help(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options] file.\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-D, --card\n");
    fprintf(stderr, "\t-d, --device\n");
    fprintf(stderr, "\t-c, --channels\n");
    fprintf(stderr, "\t-r, --rate\n");
    fprintf(stderr, "\t-p, --period-size\n");
    fprintf(stderr, "\t-P, --period-count\n");
    fprintf(stderr, "\t-t, --time\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "The file argument may be a path to a file or stdout, indicated by '--'.\n");
}

void tinycap_print_version(const char *argv0)
{
    fprintf(stderr, "%s (tinyalsa %s)\n",
            argv0, TINYALSA_VERSION_STRING);
}

int main(int argc, char **argv)
{
    FILE *file;
    struct wav_header header;
    unsigned int card = 0;
    unsigned int device = 0;
    unsigned int channels = 2;
    unsigned int rate = 48000;
    unsigned int bits = 16;
    unsigned int frames = 0;
    unsigned int period_size = 1024;
    unsigned int period_count = 4;
    unsigned int capture_time = UINT_MAX;
    enum pcm_format format;
    int no_header = 0;

    struct option opts[] =
    {
        { "card", required_argument, 0, 'D' },
        { "device", required_argument, 0, 'd' },
        { "channels", required_argument, 0, 'c' },
        { "rate", required_argument, 0, 'r' },
        { "period-size", required_argument, 0, 'p' },
        { "period-count", required_argument, 0, 'P' },
        { "time", required_argument, 0, 't' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int c = getopt_long(argc, argv, "D:d:c:r:p:P:t:hv", opts, NULL);
        if (c == 'D') {
            if (sscanf(optarg, "%u", &card) != 1) {
                fprintf(stderr, "Failed parsing card number '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'd') {
            if (sscanf(optarg, "%u", &device) != 1) {
                fprintf(stderr, "Failed parsing device number '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'c') {
            if (sscanf(optarg, "%u", &channels) != 1) {
                fprintf(stderr, "Failed parsing channels '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'r') {
            if (sscanf(optarg, "%u", &rate) != 1) {
                fprintf(stderr, "Failed parsing rate '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'p') {
            if (sscanf(optarg, "%u", &period_size) != 1) {
                fprintf(stderr, "Failed parsing period size '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'P') {
            if (sscanf(optarg, "%u", &period_count) != 1) {
                fprintf(stderr, "Failed parsing period_count '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 't') {
            if (sscanf(optarg, "%u", &capture_time) != 1) {
                fprintf(stderr, "Failed parsing capture time '%s'.\n", optarg);
                return EXIT_FAILURE;
            }
        } else if (c == 'h') {
            tinycap_print_help(argv[0]);
            return EXIT_FAILURE;
        } else if (c == 'v') {
            tinycap_print_version(argv[0]);
            return EXIT_FAILURE;
        } else if (c == '?') {
            /* error occured */
            return EXIT_FAILURE;
        } else if (c == -1) {
            /* end of args */
            break;
        } else {
            /* uhandled */
            return EXIT_FAILURE;
        }
    }

    const char *filename;
    if (optind >= argc) {
        fprintf(stderr, "No file specified.\n");
        return EXIT_FAILURE;
    } else {
        filename = argv[optind];
    }

    if (strcmp(filename,"--") == 0) {
        file = stdout;
        prinfo = 0;
        no_header = 1;
    } else {
        file = fopen(filename, "wb");
        if (!file) {
            fprintf(stderr, "Unable to create file '%s'.\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    header.riff_id = ID_RIFF;
    header.riff_sz = 0;
    header.riff_fmt = ID_WAVE;
    header.fmt_id = ID_FMT;
    header.fmt_sz = 16;
    header.audio_format = FORMAT_PCM;
    header.num_channels = channels;
    header.sample_rate = rate;

    switch (bits) {
    case 32:
        format = PCM_FORMAT_S32_LE;
        break;
    case 24:
        format = PCM_FORMAT_S24_LE;
        break;
    case 16:
        format = PCM_FORMAT_S16_LE;
        break;
    default:
        fprintf(stderr, "%u bits is not supported.\n", bits);
        return EXIT_FAILURE;
    }

    header.bits_per_sample = pcm_format_to_bits(format);
    header.byte_rate = (header.bits_per_sample / 8) * channels * rate;
    header.block_align = channels * (header.bits_per_sample / 8);
    header.data_id = ID_DATA;

    /* leave enough room for header */
    if (!no_header) {
        fseek(file, sizeof(struct wav_header), SEEK_SET);
    }

    /* install signal handler and begin capturing */
    signal(SIGINT, sigint_handler);
    frames = capture_sample(file, card, device, header.num_channels,
                            header.sample_rate, format,
                            period_size, period_count, capture_time);
    if (prinfo) {
        printf("Captured %u frames.\n", frames);
    }

    /* write header now all information is known */
    if (!no_header) {
        header.data_sz = frames * header.block_align;
        header.riff_sz = header.data_sz + sizeof(header) - 8;
        fseek(file, 0, SEEK_SET);
        fwrite(&header, sizeof(struct wav_header), 1, file);
    }

    fclose(file);

    return EXIT_SUCCESS;
}

unsigned int capture_sample(FILE *file, unsigned int card, unsigned int device,
                            unsigned int channels, unsigned int rate,
                            enum pcm_format format, unsigned int period_size,
                            unsigned int period_count, unsigned int capture_time)
{
    struct pcm_config config;
    struct pcm *pcm;
    char *buffer;
    unsigned int size;
    unsigned int frames_read;
    unsigned int total_frames_read;
    unsigned int bytes_per_frame;

    memset(&config, 0, sizeof(config));
    config.channels = channels;
    config.rate = rate;
    config.period_size = period_size;
    config.period_count = period_count;
    config.format = format;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;

    pcm = pcm_open(card, device, PCM_IN, &config);
    if (!pcm || !pcm_is_ready(pcm)) {
        fprintf(stderr, "Unable to open PCM device (%s)\n",
                pcm_get_error(pcm));
        return 0;
    }

    size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "Unable to allocate %u bytes\n", size);
        pcm_close(pcm);
        return 0;
    }

    if (prinfo) {
        printf("Capturing sample: %u ch, %u hz, %u bit\n", channels, rate,
           pcm_format_to_bits(format));
    }

    bytes_per_frame = pcm_frames_to_bytes(pcm, 1);
    total_frames_read = 0;
    frames_read = 0;
    while (capturing) {
        frames_read = pcm_readi(pcm, buffer, pcm_get_buffer_size(pcm));
        total_frames_read += frames_read;
        if ((total_frames_read / rate) >= capture_time) {
            capturing = 0;
        }
        if (fwrite(buffer, bytes_per_frame, frames_read, file) != frames_read) {
            fprintf(stderr,"Error capturing sample\n");
            break;
        }
    }

    free(buffer);
    pcm_close(pcm);
    return total_frames_read;
}

