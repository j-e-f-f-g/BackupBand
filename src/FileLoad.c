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

#include "Options.h"
#include "Main.h"
#include "MidiIn.h"
#include "AudioPlay.h"
#include "StyleData.h"
#include "SongSheet.h"
#include "Setup.h"
#include "FileLoad.h"
#include <dirent.h>
#include <errno.h>
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif



const char *				NamePtr;
static pthread_t			LoadThreadHandle;
unsigned short				WhatToLoadFlag;
unsigned char				DataPathFlag, IgnoreFlag;
static unsigned char		StyleCount;

static const char			StylesDir[] = "Styles/";
static const char			CatDir[] = "Categories/";
#ifndef NO_SONGSHEET_SUPPORT
static const char			SongSheetDir[] = "Songs/";
#endif




unsigned char getNumOfStyles(void)
{
	return StyleCount;
}




/******************* get_datapath() *****************
 * Gets the path where data files such as Instruments,
 * Styles, Window layouts, and other such data is
 * found. Since BackupBand only reads (not writes)
 * this data, it may be on readonly media, unlike
 * with our Preferences data which we must write to
 * disk.
 */

static uint32_t get_datapath(register char * path)
{
	register unsigned long	size;

	if (!DataPathFlag)
		size = get_home_path(path);
	else
		size = get_exe_path(path);

	return size;
}





uint32_t get_style_datapath(register char * path)
{
	register unsigned long	size;

	size = get_datapath(path);
	strcpy(&path[size], StylesDir);
	return (uint32_t)(size + sizeof(StylesDir) - 1);
}




/******************** post_load_error() *******************
 * Called by the "load thread" to signal the main thread of
 * an error and get a user response whether to continue.
 */

static void post_load_error(register void * signalHandle)
{
	// Don't bother signaling main if it wants us to abort
	// Ditto if user wants to ignore errs
	if (WhatToLoadFlag && !IgnoreFlag)
	{
		// Signal main and wait for it to read our errmsg. Main will either
		// clear "WhatToLoadFlag" if it wants the load thread to abort, or clear
		// "ErrMsg" if we should continue loading
//		GuiWinSignal(GuiApp, signalHandle, SIGNALMAIN_LOAD);	// Lv2 plugin needs signalHandle
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOAD);
		while (WhatToLoadFlag && getErrorStr()) sleep(1);
	}
}



static const char InstrumentsPath[] = {'I','n','s','t','r','u','m','e','n','t','s','/','4','4','k','h','z','/','.',0};

uint32_t getInstrumentPath(register char * path)
{
	register unsigned long	size;

	// Get path to files
	size = get_datapath(path);
	memcpy(&path[size], &InstrumentsPath[0], sizeof(InstrumentsPath));
	size += (sizeof(InstrumentsPath) - 2);
	if (setSampleRateFactor(0xFF) & 0x01) path[size - 5] = '8';
	return size;
}





/******************** loadDataThread() *******************
 * The "load thread". Loads all the kits/basses/patches/styles
 * in config or exe dir.
 */

