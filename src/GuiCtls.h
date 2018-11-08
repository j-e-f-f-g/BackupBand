// GUI stuff. I use my own really thin toolkit wrapping XWindows and cairo

#ifdef WIN32
#include "windows.h"
#ifndef uint32_t
typedef uint32_t DWORD
#endif
#else
#include "XLibGuiCtls.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef I_AM_GUICTLS
typedef struct GUIAPP * GUIAPPHANDLE;
typedef struct GUIWINSTRUCT GUIWIN;
#else
typedef struct GUIAPP2 * GUIAPPHANDLE;
typedef struct GUIWIN2 GUIWIN;
#endif

#pragma pack(1)
typedef struct {
	uint32_t	X, Y, Width, Height;
} GUIBOX;

typedef struct {
	uint32_t	Left, Top, Right, Bottom;
	uint32_t	XPos, YPos, ItemIndex;
} GUIAREA;

typedef struct {
	uint32_t		CurrItemNum, TopItemNum, NumOfItems, NumOfItemsInView, ColumnWidth, SelX, SelY;
} GUILIST;

// ========================= GUI Controls ==========================

// GUICTL Type
#define CTLTYPE_END			0
#define CTLTYPE_PUSH			1
#define CTLTYPE_CHECK		2
#define CTLTYPE_RADIO		3
#define CTLTYPE_ARROWS		4
#define CTLTYPE_EDIT			5
#define CTLTYPE_AREA			6
#define CTLTYPE_INACTIVE	11
#define CTLTYPE_STATIC		(CTLTYPE_INACTIVE+0)
#define CTLTYPE_GROUPBOX	(CTLTYPE_INACTIVE+1)		// Should appear immediately after all ctls[] in the group
#define CTLTYPE_ENUM_MASK	0x0F
#define CTLTYPE_DONE			0x10
#define CTLTYPE_GROUPHIDE	0x20
#define CTLTYPE_NO_FOCUS   0x40
#define CTLTYPE_HIDE			0x80

// Presets
#define CTLFLAG_UPPER		0x01
#define CTLFLAG_SELECTED	0x02

// PUSH
#define CTLFLAG_ESC			0x01

// CTLTYPE_STATIC
#define CTLFLAG_CENTER_Y	0x01		// For CTLFLAG_MULTILINE, centers text
#define CTLFLAG_MULTILINE	0x02		// Multiple lines separated by '\n'. .NumOfLines sets the limit
#define CTLFLAG_LEFTTEXT	0x04

// CHECK/RADIO
#define CTLFLAG_LABELBOX	0x80		// GROUPBOX drawn around the buttons

// CTLTYPE_ARROWS
#define CTLFLAG_UP_SELECT		0x01	// Up arrow is selected
#define CTLFLAG_DOWN_SELECT	0x02	// Down arrow is selected
#define CTLFLAG_NO_DOWN			0x10	// Down arrow is disabled
#define CTLFLAG_NO_UP			0x20	// Up arrow is disabled
#define CTLFLAG_NOSTRINGS		0x40	// .Attrib.Value is a literal numeric value, versus a index into Label[]
#define CTLFLAG_AREA_HELPER	0x80	// Arrows associated with a preceding AREA

// CTLTYPE_AREA
#define CTLFLAG_AREA_X_EXPAND		0x04	// AREA expands only by width, not height
#define CTLFLAG_AREA_FULL_SIZE	0x08	// AREA automatically occupies all unallocated space
#define CTLFLAG_AREA_LIST			0x10	// Displays a list of strings
#define CTLFLAG_AREA_STATIC		0x20	// Doesn't resize
#define CTLFLAG_AREA_EVENHEIGHT 0x40 // Round height to a multiple of large font lines
#define CTLFLAG_AREA_REQ_SIZE	0x80	// Set by GUICTL lib

// CTLTYPE_EDIT
#define CTLFLAG_WANT_RETURN 0x01	// GuiWinGetMsg returns GUI_KEY_PRESS when user presses Enter, Tab, or ESC on
		// a selected EDIT ctl. When GUIWIN->Flags = GUIWIN_TAB_KEY, and the user presses Tab, a GUI_KEY_PRESS
		// event happens with GUIKEY->Code = XK_Tab to indicate loss of focus. When the user presses Esc (and
		// GUIWIN->Flags != GUIWIN_ESC_KEY), a GUI_KEY_PRESS event happens with GUIKEY->Code = XK_Esc. When
		// the user presses Enter, a GUI_KEY_PRESS event happens with GUIKEY->Code = XK_Return to indicate text entry is done

