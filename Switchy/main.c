#include <Windows.h>
#include <wctype.h>
#include <wchar.h>
#include <stdlib.h>
#if _DEBUG
#include <stdio.h>
#endif

#define CLIPBOARD_DELAY_MS 50

// Known terminal process names (lowercase for comparison)
static const wchar_t* TERMINAL_PROCESSES[] = {
	L"cmd.exe",
	L"powershell.exe",
	L"pwsh.exe",
	L"windowsterminal.exe",
	L"tabby.exe",
	L"mintty.exe",
	L"conemu.exe",
	L"conemu64.exe",
	L"conhost.exe",
	L"alacritty.exe",
	L"wezterm-gui.exe",
	L"hyper.exe",
	L"terminus.exe",
	L"kitty.exe",
	L"wt.exe",
	NULL
};

// Bidirectional transliteration mapping (QWERTY <-> JCUKEN)
static const wchar_t MAP_LATIN[] =    L"qwertyuiop[]asdfghjkl;'zxcvbnm,.`QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>~";
static const wchar_t MAP_CYRILLIC[] = L"\x0439\x0446\x0443\x043A\x0435\x043D\x0433\x0448\x0449\x0437\x0445\x044A\x0444\x044B\x0432\x0430\x043F\x0440\x043E\x043B\x0434\x0436\x044D\x044F\x0447\x0441\x043C\x0438\x0442\x044C\x0431\x044E\x0451\x0419\x0426\x0423\x041A\x0415\x041D\x0413\x0428\x0429\x0417\x0425\x042A\x0424\x042B\x0412\x0410\x041F\x0420\x041E\x041B\x0414\x0416\x042D\x042F\x0427\x0421\x041C\x0418\x0422\x042C\x0411\x042E\x0401";
static const int MAP_LEN = sizeof(MAP_LATIN) / sizeof(wchar_t) - 1;


void ShowError(LPCSTR message);
BOOL IsTerminalWindow(HWND hwnd);
void PressKey(int keyCode);
void ReleaseKey(int keyCode);
void ToggleCapsLockState();
void SwitchLayout();
wchar_t TransliterateChar(wchar_t ch);
void TransliterateText(wchar_t* text);
void ToggleCaseText(wchar_t* text);
BOOL TryTransformSelectedText(void (*transformFn)(wchar_t*), BOOL shiftHeld);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);


HHOOK hHook;
BOOL keystrokeCapsProcessed = FALSE;
BOOL keystrokeShiftProcessed = FALSE;


int main(int argc, char** argv)
{
	HANDLE hMutex = CreateMutex(0, 0, "Switchy");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		ShowError("Another instance of Switchy is already running!");
		return 1;
	}

	hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
	if (hHook == NULL)
	{
		ShowError("Error calling \"SetWindowsHookEx(...)\"");
		return 1;
	}

	MSG messages;
	while (GetMessage(&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}

	UnhookWindowsHookEx(hHook);
	return 0;
}


void ShowError(LPCSTR message)
{
	MessageBox(NULL, message, "Error", MB_OK | MB_ICONERROR);
}


BOOL IsTerminalWindow(HWND hwnd)
{
	// Check window class for native console windows
	char className[256];
	if (GetClassNameA(hwnd, className, sizeof(className)))
	{
		if (strcmp(className, "ConsoleWindowClass") == 0 ||
			strcmp(className, "CASCADIA_HOSTING_WINDOW_CLASS") == 0)
			return TRUE;
	}

	// Check process name for third-party terminals (Tabby, Alacritty, etc.)
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!hProcess) return FALSE;

	wchar_t exePath[MAX_PATH];
	DWORD size = MAX_PATH;
	BOOL result = FALSE;

	if (QueryFullProcessImageNameW(hProcess, 0, exePath, &size))
	{
		wchar_t* filename = wcsrchr(exePath, L'\\');
		filename = filename ? filename + 1 : exePath;

		wchar_t lower[MAX_PATH];
		for (int i = 0; filename[i] && i < MAX_PATH - 1; i++)
		{
			lower[i] = towlower(filename[i]);
			lower[i + 1] = L'\0';
		}

		for (int i = 0; TERMINAL_PROCESSES[i]; i++)
		{
			if (wcscmp(lower, TERMINAL_PROCESSES[i]) == 0)
			{
				result = TRUE;
				break;
			}
		}
	}

	CloseHandle(hProcess);
	return result;
}


void PressKey(int keyCode)
{
	keybd_event(keyCode, 0, 0, 0);
}


void ReleaseKey(int keyCode)
{
	keybd_event(keyCode, 0, KEYEVENTF_KEYUP, 0);
}


void ToggleCapsLockState()
{
	PressKey(VK_CAPITAL);
	ReleaseKey(VK_CAPITAL);
#if _DEBUG
	printf("Caps Lock state has been toggled\n");
#endif
}


wchar_t TransliterateChar(wchar_t ch)
{
	for (int i = 0; i < MAP_LEN; i++)
	{
		if (MAP_LATIN[i] == ch) return MAP_CYRILLIC[i];
		if (MAP_CYRILLIC[i] == ch) return MAP_LATIN[i];
	}
	return ch;
}


void TransliterateText(wchar_t* text)
{
	for (; *text; text++)
		*text = TransliterateChar(*text);
}


void ToggleCaseText(wchar_t* text)
{
	for (; *text; text++)
	{
		wchar_t upper = *text, lower = *text;
		CharUpperBuffW(&upper, 1);
		CharLowerBuffW(&lower, 1);
		if (*text == upper && *text != lower)
			*text = lower;
		else if (*text == lower && *text != upper)
			*text = upper;
	}
}