static void * loadDataThread(void * arg)
{
	char							path[PATH_MAX];
	register uint32_t			size;
	register DIR *				dirp;
	register struct dirent *dirEntryPtr;

//	arg = GuiWinSignal(GuiApp, 0, 0);
	arg = (void *)4;

#ifdef GIGGING_DRUMS
	DataPathFlag = 1;
#else
	// Assume data's in config dir
	DataPathFlag = 0;
#endif

again:
	// =================== Load the instruments ====================
	if (WhatToLoadFlag & LOADFLAG_INSTRUMENTS)
	{
		register unsigned char	i;

		// Get path to files
		// Depending on sample rate, open the Drums, Bass, Guitar, or Pad
		// dir in either <data dir>/Instruments/44khz or <data dir>/Instruments/48khz
		size = getInstrumentPath(path);

		i = PLAYER_PAD + 1;
		do
		{
			--i;
			if ((WhatToLoadFlag & (0x10 << i)) && !getNextInstrument(0, i))
			{
				strcpy(&path[size], getMusicianName(i));
				size += strlen(&path[size]);
				path[size++] = '/';
				path[size] = '.';
				path[size+1] = 0;

				// Open the dir for searching. If it doesn't exist, that may not be an error
				if ((dirp = opendir(&path[0])))
				{
					while ((dirEntryPtr = readdir(dirp)))
					{
						NamePtr = dirEntryPtr->d_name;
						if (NamePtr[0] != '.')
						{
							strcpy(&path[size], NamePtr);
							headingCopyTo(NamePtr, 0);
							strcat(&path[size], "/");
							GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOADMSG_BASE|WhatToLoadFlag);
							loadInstrument(path, size + strlen(NamePtr) + 1, i);
							if (getErrorStr()) post_load_error(arg);
							if (!WhatToLoadFlag) goto abort;
						}
					}

					closedir(dirp);
				}

				size--;
				while (size && path[--size] != '/');
				size++;

				WhatToLoadFlag &= ~(0x10 << i);
			}
		} while ((WhatToLoadFlag & LOADFLAG_INSTRUMENTS));
	}

	{
	register uint32_t	len;

	// =================== Load the midi/mouse/pckey cmd assigns ====================
	size = get_datapath(path);
	if (WhatToLoadFlag & (LOADFLAG_ASSIGNS|LOADFLAG_STYLES|LOADFLAG_SONGS))
	{
		strcpy(&path[size], StylesDir);
		len = size + sizeof(StylesDir) - 1;
	}

	if (WhatToLoadFlag & LOADFLAG_ASSIGNS)
	{
		GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOADMSG_BASE|WhatToLoadFlag);
		loadCmdAssigns(path, len);
		WhatToLoadFlag &= ~LOADFLAG_ASSIGNS;
	}

	// =================== Load the styles ====================
	if (WhatToLoadFlag & LOADFLAG_STYLES)
	{
		strcpy(&path[len], CatDir);
		len += sizeof(CatDir) - 1;
		path[len] = '.';
		StyleCount = path[len+1] = 0;

		if ((dirp = opendir(&path[0])))
		{
			GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOADMSG_BASE|WhatToLoadFlag);
			while ((dirEntryPtr = readdir(dirp)))
			{
				NamePtr = dirEntryPtr->d_name;
				if (NamePtr[0] != '.')
				{
					register DIR *				dirp2;
					register void *			cat;
					register unsigned long	len2;

					// Load/create the category info
					if (!(cat = create_style_category(NamePtr)))
					{
						setMemErrorStr();
						post_load_error(arg);
					}
					else
					{
						strcpy(&path[len], NamePtr);
						len2 = len + strlen(NamePtr);
						path[len2++] = '/';
						path[len2] = '.';
						path[len2+1] = 0;
						if ((dirp2 = opendir(&path[0])))
						{
							while (WhatToLoadFlag && (dirEntryPtr = readdir(dirp2)))
							{
								NamePtr = dirEntryPtr->d_name;
								if (NamePtr[0] != '.')
								{
									strcpy(&path[len2], NamePtr);
									loadStyle(cat, &path[0]);
									if (getErrorStr()) post_load_error(arg);
									else StyleCount++;
								}
							}

							closedir(dirp2);
						}
					}
				}

				if (!WhatToLoadFlag) goto abort;
			}

			WhatToLoadFlag &= ~LOADFLAG_STYLES;

#ifndef NO_SONGSHEET_SUPPORT
			closedir(dirp);
		}
	}

	// =================== Load the song sheets ====================
	if (WhatToLoadFlag & LOADFLAG_SONGS)
	{
//		strcpy(&path[size], StylesDir);
//		len = size + sizeof(StylesDir) - 1;
		strcpy(&path[len], SongSheetDir);
		len += sizeof(SongSheetDir) - 1;
		path[len] = '.';
		path[len+1] = 0;

		if ((dirp = opendir(&path[0])))
		{
			GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOADMSG_BASE|WhatToLoadFlag);
			while (WhatToLoadFlag && (dirEntryPtr = readdir(dirp)))
			{
				NamePtr = dirEntryPtr->d_name;
				if (NamePtr[0] != '.')
				{
					strcpy(&path[len], NamePtr);
					loadSongSheet(&path[0]);
					if (getErrorStr()) post_load_error(arg);
				}
			}

			WhatToLoadFlag &= ~LOADFLAG_SONGS;
#endif
abort:	closedir(dirp);
		}
	}
	}

	// If the data we seek wasn't found, try exe dir,
	// unless user wants to abort
	if (WhatToLoadFlag && !DataPathFlag)
	{
		DataPathFlag = 1;
		goto again;
	}

	// Signal main that we're done
	LoadThreadHandle = 0;
