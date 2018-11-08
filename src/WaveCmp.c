/*
 * Converts a WAVE file into our proprietary compressed format.
 */

#include "WaveCmp.h"
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(1)
/////////////////////// WAVE File Stuff /////////////////////
// An IFF file header looks like this
typedef struct
{
	unsigned char	ID[4];	// could be {'R', 'I', 'F', 'F'} or {'F', 'O', 'R', 'M'}
	uint32_t			Length;	// Length of subsequent file (including remainder of header). This is in
							// Intel reverse byte order if RIFF, Motorola format if FORM.
	unsigned char	Type[4];	// {'W', 'A', 'V', 'E'} or {'A', 'I', 'F', 'F'}
} FILE_head;

// An IFF chunk header looks like this
typedef struct
{
	unsigned char	ID[4];	// 4 ascii chars that is the chunk ID
	uint32_t			Length;	// Length of subsequent data within this chunk. This is in Intel reverse byte
							// order if RIFF, Motorola format if FORM. Note: this doesn't include any
							// extra byte needed to pad the chunk out to an even size.
} CHUNK_head;

// WAVE fmt chunk
typedef struct {
	short			wFormatTag;
	unsigned short	wChannels;
	uint32_t			dwSamplesPerSec;
	uint32_t			dwAvgBytesPerSec;
	unsigned short	wBlockAlign;
	unsigned short	wBitsPerSample;
  // Note: there may be additional fields here, depending upon wFormatTag
} FORMAT;

typedef struct {
	FILE_head		head;
	CHUNK_head		fmt;
	FORMAT			fmtdata;
} WAVESTART;

// WAVE Sample Loop struct
typedef struct
{
	int32_t			dwIdentifier;
	int32_t			dwType;
	uint32_t			dwStart;
	uint32_t			dwEnd;
	int32_t			dwFraction;
	int32_t			dwPlayCount;
} SAMPLELOOP;

// WAVE Smpl chunk
typedef struct
{
	int32_t	dwManufacturer;
	int32_t	dwProduct;
	int32_t	dwSamplePeriod;
	int32_t	dwMIDIUnityNote;
	int32_t	dwMIDIPitchFraction;
	int32_t	dwSMPTEFormat;
	int32_t	dwSMPTEOffset;
	int32_t	cSampleLoops;
	int32_t	cbSamplerData;
} SAMPLER;

typedef struct {
  int32_t			dwIdentifier;
  uint32_t			dwPosition;
  unsigned char	fccChunk[4];
  uint32_t			dwChunkStart;
  uint32_t			dwBlockStart;
  uint32_t			dwSampleOffset;
} CUELOOP;

// Header on proprietary compressed format
typedef struct {
	uint32_t	DataLength;
	uint32_t	CompressPoint;
	uint32_t	LoopStart;
	uint32_t	LoopEnd;
	unsigned char	StereoFlag;
} DRUMBOXFILE;

#pragma pack()




// WAVE File ID strings
//static const unsigned char Riff[4]	= { 'R', 'I', 'F', 'F' };
//static const unsigned char Wave[4] = { 'W', 'A', 'V', 'E' };
//static const unsigned char Fmt[4] = { 'f', 'm', 't', ' ' };
static const unsigned char Cue[4] = { 'c', 'u', 'e', ' ' };
static const unsigned char Smpl[4] = { 's', 'm', 'p', 'l' };
static const unsigned char Data[4] = { 'd', 'a', 't', 'a' };   // "data" chunk of a WAVE file. The length of data
														// follows (uint32_t), and then the interleaved data

static const char ErrorMsgs[] = {'d','i','d','n','\'','t',' ','o','p','e','n',0,
'u','s','e','s',' ','u','n','s','u','p','p','o','r','t','e','d',' ','c','o','m','p','r','e','s','s','i','o','n',0,
'm','u','s','t',' ','b','e',' ','a',' ','m','o','n','o',' ','o','r',' ','s','t','e','r','e','o',' ','w','a','v','e',0,
'i','s',' ','t','o','o',' ','b','i','g',' ','t','o',' ','f','i','t',' ','i','n',' ','R','A','M',0,
'i','s','n','\'','t',' ','a',' ','W','A','V','E',' ','f','i','l','e',' ','f','o','r','m','a','t',0,
'c','a','n','\'','t',' ','b','e',' ','c','r','e','a','t','e','d',0,
'c','o','u','l','d','n','\'','t',' ','b','e',' ','c','o','m','p','l','e','t','e','l','y',' ','s','t','o','r','e','d',0,
0};

