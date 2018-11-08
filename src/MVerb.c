// Copyright (c) 2010 Martin Eastwood
// This code is distributed under the terms of the GNU General Public License

// MVerb is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option, any later version.
//
// MVerb is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this MVerb. If not, see <http://www.gnu.org/licenses/>.
//
// This is the C code version, derived from the original C++ code (with
// some minor design changes to minimize code and memory usage), by Jeff
// Glatt.

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>

//#define REV_DBL_SUPPORT

// ============================== Filter ==============================

#define FILTER_OVERSAMPLECOUNT		4

#pragma pack(1)

struct AUDIO_FILTER
{
	float		Low, High, Band /* , Notch */;
	float		SampleRate;
	float		Frequency;
	float		Q;
	float		F;
};

// ============================== Delay =============================

struct AUDIO_DELAY
{
	float *			Buffer;
	unsigned int	Length;
	float			Feedback;
	unsigned int	NumOfTaps;
	unsigned int	Index[1];
};


struct AUDIO_DELAY3
{
	float *			Buffer;
	unsigned int	Length;
	float			Feedback;
	unsigned int	NumOfTaps;
	unsigned int	Index[3];
};

struct AUDIO_DELAY4
{
	float *			Buffer;
	unsigned int	Length;
	float			Feedback;
	unsigned int	NumOfTaps;
	unsigned int	Index[4];
};

struct AUDIO_DELAY8
{
	float *			Buffer;
	unsigned int	Length;
	float			Feedback;
	unsigned int	NumOfTaps;
	unsigned int	Index[8];
};

// ============================= Reverb =============================

#define REVSIZE_PREDELAY	60000

struct REVERB
{
	struct AUDIO_DELAY8		EarlyReflectionsDelay[2];
	struct AUDIO_FILTER		BandwidthFilter[2];
	struct AUDIO_FILTER		DampingFilter[2];
	struct AUDIO_DELAY		PreDelay;
	struct AUDIO_DELAY		Allpass[6];
	struct AUDIO_DELAY3		Allpass3Tap[2];
	struct AUDIO_DELAY4		StaticDelay[4];
	char *					BufferPtr;
	float					SampleRate;
	float					/* Gain, */ Size, DampingFreq, Density1, BandwidthFreq, PreDelayTime, Decay, RevLevel, EarlyLate;
	float					Density2, EarlyLateSmooth, BandwidthSmooth, DampingSmooth, PredelaySmooth, SizeSmooth, DensitySmooth, DecaySmooth, PreviousLeftTank, PreviousRightTank;
	unsigned int			BufferSize;
	unsigned char			SampleType;
	unsigned char			ControlRate, ControlRateCounter;
};

#pragma pack()

#include "MVerb.h"

// ============================== Filter ==============================

static float FilterProc(register struct AUDIO_FILTER * filter, register float input)
{
	register unsigned char i;

	for (i = 0; i < FILTER_OVERSAMPLECOUNT; i++)
	{
		filter->Low += (filter->F * filter->Band) + 1e-25;
		filter->High = input - filter->Low - filter->Q * filter->Band;
		filter->Band += filter->F * filter->High;
//		filter->Notch = filter->Low + filter->High;
	}

	return filter->Low;
}

static void FilterReset(register struct AUDIO_FILTER * filter)
{
	filter->Low = filter->High = filter->Band = /* filter->Notch = */ 0;
}

static void FilterUpdateCoefficient(register struct AUDIO_FILTER * filter)
{
	filter->F = 2. * sin(3.141592654 * filter->Frequency / filter->SampleRate);
}

static void FilterSetSampleRate(register struct AUDIO_FILTER * filter, register float rate)
{
	filter->SampleRate = rate * FILTER_OVERSAMPLECOUNT;
	FilterUpdateCoefficient(filter);
}

static void FilterFrequency(register struct AUDIO_FILTER * filter, register float freq)
{
	filter->Frequency = freq;
	FilterUpdateCoefficient(filter);
}