#define CTLFLAG_NUMERIC		0x02
#define CTLFLAG_U_NUMERIC	0x04
#define CTLFLAG_INTEGER		0x08

// CTLTYPE_GROUPBOX
//#define CTLFLAG_GROUPHIDE	0x01

// GUICTL->Flags.Global
#define CTLGLOBAL_PRESET		0x80	// One of the Preset buttons in GUIWIN->LowerPresetBtns/UpperPresetBtns
#define CTLGLOBAL_AUTO_VAL		0x40	// CHECK/RADIO/ARROWS/AREA=LIST
#define CTLGLOBAL_GET_LABEL	0x20	// All except AREA/EDIT. Ctl supplies label via BtnLabel() callback
#define CTLGLOBAL_X_ALIGN		0x10	// GUICTL->X is referenced to another preceding ctl. All supported
#define CTLGLOBAL_GROUPSTART	0x08	// First control of a GROUP
#define CTLGLOBAL_APPUPDATE	0x04	// Lib doesn't mark a ctl for redraw when selected/unselected. App
												// must call GuiCtlUpdate()
#define CTLGLOBAL_NOPADDING	0x02	// Min X spacing between preceding ctl
#define CTLGLOBAL_SMALL			0x01	// PUSH are single (versus 2) line height. Label can't contain '\n'
												// CHECK/RADIO have minimum space between boxes
struct GUIATTRIB {
	// Attrib.NumOfLabels = For PUSH/RADIO/CHECK/ARROWS/STATIC, # of strings contained
	//								in Label[], not counting any group label.
	// Attrib.LabelNum =		For EDIT, 0-based index of label within GUIWIN->EditLabels[]
	union {
	unsigned char		NumOfLabels;
	unsigned char		LabelNum;
	};
	// Attrib.Color = 		For STATIC/PUSH. Low nibble is color for background, High nibble is text color
	// Attrib.Value =			For RADIO/CHECK/ARROWS, index of currently selected Labels[] string
	// Attrib.MaxChars =		For EDIT, max chars that can be entered into text box.
	union {
	unsigned char		Color;
	unsigned char		Value;
	unsigned char		MaxChars;
	};
};

struct GUICTLFLAGS {
	unsigned char		Local;
	unsigned char		Global;
};

#define GUICTL_ABS_SIZE		0x8000
#define GUICTL_ABS_XOFFSET	0x8000

