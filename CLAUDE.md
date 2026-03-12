# Switchy

Windows-утилита, переназначающая Caps Lock для переключения раскладки, транслитерации (QWERTY↔ЙЦУКЕН) и смены регистра. Весь код — один файл `Switchy/main.c` (~410 строк, чистый C, Windows API).

## Архитектура

- **Keyboard hook** (`WH_KEYBOARD_LL`) перехватывает Caps Lock и Shift глобально
- **IsTerminalWindow()** — определяет терминалы по имени класса окна и процессу; в терминалах clipboard-операции пропускаются (Ctrl+C = SIGINT)
- **TryTransformSelectedText()** — сохраняет clipboard → Ctrl+C → трансформация → Ctrl+V → восстанавливает clipboard
- **SwitchLayout()** — переключает раскладку через `WM_INPUTLANGCHANGEREQUEST`
- Один мьютекс `"Switchy"` не даёт запустить второй экземпляр

## Ключевые нюансы

- IDE (VS Code, JetBrains, Sublime) копируют всю строку по Ctrl+C без выделения — это детектится по `\n` в конце скопированного текста (см. "IDE line-copy detection" в `TryTransformSelectedText`)
- Список терминалов захардкожен в `TERMINAL_PROCESSES[]` — при добавлении нового терминала дописать туда
- `CLIPBOARD_DELAY_MS = 50` — задержка между clipboard-операциями; если трансформация глючит — можно увеличить
- `keybd_event()` с флагом `LLKHF_INJECTED` фильтруется хуком, чтобы не зациклиться на собственных нажатиях
- Release-сборка использует entry point `mainCRTStartup` и subsystem Windows (без консольного окна)
- Debug-сборка использует subsystem Console и выводит отладку через printf

## Сборка

### GCC (MSYS2 MinGW)

```bash
PATH="/c/msys64/mingw64/bin:$PATH" gcc -Wall -O2 -mwindows Switchy/main.c -o Switchy.exe -luser32
```

**Важно:** gcc нужен полный PATH к тулчейну (cc1, as, ld), иначе молча падает с exit code 1.

### Visual Studio

Открыть `Switchy.sln`, собрать Release|x64. Выход: `x64/Release/Switchy.exe`.

### CI

GitHub Actions (`build.yml`) — MSBuild на `windows-latest`, артефакт `x64/Release/Switchy.exe`.

## Файлы

```
Switchy/main.c          — весь код приложения
Switchy/Switchy.vcxproj — проект Visual Studio (v143, Win10 SDK)
Switchy.sln             — solution
.github/workflows/build.yml — CI
```