//static void FilterResonance(register struct AUDIO_FILTER * filter, register float resonance)
//{
//	filter->Q = 2.f - (2.f * resonance);
//}

static void FilterInit(register struct AUDIO_FILTER * filter)
{
//	filter->SampleRate = 44100.f * FILTER_OVERSAMPLECOUNT;
	filter->Frequency = 1000.f;
	filter->Q = 2.f;
//	FilterUpdateCoefficient(filter);
	FilterReset(filter);
}












// ============================== Delay =============================

// For "Static" filters without feedback
static float DelayProc(register struct AUDIO_DELAY * delay, register float input)
{
	register float output;

	output = delay->Buffer[delay->Index[0]];
	delay->Buffer[delay->Index[0]] = input;

	{
	register unsigned int	i;

	for (i=0; i < delay->NumOfTaps; i++)
	{
		if (++delay->Index[i] >= delay->Length) delay->Index[i] = 0;
	}
	}

	return output;
}

// For "All Pass" filters with feedback
static float DelayAllpassProc(register struct AUDIO_DELAY * delay, register float input)
{
	register float output;
	{
	register float bufout;
	register float temp;

	bufout = delay->Buffer[delay->Index[0]];
	temp = input * (-delay->Feedback);
	output = bufout + temp;
	delay->Buffer[delay->Index[0]] = input + ((bufout + temp) * delay->Feedback);
	}

	{
	register unsigned int	i;

	for (i=0; i < delay->NumOfTaps; i++)
	{
		if (++delay->Index[i] >= delay->Length) delay->Index[i] = 0;
	}
	}

	return output;
}

static float DelayGetIndex(register struct AUDIO_DELAY * delay, register unsigned int which)
{
	return delay->Buffer[delay->Index[which]];
}

static void DelayClear(register struct AUDIO_DELAY * delay)
{
#ifdef WIN32
//	ZeroMemory(delay->Buffer, delay->Length * sizeof(float));
#else
//	memset(delay->Buffer, 0, delay->Length * sizeof(float));
#endif
	{
	register unsigned int	i;

	for (i=0; i < delay->NumOfTaps; i++) delay->Index[i] = 0;
	}
}

//static void DelayInit(register struct AUDIO_DELAY * delay, register unsigned int numOfTaps)
//{
//	delay->NumOfTaps = numOfTaps;
//	delay->Length = 0;
//	DelayClear(delay);
//	delay->Feedback = 0.5;
//}








// ============================= Reverb =============================

/****************** ReverbProcess() *****************
 * Adds reverb to the waveform data in the stereo
 * input buffer, and stores the new data in the
 * stereo output buffer.
 *
 * input =			Pointer to input interleaved input buffer.
 * output =			Pointer to interleaved output buffer.
 * sampleFrames =	How many sample frames (not bytes)
 *					to process. For example, 16 frames
 *					of stereo 32-bit data means 128 bytes
 *					(16 frames * 2 channels * sizeof(uint32_t)).
 *
 * NOTES: "inputs" and "outputs" can both point to the
 * same buffer if desired. This means the original waveform
 * data is modified.
 *
 * Output buffer must be big enough for the processed data.
 */