static const char WavExtension[] = ".wav";




/************************** fileCat() ***************************
 * Puts the specified extension on the specified filename,
 * replacing any old extension.
 *
 * pch =	File name to append the extension to. Must be in a
 *			buffer of PATH_MAX size.
 * pchCat=	Extension to append, including the '.'
 */

static void fileCat(char * pchName, const char * pchCat)
{
	register char * pch;

	pch = pchName + (strlen(pchName) - 1);

	// back up to '.' or '\\'
	while (*pch != '.')
	{
		if (*pch == '\\' || pch <= pchName)
		{
			// no extension, add one
			pch = pchName;
			break;
		}

		--pch;
	}

	strcpy(pch, pchCat);
}





static void format_errmsg(register char * pchName, register uint32_t errorNum)
{
	if (errorNum)
	{
		register char * pch;

		// back up to '\\'
		pch = pchName + strlen(pchName);
		while (--pch > pchName && *pch != '\\');
		if (*pch == '\\') ++pch;

		// Move fn to head of buffer
		while ((*pchName++ = *pch++));
		pchName[-1] = ' ';

		// Append err msg
		pch = (char *)&ErrorMsgs[0];
		while (--errorNum) pch += strlen(pch) + 1;
		while ((*pchName++ = *pch++));
		pchName[-1] = '!';
		pchName[0] = 0;
	}
}





/********************** compareID() *********************
 * Compares the passed ID str (ie, a ptr to 4 Ascii
 * bytes) with the ID at the passed ptr. Returns TRUE if
 * a match, FALSE if not.
 */

static unsigned char compareID(const unsigned char * id, unsigned char * ptr)
{
	register unsigned char i = 4;

	while (i--)
	{
		if ( *(id)++ != *(ptr)++ ) return 0;
	}
	return 1;
}





/******************** WaveCmpConvert() *******************
 * Converts a WAVE file to proprietary compressed format.
 */

