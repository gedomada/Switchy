# Switchy

Lightweight Windows utility that repurposes the Caps Lock key for keyboard layout switching, text transliteration, and case toggling. Zero dependencies, single C file, ~300 lines.

## Features

| Shortcut | With selected text | Without selected text |
|---|---|---|
| **CapsLock** | Transliterate selection (EN↔RU, QWERTY↔ЙЦУКЕН) | Switch keyboard layout |
| **Shift+CapsLock** | Toggle case (a↔A, б↔Б) | Toggle CapsLock state |

### Transliteration

Select text typed in the wrong layout and press **CapsLock** — it will be converted in-place:

- `ghbdtn` → `привет`
- `руддщ` → `hello`

Full QWERTY↔ЙЦУКЕН mapping including punctuation: `[]` ↔ `хъ`, `;'` ↔ `жэ`, `,.` ↔ `бю`, `` ` `` ↔ `ё`, etc.

### Case toggle

Select text and press **Shift+CapsLock** to flip case:

- `Hello World` → `hELLO wORLD`
- `ПРИВЕТ` → `привет`

Works with both Latin and Cyrillic characters.

## Installation

1. Download [Switchy.exe](https://github.com/erryox/Switchy/releases/latest)
2. Place it in the startup folder (press **Win+R**, type `shell:startup`)

To hide the Windows 10/11 language popup, create a shortcut with `nopopup` parameter instead.

## Running with administrator privileges

For Switchy to work in programs running as administrator (e.g. elevated Command Prompt, Task Manager), it must also run with admin privileges. The easiest way is via **Task Scheduler**:

### Autostart via Task Scheduler (recommended)

1. Copy `Switchy.exe` to a permanent location, e.g. `C:\Switchy\Switchy.exe`
2. Open **Command Prompt as Administrator** and run:
   ```
   schtasks /Create /TN "Switchy" /TR "C:\Switchy\Switchy.exe" /SC ONLOGON /RL HIGHEST /F
   ```
3. Done — Switchy will start automatically at logon with admin privileges

To remove:
```
schtasks /Delete /TN "Switchy" /F
```

## Building from source

### Prerequisites

- [MSYS2](https://www.msys2.org/) with MinGW-w64 GCC, or
- Visual Studio 2022 with C/C++ workload

### Build with GCC (MSYS2)

```bash
gcc -Wall -O2 -mwindows Switchy/main.c -o Switchy.exe -luser32
```

### Build with Visual Studio

Open `Switchy.sln` and build the Release|x64 configuration.

## How it works

Switchy installs a low-level keyboard hook (`WH_KEYBOARD_LL`) that intercepts Caps Lock and Shift key events before they reach any application. When Caps Lock is pressed:

1. **No text selected** — switches to the next keyboard layout via `WM_INPUTLANGCHANGEREQUEST`
2. **Text is selected** — saves the clipboard, copies selection via simulated Ctrl+C, applies the transformation (transliteration or case toggle), pastes via Ctrl+V, then restores the original clipboard

The transliteration uses a bidirectional character mapping table that covers all standard QWERTY↔ЙЦУКЕН positions. A single mutex prevents multiple instances from running.

## License

[MIT](LICENSE) — originally by Max Ignatiev, extended fork.