void ReverbProcess(REVERBHANDLE revHandle, const float * input, float * output, uint32_t sampleFrames)
{
	register struct REVERB * rev;
	float			earlyLateDelta;
	float			bandwidthDelta;
	float			dampingDelta;
	float			predelayDelta;
	float			sizeDelta;
	float			decayDelta;
	float			densityDelta;

	rev = (struct REVERB *)revHandle;
	{
	register float	oneOverSampleFrames;

	oneOverSampleFrames = 1.f / sampleFrames;
	earlyLateDelta = (rev->EarlyLate - rev->EarlyLateSmooth) * oneOverSampleFrames;
	bandwidthDelta = (((rev->BandwidthFreq * 18400.f) + 100.f) - rev->BandwidthSmooth) * oneOverSampleFrames;
	dampingDelta = (((rev->DampingFreq * 18400.f) + 100.f) - rev->DampingSmooth) * oneOverSampleFrames;
	predelayDelta = ((rev->PreDelayTime * 200.f * (rev->SampleRate / 1000.f)) - rev->PredelaySmooth) * oneOverSampleFrames;
	sizeDelta = (rev->Size - rev->SizeSmooth) * oneOverSampleFrames;
	decayDelta = (((0.7995f * rev->Decay) + 0.005f) - rev->DecaySmooth) * oneOverSampleFrames;
	densityDelta = (((0.7995f * rev->Density1) + 0.005f) - rev->DensitySmooth) * oneOverSampleFrames;
	}

	do
	{
		float	left, right;
		float	smearedInput;
		float	earlyReflectionsL, earlyReflectionsR;

		if (rev->BufferPtr)
		{
			left = *input++;
			right = *input++;

			rev->EarlyLateSmooth += earlyLateDelta;
			rev->BandwidthSmooth += bandwidthDelta;
			rev->DampingSmooth += dampingDelta;
			rev->PredelaySmooth += predelayDelta;
			rev->SizeSmooth += sizeDelta;
			rev->DecaySmooth += decayDelta;
			rev->DensitySmooth += densityDelta;

			if (rev->ControlRateCounter >= rev->ControlRate)
			{
				rev->ControlRateCounter = 0;
				FilterFrequency(&rev->BandwidthFilter[0], rev->BandwidthSmooth);
				FilterFrequency(&rev->BandwidthFilter[1], rev->BandwidthSmooth);
				FilterFrequency(&rev->DampingFilter[0], rev->DampingSmooth);
				FilterFrequency(&rev->DampingFilter[1], rev->DampingSmooth);
			}
			++rev->ControlRateCounter;

			if (rev->PredelaySmooth > REVSIZE_PREDELAY)
				rev->PredelaySmooth = REVSIZE_PREDELAY;
			rev->PreDelay.Length = (unsigned int)rev->PredelaySmooth;
			rev->Density2 = rev->DecaySmooth + 0.15f;
			if (rev->Density2 > 0.5f) rev->Density2 = 0.5f;
			if (rev->Density2 < 0.25f) rev->Density2 = 0.25f;
			rev->Allpass[4].Feedback = rev->Allpass[5].Feedback = rev->Density1;
			rev->Allpass3Tap[0].Feedback = rev->Allpass3Tap[1].Feedback = rev->Density2;

			{
			register float				bandwidthLeft, bandwidthRight;
			register float *			buffer;
			register unsigned int *	index;

			bandwidthLeft = FilterProc(&rev->BandwidthFilter[0], left);
			bandwidthRight = FilterProc(&rev->BandwidthFilter[1], right);
			buffer = rev->EarlyReflectionsDelay[0].Buffer;
			index = &rev->EarlyReflectionsDelay[0].Index[0];
			earlyReflectionsL = DelayProc((struct AUDIO_DELAY *)&rev->EarlyReflectionsDelay[0], bandwidthLeft * 0.5f + bandwidthRight * 0.3f)
								+ buffer[index[2]] * 0.6f
								+ buffer[index[3]] * 0.4f
								+ buffer[index[4]] * 0.3f
								+ buffer[index[5]] * 0.3f
								+ buffer[index[6]] * 0.1f
								+ buffer[index[7]] * 0.1f
								+ ( bandwidthLeft * 0.4f + bandwidthRight * 0.2f) * 0.5f;
			buffer = rev->EarlyReflectionsDelay[1].Buffer;
			index = &rev->EarlyReflectionsDelay[1].Index[0];
			earlyReflectionsR = DelayProc((struct AUDIO_DELAY *)&rev->EarlyReflectionsDelay[1], bandwidthLeft * 0.3f + bandwidthRight * 0.5f)
								+ buffer[index[2]] * 0.6f
								+ buffer[index[3]] * 0.4f
								+ buffer[index[4]] * 0.3f
								+ buffer[index[5]] * 0.3f
								+ buffer[index[6]] * 0.1f
								+ buffer[index[7]] * 0.1f
								+ ( bandwidthLeft * 0.2f + bandwidthRight * 0.4f) * 0.5f;
			smearedInput = DelayProc(&rev->PreDelay, (bandwidthRight + bandwidthLeft) * 0.5f);
			}

			{
			register unsigned int		j;

			for (j=0; j < 4;j++) smearedInput = DelayAllpassProc(&rev->Allpass[j], smearedInput);
			}

			{
			register float leftTank, rightTank;

			leftTank = DelayAllpassProc((struct AUDIO_DELAY *)&rev->Allpass[4], smearedInput + rev->PreviousRightTank);
			rightTank = DelayAllpassProc((struct AUDIO_DELAY *)&rev->Allpass[5], smearedInput + rev->PreviousLeftTank);
			leftTank = DelayProc((struct AUDIO_DELAY *)&rev->StaticDelay[0], leftTank);
			leftTank = FilterProc(&rev->DampingFilter[0], leftTank);
			leftTank = DelayAllpassProc((struct AUDIO_DELAY *)&rev->Allpass3Tap[0], leftTank);
			rev->PreviousLeftTank = DelayProc((struct AUDIO_DELAY *)&rev->StaticDelay[1], leftTank) * rev->DecaySmooth;
			rightTank = DelayProc((struct AUDIO_DELAY *)&rev->StaticDelay[2], rightTank);
			rightTank = FilterProc(&rev->DampingFilter[1], rightTank);
			rightTank = DelayAllpassProc((struct AUDIO_DELAY *)&rev->Allpass3Tap[1], rightTank);
			rev->PreviousRightTank = DelayProc((struct AUDIO_DELAY *)&rev->StaticDelay[3], rightTank) * rev->DecaySmooth;
			}

			{
			register float accumulatorL, accumulatorR;
			register float factor;

			factor = 0.6f;
			accumulatorL =    (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[2], 1))
							+ (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[2], 2))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->Allpass3Tap[1], 1))
							+ (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[3], 1))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[0], 1))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->Allpass3Tap[0], 1))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[1], 1));
			accumulatorR =    (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[0], 2))
							+ (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[0], 3))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->Allpass3Tap[0], 2))
							+ (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[1], 2))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[2], 3))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->Allpass3Tap[1], 2))
							- (factor * DelayGetIndex((struct AUDIO_DELAY *)&rev->StaticDelay[3], 2));
			left = rev->RevLevel * (((accumulatorL * rev->EarlyLate) + ((1.f - rev->EarlyLate) * earlyReflectionsL)) - left);
			right = rev->RevLevel * (((accumulatorR * rev->EarlyLate) + ((1.f - rev->EarlyLate) * earlyReflectionsR)) - right);
			}

			*(output++) += left;
			*(output++) += right;
		}
	} while (--sampleFrames);
}

