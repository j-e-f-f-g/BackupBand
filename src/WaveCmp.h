/* ============================== WaveCmp.H =================================
 * Include file for Jeff Glatt's libwavecmp library.
 *
 * Copyright (C) 2009 Jeff Glatt
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _INC_WAVECMP
#define _INC_WAVECMP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>

uint32_t WaveCmpConvert(char *);
typedef uint32_t WaveCmpConvertPtr(char *);

uint32_t WaveCmpDir(char *);
typedef uint32_t WaveCmpDirPtr(char *);

#ifdef __cplusplus
}
#endif

#endif /* _INC_WAVECMP */