// Info about one control in a window.
typedef struct _GUICTL {
	union {
	// .Label = All except END, EDIT, AREA, or ctls with CTLGLOBAL_GET_LABEL.
	//				For PUSH/STATIC, ptr to 1 line (or 2 line with \n) nul-term text.
	//				For ARROWS, ptr to an array of nul-term strs for the items, preceded by the label or a nul
	//					byte. If CTLFLAG_NOSTRINGS, then only label or 0.
	//				For RADIO/CHECK, ptr to an array of nul-term labels for the buttons, followed by the label
	//					for group box if CTLFLAG_LABELBOX.
	//				For GROUP, ptr to nul-term label.
	// .Buffer = For EDIT, ptr to mem where user-entered text stored.
	// .AreaDraw = For AREA without CTLFLAG_AREA_LIST
	// .ListDraw = For AREA with CTLFLAG_AREA_LIST
	// .BtnLabel = For ctls with CTLGLOBAL_GET_LABEL.
	// .Next = For END, ptr to next array of GUICTLs, or 0 if none
	const char *		Label;
	char *				Buffer;
	void					(*AreaDraw)(GUIAPPHANDLE, struct _GUICTL *, GUIAREA *);
	GUILIST *			(*ListDraw)(GUIAPPHANDLE, struct _GUICTL *, GUIAREA *);
	const char *		(*BtnLabel)(GUIAPPHANDLE, struct _GUICTL *, char *);
	struct _GUICTL *	Next;
	};

	// For app's use. Should #define APPDATATYPE before #including GuiCtls.h
	// to be some data type, such as:
	//
	// #define APPDATATYPE struct MYSTRUCT;
	// #include "GuiCtls.h"
	union {
#ifdef APPCTLDATATYPE
	APPCTLDATATYPE *	AppData;
#endif
	void *			Ptr;
	};

	union {
	unsigned short		X;					// X position in pixels. Set by GuiCtlScale(). Initially, X offset in chars
	unsigned short		LayerWidth;
	};
	union {
	unsigned short		Y;					// Y Position in pixels. Set by GuiCtlScale(). Initially, line number
	unsigned short		LayerHeight;
	};
	union {
	unsigned short		Width;			// Width in pixels. Set by GuiCtlScale(). Initially, default width in chars
 	unsigned short		LayerUnused1;
	};

	// .BottomY =				For AREA/GROUP, lower Y position.
	// .Attrib.NumOfLabels = For PUSH/RADIO/CHECK/ARROWS/STATIC, # of strings contained
	//								in Label[], not counting any group label.
	// .Attrib.Value =		For RADIO/CHECK/ARROWS, index of currently selected Labels[] string.
	// .Attrib.Color = 		For STATIC/PUSH. Low nibble is color for background, High nibble is
	//								text color.
	//
	// .Attrib.LabelNum =	For EDIT, 0-based index of label within GUIWIN->EditLabels[].
	// .Attrib.MaxChars =	For EDIT, max chars that can be entered into text box.
	union {
	struct GUIATTRIB	Attrib;
	unsigned short		BottomY;
 	unsigned short		LayerUnused2;
	};

	// CTLTYPE_xxx
	unsigned char		Type;

	// .Select =		Currently selected item for CHECK/RADIO.
	// .CursorPos =	Cursor pos for EDIT.
	// .NumOfCtls =	# of preceding ctls in GROUP. Set by GuiCtlScale().
	// .MinValue =		display min value for NOSTRINGS ARROW.
	// .NumOfLines =	# of text lines (height) for STATIC with CTLFLAG_MULTILINE.
	// .PresetId =		GUIBTN_xxx ID for CTLGLOBAL_PRESET ctls.
	union {
	unsigned char		Select;
	unsigned char		CursorPos;
	unsigned char		NumOfCtls;
	unsigned char		MinValue;
	unsigned char		NumOfLines;
	unsigned char		PresetId;
	unsigned char		LayerUnused3;
	};

	union {
	struct GUICTLFLAGS	Flags;
 	unsigned short			LayerUnused4;
	};
} GUICTL;

typedef void GUIMENUFUNC(GUIAPPHANDLE);

typedef struct {
	const char **		Labels;
	unsigned short *	Widths;
	GUIMENUFUNC **		Funcs;
	unsigned short		Padding;
	unsigned char		Select;		// Label[] index of selected item
	unsigned char		LabelCnt;	// High nibble = how many chars of space to reserve at right border
	unsigned char		Color;
} GUIMENU;

// GUIWIN->LowerPresetBtns/UpperPresetBtns
#define GUIBTN_SHOWMASK		0x00003FFF
#define GUIBTN_HIDESHIFT	15
#define GUIBTN_CENTER		0x40000000				// GUIWIN UpperPresetBtns/LowerPresetBtns
#define GUIBTN_RESERVED		0x80000000
#define GUIBTN_TIMEOUT_SHOW 0x80000000				// GuiErrShow,GuiWinModal only
#define GUIBTN_ESC_SHOW		 0x40000000				// GuiErrShow,GuiWinModal only
#define GUIBTN_SHOW(a)		(0x00000001 << (a))
#define GUIBTN_DEFAULT(a)	(((a)+1)<<16)				// GuiErrShow,GuiWinModal only
#define GUIBTN_HIDE(a)		(0x00000001 << (GUIBTN_HIDESHIFT + (a)))

#define GUIBTN_CANCEL	0
#define GUIBTN_NO			1
#define GUIBTN_YES		2
#define GUIBTN_OK			3
#define GUIBTN_SAVE		4
#define GUIBTN_LOAD		5
#define GUIBTN_HELP		6
#define GUIBTN_NEW		7
#define GUIBTN_DEL		8
#define GUIBTN_COPY		9
#define GUIBTN_ABORT		10
#define GUIBTN_IGNORE	11
#define GUIBTN_RETRY		12
#define GUIBTN_SKIP		13
#define GUIBTN_NUMPAD	25		// Returned only via GuiNumpadXXX()
#define GUIBTN_ESC		252	// Returned by GuiErrShow if usr presses ESC, and if GUIBTN_ESC_SHOW
#define GUIBTN_TIMEOUT	253	// Returned by GuiErrShow after 15 sec no activity, and if GUIBTN_TIMEOUT_SHOW
#define GUIBTN_QUIT		254	// Returned by GuiErrShow if user clicks closebox. Can be returned any time
#define GUIBTN_ERROR		255	// Returned by GuiErrShow if a failure. Can be returned any time

