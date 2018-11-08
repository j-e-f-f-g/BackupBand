/* A simple "toolkit" (push/check/radio/spin buttons, menubar, text entry widget,
 * etc) which uses cairo for drawing, and XWindows API for input. It is designed
 * to work well as a GUI for an LV2 plugin, and also on small touchscreens.
 */

// GuiCtls.c
// Copyright 2013 Jeff Glatt

// GuiCtls.c is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GuiCtls.c is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GuiCtls. If not, see <http://www.gnu.org/licenses/>.

#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#define I_AM_GUICTLS
#include "GuiCtls.h"
#include <dlfcn.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#ifndef O_NOATIME
#define O_NOATIME        01000000
#endif

#define GUI_TITLEBAR_SPACE	2

#pragma pack(1)

// Gtk File Dialog
typedef char			gchar;
typedef int				gint;
typedef int				gboolean;
typedef void *			GtkWidget;

typedef enum
{
  GTK_FILE_CHOOSER_ACTION_OPEN,
  GTK_FILE_CHOOSER_ACTION_SAVE,
  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
  GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER
} GtkFileChooserAction;

typedef enum
{
  GTK_RESPONSE_NONE         = -1,
  GTK_RESPONSE_REJECT       = -2,
  GTK_RESPONSE_ACCEPT       = -3,
  GTK_RESPONSE_DELETE_EVENT = -4,
  GTK_RESPONSE_OK           = -5,
  GTK_RESPONSE_CANCEL       = -6,
  GTK_RESPONSE_CLOSE        = -7,
  GTK_RESPONSE_YES          = -8,
  GTK_RESPONSE_NO           = -9,
  GTK_RESPONSE_APPLY        = -10,
  GTK_RESPONSE_HELP         = -11
} GtkResponseType;

typedef void GTKDESTROY(GtkWidget);
typedef GtkWidget GTKFN(const gchar *, void *, GtkFileChooserAction, const gchar *, ...);
typedef gboolean GTKPENDING(void);
typedef void GTKFREE(void *);
typedef gint GTKRUN(void *);
typedef gchar* GTKGETFN(void *);
typedef void GTKADDFILTER(void *, void *);
typedef void * GTKNEWFILTER(void);
typedef void GTKNAMEFILTER(void *, const gchar *);
typedef void GTKINIT(int *, char ***);

struct GTKPTRS {
	GTKNEWFILTER *	file_filter_new;
	GTKNAMEFILTER *file_filter_set_name;
	GTKNAMEFILTER *file_filter_add_pattern;
	GTKADDFILTER *	file_chooser_add_filter;
	GTKFN *			file_chooser_dialog_new;
	GTKGETFN *		file_chooser_get_filename;
	GTKDESTROY *	widget_destroy;
	GTKRUN *			dialog_run;
	GTKPENDING *	events_pending;
	GTKPENDING *	main_iteration;
	GTKINIT *		init;
};

struct GUIEDITCTL	{
	GUICTL *		CurrCtl;
	char *		CurrChar;
};

struct GUIHELPMEM	{
	const char *	Mem;
	const char *	TopPtr;
};

struct GUIMODALVARS	{
	const char *	Str;
	unsigned short	YPos;
	unsigned char	Color;
	unsigned char	DrawFlags;
};

struct PRESETSEL {
	unsigned char	Num, Which;
};

struct GUINUMPAD	{
	unsigned char	Index;
	unsigned char	Limit;
	char 				Buffer[6];
};

typedef struct {
	GUIWIN *			GWin;
	unsigned short	X;
	unsigned short	Y;
} GUIFAKE;

// GUIWIN2->InternalFlags
#define GUIWIN_MODAL			0x00000001
#define GUIWIN_FAKESIZE		0x00000002
#define GUIWIN_HELP_AT_END	0x00000004
#define GUIWIN_HELP			0x00000008
#define GUIWIN_SKIP_REDRAW	0x00000010
#define GUIWIN_TEMP_HIDE	0x00000020
#define GUIWIN_AREA_RECALC	0x00000040
#define GUIWIN_NOTITLEBAR	0x00000080
#define GUIWIN_HELP_FAKESIZE 0x00000100
#define GUIWIN_HELP_CLOSE 	0x00000200
#define GUIWIN_HELP_LINK 	0x00000400
#define GUIWIN_IS_LINKED 	0x00000800
#define GUIWIN_DRAW_COLLECT 0x00001000
#define GUIWIN_SIZE_COLLECT 0x00002000
#define GUIWIN_MINSIZE_SET	0x00004000
//#define 	0x00008000
#define GUIWIN_HELP_IN_RAM	0x00010000
#define GUIWIN_NUMPAD		0x00020000
#define GUIWIN_STRING		0x00040000
#define GUIWIN_AREA_PROCESSED 0x00080000
#define GUIWIN_ERROR_DLG	0x00100000

struct GUIWIN2 {
	struct GUIWINSTRUCT	Win;
	// Hidden
	cairo_t *			CairoGraphics;
	cairo_surface_t *	CairoWindow;
	struct GUIWIN2 *	Next;
	Window				BaseWindow;
	union {
	// When InternalFlags == GUIWIN_MODAL
	struct GUIMODALVARS	MsgBox;
	// When InternalFlags == GUIWIN_HELP
	struct GUIHELPMEM		Help;
	};
	GUICTL *				CurrentCtl;
	struct PRESETSEL	Preset;
	uint32_t				InternalFlags;
	struct GUINUMPAD	Numpad;
	unsigned short		CurrentCtlY;
	unsigned char		HelpPage;
};

struct GUIAPP2 {
	struct GUIAPP		Host;
	struct GUIWIN2		MainWin;
	GUICTL *				GrabCtl;
	GUIWIN *				GrabWin;
	Display *			XDisplay;
	int					EventHandle, EventQueue;
	void *				GLib;
	void *				GtkLib;
	struct GTKPTRS		Gtk;
	GTKFREE *			g_free;
	union {
	XEvent				XMsg;
	GUIMSG				Msg;
	char					Filename[PATH_MAX+1];
	GUIINIT				Init;
	const char *		HelpString;
	uint32_t				Milliseconds;
	};
	unsigned char		InternalAppFlags;
	uint32_t				GrabTimeout;
	GUIFAKE				FakeMsg;
};

#pragma pack()

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct
{
	uint32_t		flags;
	uint32_t		functions;
	uint32_t		decorations;
	int32_t		input_mode;
	uint32_t		status;
} MWMHints;

// GUIAPP->InternalAppFlags
#define GUIAPP_FLUSHMODAL		0x01
#define GUIAPP_MODAL				0x02
#define GUIAPP_POLL				0x04
//#define GUIAPP_		0x08
#define GUIAPP_LIST_MADE		0x10
#define GUIAPP_PLUGIN			0x20
#define GUIAPP_SIZE_COLLECT	0x40
#define GUIAPP_DRAW_COLLECT	0x80

//============== CONST DATA ===============

#pragma pack(1)
typedef struct {
	float		Red,Green,Blue;
} COLORVALUE;
#pragma pack()

// Must match the order of GUICOLOR_ symbols in GuiCtls.h
static const COLORVALUE ColorVal[] = {
	{.95f,.95f,.95f},	// BACKGROUND
	{0.f,0.f,0.f},		// BLACK
	{.9f,0.f,0.f},		// RED
	{0.f,.9f,0.f},		// GREEN
	{.7f,.5f,0.f},		// ORANGE
	{.55f,.6f,.8f},	// LIGHTBLUE
	{1.f,.5f,.5f},		// PINK
	{.55f,.55f,.35f}, // GOLD
	{0.f,.5f,0.f},		// DARKGREEN
	{.25f,.25f,.8f},	// PURPLE
	{.75f,.5f,.65f},	// VIOLET
	{0.f,0.f,.9f},		// BLUE
	{.3f,.3f,.3f},		// DARKGRAY
	{.6f,.6f,.6f},		// GRAY
	{.8f,.8f,.4f},		// LIGHTGOLD
	{.5f,.7f,.9f}};	// MENU

static const char	WmDeleteAtomName[] = "WM_DELETE_WINDOW";
static const char	WmProtocolsName[] = "WM_PROTOCOLS";
static const char	WmNoTitleBarName[] = "_MOTIF_WM_HINTS";

static const char	TestStr[] = "X X";
static const char	FontName[] = "Serif";

static const char	NoMemStr[] = "Out of RAM!";

static const char PresetStrs[] = {'C','a','n','c','e','l', 0,
'N','o', 0,
GUICOLOR_LIGHTGOLD, 'Y','e','s', 0,
GUICOLOR_LIGHTGOLD, 'O','k', 0,
GUICOLOR_LIGHTGOLD, 'S','a','v','e', 0,
GUICOLOR_LIGHTGOLD, 'L','o','a','d', 0,
'H','e','l','p', 0,
GUICOLOR_LIGHTGOLD, 'N','e','w', 0,
'D','e','l','e','t','e', 0,
'C','o','p','y', 0,
'A','b','o','r','t', 0,
'I','g','n','o','r','e', 0,
GUICOLOR_LIGHTGOLD, 'R','e','t','r','y', 0,
'S','k','i','p', 0,
0};

// ========================================

// X Windows structs
static Atom					WmDeleteWindowAtom;
static Atom					WmProtocolsAtom;
static Atom					NoTitleAtom;

static unsigned short	Arrows[4];
// Font sizes
#define APP_NUM_FONTS	2
static unsigned char		FontSizes[APP_NUM_FONTS];

static const char GtkNames[] = {"gtk_file_filter_new\0\
gtk_file_filter_set_name\0\
gtk_file_filter_add_pattern\0\
gtk_file_chooser_add_filter\0\
gtk_file_chooser_dialog_new\0\
gtk_file_chooser_get_filename\0\
gtk_widget_destroy\0\
gtk_dialog_run\0\
gtk_events_pending\0\
gtk_main_iteration\0\
gtk_init\0"};

static const char Extension[] = ".txt";
static const char UserShare[] = {'/','u','s','r','/','s','h','a','r','e','/'};

static void make_help_btns(register GUIAPPHANDLE, register GUIWIN *, register GUICTL *);
static uint32_t init_presets(GUIAPPHANDLE, register GUIWIN *, register GUICTL *, register uint32_t);
static void virtkey_to_keymap(register GUIAPPHANDLE, register unsigned char);
static void listAreaDraw(GUIAPPHANDLE, GUICTL *, GUIAREA *);
static GUICTL * make_numpad(GUIAPPHANDLE, GUIWIN *);
static int32_t asciiToNum(register const char *);
static GUICTL * update_helper_arrows(GUIAPPHANDLE, register GUICTL *, register GUILIST *);
static GUICTL * findListCtl(register GUIWIN *, register GUICTL *);
























// =======================================
// Auto-help window
// =======================================

static const char		PageStr[] = "Page:";
static const char		DoneStr[] = "Done";

/******************* GuiExeGetPath() *******************
 * Copies the path of this EXE into the passed buffer.
 *
 * RETURNS: Size of path in CHARs.
 */

uint32_t GuiExeGetPath(char * buffer)
{
	char					linkname[64];
	register pid_t		pid;
	register long		offset;

	pid = getpid();
	snprintf(&linkname[0], sizeof(linkname), "/proc/%i/exe", pid);
	offset = readlink(&linkname[0], buffer, PATH_MAX);
	if (offset == -1)
	{
		const char		*ptr;

		// Get the path to the user's home dir
		if (!(ptr = getenv("HOME"))) ptr = &Extension[4];
		strcpy(buffer, ptr);
		offset = strlen(buffer);
	}
	else
	{
		buffer[offset] = 0;
		while (offset && buffer[offset - 1] != '/') --offset;
	}

	if (offset && buffer[offset - 1] != '/') buffer[offset++] = '/';

	return (uint32_t)offset;
}





/**************** loadHelpPage() ******************
 * Loads the specified (nul-terminated) help page
 * into allocated ram, and stores the pointer in
 * "GUIWIN->Help.Mem" (which must be freed by caller).
 *
 * name =	Nul-term filename of help page, minus
 *				.txt extension and path. If 0, then app
 *				has passed a nul-term'ed buffer in
 *				GUIAPP->HelpString.
 *
 * Note: Because an autohelp window calls GuiWinGetMsg, it
 * may overwrite part of GUIAPP's TEMPBUFFER.
 */

static int loadHelpPage(GUIAPPHANDLE app, GUIWIN * win, const char * name)
{
	register int					hFile;
	register unsigned char		flag;
	register unsigned long		len;
	int								err;

	if (!name)
	{
		win->InternalFlags |= GUIWIN_HELP_IN_RAM;
		win->Help.Mem = app->HelpString;
		return 0;
	}

	if (!(len = strlen(&app->Msg.Filename[0])))
	{
		// Get name of help file in exe dir
		flag = 1;
		len = GuiExeGetPath(&app->Msg.Filename[0]);
	}

	flag = 0;

again:
	if (len && app->Msg.Filename[len - 1] != '/') app->Msg.Filename[len++] = '/';
	strcpy(&app->Msg.Filename[len], name);
	strcat(&app->Msg.Filename[len], &Extension[0]);

	// Open the file
	if ((hFile = open(&app->Msg.Filename[0], O_RDONLY|O_NOATIME)) != -1)
	{
		// Get the size
		len = lseek(hFile, 0, SEEK_END);
		if (len < 0)
			err = errno;
		else
		{
			char *	ptr;

			// Allocate a buffer to read in the whole file
			if (!(win->Help.Mem = ptr = (char *)malloc(len + 1)))
				err = ENOMEM;

			else
			{
				// nul-terminate
				err = ptr[len] = 0;

				// Read in the file
				lseek(hFile, 0, SEEK_SET);
				if (read(hFile, ptr, len) != len)
				{
					err = errno;
					free(ptr);
					win->Help.Mem = 0;
				}
			}
		}

		close(hFile);
	}
	else
		err = errno;

	// If not in exe dir, look in /usr/share/(exeName)
	if (err == ENOENT && flag)
	{
		len = GuiExeGetPath(&app->Msg.Filename[0]);
		flag = strlen(&app->Msg.Filename[len]) + 1;
		memmove(&app->Msg.Filename[sizeof(UserShare)], &app->Msg.Filename[len], flag);
		memcpy(&app->Msg.Filename[0], &UserShare[0], sizeof(UserShare));
		len = strlen(&app->Msg.Filename[0]);
		flag = 0;
		goto again;
	}

	return err;
}




/******************* help_draw() *********************
 * Draws an auto-help window. Called for an XWindow
 * Expose event.
 */

static void help_draw(GUIAPPHANDLE app, GUIWIN * win)
{
	if (win->Help.Mem)
	{
		register const char *	ptr;
		uint32_t						yPos, x;
		register char				chr;
		unsigned char				pageNum, buttons;
		unsigned char				color;

		GuiTextSetColor(win, GUICOLOR_BLACK);
		GuiTextSetLarge(win);

		// Get page (text) start. win->Help.TopPtr = 0 if we haven't yet located the page start
		chr = pageNum = 0;
		if (!win->HelpPage) win->Help.TopPtr = win->Help.Mem;
		if (!(ptr = win->Help.TopPtr)) ptr = win->Help.Mem;

		// Start drawing beneath the scroll arrows
		buttons = GuiCtlGetTypeHeight(app, CTLTYPE_ARROWS) + GuiWinGetBound(app, win, GUIBOUND_YSPACE);
		if (buttons + app->Host.GuiFont.Height <= win->Win.WinPos.Height)
		{
			color = GUICOLOR_BLACK;

			// Do word wrap
			goto start;
			for (;;)
			{
				char *						dest;
				uint32_t						width;
				{
				register const char *	endptr;
				register char *			temp;
				cairo_text_extents_t		textExtents;

				// Strip leading spaces from the next word
				temp = dest;
				while (*ptr == ' ') ptr++;
				chr = *ptr;
	 			*temp++ = ' ';

				// If the word begins with a char 0x01 to 0x09, then change color
				if (chr < 9 || temp >= &app->Msg.Filename[PATH_MAX - 8])
				{
					if (chr < 9) ptr++;

					// We first need to flush any pending output at the old color
					if (win->Help.TopPtr && dest > &app->Msg.Filename[0])
					{
						*temp = 0;
						GuiTextDraw(app, x, yPos, &app->Msg.Filename[0]);
					}

					// Throw away chars just displayed
					temp = dest = &app->Msg.Filename[0];

					if (chr != color)
					{
						color = chr;
						GuiTextSetColor(win, chr);
					}

					x += (x == 5 ? app->Host.GuiFont.CharWidth : app->Host.GuiFont.CharWidth / 2);
					x += width;
				}
				endptr = ptr;
				while ((chr = *endptr) && chr != ' ' && chr != '\n')
				{
					*temp++ = chr;
					endptr++;
				}
				if (!chr) chr = '\n';

				// See if it fits on the current line
				*temp = 0;
				cairo_text_extents(win->CairoGraphics, &app->Msg.Filename[0], &textExtents);
				width = textExtents.width;
				if (x + width > win->Win.WinPos.Width)
				{
					// Start a new line
					chr = '\n';
				}
				else
				{
					ptr = endptr;
					dest = temp;
				}
				}

				// Increase yPos to next line?
				while (chr == '\n')
				{
					if (win->Help.TopPtr && dest > &app->Msg.Filename[0])
					{
						*dest = 0;
						GuiTextDraw(app, x, yPos, &app->Msg.Filename[0]);
					}

					// Can another line fit in the window?
					yPos += app->Host.GuiFont.Height;
					if (yPos + buttons + app->Host.GuiFont.Height > win->Win.WinPos.Height)
					{
						// No. If we're still seeking to the page start, just advance
						// the page # and keep seeking. Otherwise we're done
						if (win->Help.TopPtr) goto done;
						if (++pageNum >= win->HelpPage) win->Help.TopPtr = ptr;
start:				yPos = 2;
					}

					dest = &app->Msg.Filename[0];
					x = 5;
					width = 0;

					// Toss away leading spaces on a line
					while ((chr = *ptr++) && chr == ' ');
					if (!chr) goto done;
					if (chr != '\n') ptr--;
				}
			}
		}
done:
		{
		GUICTL		temp[3];

		// Draw Page and Done
		win->InternalFlags &= ~GUIWIN_HELP_AT_END;
		if (!chr) win->InternalFlags |= GUIWIN_HELP_AT_END;
		make_help_btns(app, win, &temp[0]);
		GuiCtlDraw(app, &temp[0]);
		}
	}
}




/********************* make_help_btns() *********************
 * For an auto-help window, we create temp copies of
 * our Page and Done GUICTLs whenever we need them.
 * These copies are made in the same temp buffer
 * where we format Preset ctls. (An auto-help doesn't
 * use presets). But otherwise, these behave like
 * lower presets.
 */
static void make_help_btns(register GUIAPPHANDLE app, register GUIWIN * win,  register GUICTL * baseArray)
{
	memset(&baseArray[0], 0, sizeof(GUICTL) * 3);
	baseArray[0].Type = CTLTYPE_ARROWS;
	baseArray[0].Label = PageStr;
	baseArray[0].Flags.Local = CTLFLAG_NOSTRINGS;
	baseArray[0].Attrib.Value = (unsigned char)win->HelpPage + 1;

	baseArray[1].Type = CTLTYPE_PUSH;
	baseArray[1].Attrib.NumOfLabels = 1;
	baseArray[1].Label = DoneStr;

	baseArray[1].Y = baseArray[0].Y = win->Win.WinPos.Height - (GuiCtlGetHeight(app, baseArray) + GuiWinGetBound(app, win, GUIBOUND_YSPACE));
	baseArray[1].Flags.Global = baseArray[0].Flags.Global = CTLGLOBAL_PRESET;
	baseArray[0].X = GuiWinGetBound(app, win, GUIBOUND_XBORDER);
	GuiCtlSetWidth(app, win, &baseArray[0]);
	baseArray[1].X = baseArray[0].X + baseArray[0].Width + (GuiWinGetBound(app, win, GUIBOUND_XSPACE) * 4);
	GuiCtlSetWidth(app, win, &baseArray[1]);

	// Disable "Back" button?
	if (!win->HelpPage) baseArray[0].Flags.Local = CTLFLAG_NO_UP|CTLFLAG_NOSTRINGS;

	// Disable "Next" button?
	if (win->InternalFlags & GUIWIN_HELP_AT_END) baseArray[0].Flags.Local |= CTLFLAG_NO_DOWN;

	// Indicate the temp array of GUICTLs has been made
	app->InternalAppFlags |= GUIAPP_LIST_MADE;

	// Store for caller
	app->Msg.Mouse.SelectedCtl = baseArray;
}





/******************** free_help() *********************
 * Frees any loaded help page.
 *
 * Returns: GUI_WINDOW_SIZE, GUI_WINDOW_CLOSE, or 0.
 */

static unsigned char free_help(register GUIAPPHANDLE app, register GUIWIN * win)
{
	register uint32_t	flags;

	// Get the flags before we clear them below
	flags = win->InternalFlags;

	// Is this currently an auto-help window?
	if (flags & GUIWIN_HELP)
	{
		// GUISTATE_CLOSE below will call free_help() again, so clear GUIWIN_HELP
		// now
		win->InternalFlags &= ~(GUIWIN_HELP|GUIWIN_HELP_CLOSE|GUIWIN_HELP_AT_END|GUIWIN_HELP_IN_RAM);

		// Free loaded text
		if (win->Help.Mem && !(flags & GUIWIN_HELP_IN_RAM)) free((void *)win->Help.Mem);
		win->Help.Mem = win->Help.TopPtr = 0;

		// If we opened the window, then close it and report GUI_CLOSE_WINDOW to
		// the app. Otherwise just clear the window, and report GUI_WINDOW_SIZE if we
		// siphoned off a window resize while in auto-help mode
		if (!(flags & GUIWIN_HELP_CLOSE))
		{
			GuiWinUpdate(app, win);
			if (flags & GUIWIN_HELP_FAKESIZE) return GUI_WINDOW_SIZE;
		}
		else
		{
			GuiWinState(app, win, (win->InternalFlags & GUIWIN_HELP_LINK) ? GUISTATE_CLOSE|GUISTATE_UNLINK : GUISTATE_CLOSE);
			return GUI_WINDOW_CLOSE;
		}
	}

	// Caller is about to invoke HELP or MODAL mode on a GUIWIN that may already
	// be in HELP or MODAL mode. We don't support that except within GuiErrShow()
	// which saves/restores the GUIWIN
	win->InternalFlags &= ~GUIWIN_STRING;

	return 0;
}





/******************* help_keypress() *********************
 * Called by GUI thread to process user keyboard input in
 * Auto-Help window.
 *
 * RETURNS: Non-zero if user pressed Backspace, Esc, or Enter.
 */

static unsigned char help_keypress(register GUIAPPHANDLE app, register GUIWIN * win, register uint32_t keycode)
{
	switch (keycode)
	{
		case XK_BackSpace:
		case XK_Escape:
		case XK_Return:
			return 1;

		case XK_Up:
		case XK_Page_Up:
		{
			if (win->Help.Mem && win->HelpPage)
			{
				--win->HelpPage;
				goto page;
			}
			break;
		}

		case XK_space:
		case XK_Down:
		case XK_Page_Down:
		{
			if (win->Help.Mem)
			{
				if (!(win->InternalFlags & GUIWIN_HELP_AT_END) && win->HelpPage < 255)
				{
					++win->HelpPage;

page:				win->Help.TopPtr = 0;
					GuiWinUpdate(app, win);
				}
			}
			break;
		}

		case XK_Home:
		{
			if (win->Help.Mem && win->HelpPage)
			{
				win->HelpPage = 0;
				win->InternalFlags &= ~GUIWIN_HELP_AT_END;
				goto page;
			}
		}
	}

	return 0;
}





/********************* help_mouse() **********************
 * Called by GUI thread to process user mouse input in
 * Auto-Help window.
 *
 * RETURNS: Non-zero if user clicked "Done" button.
 */

static unsigned char help_mouse(register GUIAPPHANDLE app, register GUIWIN * win)
{
	register GUICTL * ctl;
	GUICTL				temp[3];
	register unsigned short	x,y;

	if (app->Msg.Mouse.ButtonNum == 1)
	{
		x = app->Msg.Mouse.X;		// XY overlaps TEMPBUFFER, which make_help_btns overwrites
		y = app->Msg.Mouse.Y;
		make_help_btns(app, win, &temp[0]);
		app->Msg.Mouse.X = x;
		app->Msg.Mouse.Y = y;
		if ((ctl = GuiCtlSelectXY(app, &temp[0])))
		{
			if (ctl->Type != CTLTYPE_ARROWS) return 1;

			help_keypress(app, win, ((temp[0].Flags.Local & CTLFLAG_UP_SELECT) ? XK_Up : XK_Down));
		}
	}
	return 0;
}





/******************* GuiHelpShow() ********************
 * Loads/displays a help page.
 *
 * name =	Nul-term filename of help page, minus
 *				.txt extension and path.
 *
 * GUIAPP->Msg.Filename is path to help files. "" indicates
 * use executable path or executable's /usr/share path.
 */

int GuiHelpShow(GUIAPPHANDLE app, GUIWIN * helpwin, const char * name)
{
	register GUIWIN *		win;
	register int			err;

	// If caller passed a zero, see if there's a GUIWIN_HELP_ANCHOR window
	if (!(win = helpwin))
	{
		win = &app->MainWin;
		do
		{
			if (win->Win.Flags & GUIWIN_HELP_ANCHOR) goto found;
		} while ((win = win->Next));

		// Use main window
		win = &app->MainWin;
	}
found:
	free_help(app, win);

	if (!(err = loadHelpPage(app, win, name)))
	{
		// Mark this GUIWIN in help mode
		win->InternalFlags |= GUIWIN_HELP;

		win->HelpPage = 0;

		// If app didn't already open the window, then we need to do that, and
		// then flag that we need to close it when user is done
		if (win->BaseWindow)
			GuiWinUpdate(app, win);
		else
		{
			if (!(win->InternalFlags & GUIWIN_IS_LINKED)) win->InternalFlags |= GUIWIN_HELP_LINK;
			app->Init.ParentHandle = 0;
			app->Init.Title = name;
			if (!GuiWinState(app, win, GUISTATE_OPEN|GUISTATE_LINK|GUISTATE_TITLE|GUISTATE_SHOW)) err = EAGAIN;
			win->InternalFlags |= GUIWIN_HELP_CLOSE;
		}

		if (err) free_help(app, win);
	}

	return err;
}





















