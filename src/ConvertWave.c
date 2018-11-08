/*
 * Converts a WAVE file into our proprietary compressed format.
 *
 * Requires x11-dev and cairo packages to compile.
 */

#include "ConvertWave.h"
#include "WaveCmp.h"
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif






//============== CONST DATA ===============

static const char	WindowTitle[] = "WAVE Converter";

static const char	CmpExtension[] = ".cmp";
static const char	WavExtensions[] = "WAVE files (*.wav)\0*.wav\0\n";
static const char	CmpExtensions[] = "CMP files (*.cmp)\0*.cmp\0\n";
static const char	MakeCmpStr[] = "_Make .cmp";
static const char	MakeWavStr[] = "_Make .wav";

static const char	WaveCmpName[] = "libwavecmp.so";

static const char	HomeStr[] = "HOME";








//============== DATA ===============
GUIAPPHANDLE				GuiApp;
GUIWIN *						MainWin;

// Flags when to abort Gui loop
unsigned char				GuiLoop;

static WaveCmpConvertPtr *	WaveConvert;
static WaveCmpDirPtr *		DirConvert;












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
	short				wFormatTag;
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

typedef struct
{
	CHUNK_head		head;
	SAMPLER			smpl;
	SAMPLELOOP		smploop;
} SMPL;

#define WAVEFLAG_STEREO		0x01
#define WAVEFLAG_48000		0x10
#define WAVEFLAG_88200		0x20
#define WAVEFLAG_96000		0x30

// Holds info about one loaded waveform
typedef struct {
	uint32_t					WaveformLen;	// Size of data in 16-bit samples -- not frames nor bytes
	uint32_t					CompressPoint;	// Sample offset to 8-bit bytes
	uint32_t					LoopBegin;		// Sample offset to loop start. If LoopBegin >= WaveformLen, then no loop
	uint32_t					LoopEnd;			// Sample offset to loop end
	unsigned char			WaveFlags;
	unsigned char			Unused;
	char						WaveForm[1];	// Loaded wave data. Size=WaveformLen*2
} WAVEFORM_INFO;

typedef struct {
	uint32_t					WaveformLen;	// Size of data in 16-bit samples -- not frames nor bytes
	uint32_t					CompressPoint;	// Sample offset to 8-bit bytes
	uint32_t					LoopBegin;		// Sample offset to loop start. If LoopBegin >= WaveformLen, then no loop
	uint32_t					LoopEnd;			// Sample offset to loop end
	unsigned char			WaveFlags;
} DRUMBOXFILE;
#pragma pack()

static WAVESTART WaveStart = {
	{{'R', 'I', 'F', 'F'}, 4+0, {'W', 'A', 'V', 'E'}},
	{{'f', 'm', 't', ' '}, sizeof(FORMAT)},
	{1, 2, 44100, 0, 0, 16},
};

static CHUNK_head WaveData = {
	{'d', 'a', 't', 'a'}, 0,
};

static SMPL WaveLoop = {
	{{'s', 'm', 'p', 'l'}, sizeof(SMPL) - 8},
	{0,0,22675,60,0,0,0,1,0},
	{0,0,0,0,0,0},
};

static const uint32_t	LoopRates[] = {22676, 20833, 11338, 10417};
static const uint32_t	Rates[] = {44100, 48000, 88200, 96000};
static const char			DidNotOpen[] = " didn't open";