#define GUIBTN_CANCEL_HIDE	GUIBTN_HIDE(GUIBTN_CANCEL)
#define GUIBTN_NO_HIDE		GUIBTN_HIDE(GUIBTN_NO)
#define GUIBTN_YES_HIDE		GUIBTN_HIDE(GUIBTN_YES)
#define GUIBTN_OK_HIDE		GUIBTN_HIDE(GUIBTN_OK)
#define GUIBTN_SAVE_HIDE	GUIBTN_HIDE(GUIBTN_SAVE)
#define GUIBTN_LOAD_HIDE	GUIBTN_HIDE(GUIBTN_LOAD)
#define GUIBTN_HELP_HIDE	GUIBTN_HIDE(GUIBTN_HELP)
#define GUIBTN_NEW_HIDE		GUIBTN_HIDE(GUIBTN_NEW)
#define GUIBTN_DEL_HIDE		GUIBTN_HIDE(GUIBTN_DEL)
#define GUIBTN_COPY_HIDE	GUIBTN_HIDE(GUIBTN_COPY)
#define GUIBTN_ABORT_HIDE	GUIBTN_HIDE(GUIBTN_ABORT)
#define GUIBTN_IGNORE_HIDE	GUIBTN_HIDE(GUIBTN_IGNORE)
#define GUIBTN_RETRY_HIDE	GUIBTN_HIDE(GUIBTN_RETRY)
#define GUIBTN_SKIP_HIDE	GUIBTN_HIDE(GUIBTN_SKIP)
#define GUIBTN_CANCEL_SHOW	GUIBTN_SHOW(GUIBTN_CANCEL)
#define GUIBTN_NO_SHOW		GUIBTN_SHOW(GUIBTN_NO)
#define GUIBTN_YES_SHOW		GUIBTN_SHOW(GUIBTN_YES)
#define GUIBTN_OK_SHOW		GUIBTN_SHOW(GUIBTN_OK)
#define GUIBTN_SAVE_SHOW	GUIBTN_SHOW(GUIBTN_SAVE)
#define GUIBTN_LOAD_SHOW	GUIBTN_SHOW(GUIBTN_LOAD)
#define GUIBTN_HELP_SHOW	GUIBTN_SHOW(GUIBTN_HELP)
#define GUIBTN_NEW_SHOW		GUIBTN_SHOW(GUIBTN_NEW)
#define GUIBTN_DEL_SHOW		GUIBTN_SHOW(GUIBTN_DEL)
#define GUIBTN_COPY_SHOW	GUIBTN_SHOW(GUIBTN_COPY)
#define GUIBTN_ABORT_SHOW	GUIBTN_SHOW(GUIBTN_ABORT)
#define GUIBTN_IGNORE_SHOW	GUIBTN_SHOW(GUIBTN_IGNORE)
#define GUIBTN_RETRY_SHOW	GUIBTN_SHOW(GUIBTN_RETRY)
#define GUIBTN_SKIP_SHOW	GUIBTN_SHOW(GUIBTN_SKIP)

// Passed to GuiShowError/GuiWinModal
#define GUIBTN_CANCEL_DEFAULT	GUIBTN_DEFAULT(GUIBTN_CANCEL)
#define GUIBTN_NO_DEFAULT		GUIBTN_DEFAULT(GUIBTN_NO)
#define GUIBTN_YES_DEFAULT		GUIBTN_DEFAULT(GUIBTN_YES)
#define GUIBTN_OK_DEFAULT		GUIBTN_DEFAULT(GUIBTN_OK)
#define GUIBTN_SAVE_DEFAULT	GUIBTN_DEFAULT(GUIBTN_SAVE)
#define GUIBTN_LOAD_DEFAULT	GUIBTN_DEFAULT(GUIBTN_LOAD)
#define GUIBTN_HELP_DEFAULT	GUIBTN_DEFAULT(GUIBTN_HELP)
#define GUIBTN_NEW_DEFAULT		GUIBTN_DEFAULT(GUIBTN_NEW)
#define GUIBTN_DEL_DEFAULT		GUIBTN_DEFAULT(GUIBTN_DEL)
#define GUIBTN_COPY_DEFAULT	GUIBTN_DEFAULT(GUIBTN_COPY)
#define GUIBTN_ABORT_DEFAULT	GUIBTN_DEFAULT(GUIBTN_ABORT)
#define GUIBTN_IGNORE_DEFAULT	GUIBTN_DEFAULT(GUIBTN_IGNORE)
#define GUIBTN_RETRY_DEFAULT	GUIBTN_DEFAULT(GUIBTN_RETRY)
#define GUIBTN_SKIP_DEFAULT	GUIBTN_DEFAULT(GUIBTN_SKIP)