static void getNewRevLengths(register struct REVERB * rev, register unsigned int * lengths)
{
	register float	factor;

	factor = rev->SampleRate * rev->Size;
	*(lengths)++ = (unsigned int)(0.020f * factor);	// rev->Allpass[4].Length
	*(lengths)++ = (unsigned int)(0.030f * factor);	// rev->Allpass[5].Length
	*(lengths)++ = (unsigned int)(0.060f * factor);	// rev->Allpass3Tap[0].Length
	*(lengths)++ = (unsigned int)(0.089f * factor);	// rev->Allpass3Tap[1].Length
	*(lengths)++ = (unsigned int)(0.15f * factor);	// rev->StaticDelay[0].Length
	*(lengths)++ = (unsigned int)(0.12f * factor);	// rev->StaticDelay[1].Length
	*(lengths)++ = (unsigned int)(0.14f * factor);	// rev->StaticDelay[2].Length
	*lengths = (unsigned int)(0.11f * factor);		// rev->StaticDelay[3].Length
}

static void setNewRevLengths(register struct REVERB * rev, register unsigned int * lengths)
{
	register float			factor;
	register unsigned int	j;

	for (j=4; j < 6; j++)
	{
		rev->Allpass[j].Length = *(lengths)++;
		DelayClear((struct AUDIO_DELAY *)&rev->Allpass[j]);
	}
	for (j=0; j < 2; j++)
	{
		rev->Allpass3Tap[j].Length = *(lengths)++;
		DelayClear((struct AUDIO_DELAY *)&rev->Allpass3Tap[j]);
	}
	for (j=0; j < 4; j++)
	{
		rev->StaticDelay[j].Length = *(lengths)++;
		DelayClear((struct AUDIO_DELAY *)&rev->StaticDelay[j]);
	}

	factor = rev->SampleRate * rev->Size;

	rev->Allpass3Tap[0].Index[1] = (unsigned int)(0.006f * factor);
	rev->Allpass3Tap[0].Index[2] = (unsigned int)(0.041f * factor);
	rev->Allpass3Tap[1].Index[1] = (unsigned int)(0.031f * factor);
	rev->Allpass3Tap[1].Index[2] = (unsigned int)(0.011f * factor);

	rev->StaticDelay[0].Index[1] = (unsigned int)(0.067f * factor);
	rev->StaticDelay[0].Index[2] = (unsigned int)(0.011f * factor);
	rev->StaticDelay[0].Index[3] = (unsigned int)(0.121f * factor);
	rev->StaticDelay[1].Index[1] = (unsigned int)(0.036f * factor);
	rev->StaticDelay[1].Index[2] = (unsigned int)(0.089f * factor);
	rev->StaticDelay[2].Index[1] = (unsigned int)(0.0089f * factor);
	rev->StaticDelay[2].Index[2] = (unsigned int)(0.099f * factor);
	rev->StaticDelay[3].Index[1] = (unsigned int)(0.067f * factor);
	rev->StaticDelay[3].Index[2] = (unsigned int)(0.0041f * factor);
}

