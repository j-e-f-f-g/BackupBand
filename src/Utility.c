// Backup Band for Linux
// Copyright 2013 Jeff Glatt

// Backup Band is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Backup Band is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Backup Band. If not, see <http://www.gnu.org/licenses/>.

#include "Main.h"

unsigned char *		TempBuffer;
uint32_t					TempBufferSize;
static const char *	ErrMsg;

static const char		Home[] = "HOME";
const char				NoMemStr[] = "Out of RAM!";








/******************* hash_string() ********************
 * Gets a 32-bit hash for the nul-term string.
 */

uint32_t hash_string(register const unsigned char * str)
{
	register uint32_t hash;
	register uint32_t len;

	hash = 0;
	while ((len = *str++))
	{
		hash += len;
		hash += (hash<<10);
		hash ^= (hash>>6);
	}
	hash += (hash<<3);
	hash ^= (hash>>11);
	hash += (hash<<15);

	return hash;
}






/********************* format_syserr() ********************
 * Display system API error message.
 */

void format_syserr(char * buffer, unsigned int bufferSize)
{
	register char		*ptr;

	ptr = strerror(errno);
	strncpy(buffer, ptr, bufferSize);
	show_msgbox(buffer);
}






/*********************** get_exe_path() **********************
 * Copies the path of this EXE into the passed buffer.
 *
 * RETURNS: Size of path in CHARs.
 */

uint32_t get_exe_path(register char * buffer)
{
	char							linkname[64];
	register pid_t				pid;
	register unsigned long	offset;

	pid = getpid();
	snprintf(&linkname[0], sizeof(linkname), "/proc/%i/exe", pid);
	offset = readlink(&linkname[0], buffer, PATH_MAX);
	if (offset < PATH_MAX)
	{
		while (offset && buffer[offset - 1] != '/') --offset;
		if (offset && buffer[offset - 1] != '/') buffer[offset++] = '/';
	}
	else
		offset = 0;

	buffer[offset] = 0;

	return offset;
}





uint32_t get_home_path(register char * buffer)
{
	register const char *	ptr;
	register uint32_t			len;

	if ((ptr = getenv(Home)))
	{
		strcpy(buffer, ptr);
		len = strlen(buffer);
	}
	else
	{
		buffer[0] = '.';
		len = 1;
	}

	strcpy(&buffer[len], "/BackupBand/");
	return len + strlen(&buffer[len]);
}





/********************* getLong() *********************
 * gets the uint32_t in Big endian order.
 */

uint32_t getLong(register unsigned char * ptr)
{
	return (((uint32_t)ptr[0]) << 24) | (((uint32_t)ptr[1]) << 16) | (((uint32_t)ptr[2]) << 8) | ((uint32_t)ptr[3]);
}





/********************* getShort() *********************
 * gets the uint16_t in Big endian order.
 */

uint16_t getShort(register unsigned char * ptr)
{
	return (((uint16_t)ptr[0]) << 8) | (uint16_t)ptr[1];
}







/********************* storeLong() *********************
 * Stores the uint32_t in Big endian order.
 */

void storeLong(register uint32_t val, register unsigned char * buffer)
{
	buffer[0] = (unsigned char)((val >> 24) & 0xFF);
	buffer[1] = (unsigned char)((val >> 16) & 0xFF);
	buffer[2] = (unsigned char)((val >> 8) & 0xFF);
	buffer[3] = (unsigned char)(val & 0xFF);
}





/********************* storeShort() *********************
 * Stores the uint16_t in Big endian order.
 */

void storeShort(register uint16_t val, register unsigned char * buffer)
{
	buffer[0] = (unsigned char)((val >> 8) & 0xFF);
	buffer[1] = (unsigned char)(val & 0xFF);
}





void * alloc_temp_buffer(register uint32_t minSize)
{
	register void *	buffer;

	// If there's already a buffer, is it big enough, and caller safe?
	if (!(buffer = TempBuffer) || TempBufferSize < minSize)
	{
		// No. Get rid of it
		free_temp_buffer();
		TempBufferSize = minSize;
		if (!(buffer = malloc(minSize)))
			setErrorStr(NoMemStr);
	}

	TempBuffer = (unsigned char *)buffer;
	return buffer;
}





void free_temp_buffer(void)
{
	if (TempBuffer && (char *)TempBuffer != NoMemStr) free(TempBuffer);
	if (ErrMsg == (char *)TempBuffer && ErrMsg != NoMemStr) ErrMsg = 0;
	TempBuffer = 0;
}




void setErrorStr(const char * str)
{
	ErrMsg = str;
}





void setMemErrorStr(void)
{
	ErrMsg = NoMemStr;
}





const char * getErrorStr(void)
{
	return ErrMsg;
}




/********************** asciiToNum() **********************
 * Converts the ascii str of digits (expressed in base 10)
 * to a long. Returns the end of the ascii str of digits
 * (ie, after the last non-numeric digit) in passed handle.
 */