// ---------------------
// GUIWIN Flags
// ---------------------

// Set for a Help window that GuiHelpShow() uses if app doesn't specify a
// particular window. Also GuiWinCloseAll() doesn't close it
#define GUIWIN_HELP_ANCHOR		0x80000000

// If set, when GuiErrShow() times out with no user response, it returns GUIBTN_TIMEOUT
#define GUIWIN_TIMEOUT_ERRS	0x40000000

// GuiCtlSelectXY() simply tests whether a mouse position is within any ctl, but does
// not select the ctl
#define GUIWIN_NO_UPD_CURR		0x20000000

// ======= GUI_MOUSE_CLICK ==========
// GUI_MOUSE_CLICK with GUIMOUSE->Direction=1 when mouse button is released
#define GUIWIN_WANT_MOUSEUP	0x00200000
// For MOUSEUP, then a mouse release sets GUIMOUSE's SelectedCtl when released over a ctl.
// If GUIWIN_GENERIC_UP set, then SelectedCtl not set.
// Ignored if GUIWIN_WANT_MOUSEUP not also specified
#define GUIWIN_GENERIC_UP		0x00100000
// Other mouse buttons (besides Left) report clicks/releases via GUI_MOUSE_CLICK (with
// GUIMOUSE->Direction = 0 or 1). Presses ignored if GUIWIN_WANT_MOUSEDOWN not also specified.
// Releases ignored if GUIWIN_WANT_MOUSEUP not also specified
#define GUIWIN_ALL_MOUSEBTN	0x00080000
// Raw mouse with no extra processing. Only the following set: GUIAPP's PreviousTime,
// CurrTime, CurrentWin. GUIMOUSE's Direction, Flags, ButtonNum, SelectedCtl=0. Defeats
// all mouse events below
#define GUIWIN_RAW_MOUSE		0x00040000
// Returns a GUI_MOUSE_CLICK with GUIMOUSE's SelectedCtl=0 when click outside of any ctl
#define GUIWIN_BACKGND_CLICK	0x00020000
// Returns a GUI_HELP event if click on a GUIBTN_HELP preset.
#define GUIWIN_HELP_BTN			0x00010000

// ======= GUI_KEY_PRESS ==========
// Receive key release events. Same as key down but GUIKEY's Direction = 1 (instead of 0)
#define GUIWIN_WANT_KEYUP		0x00008000
// Raw keycode with no extra processing. Only the following set: GUIAPP's PreviousTime,
// CurrTime, CurrentWin. GUIKEY's Direction, Flags, Code. Defeats all key events below
#define GUIWIN_RAW_KEY			0x00000200
// Pressing F1 returns a GUI_HELP event
#define GUIWIN_HELP_KEY			0x00000100
// Translates virtual to keycap
#define GUIWIN_KEYCAP			0x00000080
// Pressing SHIFT+TAB changes the focus to the next menu item
#define GUIWIN_SHIFTTAB_KEY	0x00000040
// Pressing TAB changes the focus to the next ctl. No GUI_KEY_PRESS event happens.
#define GUIWIN_TAB_KEY			0x00000020
// Pressing ESC translates to a GUI_MOUSE_CLICK event on any PUSH ctl
// with GUICTL->Flags.Local = CTLFLAG_ESC. If no such ctl, generates a
// a GUI_MOUSE_CLICK event on an invisible preset ctl with
// GUIMOUSE->SelectedCtl->Flags.Global = CTLGLOBAL_PRESET and
// GUIMOUSE->SelectedCtl->PresetId = GUIBTN_ESC
#define GUIWIN_ESC_KEY			0x00000010
// Pressing Enter causes a GUI_MOUSE_CLICK event to be returned instead of
// GUI_KEY_PRESS. For an EDIT ctl with CTLFLAG_WANT_RETURN, a GUI_KEY_PRESS
// event happens instead with GUIKEY->Code = XK_Return
#define GUIWIN_ENTER_KEY		0x00000008
// Lib handles all char input for an EDIT ctl (except Enter/Tab/ESC)
#define GUIWIN_EDIT_AUTOPROCESS	0x00000004
// When no ctl is selected, and user presses a key,
// returns GUI_KEY_PRESS event with GUIKEY->SelectedCtl = 0
#define GUIWIN_BACKGND_KEY		0x00000002
// Arrow up/down, Page up/down. ESC, and Enter keys get translated
// to GUI_MOUSE_CLICK for AREA ctl with CTLFLAG_AREA_LIST.
// GUIMOUSE->ListAction set to one of the GUILIST_xxx values
#define GUIWIN_LIST_KEY			0x00000001

