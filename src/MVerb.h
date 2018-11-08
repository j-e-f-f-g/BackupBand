//	Copyright (c) 2010 Martin Eastwood
//  This code is distributed under the terms of the GNU General Public License

//  MVerb is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  at your option) any later version.
//
//  MVerb is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this MVerb. If not, see <http://www.gnu.org/licenses/>.

#ifndef EMVERB_H
#define EMVERB_H

#ifdef WIN32
#include <windows.h>
#ifndef int32_t
#define int32_t long
typedef DWORD REV_ERROR;
#endif
#else
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
typedef int REV_ERROR;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* For ReverbSetParams()/ReverbGetParams() flags */
#define REVPARAM_RATEMASK			0x00000001
#define REVPARAM_FORMATMASK			0x00000002
#define REVPARAM_SIZEMASK			0x00000004
#define REVPARAM_GAINMASK			0x00000008
#define REVPARAM_DAMPINGFREQMASK	0x00000010
#define REVPARAM_DENSITYMASK		0x00000020
#define REVPARAM_BANDWIDTHFREQMASK	0x00000040
#define REVPARAM_DECAYMASK			0x00000080
#define REVPARAM_PREDELAYMASK		0x00000100
#define REVPARAM_REVLEVELMASK		0x00000200
#define REVPARAM_EARLYLATEMASK		0x00000400
#define REVERB_ALL_PARAMS (REVPARAM_RATEMASK|REVPARAM_FORMATMASK|REVPARAM_SIZEMASK|REVPARAM_GAINMASK|REVPARAM_DAMPINGFREQMASK|REVPARAM_DENSITYMASK|REVPARAM_BANDWIDTHFREQMASK|REVPARAM_DECAYMASK|REVPARAM_PREDELAYMASK|REVPARAM_REVLEVELMASK|REVPARAM_EARLYLATEMASK)

/* Indexs when setting or getting all params */
#define REVPARAM_RATE			0
#define REVPARAM_FORMAT			1
#define REVPARAM_SIZE			2
#define REVPARAM_GAIN			3
#define REVPARAM_DAMPINGFREQ	4
#define REVPARAM_DENSITY		5
#define REVPARAM_BANDWIDTHFREQ	6
#define REVPARAM_DECAY			7
#define REVPARAM_PREDELAY		8
#define REVPARAM_REVLEVEL		9
#define REVPARAM_EARLYLATE		10
#define REV_NUM_PARAMS			11

typedef void * REVERBHANDLE;

typedef void ReverbProcessPtr(REVERBHANDLE, const float *, float *, uint32_t);
typedef REV_ERROR ReverbResetPtr(REVERBHANDLE);
typedef REV_ERROR ReverbSetParamsPtr(REVERBHANDLE, unsigned int, unsigned int *);
typedef void ReverbGetParamsPtr(REVERBHANDLE, unsigned int, unsigned int *);
typedef REVERBHANDLE ReverbAllocPtr(void);
typedef void ReverbFreePtr(REVERBHANDLE);

extern void ReverbProcess(REVERBHANDLE, const float *, float *, uint32_t);
extern REV_ERROR ReverbReset(REVERBHANDLE);
extern REV_ERROR ReverbSetParams(REVERBHANDLE, unsigned int, unsigned int *);
extern void ReverbGetParams(REVERBHANDLE, unsigned int, unsigned int *);
extern REVERBHANDLE ReverbAlloc(void);
extern void ReverbFree(REVERBHANDLE);

#ifdef __cplusplus
}
#endif

#endif