//	GuiWinSignal(GuiApp, arg, SIGNALMAIN_LOAD);	// Lv2 plugin needs signalHandle
	GuiWinSignal(GuiApp, 0, SIGNALMAIN_LOAD);

	return 0;
}





/******************* abort_load() *******************
 * Called by Main thread to alert the load thread to abort.
 */

void abort_load(void)
{
	// Signal load thread to terminate by clearing WhatToLoadFlag
	WhatToLoadFlag = 0;
	headingCopyTo("Aborting ...", 0);
	clearMainWindow();
}






/****************** serviceLoadThread() ******************
 * Called by main thread to handle the load thread
 * signaling either it's done, or needs the user to
 * respond to an error msg, or update the progress
 * indicator.
 *
 * RETURN: non-zero if load thread still running.
 */

void * serviceLoadThread(void)
{
	// If aborting (WhatToLoadFlag=0), ignore any error. If
	// the Load thread doesn't have an error msg for us (ErrMsg=0),
	// then it just wants us to update the progress. If the Load
	// thread has terminated (LoadThreadHandle=0), then load is
	// done
	if (WhatToLoadFlag && getErrorStr() && LoadThreadHandle)
	{
		register uint32_t		res;

		// Display an error msg to the user and get his response
		res = GuiErrShow(GuiApp, getErrorStr(),
			(AppFlags2 & APPFLAG2_TIMED_ERR) ? GUIBTN_ESC_SHOW|GUIBTN_TIMEOUT_SHOW|GUIBTN_OK_SHOW|GUIBTN_ABORT_SHOW|GUIBTN_IGNORE_SHOW|GUIBTN_OK_DEFAULT :
			 GUIBTN_ESC_SHOW|GUIBTN_OK_SHOW|GUIBTN_ABORT_SHOW|GUIBTN_IGNORE_SHOW|GUIBTN_OK_DEFAULT);

		// User wants to continue (OK or IGNORE)? Regard a timeout as ignore
		if (res != GUIBTN_QUIT && res != GUIBTN_ABORT)
		{
			// Tell load thread to ignore further errs if user wants
			if (res != GUIBTN_OK) IgnoreFlag = 1;

			// Let load thread resume
			setErrorStr(0);
		}
		else
			// Signal load thread to terminate by WhatToLoadFlag = 0
			abort_load();
	}

	// Load thread will clear "LoadThreadHandle" when it finishes its job
	return (void *)LoadThreadHandle;
}





/****************** startLoadThread() ******************
 * Called by main thread to start a second thread that
 * loads waves/styles.
 *
 * Caller must set WhatToLoadFlag to a non-zero bitmask of
 * LOADFLAG_xxx.
 *
 * RETURN: 0 = success. Displays an error msg.
 */

int startLoadThread(void)
{
	setErrorStr(0);
	IgnoreFlag = 0;
	if (!pthread_create(&LoadThreadHandle, 0, loadDataThread, 0)) return 0;
	LoadThreadHandle = 0;
	show_msgbox("Can't start load thread");
	return -1;
}





/****************** load_text_file() ******************
 * Loads a text file into the temp buffer, and nul
 * terminates it. If an error, returns an error
 * message (in the temp buffer, except for an
 * out-of-mem).
 *
 * RETURN: Size of file, or 0 if error.
 */

