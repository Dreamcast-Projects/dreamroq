/*
 * Dreamroq by Mike Melanson
 *
 * This is a simple, sample program that helps test the Dreamroq library.
 */

#include <stdio.h>
#include "dreamroqlib.h"

int quit_cb()
{
    /* big, fat no-op for command line tool */
    return 0;
}

void video_callback(unsigned short* buf, int width, int height, int stride, int texture_height)
{
    static int count = 0;
    FILE *out;
    char filename[20];
    int x, y;
    unsigned int pixel;
    unsigned short *buf_rgb565 = (unsigned short*)buf;

    sprintf(filename, "extract/%04d.pnm", count);
    printf("writing frame %d to file %s\n", count, filename);
    count++;
    out = fopen(filename, "wb");
    if (!out)
        return;
    fprintf(out, "P6\n%d %d\n255\n", width, height);
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            pixel = *buf_rgb565++;
            fputc(((pixel >> 11) << 3) & 0xFF, out);  /* red */
            fputc(((pixel >>  5) << 2) & 0xFF, out);  /* green */
            fputc(((pixel >>  0) << 3) & 0xFF, out);  /* blue */
        }
        buf_rgb565 += (stride - width);
    }
    fclose(out);
}

#define AUDIO_FILENAME "extract/roq-audio.wav"
static char wav_header[] = {
    'R', 'I', 'F', 'F',  /* RIFF header */
      0,   0,   0,   0,  /* file size will be filled in later */
    'W', 'A', 'V', 'E',  /* more header stuff */
    'f', 'm', 't', 0x20,
    0x10,  0,   0,   0,  /* length of format chunk */
      1,   0,            /* format = 1 (PCM) */
      0,   0,            /* channel count will be filled in later */
    0x22, 0x56, 0,   0,  /* frequency is always 0x5622 = 22050 Hz */
      0,   0,   0,   0,  /* byte rate will be filled in later */
      1,   0, 0x10,  0,  /* data alignment and bits per sample */
    'd', 'a', 't', 'a',  /* start of data chunk */
      0,   0,   0,   0   /* data block size will be filled in later */
};
#define WAV_HEADER_SIZE 44
#define SAMPLE_RATE 22050
static FILE *wav_output;
static int data_size = 0;
static int audio_output_initialized = 0;

void audio_callback(unsigned char* buf_rgb565, int samples, int channels)
{
    int byte_rate;

    if (!audio_output_initialized)
    {
        wav_output = fopen(AUDIO_FILENAME, "wb");
        if (!wav_output)
            return;

        /* fill in channels and data rate fields */
        if (channels != 1 && channels != 2)
            return;
        wav_header[22] = channels;
        byte_rate = SAMPLE_RATE * 2 * channels;
        wav_header[0x1C] = (byte_rate >>  0) & 0xFF;
        wav_header[0x1D] = (byte_rate >>  8) & 0xFF;
        wav_header[0x1E] = (byte_rate >> 16) & 0xFF;
        wav_header[0x1F] = (byte_rate >> 24) & 0xFF;

        /* write the header */
        if (fwrite(wav_header, WAV_HEADER_SIZE, 1, wav_output) != 1)
        {
            fclose(wav_output);
        }

        audio_output_initialized = 1;
    }

    /* dump the data and account for the amount */
    if (fwrite(buf_rgb565, samples, 1, wav_output) != 1)
    {
        fclose(wav_output);
    }
    data_size += samples;
}

int finish_cb()
{
    if (audio_output_initialized)
    {
        /* rewind and rewrite the header with the known parameters */
        printf("Wrote %d (0x%X) bytes to %s\n", data_size, data_size,
            AUDIO_FILENAME);
        fseek(wav_output, 0, SEEK_SET);
        wav_header[40] = (data_size >>  0) & 0xFF;
        wav_header[41] = (data_size >>  8) & 0xFF;
        wav_header[42] = (data_size >> 16) & 0xFF;
        wav_header[43] = (data_size >> 24) & 0xFF;
        data_size += WAV_HEADER_SIZE - 8;
        wav_header[4] = (data_size >>  0) & 0xFF;
        wav_header[5] = (data_size >>  8) & 0xFF;
        wav_header[6] = (data_size >> 16) & 0xFF;
        wav_header[7] = (data_size >> 24) & 0xFF;
        if (fwrite(wav_header, WAV_HEADER_SIZE, 1, wav_output) != 1)
        {
            fclose(wav_output);
            return ROQ_CLIENT_PROBLEM;
        }
    }

    return ROQ_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("USAGE: test-dreamroq <file.roq>\n");
        return 1;
    }

    roq_t *roq = roq_create_with_filename(argv[1]);

    // Install the video & audio decode callbacks
    roq_set_video_decode_callback(roq, video_callback);
    roq_set_audio_decode_callback(roq, audio_callback);

    // Decode
    do {
        if(quit_cb())
            break;
	
        // Decode
        roq_decode(roq);
    } while (!roq_has_ended(roq));

    printf("DONE");

    // All done
    roq_destroy(roq);

    return 0;
}