static REV_ERROR allocRevBuffers(register struct REVERB * rev, register unsigned int * lengths)
{
	register unsigned long	totalLen;
	register unsigned int	j;
	register char *			buffer;
	register char *			temp;

	// Calc how many floats we need to store
	totalLen = REVSIZE_PREDELAY;
	for (j=0; j < 14; j++) totalLen += lengths[j];

	// Do we need a larger buffer? If so, malloc it, with a little extra room to minimize future realloc
	if (totalLen > rev->BufferSize)
	{
#ifdef WIN32
		if (!(buffer = GlobalAlloc(GMEM_FIXED, (totalLen + 1000) * sizeof(float)))) return GetLastError();
#else
		if (!(buffer = (char *)malloc((totalLen + 1000) * sizeof(float)))) return errno;
#endif
		rev->BufferSize = totalLen + 1000;

		// Clear the buffer(s)
#ifdef WIN32
		ZeroMemory(buffer, totalLen * sizeof(float));
#else
		memset(buffer, 0, totalLen * sizeof(float));
#endif
		// Update all ptrs to buffers
		temp = buffer;
		rev->PreDelay.Buffer = (float *)temp;
		buffer += (REVSIZE_PREDELAY * sizeof(float));
		for (j=0; j < 2; j++)
		{
			rev->EarlyReflectionsDelay[j].Buffer = (float *)buffer;
			buffer += (*(lengths)++ * sizeof(float));
		}
		for (j=0; j < 6; j++)
		{
			rev->Allpass[j].Buffer = (float *)buffer;
			buffer += (*(lengths)++ * sizeof(float));
		}
		for (j=0; j < 2; j++)
		{
			rev->Allpass3Tap[j].Buffer = (float *)buffer;
			buffer += (*(lengths)++ * sizeof(float));
		}
		for (j=0; j < 4; j++)
		{
			rev->StaticDelay[j].Buffer = (float *)buffer;
			buffer += (*(lengths)++ * sizeof(float));
		}

#ifdef WIN32
		if (rev->BufferPtr) GlobalFree(rev->BufferPtr);
#else
		if (rev->BufferPtr) free(rev->BufferPtr);
#endif
		rev->BufferPtr = temp;
	}

	return 0;
}