void show_error2(const char * msg)
{
	if (GuiApp)
		GuiErrShow(GuiApp, msg, GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
	else
		printf("%s\r\n", msg);
}

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

static void filename_error(char * fn)
{
	register char *	mem;

	if ((mem = malloc(strlen(fn) + 1)))
	{
		strcpy(mem, fn);
		GuiErrShow(GuiApp, mem, GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
		free(mem);
	}
}

/************************ waveLoad() ********************
 * Reads in a compressed WAVE file, and writes an
 * uncompressed WAVE.
 *
 * fn =					Filename to load.
 *
 * RETURNS: 0 = success, error msg = fail.
 *
 * If an error, copies a nul-terminated msg to fn[].
 */

static const char * waveLoad(char * fn)
{
	register WAVEFORM_INFO *waveInfo;
	register const char *	message;
	unsigned long				size;
	register int				inHandle;

	message = &DidNotOpen[0];
	waveInfo = 0;

	// Open the WAVE file for reading
	if ((inHandle = open(fn, O_RDONLY|O_NOATIME)) != -1)
	{
		DRUMBOXFILE					drum;

		message = " is a bad WAVE file";


		// Read in File header
		if (read(inHandle, &drum, sizeof(DRUMBOXFILE)) == sizeof(DRUMBOXFILE))
		{
			struct stat			buf;

			if (drum.WaveformLen >= drum.CompressPoint && (drum.LoopBegin == (uint32_t)-1 || drum.LoopBegin < drum.WaveformLen) && (drum.LoopEnd == (uint32_t)-1 || drum.LoopEnd > drum.LoopBegin))
			{
				// Allocate a WAVEFORM_INFO to load in the wave data
				fstat(inHandle, &buf);
				size = buf.st_size - sizeof(DRUMBOXFILE);
				if (!(waveInfo = (WAVEFORM_INFO *)malloc(size + sizeof(WAVEFORM_INFO) - 1)))
				{
					strcpy(fn, "No memory");
					close(inHandle);
					goto badout;
				}

				memset(waveInfo, 0, sizeof(WAVEFORM_INFO));
				waveInfo->WaveformLen = drum.WaveformLen;
				waveInfo->CompressPoint = drum.CompressPoint;
				waveInfo->LoopBegin = drum.LoopBegin;
				waveInfo->LoopEnd = drum.LoopEnd;
				waveInfo->WaveFlags = drum.WaveFlags;
				if (read(inHandle, &waveInfo->WaveForm[0], size) == size) message = 0;
			}
		}

		close(inHandle);
	}

	if (!message)
	{
		fileCat(fn, ".wav");

		if ((inHandle = open(fn, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
		{
			// Error
			message = " can't create!";
		}
		else
		{
			WaveData.Length = (waveInfo->WaveformLen << 1);
			WaveStart.head.Length = sizeof(WAVESTART) + WaveData.Length;
			if (waveInfo->LoopEnd > waveInfo->LoopBegin) WaveStart.head.Length += sizeof(SMPL);
			WaveStart.fmtdata.wChannels = (waveInfo->WaveFlags & WAVEFLAG_STEREO) ? 2 : 1;
			WaveStart.fmtdata.dwSamplesPerSec = Rates[waveInfo->WaveFlags >> 4];
			WaveStart.fmtdata.dwAvgBytesPerSec = WaveStart.fmtdata.dwSamplesPerSec * 2 * WaveStart.fmtdata.wChannels;
			WaveStart.fmtdata.wBlockAlign = 2 * WaveStart.fmtdata.wChannels;
			message = " can't save";

			if (write(inHandle, &WaveStart, sizeof(WAVESTART)) == sizeof(WAVESTART))
			{
				if (waveInfo->LoopEnd > waveInfo->LoopBegin)
				{
					WaveLoop.smpl.dwSamplePeriod = LoopRates[waveInfo->WaveFlags >> 4];
					WaveLoop.smploop.dwStart = waveInfo->LoopBegin >> 1;
					WaveLoop.smploop.dwEnd = waveInfo->LoopEnd >> 1;
					if (write(inHandle, &WaveLoop, sizeof(SMPL)) != sizeof(SMPL)) goto end2;
				}

				if (write(inHandle, &WaveData, 8) == 8)
				{
					size = (waveInfo->CompressPoint << 1);
					if (write(inHandle, &waveInfo->WaveForm[0], size) == size)
					{
						char *		ptr;
						short			pt;

						ptr = ((char *)&waveInfo->WaveForm[0]) + size;
						size = waveInfo->WaveformLen - waveInfo->CompressPoint;
						while (size--)
						{
							pt = *ptr++;
							if (write(inHandle, &pt, 2) != 2) goto end2;
						}

						message = 0;
					}
				}
			}

end2:		close(inHandle);
		}
	}

	if (message)
	{
		// Error
		register char *pstr;
		register char *copy;
		register unsigned char	two;

		two = 2;
		pstr = fn + strlen(fn);
		while (pstr > fn)
		{
			if (*(--pstr) == '/')
			{
				if (!(--two))
				{
					++pstr;
					copy = fn;
					while ((*(copy)++ = *(pstr)++));
					break;
				}
			}
		}

		strcat(fn, message);
	}

	if (waveInfo) free(waveInfo);
badout:
	return message;
}

static void WaveDeCmpDir(void)
{
	register struct dirent *	dirEntryPtr;
	register uint32_t				size;
	register DIR *					dirp;
	register char *				fn;

	if ((fn = GuiFileDlg(GuiApp, "Decompress folder", &MakeWavStr[0], 0, GUIFILE_DIR)) && fn[0])
	{
		// Open the dir for searching
		if (!(dirp = opendir(&fn[0])))
			GuiErrShow(GuiApp, "Can't access folder", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
		else
		{
			size = strlen(fn);
			fn[size++] = '/';

			// Get next file/dir in the dir
			while ((dirEntryPtr = readdir(dirp)))
			{
				register uint32_t	len;

				// Check that name ends in .cmp
				len = strlen(dirEntryPtr->d_name);
				if (len > 4 && !strcasecmp(&dirEntryPtr->d_name[len - 4], &CmpExtension[0]))
				{
					strcpy(&fn[size], dirEntryPtr->d_name);
					if (waveLoad(fn))
					{
						filename_error(fn);
						break;
					}
				}
			}

			closedir(dirp);
		}
	}
}






/********************* clearMainWindow() ********************
 * Invalidates area of main window, thereby causing a Expose
 * message to be sent to it.
 */

void clearMainWindow(void)
{
	GuiWinUpdate(GuiApp, MainWin);
}











//======================================================
// CONVERT SCREEN
//======================================================

// Convert screen ctls
#define CTLID_WAVEDIR		0
#define CTLID_WAVEFILE		1
#define CTLID_CMPDIR			3
#define CTLID_CMPFILE		4
#define CTLID_MAPDRUMS		6
#define CTLID_MAPPATCH		7

static GUICTL		ConvertCtls[] = {
 	{.Type=CTLTYPE_PUSH,			.Y=0,	.Label="WAVE Dir", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_PUSH,			.Y=0,	.Label="WAVE File", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_GROUPBOX, .Label="Compress"},

 	{.Type=CTLTYPE_PUSH,			.Y=1,	.Label="CMP Dir", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_PUSH,			.Y=1,	.Label="CMP File", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_GROUPBOX,	.Y=1, .Label="DeCompress"},

 	{.Type=CTLTYPE_PUSH,			.Y=2,	.Label="Drums", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_GROUPSTART},
 	{.Type=CTLTYPE_PUSH,			.Y=2,	.Label="Patch", .Width=10, .Attrib.NumOfLabels=1, .Flags.Global=CTLGLOBAL_NOPADDING},
 	{.Type=CTLTYPE_GROUPBOX,	.Y=2, .Label="Map"},
	{.Type=CTLTYPE_END},
};





static void doFileConvert2(void)
{
	register char *				fn;

	if ((fn = GuiFileDlg(GuiApp, "Pick CMP file to convert", &MakeWavStr[0], &CmpExtensions[0], 0)) && fn[0])
	{
		if (waveLoad(&fn[0])) filename_error(fn);
	}
}





static void doFileConvert(void)
{
	register char *				fn;

	if ((fn = GuiFileDlg(GuiApp, "Pick WAVE file to convert", &MakeCmpStr[0], &WavExtensions[0], 0)) && fn[0])
	{
		if (WaveConvert(&fn[0])) filename_error(fn);
	}
}





static void doDirConvert(void)
{
	register char *				fn;

	if ((fn = GuiFileDlg(GuiApp, "Pick WAVE folder", &MakeCmpStr[0], 0, GUIFILE_DIR)) && fn[0])
	{
		if (DirConvert(&fn[0])) filename_error(fn);
	}
}


//static const char GmDrumNames[] = ;
//static const char HeaderLine[] = {'P','G','M','=',' ',' ','V','O','L','=','1','0','0','\n','\n'};

static void mapDrums(void)
{
#if 0
	register char *				fn;

	getLoadName(&fn[0], 0);
	if (fn[0])
	{
		register uint32_t				size, len;
		DIR *								dirp;

		// Open the dir for searching
		if (!(dirp = opendir(&fn[0])))
			GuiErrShow(GuiApp, "Can't access folder", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
		else
		{
			int							inHandle;
			{
			register uint32_t			len2;

			len2 = len = size = strlen(fn);
			while (len && fn[len - 1] != '/') len--;
			fn[size++] = '/';
			while (len < len2) fn[size++] = fn[len++];
			strcpy(&fn[size], ".txt");
			size = len + 1;
			}

			// Create the .txt file
			if ((inHandle = open(fn, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1)
				GuiErrShow(GuiApp, "Can't create map text file", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
			else
			{
				struct dirent *	dirEntryPtr;

				if (write(inHandle, &HeaderLine[0], sizeof(HeaderLine)) != sizeof(HeaderLine))
					GuiErrShow(GuiApp, "Error saving map text file", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);

				// Get next file/dir in the dir
				else while ((dirEntryPtr = readdir(dirp)))
				{
					// Check that name ends in .cmp
					len = strlen(dirEntryPtr->d_name);
					if (len > 4 && !strcasecmp(&dirEntryPtr->d_name[len - 4], &CmpExtension[0]))
					{
						strcpy(&fn[size], dirEntryPtr->d_name);

						// See if its name matches one of the gm drum names
					}
				}
			}

			close(inHandle);
			closedir(dirp);
		}
	}
#endif
}





/********************** do_convertctl() ************************
 * Called by GUI thread when user operates a ctl in the Convert
 * screen.
 */

static void do_convertctl(register GUICTL * ctl)
{
	if (ctl)
	{
		register unsigned int	id;

		id = (unsigned int)(((char *)ctl - (char *)&ConvertCtls[0]) / sizeof(GUICTL));
		switch (id)
		{
			case CTLID_WAVEDIR:
			{
				doDirConvert();
				break;
			}

			case CTLID_WAVEFILE:
			{
				doFileConvert();
				break;
			}

			case CTLID_CMPDIR:
			{
				WaveDeCmpDir();
				break;
			}

			case CTLID_CMPFILE:
			{
				doFileConvert2();
				break;
			}

			case CTLID_MAPDRUMS:
			{
				mapDrums();
				break;
			}
		}
	}
}





/******************* handle_keypress() *********************
 * Called by GUI thread to process user keyboard input in
 * Convert screen.
 */

static void handle_keypress(register GUIMSG * msg)
{
//	switch (msg->Key.Code)
	{
	}
}





/********************* handle_mouse() **********************
 * Called by GUI thread to process user mouse input in
 * Convert screen.
 */

static void handle_mouse(register GUIMSG * msg)
{
	do_convertctl(msg->Mouse.SelectedCtl);
}





/******************* handle_drawing() *********************
 * Called by GUI thread to draw Convert screen.
 */

static void handle_drawing(void)
{
}





static GUIFUNCS ConvertGuiFuncs = {handle_drawing,
handle_mouse,
handle_keypress,
0};

/********************* doConvertScreen() ***********************
 * Shows/operates the "Convert" screen.
 */

void doConvertScreen(void)
{
	clearMainWindow();
	MainWin->Ptr = &ConvertGuiFuncs;
//	MainWin->Menu = 0;
//	MainWin->LowerPresetBtns = MainWin->UpperPresetBtns = 0;
	MainWin->Ctls = &ConvertCtls[0];
	GuiCtlSetSelect(GuiApp, MainWin, &ConvertCtls[0], &ConvertCtls[0]);
}





/*************** positionConvertGui() ****************
 * Sets initial position/scaling of GUI ctls based
 * upon font size.
 */

void positionConvertGui(void)
{
	GuiCtlScale(GuiApp, MainWin, &ConvertCtls[0], -1);
}



















/*********************** get_exe_path() **********************
 * Copies the path of this EXE into the passed buffer.
 *
 * RETURNS: Size of path in CHARs.
 */

static unsigned long get_exe_path(char * buffer)
{
	char							linkname[64];
	register unsigned long	offset;

	snprintf(&linkname[0], sizeof(linkname), "/proc/%i/exe", getpid());
	offset = readlink(&linkname[0], buffer, PATH_MAX);
	if (offset == (unsigned long)-1)
	{
		register const char	*ptr;

		// Get the path to the user's home dir
		if (!(ptr = getenv(&HomeStr[0]))) ptr = &HomeStr[4];
		strcpy(buffer, ptr);
		offset = strlen(buffer);
	}
	else
	{
		offset = strlen(buffer);
		while (offset && buffer[offset - 1] != '/') --offset;
	}

	if (offset && buffer[offset - 1] != '/') buffer[offset++] = '/';

	buffer[offset] = 0;

	return offset;
}






/************************* open_wavecmp() **************************
 * Loads the WaveCmp library, and fills in our global WaveConvert
 * with a pointer to the DLL's function that we need to call.
 *
 * RETURNS: A handle to the open DLL if all goes well, or 0 if an
 * error.
 */

static void * open_wavecmp(void)
{
	char					buffer[PATH_MAX];
   register void *	handle;
	register char *	name;

	// =============================
	name = (char *)&WaveCmpName[0];
again:
	if (!(handle = dlopen(name, RTLD_LAZY)))
	{
		if (name != buffer)
		{
			register unsigned long	offset;

			offset = get_exe_path(buffer);
			strcpy(&buffer[offset], name);
			name = buffer;
			goto again;
		}
		GuiErrShow(GuiApp, "Can't load WaveCmp!", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
	}
	else
	{
		if (!(DirConvert = (WaveCmpDirPtr *)dlsym(handle, "WaveCmpDir")) || !(WaveConvert = (WaveCmpConvertPtr *)dlsym(handle, "WaveCmpConvert")))
		{
			dlclose(handle);
			GuiErrShow(GuiApp, "Need a later version of WaveCmp!", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
			handle = 0;
		}
	}

	return handle;
}





/******************** do_msg_loop() *********************
 * Does an XWindows message loop, saving the msg data in
 * global vars, and then calling the current GuiFuncs
 * callbacks.
 */

static void do_msg_loop(void)
{
	register GUIMSG *		msg;

	GuiLoop = 0;

	msg = GuiWinGetMsg(GuiApp);

	switch (msg->Type)
	{
/*
		case GUI_HELP:
		{
			register int	err;

			if (((GUIFUNCS *)MainWin->Ptr)->HelpName && (err = GuiHelpShow(GuiApp, 0, ((GUIFUNCS *)MainWin->Ptr)->HelpName)))
				display_syserr(err);
			break;
		}
*/
		case GUI_WINDOW_DRAW:
		{
			((GUIFUNCS *)MainWin->Ptr)->DrawFunc();
			break;
		}

		case GUI_MOUSE_CLICK:
		{
			((GUIFUNCS *)MainWin->Ptr)->MouseFunc(msg);
			break;
		}

		case GUI_KEY_PRESS:
		{
			((GUIFUNCS *)MainWin->Ptr)->KeyFunc(msg);
			break;
		}

		case GUI_WINDOW_CLOSE:
		{
			GuiLoop = GUIBTN_QUIT;
//			break;
		}
	}
}





/************************** main() **********************
 * Program Entry point
 */

int main(int argc, char *argv[])
{
	// Init gui subsystem. Get default window and font sizes
	if (!(GuiApp = (GUIAPPHANDLE)GuiInit(0)))
		show_error2("Can't start XWindows");
	else
	{
		register GUIINIT *	init;

		// Initialize GUIWIN
		MainWin = GuiApp->CurrentWin;
		MainWin->Flags = GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_HELP_BTN;

		// Create the app window
		init = GuiAppState(GuiApp, GUISTATE_GET_INIT, 0);
		init->ParentHandle = 0;
		if (GuiWinState(GuiApp, MainWin, GUISTATE_CREATE|GUISTATE_MENU|GUISTATE_OPEN|GUISTATE_LINK))
		{
			// Scale GUI ctls
			positionConvertGui();

			init->Title = &WindowTitle[0];
			init->Width = ConvertCtls[2].Width + (ConvertCtls[2].X * 2);
			init->Height = MainWin->WinPos.Height;
			GuiWinState(GuiApp, 0, GUISTATE_SHOW|GUISTATE_SIZE|GUISTATE_TITLE);

			{
			register void *	handle;

			if ((handle = open_wavecmp()))
			{
				doConvertScreen();

				// Do GUI loop
				do
				{
					do_msg_loop();
				} while (GuiLoop != GUIBTN_QUIT);

				dlclose(handle);
			}
			}
		}

		// Close all open windows and free resources
		GuiDone(GuiApp, 0);
	}

	return 0;
}
