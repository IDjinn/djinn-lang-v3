#include "runtime_internal.h"


int64_t __djinn_console_write(const char* str, void* coro)
{
    if (!str) return 0;
    const int64_t len = (int64_t)strlen(str);
    if (len <= 0) return 0;
    return __djinn_async_write(1, (void*)str, len, coro);
}

int64_t __djinn_console_error(const char* str, void* coro)
{
    if (!str) return 0;
    const int64_t len = (int64_t)strlen(str);
    if (len <= 0) return 0;
    return __djinn_async_write(2, (void*)str, len, coro);
}

int64_t __djinn_console_read_line(char* buf, int64_t max, void* coro)
{
    if (!buf || max <= 0) return 0;
    return __djinn_async_read(0, buf, max, coro);
}

int32_t __djinn_terminal_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
#endif
}

int32_t __djinn_terminal_height(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 24;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
        return w.ws_row;
    return 24;
#endif
}

#ifdef _WIN32
static DWORD __djinn_original_console_mode = 0;
static int __djinn_raw_saved = 0;
#else
static struct termios __djinn_original_termios;
static int __djinn_raw_saved = 0;
#endif

void __djinn_raw_enable(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(h, &__djinn_original_console_mode);
    __djinn_raw_saved = 1;
    DWORD mode = __djinn_original_console_mode;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(h, mode);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode;
    GetConsoleMode(out, &outMode);
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(out, outMode);
#else
    tcgetattr(STDIN_FILENO, &__djinn_original_termios);
    __djinn_raw_saved = 1;
    struct termios raw = __djinn_original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
}

void __djinn_raw_disable(void)
{
    if (!__djinn_raw_saved) return;
#ifdef _WIN32
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), __djinn_original_console_mode);
#else
    tcflush(STDIN_FILENO, TCIFLUSH);
    tcsetattr(STDIN_FILENO, TCSANOW, &__djinn_original_termios);
#endif
    __djinn_raw_saved = 0;
}

static int32_t djinn_open_line = 0;

int32_t __djinn_console_open_line(int32_t set)
{
    const int32_t previous = djinn_open_line;
    if (set >= 0) djinn_open_line = set;
    return previous;
}

int32_t __djinn_read_line(char* buf, int32_t maxLen)
{
    if (!buf || maxLen <= 0) return 0;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    DWORD bytesRead = 0;
    ReadConsoleA(h, buf, (DWORD)(maxLen - 1), &bytesRead, NULL);
    SetConsoleMode(h, mode);
    buf[bytesRead] = '\0';
    int32_t len = (int32_t)bytesRead;
#else
    if (!fgets(buf, maxLen, stdin)) return 0;
    int32_t len = (int32_t)strlen(buf);
#endif
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    {
        buf[len - 1] = '\0';
        len--;
    }
    return len;
}

int32_t __djinn_read_key(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD rec;
    DWORD count;
    while (1)
    {
        if (!ReadConsoleInputA(h, &rec, 1, &count) || count == 0) continue;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
        char ch = rec.Event.KeyEvent.uChar.AsciiChar;
        DJINN_TRACE("read_key: eventType=%d vk=0x%04X ch=%d('%c') scan=0x%04X",
                    rec.EventType, vk, (int)ch, (ch >= 32 && ch < 127) ? ch : '?',
                    rec.Event.KeyEvent.wVirtualScanCode);
        if (vk == 0x26)
        {
            DJINN_TRACE("read_key: -> UP (-1)");
            return -1;
        }
        if (vk == 0x28)
        {
            DJINN_TRACE("read_key: -> DOWN (-2)");
            return -2;
        }
        if (vk == 0x25)
        {
            DJINN_TRACE("read_key: -> LEFT");
            continue;
        }
        if (vk == 0x27)
        {
            DJINN_TRACE("read_key: -> RIGHT");
            continue;
        }
        if (vk == 0x0D || ch == 13 || ch == 10)
        {
            DJINN_TRACE("read_key: -> ENTER (-3)");
            return -3;
        }
        if (vk == 0x1B || ch == 27)
        {
            DJINN_TRACE("read_key: -> ESCAPE (-5)");
            return -5;
        }
        if (vk == 0x20 || ch == 32)
        {
            DJINN_TRACE("read_key: -> SPACE (-4)");
            return -4;
        }
        if (ch == 3)
        {
            DJINN_TRACE("read_key: -> CTRL+C");
            __djinn_raw_disable();
            exit(1);
        }
        if (ch != 0)
        {
            DJINN_TRACE("read_key: -> char %d", (int)ch);
            return (int32_t)ch;
        }
        DJINN_TRACE("read_key: -> ignored (ch=0, vk=0x%04X)", vk);
    }
#else
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -5;
    if (c == 27)
    {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return -5;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return -5;
        if (seq[0] == '[')
        {
            if (seq[1] == 'A') return -1;
            if (seq[1] == 'B') return -2;
        }
        return -5;
    }
    if (c == 3) { __djinn_raw_disable(); exit(1); }
    if (c == '\r' || c == '\n') return -3;
    if (c == ' ') return -4;
    return (int32_t)c;
#endif
}