uint32_t WaveCmpConvert(char * fn)
{
	WAVESTART				file;
	FILE_head				head;
	DRUMBOXFILE				drum;
	register char			*wavePtr;
	uint32_t					action;
	register int			inHandle;

	// Open the WAVE
	if ((inHandle = open(fn, O_RDONLY|O_NOATIME)) == -1)
	{
		// Error
		action = 1;
		goto out;
	}

	// No data yet read in. No error
	wavePtr = 0;
	drum.DataLength = 0;

	// Read in IFF File header and fmt chunk
	if (read(inHandle, &file, sizeof(WAVESTART)) == sizeof(WAVESTART))
	{
		// Can't handle compressed WAVE files
		if (file.fmtdata.wFormatTag != 1)
		{
			action = 2;
			goto out2;
		}

		// Only 16 or 24 bit allowed
		if (file.fmtdata.wBitsPerSample != 16 && file.fmtdata.wBitsPerSample != 24)
		{
//			message = _T("Must be a 16 or 24 bit WAVE!");
			goto badform;
		}

		// Only mono or stereo allowed
		if (file.fmtdata.wChannels > 2)
		{
badform:	action = 3;
			goto out2;
		}

		// Only 44100 sample rate allowed
//		if (file.fmtdata.dwSamplesPerSec != 44100)
//		{
//			action = ?;
//			goto out2;
//		}

		// Assume no loop
		drum.LoopStart = drum.LoopEnd = (uint32_t)-1;
		drum.StereoFlag = (file.fmtdata.wChannels == 2);

		// Skip any extra fmt bytes
		head.Length = file.fmt.Length - sizeof(FORMAT);
		goto skip;

		while (read(inHandle, &head, sizeof(CHUNK_head)) == sizeof(CHUNK_head))
		{
			// ============================ Is it a data chunk? ===============================
			if (compareID(&Data[0], &head.ID[0]))
			{
				// Size of wave data is head.Length. Allocate a buffer and read in the wave data
				if (!(wavePtr = (char *)malloc(head.Length << 1)))
				{
					action = 4;
					goto out2;
				}

				if (read(inHandle, wavePtr, head.Length) != head.Length) goto out3;

				drum.DataLength = head.Length;
			}

			// ============================ Is it an smpl chunk? ===============================
			else if (compareID(&Smpl[0], &head.ID[0]))
			{
				SAMPLER					smpl;
				SAMPLELOOP				smploop;

				// Read in SAMPLER
				if (read(inHandle, (unsigned char *)&smpl, sizeof(SAMPLER)) != sizeof(SAMPLER)) goto out3;
				head.Length -= sizeof(SAMPLER);

				// Use only the first loop
				if (smpl.cSampleLoops)
				{
					// Read in sample loop
					if (read(inHandle, (unsigned char *)&smploop, sizeof(SAMPLELOOP)) != sizeof(SAMPLELOOP)) goto out3;

					drum.LoopStart = smploop.dwStart;
					drum.LoopEnd = smploop.dwEnd;

					head.Length -= sizeof(SAMPLELOOP);
				}

				// Skip remainder of chunk
				goto skip;
			}

			// ============================ Is it a cue chunk? ===============================
			else if (compareID(&Cue[0], &head.ID[0]))
			{
				if (drum.LoopStart == (uint32_t)-1)
				{
					CUELOOP			cueloop;

					// Read in dwCuePoints
					if (read(inHandle, &action, sizeof(uint32_t)) != sizeof(uint32_t)) goto out3;
					head.Length -= sizeof(uint32_t);

					// Use only the first loop
					if (action)
					{
						// Read in cue loop
						if (read(inHandle, (unsigned char *)&cueloop, sizeof(CUELOOP)) != sizeof(CUELOOP)) goto out3;

						drum.LoopStart = cueloop.dwPosition * (file.fmtdata.wBitsPerSample == 16 ? 2 : 4);
						drum.LoopEnd = drum.DataLength /  file.fmtdata.wChannels;

						head.Length -= sizeof(CUELOOP);
					}
				}

				// Skip remainder of chunk
				goto skip;
			}

			// ============================ Skip this chunk ===============================
			else
			{
skip:			if (head.Length & 1) ++head.Length;  // If odd, round it up to account for pad byte
				lseek(inHandle, head.Length, SEEK_CUR);
			}
		}
	}

	// We got the data?
	if (drum.DataLength)
	{
		// Convert 24-bit to 16
		if (file.fmtdata.wBitsPerSample == 24)
		{
			register short	*				dataPtr;
			register unsigned char	*	srcPtr;
			register int32_t				pt;
#if 0
			register uint32_t				high;
#endif

#if 0
			srcPtr = (unsigned char *)wavePtr;
			high = 0;
			while ((char *)srcPtr < wavePtr + drum.DataLength)
			{
				pt = (((uint32_t)srcPtr[0] << 8) | (((uint32_t)srcPtr[1]) << 16) | (((uint32_t)srcPtr[2]) << 24));
				srcPtr += 3;
				if (pt < 0) pt = -pt;
				if ((uint32_t)pt > high) high = pt;
			}
			high = 0x7FFFFF00 - high;
#endif

			srcPtr = (unsigned char *)wavePtr;
			dataPtr = (short *)wavePtr;
			while ((char *)srcPtr < wavePtr + drum.DataLength)
			{
				pt = (((uint32_t)srcPtr[0] << 8) | (((uint32_t)srcPtr[1]) << 16) | (((uint32_t)srcPtr[2]) << 24));
				srcPtr += 3;
#if 0
				if (pt < 0) pt -= high;
				else pt += high;
#endif
				*dataPtr++ = (short)(pt >> 16);
			}

			drum.DataLength = (drum.DataLength / 3) * 2;
		}
#if 0
		else
		{
			register short	*			dataPtr;
			register short				pt;
			register unsigned short	high;

			// Normalize the file
			high = 0;
			dataPtr = (short *)wavePtr;
			while ((char *)dataPtr < wavePtr + drum.DataLength)
			{
				pt = *dataPtr++;
				if (pt < 0) pt = -pt;
				if ((unsigned short)pt > high) high = pt;
			}
			high = 0x7FFF - high;

			dataPtr = (short *)wavePtr;
			while ((char *)dataPtr < wavePtr + drum.DataLength)
			{
				pt = *dataPtr;
				if (pt < 0) pt -= high;
				else pt += high;
				*dataPtr++ = pt;
			}
		}
#endif
		// Analyze the data and see how much of the trailing 16-bit words can be converted to 8-bit bytes
		{
		register short	*dataPtr;
		register short	pt;

		dataPtr = (short *)(wavePtr + drum.DataLength);
		while ((char *)dataPtr > wavePtr)
		{
			pt = *(dataPtr - 1);
			if (pt > 127 || pt < -128) break;
			--dataPtr;
		}

		// For stereo, compress pt should be on frame boundary
		if (drum.StereoFlag && (((char *)dataPtr - (char *)wavePtr) & 2)) ++dataPtr;

		drum.CompressPoint = drum.DataLength - ((drum.DataLength + wavePtr) - (char *)dataPtr);
		}

		fileCat(fn, ".cmp");

		{
		register int		outHandle;

		if ((outHandle = open(fn, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
		{
			// Error
			action = 6;
		}
		else
		{
			drum.DataLength >>= 1;
			drum.CompressPoint >>= 1;
			if (drum.LoopStart != drum.LoopEnd && (drum.LoopStart << drum.StereoFlag) < drum.DataLength)
			{
				drum.LoopStart <<= drum.StereoFlag;
				drum.LoopEnd <<= drum.StereoFlag;
				if (drum.LoopEnd > drum.DataLength) drum.LoopEnd = drum.DataLength;
				if (drum.LoopStart > drum.LoopEnd)
				{
					register uint32_t	temp;

					temp = drum.LoopEnd;
					drum.LoopEnd = drum.LoopStart;
					drum.LoopStart = temp;
				}
			}

			switch (file.fmtdata.dwSamplesPerSec)
			{
				case 48000:
					drum.StereoFlag |= 0x10;
					break;
				case 88200:
					drum.StereoFlag |= 0x20;
					break;
				case 96000:
					drum.StereoFlag |= 0x30;
			}

			// Write the drum header
			if (write(outHandle, &drum, sizeof(DRUMBOXFILE)) != sizeof(DRUMBOXFILE))
badwrite:		action = 7;
			else
			{
				register uint32_t	offset;
				register short	pt;

				// Compress the data
				offset = action = drum.CompressPoint << 1;
				while (action < drum.DataLength << 1)
				{
					pt = *((short *)(wavePtr + action));
					*(wavePtr + offset) = (char)pt;
					action += 2;
					++offset;
				}

				// Write the compressed data
				if (write(outHandle, wavePtr, offset) != offset) goto badwrite;

				action = 0;
			}

			// Close the file
			close(outHandle);
		}
		}
	}
	else
out3:	action = 5;

out2:
	close(inHandle);

	// Free the loaded wave
	if (wavePtr) free(wavePtr);

out:
	format_errmsg(fn, action);

	return action;
}



uint32_t WaveCmpDir(char * path)
{
	uint32_t		result;
	register struct dirent *		dirEntryPtr;
	register uint32_t	size;
	register DIR *				dirp;

	// Open the dir for searching
	if (!(dirp = opendir(&path[0])))
	{
		result = 1;
		format_errmsg(path, result);
	}
	else
	{
		result = 0;

		size = strlen(path);
		path[size++] = '/';

		// Get next file/dir in the dir
		while (!result && (dirEntryPtr = readdir(dirp)))
		{
			register uint32_t	len;

			// Check that name ends in .wav and if so, assume it's a WAVE
			len = strlen(dirEntryPtr->d_name);
			if (len > 4 && !strcasecmp(&dirEntryPtr->d_name[len - 4], &WavExtension[0]))
			{
				strcpy(&path[size], dirEntryPtr->d_name);
				result = WaveCmpConvert(path);
			}
		}

		closedir(dirp);
	}
	return result;
}