BOOL TryTransformSelectedText(void (*transformFn)(wchar_t*), BOOL shiftHeld)
{
	// Save current clipboard content
	HGLOBAL savedClip = NULL;
	if (OpenClipboard(NULL))
	{
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData)
		{
			wchar_t* src = (wchar_t*)GlobalLock(hData);
			if (src)
			{
				size_t len = wcslen(src) + 1;
				savedClip = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
				if (savedClip)
				{
					wchar_t* dst = (wchar_t*)GlobalLock(savedClip);
					if (dst)
					{
						memcpy(dst, src, len * sizeof(wchar_t));
						GlobalUnlock(savedClip);
					}
				}
				GlobalUnlock(hData);
			}
		}
		EmptyClipboard();
		CloseClipboard();
	}

	// Release Shift if held (avoid Ctrl+Shift+C)
	if (shiftHeld)
		ReleaseKey(VK_LSHIFT);

	// Simulate Ctrl+C
	PressKey(VK_CONTROL);
	PressKey('C');
	ReleaseKey('C');
	ReleaseKey(VK_CONTROL);
	Sleep(CLIPBOARD_DELAY_MS);

	// Check if clipboard now has text (i.e., text was selected)
	wchar_t* selectedText = NULL;
	if (OpenClipboard(NULL))
	{
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData)
		{
			wchar_t* src = (wchar_t*)GlobalLock(hData);
			if (src && wcslen(src) > 0)
			{
				size_t len = wcslen(src) + 1;
				selectedText = (wchar_t*)malloc(len * sizeof(wchar_t));
				if (selectedText)
					memcpy(selectedText, src, len * sizeof(wchar_t));
			}
			if (src) GlobalUnlock(hData);
		}
		CloseClipboard();
	}

	// Detect IDE line-copy behavior: editors like VS Code copy the entire
	// current line (ending with \r\n) when Ctrl+C is pressed with no selection.
	// Treat this as "no selection" to avoid inserting transformed junk.
	if (selectedText)
	{
		size_t len = wcslen(selectedText);
		if (len > 0 && selectedText[len - 1] == L'\n')
		{
			free(selectedText);
			selectedText = NULL;
		}
	}

	if (!selectedText)
	{
		// No text was selected - restore clipboard and return FALSE
		if (OpenClipboard(NULL))
		{
			EmptyClipboard();
			if (savedClip)
				SetClipboardData(CF_UNICODETEXT, savedClip);
			CloseClipboard();
		}
		else if (savedClip)
		{
			GlobalFree(savedClip);
		}
		if (shiftHeld)
			PressKey(VK_LSHIFT);
		return FALSE;
	}

	// Transform the text
	transformFn(selectedText);

	// Put transformed text on clipboard
	if (OpenClipboard(NULL))
	{
		EmptyClipboard();
		size_t len = wcslen(selectedText) + 1;
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
		if (hMem)
		{
			wchar_t* dst = (wchar_t*)GlobalLock(hMem);
			if (dst)
			{
				memcpy(dst, selectedText, len * sizeof(wchar_t));
				GlobalUnlock(hMem);
				SetClipboardData(CF_UNICODETEXT, hMem);
			}
		}
		CloseClipboard();
	}
	free(selectedText);

	// Simulate Ctrl+V
	PressKey(VK_CONTROL);
	PressKey('V');
	ReleaseKey('V');
	ReleaseKey(VK_CONTROL);
	Sleep(CLIPBOARD_DELAY_MS);

	// Restore original clipboard
	if (OpenClipboard(NULL))
	{
		EmptyClipboard();
		if (savedClip)
			SetClipboardData(CF_UNICODETEXT, savedClip);
		CloseClipboard();
	}
	else if (savedClip)
	{
		GlobalFree(savedClip);
	}

	// Re-press Shift if it was held
	if (shiftHeld)
		PressKey(VK_LSHIFT);

#if _DEBUG
	printf("Text has been transformed\n");
#endif
	return TRUE;
}


void SwitchLayout()
{
	HWND hwnd = GetForegroundWindow();
	DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
	HKL currentLayout = GetKeyboardLayout(threadId);

	HKL layouts[16];
	int count = GetKeyboardLayoutList(16, layouts);
	if (count <= 1) return;

	HKL nextLayout = layouts[0];
	for (int i = 0; i < count; i++)
	{
		if (layouts[i] == currentLayout)
		{
			nextLayout = layouts[(i + 1) % count];
			break;
		}
	}

	PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)nextLayout);
#if _DEBUG
	printf("Keyboard layout has been switched\n");
#endif
}


LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;

	if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED))
	{
#if _DEBUG
		const char* keyStatus = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? "pressed" : "released";
		printf("Key %d has been %s\n", key->vkCode, keyStatus);
#endif

		if (key->vkCode == VK_CAPITAL)
		{
			if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !keystrokeCapsProcessed)
			{
				keystrokeCapsProcessed = TRUE;
				HWND hwnd = GetForegroundWindow();
				BOOL isTerminal = IsTerminalWindow(hwnd);

				if (keystrokeShiftProcessed)
				{
					if (isTerminal || !TryTransformSelectedText(ToggleCaseText, TRUE))
						ToggleCapsLockState();
				}
				else
				{
					if (isTerminal || !TryTransformSelectedText(TransliterateText, FALSE))
						SwitchLayout();
				}
			}

			if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
			{
				keystrokeCapsProcessed = FALSE;
			}

			return 1;
		}

		else if (key->vkCode == VK_LSHIFT)
		{
			if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !keystrokeShiftProcessed)
			{
				keystrokeShiftProcessed = TRUE;
			}

			if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP))
			{
				keystrokeShiftProcessed = FALSE;
			}

			return 0;
		}
	}

	return CallNextHookEx(hHook, nCode, wParam, lParam);
}