/******************* ReverbReset() ******************
 * Resets the struct REVERB to default values.
 *
 * RETURNS: 0 if success, or an error number.
 */

REV_ERROR ReverbReset(REVERBHANDLE revHandle)
{
	register float				rate;
	register unsigned int		j;
	register REV_ERROR			err;
	unsigned int				lengths[14];
	register struct REVERB *	rev;

	rev = (struct REVERB *)revHandle;

	rate = rev->SampleRate;

	lengths[0] = (unsigned int)(0.089f * rate);		// rev->EarlyReflectionsDelay[0].Length
	lengths[1] = (unsigned int)(0.069f * rate);		// rev->EarlyReflectionsDelay[1].Length
	lengths[2] = (unsigned int)(0.0048f * rate);	// rev->Allpass[0].Length
	lengths[3] = (unsigned int)(0.0036f * rate);	// rev->Allpass[1].Length
	lengths[4] = (unsigned int)(0.0127f * rate);	// rev->Allpass[2].Length
	lengths[5] = (unsigned int)(0.0093f * rate);	// rev->Allpass[3].Length
	getNewRevLengths(rev, &lengths[6]);
	if (!(err = allocRevBuffers(rev, &lengths[0])))
	{
		rev->ControlRateCounter = 0;

		for (j=0; j < 2; j++)
		{
			FilterReset(&rev->BandwidthFilter[j]);
			FilterReset(&rev->DampingFilter[j]);
			FilterSetSampleRate(&rev->BandwidthFilter[j], rate);
			FilterSetSampleRate(&rev->DampingFilter[j], rate);
			rev->EarlyReflectionsDelay[j].Length = lengths[j];
			DelayClear((struct AUDIO_DELAY *)&rev->EarlyReflectionsDelay[j]);
		}

		for (j=0; j < 4; j++)
		{
			rev->Allpass[j].Length = lengths[j+2];
			DelayClear((struct AUDIO_DELAY *)&rev->Allpass[j]);
		}

		rev->PreDelay.Length = (unsigned int)rev->PreDelayTime;
		DelayClear(&rev->PreDelay);

		setNewRevLengths(rev, &lengths[6]);

		rev->EarlyReflectionsDelay[0].Index[1] = (unsigned int)(0.0199f * rate);
		rev->EarlyReflectionsDelay[0].Index[2] = (unsigned int)(0.0219f * rate);
		rev->EarlyReflectionsDelay[0].Index[3] = (unsigned int)(0.0354f * rate);
		rev->EarlyReflectionsDelay[0].Index[4] = (unsigned int)(0.0389f * rate);
		rev->EarlyReflectionsDelay[0].Index[5] = (unsigned int)(0.0414f * rate);
		rev->EarlyReflectionsDelay[0].Index[6] = (unsigned int)(0.0692f * rate);
		rev->EarlyReflectionsDelay[1].Index[1] = (unsigned int)(0.0099f * rate);
		rev->EarlyReflectionsDelay[1].Index[2] = (unsigned int)(0.011f * rate);
		rev->EarlyReflectionsDelay[1].Index[3] = (unsigned int)(0.0182f * rate);
		rev->EarlyReflectionsDelay[1].Index[4] = (unsigned int)(0.0189f * rate);
		rev->EarlyReflectionsDelay[1].Index[5] = (unsigned int)(0.0213f * rate);
		rev->EarlyReflectionsDelay[1].Index[6] = (unsigned int)(0.0431f * rate);

		rev->Allpass[4].Feedback = rev->Allpass[5].Feedback = rev->Density1;
		rev->Allpass3Tap[0].Feedback = rev->Allpass3Tap[1].Feedback = rev->Density2;
	}

	return err;
}