struct GUIWINSTRUCT {
	GUICTL *				Ctls;
	union {
#ifdef APPWINDATATYPE
	APPWINDATATYPE *	AppData;
#endif
	void *				Ptr;
	};
	GUIMENU *			Menu;
	const char *		EditLabels;
	uint32_t				Flags;
	uint32_t				UpperPresetBtns;
	uint32_t				LowerPresetBtns;
	GUIBOX				WinPos;
	unsigned short		MinWidth, MinHeight;
};

// GUIMOUSE->Direction
#define GUIMOUSE_DOWN		0
#define GUIMOUSE_UP			1
#define GUIMOUSE_OTHERDOWN	2

// GUIMOUSE->ListAction
#define GUILIST_SELECTION	0
#define GUILIST_SCROLL		1
#define GUILIST_CLICK		2
#define GUILIST_IGNORE		4
#define GUILIST_ABORT		-1

typedef struct {
	GUICTL *			SelectedCtl;
	uint16_t			X;
	uint16_t			Y;
	unsigned char	Direction;
	unsigned char	ButtonNum;
	uint16_t			AbsX;
	uint16_t			AbsY;
	unsigned char	Flags;	// For SHIFT/ALT/CTL/WIN key state
	char				ListAction;	// For AREA with CTLFLAG_AREA_LIST and AUTO_VAL
	GUIBOX			Area;
	GUIBOX			Helper;
} GUIMOUSE;

typedef struct {
	GUICTL *			SelectedCtl;
	int32_t			Value;
} GUINUMPAD;

typedef struct {
	long				Cmd, Data;
} GUISIGNAL;

#ifndef ControlMask
#define ShiftMask	(1<<0)
#define LockMask	(1<<1)
#define ControlMask	(1<<2)
#define AltMask	(1<<3)
#endif

typedef struct {
	GUICTL *			SelectedCtl;
	uint16_t			Code;
	char				Keycap;
	unsigned char	Flags;	// For SHIFT/ALT/CTL/WIN key state
	unsigned char	Direction;
	unsigned char	Pad;
} GUIKEY;

typedef struct {
	unsigned char		Type;
	char					Filename[1];
	unsigned short		Size;
	union {
	GUIKEY				Key;
	GUIMOUSE				Mouse;
	GUISIGNAL			Signal;
	GUINUMPAD			Numpad;
	unsigned char		MenuNum;
	};
	GUICTL				PresetCtls[15];
} GUIMSG;

typedef struct {
	const char *	Title;
	union {
	void *			ParentHandle;
	int				ParentInt;
	};
	uint32_t			X, Y, Width, Height;
	uint32_t			MinWidth, MinHeight;
	unsigned char	FontSize;
} GUIINIT;

typedef struct {
	unsigned short Height, CharWidth, HeightOffset, SmallHeightOffset;
	unsigned char	QuarterWidth, Spacing;
} GUIFONTS;

// Fundamental struct that keeps track of all resources for a given app
struct GUIAPP {
	GUIWIN *			CurrentWin;
	const char *	PresetBtnStrs;
	uint32_t			CurrTime;
	uint32_t			PreviousTime;
	uint32_t			Timeout;
	GUIFONTS			GuiFont;
	unsigned char	ClickSpeed;		// Each inc = +16
	unsigned char	DClickFlag;
#ifndef I_AM_GUICTLS
	struct GUIWINSTRUCT	MainWin;
#endif
};

