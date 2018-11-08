#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <asm/byteorder.h>
#include <libintl.h>
#include <endian.h>
#include <byteswap.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define COMPOSE_ID(a,b,c,d)	((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define LE_SHORT(v)		(v)
#define LE_INT(v)		(v)
#define BE_SHORT(v)		bswap_16(v)
#define BE_INT(v)		bswap_32(v)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define COMPOSE_ID(a,b,c,d)	((d) | ((c)<<8) | ((b)<<16) | ((a)<<24))
#define LE_SHORT(v)		bswap_16(v)
#define LE_INT(v)		bswap_32(v)
#define BE_SHORT(v)		(v)
#define BE_INT(v)		(v)
#else
#error "Wrong endian"
#endif

#define WAV_RIFF		COMPOSE_ID('R','I','F','F')
#define WAV_WAVE		COMPOSE_ID('W','A','V','E')
#define WAV_FMT		COMPOSE_ID('f','m','t',' ')
#define WAV_DATA		COMPOSE_ID('d','a','t','a')

#define WAV_FMT_PCM             0x0001
#define WAV_FMT_IEEE_FLOAT      0x0003

#pragma pack(1)
typedef struct {
	u_int magic;
	u_int length;
	u_int type;
} WaveHeader;

typedef struct {
	u_short format;
	u_short channels;
	u_int sample_fq;
	u_int byte_p_sec;
	u_short byte_p_spl;
	u_short bit_p_spl;
} WaveFmtBody;

typedef struct {
	u_int type;
	u_int length;
} WaveChunkHeader;
#pragma pack()


static const char 			WaveFilename[] = "test.wav";





#define MAX_RECORD_BYTES	4*2*200000

static uint32_t		WaveRecordCount;
static char *			WaveRecordBuffer = 0;

static void startWaveRecord(void)
{
	if (!WaveRecordBuffer)
	{
		WaveRecordCount = 0;
		WaveRecordBuffer = malloc(MAX_RECORD_BYTES);
	}
}

static void runWaveRecord(const char *ptr, uint32_t size)
{
	if (WaveRecordBuffer)
	{
		if (size > MAX_RECORD_BYTES - WaveRecordCount) size = MAX_RECORD_BYTES - WaveRecordCount;
		memcpy(&WaveRecordBuffer[WaveRecordCount], ptr, size);
		WaveRecordCount += size;
	}
}

static void stopWaveRecord(void)
{
	if (WaveRecordBuffer)
	{
		register int	waveHandle;
		char path[PATH_MAX];

		strcpy(&path[get_exe_path(&path[0])], "test.wav");

		// open a new file
		if ((waveHandle = open64(path, O_WRONLY | O_CREAT, 0644)) == -1)
			perror(path);
		else
		{
			// Write WAVE header
			WaveHeader			h;
			WaveFmtBody			f;
			WaveChunkHeader	cf, cd;

			h.magic = WAV_RIFF;
			h.length = LE_INT(WaveRecordCount + sizeof(WaveHeader) + sizeof(WaveChunkHeader) + sizeof(WaveFmtBody) + sizeof(WaveChunkHeader) - 8);
			h.type = WAV_WAVE;
			cf.type = WAV_FMT;
			cf.length = LE_INT(16);
			f.format = LE_SHORT(WAV_FMT_PCM);
			f.channels = LE_SHORT(2);
			f.sample_fq = LE_INT(44100);
			f.byte_p_spl = LE_SHORT(8);
			f.byte_p_sec = LE_INT(8 * 44100);
			f.bit_p_spl = LE_SHORT(32);
			cd.type = WAV_DATA;
			cd.length = LE_INT(WaveRecordCount);

			write(waveHandle, &h, sizeof(WaveHeader));
			write(waveHandle, &cf, sizeof(WaveChunkHeader));
			write(waveHandle, &f, sizeof(WaveFmtBody));
			write(waveHandle, &cd, sizeof(WaveChunkHeader));

			write(waveHandle, WaveRecordBuffer, WaveRecordCount);

			close(waveHandle);
		}
	}
}

static void freeWaveRecord(void)
{
	if (WaveRecordBuffer) free(WaveRecordBuffer);
	WaveRecordBuffer = 0;
}

