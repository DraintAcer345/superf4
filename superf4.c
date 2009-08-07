/*
	SuperF4 - Force kill programs with Ctrl+Alt+F4
	Copyright (C) 2009  Stefan Sundin (recover89@gmail.com)
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#define UNICODE
#define _UNICODE

#include <stdio.h>
#include <stdlib.h>
#define _WIN32_WINNT 0x0500
#define _WIN32_IE 0x0600
#include <windows.h>
#include <shlwapi.h>

//App
#define APP_NAME      L"SuperF4"
#define APP_VERSION   "1.1"
#define APP_URL       L"http://superf4.googlecode.com/"
#define APP_UPDATEURL L"http://superf4.googlecode.com/svn/wiki/latest-stable.txt"

//Messages
#define WM_ICONTRAY            WM_USER+1
#define SWM_TOGGLE             WM_APP+1
#define SWM_HIDE               WM_APP+2
#define SWM_AUTOSTART_ON       WM_APP+3
#define SWM_AUTOSTART_OFF      WM_APP+4
#define SWM_AUTOSTART_HIDE_ON  WM_APP+5
#define SWM_AUTOSTART_HIDE_OFF WM_APP+6
#define SWM_UPDATE             WM_APP+7
#define SWM_ABOUT              WM_APP+8
#define SWM_EXIT               WM_APP+9

//Stuff missing in MinGW
#define NIIF_USER 4
#define NIN_BALLOONSHOW        WM_USER+2
#define NIN_BALLOONHIDE        WM_USER+3
#define NIN_BALLOONTIMEOUT     WM_USER+4
#define NIN_BALLOONUSERCLICK   WM_USER+5

//Localization
struct strings {
	wchar_t *menu_enable;
	wchar_t *menu_disable;
	wchar_t *menu_hide;
	wchar_t *menu_autostart;
	wchar_t *menu_update;
	wchar_t *menu_about;
	wchar_t *menu_exit;
	wchar_t *tray_enabled;
	wchar_t *tray_disabled;
	wchar_t *update_balloon;
	wchar_t *update_dialog;
	wchar_t *about_title;
	wchar_t *about;
};
#include "localization/strings.h"
struct strings *l10n = &en_US;

//Boring stuff
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
HICON icon[2];
NOTIFYICONDATA traydata;
UINT WM_TASKBARCREATED = 0;
UINT WM_UPDATESETTINGS = 0;
UINT WM_ADDTRAY = 0;
UINT WM_HIDETRAY = 0;
int tray_added = 0;
int hide = 0;
struct {
	int CheckForUpdate;
} settings = {0};
wchar_t txt[1000];

//Cool stuff
HINSTANCE hinstDLL = NULL;
HHOOK keyhook = NULL;
HHOOK mousehook = NULL;
HWND cursorwnd = NULL;
int ctrl = 0;
int alt = 0;
int win = 0;
int superkill = 0;

//Error() and CheckForUpdate()
#include "include/error.h"
#include "include/update.h"

//Entry point
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow) {
	//Check command line
	if (!strcmp(szCmdLine,"-hide")) {
		hide = 1;
	}
	
	//Look for previous instance
	WM_UPDATESETTINGS = RegisterWindowMessage(L"UpdateSettings");
	WM_ADDTRAY = RegisterWindowMessage(L"AddTray");
	WM_HIDETRAY = RegisterWindowMessage(L"HideTray");
	HWND previnst = FindWindow(APP_NAME,NULL);
	if (previnst != NULL) {
		PostMessage(previnst, WM_UPDATESETTINGS, 0, 0);
		if (hide) {
			PostMessage(previnst, WM_HIDETRAY, 0, 0);
		}
		else {
			PostMessage(previnst, WM_ADDTRAY, 0, 0);
		}
		return 0;
	}
	
	//Load settings
	wchar_t path[MAX_PATH];
	GetModuleFileName(NULL, path, sizeof(path)/sizeof(wchar_t));
	PathRenameExtension(path, L".ini");
	GetPrivateProfileString(L"Update", L"CheckForUpdate", L"0", txt, sizeof(txt)/sizeof(wchar_t), path);
	swscanf(txt, L"%d", &settings.CheckForUpdate);
	GetPrivateProfileString(APP_NAME, L"Language", L"en-US", txt, sizeof(txt)/sizeof(wchar_t), path);
	int i;
	for (i=0; i < num_languages; i++) {
		if (!wcscmp(txt,languages[i].code)) {
			l10n = languages[i].strings;
			break;
		}
	}
	
	//Create window class
	WNDCLASSEX wnd;
	wnd.cbSize = sizeof(WNDCLASSEX);
	wnd.style = 0;
	wnd.lpfnWndProc = WindowProc;
	wnd.cbClsExtra = 0;
	wnd.cbWndExtra = 0;
	wnd.hInstance = hInst;
	wnd.hIcon = NULL;
	wnd.hIconSm = NULL;
	wnd.hCursor = LoadImage(hInst,L"kill",IMAGE_CURSOR,0,0,LR_DEFAULTCOLOR);
	wnd.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wnd.lpszMenuName = NULL;
	wnd.lpszClassName = APP_NAME;
	
	//Register class
	RegisterClassEx(&wnd);
	
	//Create window
	cursorwnd = CreateWindowEx(WS_EX_TOOLWINDOW|WS_EX_TOPMOST, wnd.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL); //WS_EX_LAYERED
	
	//Load icons
	icon[0] = LoadImage(hInst,L"tray-disabled",IMAGE_ICON,0,0,LR_DEFAULTCOLOR);
	icon[1] = LoadImage(hInst,L"tray-enabled",IMAGE_ICON,0,0,LR_DEFAULTCOLOR);
	if (icon[0] == NULL || icon[1] == NULL) {
		Error(L"LoadImage('tray-*')", L"Fatal error.", GetLastError(), __LINE__);
		PostQuitMessage(1);
	}
	
	//Create icondata
	traydata.cbSize = sizeof(NOTIFYICONDATA);
	traydata.uID = 0;
	traydata.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP;
	traydata.hWnd = cursorwnd;
	traydata.uCallbackMessage = WM_ICONTRAY;
	//Balloon tooltip
	traydata.uTimeout = 10000;
	wcsncpy(traydata.szInfoTitle, APP_NAME, sizeof(traydata.szInfoTitle)/sizeof(wchar_t));
	traydata.dwInfoFlags = NIIF_USER;
	
	//Register TaskbarCreated so we can re-add the tray icon if explorer.exe crashes
	WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");
	
	//Update tray icon
	UpdateTray();
	
	//Hook keyboard
	HookKeyboard();
	
	//Add tray if hooking failed, even though -hide was supplied
	if (hide && !keyhook) {
		hide = 0;
		UpdateTray();
	}
	
	//Check for update
	if (settings.CheckForUpdate) {
		CheckForUpdate();
	}
	
	//Message loop
	MSG msg;
	while (GetMessage(&msg,NULL,0,0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

void ShowContextMenu(HWND hwnd) {
	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu = CreatePopupMenu();
	
	//Toggle
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_TOGGLE, (keyhook?l10n->menu_disable:l10n->menu_enable));
	
	//Hide
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_HIDE, l10n->menu_hide);
	
	//Check autostart
	int autostart_enabled=0, autostart_hide=0;
	//Registry
	HKEY key;
	wchar_t autostart_value[MAX_PATH+10];
	DWORD len = sizeof(autostart_value);
	RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key);
	RegQueryValueEx(key, APP_NAME, NULL, NULL, (LPBYTE)autostart_value, &len);
	RegCloseKey(key);
	//Compare
	wchar_t path[MAX_PATH];
	GetModuleFileName(NULL, path, MAX_PATH);
	swprintf(txt, L"\"%s\"", path);
	if (!wcscmp(txt,autostart_value)) {
		autostart_enabled=1;
	}
	else {
		swprintf(txt, L"\"%s\" -hide", path);
		if (!wcscmp(txt,autostart_value)) {
			autostart_enabled = 1;
			autostart_hide = 1;
		}
	}
	//Autostart
	HMENU hAutostartMenu = CreatePopupMenu();
	InsertMenu(hAutostartMenu, -1, MF_BYPOSITION|(autostart_enabled?MF_CHECKED:0), (autostart_enabled?SWM_AUTOSTART_OFF:SWM_AUTOSTART_ON), l10n->menu_autostart);
	InsertMenu(hAutostartMenu, -1, MF_BYPOSITION|(autostart_hide?MF_CHECKED:0), (autostart_hide?SWM_AUTOSTART_HIDE_OFF:SWM_AUTOSTART_HIDE_ON), l10n->menu_hide);
	InsertMenu(hMenu, -1, MF_BYPOSITION|MF_POPUP, (UINT)hAutostartMenu, l10n->menu_autostart);
	InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	
	//Update
	if (update) {
		InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_UPDATE, l10n->menu_update);
		InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	}
	
	//About
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_ABOUT, l10n->menu_about);
	
	//Exit
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_EXIT, l10n->menu_exit);

	//Track menu
	SetForegroundWindow(hwnd);
	TrackPopupMenu(hMenu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(hMenu);
}

int UpdateTray() {
	wcsncpy(traydata.szTip, (keyhook?l10n->tray_enabled:l10n->tray_disabled), sizeof(traydata.szTip)/sizeof(wchar_t));
	traydata.hIcon = icon[keyhook?1:0];
	
	//Only add or modify if not hidden or if balloon will be displayed
	if (!hide || traydata.uFlags&NIF_INFO) {
		int tries = 0; //Try at least ten times, sleep 100 ms between each attempt
		while (Shell_NotifyIcon((tray_added?NIM_MODIFY:NIM_ADD),&traydata) == FALSE) {
			tries++;
			if (tries >= 10) {
				Error(L"Shell_NotifyIcon(NIM_ADD/NIM_MODIFY)", L"Failed to update tray icon.", GetLastError(), __LINE__);
				return 1;
			}
			Sleep(100);
		}
		
		//Success
		tray_added = 1;
	}
	return 0;
}

int RemoveTray() {
	if (!tray_added) {
		//Tray not added
		return 1;
	}
	
	if (Shell_NotifyIcon(NIM_DELETE,&traydata) == FALSE) {
		Error(L"Shell_NotifyIcon(NIM_DELETE)", L"Failed to remove tray icon.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Success
	tray_added=0;
	return 0;
}

void SetAutostart(int on, int hide) {
	//Open key
	HKEY key;
	int error = RegCreateKeyEx(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
	if (error != ERROR_SUCCESS) {
		Error(L"RegOpenKeyEx(HKEY_CURRENT_USER,'Software\\Microsoft\\Windows\\CurrentVersion\\Run')", L"Error opening the registry.", error, __LINE__);
		return;
	}
	if (on) {
		//Get path
		wchar_t path[MAX_PATH];
		if (GetModuleFileName(NULL,path,MAX_PATH) == 0) {
			Error(L"GetModuleFileName(NULL)", L"SetAutostart()", GetLastError(), __LINE__);
			return;
		}
		//Add
		wchar_t value[MAX_PATH+10];
		swprintf(value, (hide?L"\"%s\" -hide":L"\"%s\""), path);
		error = RegSetValueEx(key,APP_NAME,0,REG_SZ,(LPBYTE)value,(wcslen(value)+1)*sizeof(wchar_t));
		if (error != ERROR_SUCCESS) {
			Error(L"RegSetValueEx('"APP_NAME"')", L"SetAutostart()", error, __LINE__);
			return;
		}
	}
	else {
		//Remove
		error = RegDeleteValue(key,APP_NAME);
		if (error != ERROR_SUCCESS) {
			Error(L"RegDeleteValue('"APP_NAME"')", L"SetAutostart()", error, __LINE__);
			return;
		}
	}
	//Close key
	RegCloseKey(key);
}

//Hooks
void Kill(HWND hwnd) {
	//Get process id of hwnd
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	
	int SeDebugPrivilege = 0;
	//Get process token
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;
	if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken) == 0) {
		#ifdef DEBUG
		Error(L"OpenProcessToken()", L"Kill()", GetLastError(), __LINE__);
		#endif
	}
	else {
		//Get LUID for SeDebugPrivilege
		LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid);
		tkp.PrivilegeCount = 1;
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		
		//Enable SeDebugPrivilege
		if (AdjustTokenPrivileges(hToken,FALSE,&tkp,0,NULL,0) == 0 || GetLastError() != ERROR_SUCCESS) {
			#ifdef DEBUG
			Error(L"AdjustTokenPrivileges()", L"Kill()", GetLastError(), __LINE__);
			#endif
		}
		else {
			//Got it
			SeDebugPrivilege = 1;
		}
	}
	
	//Open the process
	HANDLE process = OpenProcess(PROCESS_TERMINATE,FALSE,pid);
	if (process == NULL) {
		#ifdef DEBUG
		Error(L"OpenProcess()", L"Kill()", GetLastError(), __LINE__);
		#endif
		return;
	}
	
	//Terminate process
	if (TerminateProcess(process,1) == 0) {
		#ifdef DEBUG
		Error(L"TerminateProcess()", L"Kill()", GetLastError(), __LINE__);
		#endif
		return;
	}
	
	//Disable SeDebugPrivilege
	if (SeDebugPrivilege) {
		tkp.Privileges[0].Attributes = 0;
		AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
	}
}

_declspec(dllexport) LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		int vkey = ((PKBDLLHOOKSTRUCT)lParam)->vkCode;
		
		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
			//Check for Ctrl+Alt+F4
			if (vkey == VK_LCONTROL) {
				ctrl = 1;
			}
			if (vkey == VK_LMENU) {
				alt = 1;
			}
			if (ctrl && alt && vkey == VK_F4) {
				//Double check that Ctrl and Alt are being pressed
				//This prevents a faulty kill if we didn't received the keyup for these keys
				if (!(GetAsyncKeyState(VK_LCONTROL)&0x8000)) {
					ctrl = 0;
				}
				else if (!(GetAsyncKeyState(VK_LMENU)&0x8000)) {
					alt = 0;
				}
				else {
					//Get hwnd of foreground window
					HWND hwnd = GetForegroundWindow();
					if (hwnd == NULL) {
						return CallNextHookEx(NULL, nCode, wParam, lParam);
					}
					
					//Kill it!
					Kill(hwnd);
					
					//Prevent this keypress from being propagated
					return 1;
				}
			}
			//Check for [the windows key]+F4
			if (vkey == VK_LWIN) {
				win = 1;
			}
			if (win && vkey == VK_F4) {
				//Double check that the windows button is being pressed
				if (!(GetAsyncKeyState(VK_LWIN)&0x8000)) {
					win = 0;
				}
				else {
					//Hook mouse
					HookMouse();
					//Prevent this keypress from being propagated
					return 1;
				}
			}
			if (vkey == VK_ESCAPE && mousehook) {
				//Unhook mouse
				UnhookMouse();
				//Prevent this keypress from being propagated
				return 1;
			}
		}
		else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
			if (vkey == VK_LCONTROL) {
				ctrl = 0;
			}
			if (vkey == VK_LMENU) {
				alt = 0;
			}
			if (vkey == VK_LWIN) {
				win = 0;
			}
		}
	}
	
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		if (wParam == WM_LBUTTONDOWN && superkill) {
			POINT pt = ((PMSLLHOOKSTRUCT)lParam)->pt;
			
			//Make sure cursorwnd isn't in the way
			ShowWindow(cursorwnd, SW_HIDE);
			SetWindowLongPtr(cursorwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW); //Workaround for http://support.microsoft.com/kb/270624/
			
			//Get hwnd
			HWND hwnd = WindowFromPoint(pt);
			if (hwnd == NULL) {
				#ifdef DEBUG
				Error(L"WindowFromPoint()", L"LowLevelMouseProc()", GetLastError(), __LINE__);
				#endif
				return CallNextHookEx(NULL, nCode, wParam, lParam);
			}
			hwnd = GetAncestor(hwnd,GA_ROOT);
			
			//Kill it!
			Kill(hwnd);
			
			//Unhook mouse
			UnhookMouse();
			
			//Prevent mousedown from propagating
			return 1;
		}
		else if (wParam == WM_RBUTTONDOWN) {
			//Disable mouse
			DisableMouse();
			//Prevent mousedown from propagating
			return 1;
		}
		else if (wParam == WM_RBUTTONUP) {
			//Unhook mouse
			UnhookMouse();
			//Prevent mouseup from propagating
			return 1;
		}
	}
	
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int HookMouse() {
	if (mousehook) {
		//Mouse already hooked
		return 1;
	}
	
	//Set up the mouse hook
	mousehook = SetWindowsHookEx(WH_MOUSE_LL,LowLevelMouseProc,hinstDLL,0);
	if (mousehook == NULL) {
		#ifdef DEBUG
		Error(L"SetWindowsHookEx(WH_MOUSE_LL)", L"HookMouse()", GetLastError(), __LINE__);
		#endif
		return 1;
	}
	
	//Show cursor
	int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	MoveWindow(cursorwnd, left, top, width, height, FALSE);
	SetWindowLongPtr(cursorwnd, GWL_EXSTYLE, WS_EX_LAYERED|WS_EX_TOOLWINDOW); //Workaround for http://support.microsoft.com/kb/270624/
	SetLayeredWindowAttributes(cursorwnd, 0, 1, LWA_ALPHA); //Almost transparent
	ShowWindowAsync(cursorwnd, SW_SHOWNA);
	
	//Success
	superkill = 1;
	return 0;
}

DWORD WINAPI DelayedUnhookMouse() {
	//Sleep so mouse events have time to be canceled
	Sleep(100);
	
	//Unhook the mouse hook
	if (UnhookWindowsHookEx(mousehook) == 0) {
		#ifdef DEBUG
		Error(L"UnhookWindowsHookEx(mousehook)", L"UnhookMouse()", GetLastError(), __LINE__);
		#endif
		return 1;
	}
	
	//Success
	mousehook = NULL;
}

int UnhookMouse() {
	if (!mousehook) {
		//Mouse not hooked
		return 1;
	}
	
	//Disable
	DisableMouse();
	
	//Unhook
	CreateThread(NULL, 0, DelayedUnhookMouse, NULL, 0, NULL);
	
	//Success
	return 0;
}

int DisableMouse() {
	//Disable
	superkill = 0;
	
	//Hide cursor
	ShowWindow(cursorwnd, SW_HIDE);
	SetWindowLongPtr(cursorwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW); //Workaround for http://support.microsoft.com/kb/270624/
}

int HookKeyboard() {
	if (keyhook) {
		//Keyboard already hooked
		return 1;
	}
	
	//Update settings
	SendMessage(traydata.hWnd, WM_UPDATESETTINGS, 0, 0);
	
	//Load library
	wchar_t path[MAX_PATH];
	GetModuleFileName(NULL, path, sizeof(path)/sizeof(wchar_t));
	hinstDLL = LoadLibrary(path);
	if (hinstDLL == NULL) {
		Error(L"LoadLibrary()", L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Get address to keyboard hook (beware name mangling)
	HOOKPROC procaddr = (HOOKPROC)GetProcAddress(hinstDLL,"LowLevelKeyboardProc@12");
	if (procaddr == NULL) {
		Error(L"GetProcAddress('LowLevelKeyboardProc@12')", L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Set up the hook
	keyhook = SetWindowsHookEx(WH_KEYBOARD_LL,procaddr,hinstDLL,0);
	if (keyhook == NULL) {
		Error(L"SetWindowsHookEx(WH_KEYBOARD_LL)", L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Success
	UpdateTray();
	return 0;
}

int UnhookKeyboard() {
	if (!keyhook) {
		//Keyboard not hooked
		return 1;
	}
	
	//Remove keyboard hook
	if (UnhookWindowsHookEx(keyhook) == 0) {
		Error(L"UnhookWindowsHookEx(keyhook)", L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Remove mouse hook (it probably isn't hooked, but just in case)
	UnhookMouse();
	
	//Unload library
	if (FreeLibrary(hinstDLL) == 0) {
		Error(L"FreeLibrary()", L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.", GetLastError(), __LINE__);
		return 1;
	}
	
	//Success
	keyhook = NULL;
	UpdateTray();
	return 0;
}

void ToggleState() {
	if (keyhook) {
		UnhookKeyboard();
	}
	else {
		HookKeyboard();
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_ICONTRAY) {
		if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONDBLCLK) {
			ToggleState();
		}
		else if (lParam == WM_RBUTTONDOWN) {
			ShowContextMenu(hwnd);
		}
		else if (lParam == NIN_BALLOONTIMEOUT) {
			if (hide) {
				RemoveTray();
			}
		}
		else if (lParam == NIN_BALLOONUSERCLICK) {
			hide = 0;
			SendMessage(hwnd, WM_COMMAND, SWM_UPDATE, 0);
		}
	}
	else if (msg == WM_UPDATESETTINGS) {
		//Load settings
		wchar_t path[MAX_PATH];
		GetModuleFileName(NULL, path, sizeof(path)/sizeof(wchar_t));
		PathRenameExtension(path, L".ini");
		//Language
		GetPrivateProfileString(APP_NAME, L"Language", L"en-US", txt, sizeof(txt)/sizeof(wchar_t), path);
		int i;
		for (i=0; i < num_languages; i++) {
			if (!wcscmp(txt,languages[i].code)) {
				l10n = languages[i].strings;
				break;
			}
		}
	}
	else if (msg == WM_ADDTRAY) {
		hide = 0;
		UpdateTray();
	}
	else if (msg == WM_HIDETRAY) {
		hide = 1;
		RemoveTray();
	}
	else if (msg == WM_TASKBARCREATED) {
		tray_added = 0;
		UpdateTray();
	}
	else if (msg == WM_COMMAND) {
		int wmId=LOWORD(wParam), wmEvent=HIWORD(wParam);
		if (wmId == SWM_TOGGLE) {
			ToggleState();
		}
		else if (wmId == SWM_HIDE) {
			hide = 1;
			RemoveTray();
		}
		else if (wmId == SWM_AUTOSTART_ON) {
			SetAutostart(1, 0);
		}
		else if (wmId == SWM_AUTOSTART_OFF) {
			SetAutostart(0, 0);
		}
		else if (wmId == SWM_AUTOSTART_HIDE_ON) {
			SetAutostart(1, 1);
		}
		else if (wmId == SWM_AUTOSTART_HIDE_OFF) {
			SetAutostart(1, 0);
		}
		else if (wmId == SWM_UPDATE) {
			if (MessageBox(NULL,l10n->update_dialog,APP_NAME,MB_ICONINFORMATION|MB_YESNO|MB_SYSTEMMODAL) == IDYES) {
				ShellExecute(NULL, L"open", APP_URL, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		else if (wmId == SWM_ABOUT) {
			MessageBox(NULL, l10n->about, l10n->about_title, MB_ICONINFORMATION|MB_OK);
		}
		else if (wmId == SWM_EXIT) {
			DestroyWindow(hwnd);
		}
	}
	else if (msg == WM_DESTROY) {
		showerror = 0;
		UnhookKeyboard();
		UnhookMouse();
		RemoveTray();
		PostQuitMessage(0);
		return 0;
	}
	else if (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN) {
		//Hide the window if clicked on, this might happen if it wasn't hidden by the hooks for some reason
		ShowWindow(hwnd, SW_HIDE);
		SetWindowLongPtr(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW); //Workaround for http://support.microsoft.com/kb/270624/
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}