/********************** GuiTextSetColor() ********************
 * Sets the text color.
 */

void GuiTextSetColor(GUIWIN * win, uint32_t color)
{
	cairo_set_source_rgb(win->CairoGraphics, ColorVal[color].Red, ColorVal[color].Green, ColorVal[color].Blue);
}





/******************* GuiTextSetLarge() *******************
 * Sets the window to use the large text font.
 */

void GuiTextSetLarge(GUIWIN * win)
{
	cairo_select_font_face(win->CairoGraphics, &FontName[0], CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(win->CairoGraphics, FontSizes[0]);
}




/******************* GuiTextSetSmall() *******************
 * Sets the window to use the small text font.
 */

void GuiTextSetSmall(GUIWIN * win)
{
	cairo_select_font_face(win->CairoGraphics, &FontName[0], CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(win->CairoGraphics, FontSizes[1]);
}





/******************* GuiTextDraw() *******************
 * Displays a nul-terminated string using the large text font.
 */

void GuiTextDraw(GUIAPPHANDLE app, uint32_t x, uint32_t y, const char * str)
{
	cairo_move_to((app->Host.CurrentWin)->CairoGraphics, x, y + app->Host.GuiFont.HeightOffset);
	cairo_show_text((app->Host.CurrentWin)->CairoGraphics, str);
}





/******************* GuiTextDrawSmall() *******************
 * Displays a nul-terminated string using the small text font.
 */

void GuiTextDrawSmall(GUIAPPHANDLE app, uint32_t x, uint32_t y, const char * str)
{
	cairo_move_to((app->Host.CurrentWin)->CairoGraphics, x, y + app->Host.GuiFont.SmallHeightOffset);
	cairo_show_text((app->Host.CurrentWin)->CairoGraphics, str);
}





/******************* GuiTextWidth() *******************
 * Gets the pixel width of a nul-terminated string.
 */

uint32_t GuiTextWidth(GUIWIN * win, const char * str)
{
	cairo_text_extents_t			textExtents;

	cairo_text_extents(win->CairoGraphics, str, &textExtents);
	return textExtents.width;
}





/****************** menuClick() ******************
 * Process user mouse clicks for menu bar.
 */

static unsigned char menuClick(register GUIAPPHANDLE app, register GUIMENU * menu)
{
	register uint32_t				x, padding;
	register unsigned short *	width;
	register unsigned char		numlabels;

	width = menu->Widths;
	numlabels = menu->LabelCnt & 0x0F;
	padding = menu->Padding << 1;
	x = 0;
	do
	{
		x += *width++ + padding;
	} while (app->Msg.Mouse.X > x && --numlabels);

	return (menu->LabelCnt & 0x0F) - numlabels;
}





/****************** menuCalcLabelWidths() ******************
 * Sets the GUIMENU->Widths[] array to the pixel width of each
 * label in the menu bar.
 */

uint32_t menuCalcLabelWidths(GUIAPPHANDLE app, GUIWIN * win)
{
	register unsigned short *	width;
	register unsigned short		total;
	register const char **		labelPtr;
	register unsigned char		numlabels;
	register GUIMENU *			menu;

	total = 0;
	if ((menu = win->Win.Menu))
	{
		if (menu->Color < 2) menu->Color = (GUICOLOR_BLACK << 4) | GUICOLOR_LIGHTBLUE;

		GuiTextSetSmall(win);
		labelPtr = menu->Labels;
		width = menu->Widths;
		numlabels = menu->LabelCnt & 0x0F;
		goto start;
		do
		{
			total += *width++ + (2 * app->Host.GuiFont.CharWidth);
start:	*width = (unsigned short)GuiTextWidth(win, *labelPtr++);
		} while (--numlabels);
	}

	return total;
}





/******************** menuDraw() *********************
 * Draws the menu bar.
 *
 * Called upon an XWindows Expose message.
 */

static void menuDraw(register GUIAPPHANDLE app, register GUIWIN * win)
{
	register uint32_t				x, padding;
	register const char **		labelPtr;
	register unsigned short *	width;
	register unsigned char		labelNum;
	unsigned short					winEdge;
	GUIMENU *						menu;

	if ((menu = win->Win.Menu))
	{
		GuiTextSetSmall(win);

		// Calculate the amount of blank space between each
		// label in the menu bar, to fill the window width
		width = menu->Widths;
		labelNum = (menu->LabelCnt & 0x0F);
		x = 0;
		do
		{
			x += *width++;
		} while (--labelNum);

		// Leave room for any close box, and other app customizations to titlebar
		padding = ((((win->InternalFlags & (GUIWIN_NOTITLEBAR|GUIAPP_PLUGIN)) == GUIWIN_NOTITLEBAR) * 8) + (menu->LabelCnt >> 4)) * app->Host.GuiFont.QuarterWidth;
		winEdge = win->Win.WinPos.Width;
		if (winEdge > padding)
		{
			winEdge -= padding;
			if (x > winEdge) x = winEdge;
			labelNum = (menu->LabelCnt & 0x0F);
			padding = (uint32_t)((winEdge - x) / (labelNum << 1));
			menu->Padding = (unsigned short)padding;

			// Background
			GuiTextSetColor(win, menu->Color & 0x0F);
			cairo_rectangle(win->CairoGraphics, 0, 0, winEdge, app->Host.GuiFont.Height);
			cairo_fill(win->CairoGraphics);

			// label color
			GuiTextSetColor(win, menu->Color >> 4);

			labelPtr = menu->Labels;
			width = menu->Widths;
			x = labelNum = 0;
			goto start;
			do
			{
				x += *width++ + padding;
				cairo_move_to(win->CairoGraphics, x, 0);
				cairo_line_to(win->CairoGraphics, x, app->Host.GuiFont.Height - 1);
				cairo_stroke(win->CairoGraphics);
start:
				x += padding;
				if (x >= winEdge) break;
				if (labelNum == menu->Select) GuiTextSetColor(win, GUICOLOR_RED);
				cairo_move_to(win->CairoGraphics, x, app->Host.GuiFont.SmallHeightOffset);
				cairo_show_text(win->CairoGraphics, *labelPtr++);
				if (labelNum == menu->Select) GuiTextSetColor(win, menu->Color >> 4);
			} while (++labelNum < (menu->LabelCnt & 0x0F));
		}

		// Caller draws window close, and app custom drawing
	}
}




/******************* numpad_process() ********************
 * Handles key input for a window displaying a numeric pad
 * via GuiNumpadInit().
 */

static unsigned char numpad_process(register GUIAPPHANDLE app, register uint32_t keycode)
{
	register GUIWIN *	win;
	register char *	ptr;

	win = app->Host.CurrentWin;
	ptr = &win->Numpad.Buffer[0];

	if (keycode >= '0' && keycode <= '9')
	{
		// If more than Limit chars, then shift one char
		if (win->Numpad.Index >= win->Numpad.Limit)
		{
			--win->Numpad.Index;
			memmove(&ptr[0], &ptr[1], 5);
		}

		ptr[win->Numpad.Index++] = keycode;
redo:	ptr[win->Numpad.Index] = 0;

		{
		register GUIBOX *		box;

		box = (GUIBOX *)((char *)(&app->Filename[PATH_MAX]) - sizeof(GUIBOX));
		box->Y = win->MsgBox.YPos;
		box->Height = GuiCtlGetTypeHeight(app, CTLTYPE_PUSH);
		box->X = GuiWinGetBound(app, win, GUIBOUND_XBORDER);
		box->Width = app->Host.GuiFont.CharWidth * 5;
		GuiWinAreaUpdate(app, win, box);
		}
	}

	else switch (keycode)
	{
		// ======= Backspace =======
		case XK_BackSpace:
		{
			if (win->Numpad.Index)
			{
				--win->Numpad.Index;
				goto redo;
			}

			break;
		}

		// ======= ENTER =======
		case XK_Return:
		// ============ Esc ==========
		case XK_Escape:
		{
			register GUICTL *		temp;

			app->Msg.Numpad.Value = GuiNumpadEnd(app, win);
			temp = &app->Msg.PresetCtls[0];
			app->Msg.Numpad.SelectedCtl = temp;
			temp->PresetId = (keycode == XK_Return ? GUIBTN_NUMPAD : GUIBTN_CANCEL);
			temp->Flags.Global = CTLGLOBAL_PRESET;
			return 1;
		}
	}

	return 0;
}




GUIMSG * GuiWinGetTimedMsg(GUIAPPHANDLE app, uint32_t waitAmt)
{
	if ((app->Host.Timeout = waitAmt) && (app->InternalAppFlags & (GUIAPP_POLL|GUIAPP_PLUGIN)))
	{
		GuiAppState(app, GUISTATE_GET_TIME, &waitAmt);
 		app->Host.Timeout += waitAmt;
	}
	return GuiWinGetMsg(app);
}

void GuiWinResize(GUIWIN * win)
{
	cairo_xlib_surface_set_size(win->CairoWindow, win->Win.WinPos.Width, win->Win.WinPos.Height);
}

static void draw_close(register GUIAPPHANDLE app, register GUIWIN * win)
{
	// Draw close box if no titlebar
	if ((win->InternalFlags & GUIWIN_NOTITLEBAR) && !(app->InternalAppFlags & GUIAPP_PLUGIN))
	{
		GuiTextSetColor(win, GUICOLOR_BLACK);
		GuiTextSetSmall(win);
		GuiTextDrawSmall(app, win->Win.WinPos.Width - (8 * app->Host.GuiFont.QuarterWidth), 4, &TestStr[2]);
	}
}

/******************** GuiWinGetMsg() *********************
 * Gets the next XWindows message, saving the msg data in
 * GUIAPP->GUIMSG or some GUIWIN, and then returns
 * a generic "event type".
 */

GUIMSG * GuiWinGetMsg(GUIAPPHANDLE app)
{
	register GUIWIN *		win;
	register uint32_t		ret;

	// Special handling for CTLTYPE_EDIT when user presses enter key
	if (app->FakeMsg.GWin)
	{
		register GUIFAKE *	ptr;

		ptr = &app->FakeMsg;
		app->Host.CurrentWin = win = ptr->GWin;
		app->Msg.Mouse.X = ptr->X;
		app->Msg.Mouse.Y = ptr->Y;
		app->Msg.Mouse.ButtonNum = 1;
		app->FakeMsg.GWin = 0;
		goto fakemouse;
	}

again:
	// Issue fake resizes upon leaving modal mode
	if (app->InternalAppFlags & GUIAPP_FLUSHMODAL)
	{
		ret = 0;
		win = &app->MainWin;
		do
		{
			if (win->InternalFlags & GUIWIN_FAKESIZE)
			{
				win->InternalFlags &= ~(GUIWIN_FAKESIZE);
				GuiWinUpdate(app, win);
				goto send_fake_size;
			}

			ret |= (win->InternalFlags & GUIWIN_MODAL);
		} while ((win = win->Next));

		app->InternalAppFlags &= ~GUIAPP_FLUSHMODAL;

		// Drop out of modal mode only after all fake size msgs are
		// reported to app, and app clears modal from all windows
		if (!ret) app->InternalAppFlags &= ~GUIAPP_MODAL;
	}

	for (;;)
	{
		if (!XPending(app->XDisplay))
		{
			register uint32_t			currTime;

			// No more queued messages

			// We're about to overwrite any temp array of GUICTLs
			app->InternalAppFlags &= ~GUIAPP_LIST_MADE;

			// Do we have an accumulated size msg to send for some GUIWIN?
			if (app->InternalAppFlags & GUIAPP_SIZE_COLLECT)
			{
				// Yes. Retrieve that GUIWIN
				win = &app->MainWin;
				do
				{
					if (win->InternalFlags & GUIWIN_SIZE_COLLECT)
					{
send_fake_size:	win->InternalFlags &= ~(GUIWIN_SIZE_COLLECT);

						// If a help win, postpone sending the app a size msg
						// until the user closes the document
						if (win->InternalFlags & GUIWIN_HELP)
							win->InternalFlags |= GUIWIN_HELP_FAKESIZE;
						else
						{
							// Mark all CTLTYPE_AREA as needing to be recalculated
							win->InternalFlags |= GUIWIN_AREA_RECALC;

							ret = GUI_WINDOW_SIZE;
							app->Host.CurrentWin = win;
							goto send_to_app;
						}
					}
				} while ((win = win->Next));

				// Only when all guiwin SIZEs are sent do we clear this
				app->InternalAppFlags &= ~GUIAPP_SIZE_COLLECT;
			}

			// Do we have an accumulated draw msg to send? As per above
			if (app->InternalAppFlags & GUIAPP_DRAW_COLLECT)
			{
				win = &app->MainWin;
				do
				{
					if (win->InternalFlags & GUIWIN_DRAW_COLLECT)
					{
						app->Host.CurrentWin = win;

						// Reset flag that "collects" our own window invalidation calls. Minimizes
						// unnecessary draw requests
						win->InternalFlags &= ~(GUIWIN_SKIP_REDRAW|GUIWIN_DRAW_COLLECT);

						// If an auto-help window, draw it here without needing to report to the app
						if (win->InternalFlags & GUIWIN_HELP)
						{
							help_draw(app, win);
							draw_close(app, win);
							goto again;
						}

						// If app supplied a msg string, display it and save YPos for preset buttons
						if (win->InternalFlags & GUIWIN_STRING)
							win->MsgBox.YPos = GuiTextDrawMsg(app, win->MsgBox.Str, &win->Win.WinPos, win->MsgBox.DrawFlags | ((uint32_t)win->MsgBox.Color << 24));

						// Draw app's ctls for this window
						GuiCtlDraw(app, 0);

						// Draw numpad if applicable
						if (win->InternalFlags & GUIWIN_NUMPAD)
							GuiCtlDraw(app, make_numpad(app, win));

						{
						register GUICTL *		temp;

						// Draw upper/lower preset btns
						temp = &app->Msg.PresetCtls[0];
						ret = 0;
						if (win->Win.UpperPresetBtns)
							ret = init_presets(app, win, temp, GUIBTN_RESERVED);
						if (win->Win.LowerPresetBtns)
							ret += init_presets(app, win, &temp[ret], 0);
						if (ret) GuiCtlDraw(app, temp);

						// Draw the menu if supplied
						menuDraw(app, win);
						}

						// Draw close button if no titlebar
						draw_close(app, win);

						// Let caller know this window needs any extra custom drawing
						ret = GUI_WINDOW_DRAW;
						goto send_to_app;
					}
				} while ((win = win->Next));

				app->InternalAppFlags &= ~GUIAPP_DRAW_COLLECT;
			}

			// If app is polling, we can't wait here. We check only for
			// a mouse grab "tick", or perhaps an app-specified timeout
			ret = !app->GrabCtl ? app->Host.Timeout : app->GrabTimeout;
			if (app->InternalAppFlags & (GUIAPP_POLL|GUIAPP_PLUGIN))
			{
				// Did the app set a timeout, or GuiCtl start a mouse grab?
				if (ret)
				{
					// Check the elapsed time
					GuiAppState(app, GUISTATE_GET_TIME, 0);
					currTime = app->Milliseconds;
					if (currTime >= app->GrabTimeout)
					{
						register GUICTL *	ctl;

do_time:				if ((ctl = app->GrabCtl) && (win = app->GrabWin))
						{
							// For a grab, create a fake click on the currently selected (ARROWS) ctl
							app->Msg.Mouse.SelectedCtl = win->CurrentCtl = ctl;

							if (!(ctl->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, ctl, 0, 0);

							if ((ctl->Flags.Local & (CTLFLAG_NO_UP|CTLFLAG_UP_SELECT)) == (CTLFLAG_NO_UP|CTLFLAG_UP_SELECT) ||
								(ctl->Flags.Local & (CTLFLAG_NO_DOWN|CTLFLAG_DOWN_SELECT)) == (CTLFLAG_NO_DOWN|CTLFLAG_DOWN_SELECT))
							{
								app->GrabCtl = 0;
								app->GrabTimeout = 0;
								goto again;
							}

							// Reset timer
							app->GrabTimeout = currTime + 100;

							goto fakeclick2;
						}
					}

					if (!(app->InternalAppFlags & (GUIAPP_POLL|GUIAPP_PLUGIN)) || (app->Host.Timeout && currTime >= app->Host.Timeout))
					{
						// Regular timeout
						app->Host.Timeout = 0;
						ret = GUI_TIMEOUT;
						goto send_to_app;
					}

					// Still waiting for a msg, timeout, or grab tick. If polling, return 0 to let the caller
					// do whatever, then subsequently call back here to check for msgs/timeout.
				}

ret0:			return 0;
			}

			// Wait for a XWindows server message, or a GuiWinSignal() (or
			// optionally a timeout)
			{
			register struct epoll_event *	ev;
			{
			register int						result;

			ev = (struct epoll_event *)&app->XMsg;
			result = epoll_wait(app->EventHandle, ev, 1, ret ? ret : -1);
			if (result <= 0 || !(ev->events & EPOLLIN))
			{
				// Timeout?
				if (!(currTime = result)) goto do_time;

				// An error. Return 0 to let caller know we have some unordinary situation
				goto ret0;
			}
			}

			// A signal from one of the app's threads calling GuiWinSignal()?
			if (ev->data.fd == app->EventQueue)
			{
				uint64_t		data;

				if (read(app->EventQueue, &data, sizeof(uint64_t)) < 0) goto again;

				app->XMsg.xclient.message_type = 1000;
				app->XMsg.xclient.data.l[0] = (long)data;
				win = &app->MainWin;
				goto client;
			}
			}
		}

		// Get an input event from XWindows, and put it in our temp buffer
		XNextEvent(app->XDisplay, &app->XMsg);

		// Get which GUIWIN this msg is for. Set it as GUIAPP'S "current window".
		// Ignore msgs not sent specifically to one of our wins
		win = &app->MainWin;
		do
		{
			if (app->XMsg.xany.window == win->BaseWindow) goto got_it;
		} while ((win = win->Next));
	}
got_it:
	app->Host.CurrentWin = win;

	// Do any processing before we give it to the app
	switch (app->XMsg.type)
	{
		case ConfigureNotify:
		{
			// Save in the GUIWIN
			win->Win.WinPos.X = app->XMsg.xconfigure.x;
			win->Win.WinPos.Y = app->XMsg.xconfigure.y;
			win->Win.WinPos.Width = app->XMsg.xconfigure.width;
			win->Win.WinPos.Height = app->XMsg.xconfigure.height;

			cairo_xlib_surface_set_size(win->CairoWindow, win->Win.WinPos.Width, win->Win.WinPos.Height);

			win->InternalFlags |= (GUIWIN_SIZE_COLLECT|GUIWIN_AREA_RECALC);
			app->InternalAppFlags |= GUIAPP_SIZE_COLLECT;

			goto again;
		}

		case Expose:
		{
			// Collect all queued Expose msgs, and do only a clear of the updated areas -- postpone
			// drawing of the controls. Speeds up display, and minimizes flickering

			win->InternalFlags |= GUIWIN_DRAW_COLLECT;
			app->InternalAppFlags |= GUIAPP_DRAW_COLLECT;

			// Fill window background
			GuiTextSetColor(win, GUICOLOR_BACKGROUND);
			cairo_rectangle(win->CairoGraphics, app->XMsg.xexpose.x, app->XMsg.xexpose.y, app->XMsg.xexpose.width, app->XMsg.xexpose.height);
			cairo_fill(win->CairoGraphics);
			cairo_stroke(win->CairoGraphics);

			goto again;
		}

		case ButtonPress:
		{
			// Mouse grab ends
			if (app->GrabCtl)
			{
				app->GrabCtl = 0;
				app->GrabTimeout =  0;
			}

			app->Msg.Mouse.X = (uint16_t)app->XMsg.xbutton.x;
			app->Msg.Mouse.Y = (uint16_t)app->XMsg.xbutton.y;

			// Click on the custom drawn close box (in a window without a titlebar)?
			if ((win->InternalFlags & GUIWIN_NOTITLEBAR) && !(app->InternalAppFlags & GUIAPP_PLUGIN) && app->XMsg.xbutton.button == 1 &&
				app->Msg.Mouse.Y < app->Host.GuiFont.Height && app->Msg.Mouse.X > win->Win.WinPos.Width - (8 * app->Host.GuiFont.QuarterWidth))
			{
				goto closew;
			}

			// Reset mouse doubleclick detection
			app->Host.PreviousTime = app->Host.CurrTime;
			app->Host.CurrTime = app->XMsg.xbutton.time;
			app->Host.DClickFlag = ((app->Host.CurrTime - app->Host.PreviousTime) < (app->Host.ClickSpeed << 4));

			// Collect info for caller in a generic GUIMOUSE struct
			app->Msg.Mouse.ButtonNum = app->XMsg.xbutton.button;
			app->Msg.Mouse.Flags = app->XMsg.xbutton.state;
			app->Msg.Mouse.SelectedCtl = 0;
			ret = GUIMOUSE_DOWN;

			// If auto-help, handle it ourselves
			if (win->InternalFlags & GUIWIN_HELP)
			{
				if (help_mouse(app, win))
				{
finish_help:	if ((ret = free_help(app, win)) == GUI_WINDOW_CLOSE) goto closew;
					goto send_fake_size;
				}
				goto again;
			}

			// If we're in modal mode, and this isn't a modal win, then throw away the mouse click. This
			// prevents the user from interacting with non-modal wins
			if ((app->InternalAppFlags & GUIAPP_MODAL) && !(win->InternalFlags & GUIWIN_MODAL)) goto again;

			if (!(win->Win.Flags & GUIWIN_RAW_MOUSE))
			{
				// Do extra handling for left mouse button
				if (app->Msg.Mouse.ButtonNum == 1)
				{
					register GUICTL *		temp;

					// Check for click on menu
					if (win->Win.Menu && app->Msg.Mouse.Y < app->Host.GuiFont.Height)
					{
						register GUIMENUFUNC **	func;
						register GUIMENU *		menu;

						ret = menuClick(app, win->Win.Menu);
menu_done:			menu = (GUIMENU *)win->Win.Menu;
						if ((func = menu->Funcs))
						{
							if (ret < (menu->LabelCnt & 0x0F))
							{
								if (ret != menu->Select)
								{
									menu->Select = ret;
									GuiWinUpdate(app, win);
									(*func[ret])(app);
								}
								goto again;
							}
						}
						app->Msg.MenuNum = ret;
						ret = GUI_MENU_SELECT;
						break;
					}

					// Is there a currently selected ctl? Is it an EDIT?
					if ((temp = win->CurrentCtl) && temp != (GUICTL *)app && temp->Type == CTLTYPE_EDIT)
					{
						ret = temp->Y + win->CurrentCtlY + GuiWinGetBound(app, win, GUIBOUND_UPPER) +
							((win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0);

						// Ignore redundant clicks
						if (app->Msg.Mouse.Y >= ret && app->Msg.Mouse.Y < ret + app->Host.GuiFont.Height + 4 &&
							app->Msg.Mouse.X > temp->X && app->Msg.Mouse.X < temp->X + temp->Width)
							goto again;

						// We need to send a fake XK_Return GUI_KEY_PRESS for
						// the EDIT first, and then upon the next GuiWinMsg call, retrieve
						// this current msg's params. Let's temporarily store the GUIWIN and X/Y
						// in the GUIAPP FakeMsg. We do this only if the edit ctl is marked CTLFLAG_WANT_RETURN
						if (temp->Flags.Local & CTLFLAG_WANT_RETURN)
						{
							register GUIFAKE *	ptr;

							ptr = &app->FakeMsg;
							ptr->X = app->Msg.Mouse.X;
							ptr->Y = app->Msg.Mouse.Y;
							ptr->GWin = win;
							app->Msg.Mouse.SelectedCtl = temp;
        					ret = GUIMOUSE_DOWN;
							goto fakeclick;
						}
					}

fakemouse:		ret = GUIMOUSE_DOWN;

					// Get ctl clicked on, and mark it for selection/redraw
release:			app->Msg.Mouse.SelectedCtl = GuiCtlSelectXY(app, win->Win.Ctls);
					win->Win.Flags &= ~GUIWIN_NO_UPD_CURR;

fakeclick:		app->Msg.Mouse.AbsX = app->Msg.Mouse.X;
					app->Msg.Mouse.AbsY = app->Msg.Mouse.Y;

					if (!(temp = app->Msg.Mouse.SelectedCtl))
					{
						if (win->InternalFlags & GUIWIN_NUMPAD)
						{
							register GUICTL *		base;

							app->Msg.Mouse.SelectedCtl = base = make_numpad(app, win);
							app->InternalAppFlags |= GUIAPP_LIST_MADE;
							win->Win.Flags |= GUIWIN_NO_UPD_CURR;
							temp = GuiCtlSelectXY(app, base);
							win->Win.Flags &= ~GUIWIN_NO_UPD_CURR;
							app->InternalAppFlags &= ~GUIAPP_LIST_MADE;
							if (temp)
							{
								if (app->Msg.Mouse.ButtonNum == 1 && ret == GUIMOUSE_DOWN)
								{
									ret = temp->Label[0];
									if (temp <= &base[1]) ret = (temp == base ? XK_Return : XK_Escape);

numpad:							if (numpad_process(app, ret))
									{
										ret = GUIMOUSE_DOWN;
										app->Msg.Mouse.ButtonNum = 1;
										goto release2;
									}
								}

								goto again;
							}
						}

						// If not a click on any ctl, see if app wants it
						if (!(win->Win.Flags & GUIWIN_BACKGND_CLICK)) goto again;
					}
					else
					{
						// Adjust Y by preceding layers, menubar and upper preset buttons, and any Header string
						app->Msg.Mouse.Y -= (win->CurrentCtlY + GuiWinGetBound(app, win, GUIBOUND_UPPER)
							+ ((win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0));

						if (app->Msg.Mouse.ButtonNum == 1 && ret == GUIMOUSE_DOWN)
						{
							// ========================= Do any optional processing for preset btns
							if ((temp->Flags.Global & CTLGLOBAL_PRESET) &&

								// If GUIWIN_HELP_BTN, report "Help" preset button to app
								temp->Type == CTLTYPE_PUSH && temp->PresetId == GUIBTN_HELP && (win->Win.Flags & GUIWIN_HELP_BTN))
							{
dohelp:						ret = GUI_HELP;
								app->Msg.Filename[0] = 0;
								break;
							}

							// ================ Process some app control the user clicked
							else
							{
								if (temp->Type == CTLTYPE_ARROWS && !app->GrabCtl)
								{
									// Start mouse grab
									app->GrabCtl = temp;
									app->GrabWin = win;

									// Get initial time, to be used as a start reference, and then add the
									// amount of milliseconds to wait (3 secs). If not polling, we don't
									// need a start reference -- just an amount of time to wait
									app->GrabTimeout = 1000;
									if (app->InternalAppFlags & (GUIAPP_POLL|GUIAPP_PLUGIN))
									{
										GuiAppState(app, GUISTATE_GET_TIME, &app->GrabTimeout);
										app->GrabTimeout += 1000;
									}
								}

								// Handle CTLGLOBAL_AUTO_VAL. Note: App must do GuiCtlUpdate(), except for
								// AREA ctl with CTLFLAG_AREA_LIST
								if (temp->Flags.Global & CTLGLOBAL_AUTO_VAL)
								{
									switch (temp->Type)
									{
										case CTLTYPE_RADIO:
											if (temp->Attrib.Value == temp->Select) goto again;
											temp->Attrib.Value = temp->Select;
											break;
										case CTLTYPE_CHECK:
											temp->Attrib.Value ^= (1 << (temp->Select - 1));
											break;
										case CTLTYPE_ARROWS:
										{
											// If AREA LIST helper, process selection change on AREA.
											// Otherwise normal arrow value inc/dec
											if (temp->Flags.Local & CTLFLAG_AREA_HELPER) goto list;
											if (!GuiCtlArrowsValue(app, temp)) goto again;
											break;
										}
										case CTLTYPE_AREA:
										{
											// A fake click from KeyPress?
											if (win->InternalFlags & GUIWIN_AREA_PROCESSED) goto areap;

											if (temp->Flags.Local & CTLFLAG_AREA_LIST)
											{
list:											app->Msg.Mouse.ListAction = GuiListMouse(app, temp);
areap:										win->InternalFlags &= ~GUIWIN_AREA_PROCESSED;
												if (app->Msg.Mouse.ListAction == GUILIST_IGNORE) goto again;
											}
										}
									}
								}
							}
						}
					}
				}
				else if (((win->Win.Flags & GUIWIN_ALL_MOUSEBTN) && ret == GUIMOUSE_DOWN))
				{
					ret = GUIMOUSE_OTHERDOWN;
					goto release;
				}
			}	// RAW_MOUSE
release2:
			app->Msg.Mouse.Direction = ret & ~GUIMOUSE_OTHERDOWN;
			ret = GUI_MOUSE_CLICK;
			break;
		}

		case ButtonRelease:
		{
			// Mouse grab ends
			app->GrabCtl = 0;
			app->GrabTimeout = 0;

			if ((!(app->InternalAppFlags & GUIAPP_MODAL) || (win->InternalFlags & GUIWIN_MODAL)) &&
				((win->Win.Flags & GUIWIN_WANT_MOUSEUP) && !(win->InternalFlags & GUIWIN_HELP)))
			{
				app->Msg.Mouse.X = (uint16_t)app->XMsg.xbutton.x;
				app->Msg.Mouse.Y = (uint16_t)app->XMsg.xbutton.y;
				ret = GUIMOUSE_UP;

				// Does app want GUIMSG->SelectedCtl set to the ctl released upon (for mouse left button)?
				app->Host.DClickFlag = 0;
				app->Msg.Mouse.SelectedCtl = 0;
				if ((app->Msg.Mouse.ButtonNum = app->XMsg.xbutton.button) != 1 || (win->Win.Flags & GUIWIN_GENERIC_UP)) goto release2;

				// Yes. Set SelectedCtl, but note that mouse button release doesn't change the (win->CurrentCtl) selection
				win->Win.Flags |= GUIWIN_NO_UPD_CURR;
				goto release;
			}
			goto again;
		}

		case KeyRelease:
		{
			register XEvent *	ptr;

			// Mouse grab ends
			app->GrabCtl = 0;
			app->GrabTimeout = 0;

			// Eliminate key repeat except for arrow keys
			if (app->XMsg.xkey.keycode != Arrows[0] &&
				app->XMsg.xkey.keycode != Arrows[1] &&
				app->XMsg.xkey.keycode != Arrows[2] &&
				app->XMsg.xkey.keycode != Arrows[3])
			{
				ptr = (XEvent *)((char *)&app->Filename[PATH_MAX] - sizeof(XEvent));
				if (XEventsQueued(app->XDisplay, QueuedAfterReading))
				{
					XPeekEvent(app->XDisplay, ptr);

					if (ptr->type == KeyPress && ptr->xkey.time == app->XMsg.xkey.time && ptr->xkey.keycode == app->XMsg.xkey.keycode)
					{
						XNextEvent(app->XDisplay, ptr);
						goto again;
					}
				}
			}
			if (!(win->Win.Flags & GUIWIN_WANT_KEYUP) || (win->InternalFlags & (GUIWIN_HELP|GUIWIN_MODAL))) goto again;
			goto press;
		}

		case KeyPress:
		{
			// Mouse grab ends
			app->GrabCtl = 0;
			app->GrabTimeout = app->Host.DClickFlag = 0;

//			app->Host.PreviousTime = app->Host.CurrTime;
//			app->Host.CurrTime = app->XMsg.xkey.time;

press:	ret = XLookupKeysym((XKeyEvent *)&app->XMsg, 0);

			// Store arrow raw codes so our keyrelease code above can filter out
			// repeated keys, except for arrows
			if (ret >= XK_Left && ret <= XK_Down)
				Arrows[ret - XK_Left] = app->XMsg.xkey.keycode;

			if (win->InternalFlags & GUIWIN_HELP)
			{
				if (help_keypress(app, win, ret)) goto finish_help;
				goto again;
			}

			if ((app->InternalAppFlags & GUIAPP_MODAL) && !(win->InternalFlags & GUIWIN_MODAL)) goto again;

			if ((win->InternalFlags & GUIWIN_NUMPAD) && ((ret >= '0' && ret <= '9') || ret == XK_Return || ret == XK_Escape)) goto numpad;

			if (!(win->Win.Flags & GUIWIN_RAW_KEY))
			{
				register GUICTL *		temp;

				// if GUIWIN_HELP_KEY, then F1 selects help
				if ((win->Win.Flags & GUIWIN_HELP_KEY) && ret == XK_F1) goto dohelp;

				temp = win->CurrentCtl;
				if (temp == (GUICTL *)app) temp = 0;

				// If edit ctl or app set GUIWIN_KEYCAP, translate virtual to keycap. Do this before overwriting XMsg!!!
				if ((temp && temp->Type == CTLTYPE_EDIT) || (win->Win.Flags & GUIWIN_KEYCAP)) virtkey_to_keymap(app, 0);
				else app->Msg.Key.Keycap = 0;

				app->Msg.Key.Direction = (app->XMsg.type == KeyRelease);
				app->Msg.Key.Flags = (unsigned char)app->XMsg.xkey.state;
				app->Msg.Key.Code = ret;

				// Get currently selected ctl if CurrentCtl not previously set
				if (!temp) temp = GuiCtlGetSelect(app, 0);

				if (!app->Msg.Key.Direction) switch (ret)
				{
					case XK_Escape:
					{
						// App wants us to handle ESC key by translating to mouse click on CTLFLAG_ESC PUSH button?
						if (win->Win.Flags & GUIWIN_ESC_KEY)
						{
							if ((temp = win->Win.Ctls))
							{
								do
								{
									while (temp->Type)
									{
										if (temp->Type == CTLTYPE_PUSH && (temp->Flags.Local & CTLFLAG_ESC)) goto fakeclick3;
										temp++;
									}
								} while ((temp = temp->Next));
							}
							temp = &app->Msg.PresetCtls[0];
							memset(temp, 0, sizeof(GUICTL));
							temp->PresetId = GUIBTN_ESC;
							temp->Flags.Global = CTLGLOBAL_PRESET;
							goto fakeclick3;
						}

						if (temp && temp->Type == CTLTYPE_EDIT && (temp->Flags.Local & CTLFLAG_WANT_RETURN)) goto retkey;
						break;
					}

					case XK_Return:
					{
						if (temp)
						{
							register unsigned char	type;

							type = temp->Type & CTLTYPE_ENUM_MASK;

							// If an EDIT ctl, always return ENTER key as GUI_KEY_PRESS, not a mouse click.
							// This differentiates between "entering" and "leaving" the ctl, since entering
							// (ie, selecting) the ctl will result in a GUI_MOUSE_CLICK event, whereas leaving
							// results in this XK_Return GUI_MOUSE_CLICK
							if (type == CTLTYPE_EDIT)
							{
								if (temp->Flags.Local & CTLFLAG_WANT_RETURN) goto retkey;
							}

							// App wants us to handle ENTER key by translating to mouse click?
							else if (type != CTLTYPE_AREA && win->Win.Flags & GUIWIN_ENTER_KEY)
							{
								// PUSH controls have no visual reaction to user interaction, so don't update. Also,
								// if caller wants to handle updating this ctl, don't do it here
								if (type > CTLTYPE_PUSH && !(temp->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, temp, 0, win->CurrentCtlY);

								// Fake a mouse click
fakeclick3:					app->Msg.Key.SelectedCtl = temp;
fakeclick2:					app->Msg.Mouse.ButtonNum = 1;
								ret = GUIMOUSE_DOWN;
								goto fakeclick;
							}
						}

						break;
					}

					case XK_Tab:
					{
						register GUIMENU *	menu;

						// Shift-Tab rotates the menubar selection if app wants
						if ((win->Win.Flags & GUIWIN_SHIFTTAB_KEY) && (ShiftMask & app->Msg.Key.Flags) && (menu = win->Win.Menu))
						{
							ret = menu->Select + 1;
							if (ret >= (menu->LabelCnt & 0x0F)) ret = 0;
							goto menu_done;
						}

						// Tab key selects next focusable GUICTL if app wants
						if (win->Win.Flags & GUIWIN_TAB_KEY)
						{
							GuiCtlNext(app, 0);

							// If we just "tabbed out" of an edit ctl, then report this as an XK_Tab GUI_KEY_PRESS
							// so app can regard it as losing the focus. But only if WANT_RETURN
							if (temp && temp->Type == CTLTYPE_EDIT && (temp->Flags.Local & CTLFLAG_WANT_RETURN)) goto retkey;
							goto again;
						}

						break;
					}

					default:
					{
						// If an edit ctl, and app doesn't want to handle the user input, then do it here
						if (!(win->Win.Flags & GUIWIN_EDIT_AUTOPROCESS) && GuiCtlEditboxKey(app)) goto again;
					}
				}

				// If GUIWIN_LIST_KEY, and user pressed ESC, ENTER, PAGE up/down, arrow up/down
				// then user wants the AREA LIST (or first LIST) selected for this keystroke
				if (win->Win.Flags & GUIWIN_LIST_KEY)
				{
					switch (ret)
					{
						case XK_Page_Down:
						case XK_Page_Up:
						case XK_Up:
						case XK_Down:
						case XK_Escape:
				//		case XK_space:
						case XK_Return:
						{
							temp = findListCtl(win, temp);
						}
					}
				}

				// If an AUTO_VAL AREA LIST, process key strokes and convert to mouse event
				if (temp)
				{
					if (temp->Type == CTLTYPE_ARROWS && (temp->Flags.Local & CTLFLAG_AREA_HELPER)) temp--;
					if (temp->Type == CTLTYPE_AREA && (temp->Flags.Global & CTLGLOBAL_AUTO_VAL) && (temp->Flags.Local & CTLFLAG_AREA_LIST))
					{
						if ((app->Msg.Mouse.ListAction = GuiListKey(app, temp)) != GUILIST_IGNORE)
						{
							win->InternalFlags |= GUIWIN_AREA_PROCESSED;
							goto fakeclick3;
						}
					}
				}

				// If no ctl was selected when user pressed a key, does the app want it anyway?
				if (!(app->Msg.Key.SelectedCtl = temp) && !(win->Win.Flags & GUIWIN_BACKGND_KEY)) goto again;
			}
			else
			{
				if (win->Win.Flags & GUIWIN_KEYCAP) virtkey_to_keymap(app, 1);
				app->Msg.Key.Direction = (app->XMsg.type == KeyRelease);
				app->Msg.Key.Flags = (unsigned char)app->XMsg.xkey.state;
				app->Msg.Key.Code = ret;
			}

retkey:	ret = GUI_KEY_PRESS;
			break;
		}

		case ClientMessage:
		{
			// Some app thread sent a proprietary msg to this gui thread via GuiWinSignal()?
client:	if (app->XMsg.xclient.message_type == 1000 && !(win->InternalFlags & GUIWIN_HELP))
			{
				app->Msg.Signal.Cmd = app->XMsg.xclient.data.l[0];
				ret = GUI_SIGNAL;
				break;
			}

			// Window close?
			if (app->XMsg.xclient.message_type == WmProtocolsAtom && app->XMsg.xclient.data.l[0] == WmDeleteWindowAtom)
			{
closew:		free_help(app, win);

				ret = GUI_WINDOW_CLOSE;
				break;
			}
			// fall through
		}

		default:
			goto again;
	}
send_to_app:
	// Caller may overwrite any temp array of GUICTLs, so discard it
	app->InternalAppFlags &= ~GUIAPP_LIST_MADE;

	app->Msg.Type = ret;
	return &app->Msg;
}






static void virtkey_to_keymap(register GUIAPPHANDLE app, register unsigned char israw)
{
	KeySym				keysym;
	register char *	buffer;

	buffer = &app->Filename[0] + sizeof(XKeyEvent);
	XLookupString((XKeyEvent *)&app->XMsg, buffer, ((char *)&app->Filename[PATH_MAX] - buffer) - 1, &keysym, 0);
	app->Msg.Key.Keycap = buffer[0];

	if (israw)
	{
		register char *		temp;

		temp = (char *)&app->Msg.Key.Pad;
		while ((*temp++ = *buffer++));
	}
}





/******************* get_clipboard() *******************
 * Copies the XWindows clipboard to the currently selected
 * CTLTYPE_EDIT GUICTL.
 */

static void get_clipboard(register GUIAPPHANDLE app, register GUIWIN * win)
{
	register Window		txwin;
	Atom						type;
	int						format;
	unsigned long			len, bytes_left;
	unsigned char *		data;
	Atom						XA_CLIPBOARD;

	// XA_CLIPBOARD is not an internal atom defined in Xatom.h
	XA_CLIPBOARD = XInternAtom(app->XDisplay, "CLIPBOARD", 0);

	// Get the xwindow that owns the specified selection (XA_CLIPBOARD)
	txwin = XGetSelectionOwner(app->XDisplay, XA_CLIPBOARD);
	if (txwin)
	{
		// Get data from XA_CLIPBOARD and put in XA_PRIMARY property
		XConvertSelection(app->XDisplay, XA_CLIPBOARD, XA_STRING, XA_PRIMARY, txwin, CurrentTime);
		XGetWindowProperty(app->XDisplay, txwin, XA_PRIMARY, 0, 10000000L, 0, XA_STRING, &type, &format, &len, &bytes_left, &data);

		//data will have the string
		if (len >= win->CurrentCtl->Attrib.MaxChars) len = win->CurrentCtl->Attrib.MaxChars - 1;
		memcpy(win->CurrentCtl->Buffer, data, len);
		win->CurrentCtl->CursorPos = len;
		win->CurrentCtl->Buffer[len] = 0;

		XFree(data);
		XFlush(app->XDisplay);
	}
}





/******************* GuiCtlEditboxKey() *******************
 * Handles user key input for any currently selected
 * CTLTYPE_EDIT GUICTL.
 */

int GuiCtlEditboxKey(GUIAPPHANDLE app)
{
	register GUIWIN * 	win;
	register GUICTL *		ctl;

	win = app->Host.CurrentWin;

	if ((ctl = win->CurrentCtl) && (ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_EDIT)
	{
		register uint32_t	keycode;
		register char	*	from;

		from = &ctl->Buffer[ctl->CursorPos];
		keycode = app->Msg.Key.Code;
		switch (keycode)
		{
			case XK_BackSpace:
			{
				if (from > ctl->Buffer)
				{
					register char	*to;

					--ctl->CursorPos;
					if (!from[0])
						from[-1] = 0;
					else
					{
						to = from - 1;
						while ((*to++ = *from++));
					}

					goto redraw;
				}
				goto ret1;
			}

			case XK_Delete:
			{
				if (from[0])
				{
					register char	*end;

					end = from + 1;
					while ((*from++ = *end++));
					goto redraw;
				}
				goto ret1;
			}

			case XK_Home:
			{
				ctl->CursorPos = 0;
				goto redraw;
			}

			case XK_End:
			{
				ctl->CursorPos += strlen(from);
				goto redraw;
			}

			case XK_Right:
			{
				if (from[0]) ++ctl->CursorPos;
				goto redraw;
			}

			case XK_Left:
			{
				if (ctl->CursorPos) --ctl->CursorPos;
				goto redraw;
			}

			default:
			{
//printf("%08x %08x\r\n", app->Msg.Key.Flags, keycode);
				if (app->Msg.Key.Flags == 0x14 && keycode == 'v')
				{
					get_clipboard(app, win);
					goto redraw;
				}
				from += strlen(from);
				if (keycode >= ' ' && keycode <= 0x7F && from < ctl->Buffer + ctl->Attrib.MaxChars - 1)
				{
					if (!(ctl->Flags.Local & (CTLFLAG_NUMERIC|CTLFLAG_U_NUMERIC|CTLFLAG_INTEGER)) || (keycode >= '1' && keycode <= '9')) goto store;
					if (keycode == '.' && !(ctl->Flags.Local & CTLFLAG_INTEGER))
					{
						register const char	*to;

						to = &ctl->Buffer[0];
						do
						{
							if (*to == '.') goto ret1;
						} while (*to++);
						goto store;
					}
					if (!ctl->CursorPos)
					{
						if (!(ctl->Flags.Local & CTLFLAG_U_NUMERIC) && keycode == '-') goto store;
					}
					else if (keycode == '0')
					{
store:				from[1] = 0;
						while (from-- > &ctl->Buffer[ctl->CursorPos]) from[1] = from[0];
						*(ctl->Buffer + ctl->CursorPos++) = app->Msg.Key.Keycap;
redraw:				GuiCtlUpdate(app, win, ctl, win->Win.Ctls, 0);
					}
ret1:				return 1;
				}
			}
		}
	}

	return 0;
}





/******************* GuiWinRect() ******************
 * Draws a rectangle in current window. If rect = 0,
 * fills the window with the specified color.
 */

void GuiWinRect(GUIAPPHANDLE app, GUIBOX * rect, uint32_t color)
{
	register GUIWIN *	win;
	register cairo_t *	currentCairo;

	win = app->Host.CurrentWin;
	currentCairo = win->CairoGraphics;

	if (!rect)
	{
		GuiTextSetColor(win, color);
		cairo_rectangle(currentCairo, win->Win.WinPos.X, win->Win.WinPos.Y, win->Win.WinPos.Width, win->Win.WinPos.Height);
		goto fill;
	}

	GuiTextSetColor(win, GUICOLOR_BLACK);
	cairo_rectangle(currentCairo, rect->X, rect->Y, rect->Width, rect->Height);
	cairo_stroke(currentCairo);

	if (color)
	{
		GuiTextSetColor(win, color);
		cairo_rectangle(currentCairo, rect->X + 1, rect->Y + 1, rect->Width - 1, rect->Height - 1);
fill:	cairo_fill(currentCairo);
	}
}





/******************* GuiWinUpdate() ******************
 * Invalidates the entire area of window, thereby causing an
 * Expose message to be sent to it.
 */

void GuiWinUpdate(GUIAPPHANDLE app, GUIWIN * win)
{
	if (!(win->InternalFlags & GUIWIN_SKIP_REDRAW))
	{
#ifdef USE_CAIRO_DIRTY
		cairo_surface_mark_dirty(win->CairoWindow);
#else
		XClearArea(app->XDisplay, win->BaseWindow, 0, 0, 0, 0, True);
#endif
		win->InternalFlags |= (GUIWIN_SKIP_REDRAW|GUIWIN_AREA_RECALC);
	}
}




/******************* GuiWinAreaUpdate() ******************
 * Invalidates the specified area of window. If rec = 0,
 * invalidates the entire win, and deselects any ctl in it.
 */

void GuiWinAreaUpdate(GUIAPPHANDLE app, GUIWIN * win, GUIBOX * rec)
{
	if (!rec)
	{
		win->CurrentCtl = 0;
		GuiWinUpdate(app, win);
	}
	else
	{
#ifdef USE_CAIRO_DIRTY
		cairo_surface_mark_dirty_rectangle(win->CairoWindow, rec);
#else
		XClearArea(app->XDisplay, win->BaseWindow, rec->X, rec->Y, rec->Width, rec->Height, True);
#endif
	}
}





/**************** GuiWinSetHeading() *****************
 * Sets or clears a string of text that is automatically
 * drawn centered at the top of a window whenever GuiCtlDraw()
 * is called.
 *
 * msg =		Nul-terminated string, or 0 if clearing.
 * flags =	GUIDRAW_Y_CENTER for centered text.
 * color =	Text color (ie GUICOLOR_RED).
 *
 * NOTE: msg buffer must not be freed until text is cleared.
 */

void GuiWinSetHeading(GUIAPPHANDLE app, GUIWIN * win, const char * msg, unsigned char flags, unsigned char color)
{
	// This here window can't simultaneously be an auto-help and modal. Our
	// logic doesn't support it
	free_help(app, win);

	if ((win->MsgBox.Str = msg)) win->InternalFlags |= GUIWIN_STRING;
	win->MsgBox.DrawFlags = flags;
	win->MsgBox.Color = color;
}




/******************* GuiWinModal() ******************
 * Begins or ends "modal mode" for the current window.
 *
 * msg =		Nul-term string to display for heading, or 0
 * 			if none.
 * flags =	Bitmask of what lower preset buttons to include,
 * 			or -1 if ending modal mode.
 *
 * When we make a window modal, all other (not modal) windows still get
 * SIZE, CLOSE, and DRAW messages. That's because, if a window doesn't
 * respond to those, some window managers assume the app is hung.
 * But the other windows don't receive any MOUSE, KEY, MENU, or
 * HELP msgs. So the user can't interact with them.
 *
 * NOTE: Our design allows more than 1 window to simultaneously be
 * modal.
 *
 * Caller can't use GUIAPP'S TEMPBUF for msg.
 */

uint32_t GuiWinModal(GUIAPPHANDLE app, const char * msg, uint32_t flags)
{
	register GUIWIN *		win;

	// Get the current window which we're going to make modal
	if (!(win = app->Host.CurrentWin)) win = &app->MainWin;

	if (flags != (uint32_t)-1)
	{
		GuiWinSetHeading(app, win, msg, GUIDRAW_Y_CENTER, GUICOLOR_BLUE);
		win->Win.Menu = 0;
		win->Win.Ctls = 0;
		win->Win.UpperPresetBtns = 0;
		win->CurrentCtl = 0;
		if ((win->Win.LowerPresetBtns = (flags & GUIBTN_SHOWMASK) ? (flags & GUIBTN_SHOWMASK) | GUIBTN_CENTER : 0) && (flags & 0x000F0000))
		{
			win->Preset.Num = ((flags >> 16) & 0x0f) - 1;
			win->Preset.Which = 0;
			win->CurrentCtl = (GUICTL *)app;
		}
		win->Win.Flags = (flags & GUIBTN_ESC_SHOW) ? GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_GENERIC_UP|GUIWIN_ESC_KEY : GUIWIN_TAB_KEY|GUIWIN_ENTER_KEY|GUIWIN_GENERIC_UP;

		// Indicate bypass of some window behavior
		win->InternalFlags |= (GUIWIN_MODAL|GUIWIN_ERROR_DLG);
		app->InternalAppFlags |= GUIAPP_MODAL;

		// Does app want to do a timeout?
		if (flags & GUIBTN_TIMEOUT_SHOW)
		{
			// Get the amount of milliseconds to wait (5 secs)
			flags = 5000;
		}
		else
			flags = 0;
	}
	else
	{
		win->InternalFlags &= ~(GUIWIN_MODAL|GUIWIN_STRING|GUIWIN_ERROR_DLG);
		win->MsgBox.Str = 0;
		win->CurrentCtl = 0;

		// FLush fake size msgs
		app->InternalAppFlags |= GUIAPP_FLUSHMODAL;
	}

	// Indicate the window needs redrawing
	GuiWinUpdate(app, win);

	return flags;
}





/******************** GuiTextDrawMsg() ******************
 * Displays a string as centered text within a rectangular
 * area.
 *
 * Return: The Y coord of the bottom of the text, plus
 * spacing.
 */

uint32_t GuiTextDrawMsg(GUIAPPHANDLE app, const char * msg, GUIBOX * box, uint32_t flags)
{
	GUIWIN *						win;
	register char *			end;
	register char *			str;
	unsigned short *			lenArray;

	if (!(win = app->Host.CurrentWin)) win = &app->MainWin;

	if (flags & GUIDRAW_SMALLTEXT)
		GuiTextSetSmall(win);
	else
		GuiTextSetLarge(win);
	GuiTextSetColor(win, (flags >> 24) ? (flags >> 24) : GUICOLOR_BLACK);

	if (!msg) msg = NoMemStr;

	// Copy the msg over to our temp buffer. We need to alter the string,
	// and caller may have passed a const *. It's ok if this is the same
	// buffer. While we copy, let's remove redundant space and unprintable
	// characters
	//
	// We'll also break the msg into words, and get the pixel width of
	// each word
	{
	register char 				tempChr;
	cairo_text_extents_t		textExtents;

	str = end = (char *)&app->Msg;
	lenArray = (unsigned short *)((char *)&app->Msg + PATH_MAX);
	tempChr = ' ';
	while ((*str = *msg++))
	{
		// The end of another word?
		if (*str <= ' ' && tempChr > ' ')
		{
			// Nul-term it, and store its pixel width
			*str++ = 0;
			if (str >= (char *)lenArray) goto ovr;
			cairo_text_extents(win->CairoGraphics, end, &textExtents);
			*(--lenArray) = (unsigned short)textExtents.width;
			end = str;
		}
		else if (*str > ' ' || tempChr > ' ')
		{
			if (str + 4 >= (char *)lenArray) break;
			tempChr = *str++;
		}
	}
	*str = 0;
	if (end < str && str+1 < (char *)lenArray)
	{
		cairo_text_extents(win->CairoGraphics, end, &textExtents);
		*(--lenArray) = (unsigned short)textExtents.width;
	}
	}
ovr:
	// Pack the words into lines, and ensure that the last line is
	// within the average line width. Reduce "width" until that is
	// achieved
	{
	register unsigned short *	lenArrayTemp;
	register unsigned short		avg_width, lineWidth, spacing;
	uint32_t							numLines, width, width_shrink;

	width = box->Width - (box == &win->Win.WinPos ? (app->Host.GuiFont.QuarterWidth * 2) : 0);
	spacing = app->Host.GuiFont.Spacing;
	width_shrink = 0;
again:
	lenArrayTemp = (unsigned short *)((char *)&app->Msg + PATH_MAX);
	lineWidth = avg_width = numLines = 0;
	do
	{
		lineWidth += *(--lenArrayTemp);
		if (lineWidth > width)
		{
			if (lineWidth > *lenArrayTemp) lineWidth -= (*lenArrayTemp++ + spacing);
			avg_width += lineWidth;
			if (++numLines >= box->Height / app->Host.GuiFont.Height && !(flags & GUIDRAW_NODRAW))
			{
				width += width_shrink;
				goto excess2;
			}
			lineWidth = 0;
		}
		else
			lineWidth += spacing;
	} while (lenArrayTemp > lenArray);

	if (lineWidth)
	{
		if (numLines)
		{
			// See if this width produces a final line that is within "spacing * 8" of the average line width
			width_shrink = (app->Host.GuiFont.QuarterWidth * 2);
			if ((lineWidth < (avg_width / numLines) - (spacing<<3) || lineWidth > (avg_width / numLines) + (spacing<<3))
				&& width > width_shrink
				&& (numLines < box->Height / app->Host.GuiFont.Height || (flags & GUIDRAW_NODRAW)))
			{
				// Try again with a slightly shorter width
				width -= width_shrink;
				goto again;
			}
		}

		numLines++;
	}
excess2:
	// Center horizontally?
	if (flags & GUIDRAW_Y_CENTER)
	{
		avg_width = win->Win.LowerPresetBtns ? (app->Host.GuiFont.Height >> 3) + (app->Host.GuiFont.Height * 2) : 0;
		avg_width += (numLines * app->Host.GuiFont.Height);
		if (avg_width > box->Height) avg_width = box->Height;
		numLines = (box->Height - avg_width) >> 1;
	}
	else
		numLines = 0;
	if (box != &win->Win.WinPos)
	{
		numLines += box->Y;
		avg_width = box->X;
	}
	else
		avg_width = 0;

	// ======================
	// Form sentences from individual words

	end = (char *)&app->Msg;
	lenArrayTemp = (unsigned short *)((char *)&app->Msg + PATH_MAX);
	goto start2;
	do
	{
		// Does the next word fit on this line?
		lineWidth += *(--lenArrayTemp);
		if (lineWidth > width)
		{
			// If only 1 word and it doesn't fit, skip it
			if (end <= str)
				while (*end++);
			else
			{
				lineWidth -= (*lenArrayTemp++ + spacing);
       		if (!(flags & GUIDRAW_NODRAW))
print:     		GuiTextDraw(app, avg_width + ((box->Width - lineWidth) >> 1), numLines, str);
       	}

       	// Next line Y pos. Can we fit another line?
			numLines += app->Host.GuiFont.Height;
			if (numLines + app->Host.GuiFont.Height > box->Y + box->Height && !(flags & GUIDRAW_NODRAW)) goto excess;

			// New empty line
start2:	str = end;
			lineWidth = 0;
		}
		else
		{
			// Append the next word to this line, and put a space between any preceding word
			if (end > str) *(end - 1) = ' ';
			while (*end++);

			// Allow for a space between the next word
			lineWidth += spacing;
		}
		// Another word?
	} while (lenArrayTemp > lenArray);

	// Any partial final line?
	if (end > str)
	{
		if (!(flags & GUIDRAW_NODRAW)) goto print;
		numLines += app->Host.GuiFont.Height;
	}

excess:
	numLines += (app->Host.GuiFont.Height >> 3);

	return numLines;
	}
}





/*********************** GuiErrShow() ***********************
 * Displays the specified string in current window, using
 * "modal mode", and returns when the user clicks upon a
 * response button.
 *
 * msg =		String to display.
 * flags =	GUIBTN_SHOW symbols mask, plus GUIBTN_DEFAULT
 *				macro to select the default button.
 *
 * RETURNS: GUIBTN_OK, _CANCEL... etc... _TIMEOUT, or _QUIT
 *
 * Caller can't use GUIAPP'S TEMPBUF for msg.
 */

uint32_t GuiErrShow(GUIAPPHANDLE app, const char * msg, uint32_t flags)
{
	GUIWIN *					win;
	struct GUIWINSTRUCT	orig;
	struct GUIMODALVARS	msgBox;
	uint32_t					stringFlag;

	// Get the current window in which we draw the msg
	if (!(win = app->Host.CurrentWin)) win = &app->MainWin;

	// Save app settings temporarily, so we can restore them on return
	memcpy(&orig, win, sizeof(struct GUIWINSTRUCT));
	memcpy(&msgBox, &win->MsgBox, sizeof(struct GUIMODALVARS));
	stringFlag = win->InternalFlags & GUIWIN_STRING;

	if (win->Win.Flags & GUIWIN_TIMEOUT_ERRS) flags |= GUIBTN_TIMEOUT_SHOW;

	// Setup for modal GuiWinGetMsg()
	flags = GuiWinModal(app, msg, flags);

	// If the app called GuiErrShow(), and then called it again with no GuiWinGetMsg()
	// inbetween, there may be some pending FAKESIZE events we have to generate. We
	// don't want to catch them on our own GuiWinGetMsg call below. After all, the whole
	// idea of FAKESIZE is to let the app know of any window size messages GuiErrShow
	// or an auto help (GUIWIN_HELP) window siphon off without the app's knowledge. So
	// make sure FLUSHMODAL is not set (until GuiErrShow is ready to return)
	app->InternalAppFlags &= ~GUIAPP_FLUSHMODAL;

	XBell(app->XDisplay, 50);

	for (;;)
	{
		{
		register GUIMSG *		msg;

		if (!(msg = GuiWinGetTimedMsg(app, flags)))
		{
			flags = GUIBTN_ERROR;
			goto out;
		}
		}

		{
		register GUIWIN *		currentWinGui;

		currentWinGui = app->Host.CurrentWin;

		switch (app->Msg.Type)
		{
			case GUI_TIMEOUT:
				flags = GUIBTN_TIMEOUT;
				goto out;

			case GUI_WINDOW_SIZE:
			{
				// Postpone informing the app about window resizing until
				// after the msg box is dismissed
				currentWinGui->InternalFlags |= GUIWIN_FAKESIZE;
				if (currentWinGui == win) memcpy(&orig.WinPos, &currentWinGui->Win.WinPos, sizeof(GUIBOX));
				break;
			}

			case GUI_MOUSE_CLICK:
			{
				if (currentWinGui == win && app->Msg.Mouse.ButtonNum == 1)
				{
					register GUICTL *		temp;

					if ((temp = app->Msg.Mouse.SelectedCtl))
					{
						flags = temp->PresetId;
						goto out;
					}
				}

				break;
			}

			case GUI_WINDOW_CLOSE:
				flags = GUIBTN_QUIT;
				goto out;
		}
		}
	}
out:
	// Restore "current win" upon entry (in case it got changed during WinGetMsg above)
	app->Host.CurrentWin = win;
	memcpy(win, &orig, sizeof(struct GUIWINSTRUCT));

	GuiWinModal(app, 0, (uint32_t)-1);

	memcpy(&win->MsgBox, &msgBox, sizeof(struct GUIMODALVARS));
	win->InternalFlags |= stringFlag;

	return flags;
}





/****************** GuiCtlCenterBtns() ****************
 * Centers buttons along the x axis in the window.
 *
 * ctls = The first button (on the left) of the group.
 * NOTE: Buttons must be in consecutive order.
 */

void GuiCtlCenterBtns(GUIAPPHANDLE app, GUIWIN * win, GUICTL * ctls, uint32_t numCtls)
{
	register unsigned char	i;
	register uint32_t			width, x;

	i = numCtls;

	// Get the total width of all ctls to be centered
	width = 0;
	while (i--)
	{
		if (width) width += ((ctls[i].Flags.Global & CTLGLOBAL_NOPADDING) ? app->Host.GuiFont.QuarterWidth : (app->Host.GuiFont.Height * 2));
		width += ctls[i].Width;
 	}

	if (win->Win.WinPos.Width <= width)
		x = 0;
	else
		x = (win->Win.WinPos.Width - width) / 2;

	i = 0;
	while (numCtls--)
	{
		if (i) x += ((ctls[i].Flags.Global & CTLGLOBAL_NOPADDING) ? app->Host.GuiFont.QuarterWidth : (app->Host.GuiFont.Height * 2));
		ctls[i].X = x;
		x += ctls[i++].Width;
 	}
}





/******************* GuiCtlArrowsInit() ********************
 * Enables/disables the up/down buttons in an ARROWS ctl,
 * depending upon its current value.
 *
 * NOTE: Caller must GuiCtlUpdate().
 */

void GuiCtlArrowsInit(GUICTL * ctl, unsigned char val)
{
	ctl->Flags.Local &= ~(CTLFLAG_NO_UP|CTLFLAG_NO_DOWN);
	if (!(ctl->Attrib.Value = val)) ctl->Flags.Local |= CTLFLAG_NO_DOWN;
	if (val >= ctl->Attrib.NumOfLabels - 1) ctl->Flags.Local |= CTLFLAG_NO_UP;
}

/******************* GuiCtlArrowsValue() ********************
 * Incs/decs the current value of an ARROWS ctl, depending upon
 * whether user clicked its up or down button.
 *
 * RETURN: 0 if value limit reached.
 *
 * NOTE: Calls GuiCtlArrowsInit.
 *
 * NOTE: Caller must GuiCtlUpdate().
 */

unsigned char GuiCtlArrowsValue(GUIAPPHANDLE app, GUICTL * ctl)
{
	if (ctl->Flags.Local & CTLFLAG_DOWN_SELECT)
	{
		if (ctl->Attrib.Value)
		{
			ctl->Attrib.Value--;
			goto redraw;
		}
	}
	else if ((ctl->Flags.Local & CTLFLAG_UP_SELECT) && ctl->Attrib.Value < ctl->Attrib.NumOfLabels - 1)
	{
		ctl->Attrib.Value++;
redraw:
		GuiCtlArrowsInit(ctl, ctl->Attrib.Value);
		return 1;
	}

	if (ctl == app->GrabCtl)
	{
		app->GrabCtl = 0;
		app->GrabTimeout = 0;
	}
	return 0;
}




/******************* setWidthToLongLabel() ********************
 * Gets/stores width of the longest label in a group of buttons
 * (without CTLGLOBAL_SMALL).
 */

static void setWidthToLongLabel(register GUIWIN * win, register GUICTL * ctl)
{
	register const char *	label;
	register uint32_t			size, width;
	register unsigned char	numLabels;
	char							temp[64];

	label = ctl->Label;
	size = 0;
	if ((numLabels = ctl->Attrib.NumOfLabels))
	{
		if ((ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_ARROWS || ((ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_RADIO && !(ctl->Flags.Local & CTLFLAG_LABELBOX))) numLabels++;

		do
		{
			for (width=0; width<63; width++)
			{
				if (!(temp[width] = *label++) || temp[width] == '\n') break;
			}
			if (!temp[width]) numLabels--;
			temp[width] = 0;

			width = GuiTextWidth(win, temp);
			if (width > size) size = width;

		} while (numLabels);
	}

	ctl->Width = (unsigned short)size;
}





/******************* getEditLabelWidth() ********************
 * Returns width of EDIT ctl's label.
 */

static unsigned short getEditLabelWidth(GUIAPPHANDLE app, register GUIWIN * win, register GUICTL * ctl)
{
	register const char		*ptr;
	register unsigned char	len;

	ptr = win->Win.EditLabels;
	len = ctl->Attrib.LabelNum;
	while (len--) ptr += (strlen(ptr) + 1);
	return GuiTextWidth(win, ptr) + (app->Host.GuiFont.QuarterWidth);
}





/******************* getCompactWidth() ********************
 * Calcs total width of a group of buttons that specify
 * CTLGLOBAL_SMALL.
 */

void getCompactWidth(GUIAPPHANDLE app, GUIWIN * win, GUICTL * ctl, register const char * label)
{
	register unsigned short	width;
	register unsigned char	cnt;

	width = 0;
	if ((cnt = ctl->Attrib.NumOfLabels))
	{
		for (;;)
		{
			if (*label)
			{
				// Add width of 1 button + spacing between buttons
				width += app->Host.GuiFont.Height + 8;

				// Add label width + spacing to the next button
				width += GuiTextWidth(win, label) + (app->Host.GuiFont.CharWidth * 2);
			}

			if (!--cnt) break;
			while (*label++);
		}
	}

	// Set final calculated width
	ctl->Width = width + ((ctl->Flags.Local & CTLFLAG_LABELBOX) ? 8 : 0);
}





void GuiCtlSetWidth(GUIAPPHANDLE app, GUIWIN * win, GUICTL * ctl)
{
	register unsigned char	type;
	register const char *	label;
	char							buffer[64];

	if (ctl->Flags.Global & CTLGLOBAL_GET_LABEL)
		label = ctl->BtnLabel(0, ctl, buffer);		// get longest label
	else
		label = ctl->Label;

	if ((type = (ctl->Type & CTLTYPE_ENUM_MASK)))
	{
		switch (type)
		{
			case CTLTYPE_EDIT:
				// Width = Character limit * CharWidth
				ctl->Width = (unsigned short)(app->Host.GuiFont.CharWidth * ctl->Attrib.MaxChars);
				break;

			case CTLTYPE_ARROWS:
			{
				if (label)
				{
					// If no string values (ie, numeric only), use only the label width
					if ((ctl->Flags.Local & CTLFLAG_NOSTRINGS) | (ctl->Flags.Global & CTLGLOBAL_GET_LABEL)) ctl->Width = GuiTextWidth(win, label);
					else setWidthToLongLabel(win, ctl);
				}

				// Add the width of 2 arrows
arrow:		ctl->Width += (app->Host.GuiFont.Height + 2) * 2;
				break;
			}

			case CTLTYPE_AREA:
				// For non-static area, min width is helper ARROWS or 0.
				// Since there is a CTLTYPE_END, it's always safe to look
				// ahead one
				if (ctl[1].Type == CTLTYPE_ARROWS && (ctl[1].Flags.Local & CTLFLAG_AREA_HELPER)) goto arrow;
				break;

			case CTLTYPE_CHECK:
			{
				if (!(ctl->Flags.Global & CTLGLOBAL_SMALL)) goto def;
			}
			// fall through

			case CTLTYPE_RADIO:
			{
				getCompactWidth(app, win, ctl, label);
				break;
			}

			default:
//			case CTLTYPE_PUSH:
//			case CTLTYPE_STATIC:
			{
def:			if (ctl->Flags.Global & CTLGLOBAL_GET_LABEL)
					ctl->Width = GuiTextWidth(win, label);
				else
				{
					// Width = longest label width + padding
					setWidthToLongLabel(win, ctl);
					if (type == CTLTYPE_PUSH) ctl->Width += (app->Host.GuiFont.CharWidth * ((ctl->Flags.Global & CTLGLOBAL_SMALL) ? 1 : 2));
				}
			}
		}
	}
}





/****************** set_group_count() *****************
 * Sets the count of how many ctls inside the group.
 *
 * ctl = First CUICTL in the group (with CTLGLOBAL_GROUPSTART)
 */

static GUICTL * set_group_count(register GUICTL * ctl)
{
	register GUICTL *			end;
	register unsigned char	type, levels;

	end = ctl + 1;
	levels = 0;
	while ((type = (end->Type & CTLTYPE_ENUM_MASK)))
	{
		if (type == CTLTYPE_GROUPBOX && !levels--) goto out;
		// Count embedded GROUP ctls
		if (end->Flags.Global & CTLGLOBAL_GROUPSTART) levels++;
		end++;
	}
	end--;
out:
	end->CursorPos = (end - ctl);
	return end;
}

/****************** set_group_width() *****************
 * Sets the width of a groupbox. set_group_count() must
 * have already been called.
 */

static void set_group_width(GUIAPPHANDLE app, GUIWIN * win, register GUICTL * groupCtl)
{
	register GUICTL *				ctl;
	register unsigned short		widest;

	ctl = groupCtl - groupCtl->NumOfCtls;
	if (!(widest = groupCtl->X)) groupCtl->X = ctl->X - GuiWinGetBound(app, win, GUIBOUND_XBORDER);
	do
	{
		register unsigned short	width;

		width = ctl->X;
		switch (ctl->Type & CTLTYPE_ENUM_MASK)
		{
			case CTLTYPE_CHECK:
			{
				if (!(ctl->Flags.Global & CTLGLOBAL_SMALL))
				{
					width += ((ctl->Width + app->Host.GuiFont.Height + 7) * ctl->Attrib.NumOfLabels) + ((ctl->Attrib.NumOfLabels - 1) * 8);
					break;
				}
			}
			// fall through

			default:
				width += ctl->Width;
		}

		if (widest < width) widest = width;
	} while (++ctl < groupCtl);

	groupCtl->Width = (widest - groupCtl->X) + GuiWinGetBound(app, win, GUIBOUND_XBORDER);
}

static void set_group_height(GUIAPPHANDLE app, GUIWIN * win, register GUICTL * groupCtl)
{
	register GUICTL *				ctl;
	register unsigned short		height, total, topY;

	// Start at the first ctl in group
	ctl = groupCtl - groupCtl->NumOfCtls;
	total = 0;
	if (!groupCtl->Y) groupCtl->Y = -1;
	do
	{
		topY = ctl->Y - ((app->Host.GuiFont.Height / 2) + 4);
		if (groupCtl->Y > topY) groupCtl->Y = topY;
		height = ctl->Y + GuiCtlGetHeight(app, ctl);
		if (total < height) total = height;
	} while (++ctl < groupCtl);

	// Store pixel height in GROUP's BottomY
	groupCtl->BottomY = total + GuiWinGetBound(app, win, GUIBOUND_YSPACE);
}

void GuiCtlAbsScale(GUIAPPHANDLE app, GUIWIN * win, GUICTL * baseArray)
{
	register unsigned short		height, lowY, total;
	register GUICTL *				ctl;

	ctl = baseArray;

	GuiTextSetLarge(win);

	total = 0;

	do
	{
		unsigned short					width;
		register unsigned char		type;

		width = lowY = 0;

		while ((type = ctl->Type & CTLTYPE_ENUM_MASK))
		{
			if (type == CTLTYPE_AREA && ctl->BottomY < ctl->Y) ctl->BottomY = ctl->Y;

			if (ctl->Flags.Global & CTLGLOBAL_GROUPSTART)
				set_group_count(ctl);

			if (!ctl->Width)
			{
				if (type == CTLTYPE_GROUPBOX)
					set_group_width(app, win, ctl);
				else
					GuiCtlSetWidth(app, win, ctl);
			}

			if (ctl->Flags.Global & CTLGLOBAL_X_ALIGN)
				ctl->X = baseArray[ctl->X].X;		// aligned to a preceding ctl

			if (type == CTLTYPE_GROUPBOX && !ctl->BottomY)
				set_group_height(app, win, ctl);
			height = GuiCtlGetHeight(app, ctl);
			if (lowY < ctl->Y + height) lowY = ctl->Y + height;
			if (width < ctl->X + ctl->Width) width = ctl->X + ctl->Width;
			ctl++;
		}

		// Store CTLTYPE_END's LayerWidth, LayerHeight
		ctl->LayerHeight = lowY;
		ctl->LayerWidth = width + GuiWinGetBound(app, win, GUIBOUND_XBORDER);
		if (win->Win.MinWidth < ctl->LayerWidth) win->Win.MinWidth = ctl->LayerWidth;
		total += lowY;
	} while ((ctl = ctl->Next));

	if (total > win->Win.WinPos.Height) total = win->Win.WinPos.Height;
	if (win->Win.MinHeight < total) win->Win.MinHeight = total;
}

/********************* GuiCtlScale() ********************
 * Positions/sizes the ctls based on font size.
 *
 * win =			GUIWIN for window containing ctls.
 * baseArray =	Ptr to base of GUICTL array.
 * yOffset =	Y start position in pixels (perhaps to account
 *					for any additional ctls above). -1 leaves space
 *					menubar.
 *
 * note: Uses GUIAPP'S TEMPBUF. CTLGLOBAL_GET_LABEL ctls must
 * not use it.
 */

#define GUICTL_GOT_GROUP			0x80
#define GUICTL_GOT_GROUPEND		0x40

#pragma pack(1)
struct GUILINE {
	unsigned short	XOffset;
	unsigned char	Height, Flags;
};
#pragma pack()

void GuiCtlScale(GUIAPPHANDLE app, GUIWIN * win, GUICTL * baseArray, uint32_t yOffset)
{
	register GUICTL *				ctl;
	register unsigned char		type;
	register struct GUILINE *	linePos;

	GuiTextSetLarge(win);

	if (yOffset == -1) yOffset = GUI_TITLEBAR_SPACE;

	do
	{
		linePos = (struct GUILINE *)(&app->Msg);
		memset(linePos, 0, PATH_MAX);

		// ==================================================
		// Set widths and X
		{
		register unsigned char		lineNum;
		unsigned short *				groupMargin;
		unsigned short					widest, leftMargin;

		widest = 0;
		groupMargin = (unsigned short *)((char *)linePos + PATH_MAX);
		leftMargin = GuiWinGetBound(app, win, GUIBOUND_XBORDER);
		ctl = baseArray;
		while ((type = (ctl->Type & CTLTYPE_ENUM_MASK)))
		{
			linePos = (struct GUILINE *)(&app->Msg);

			// Does this ctl start a group?
			if (ctl->Flags.Global & CTLGLOBAL_GROUPSTART)
			{
				register GUICTL *			groupCtl;
				register unsigned char	first, last;

				// Find the corresponding CTLTYPE_GROUPBOX, and set its count
				groupCtl = set_group_count(ctl);

				// Get the Left X for the groupbox. That would be the longest line
				// to the left of the groupbox, plus a border pad. Set all those
				// lines to that position, plus another border pad

				// Get start and end line numbers the group spans
				{
				register GUICTL *		prev;

				last = 0;
				first = 0xFF;
				prev = ctl;
				while (prev < groupCtl)
				{
					if (prev->Type != CTLTYPE_GROUPBOX)
					{
						lineNum = prev->Y;
						if (last < lineNum) last = lineNum;
						if (first > lineNum) first = lineNum;
					}
					prev++;
				}
				}
//				if (first >= last) first = last;

				// Set group's Y to point to the last line
				groupCtl->Y = last++;

				// Indicate where extra Y spacing needed
				linePos[first].Flags |= GUICTL_GOT_GROUP;
				linePos[last].Flags |= GUICTL_GOT_GROUPEND;

				// Push current margin on the stack
				*(--groupMargin) = leftMargin;

				// Get the X offset of the longest of those lines
				leftMargin = 0;
				lineNum = first;
				while (lineNum < last)
				{
					if (leftMargin < linePos[lineNum].XOffset) leftMargin = linePos[lineNum].XOffset;
					lineNum++;
				}

				// Set groupbox left X to XSPACE pixels (border pad) beyond that
				leftMargin += GuiWinGetBound(app, win, ((groupCtl->Flags.Global & CTLGLOBAL_NOPADDING) ? GUIBOUND_XBORDER : GUIBOUND_XSPACE));
				groupCtl->X = leftMargin;

				// Update those lines' X offsets to XBORDER pixels inside of the box
				leftMargin += GuiWinGetBound(app, win, GUIBOUND_XBORDER);
				lineNum = first;
				while (lineNum < last)
				{
					linePos[lineNum].XOffset = leftMargin;
					lineNum++;
				}
			}

			{
			register unsigned short	x;

			// If a GROUPBOX, set its Width
			if (type == CTLTYPE_GROUPBOX)
			{
				register GUICTL *			prev;

				set_group_width(app, win, ctl);

				// Update all the lines' X that the group spans
				x = ctl->X + ctl->Width;
				prev = ctl - ctl->NumOfCtls;
				do
				{
					linePos[ctl->Y].XOffset = x;
				} while (++prev < ctl);

				// Reset margin
				x = leftMargin = *groupMargin++;
			}
			else
			{
				lineNum = ctl->Y;
				linePos += lineNum;

				// If not the first ctl on the line, insert X padding. (XSPACE if NOPADDING
				// flag, otherwise XBETWEEN). Otherwise just border padding
				if (!(x = linePos->XOffset)) x = GuiWinGetBound(app, win, GUIBOUND_XBORDER);
				if (leftMargin != x)
					x += GuiWinGetBound(app, win, ((ctl->Flags.Global & CTLGLOBAL_NOPADDING) ? GUIBOUND_XSPACE : GUIBOUND_XBETWEEN));

				// See if width specified (in CharWidth units or absolute pixels)
				if (!ctl->Width)
				{
					// If 0, calc width based upon type/flags/labels
					GuiCtlSetWidth(app, win, ctl);
				}
				else
				{
					if (ctl->Width & GUICTL_ABS_SIZE)
						ctl->Width &= ~GUICTL_ABS_SIZE;
					else
						ctl->Width *= app->Host.GuiFont.CharWidth;
					if (type == CTLTYPE_ARROWS) ctl->Width += (app->Host.GuiFont.Height + 2) * 2;
				}

				// Offset X (in CharWidth units or absolute pixels) if specified
				if (ctl->Flags.Global & CTLGLOBAL_X_ALIGN)
					x = baseArray[ctl->X].X;		// aligned to a preceding ctl
				else
				{
					if (ctl->X)
					{
						if (ctl->X & GUICTL_ABS_XOFFSET)
							x += (ctl->X & ~GUICTL_ABS_XOFFSET);
						else
							x += (ctl->X * app->Host.GuiFont.CharWidth);
					}
					else if (type == CTLTYPE_EDIT) x += getEditLabelWidth(app, win, ctl);
				}

				// Store X, then inc by width
				ctl->X = x;
				if (type == CTLTYPE_CHECK && !(ctl->Flags.Global & CTLGLOBAL_SMALL))
					x += ((ctl->Width + app->Host.GuiFont.Height + 7) * ctl->Attrib.NumOfLabels) + ((ctl->Attrib.NumOfLabels - 1) * 8);
				else
					x += ctl->Width;

				// Update line width
				linePos->XOffset = x;

				// Update line's max height
				{
				register unsigned short height;

				if (type == CTLTYPE_AREA)
				{
					// For a static area, update the height of each line it spans
					if ((ctl->Flags.Local & CTLFLAG_AREA_STATIC) && (height = ctl->BottomY))
					{
						ctl->BottomY = height * app->Host.GuiFont.Height;
						do
						{
							linePos[--height + lineNum].XOffset = x;
							if (!linePos[height + lineNum].Height) linePos[height + lineNum].Height = app->Host.GuiFont.Height;
						} while (height);
					}
				}
				else
				{
					height = GuiCtlGetHeight(app, ctl);
					if ((ctl->Flags.Local & CTLFLAG_LABELBOX) && (type == CTLTYPE_CHECK || type == CTLTYPE_RADIO))
						height = (app->Host.GuiFont.Height * 2);
					if (height > linePos->Height) linePos->Height = height;
				}
				}

//				linePos->Flags++;
			}

			if (x > widest) widest = x;
			ctl++;
			}
		}

		// Set CTLTYPE_END's LayerWidth
		ctl->LayerWidth = widest + GuiWinGetBound(app, win, GUIBOUND_XBORDER);
		if (win->Win.MinWidth < ctl->LayerWidth) win->Win.MinWidth = ctl->LayerWidth;
		}

		// ==================================================
		// Set Y

		{
		unsigned short		lowY;
		unsigned char		moreToDo, lineNum;

		// Start with first line
		linePos = (struct GUILINE *)(&app->Msg);
		lowY = lineNum = 0;

		do
		{
			unsigned short		height;

			// Reset for start of line
			moreToDo = 0;

			if (linePos->Flags & GUICTL_GOT_GROUP) yOffset += app->Host.GuiFont.Height;
			else if (linePos->Flags & GUICTL_GOT_GROUPEND) yOffset += (app->Host.GuiFont.Height/4);
			height = linePos->Height;

			// Find the next ctl on this line
			ctl = baseArray;
			while ((type = (ctl->Type & CTLTYPE_ENUM_MASK)))
			{
				// Not yet processed?
				if (!(ctl->Flags.Global & CTLGLOBAL_PRESET))
				{
					// Is it on this line? If so, set ctl's Y/height
					if (ctl->Y == lineNum)
					{
						register unsigned short	tempHeight, y;

						// Mark as processed
						ctl->Flags.Global |= CTLGLOBAL_PRESET;

						type = (ctl->Type & CTLTYPE_ENUM_MASK);
						if (type == CTLTYPE_GROUPBOX)
						{
							// If GROUPBOX, set Y pos and height
							ctl->Y = 0;
							set_group_height(app, win, ctl);
						}
						else
						{
							y = yOffset;

							if (type == CTLTYPE_AREA) ctl->BottomY += y;
							else
							{
								tempHeight = GuiCtlGetHeight(app, ctl);
								if (height > tempHeight)
									y += ((height - (tempHeight & ~1)) / 2);
								if ((ctl->Flags.Local & CTLFLAG_LABELBOX) && (type == CTLTYPE_CHECK || type == CTLTYPE_RADIO))
									y += (app->Host.GuiFont.SmallHeightOffset / 2);
							}
							ctl->Y = y;
						}
					}
					else
						moreToDo = 1;
				}

				ctl++;
			}

			// Next line
			yOffset += height + GuiWinGetBound(app, win, GUIBOUND_YSPACE);
			if (lowY < yOffset) lowY = yOffset;
			linePos++;
			lineNum++;
		} while (moreToDo);

		// Clear CTLGLOBAL_PRESET
		while (baseArray->Type)
		{
			baseArray->Flags.Global &= ~CTLGLOBAL_PRESET;
			baseArray++;
		}

		// Store CTLTYPE_END LayerHeight
		baseArray->LayerHeight = lowY;
		if (win->Win.MinHeight < lowY) win->Win.MinHeight = lowY;
		}

		yOffset = 0;
	} while ((baseArray = baseArray->Next));
}





/********************* unselect_preset() ******************
 * Marks any selected preset btn for redraw as unselected.
 */

static unsigned char unselect_preset(GUIAPPHANDLE app, GUIWIN * win, uint32_t which)
{
	register GUICTL *	baseArray;

	baseArray = &app->Msg.PresetCtls[0];

	// If the list is already made, use it
	if (app->InternalAppFlags & GUIAPP_LIST_MADE)
	{
		if (app->Msg.Mouse.SelectedCtl == baseArray) goto find;
	}

	// Create a temp array of the presets, and unselect any
	else if (init_presets(app, win, baseArray, which))
	{
find:	while (baseArray->Flags.Global & CTLGLOBAL_PRESET)
		{
			if (baseArray->Flags.Local & CTLFLAG_SELECTED)
			{
				GuiCtlUpdate(app, win, baseArray, 0, 0);
				break;
			}

			baseArray++;
		}

		return 1;
	}

	return 0;
}





uint32_t GuiCtlGetTypeHeight(GUIAPPHANDLE app, uint32_t type)
{
	register uint32_t		height;

	height = app->Host.GuiFont.Height;

	switch (type & 0x0f)
	{
		case CTLTYPE_ARROWS:
		case CTLTYPE_PUSH:
			if (!(type & (CTLGLOBAL_SMALL << 8))) height <<= 1;
			height += 2;
			break;

	 	case CTLTYPE_AREA:
	 	case CTLTYPE_GROUPBOX:
		{
			height = 0;
			break;
		}

	 	case CTLTYPE_EDIT:
			height += 2;
		default:
			height += 4;
	}

	return height;

}





/********************* GuiCtlGetHeight() ******************
 * Returns the pixel height of the ctl.
 *
 * ctl =		Ptr to GUICTL, or one of the CTLTYPE_ defines.
 */

uint32_t GuiCtlGetHeight(GUIAPPHANDLE app, const GUICTL * ctl)
{
	register uint32_t		height;

	height = app->Host.GuiFont.Height;

	switch (ctl->Type & CTLTYPE_ENUM_MASK)
	{
		case CTLTYPE_ARROWS:
			if (!ctl->Label) goto small;
			goto big;
		case CTLTYPE_PUSH:
			if (ctl->Flags.Global & CTLGLOBAL_SMALL) goto small;
big:		height <<= 1;
small:	height += 2;
			break;

	 	case CTLTYPE_AREA:
	 	case CTLTYPE_GROUPBOX:
		{
			height = ctl->BottomY;
			if (height < ctl->Y) height = ctl->Y;
			height -= ctl->Y;
			break;
		}

	 	case CTLTYPE_EDIT:
			height += 2;
		default:
			height += 4;
	}

	return height;
}





/*********************** GuiCtlSetSelect() ********************
 * Sets the specified GUICTL as currently selected, and marks it
 * as needing to be redrawn.
 *
 * win =			GUIWIN for window containing ctls.
 * baseArray =	Ptr to base of GUICTL array.
 * set =			GUICTL to select. If GUIBTN_CANCEL, GUIBTN_OK, etc
 *					then a preset button is selected.
 */

GUICTL * GuiCtlSetSelect(GUIAPPHANDLE app, GUIWIN * winPtr, GUICTL * baseArray, GUICTL * set)
{
	register GUIWIN *	win;
	uint32_t				yOffset;

	if (!(win = winPtr) && !(win = app->Host.CurrentWin)) win = &app->MainWin;

	if (!baseArray) baseArray = win->Win.Ctls;

	yOffset = 0;

	if (set <= (GUICTL *)14)
	{
		register	unsigned char	arrayMadeFlag;
		register uint32_t			i, which;

		i = (uint32_t)((intptr_t)set);
		arrayMadeFlag = 0;
		which = (0x00000001 << i);

		// Is this btn enabled among upper btns?
		// Is it not hidden?
		if ((win->Win.UpperPresetBtns & (which | (which << GUIBTN_HIDESHIFT))) == which)
		{
			which = GUIBTN_RESERVED;
			goto preset;
		}
		if ((win->Win.LowerPresetBtns & (which | (which << GUIBTN_HIDESHIFT))) == which)
		{
			which = 0;

preset:	//	Is one of the other preset buttons currently selected? If so, we must
			// mark it for redraw (in unselected state). But if this entire window was marked
			// for redraw, then we don't need to mark the ctl
			if (!(win->InternalFlags & GUIWIN_SKIP_REDRAW) && win->CurrentCtl == (GUICTL *)app &&

				// If this button is one we're selecting, no need to mark it here
				(win->Preset.Num != i || ((uint32_t)win->Preset.Which << 31) != which))
			{
				if (((uint32_t)win->Preset.Which << 31) != which)
					unselect_preset(app, win, which ^ GUIBTN_RESERVED);
				else
  					arrayMadeFlag = unselect_preset(app, win, which);
			}

			// Make the array if not already made. And flag any currently selected preset
			// just in case it's the same one we're selecting now
			set = &app->Msg.PresetCtls[0];
			if (arrayMadeFlag || init_presets(app, win, set, which))
			{
				while (set->Flags.Global & CTLGLOBAL_PRESET)
				{
					if (i == set->PresetId)
					{
						// If this button is already selected, we're done. No redrawing needed
donepre:				if (set->Flags.Local & CTLFLAG_SELECTED) goto done;
						GuiCtlUpdate(app, win, set, 0, 0);

						// We cleared any preset selection above
						if (win->CurrentCtl == (GUICTL *)app) win->CurrentCtl = 0;

						// Select the new preset
						set->Flags.Global = CTLGLOBAL_PRESET|CTLGLOBAL_NOPADDING;
						set->Flags.Local |= CTLFLAG_SELECTED;
						win->Preset.Num = set->PresetId;
						win->Preset.Which = (set->Flags.Local & CTLFLAG_UPPER) ? 1 : 0;

						goto out;
					}

					set++;
				}
			}
		}

		goto notfound;
	}

	//====================== Not preset =====================
	{
	register GUICTL *		ctls;

	if ((ctls = baseArray))
	{
		do
		{
			while (ctls->Type)
			{
				if (set == ctls) goto found;
				ctls++;
			}

			yOffset += ctls->LayerHeight;
		} while ((ctls = ctls->Next));
	}
found:
	if (!ctls || set->Type >= CTLTYPE_INACTIVE)
	{
notfound:
		set = 0;
	}
	}

	// If we set a non-preset, then unselect any preset button
	if (win->CurrentCtl == (GUICTL *)app)
	{
		if (!(win->InternalFlags & GUIWIN_SKIP_REDRAW))
		{
			if (set && (set->Flags.Local & CTLFLAG_SELECTED) && (set->Flags.Global & CTLGLOBAL_PRESET)) goto done;

			unselect_preset(app, win, (uint32_t)win->Preset.Which << 31);
		}
		win->CurrentCtl = 0;
	}

	// If caller had the preset GUICTL already made, then it passed that
	if (set && (set->Flags.Global & CTLGLOBAL_PRESET)) goto donepre;

out:
	// If same selection, do nothing. no! clicking on CHECK toggles it. We also need to allow
	// double-click
//	if (win->CurrentCtl != set)
	{
		// Mark any previously selected ctl for redraw (in unselected state)
		if (win->CurrentCtl && win->CurrentCtl != set && !(win->CurrentCtl->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, win->CurrentCtl, 0, win->CurrentCtlY);

		// Mark the new ctl for redraw, but not if a preset. that's done above
		if ((win->CurrentCtl = set) && !(set->Flags.Global & CTLGLOBAL_PRESET))
		{
			win->CurrentCtlY = yOffset;
			if (!(set->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, set, 0, yOffset);
		}
		else if (set)
		{
			win->CurrentCtl = (GUICTL *)app;
			win->CurrentCtlY = 0;
		}
	}
done:
	return set;
}





/*********************** GuiCtlGetSelect() **********************
 * Gets the currently selected GUICTL.
 *
 * baseArray =			Ptr to base of GUICTL array.
 *
 * RETURNS: The currently selected GUICTL, or 0 if none.
 *
 * Note: GUIAPP->Host.CurrentWin =	GUIWIN for window containing ctls.
 */

GUICTL * GuiCtlGetSelect(GUIAPPHANDLE app, GUICTL * baseArray)
{
	register GUIWIN *		win;
	register GUICTL *		ctls;
	register uint32_t		i;
	register uint32_t		yOffset;

	win = app->Host.CurrentWin;

	if (!baseArray) baseArray = win->Win.Ctls;

	if (!(win->InternalFlags & (GUIWIN_HELP|GUIWIN_TEMP_HIDE)))
	{
		// Create a temporary GUICTL array of the preset buttons so we can
		// return a GUICTL. Also, GuiCtlNext() wants this array
		ctls = &app->Msg.PresetCtls[0];
		i = 0;
		if (win->Win.UpperPresetBtns) i = init_presets(app, win, ctls, GUIBTN_RESERVED);
		if (win->Win.LowerPresetBtns) i += init_presets(app, win, &ctls[i], 0);
		if (i)
		{
			// Put the presets first, and link any app-supplied ctls
			ctls[i].Next = baseArray;
			baseArray = ctls;
		}
	}

	// Indicate the full array of GUICTLs, including presets, has been made
	app->InternalAppFlags |= GUIAPP_LIST_MADE;

	// Store for caller
	app->Msg.Mouse.SelectedCtl = baseArray;

	yOffset = 0;
	while (baseArray)
	{
		while (baseArray->Type)
		{
			if (baseArray->Type < CTLTYPE_INACTIVE)
			{
				if (baseArray->Flags.Global & CTLGLOBAL_PRESET)
				{
					if (baseArray->Flags.Local & CTLFLAG_SELECTED) goto out2;
				}
				else if (win->CurrentCtl == baseArray)
				{
					// Update this in case the app changed hierarchy
out2:				win->CurrentCtlY = yOffset;

					// Don't change the edit ctl's cursor position unless it's illegal
					if (baseArray->Type == CTLTYPE_EDIT && baseArray->CursorPos >= baseArray->Attrib.MaxChars)
						baseArray->CursorPos = baseArray->Attrib.MaxChars - 1;

					goto out;
				}

			}
			++baseArray;
		}

		yOffset += baseArray->LayerHeight;
		baseArray = baseArray->Next;
	}
out:
	return baseArray;
}





static uint32_t calc_y(register const GUICTL * ctl, register const GUICTL * baseArray)
{
	register uint32_t		yOffset;

	yOffset = 0;
	while (baseArray != ctl)
	{
		baseArray++;
		if (!baseArray->Type)
		{
			if (!baseArray->Next) break;
			yOffset += baseArray->LayerHeight;
			baseArray = baseArray->Next;
		}
	}

	return yOffset;
}

/********************* GuiCtlShow() ********************
 * Shows or hides the specified ctl.
 *
 * ctl =		GUICTL to show/hide.
 *
 * Uses GUIAPP->CurrentWin and GUIWIN->Ctls
 */

void GuiCtlShow(GUIAPPHANDLE app, GUIWIN * winPtr, GUICTL * ctl, unsigned char flag)
{
	register GUIWIN *			win;
	register uint32_t			height, x, y, width;

	if (ctl && (ctl->Type & CTLTYPE_HIDE) != flag)
	{
		ctl->Type ^= CTLTYPE_HIDE;

		if (!(win = winPtr) && !(win = app->Host.CurrentWin)) win = &app->MainWin;

		if (!(win->InternalFlags & GUIWIN_SKIP_REDRAW))
		{
			if (win->CurrentCtl == ctl)
				y = win->CurrentCtlY;
			else
				y = calc_y(ctl, win->Win.Ctls);

			switch (ctl->Type & CTLTYPE_ENUM_MASK)
			{
				case CTLTYPE_RADIO:
				case CTLTYPE_CHECK:
				{
					if (!(ctl->Flags.Local & CTLFLAG_LABELBOX)) goto def;
				}

				case CTLTYPE_EDIT:
				{
					if (win->InternalFlags & GUIWIN_STRING) y += win->MsgBox.YPos;
					y += ctl->Y + GuiWinGetBound(app, win, GUIBOUND_UPPER);

					x = ctl->X;
					width = ctl->Width;
					height = app->Host.GuiFont.Height;

					if ((ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_EDIT)
					{
						register const char		*ptr;
						register uint32_t			len;

						y--;
						height += 2;
						GuiTextSetLarge(win);
						ptr = win->Win.EditLabels;
						len = ctl->Attrib.LabelNum;
						while (len--) ptr += (strlen(ptr) + 1);
						len = GuiTextWidth(win, ptr) + 2;
						x -= len;
						width += len + 4;
					}
					else
					{
						y -= (((app->Host.GuiFont.Height / 2) + (app->Host.GuiFont.SmallHeightOffset / 2)) + 4);
						height = (app->Host.GuiFont.Height * 2) + 7;
						width += app->Host.GuiFont.CharWidth + 6;
						x -= (app->Host.GuiFont.CharWidth + 1);
					}

#ifdef USE_CAIRO_DIRTY
					cairo_surface_mark_dirty_rectangle(win->CairoWindow, x, y, width, height);
#else
					XClearArea(app->XDisplay, win->BaseWindow, x, y, width, height, True);
#endif
					break;
				}

				default:
def:				GuiCtlUpdate(app, win, ctl, 0, y);
			}
		}
	}
}





/********************* GuiCtlUpdate() ********************
 * Invalidates area of specified ctl, thereby causing an
 * Expose message to be sent to window. Invalidates only
 * the area that displays the ctl's current "value".
 *
 * winPtr =		GUIWIN for window containing ctl. If 0, then
 *					GUIAPP->Host.CurrentWin.
 * baseArray =	0 if yOffset is specified. Otherwise, the base
 *					of all GUICTL layers.
 * yOffset =	The upper Y pixel position of "ctl".
 *
 * If "ctl" is currently selected, "baseArray" and "yOffset" can
 * both be 0.
 */

void GuiCtlUpdate(GUIAPPHANDLE app, GUIWIN * winPtr, const GUICTL * ctl, const GUICTL * baseArray, uint32_t yOffset)
{
	register uint32_t 			yPos;
	register unsigned short		xPos, width, height;
	register unsigned	char		type;
	GUIWIN *							win;

	if (!(win = winPtr) && !(win = app->Host.CurrentWin)) win = &app->MainWin;

	// If this entire window was marked for redraw, then we don't need to mark the ctl
	if (ctl && !(win->InternalFlags & GUIWIN_SKIP_REDRAW))
	{
		// If it happens to be the currently selected ctl, we already have yOffset
		if (win->CurrentCtl == ctl) yOffset = win->CurrentCtlY;

		// If caller passed the head of the array, it wants us to calc yOffset. Otherwise, yOffset
		// has been calculated by the caller
		else if (baseArray) yOffset = calc_y(ctl, baseArray);

		type = ctl->Type & CTLTYPE_ENUM_MASK;

		xPos = ctl->X;
		yPos = ctl->Y + yOffset;
		width = ctl->Width;
		height = app->Host.GuiFont.Height;
		if (!(ctl->Flags.Global & CTLGLOBAL_PRESET))
			yPos += GuiWinGetBound(app, win, GUIBOUND_UPPER) + ((win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0);

		if (yPos < win->Win.WinPos.Height)
		{
			switch (type)
			{
				case CTLTYPE_ARROWS:
				{
					if (ctl->Flags.Local & CTLFLAG_AREA_HELPER)
					{
						width = app->Host.GuiFont.Height + 2;
#ifdef USE_CAIRO_DIRTY
						cairo_surface_mark_dirty_rectangle(win->CairoWindow,
								(xPos + ctl[-1].Width) - width, yPos, width, height);
#else
						XClearArea(app->XDisplay, win->BaseWindow,
								(xPos + ctl[-1].Width) - width, yPos, width, height, True);
#endif
					}
					if (!ctl->Label) goto small;
					goto big;
				}
				case CTLTYPE_PUSH:
				{
					if (!(ctl->Flags.Global & CTLGLOBAL_SMALL))
big:					height <<= 1;
					xPos--;
					yPos--;
					width += 2;
small:			height += 4;
					break;
				}

				case CTLTYPE_EDIT:
				{
					xPos -= 2;
					yPos -= 3;
					width += 4;
					height += 6;
					break;
				}

			 	case CTLTYPE_AREA:
				{
//					if (ctl->Flags.Global & CTLGLOBAL_APPUPDATE) return;
				}
			 	case CTLTYPE_GROUPBOX:
				{
					height = GuiCtlGetHeight(app, ctl);
					break;
				}

				default:
				{
					if (type == CTLTYPE_CHECK && !(ctl->Flags.Global & CTLGLOBAL_SMALL))
					{
						width += ((width + height + 7 + 8) * (ctl->Attrib.NumOfLabels - 1)) + height + 7;
					}
					yPos--;
					xPos--;
					width++;
					height += 7;
				}
			}

#ifdef USE_CAIRO_DIRTY
			cairo_surface_mark_dirty_rectangle(win->CairoWindow, xPos, yPos, width, height);
#else
			XClearArea(app->XDisplay, win->BaseWindow, xPos, yPos, width, height, True);
#endif
		}
	}
}





/*********************** GuiCtlNext() ********************
 * Sets the next GUICTL as currently selected, and marks it
 * as needing to be redrawn (unless CTLGLOBAL_APPUPDATE set).
 *
 * baseArray =			Ptr to base of GUICTL array.
 *
 * Note: GUIAPP->Host.CurrentWin =	GUIWIN for window containing ctls.
 */

GUICTL * GuiCtlNext(GUIAPPHANDLE app, GUICTL * baseArray)
{
	register GUICTL *		selctl;
	register GUICTL *		cycle;
	register GUIWIN *		win;
	register unsigned	char		type;
	uint32_t					yOffset;

	win = app->Host.CurrentWin;

	selctl = GuiCtlGetSelect(app, baseArray);
	baseArray = app->Msg.Mouse.SelectedCtl;
	app->Msg.Mouse.SelectedCtl = 0;

	if (!selctl)
	{
		// If none currently selected, select first ctl
		if (!(cycle = baseArray)) goto done;
wrap:	selctl = baseArray;
		yOffset = 0;
		goto loop;
	}

	type = selctl->Type;

	yOffset = win->CurrentCtlY;

	// We're going to unselect this ctl, so mark it for redraw
	if (!(selctl->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, selctl, 0, yOffset);

	// Radio, check, and arrow ctls have multiple components to tab through
	if (type == CTLTYPE_ARROWS)
	{
		if (!(selctl->Flags.Local & (CTLFLAG_UP_SELECT|CTLFLAG_NO_UP)))
		{
			selctl->Flags.Local ^= (CTLFLAG_UP_SELECT|CTLFLAG_DOWN_SELECT);
			goto out;
		}
		selctl->Flags.Local &= ~(CTLFLAG_UP_SELECT|CTLFLAG_DOWN_SELECT);
	}

	if (type >= CTLTYPE_CHECK && type <= CTLTYPE_RADIO)
	{
		if (++selctl->Select <= selctl->Attrib.NumOfLabels) goto out;
		selctl->Select = 0;
	}

	cycle = selctl;
	goto loop2;
	do
	{
		do
		{
			// We wrap around once at most
			if (cycle == selctl) goto sel;

			// Skip hidden and unfocusable ctls
loop:		type = selctl->Type;
			if (type < CTLTYPE_INACTIVE && (type != CTLTYPE_ARROWS || (selctl->Flags.Local & (CTLFLAG_NO_DOWN|CTLFLAG_NO_UP)) != (CTLFLAG_NO_DOWN|CTLFLAG_NO_UP)))
			{
sel:			if (type >= CTLTYPE_CHECK && type <= CTLTYPE_RADIO) selctl->Select = 1;
				if (type == CTLTYPE_ARROWS) selctl->Flags.Local |= ((selctl->Flags.Local & CTLFLAG_NO_DOWN) ? CTLFLAG_UP_SELECT : CTLFLAG_DOWN_SELECT);

				// Set this ctl as currently selected
				if (selctl->Flags.Global & CTLGLOBAL_PRESET)
				{
					win->CurrentCtl = (GUICTL *)app;
					win->Preset.Num = selctl->PresetId;
					win->Preset.Which = (selctl->Flags.Local & CTLFLAG_UPPER) ? 1 : 0;
					win->CurrentCtlY = 0;
				}
				else
				{
					win->CurrentCtl = selctl;
					win->CurrentCtlY = yOffset;
				}

				// Mark it for redraw
out:			if (!(selctl->Flags.Global & CTLGLOBAL_APPUPDATE)) GuiCtlUpdate(app, win, selctl, 0, yOffset);

				goto done;
			}
loop2:	++selctl;
		} while (selctl->Type);

		yOffset += selctl->LayerHeight;
	} while ((selctl = selctl->Next));

	// Do we need to wrap around?
	if (cycle != baseArray) goto wrap;

done:
	return selctl;
}





/***************** GuiWinGetBound() *******************
 * Gets the window's upper or lower Y boundary, taking any
 * menu, and Lower/Upper preset buttons into consideration.
 * Also returns window padding amounts.
 */

unsigned short GuiWinGetBound(GUIAPPHANDLE app, GUIWIN * win, uint32_t which)
{
	register uint32_t		val;

	val = 0;
	if (which & (GUIBOUND_LOWER|GUIBOUND_UPPER))
	{
		// Help window has one row of ctls at top of window
		if (win->InternalFlags & GUIWIN_HELP)
			val = (app->Host.GuiFont.Height * 2);
		else
		{
			val = win->Win.Menu ? app->Host.GuiFont.Height + 4 : GUI_TITLEBAR_SPACE;
			if ((win->Win.UpperPresetBtns & GUIBTN_SHOWMASK)) val += GuiCtlGetTypeHeight(app, CTLTYPE_PUSH) + GUI_TITLEBAR_SPACE;
		}
		if (which & GUIBOUND_LOWER)
		{
			register uint32_t		height, upper;

			upper = val;
			height = 0;
			val = win->Win.WinPos.Height;
			if (win->Win.LowerPresetBtns & GUIBTN_SHOWMASK)
			{
				height = GuiCtlGetTypeHeight(app, CTLTYPE_PUSH) + GUI_TITLEBAR_SPACE;
				upper += height;
			}
			if (upper >= win->Win.WinPos.Height)
				val = win->Win.WinPos.Height + 6; // offscreen
			else
				val -= height;
		}
	}

	if (which & GUIBOUND_XSPACE) val += app->Host.GuiFont.QuarterWidth;
	if (which & GUIBOUND_XBETWEEN) val += (app->Host.GuiFont.Height * 2);
	if (which & (GUIBOUND_XBORDER|GUIBOUND_YBORDER)) val += 5;
	if ((which & GUIBOUND_YHEADSTR) && (win->InternalFlags & GUIWIN_STRING)) val += win->MsgBox.YPos;
	if (which & GUIBOUND_YSPACE) val += (app->Host.GuiFont.Height / 4);
	return val;
}





/********************** init_presets() *********************
 * Initializes the passed GUICTL array with either the
 * GUIWIN's upper or lower preset buttons.
 *
 * flags = OR'd with 0x8000000 (GUIBTN_RESERVED) for
 * upper btns, cleared for lower btns.
 * RETURN: # of GUICTLs in array, or 0 if none.
 *
 */

static uint32_t init_presets(GUIAPPHANDLE app, GUIWIN * win, GUICTL * baseArray, uint32_t flags)
{
	register GUICTL *			temp;
	register uint32_t			widest;
	{
	register uint32_t			labelmask, width;
	register const char *	labels;
	unsigned char				id, color;

	GuiTextSetLarge(win);
	labels = ((win->InternalFlags & GUIWIN_MODAL)) ? &PresetStrs[0] : app->Host.PresetBtnStrs;
	widest = id = 0;
	labelmask = (flags & GUIBTN_RESERVED) ? win->Win.UpperPresetBtns : win->Win.LowerPresetBtns;
	flags |= (labelmask & GUIBTN_CENTER);
	labelmask &= ~GUIBTN_CENTER;
	temp = baseArray;
	do
	{
		if (1 & labelmask)
		{
			temp->Type = ((1 << GUIBTN_HIDESHIFT) & labelmask) ? CTLTYPE_PUSH|CTLTYPE_HIDE : CTLTYPE_PUSH;
			temp->PresetId = id;
			temp->Attrib.NumOfLabels = 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
			temp->Ptr = color = 0;
#pragma GCC diagnostic pop
			if (*labels < ' ')
			{
				color = *labels;
				if (color == 1)
				{
					const char *	str;

					str = &PresetStrs[0];
					if ((color = id))
					{
						do
						{
							str += strlen(str) + 1;
						} while (--color);
					}
					if (*str < ' ') color = *str++;
					temp->Label = str;
					goto sub;
				}
				labels++;
			}
			temp->Label = labels;
sub:		temp->Attrib.Color = (color > 1 ? color : GUICOLOR_GRAY) | (GUICOLOR_BLACK << 4);
			width = GuiTextWidth(win, temp->Label);
			if (widest < width) widest = width;

			temp->Flags.Global = CTLGLOBAL_PRESET|CTLGLOBAL_NOPADDING;
			temp->Flags.Local = (win->CurrentCtl == (GUICTL *)app && win->Preset.Num == id && win->Preset.Which == (flags >> 31)) ? CTLFLAG_SELECTED : 0;
			if (flags & GUIBTN_RESERVED) temp->Flags.Local |= CTLFLAG_UPPER;

			++temp;
		}

		++id;
		labelmask &= ~(1 << GUIBTN_HIDESHIFT);
		if (*labels != 1) labels += strlen(labels);
		labels++;
		labelmask >>= 1;
	} while (*labels && (labelmask & GUIBTN_SHOWMASK));
	}

	memset(temp, 0, sizeof(GUICTL));

	{
	register uint32_t		count;

	count = (temp - baseArray);
	if (count)
	{
		widest += (app->Host.GuiFont.CharWidth * 2);

		{
		register uint32_t		y, height;

		// Set the Y position

		// Assume at window top
		y = (win->Win.Menu ? app->Host.GuiFont.Height + 4 : 6) + ((win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0);
 		if (!(flags & GUIBTN_RESERVED))
		{
			// Calc Y positions with buttons at bottom of window
			height = GuiCtlGetTypeHeight(app, CTLTYPE_PUSH) + GUI_TITLEBAR_SPACE;
			if (win->Win.WinPos.Height >= height)
			{
				height = (win->InternalFlags & GUIWIN_ERROR_DLG) ? win->MsgBox.YPos : win->Win.WinPos.Height - height;
				if (!win->Win.UpperPresetBtns || height >= y + 4) y = height;
			}
		}

		temp = baseArray;
		do
		{
			temp->Y = y;
			temp++;
		} while (temp->Type);
		}

		{
		register uint32_t		x;

		x = 5;
		if (flags & GUIBTN_CENTER)
		{
			flags = (count * widest) + ((count - 1) * 4);
			if (win->Win.WinPos.Width <= flags)
				x = 0;
			else
				x = (win->Win.WinPos.Width - flags) / 2;
		}

		while (baseArray->Type)
		{
			baseArray->Width = widest;
			baseArray->X = x;
			baseArray++;
			x += widest + 4;
		}
		}
	}

	return count;
	}
}





/*********************** GuiCtlSelectXY() ********************
 * Searches through an array of GUICTLs for the one that
 * matches the specified mouse XY position of a XWindows
 * ButtonPressed event. Sets that GUICTL as the currently
 * selected ctl (unless GUIWIN_NO_UPD_CURR specified), but
 * does _NOT_ mark it as needing to be redrawn (app must
 * do GuiCtlUpdate).
 *
 * app =			GUIAPP that processed the ButtonPressed.
 *					GUIAPP->Host.CurrentWin is the Window containing ctls.
 * baseArray =	Ptr to base of GUICTL array.
 *
 * RETURNS: The GUICTL matching the given XY position, or 0
 * if none.
 */

GUICTL * GuiCtlSelectXY(GUIAPPHANDLE app, GUICTL * baseArray)
{
	register GUIWIN *			win;
	GUICTL *						orig;

	win = app->Host.CurrentWin;
	orig = baseArray;

	// If the temp array of GUICTLs has been made, use it
	if (app->InternalAppFlags & GUIAPP_LIST_MADE)
		baseArray = app->Msg.Mouse.SelectedCtl;
	else
	{
		// Does window include the preset btns? NOTE: Help window handles its own
		if (!(win->InternalFlags & (GUIWIN_HELP|GUIWIN_TEMP_HIDE)) && (win->Win.UpperPresetBtns | win->Win.LowerPresetBtns))
		{
			register GUICTL *		temp;
			register uint32_t		count;

			// Make the temp array of preset GUICTLs, and chain app's ctls to the end
			temp = &app->Msg.PresetCtls[0];
			count = init_presets(app, win, temp, GUIBTN_RESERVED);
			count += init_presets(app, win, &temp[count], 0);
			if (count)
			{
				temp[count].Next = baseArray;
				orig = baseArray = temp;
			}
		}

		// Indicate the full array of GUICTLs, including presets, has been made
		app->InternalAppFlags |= GUIAPP_LIST_MADE;

		// Store for caller
		app->Msg.Mouse.SelectedCtl = baseArray;
	}
	{
	register unsigned short		xPos, yPos, yOffset;
	register unsigned char		type;

	GuiTextSetLarge(win);

	xPos = app->Msg.Mouse.X;
	yPos = app->Msg.Mouse.Y;

	yOffset = ((win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0);

	// Search for the ctl that has been clicked within
	while (baseArray && yOffset < win->Win.WinPos.Height)
	{
		while ((type = baseArray->Type))
		{
			// Ignore hidden and unfocusable ctls
			if (type < CTLTYPE_INACTIVE)
			{
				register unsigned short	ctlY;

				ctlY = baseArray->Y + yOffset;
				if (!(baseArray->Flags.Global & CTLGLOBAL_PRESET))
					ctlY += GuiWinGetBound(app, win, GUIBOUND_UPPER);
				else if (win->InternalFlags & GUIWIN_STRING)
					ctlY -= win->MsgBox.YPos;

				// Click within this ctl?
				if (xPos >= baseArray->X && yPos >= ctlY)
				{
					if (type == CTLTYPE_AREA)
					{
						ctlY += (baseArray->BottomY - baseArray->Y);
						if (yPos < ctlY) goto chk;
						if (baseArray[1].Type == CTLTYPE_ARROWS && (baseArray[1].Flags.Local & CTLFLAG_AREA_HELPER))
						{
							baseArray++;
							if (yPos < ctlY + GuiCtlGetTypeHeight(app, CTLTYPE_ARROWS | (CTLGLOBAL_SMALL << 8)))
							{
								if (xPos < baseArray->X + app->Host.GuiFont.Height ||
									xPos >= (baseArray->X + baseArray[-1].Width) - app->Host.GuiFont.Height)
								{
									goto arrow;
								}
							}
						}
					}

					else if (yPos < ctlY + GuiCtlGetHeight(app, baseArray))
					{
						if (type == CTLTYPE_PUSH) goto chk;

						//======================== ARROW
						if (type == CTLTYPE_ARROWS)
						{
							if (xPos < baseArray->X + baseArray->Width)
							{
								// Set UP/DOWN_SELECT depending upon whether usr clicked up or down arrow
arrow:						if (win->Win.Flags & GUIWIN_NO_UPD_CURR) goto no_sel;
								baseArray->Flags.Local &= ~(CTLFLAG_UP_SELECT|CTLFLAG_DOWN_SELECT);
								baseArray->Flags.Local |= (xPos >= baseArray->X + (baseArray->Width/2)) ? CTLFLAG_UP_SELECT : CTLFLAG_DOWN_SELECT;
								goto ret_it;
							}
						}

						// ====================== COMPACT CHECK
						else if (type == CTLTYPE_CHECK && !(baseArray->Flags.Global & CTLGLOBAL_SMALL))
						{
							if (xPos < baseArray->X + ((baseArray->Width + app->Host.GuiFont.Height + 7) * baseArray->Attrib.NumOfLabels) + ((baseArray->Attrib.NumOfLabels - 1) * 8))
							{
								register const char *	label;
								register unsigned char	len, mask;

								if (win->Win.Flags & GUIWIN_NO_UPD_CURR) goto no_sel;

								if (baseArray->Flags.Global & CTLGLOBAL_GET_LABEL)
									label = baseArray->BtnLabel(app, baseArray, (char *)&app->Msg.PresetCtls[16]);
								else
									label = baseArray->Label;
								mask = 1;
								xPos -= baseArray->X;
								do
								{
									len = strlen(label);
									if (xPos < baseArray->Width + app->Host.GuiFont.Height + 8)
									{
										if (xPos >= GuiTextWidth(win, label) + app->Host.GuiFont.Height + 7) break;

										// Set Select to checkbox selected, where 1 is first
										baseArray->Select = mask;
										goto ret_it;
									}

									xPos -= (baseArray->Width + app->Host.GuiFont.Height + 7 + 8);
									label += len + 1;
								} while (++mask <= baseArray->Attrib.NumOfLabels);

								break;
							}
						}
						else
						{
chk:						if (xPos < baseArray->X + baseArray->Width)
							{
								// ==================== PUSH, EDIT, PRESET
								if (type != CTLTYPE_RADIO && type != CTLTYPE_CHECK)
								{
ret_it:							if (!((win->InternalFlags & (GUIWIN_HELP|GUIWIN_NUMPAD)) | (win->Win.Flags & GUIWIN_NO_UPD_CURR)))
ret_it2:								baseArray = GuiCtlSetSelect(app, win, orig, baseArray);
									else if ((win->InternalFlags & GUIWIN_NUMPAD))
									{
										if (orig != (GUICTL *)((char *)&app->Filename[PATH_MAX] - (sizeof(GUICTL) * 13))) goto ret_it2;
										win->CurrentCtl = baseArray;
										win->CurrentCtlY = yOffset;
									}

no_sel:							return baseArray;
								}

								// ==================== RADIO, CHECK
								{
								register const char *	label;
								register unsigned char	len, mask;

								if (baseArray->Flags.Global & CTLGLOBAL_GET_LABEL)
									label = baseArray->BtnLabel(app, baseArray, (char *)&app->Msg.PresetCtls[16]);
								else
									label = baseArray->Label;
								mask = 0;
								yPos = baseArray->X;
								goto start;
								do
								{
									if ((len = strlen(label)))
										yPos += GuiTextWidth(win, label) + (app->Host.GuiFont.CharWidth * 2);

									if (xPos < yPos)
									{
										if (!mask) break;

										if (win->Win.Flags & GUIWIN_NO_UPD_CURR) goto no_sel;

										// Set Select to radio button selected, where 1 is first
										baseArray->Select = mask;
										goto ret_it;
									}

									label += len + 1;
start:							yPos += app->Host.GuiFont.Height + 8;
								} while (++mask <= baseArray->Attrib.NumOfLabels);
								}
							}
						}
					}
				}
			}

			// Next ctl
			++baseArray;
		}

		yOffset += baseArray->LayerHeight;
		baseArray = baseArray->Next;
	}
	}

//	if (!(win->Win.Flags & GUIWIN_NO_UPD_CURR)) win->CurrentCtl = 0;

	return 0;
}





#if 0
void GuiCtlDrawStart(GUIAPPHANDLE app)
{
}

void GuiCtlDrawEnd(GUIAPPHANDLE app)
{
}
#endif

static void guiDrawText(GUIAPPHANDLE, register cairo_t *, register uint32_t, register uint32_t, register const char *);

#define ARROWDOWN 0
#define ARROWUP 1

static void drawArrow(GUIWIN * win, register GUIBOX * box, uint32_t flag)
{
	register	short			mid;
	register cairo_t *	currentCairo;

	currentCairo = win->CairoGraphics;
	mid = box->X + ((box->Width - box->X) >> 1);

	if (flag)
	{
		cairo_move_to(currentCairo, box->X, box->Height);
		cairo_line_to(currentCairo, box->Width, box->Height);
		cairo_line_to(currentCairo, mid, box->Y);
		cairo_line_to(currentCairo, box->X, box->Height);
	}
	else
	{
		cairo_move_to(currentCairo, box->X, box->Y);
		cairo_line_to(currentCairo, box->Width, box->Y);
		cairo_line_to(currentCairo, mid, box->Height);
		cairo_line_to(currentCairo, box->X, box->Y);
	}

	cairo_fill(currentCairo);
}

static void draw_groupbox(GUIAPPHANDLE app, GUIWIN * win, GUICTL * ctl, const char * label, register GUIAREA * box)
{
	register cairo_t *	currentCairo;
	register uint32_t		size;
	cairo_text_extents_t	textExtents;

	currentCairo = win->CairoGraphics;

	GuiTextSetSmall(win);
	cairo_text_extents(currentCairo, label, &textExtents);
	size = textExtents.width;

	// Draw the box with a blank area where the label will go
	GuiTextSetColor(win, GUICOLOR_DARKGRAY);
	cairo_move_to(currentCairo, box->Left + 8, box->Top + 6);
	cairo_line_to(currentCairo, box->Left, box->Top + 6);
	cairo_line_to(currentCairo, box->Left, box->Bottom);
	cairo_line_to(currentCairo, box->Right, box->Bottom);
	cairo_line_to(currentCairo, box->Right, box->Top + 6);
	cairo_line_to(currentCairo, (box->Left + size + app->Host.GuiFont.CharWidth + 8), box->Top + 6);
	cairo_stroke(currentCairo);

	// Draw the label
	GuiTextSetColor(win, GUICOLOR_DARKGREEN);
	guiDrawText(app, currentCairo, box->Left + 16, box->Top + (app->Host.GuiFont.Height/2) - app->Host.GuiFont.HeightOffset, label);

	GuiTextSetLarge(win);
}

static void guiDrawText(GUIAPPHANDLE app, register cairo_t * currentCairo, register uint32_t x, register uint32_t y, register const char * str)
{
	cairo_move_to(currentCairo, x, y + app->Host.GuiFont.HeightOffset);
	cairo_show_text(currentCairo, str);
}

/*********************** GuiCtlDraw() *********************
 * Draws a window's ctls.
 *
 * app =			GUIAPP that processed the Expose event.
 *					GUIAPP->Host.CurrentWin is the Window containing ctls.
 * baseArray =	Ptr to array of GUICTLs, or 0 if use GUIWIN->Ctls.
 */

uint32_t GuiCtlDraw(GUIAPPHANDLE app, GUICTL * baseArray)
{
	register GUICTL *		ctl;
	register GUIWIN *		win;
	uint32_t					winBottom, winTop;
	GUIAREA					rc;
	uint32_t					yOffset;

	win = app->Host.CurrentWin;

	// We need some bounds limits, to adjust each panel/ctl relative XY to absolute window XY
	winTop = GuiWinGetBound(app, win, GUIBOUND_UPPER);
	winBottom = GuiWinGetBound(app, win, GUIBOUND_LOWER); // - GUI_TITLEBAR_SPACE;

	// Fetch any heading text's lower Y coord, as the starting Y
	yOffset = (win->InternalFlags & GUIWIN_STRING) ? win->MsgBox.YPos : 0;

	// Make sure we have some ctls, and room to display something
	if ((!baseArray && !(baseArray = win->Win.Ctls)) || winTop + yOffset >= winBottom) goto out;

	GuiTextSetLarge(win);

	for (;;)
	{
		// =========================================
		// Do all resizeable areas be recalculated? Typically done as a result of window resize
		if (win->InternalFlags & GUIWIN_AREA_RECALC)
		{
			register	int16_t	compensation, areaBottom;

			GuiTextSetColor(win, GUICOLOR_BLACK);
			areaBottom = compensation = 0;
			for (;;)
			{
				// ==========================
				// Find the next AREA ctl lower than the previously drawn AREA
				{
				register GUICTL *		nextGui;

				nextGui = 0;
				rc.Top = -1;
				ctl = baseArray;
				for (;;)
				{
					// Not already processed?
nohlp:			if (!(ctl->Type & CTLTYPE_DONE))
					{
						// End of layer?
						if (!ctl->Type)
						{
							ctl->LayerHeight += compensation;

							// If no more resizeable AREAs in this layer, draw the layer's ctls now
							if (!(ctl = nextGui)) goto draw_layer;

							// Mark this AREA processed, and then redraw it
							ctl->Type |= CTLTYPE_DONE;
							break;
						}

						if (ctl->Y < areaBottom)
							ctl->Type |= CTLTYPE_DONE;
						else
						{
							// Adjust Y from any prev AREA
							ctl->Y += compensation;

							if ((ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_GROUPBOX)
								ctl->BottomY += compensation;

							if ((ctl->Type & CTLTYPE_ENUM_MASK) == CTLTYPE_AREA)
							{
								ctl->BottomY += compensation;

								// A resizeable area? Next to be drawn?
								if (!(ctl->Flags.Local & CTLFLAG_AREA_STATIC) && ctl->Y < rc.Top)
								{
									nextGui = ctl;
									rc.Top = ctl->Y;
								}

								// Adjust any helper ARROWs. Since there is a CTLTYPE_END, it's always safe to look ahead one
								ctl++;
								if ((ctl->Type & CTLTYPE_ENUM_MASK) != CTLTYPE_ARROWS || !(ctl->Flags.Local & CTLFLAG_AREA_HELPER)) goto nohlp;
								ctl->Y = ctl[-1].BottomY + 1;
								ctl->X = ctl[-1].X;
							}
						}
					}

					ctl++;
				}	// for (;;)

				// ===============================
				ctl = nextGui;
				}

				// Ask AREA to provide its desired height based upon window's current size. Pass it the
				// max avail space where:
				// Left = Area's left X
				// Right = Window right edge - 5
				// Top = Below any menubar, upper preset buttons, and all ctls in any preceding layers
				// Bottom = Window bottom minus space for any lower preset buttons
				//
				// If it's hidden, collapse it
				if (ctl->Type & CTLTYPE_HIDE)
				{
					rc.Bottom = 0;
					goto move;
				}

				rc.Left = ctl->X;
				rc.Right = win->Win.WinPos.Width;
				if (rc.Left + 5 >= rc.Right)
					rc.Bottom = ctl->Width = 0;	// Not enough room to draw anything
				else
				{
					rc.Right -= 5;
					ctl->Width = (rc.Right - rc.Left);
					rc.Top = ctl->Y + winTop + yOffset;
					rc.Bottom = winBottom;

					// Set flag to tell app we need its height reported back. It reports
					// space used by setting GUIAREA->Bottom to the lowest Y of consumed area.
					// If app redraws the AREA now, it should leave CTLFLAG_AREA_REQ_SIZE set.
					// Otherwise, to draw later below, clear CTLFLAG_AREA_REQ_SIZE.
					//
					// NOTE: App must not alter GUIAREA->Top nor GUICTL's Y or BottomY
					ctl->Flags.Local |= CTLFLAG_AREA_REQ_SIZE;
					if (ctl->Flags.Local & CTLFLAG_AREA_LIST)
					{
//						if (ctl->Flags.Local & CTLFLAG_AREA_FULL_SIZE)
						{
							register GUILIST *	list;
							register uint32_t		itemsPerRow, colwidth;

							list = ctl->ListDraw(0, ctl, 0);
							if (list->ColumnWidth)
							{
								if (ctl->Width < list->ColumnWidth)
									itemsPerRow = 0;
								else
								{
									colwidth = list->ColumnWidth + (app->Host.GuiFont.CharWidth * 3);
									itemsPerRow = 1 + ((ctl->Width - list->ColumnWidth) / colwidth);
									colwidth = ((list->NumOfItems % itemsPerRow) ? app->Host.GuiFont.Height : 0);
									itemsPerRow = ((list->NumOfItems / itemsPerRow) * app->Host.GuiFont.Height) + colwidth;
								}
								if (itemsPerRow > rc.Bottom - rc.Top) itemsPerRow = rc.Bottom - rc.Top;
//								if (ctl->Flags.Local & CTLFLAG_AREA_EVENHEIGHT)
									itemsPerRow = (itemsPerRow / app->Host.GuiFont.Height) * app->Host.GuiFont.Height;
								rc.Bottom = rc.Top + itemsPerRow;
							}
						}
						listAreaDraw(app, ctl, &rc);
					}
					else
						(ctl->AreaDraw)(app, ctl, &rc);
					if (ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE)
					{
						GuiTextSetLarge(win);
						GuiTextSetColor(win, GUICOLOR_BLACK);
					}
					if (rc.Bottom > winBottom) rc.Bottom = winBottom;
					if (rc.Top > rc.Bottom) rc.Top = rc.Bottom;

					rc.Bottom -= rc.Top;
					// Round down to multiple of font height
					if (ctl->Flags.Local & CTLFLAG_AREA_LIST)
//					if (ctl->Flags.Local & (CTLFLAG_AREA_EVENHEIGHT|CTLFLAG_AREA_LIST))
						rc.Bottom = (rc.Bottom / app->Host.GuiFont.Height) * app->Host.GuiFont.Height;
				}

move:			areaBottom = ctl->BottomY;
				ctl->BottomY = ctl->Y + rc.Bottom;
				if (!(ctl->Flags.Local & CTLFLAG_AREA_X_EXPAND))
				{
					// Get the difference between the old and new size
					compensation = (int16_t)rc.Bottom - (int16_t)(areaBottom - ctl->Y);
				}

				// Reposition/redraw up/down helper arrows
				update_helper_arrows(app, ctl, 0);
			}
		}

		// ===================== Draw the controls ====================
		{
		register cairo_t *	currentCairo;
		const char *			label;
		cairo_text_extents_t	textExtents;

draw_layer:
		ctl = baseArray;
		currentCairo = win->CairoGraphics;
		for (;;)
		{
			ctl->Type &= ~CTLTYPE_DONE;

			// Convert Top Y relative to absolute. NOTE: Preset btns use absolute originally
			rc.Left = ctl->X;
			if (!ctl->Width || rc.Left >= win->Win.WinPos.Width) goto next;
			rc.Top = ctl->Y + yOffset;
			if (!(ctl->Flags.Global & CTLGLOBAL_PRESET))
			{
				rc.Top += winTop;
				if (rc.Top >= winBottom) goto next;
			}
			else if (win->InternalFlags & GUIWIN_STRING)
				rc.Top -= win->MsgBox.YPos;

			// Calc Right X based upon width
			rc.Right = rc.Left + ctl->Width;

			if (ctl->Flags.Global & CTLGLOBAL_GET_LABEL)
				label = ctl->BtnLabel(app, ctl, (char *)&app->Msg.PresetCtls[16]);
			else
				label = ctl->Label;

			switch (ctl->Type & (CTLTYPE_HIDE|CTLTYPE_GROUPHIDE|CTLTYPE_ENUM_MASK))
			{
				case CTLTYPE_AREA:
				{
					rc.Bottom = ctl->BottomY + (rc.Top - ctl->Y);
					if (rc.Bottom > winBottom) rc.Bottom = winBottom;
					if (rc.Bottom > rc.Top)
					{
						if (rc.Right >= win->Win.WinPos.Width) rc.Right = win->Win.WinPos.Width;

						// If app left CTLFLAG_AREA_REQ_SIZE set, then it has already drawn the area
						if (!(ctl->Flags.Local & CTLFLAG_AREA_REQ_SIZE))
						{
							GuiTextSetColor(win, GUICOLOR_BLACK);
							if (ctl->Flags.Local & CTLFLAG_AREA_LIST)
								listAreaDraw(app, ctl, &rc);
							else
								(ctl->AreaDraw)(app, ctl, &rc);

							update_helper_arrows(app, ctl, 0);

							GuiTextSetLarge(win);
						}

						ctl->Flags.Local &= ~CTLFLAG_AREA_REQ_SIZE;
					}

					break;
				}

				// GroupBox
				case CTLTYPE_GROUPBOX:
				{
					rc.Bottom = rc.Top + GuiCtlGetHeight(app, ctl);
					draw_groupbox(app, win, ctl, label, &rc);
					break;
				}

				// Static
				case CTLTYPE_STATIC:
				{
					if (!(rc.XPos = ctl->Attrib.Color)) rc.XPos = GUICOLOR_DARKGRAY;
					rc.XPos &= 0x0f;

					if (ctl->Flags.Local & CTLFLAG_LEFTTEXT)
					{
						GuiTextSetColor(win, rc.XPos);
						guiDrawText(app, currentCairo, rc.Left, rc.Top + 2, label);
						break;
					}

					if (ctl->Flags.Local & CTLFLAG_MULTILINE)
					{
						rc.Bottom = ctl->NumOfLines;
						GuiTextDrawMsg(app, label, (GUIBOX *)&rc, (rc.XPos << 24) | ctl->Flags.Local);
						break;
					}

					goto draw;
				}

				// Button
				case CTLTYPE_PUSH:
				{
					register char				chr;

					rc.Bottom = GuiCtlGetHeight(app, ctl);

					// Low nibble of value is the frame background color. High nibble is text
					if (!(rc.YPos = ctl->Attrib.Color)) rc.YPos = (GUICOLOR_BLACK << 4) | GUICOLOR_GRAY;
					if (rc.YPos == 1) rc.YPos = (GUICOLOR_BLACK << 4) | GUICOLOR_LIGHTGOLD;
					rc.XPos = rc.YPos >> 4;
					rc.YPos &= 0x0F;

					// Color the background
					GuiTextSetColor(win, rc.YPos);
					cairo_rectangle(currentCairo, rc.Left, rc.Top, ctl->Width, rc.Bottom);
					cairo_fill(currentCairo);

					// Draw the button frame
					GuiTextSetColor(win, GUICOLOR_BLACK);
					cairo_rectangle(currentCairo, rc.Left, rc.Top, ctl->Width, rc.Bottom);
					cairo_stroke(currentCairo);

					// Highlight it if the current ctl
					if (!(ctl->Flags.Global & CTLGLOBAL_APPUPDATE) && (((ctl->Flags.Local & CTLFLAG_SELECTED) && (ctl->Flags.Global & CTLGLOBAL_PRESET)) || ctl == win->CurrentCtl))
						rc.XPos = (rc.YPos != GUICOLOR_RED ? GUICOLOR_RED : GUICOLOR_GRAY);

					// Center text horiz
					if (!(ctl->Flags.Global & CTLGLOBAL_SMALL)) rc.Top += (app->Host.GuiFont.Height/2) - 4;

					// Draw the label, possibly spanning 2 lines if a '\n'
draw:				GuiTextSetColor(win, rc.XPos);

					// We may need to temporarily alter a const label, so copy it to temp buffer. Caller
					// may be using the buffer now to hold preset buttons, so copy after that array
					{
					char *	to;

					to = (char *)&app->Msg.PresetCtls[16];
					while ((chr = *label++) && chr != '\n')
					{
						if (to + 1 < (char *)&app->Filename[PATH_MAX]) *to++ = chr;
					}
					*to = 0;
					}

					{
					register unsigned short	size;

					// Get width so we can center it
					cairo_text_extents(currentCairo, (char *)&app->Msg.PresetCtls[16], &textExtents);
					size = textExtents.width;
					if (size > ctl->Width) size = ctl->Width;

	//				if (ctl->Flags.Global & CTLGLOBAL_SMALL) chr = 0;
					guiDrawText(app, currentCairo, rc.Left + ((ctl->Width - size) / 2),
									rc.Top + (chr ? ((app->Host.GuiFont.Height/2) - app->Host.GuiFont.HeightOffset) : 2),
									(char *)&app->Msg.PresetCtls[16]);
					if (chr)
					{
						cairo_text_extents(currentCairo, label, &textExtents);
						size = textExtents.width;
						if (size > ctl->Width) size = ctl->Width;
						guiDrawText(app, currentCairo, rc.Left + ((ctl->Width - size) / 2), rc.Top + 2 + (app->Host.GuiFont.Height/2), label);
					}
					}

					break;
				}

				// Checkmark
				case CTLTYPE_CHECK:
				{
					register unsigned char	mask;

					rc.XPos = GUICOLOR_BLACK;
					rc.YPos = GUICOLOR_GRAY;
					rc.Bottom = GuiCtlGetHeight(app, ctl);
					if ((rc.Right = rc.Bottom / 6) < 4) rc.Right = 4;
					if (ctl->Flags.Global & CTLGLOBAL_SMALL) goto compact;
					mask = 0;
					do
					{
						rc.ItemIndex = ctl->Attrib.Value & (0x01 << mask++);
						GuiCtlDrawCheck(win, &rc);

						// Draw the label
						rc.Left += app->Host.GuiFont.Height + 7;
						GuiTextSetColor(win, (ctl == win->CurrentCtl && ctl->Select == mask && !(ctl->Flags.Global & CTLGLOBAL_APPUPDATE)) ? GUICOLOR_RED : GUICOLOR_BLUE);
						guiDrawText(app, currentCairo, rc.Left, rc.Top + 2, label);

						rc.Left += ctl->Width + 8;
						while (*label++);
					} while (mask < ctl->Attrib.NumOfLabels);

					// Draw groupbox
grp:				if (ctl->Flags.Local & CTLFLAG_LABELBOX)
					{
						rc.Top -= ((app->Host.GuiFont.Height / 2) + (app->Host.GuiFont.SmallHeightOffset / 2));
						rc.Bottom = rc.Top + (app->Host.GuiFont.Height * 2) + 2;
						rc.Right = rc.Left + 4;
						rc.Left = ctl->X - app->Host.GuiFont.CharWidth;
						draw_groupbox(app, win, ctl, label, &rc);
					}
					break;
				}

				// Radio
				case CTLTYPE_RADIO:
				{
					register unsigned char	mask;

					rc.Bottom = GuiCtlGetHeight(app, ctl);
compact:			mask = 0;

					do
					{
						if (*label)
						{
							if (ctl->Type == CTLTYPE_RADIO)
							{
								register unsigned short	size;

								size = rc.Bottom >> 1;
								GuiTextSetColor(win, GUICOLOR_BLACK);
								if (!(ctl->Flags.Global & CTLGLOBAL_SMALL))
								{
									cairo_stroke(currentCairo);
									cairo_arc(currentCairo, rc.Left + size, rc.Top + size, size, 0, 2 * M_PI);
									cairo_stroke(currentCairo);
								}
								if (ctl->Attrib.Value == ++mask)
								{
									cairo_arc(currentCairo, rc.Left + size, rc.Top + size, size >> 1, 0, 2 * M_PI);
									cairo_fill(currentCairo);
									cairo_stroke(currentCairo);
								}
							}
							else
							{
								rc.ItemIndex = ctl->Attrib.Value & (0x01 << mask++);
								GuiCtlDrawCheck(win, &rc);
							}

							rc.Left += app->Host.GuiFont.Height + 8;

							// Draw the label
							GuiTextSetColor(win, (ctl == win->CurrentCtl && ctl->Select == mask && !(ctl->Flags.Global & CTLGLOBAL_APPUPDATE)) ? GUICOLOR_RED : GUICOLOR_BLUE);
							cairo_text_extents(currentCairo, label, &textExtents);
							guiDrawText(app, currentCairo, rc.Left, rc.Top + 2, label);
							rc.Left += textExtents.width + (app->Host.GuiFont.CharWidth * 2);
						}
						while (*label++);
					} while (mask < ctl->Attrib.NumOfLabels);

					goto grp;
				}

				// Arrow
				case CTLTYPE_ARROWS:
				{
					register uint32_t			len;

					rc.Bottom = rc.Top + GuiCtlGetHeight(app, ctl);

					// Draw the Up/Down arrows
					len = GUICOLOR_BLACK;
					if (ctl->Flags.Local & CTLFLAG_NO_DOWN) len = GUICOLOR_GRAY;
					else if (ctl == win->CurrentCtl && (ctl->Flags.Local & CTLFLAG_DOWN_SELECT) && !(ctl->Flags.Global & CTLGLOBAL_APPUPDATE)) len = GUICOLOR_RED;
					GuiTextSetColor(win, len);

					rc.Right = rc.Left + app->Host.GuiFont.Height;
					drawArrow(win, (GUIBOX *)&rc, ARROWDOWN);

					len = GUICOLOR_BLACK;
					if (ctl->Flags.Local & CTLFLAG_NO_UP) len = GUICOLOR_GRAY;
					else if (ctl == win->CurrentCtl && (ctl->Flags.Local & CTLFLAG_UP_SELECT) && !(ctl->Flags.Global & CTLGLOBAL_APPUPDATE)) len = GUICOLOR_RED;
					GuiTextSetColor(win, len);

					if (ctl->Flags.Local & CTLFLAG_AREA_HELPER)
					{
						rc.Right = rc.Left + ctl[-1].Width;
						rc.Left = rc.Right - app->Host.GuiFont.Height;
					}
					else
					{
						rc.Right = rc.Left + ctl->Width;
						rc.Left += (ctl->Width - app->Host.GuiFont.Height);
					}
					drawArrow(win, (GUIBOX *)&rc, ARROWUP);

					if (label)
					{
						// Draw the label
						rc.Right = ctl->Width - ((app->Host.GuiFont.Height + 1) * 2);
						if ((len = strlen(label)))
						{
							GuiTextSetColor(win, GUICOLOR_DARKGRAY);
							GuiTextSetSmall(win);
							cairo_text_extents(currentCairo, label, &textExtents);
							guiDrawText(app, currentCairo, ctl->X + app->Host.GuiFont.Height + 1 + ((rc.Right - textExtents.width) / 2), rc.Top, label);
							GuiTextSetLarge(win);
						}
						label += len + 1;

						// Draw the value
						rc.Top += app->Host.GuiFont.Height + 1;
						GuiTextSetColor(win, GUICOLOR_BLUE);
						len = ctl->Attrib.Value;
						if (ctl->Flags.Local & CTLFLAG_NOSTRINGS)
						{
							label = (char *)&app->Msg.PresetCtls[16];
							sprintf((char *)label, "%u", len + ctl->MinValue);
						}
						else
						{
							while (len--)
							{
								while (*label++);
							}
						}
						cairo_text_extents(currentCairo, label, &textExtents);
						guiDrawText(app, currentCairo, ctl->X + app->Host.GuiFont.Height + 1 + ((rc.Right - textExtents.width) / 2), rc.Top, label);
					}

					break;
				}

				// Edit
				case CTLTYPE_EDIT:
				{
	//				rc.Bottom = GuiCtlGetHeight(app, ctl) + rc.Top;

					// Draw the button frame
					GuiTextSetColor(win, (ctl == win->CurrentCtl && !(ctl->Flags.Global & CTLGLOBAL_APPUPDATE) ? GUICOLOR_RED : GUICOLOR_BLACK));
					cairo_rectangle(currentCairo, rc.Left, rc.Top, ctl->Width, app->Host.GuiFont.Height + 4);
	//				cairo_stroke(currentCairo);
					cairo_rectangle(currentCairo, rc.Left + 1, rc.Top + 1, ctl->Width - 2, app->Host.GuiFont.Height + 2);
					cairo_stroke(currentCairo);

					// Draw the label
					{
					register unsigned char	len;

					rc.Top += 2;
					label = win->Win.EditLabels;
					len = ctl->Attrib.LabelNum;
					while (len--) label += (strlen(label) + 1);
					cairo_text_extents(currentCairo, label, &textExtents);
					guiDrawText(app, currentCairo, rc.Left - textExtents.width - app->Host.GuiFont.QuarterWidth, rc.Top, label);
					}

					label = ctl->Buffer;
					if (win->CurrentCtl == ctl)
					{
						register char *	ptr2;
						register char		temp, temp2;

						// Draw the caret. Note: If our substring ends with trailing spaces, Cairo strips
						// them off before calc'ing the width. It shouldn't. To get around this bug, we
						// must make sure the string ends with non-space
						ptr2 = (char *)&label[ctl->CursorPos];
						GuiTextSetColor(win, GUICOLOR_DARKGREEN);
						temp = *ptr2;
						temp2 = 1;
						if (ptr2 > label && *(ptr2 - 1) == ' ')
						{
							*ptr2++ = 'X';
							temp2 = *ptr2;
						}
						*ptr2 = 0;
						cairo_text_extents(currentCairo, label, &textExtents);
						if (temp2 != 1)
						{
							*ptr2-- = temp2;
							textExtents.width -= app->Host.GuiFont.CharWidth;
						}
						*ptr2 = temp;
						cairo_rectangle(currentCairo, rc.Left + 3 + textExtents.width, rc.Top - 1, app->Host.GuiFont.QuarterWidth, app->Host.GuiFont.Height + 2);
					}

					GuiTextSetColor(win, GUICOLOR_BLACK);
					guiDrawText(app, currentCairo, rc.Left + 3, rc.Top, label);

					// weird mess if omitted
					cairo_fill(currentCairo);

	//				break;
				}
			}

next:		// Next GUICTL
			++ctl;

			if (!ctl->Type)
			{
				// Clear CTLTYPE_DONE on this layer
				ctl = baseArray;
				while (ctl->Type)
				{
					ctl->Type &= ~CTLTYPE_DONE;
					ctl++;
				}

				yOffset += ctl->LayerHeight;
				if (!(baseArray = ctl->Next) || yOffset >= win->Win.WinPos.Height) goto out;
				break;
			}
		}
		}
	}
out:
	return yOffset;
}

void GuiCtlDrawCheck(GUIWIN * win, GUIAREA * box)
{
	register cairo_t *		currentCairo;
	register unsigned short	size, width, top, left;

	left = box->Left;
	top = box->Top;
	width = box->Right;	// Width
	size = box->Bottom;	// Height

	GuiTextSetColor(win, box->XPos);

	// Draw box shadow
	currentCairo = win->CairoGraphics;
	cairo_rectangle(currentCairo, left + 1, top + 1, size, size);
	cairo_stroke(currentCairo);

	// Draw an X if selected
	if (box->ItemIndex)
	{
		size -= width;
		cairo_move_to(currentCairo, left + width,	top + width-1);
		cairo_line_to(currentCairo, left + size,	top + size);
		cairo_move_to(currentCairo, left + width,	top + width);
		cairo_line_to(currentCairo, left + size,	top + size+1);
		cairo_move_to(currentCairo, left + width,	top + size);
		cairo_line_to(currentCairo, left + size,	top + width-1);
		cairo_move_to(currentCairo, left + width,	top + size+1);
		cairo_line_to(currentCairo, left + size,	top + width);
		cairo_stroke(currentCairo);
	}

	size = box->Bottom;
	GuiTextSetColor(win, box->YPos);
	cairo_rectangle(currentCairo, left, top, size, size);
	cairo_stroke(currentCairo);
}



static const unsigned short	ScreenSizes[] = {1080,1024,864,768,600,480,0};

static void get_screen_size(register GUIAPPHANDLE app)
{
	register unsigned short		width, height;
	{
	register Window				wid;
	XWindowAttributes				xwAttr;

	wid = DefaultRootWindow(app->XDisplay);
	XGetWindowAttributes(app->XDisplay, wid, &xwAttr);

	app->Init.Width = width = xwAttr.width;
	app->Init.Height = height = xwAttr.height;
	}

	{
	register const unsigned short *		sizes;

	sizes = &ScreenSizes[0];
	width = 28;
//	width = (height/40) & ~1;
	while (*sizes++ > height && width > 10) width -= 2;
	}
	app->Init.FontSize = FontSizes[0] = (unsigned char)width;
}




/********************* GuiInit() ********************
 * Called at start to calculate font/ctls/window size
 * based upon desktop resolution, and obtain a GUIAPP.
 */

void * GuiInit(uint32_t extra)
{
	register GUIAPPHANDLE	app;
	register void *			mem;

	if ((mem = malloc(sizeof(struct GUIAPP2) + extra)))
	{
		memset(mem, 0, sizeof(struct GUIAPP2) + extra);

		XInitThreads();

		app = (GUIAPPHANDLE)((char *)mem + extra);
		if ((app->XDisplay = XOpenDisplay(0)))
		{
			register GUIWIN *				win;

			if ((app->EventQueue = eventfd(0, EFD_NONBLOCK)) >= 0)
			{
				if ((app->EventHandle = epoll_create(2)) >= 0)
				{
					struct epoll_event	eventMsg;

					eventMsg.events = EPOLLIN;
					eventMsg.data.fd = app->EventQueue;

					if (epoll_ctl(app->EventHandle, EPOLL_CTL_ADD, eventMsg.data.fd, &eventMsg) >= 0)
					{
						eventMsg.events = EPOLLHUP | EPOLLERR | EPOLLIN;
						eventMsg.data.fd = XConnectionNumber(app->XDisplay);
						if (epoll_ctl(app->EventHandle, EPOLL_CTL_ADD, eventMsg.data.fd, &eventMsg) >= 0)
						{
							get_screen_size(app);
							app->Host.CurrentWin = win = &app->MainWin;
							win->Win.WinPos.Height = app->Init.Height;
							win->Win.WinPos.Width = app->Init.Width;

							//
							WmDeleteWindowAtom = XInternAtom(app->XDisplay, &WmDeleteAtomName[0], 0);
							WmProtocolsAtom = XInternAtom(app->XDisplay, &WmProtocolsName[0], 0);
							NoTitleAtom = XInternAtom(app->XDisplay, &WmNoTitleBarName[0], 0);

							app->Host.ClickSpeed = 688/16;
							app->Host.PresetBtnStrs = PresetStrs;
							goto good;
						}
					}

					close(app->EventHandle);
				}

				close(app->EventQueue);
			}

			XCloseDisplay(app->XDisplay);
		}

		free(mem);
		mem = 0;
	}
good:
	return mem;
}





/*********************** GuiDone() ***********************
 * Called by app once when done using the lib.
 */

void GuiDone(void * mem, uint32_t size)
{
	register GUIAPPHANDLE	app;

	if (mem)
	{
		app = (GUIAPPHANDLE)((char *)mem + size);

		// Close all open windows
		GuiWinCloseAll(app);

		// Free any help anchor windows
		while (app->MainWin.Next) GuiWinState(app, app->MainWin.Next, GUISTATE_FREE);

		if (app->GLib) dlclose(app->GLib);
		if (app->GtkLib) dlclose(app->GtkLib);

		close(app->EventHandle);
		close(app->EventQueue);

		// Free XWindows stuff
		XCloseDisplay(app->XDisplay);

		free(mem);
	}
}





/********************* initFont() *********************
 * Initializes font variables.
 */

static void initFont(register GUIAPPHANDLE app, register GUIWIN * win)
{
	if (win->BaseWindow)
	{
		cairo_font_extents_t fontextent;

		// Get font character width/height for the 2 fonts
		GuiTextSetSmall(win);
		cairo_font_extents(win->CairoGraphics, &fontextent);
		app->Host.GuiFont.SmallHeightOffset = (unsigned char)(fontextent.height - fontextent.descent);

		GuiTextSetLarge(win);
		cairo_font_extents(win->CairoGraphics, &fontextent);
		app->Host.GuiFont.Height = fontextent.height;
		app->Host.GuiFont.HeightOffset = app->Host.GuiFont.Height - (unsigned short)fontextent.descent;
	 	app->Host.GuiFont.CharWidth = GuiTextWidth(win, &TestStr[2]);
	 	app->Host.GuiFont.Spacing = GuiTextWidth(win, &TestStr[0]) - (app->Host.GuiFont.CharWidth * 2);
		app->Host.GuiFont.QuarterWidth = app->Host.GuiFont.CharWidth / 4;
	}
}





/******************* openWindow() ********************
 * Creates a desktop window at GUIWIN->WinPos.
 *
 * NOTE: Window is not shown until you call GuiWinState(GUISTATE_SHOW).
 */

static Window openWindow(register GUIAPPHANDLE app, register GUIWIN * win, register Window newWindow)
{
	register void *	display;

	// If app has set WinPos, use its dimensions/position
	if (!win->Win.WinPos.Width)
	{
		memcpy(&win->Win.WinPos, &app->MainWin.Win.WinPos, sizeof(GUIBOX));
		win->Win.WinPos.X += app->Host.GuiFont.Height;
		win->Win.WinPos.Y += app->Host.GuiFont.Height;
	}

	display = app->XDisplay;

	// Create the app window
	if ((newWindow = XCreateSimpleWindow(display, newWindow ? newWindow : DefaultRootWindow(display), win->Win.WinPos.X, win->Win.WinPos.Y, win->Win.WinPos.Width, win->Win.WinPos.Height, 0, 0, 0)))
	{
		// Get cairo structs
		if ((win->CairoWindow = cairo_xlib_surface_create(display, newWindow, DefaultVisual(display, 0), win->Win.WinPos.Width, win->Win.WinPos.Height)))
		{
			if ((win->CairoGraphics = cairo_create(win->CairoWindow)))
			{
				// We want to know about the user clicking on the close window button
				XSetWMProtocols(display, newWindow, &WmDeleteWindowAtom, 1);

				XSelectInput(display, newWindow, ExposureMask|StructureNotifyMask|ButtonPressMask|KeyPressMask|ButtonReleaseMask|KeyReleaseMask);

				goto out;
			}

			cairo_surface_destroy(win->CairoWindow);
		}

		XDestroyWindow(display, newWindow);
		newWindow = 0;
	}
out:
	win->BaseWindow = newWindow;

	return newWindow;
}





/******************* GuiWinCloseAll() ********************
 * Closes all open windows.
 */

void GuiWinCloseAll(GUIAPPHANDLE app)
{
	register GUIWIN *	nextwin;
	register GUIWIN *	win;
	register GUIWIN *	parent;

	win = &app->MainWin;

	// Close the last window in the list first (ie, reverse order
	// from how they were opened)
	while (win->Next) win = win->Next;

	while (win != &app->MainWin)
	{
		// Remove this GUIWIN from the list, except for help anchor and main wins
		parent = &app->MainWin;
		while ((nextwin = parent->Next) && nextwin != win) parent = nextwin;
		if (nextwin && !(win->Win.Flags & GUIWIN_HELP_ANCHOR)) parent->Next = win->Next;

		GuiWinState(app, win, (win->Win.Flags & GUIWIN_HELP_ANCHOR) ? GUISTATE_CLOSE : GUISTATE_CLOSE|GUISTATE_FREE);

		// Another window (besides main)?
		win = parent;
	}

	GuiWinState(app, &app->MainWin, GUISTATE_CLOSE);
}

static void close_win(register void * display, register GUIWIN * win)
{
	register Window	xwin;

	xwin = win->BaseWindow;
	win->BaseWindow = 0;
	cairo_destroy(win->CairoGraphics);
	cairo_surface_destroy(win->CairoWindow);
	XDestroyWindow(display, xwin);
	win->InternalFlags &= ~GUIWIN_SKIP_REDRAW;
}


/********************* GuiWinState() **********************
 * Various operations upon a window.
 */

static void get_min_size(register GUIAPPHANDLE);

GUIWIN * GuiWinState(GUIAPPHANDLE app, GUIWIN * winPtr, uint32_t operation)
{
	register GUIWIN *	win;

	if (!(win = winPtr))
	{
		if (operation & GUISTATE_CREATE)
		{
			if ((win = malloc(sizeof(struct GUIWIN2)))) memset(win, 0, sizeof(struct GUIWIN2));
		}
		else
		{
			win = &app->MainWin;
			if (operation & GUISTATE_ENUMERATE)
			goto out;
		}
	}

	if (win)
	{
		if (operation & GUISTATE_ENUMERATE)
		{
			win = win->Next;
			goto out;
		}

		if ((operation & GUISTATE_OPEN) && !win->BaseWindow && !openWindow(app, win, app->Init.ParentInt))
		{
			if (win != &app->MainWin && (operation & GUISTATE_CREATE)) goto free;
			goto err;
		}

		if ((operation & GUISTATE_FONTSIZE) || !app->Host.GuiFont.CharWidth)
		{
			register unsigned char	fontsize;

			fontsize = ((operation & GUISTATE_FONTSIZE) && app->Init.FontSize) ? app->Init.FontSize : FontSizes[0];
			if (fontsize < 10) fontsize = 10;
			FontSizes[0] = fontsize;
			FontSizes[1] = fontsize - (fontsize/6);
			if (FontSizes[1] == fontsize) FontSizes[1] = fontsize - 2;
			initFont(app, win);
		}

		if (operation & GUISTATE_MENU)
		{
			register uint32_t	total;

			total = menuCalcLabelWidths(app, win);
        	if (operation & GUISTATE_SIZE)
			{
				if (app->Init.Width < total) app->Init.Width = total;
			}

			if (win->Win.MinWidth < total) win->Win.MinWidth = total;
		}

		if (win == &app->MainWin) operation &= ~(GUISTATE_CREATE|GUISTATE_LINK|GUISTATE_UNLINK|GUISTATE_FREE);

		if (operation & GUISTATE_LINK)
		{
			// Make sure this GUIWIN is in the list
			register GUIWIN *	nextWin;
			register GUIWIN *	parent;

			parent = &app->MainWin;
			while ((nextWin = parent->Next))
			{
				if (win == nextWin) goto linked;
				parent = nextWin;
			}

			win->Next = 0;
			parent->Next = win;
linked:
			win->InternalFlags |= GUIWIN_IS_LINKED;
		}

		if (operation & (GUISTATE_UNLINK|GUISTATE_FREE))
		{
			register GUIWIN *	nextWin;
			register GUIWIN *	parent;

			// Remove this GUIWIN from the list, except for main win
			parent = &app->MainWin;
			while ((nextWin = parent->Next) && win != nextWin) parent = nextWin;
			if (nextWin) parent->Next = nextWin->Next;
			win->InternalFlags &= ~(GUIWIN_IS_LINKED|GUIWIN_HELP_LINK);
		}

		{
		register Window	xwin;
		register void *	display;

		display = app->XDisplay;
		if ((xwin = win->BaseWindow))
		{
			if (operation & GUISTATE_TITLE)
			{
				// If title=0, window created without titlebar
				if (!app->Init.Title)
				{
					MWMHints					mwmhints;

					memset(&mwmhints, 0, sizeof(mwmhints));
					mwmhints.flags = MWM_HINTS_DECORATIONS;
					// mwmhints.decorations = 0;
					XChangeProperty(display, xwin, NoTitleAtom, NoTitleAtom, 32, PropModeReplace, (unsigned char *)&mwmhints, PROP_MWM_HINTS_ELEMENTS);

					win->InternalFlags |= GUIWIN_NOTITLEBAR;
				}
				else
				{
					if (win->InternalFlags & GUIWIN_NOTITLEBAR)
					{
						close_win(display, win);
						xwin = openWindow(app, win, app->Init.ParentInt);
						operation |= (GUISTATE_SHOW|GUISTATE_OPEN);
					}

					XStoreName(display, xwin, app->Init.Title);
					win->InternalFlags &= ~GUIWIN_NOTITLEBAR;
				}
			}

			if (operation & GUISTATE_MOVE)
			{
				Window	win;

				win = xwin;
				if (app->InternalAppFlags & GUIAPP_PLUGIN) XQueryTree(display, xwin, 0, &win, 0, 0);
				XMoveWindow(display, xwin, app->Init.X, app->Init.Y);
			}

			if (operation & GUISTATE_SIZE)
			{
				win->Win.WinPos.Width = app->Init.Width;
				win->Win.WinPos.Height = app->Init.Height;
				XResizeWindow(display, xwin, app->Init.Width, app->Init.Height);
				win->InternalFlags |= GUIWIN_AREA_RECALC;
			}

			if (operation & GUISTATE_HIDE)
				XUnmapWindow(display, xwin);

			if (operation & GUISTATE_MINSIZE)
			{
				// Set window properties
				register XSizeHints *		size_Hints;
				register XWMHints *			wm_Hints;

set_size:	win->InternalFlags |= GUIWIN_MINSIZE_SET;

				size_Hints = (XSizeHints *)((char *)&app->Filename[PATH_MAX] - sizeof(XSizeHints));
				wm_Hints = (XWMHints *)((char *)size_Hints - sizeof(XWMHints));

				size_Hints->flags = PMinSize;
				size_Hints->min_width = app->Init.MinWidth;
				size_Hints->min_height = app->Init.MinHeight;
				wm_Hints->initial_state = NormalState;
				wm_Hints->input = True;
				wm_Hints->flags = StateHint|InputHint;
				XSetWMProperties(display, xwin, 0, 0, 0, 0, size_Hints, wm_Hints, 0);
			}

			if (operation & GUISTATE_SHOW)
			{
				if (!(win->InternalFlags & GUIWIN_MINSIZE_SET))
				{
					get_min_size(app);
					goto set_size;
				}
				XMapWindow(display, xwin);
			}
		}

		if (operation & GUISTATE_AREA_RECALC) win->InternalFlags |= GUIWIN_AREA_RECALC;

		if (operation & GUISTATE_GET_BASE) return (GUIWIN *)xwin;
		if (operation & GUISTATE_GET_DRAWHANDLE) return (GUIWIN *)win->CairoGraphics;

		if (operation & (GUISTATE_CLOSE|GUISTATE_FREE))
		{
			free_help(app, win);

			if (xwin) close_win(display, win);
		}
		}

		if (operation & GUISTATE_FREE)
		{
free:		free(win);
			if (win == app->Host.CurrentWin) app->Host.CurrentWin = &app->MainWin;
err:		win = 0;
		}
	}
out:
	return win;
}





/*********************** GuiWinSignal() *******************
 * Called by secondary threads to signal main (GUI) thread.
 */

void * GuiWinSignal(GUIAPPHANDLE app, void * sigHandle, long cmd)
{
	// If cmd == 0, caller wants to obtain or release a signal handle
	if (cmd)
	{
		// A sig handle = 0 wakes up GuiWinMsg using a poll event
		// instead of sending an XWindows message. Faster. Not
		// supported by LV2 plugins
		if (!sigHandle)
		{
			uint64_t		data;

			data = cmd;
			write(app->EventQueue, &data, sizeof(data));
		}
		else
		{
			XClientMessageEvent	xevent;

			// Signal main thread by sending it a
			// ClientMessage with 1000 as the message_type, and
			// the first data long == cmd
			xevent.type = ClientMessage;
			xevent.message_type = 1000;
			xevent.window = app->MainWin.BaseWindow;
			xevent.format = 32;
			xevent.data.l[0] = cmd;
			XSendEvent(sigHandle, xevent.window, 0, 0, (XEvent *)&xevent);
			XFlush(sigHandle);
		}
	}
	else if (app->InternalAppFlags & GUIAPP_PLUGIN)
	{
		if (sigHandle)
			XCloseDisplay(sigHandle);
		else
			sigHandle = XOpenDisplay(0);
	}
	else
		sigHandle = app->XDisplay;
	return sigHandle;
}





static void get_min_size(register GUIAPPHANDLE app)
{
	app->Init.MinWidth = app->Host.GuiFont.CharWidth * 8;
	app->Init.MinHeight = app->Host.GuiFont.Height * 4;
}


void * GuiAppState(GUIAPPHANDLE app, uint32_t operation, void * arg)
{
	register void *	ret;

	ret = 0;
	if (operation & GUISTATE_GET_INIT)
	{
		ret = &app->Init;
		memset(ret, 0, sizeof(GUIINIT));
		get_screen_size(app);
		get_min_size(app);
	}
	if (operation & GUISTATE_GET_TEMPBUF)
	{
		ret = &app->Filename[0];
		if (arg) *((uint32_t *)arg) = sizeof(app->Filename) - 1;
	}
	if (operation & GUISTATE_POLL_ON) app->InternalAppFlags |= GUIAPP_POLL;
	if (operation & GUISTATE_POLL_OFF) app->InternalAppFlags &= ~GUIAPP_POLL;
	if (operation & GUISTATE_PLUGIN) app->InternalAppFlags |= GUIAPP_PLUGIN;
	if (operation & GUISTATE_GET_TIME)
	{
		struct timespec 		tv;

		clock_gettime(CLOCK_MONOTONIC, &tv);
		if (!arg) arg = &app->Milliseconds;
		ret = arg;
		*((uint32_t *)ret) = (uint32_t)((1000 * tv.tv_sec) + (tv.tv_nsec/1000000));
	}

//	if (operation & GUISTATE_TIMEOUT_ON) app->InternalAppFlags |= GUIAPP_TIMEOUT;
//	if (operation & GUISTATE_TIMEOUT_OFF) app->InternalAppFlags &= ~GUIAPP_TIMEOUT;
	return ret;
}





/************************* GuiFileDlg() **********************
 * Get the user's choice of filename to load and copies it to
 * fn[].
 */

static const char	AllExtension[] = "All files (*.*)\0*\0";

char * GuiFileDlg(GUIAPPHANDLE app, const char * title, const char * okButton, const char * extensions, uint32_t flags)
{
again:
	if (app->Gtk.file_chooser_dialog_new)
	{
		{
		register GUIWIN *	win;

		win = &app->MainWin;
		do
		{
			if (win->BaseWindow)
			{
				win->InternalFlags |= (GUIWIN_TEMP_HIDE|GUIWIN_MODAL);
				XUnmapWindow(app->XDisplay, win->BaseWindow);
			}
		} while ((win = win->Next));
		}

		{
		register unsigned char	old;
		XFlush(app->XDisplay);
		old = app->InternalAppFlags;
		app->InternalAppFlags |= (GUIAPP_POLL);
		while (GuiWinGetMsg(app));
		app->InternalAppFlags = old;
		}

		app->Filename[0] = 0;
		{

		register GtkWidget	dlg;

		if ((dlg = app->Gtk.file_chooser_dialog_new(title, 0 /* Host.MainWindow */,
			(flags & GUIFILE_DIR) ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
			"_Cancel", GTK_RESPONSE_CANCEL, okButton, GTK_RESPONSE_OK, NULL)))
		{
			if (!(flags & GUIFILE_DIR) && extensions)
			{
				register void *				filter;

				do
				{
					filter = app->Gtk.file_filter_new();
					if (*extensions == '\n') extensions = AllExtension;
					app->Gtk.file_filter_set_name(filter, extensions);
					extensions += strlen(extensions) + 1;
					app->Gtk.file_filter_add_pattern(filter, extensions);
					extensions += strlen(extensions) + 1;
					app->Gtk.file_chooser_add_filter(dlg, filter);
				} while (*extensions);
			}

			{
			register char *	filename;

			filename = 0;
			if (app->Gtk.dialog_run(dlg) == GTK_RESPONSE_OK)
				filename = app->Gtk.file_chooser_get_filename(dlg);
			app->Gtk.widget_destroy(dlg);
			while (app->Gtk.events_pending()) app->Gtk.main_iteration();

			if (filename)
			{
				strcpy(&app->Filename[0], filename);
				app->g_free(filename);
			}
			}
		}
		}

		{
		register GUIWIN *	win;

		win = &app->MainWin;
		do
		{
			if (win->InternalFlags & GUIWIN_TEMP_HIDE)
			{
				win->InternalFlags &= ~(GUIWIN_TEMP_HIDE|GUIWIN_MODAL|GUIWIN_SKIP_REDRAW);
				app->FakeMsg.GWin = 0;
				XMapWindow(app->XDisplay, win->BaseWindow);
//				GuiWinUpdate(app, win);
			}
		} while ((win = win->Next));
		}
//printf("remap\r\n");
	}
	else
	{
		{
	   register void *	handle;

		if (!(handle = dlopen("libgtk-x11-2.0.so", RTLD_LAZY | RTLD_GLOBAL)))
		{
			if (!(handle = dlopen("libgtk-3.so", RTLD_LAZY | RTLD_GLOBAL)))
			{
				GuiErrShow(app, "This program needs Gtk 2 or 3!", GUIBTN_OK_SHOW|GUIBTN_OK_DEFAULT);
				return 0;
			}
		}
		app->GtkLib = handle;
		app->GLib = dlopen("libglib-2.0.so", RTLD_LAZY | RTLD_GLOBAL);
		}
		{
		register void **	ptrs;
		register const char * name;

		name = &GtkNames[0];
		ptrs = (void **)&app->Gtk;
		do
		{
			*ptrs++ = (void *)dlsym(app->GtkLib, name);
			name += strlen(name) + 1;
		} while (*name);
		}
		app->g_free = dlsym(app->GLib, "g_free");

		app->Gtk.init(0, 0);

		goto again;
	}

	return &app->Filename[0];
}









//===========================================
// Scrollable List area
//===========================================

/*********************** GuiListDrawItem() ********************
 * Displays the specified Item name in List ctl.
 *
 * RETURN: 0 if more items can be displayed.
 */

int32_t GuiListDrawItem(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area, const char * name)
{
	register GUILIST *	list;

	// Get the app's GUILIST ptr
	list = ctl->ListDraw(0, ctl, 0);

	if (area->ItemIndex >= list->TopItemNum)
	{
		if (area->YPos >= area->Bottom)
		{
			area->XPos += list->ColumnWidth + (app->Host.GuiFont.CharWidth * 3);
			if (!list->ColumnWidth || area->XPos + list->ColumnWidth > area->Right)
done:			return -1;
			area->YPos = area->Top;
		}
		if (area->ItemIndex >= list->NumOfItems) goto done;

		// Not a blank line?
		if (*name)
		{
			if (area->ItemIndex == list->CurrItemNum)
			{
				list->SelY = area->YPos;
				list->SelX = area->XPos;
				GuiTextSetColor(app->Host.CurrentWin, (ctl == app->Host.CurrentWin->CurrentCtl) ? GUICOLOR_RED : GUICOLOR_PURPLE);
			}

			GuiTextDraw(app, area->XPos, area->YPos, name);

			if (area->ItemIndex == list->CurrItemNum) GuiTextSetColor(app->Host.CurrentWin, GUICOLOR_BLACK);
		}

		// Count items actually displayed
		list->NumOfItemsInView++;
		area->YPos += app->Host.GuiFont.Height;
	}

	area->ItemIndex++;
	return 0;
}

/********************* listAreaDraw() *********************
 * Called by GuiCtlDraw() to draw a List AREA GUICTL.
 */

static void listAreaDraw(GUIAPPHANDLE app, GUICTL * ctl, GUIAREA * area)
{
	register GUILIST *	list;

	// Get the app's GUILIST ptr
	list = ctl->ListDraw(0, ctl, 0);

	area->XPos = area->Left;
	area->YPos = area->Top;
	area->ItemIndex = list->NumOfItemsInView = 0;

	// Let caller draw the list using GuiListDrawItem()
	ctl->ListDraw(app, ctl, area);
}

static void redraw_sel(register GUIAPPHANDLE app, register GUILIST * list, register GUICTL * ctl)
{
	if (list->CurrItemNum != -1)
	{
		GUIBOX	box;

		box.X = list->SelX;
		box.Y = list->SelY;
		if (!(box.Width = list->ColumnWidth)) box.Width = app->Host.CurrentWin->Win.WinPos.Width - box.X;
		box.Height = app->Host.GuiFont.Height;
//		if (ctl->Flags.Global & CTLGLOBAL_APPUPDATE)
//			memcpy(&app->Msg.Mouse.Area, &box, sizeof(GUIBOX));
//		else
			GuiWinAreaUpdate(app, app->Host.CurrentWin, &box);
	}
}

static GUICTL * update_helper_arrows(GUIAPPHANDLE app, register GUICTL * ctl, register GUILIST * list)
{
	register unsigned char	orig;

	// Helper ARROWS after the AREA?
	if ((ctl[1].Type & CTLTYPE_ENUM_MASK) == CTLTYPE_ARROWS && (ctl[1].Flags.Local & CTLFLAG_AREA_HELPER))
	{
		// If GuiCtlDraw() is the caller, mark this ARROWs drawn
		if (!list)
		{
			list = ctl->ListDraw(0, ctl, 0);
			ctl[1].Type |= CTLTYPE_DONE;
		}

		// Reposition ARROWS beneath the resizeable AREA now that it's stable size
		ctl++;
		ctl->Y = ctl[-1].BottomY + 1;
		ctl->X = ctl[-1].X;

		// If we're at the start/end of the list, enable/disable up/down arrows
		orig = ctl->Flags.Local;
		ctl->Flags.Local = orig & ~(CTLFLAG_NO_UP|CTLFLAG_NO_DOWN);
		if (!list->TopItemNum) ctl->Flags.Local |= CTLFLAG_NO_UP;
		if (list->NumOfItemsInView + list->TopItemNum >= list->NumOfItems) ctl->Flags.Local |= CTLFLAG_NO_DOWN;

		// If state changed, mark it for redraw
		if (orig != ctl->Flags.Local) GuiCtlUpdate(app, 0, ctl, (app->Host.CurrentWin)->Win.Ctls, 0);
	}

	return ctl;
}


static GUICTL * findListCtl(register GUIWIN * win, register GUICTL * ctl)
{
	register unsigned short		y;

	// Find the list AREA GUICTL if caller didn't pass it
	if (!ctl || ctl->Type != CTLTYPE_AREA || !(ctl->Flags.Local & CTLFLAG_AREA_LIST))
	{
		y = 0;
		if ((ctl = win->Win.Ctls))
		{
			do
			{
				do
				{
					if (ctl->Type == CTLTYPE_AREA && (ctl->Flags.Local & CTLFLAG_AREA_LIST))
					{
						win->CurrentCtl = ctl;
						win->CurrentCtlY = y;
						goto out;
					}
					ctl++;
				} while (ctl->Type);
				y += ctl->LayerHeight;
			} while ((ctl = ctl->Next));
		}
	}

out:
	return ctl;
}

/****************** GuiListKey() *******************
 * Handles user keyboard input for List AREA ctl.
 *
 * RETURN: One of the GUILIST_ values.
 */

char GuiListKey(GUIAPPHANDLE app, GUICTL * ctl)
{
	register struct GUIWIN2 *	win;
	register GUILIST *		list;
	register	char				result;

	if (!(win = app->Host.CurrentWin)) win = &app->MainWin;

	// Find the list AREA GUICTL if caller didn't pass it
	if (!(ctl = findListCtl(win, ctl))) goto out;

	// Get the GUILIST from caller
	list = ctl->ListDraw(0, ctl, 0);

	switch (app->Msg.Key.Code)
	{
		case XK_Page_Down:
		{
			if (list->NumOfItemsInView + list->TopItemNum < list->NumOfItems)
			{
				list->TopItemNum += list->NumOfItemsInView;
				if (list->TopItemNum > list->NumOfItems - list->NumOfItemsInView) goto all2;
				goto all;
			}
			break;
		}

		case XK_Page_Up:
		{
			if (list->TopItemNum)
			{
				if (list->TopItemNum < list->NumOfItemsInView) list->TopItemNum = 0;
				else list->TopItemNum -= list->NumOfItemsInView;
				goto all;
			}
			break;
		}

		// ----------------- UP arrow key decs the selection
		case XK_Up:
		{
			if (list->CurrItemNum == -1 || !list->CurrItemNum)
			{
				list->CurrItemNum = list->NumOfItems - 1;
all2:			list->TopItemNum = list->NumOfItems - list->NumOfItemsInView;
				goto all;
			}

			// If selection is off-screen, then select the last displayed item
			if (list->CurrItemNum < list->TopItemNum || list->CurrItemNum >= list->TopItemNum + list->NumOfItemsInView)
			{
				list->CurrItemNum = list->TopItemNum + list->NumOfItemsInView - 1;
				goto all;
			}

			if (--list->CurrItemNum < list->TopItemNum)
			{
				--list->TopItemNum;
				goto all;
			}

			goto one;
		}

		// ----------------- DOWN arrow key incs the selection
		case XK_Down:
		{
			if (list->CurrItemNum == -1 || list->CurrItemNum + 1 >= list->NumOfItems)
				list->CurrItemNum = list->TopItemNum = 0;
			else if (list->CurrItemNum < list->TopItemNum || list->CurrItemNum >= list->TopItemNum + list->NumOfItemsInView)
				list->CurrItemNum = list->TopItemNum;
			else if (++list->CurrItemNum >= list->TopItemNum - list->NumOfItemsInView)
				list->TopItemNum++;
			else
			{
one:			redraw_sel(app, list, ctl);
				goto arr;
			}

all:		GuiCtlUpdate(app, 0, ctl, win->Win.Ctls, 0);
arr:
			// Refresh up/down arrows
			update_helper_arrows(app, ctl, list);
			result = GUILIST_SCROLL;
			break;
		}

		// ----------------- User wants to cancel
		case XK_Escape:
		{
			result = GUILIST_ABORT;
			break;
		}

		// ----------------- ENTER/Space selects highlighted Item
//		case XK_space:
		case XK_Return:
		{
			if (list->CurrItemNum != -1)
			{
				result = GUILIST_SELECTION;
				break;
			}
		}

		default:
out:		return GUILIST_IGNORE;
	}

	// Select the list ctl if this keystroke consumed by it
	app->Msg.Mouse.SelectedCtl = ctl;

	return result;
}

/********************* GuiListMouse() **********************
 * Called by GUI thread to process user mouse input in
 * AREA List ctl, or arrow ctl associated with the list.
 *
 * RETURN: One of the GUILIST_ values.
 */

char GuiListMouse(GUIAPPHANDLE app, GUICTL * ctl)
{
	if (ctl->Type == CTLTYPE_AREA /* && (ctl->Flags.Local & CTLFLAG_AREA_LIST) */)
	{
		register GUILIST *	list;

		list = ctl->ListDraw(0, ctl, 0);

		if (list->NumOfItemsInView)
		{
			register uint32_t		item;

			if (!(item = list->ColumnWidth)) item = ctl->Width;
			item += (app->Host.GuiFont.CharWidth * 3);
			item = (((ctl->BottomY - ctl->Y) / app->Host.GuiFont.Height) * ((app->Msg.Mouse.X - ctl->X) / item)) +
				((app->Msg.Mouse.Y - ctl->Y) / app->Host.GuiFont.Height);
			if (item < list->NumOfItemsInView)
			{
				list->CurrItemNum = item + list->TopItemNum;
				if (app->Host.DClickFlag) return GUILIST_SELECTION;
				redraw_sel(app, list, ctl);
				return GUILIST_CLICK;
			}
		}
	}

	// If a click upon a companion arrow ctl, simulate up/down arrow key
	if (ctl->Type == CTLTYPE_ARROWS && (ctl->Flags.Local & CTLFLAG_AREA_HELPER))
	{
		app->Msg.Key.Code = ((ctl->Flags.Local & CTLFLAG_UP_SELECT) ? XK_Page_Up : XK_Page_Down);
		return GuiListKey(app, --ctl);
	}

	return GUILIST_IGNORE;
}

/***************** GuiListItemWidth() *********************
 * Passed each nul-terminated item, before the list is
 * displayed for the first time. Sets "ColumnWidth" to
 * the widest item's pixel width, and "NumOfItems"
 * to the count of items. If passed 0, clears those 2
 * variables.
 */

void GuiListItemWidth(GUIAPPHANDLE app, GUIWIN * win, GUILIST * list, const char * name)
{
	if (name)
	{
		if (name[0])
		{
			register uint32_t		width;

			if (!win && !(win = app->Host.CurrentWin)) win = &app->MainWin;
			if (!list->ColumnWidth) GuiTextSetLarge(win);
			width = GuiTextWidth(win, name);
			if (list->ColumnWidth < width) list->ColumnWidth = width;
		}
		list->NumOfItems++;
	}
	else
	{
		// Initially no Item selected
		list->CurrItemNum = -1;
		list->TopItemNum = list->ColumnWidth = list->NumOfItems = 0;
	}
}










//=====================================================
// Numeric Input
// ====================================================

static const char NumpadLabels[] = {0,'C','a','n','c','e','l',0,
2,'0',0,
3,'1',0,
3,'2',0,
3,'3',0,
4,'4',0,
4,'5',0,
4,'6',0,
5,'7',0,
5,'8',0,
5,'9',0};

/********************** asciiToNum() **********************
 * Converts the ascii str of digits (expressed in base 10)
 * to a long.
 */

static int32_t asciiToNum(register const char * buf)
{
	register uint32_t			result, extra;
	register unsigned char	chr, firstchr;

	// A negative value?
	if ((firstchr = *buf) == '-') ++buf;

	// Convert next digit
	result = 0;
	while (*buf)
	{
		chr = *buf++ - '0';
		if (chr > 9) break;
		extra = result << 3;
		result += (result + extra + (uint32_t)chr);
	}

	// Return value
	if (firstchr != '-') return (int32_t)result;

	return -result;
}

static GUICTL * make_numpad(GUIAPPHANDLE app, GUIWIN * win)
{
	register GUICTL *			ctls;
	register uint32_t			i, x, yOffset;
	register const char *	labels;

	yOffset = win->MsgBox.YPos;

	labels = &NumpadLabels[0];
	ctls = &app->Msg.PresetCtls[0];

	for (i=0; i < 12; i++)
	{
		if (!(i % 3)) x = GuiWinGetBound(app, win, GUIBOUND_XBORDER);
		memset(ctls, 0, sizeof(GUICTL));
		ctls->Attrib.NumOfLabels = 1;
		ctls->Type = CTLTYPE_PUSH;
		ctls->Width = app->Host.GuiFont.CharWidth * (i == 1 ? 8 : 5);
		ctls->Flags.Global = CTLGLOBAL_PRESET|CTLGLOBAL_APPUPDATE;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
		ctls->X = x;	// ignore unitialized warning
#pragma GCC diagnostic pop
		if (i)
		{
			ctls->Label = &labels[1];
			ctls->Y = (labels[0] * (GuiCtlGetTypeHeight(app, CTLTYPE_PUSH) + GuiWinGetBound(app, win, GUIBOUND_YSPACE)) + yOffset);
			labels += 3;
			if (i == 1) labels += 5;
		}
		else
		{
			ctls->Attrib.Color = (GUICOLOR_BLACK << 4) | GUICOLOR_LIGHTGOLD;
			ctls->Label = &win->Numpad.Buffer[0];
			ctls->Y = yOffset;
		}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
		if (i != 1) x += ctls->Width + GuiWinGetBound(app, win, GUIBOUND_XBORDER);
#pragma GCC diagnostic pop
		ctls++;
	}

	ctls->Next = 0;
	ctls->LayerHeight = yOffset;
	return &app->Msg.PresetCtls[0];
}

/****************** GuiNumpadInit() ******************
 * Initiates numeric input pad, where GuiWinGetMsg()
 * displays/manages a 9-digit numeric pad so the user
 * can enter a number (int32_t).
 *
 * heading = Nul-terminated text to appear at the top
 * 			of the window, or 0 for none. Buffer must not
 * 			be freed until text is cleared.
 * initValue = Default value displayed to the user, or
 * 			0 for none.
 * limit = Max number of digits that the user can enter: 1 to 4.
 *
 * Note: GuiNumpadEnd() must be called to dismiss the
 * numeric pad.
 *
 * Alters window's GuiWinSetHeading() text. Uses temp buffer.
 */

void GuiNumpadInit(GUIAPPHANDLE app, GUIWIN * win, const char * heading, int32_t initValue, unsigned char limit)
{
	if (!win && !(win = app->Host.CurrentWin)) win = &app->MainWin;
	free_help(app, win);
	GuiWinSetHeading(app, win, heading, 0, GUICOLOR_GOLD);
	win->InternalFlags |= GUIWIN_NUMPAD;
	if ((win->Numpad.Limit = limit) > 4) limit = 4;
	win->Numpad.Index = win->Numpad.Buffer[0] = 0;
	if (initValue) win->Numpad.Index = sprintf(&win->Numpad.Buffer[0], "%i", initValue);
}

int32_t GuiNumpadEnd(GUIAPPHANDLE app, GUIWIN * win)
{
	if (!win && !(win = app->Host.CurrentWin)) win = &app->MainWin;
	win->InternalFlags &= ~GUIWIN_NUMPAD;
	GuiWinSetHeading(app, win, 0, 0, 0);
	return asciiToNum(&win->Numpad.Buffer[0]);
}
