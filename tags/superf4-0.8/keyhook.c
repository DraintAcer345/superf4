/*
	SuperF4 - Force kill programs with Ctrl+Alt+F4
	Copyright (C) 2008  Stefan Sundin (recover89@gmail.com)
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

static int ctrl=0;
static int alt=0;
static char msg[100];
static FILE *output;

BOOL SetPrivilege(HANDLE hToken, LPCTSTR priv, BOOL bEnablePrivilege) {
	TOKEN_PRIVILEGES tp;
	LUID luid;
	TOKEN_PRIVILEGES tpPrevious;
	DWORD cbPrevious=sizeof(TOKEN_PRIVILEGES);

	if (!LookupPrivilegeValue(NULL, priv, &luid)) {
		return FALSE;
	}
	
	//Get current privileges
	tp.PrivilegeCount=1;
	tp.Privileges[0].Luid=luid;
	tp.Privileges[0].Attributes=0;

	if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &tpPrevious, &cbPrevious) != 0 && GetLastError() != ERROR_SUCCESS) {
		return FALSE;
	}

	//Set privileges
	tpPrevious.PrivilegeCount=1;
	tpPrevious.Privileges[0].Luid=luid;

	if(bEnablePrivilege) {
		tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
	}
	else {
		tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & tpPrevious.Privileges[0].Attributes);
	}

	if (AdjustTokenPrivileges(hToken, FALSE, &tpPrevious, cbPrevious, NULL, NULL) != 0 && GetLastError() != ERROR_SUCCESS) {
		return FALSE;
	}

	return TRUE;
}

char* GetTimestamp(char *buf, size_t maxsize, char *format) {
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo=localtime(&rawtime);
	strftime(buf,maxsize,format,timeinfo);
	return buf;
}

_declspec(dllexport) LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		int vkey=((PKBDLLHOOKSTRUCT)lParam)->vkCode;
		
		//Check if Ctrl/Alt/F4 is being pressed or released
		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
			if (vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL) {
				ctrl=1;
			}
			if (vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU) {
				alt=1;
			}
			if (ctrl && alt && vkey == VK_F4) {
				//Double check that Ctrl and Alt are pressed
				//This prevents a faulty kill if keyhook haven't received the keyup for these keys
				if (!(GetAsyncKeyState(VK_CONTROL)&0x8000)) {
					ctrl=0;
				}
				else if (!(GetAsyncKeyState(VK_MENU)&0x8000)) {
					alt=0;
				}
				else {
					//Kill program
					fprintf(output,"%s ",GetTimestamp(msg,sizeof(msg),"[%Y-%m-%d %H:%M:%S]"));
					
					//Get hwnd of foreground window
					HWND hwnd;
					if ((hwnd=GetForegroundWindow()) == NULL) {
						fprintf(output,"Error: GetForegroundWindow() failed in file %s, line %d.",__FILE__,__LINE__);
						fflush(output);
						return 0;
					}
					
					//Get hwnd title (for log)
					char title[100];
					GetWindowText(hwnd,title,100);
					
					//Get process id of hwnd
					DWORD pid;
					GetWindowThreadProcessId(hwnd,&pid);
					
					fprintf(output,"Killing \"%s\" (pid %d)... ",title,pid);
					
					//Open the process
					HANDLE process;
					if ((process=OpenProcess(PROCESS_TERMINATE,FALSE,pid)) == NULL) {
						fprintf(output,"failed!\n");
						fprintf(output,"Error: OpenProcess() failed (error: %d) in file %s, line %d.\n",GetLastError(),__FILE__,__LINE__);
						fflush(output);
						return 0;
					}
					
					//Terminate process
					if (TerminateProcess(process,1) == 0) {
						fprintf(output,"failed!\n");
						fprintf(output,"Error: TerminateProcess() failed (error: %d) in file %s, line %d.\n",GetLastError(),__FILE__,__LINE__);
						fflush(output);
						return 0;
					}
					
					fprintf(output,"success!\n");
					fflush(output);

					//Prevent this keypress from being propagated to the window selected after the kill
					return 1;
				}
			}
		}
		else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
			if (vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL) {
				ctrl=0;
			}
			if (vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU) {
				alt=0;
			}
		}
	}
	
    return CallNextHookEx(NULL, nCode, wParam, lParam); 
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		//Open log
		output=fopen("log-keyhook.txt","ab");
		fprintf(output,"\n%s ",GetTimestamp(msg,sizeof(msg),"[%Y-%m-%d %H:%M:%S]"));
		fprintf(output,"New session. Getting SeDebugPrivilege privilege... ");
		
		//Create security context
		if (ImpersonateSelf(SecurityImpersonation) == 0) {
			fprintf(output,"failed!\n");
			fprintf(output,"Error: ImpersonateSelf() failed (error: %d) in file %s, line %d.\n",GetLastError(),__FILE__,__LINE__);
			fflush(output);
			return TRUE;
		}
		//Get access token
		HANDLE hToken;
		if (OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken) == 0) {
			fprintf(output,"failed!\n");
			fprintf(output,"Error: OpenThreadToken() failed (error: %d) in file %s, line %d.\n",GetLastError(),__FILE__,__LINE__);
			fflush(output);
			return TRUE;
		}
		//Enable SeDebugPrivilege
		if (SetPrivilege(hToken, SE_DEBUG_NAME, TRUE) == FALSE) {
			fprintf(output,"failed!\n");
			fprintf(output,"Error: SetPrivilege() failed (error: %d) in file %s, line %d.\n",GetLastError(),__FILE__,__LINE__);
			fflush(output);
			CloseHandle(hToken);
			return TRUE;
		}
		CloseHandle(hToken);
		fprintf(output,"success!\n");
		fflush(output);
	}
	else if (reason == DLL_PROCESS_ATTACH) {
		//Close log
		fclose(output);
	}

	return TRUE;
}