int32_t asciiToNum(unsigned char ** save)
{
	register unsigned char *buf;
	register uint32_t			result, extra;
	register unsigned char	chr, firstchr;

	buf = *save;

	// A negative value?
	if ((firstchr = *buf) == '-') ++buf;

	// Convert next digit
	result = 0;
	while (*buf)
	{
		chr = *buf - '0';
		if (chr > 9) break;
		extra = result << 3;
		result += (result + extra + (uint32_t)chr);
		++buf;
	}

	if (*save == buf) buf = 0;
	*save = buf;

	// Return value
	if (firstchr != '-') return (int32_t)result;

	return -result;
}





/********************* skip_spaces() **********************
 * Skips to next non-space, upto the end of the current line.
 */

unsigned char * skip_spaces(register unsigned char * ptr)
{
	while (ptr[0] == ' ' || ptr[0] == '\t') ++ptr;
	if (ptr[0] == '/' && ptr[1] == '/')
	{
		do
		{
			++ptr;
		} while (ptr[0] && ptr[0] != '\r' && ptr[0] != '\n');
	}
	if (ptr[0] == '\r')
	{
		if (ptr[1] == '\n') ++ptr;
		else ptr[0] = '\n';
	}
	return ptr;
}





/********************* skip_lines() **********************
 * Skips spaces, blank lines, and comments. Returns a count
 * of lines skipped.
 */

int32_t skip_lines(unsigned char ** ptr)
{
	register uint32_t				lines;
	register unsigned char *	chars;

	lines = 0;
	chars = *ptr;
	for (;;)
	{
		chars = skip_spaces(chars);
		if (!chars[0] || chars[0] != '\n') break;
		++lines;
		++chars;
	}

	*ptr = chars;
	return lines;
}





/******************** get_field_id() *******************
 * Gets the next keyword, upto an '=' (or space/comma) or
 * end of line, and tests that it matches one of the
 * specified hash values. Sets index to the index within the
 * hash table, or if error, one of the FILEFIELD_xxx values,
 *
 * ptr = Text to parse
 * hashArray = An array of uint32_t hash values. Limit is
 * 		63 values.
 * index = A count of values in hashArray. OR'd with 0x80
 *			if a space is the terminating char, or 0x40 if a
 * 		comma terminates, otherwise '=' terminates.
 * size_of = # bytes inbetween each hash value in the
 * 		array, so values can be embedded inside an array
 * 		of structs. 0 if values aren't embedded.
 *
 * RETURN: End of parsed text.
 */

unsigned char * get_field_id(register unsigned char * ptr, register const void * hashArray, register unsigned char * index, register uint32_t size_of)
{
	register unsigned char * start;
	register unsigned char	breakChr, actualChr;

	if (!size_of) size_of = sizeof(uint32_t);

	start = ptr;
	breakChr = ((*index & 0x80) ? ' ' : '=');
	if (*index & 0x40) breakChr = ',';

	// Find end of token. 'ptr' must already be on its first char,
	// or line end
	for (;;)
	{
		actualChr = ptr[0];
		if (breakChr != '=')
		{
			if (actualChr == breakChr || actualChr == '\t' || actualChr == ' ') break;
		}
		else if (actualChr == '=') break;

		// Caller nul-terminates supplied text to search, Don't search past that
		if (!actualChr) goto end;

		// Skip C++ style comment
		if (actualChr == '/' && ptr[1] == '/')
		{
			*ptr++ = 0;
			do
			{
				++ptr;
				if (!(actualChr = ptr[0]))
				{
end:				if (breakChr != '=') goto dohash;
					*index = FILEFIELD_END;
					goto out;
				}
			} while (actualChr != '\r' && actualChr != '\n');
		}

		// Line end implicitly terminates a token
		if (actualChr == '\r' || actualChr == '\n')
		{
			ptr[0] = actualChr = '\n';
			if (breakChr != '=') goto dohash;
			*index = FILEFIELD_UNKNOWN;
			*ptr = 0;
			goto out;
		}

		// The hash match is case-insensitive. The caller's table must assume
		// uppercase tokens. Use the crc.c utility to calc a hash upon a given
		// uppercase token
		if (actualChr >= 'a' && actualChr <= 'z') *ptr &= 0x5f;
		++ptr;
	}

	{
	register unsigned char * temp;

	if (breakChr == '=') actualChr = ' ';
dohash:
	// trim trailing spaces
	temp = ptr;
	while (--temp > start && *temp <= ' ');
	// nul-terminate the token we extracted
	*(temp + 1) = 0;
	}

	// Get the token's hash, and search the hash array for a match. If found,
	// return the 0-based index into the array
	{
	register uint32_t		hash;

	if ((hash = hash_string(start)))
	{
		register unsigned char		cnt;

		*index &= ~(0x80|0x40);
		cnt = *index;
		do
		{
			if (*((uint32_t *)hashArray) == hash)
			{
				*index -= cnt;
//				if (actualChr)
				{
					// don't skip over any ',' terminating char if caller was looking for that. Otherwise
					// Advance to the next token or line end
done:				*ptr = actualChr;
					if (breakChr != ',' || actualChr != ',')
						ptr = skip_spaces(ptr);
				}

out:			return ptr;
			}

			hashArray = (char *)hashArray + size_of;
		} while (--cnt);
	}
	}

	*index = FILEFIELD_BADID;
	goto done;
}