#pragma pack()

typedef void GUIAREAFUNC(GUIAPPHANDLE, GUICTL *, GUIAREA *);
typedef GUILIST * GUILISTFUNC(GUIAPPHANDLE, GUICTL *, GUIAREA *);

void *			GuiInit(uint32_t);
void				GuiDone(void *, uint32_t);

#define GUIDRAW_Y_CENTER	CTLFLAG_CENTER_Y
#define GUIDRAW_NODRAW		0x00008000
#define GUIDRAW_SMALLTEXT	0x00004000
#define GUIHEADING_UPDATE	GUIDRAW_NODRAW		// for GuiWinSetHeading, forces heading redrawn
uint32_t			GuiTextDrawMsg(GUIAPPHANDLE, const char *, GUIBOX *, uint32_t);
void				GuiWinSetHeading(GUIAPPHANDLE, GUIWIN *, const char *, unsigned char, unsigned char);

// For GuiTextSetColor()
#define GUICOLOR_BACKGROUND	0
#define GUICOLOR_BLACK			1
#define GUICOLOR_RED				2
#define GUICOLOR_GREEN			3
#define GUICOLOR_ORANGE			4
#define GUICOLOR_LIGHTBLUE		5
#define GUICOLOR_PINK			6
#define GUICOLOR_GOLD			7
#define GUICOLOR_DARKGREEN		8
#define GUICOLOR_PURPLE			9
#define GUICOLOR_VIOLET			10
#define GUICOLOR_BLUE			11
#define GUICOLOR_DARKGRAY		12
#define GUICOLOR_GRAY			13
#define GUICOLOR_LIGHTGOLD		14
#define GUICOLOR_MENU			15
void				GuiTextSetColor(GUIWIN *, uint32_t);
void				GuiTextSetLarge(GUIWIN *);
void				GuiTextSetSmall(GUIWIN *);
uint32_t			GuiTextWidth(GUIWIN *, const char *);
void				GuiTextDrawSmall(GUIAPPHANDLE, uint32_t, uint32_t, const char *);
void				GuiTextDraw(GUIAPPHANDLE, uint32_t, uint32_t, const char *);
void				GuiCtlCenterBtns(GUIAPPHANDLE, GUIWIN *, GUICTL *, uint32_t);
void				GuiCtlSetWidth(GUIAPPHANDLE, GUIWIN *, GUICTL *);
int				GuiCtlEditboxKey(GUIAPPHANDLE);
unsigned char	GuiCtlArrowsValue(GUIAPPHANDLE, GUICTL *);
void				GuiCtlArrowsInit(GUICTL *, unsigned char);
void				GuiCtlAbsScale(GUIAPPHANDLE, GUIWIN *, GUICTL *);
void				GuiCtlScale(GUIAPPHANDLE, GUIWIN *, GUICTL *, uint32_t);
GUICTL *			GuiCtlSetSelect(GUIAPPHANDLE, GUIWIN *, GUICTL *, GUICTL *);
GUICTL *			GuiCtlGetSelect(GUIAPPHANDLE, GUICTL *);
uint32_t			GuiCtlGetHeight(GUIAPPHANDLE, const GUICTL *);
uint32_t			GuiCtlGetTypeHeight(GUIAPPHANDLE, uint32_t);
#define GUICTL_SHOW	0
#define GUICTL_HIDE	CTLTYPE_HIDE
#define GUICTL_REDRAW	0xFF;
void				GuiCtlShow(GUIAPPHANDLE, GUIWIN *, GUICTL *, unsigned char);
void				GuiCtlUpdate(GUIAPPHANDLE, GUIWIN *, const GUICTL *, const GUICTL *, uint32_t);
GUICTL *			GuiCtlNext(GUIAPPHANDLE, GUICTL *);
GUICTL *			GuiCtlSelectXY(GUIAPPHANDLE, GUICTL *);
void				GuiCtlDrawCheck(GUIWIN *, GUIAREA *);
uint32_t			GuiCtlDraw(GUIAPPHANDLE, GUICTL *);
uint32_t			GuiMenuCalcLabelWidths(GUIAPPHANDLE, GUIWIN *);
void				GuiWinCloseAll(GUIAPPHANDLE);
void				GuiWinUpdate(GUIAPPHANDLE, GUIWIN *);
void				GuiWinAreaUpdate(GUIAPPHANDLE, GUIWIN *, GUIBOX *);
void				GuiWinRect(GUIAPPHANDLE, GUIBOX *, uint32_t);
void *			GuiWinSignal(GUIAPPHANDLE, void *, long);
uint32_t			GuiExeGetPath(char *);