uint32_t load_text_file(const char * fn, unsigned char numFileLevels)
{
	register int		len;
	register int		hFile;
	struct stat			buf;

	setErrorStr(0);

	// Open the text file
	if ((hFile = open(&fn[0], O_RDONLY|O_NOATIME)) == -1)
	{
		len = errno;

		// Caller doesn't want ErrMsg set if the file doesn't exist?
		if (!(numFileLevels & FILEFLAG_NOEXIST) || len != ENOENT)
bad:		format_file_err(fn, len, numFileLevels & 0x3f);
	}
	else
	{
		// Get the size
		fstat(hFile, &buf);
		len = buf.st_size;

		// Allocate a buffer to read in the whole file, plus terminating '\0'.
		// Use existing load buffer if big enough
		if (alloc_temp_buffer(len + ((numFileLevels & FILEFLAG_NO_NUL) ? 0 : 1)))
		{
			// Read in the file, and close it
			buf.st_size = read(hFile, TempBuffer, len);
			if (buf.st_size != len)
			{
				len = errno;
				goto bad;
			}
			close(hFile);

			// Nul term it
			if (!(numFileLevels & FILEFLAG_NO_NUL)) TempBuffer[len] = 0;

			return (uint32_t)len;
		}
	}

	if (hFile != -1) close(hFile);
	return 0;
}





/****************** format_text_err() **********************
 *	Formats an error msg (that occurred in a user-edited text
 * file) in the temp buffer.
 */

void format_text_err(register const char * errmsg, register uint32_t lineNum, register unsigned char * field)
{
	register unsigned char *		dest;

	if ((dest = TempBuffer))
	{
		{
		register uint32_t		len, room;

		room = TempBufferSize;

		// If there's an id string, copy it to the end of the temp
		// buffer because we're formatting the error msg in the same temp
		// buffer as the id string. We need to prevent overwriting
		if (field)
		{
			len = strlen((char *)field) + 1;
			room -= len;
			memmove(&dest[room], field, len);
			field = &dest[room];
		}
		else
		{
			field = &dest[room - 1];
			*field = 0;
		}
		}

		// Copy filename
		{
		register const char *		name;

		name = NamePtr;
		while (dest < field && (*dest = (unsigned char)(*name++))) dest++;
		}

		// Copy line #
		if (lineNum != 0xFFFFFFFF && (field - dest) > 6 + 5)
		{
			// For practical purposes line number doesn't exceed 5 digits
			sprintf((char *)dest, " Line %u: ", lineNum);
			dest += strlen((char *)dest);
		}
		else if ((field - dest) > 2)
		{
			*dest++ = ':';
			*dest++ = ' ';
		}
		dest[0] = 0;

		// Insert the error and id strings
		lineNum = strlen(errmsg) - 1;
		if ((field - dest) >= lineNum)
			sprintf((char *)dest, errmsg, field);

		setErrorStr((char *)TempBuffer);
	}
	else
		setMemErrorStr();
}





void format_file_err(register const char * filename, register int errnoValue, register unsigned char numFileLevels)
{
	register const char *	message;
	register const char *	start;

	// Get the err message from errno
	if (!(message = strerror(errnoValue))) message = "error";

	// Extract the filename from the path, plus optionally a few
	// dir names
	{
	register const char *	end;

	end = start = filename + strlen(filename);
	while (start > filename)
	{
		if (*(--start) == '/')
		{
			if (!(--numFileLevels))
			{
				++start;
				break;
			}
		}
	}

	errnoValue = (end - start);
	}

	// Format the message in TempBuffer, so make sure we have it, and it's big
	// enough. Caller must free_temp_buffer()
	{
	register uint32_t		len;
	register char *	end;

	len = strlen(message);
	if (!TempBuffer || (TempBufferSize < len + 3 + errnoValue && (char *)TempBuffer != filename))
	{
		free_temp_buffer();
		TempBufferSize = len + 3 + errnoValue;
		if (!(TempBuffer = (unsigned char *)malloc(TempBufferSize)))
		{
			setMemErrorStr();
			return;
		}
	}

	setErrorStr((end = (char *)TempBuffer));
	if (errnoValue >= TempBufferSize) errnoValue = TempBufferSize - 1;
	memmove(end, start, errnoValue);
	end += errnoValue;
	errnoValue = TempBufferSize - errnoValue;
	if (errnoValue > 3)
	{
		*end++ = ':';
		*end++ = ' ';
		errnoValue -= 3;
		if (errnoValue < len) len = errnoValue - 1;
		memcpy(end, message, len);
		end += len;
	}
	*end = 0;
	}
}