/****************** ReverbSetParams() ****************
 * Sets the current value of the specified parameter.
 *
 * flags =	Which params to set. OR'ed bitmask of the
 *			REVPARAM_ #defines.
 * values =	An array containing the new values.
 *
 * RETURNS: 0 if success, or an error number.
 */

REV_ERROR ReverbSetParams(REVERBHANDLE revHandle, unsigned int flags, unsigned int * values)
{
	register unsigned int		i;
	register struct REVERB *	rev;

	rev = (struct REVERB *)revHandle;

	if (flags & REVPARAM_RATEMASK)
	{
		register float rate;

		rate = (float)(*values);

		if (rev->SampleRate == rate) flags &= ~REVPARAM_RATEMASK;
		rev->SampleRate = rate;
		rev->ControlRate = (unsigned char)(*values / 1000);
		++values;
	}

	for (i = REVPARAM_SIZE; i < REV_NUM_PARAMS; i++)
	{
		if (flags & (0x00000001 << i))
		{
			register float value;

			value = (float)(*values++) / 100.f;
			switch (i)
			{
				case REVPARAM_SIZE:
				{
					if (rev->Size == value) flags &= ~REVPARAM_SIZEMASK;
					rev->Size = (0.95f * value) + 0.05f;
					break;
				}
//				case REVPARAM_GAIN:
//					rev->Gain = value;
//					break;
				case REVPARAM_DAMPINGFREQ:
					rev->DampingFreq =  1.f - value;
					break;
				case REVPARAM_DENSITY:
					rev->Density1 = value;
					break;
				case REVPARAM_BANDWIDTHFREQ:
					rev->BandwidthFreq = value;
					break;
				case REVPARAM_DECAY:
					rev->Decay = value;
					break;
				case REVPARAM_PREDELAY:
					rev->PreDelayTime = value;
					break;
				case REVPARAM_REVLEVEL:
					rev->RevLevel = value;
					break;
				case REVPARAM_EARLYLATE:
					rev->EarlyLate = value;
			}
		}
	}

	if ((flags & REVPARAM_RATEMASK) || !rev->BufferPtr)
		return ReverbReset(rev);
	if (flags & REVPARAM_SIZEMASK)
	{
		register REV_ERROR	err;
		unsigned int		lengths[14];

		lengths[0] = rev->EarlyReflectionsDelay[0].Length;
		lengths[1] = rev->EarlyReflectionsDelay[1].Length;
		lengths[2] = rev->Allpass[0].Length;
		lengths[3] = rev->Allpass[1].Length;
		lengths[4] = rev->Allpass[2].Length;
		lengths[5] = rev->Allpass[3].Length;
		getNewRevLengths(rev, &lengths[6]);
		if (!(err = allocRevBuffers(rev, &lengths[0])))
			setNewRevLengths(rev, &lengths[6]);
		return err;
	}

	return 0;
}

/****************** ReverbGetParams() ****************
 * Gets the current values of the specified parameters.
 *
 * flags =	Which params to get. OR'ed bitmask of the
 *			REVPARAM_ #defines.
 * values =	An array where the values are stored.
 */