#define GUISTATE_CREATE		0x00000001
#define GUISTATE_FREE		0x00000002
#define GUISTATE_LINK		0x00000004
#define GUISTATE_UNLINK		0x00000008
#define GUISTATE_SIZE		0x00000010
#define GUISTATE_MOVE		0x00000020
#define GUISTATE_TITLE		0x00000040
#define GUISTATE_OPEN		0x00000080
#define GUISTATE_CLOSE		0x00000100
#define GUISTATE_HIDE		0x00000200
#define GUISTATE_SHOW		0x00000400
#define GUISTATE_MENU		0x00000800
#define GUISTATE_GET_BASE	0x00001000
#define GUISTATE_AREA_RECALC 0x00002000
#define GUISTATE_FONTSIZE	0x00004000
#define GUISTATE_MINSIZE	0x00008000
#define GUISTATE_ENUMERATE 0x00010000
#define GUISTATE_GET_DRAWHANDLE	0x00020000
GUIWIN *			GuiWinState(GUIAPPHANDLE, GUIWIN *, uint32_t);
void				GuiWinResize(GUIWIN *);

#define GUIBOUND_UPPER		0x01
#define GUIBOUND_LOWER		0x02
#define GUIBOUND_XBORDER	0x04
#define GUIBOUND_YBORDER	0x08
#define GUIBOUND_XSPACE		0x10
#define GUIBOUND_YSPACE		0x20
#define GUIBOUND_XBETWEEN	0x40
#define GUIBOUND_YHEADSTR	0x80
unsigned short GuiWinGetBound(GUIAPPHANDLE, GUIWIN *, uint32_t);

// GuiWinGetMsg GUIMSG->Type values
#define GUI_MOUSE_CLICK		0
#define GUI_KEY_PRESS		1
#define GUI_MENU_SELECT		2
#define GUI_WINDOW_CLOSE	3
#define GUI_WINDOW_DRAW		4
#define GUI_WINDOW_SIZE		5
#define GUI_HELP				6
#define GUI_SIGNAL			7
#define GUI_TIMEOUT			8
GUIMSG *			GuiWinGetMsg(GUIAPPHANDLE);
GUIMSG *			GuiWinGetTimedMsg(GUIAPPHANDLE, uint32_t);

int				GuiHelpShow(GUIAPPHANDLE, GUIWIN *, const char *);
uint32_t			GuiErrShow(GUIAPPHANDLE, const char *, uint32_t);
uint32_t			GuiWinModal(GUIAPPHANDLE, const char *, uint32_t);

#define GUISTATE_GET_INIT		0x00000001
#define GUISTATE_GET_TEMPBUF	0x00000002
#define GUISTATE_GET_TEMPBUFSIZE	0x00000004
#define GUISTATE_POLL_ON		0x00000008
#define GUISTATE_POLL_OFF		0x00000010
#define GUISTATE_PLUGIN			0x00000020
#define GUISTATE_GET_TIME		0x00000040
#define GUISTATE_TIMEOUT_ON	0x00000080
#define GUISTATE_TIMEOUT_OFF	0x00000100
void *	GuiAppState(GUIAPPHANDLE, uint32_t, void *);

#define GUIFILE_DIR				1
char *	GuiFileDlg(GUIAPPHANDLE, const char *, const char *, const char *, uint32_t);

void		GuiListItemWidth(GUIAPPHANDLE, GUIWIN *, GUILIST *, const char *);
int32_t	GuiListDrawItem(GUIAPPHANDLE, GUICTL *, GUIAREA *, const char *);
char		GuiListMouse(GUIAPPHANDLE, GUICTL *);
char		GuiListKey(GUIAPPHANDLE, GUICTL *);

void		GuiNumpadInit(GUIAPPHANDLE, GUIWIN *, const char *, int32_t, unsigned char);
int32_t	GuiNumpadEnd(GUIAPPHANDLE, GUIWIN *);

#ifdef __cplusplus
}
#endif