void ReverbGetParams(REVERBHANDLE revHandle, unsigned int flags, unsigned int * values)
{
	register unsigned int		i;
	register struct REVERB *	rev;

	rev = (struct REVERB *)revHandle;

	if (flags & REVPARAM_RATEMASK)
		*values++ = (unsigned int)rev->SampleRate;

	i = 0x00000001 << REVPARAM_SIZE;
	while (flags)
	{
		if (flags & i)
		{
			register float value;

			switch (i)
			{
				case REVPARAM_SIZEMASK:
					value = rev->Size;
					break;
//				case REVPARAM_GAINMASK:
//					value = rev->Gain;
//					break;
				case REVPARAM_DAMPINGFREQMASK:
					value = (1.f - rev->DampingFreq);
					break;
				case REVPARAM_DENSITYMASK:
					value = rev->Density1;
					break;
				case REVPARAM_BANDWIDTHFREQMASK:
					value = rev->BandwidthFreq;
					break;
				case REVPARAM_DECAYMASK:
					value = rev->Decay;
					break;
				case REVPARAM_PREDELAYMASK:
					value = rev->PreDelayTime;
					break;
				case REVPARAM_REVLEVELMASK:
					value = rev->RevLevel;
					break;
				case REVPARAM_EARLYLATEMASK:
					value = rev->EarlyLate;
					break;
				default:
					value = 0.f;
			}

			*values++ = (unsigned int)(value * 100.f);
			flags &= ~i;
		}

		i <<= 1;
	}
}


/******************** ReverbAlloc() ******************
 * Allocs/initializes a struct REVERB.
 */

REVERBHANDLE ReverbAlloc(void)
{
	register struct REVERB *	rev;

#ifdef WIN32
	if ((rev = GlobalAlloc(GMEM_FIXED, sizeof(struct REVERB))))
#else
	if ((rev = malloc(sizeof(struct REVERB))))
#endif
	{

#ifdef WIN32
		ZeroMemory(rev, sizeof(struct REVERB));
#else
		memset(rev, 0, sizeof(struct REVERB));
#endif
		{
		register unsigned int		j;

		for (j=0; j < 2; j++)
		{
			FilterInit(&rev->DampingFilter[j]);
			FilterInit(&rev->BandwidthFilter[j]);
			rev->EarlyReflectionsDelay[j].NumOfTaps = 8;
			rev->Allpass3Tap[j].NumOfTaps = 3;
			rev->Allpass[j+4].Feedback = rev->Allpass3Tap[j].Feedback = 0.5;
		}
		}

		rev->PreDelay.NumOfTaps = 1;

		{
		register unsigned int		j;

		for (j=0; j < 4; j++)
		{
			rev->StaticDelay[j].NumOfTaps = 4;
		}
		}

		{
		register unsigned int		j;

		for (j=0; j < 6; j++)
		{
			rev->Allpass[j].NumOfTaps = 1;
		}
		}

		rev->Allpass[0].Feedback = rev->Allpass[1].Feedback = 0.75f;
		rev->Allpass[2].Feedback = rev->Allpass[3].Feedback = 0.625f;

//		rev->BandwidthFreq = rev->DampingFreq = 0.5f;
//		rev->Density1 = rev->EarlyLate = rev->PreDelayTime = .25f;
//		rev->RevLevel = rev->Size = rev->Decay = .3f;

		rev->BandwidthFreq = 0.4150f;
		rev->DampingFreq = 0.4349f;
		rev->Density1 = 0.1149f;
		rev->EarlyLate = 0.2999f;
		rev->PreDelayTime = .1599f;
		rev->RevLevel = 0.5f;
		rev->Size = 0.4720f;
		rev->Decay = 0.5050f;
//		rev->Gain = 1.f;
		rev->SampleRate = 44100;
		rev->ControlRate = (unsigned char)(44100 / 1000);
//		rev->SampleType = REVTYPE_16BIT;
	}

	return rev;
}

/******************* ReverbFree() *****************
 * Frees a struct REVERB's resources.
 */

void ReverbFree(REVERBHANDLE revHandle)
{
	register struct REVERB *	rev;

	if ((rev = (struct REVERB *)revHandle))
	{
#ifdef WIN32
		if (rev->BufferPtr) GlobalFree(rev->BufferPtr);
		GlobalFree(rev);
#else
		if (rev->BufferPtr) free(rev->BufferPtr);
		free(rev);
#endif
	}
}

#ifdef __cplusplus
}
#endif
