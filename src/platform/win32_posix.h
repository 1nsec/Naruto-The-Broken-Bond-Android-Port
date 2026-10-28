// The subset of the Win32 API that the port's native renderer and PC features
// use, for platforms without Windows (Android). Included through
// platform/win32.h, never directly.
//
// The port was written for Windows. Rather than fork every file, this header
// gives the Win32 names a POSIX meaning where the port needs it to work
// (events, threads, timers, files, memory protection, window size, keyboard,
// embedded resources), and a harmless failing answer where the call only feeds
// Windows-specific diagnostics (minidumps, symbol lookup, thread sampling),
// which are simply unavailable on Android.
//
// Window handles (HWND) are SDL_Window pointers: rex::ui::Window's native
// handle on these platforms (see the SDK's window_sdl.cpp).
#pragma once

#ifdef _WIN32
#error "win32_posix.h is for platforms without Windows; include platform/win32.h"
#endif

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "platform/msvc_compat.h"

// ---------------------------------------------------------------------------
// Types.
// ---------------------------------------------------------------------------
typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;
typedef UCHAR* PUCHAR;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef short SHORT;
typedef uint32_t DWORD;
typedef DWORD* LPDWORD;
typedef DWORD* PDWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int INT;
typedef unsigned int UINT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t DWORD64;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef intptr_t LONG_PTR;
typedef size_t SIZE_T;
typedef intptr_t SSIZE_T;
typedef float FLOAT;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef wchar_t* PWSTR;
typedef const wchar_t* LPCWSTR;
typedef const wchar_t* PCWSTR;
typedef int32_t HRESULT;
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;
typedef ULONG_PTR* PULONG_PTR;
typedef ULONG* PULONG;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* HMONITOR;
typedef void* HRSRC;
typedef void* HGLOBAL;
typedef void* HDC;
typedef intptr_t LRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef LONG NTSTATUS;

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef struct tagRECT {
  LONG left, top, right, bottom;
} RECT, *LPRECT;

typedef struct tagPOINT {
  LONG x, y;
} POINT;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct _MEMORY_BASIC_INFORMATION {
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION;

typedef struct tagMONITORINFO {
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
} MONITORINFO;

typedef struct tagMONITORINFOEXW : tagMONITORINFO {
  WCHAR szDevice[32];
} MONITORINFOEXW;

typedef struct _devicemodeW {
  WCHAR dmDeviceName[32];
  WORD dmSize;
  DWORD dmFields;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  DWORD dmDisplayFrequency;
} DEVMODEW;

typedef struct _WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD nFileSizeHigh, nFileSizeLow;
  WCHAR cFileName[260];
} WIN32_FIND_DATAW;

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED OVERLAPPED, *LPOVERLAPPED;

// Exception records: only the shape, for code that is compiled but whose
// Windows exception handlers are never installed here.
typedef struct _EXCEPTION_RECORD {
  DWORD ExceptionCode;
  DWORD ExceptionFlags;
  struct _EXCEPTION_RECORD* ExceptionRecord;
  PVOID ExceptionAddress;
  DWORD NumberParameters;
  ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;
typedef struct _CONTEXT {
  DWORD ContextFlags;
  DWORD64 Rip, Rsp, Rbp, Pc, Sp, Fp, Lr;
} CONTEXT, *PCONTEXT;
typedef struct _EXCEPTION_POINTERS {
  EXCEPTION_RECORD* ExceptionRecord;
  CONTEXT* ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;
typedef LONG(WINAPI* PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS*);
typedef LONG(WINAPI* LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS*);

// ---------------------------------------------------------------------------
// Constants.
// ---------------------------------------------------------------------------
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0x00000000u
#define WAIT_ABANDONED 0x00000080u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(intptr_t(-1)))
#define S_OK HRESULT(0)
#define S_FALSE HRESULT(1)
#define E_FAIL HRESULT(0x80004005)
#define SUCCEEDED(hr) (HRESULT(hr) >= 0)
#define FAILED(hr) (HRESULT(hr) < 0)
#define ERROR_SUCCESS 0u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_SUPPORTED 50u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_TIMEOUT 1460u
#define ERROR_NOT_FOUND 1168u
#define ERROR_NO_MORE_FILES 18u

#define CP_ACP 0u
#define CP_UTF8 65001u

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define FILE_SHARE_DELETE 0x4u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_READONLY 0x1u
#define FILE_ATTRIBUTE_HIDDEN 0x2u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_ATTRIBUTE_TEMPORARY 0x100u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu

#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_EXECUTE_WRITECOPY 0x80u
#define PAGE_GUARD 0x100u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define MEM_PRIVATE 0x20000u
#define MEM_MAPPED 0x40000u

#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_GET_CONTEXT 0x0008u
#define THREAD_SUSPEND_RESUME 0x0002u
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800u
#define THREAD_ALL_ACCESS 0x1FFFFFu
#define DUPLICATE_SAME_ACCESS 0x2u
#define SYNCHRONIZE 0x00100000u
#define EVENT_MODIFY_STATE 0x0002u
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x1u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2u
#define TIMER_ALL_ACCESS 0x1F0003u
#define CREATE_EVENT_MANUAL_RESET 0x1u
#define CREATE_EVENT_INITIAL_SET 0x2u

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_BREAKPOINT 0x80000003u
#define STATUS_ACCESS_VIOLATION 0xC0000005u
#define CONTEXT_FULL 0x10000Bu
#define CONTEXT_CONTROL 0x100001u
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4u

#define MB_OK 0x0u
#define MB_OKCANCEL 0x1u
#define MB_YESNO 0x4u
#define MB_ICONERROR 0x10u
#define MB_ICONWARNING 0x30u
#define MB_ICONINFORMATION 0x40u
#define MB_TOPMOST 0x40000u
#define MB_SETFOREGROUND 0x10000u
#define IDOK 1
#define IDYES 6
#define IDNO 7

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SWP_NOSIZE 0x1u
#define SWP_NOMOVE 0x2u
#define SWP_NOZORDER 0x4u
#define SWP_NOACTIVATE 0x10u
#define SWP_SHOWWINDOW 0x40u
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define MONITOR_DEFAULTTONULL 0u
#define MONITOR_DEFAULTTOPRIMARY 1u
#define MONITOR_DEFAULTTONEAREST 2u
#define ENUM_CURRENT_SETTINGS (DWORD(-1))
#define WM_CLOSE 0x0010u
#define WM_QUIT 0x0012u

#define RT_RCDATA_ID 10
#define MAKEINTRESOURCEW(i) (reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(static_cast<WORD>(i))))
#define MAKEINTRESOURCEA(i) (reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(static_cast<WORD>(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#define RT_RCDATA MAKEINTRESOURCEW(RT_RCDATA_ID)
#define IS_INTRESOURCE(r) ((reinterpret_cast<uintptr_t>(r) >> 16) == 0)

#define SND_ASYNC 0x1u
#define SND_NODEFAULT 0x2u
#define SND_MEMORY 0x4u
#define SND_LOOP 0x8u
#define SND_FILENAME 0x20000u
#define TIMERR_NOERROR 0u

#define LOWORD(l) (WORD(DWORD_PTR(l) & 0xFFFF))
#define HIWORD(l) (WORD((DWORD_PTR(l) >> 16) & 0xFFFF))
#define MAKEWORD(a, b) (WORD((BYTE(a)) | (WORD(BYTE(b)) << 8)))
#define MAKELONG(a, b) (LONG((WORD(a)) | (DWORD(WORD(b)) << 16)))

// Virtual keys (the values of the Windows SDK).
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5

// ---------------------------------------------------------------------------
// Implemented in win32_posix.cpp.
// ---------------------------------------------------------------------------

// Text: UTF-16/UTF-32 (wchar_t is 32-bit here) <-> UTF-8.
int WideCharToMultiByte(UINT code_page, DWORD flags, LPCWSTR wide, int wide_count, LPSTR out, int out_size,
                        LPCSTR default_char, BOOL* used_default);
int MultiByteToWideChar(UINT code_page, DWORD flags, LPCSTR text, int count, LPWSTR out, int out_size);
namespace win32_posix {
std::string Utf8(const wchar_t* text);
std::wstring Wide(const char* text);
}  // namespace win32_posix

// Errors.
DWORD GetLastError();
void SetLastError(DWORD error);

// Processes, threads and handles. Pseudo handles: the current process is -1,
// the current thread -2, as on Windows.
HANDLE GetCurrentProcess();
HANDLE GetCurrentThread();
DWORD GetCurrentProcessId();
DWORD GetCurrentThreadId();
BOOL SetThreadPriority(HANDLE thread, int priority);
int GetThreadPriority(HANDLE thread);
HRESULT SetThreadDescription(HANDLE thread, PCWSTR name);
BOOL GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
DWORD GetActiveProcessorCount(WORD group);
#define ALL_PROCESSOR_GROUPS 0xFFFF
BOOL TerminateProcess(HANDLE process, UINT exit_code);
[[noreturn]] void ExitProcess(UINT exit_code);
void Sleep(DWORD milliseconds);
BOOL SwitchToThread();
BOOL CloseHandle(HANDLE handle);
BOOL DuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, LPHANDLE target, DWORD access,
                     BOOL inherit, DWORD options);
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD thread_id);
DWORD SuspendThread(HANDLE thread);
DWORD ResumeThread(HANDLE thread);
BOOL GetThreadContext(HANDLE thread, CONTEXT* context);
void GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high);

// Events and waitable timers (handles waited with WaitForSingleObject).
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset, BOOL initial_state, LPCWSTR name);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset, BOOL initial_state, LPCSTR name);
HANDLE CreateEventExW(LPSECURITY_ATTRIBUTES attributes, LPCWSTR name, DWORD flags, DWORD access);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL wait_all, DWORD milliseconds);
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES attributes, LPCWSTR name, DWORD flags, DWORD access);
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset, LPCWSTR name);
typedef void(CALLBACK* PTIMERAPCROUTINE)(LPVOID, DWORD, DWORD);
typedef struct _REASON_CONTEXT REASON_CONTEXT, *PREASON_CONTEXT;
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due_time, LONG period, PTIMERAPCROUTINE completion,
                      LPVOID argument, BOOL resume);
BOOL SetWaitableTimerEx(HANDLE timer, const LARGE_INTEGER* due_time, LONG period, PTIMERAPCROUTINE completion,
                        LPVOID argument, PREASON_CONTEXT wake_context, ULONG tolerable_delay);
BOOL CancelWaitableTimer(HANDLE timer);

// Time.
BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
ULONGLONG GetTickCount64();
DWORD GetTickCount();
DWORD timeGetTime();
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);
void GetSystemTimeAsFileTime(LPFILETIME time);

// Memory.
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect);
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD free_type);
BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect);
SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
BOOL ReadProcessMemory(HANDLE process, LPCVOID base, LPVOID buffer, SIZE_T size, SIZE_T* read);
BOOL WriteProcessMemory(HANDLE process, LPVOID base, LPCVOID buffer, SIZE_T size, SIZE_T* written);
BOOL FlushInstructionCache(HANDLE process, LPCVOID base, SIZE_T size);

// Files.
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file);
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                   DWORD flags, HANDLE template_file);
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped);
BOOL FlushFileBuffers(HANDLE file);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size);
BOOL DeleteFileW(LPCWSTR path);
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES attributes);
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetTempPathW(DWORD size, LPWSTR buffer);
UINT GetTempFileNameW(LPCWSTR path, LPCWSTR prefix, UINT unique, LPWSTR out);
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW* data);
BOOL FindNextFileW(HANDLE find, WIN32_FIND_DATAW* data);
BOOL FindClose(HANDLE find);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
// std::filesystem::path::c_str() is narrow (UTF-8) here: the "W" file
// functions take it too.
inline HANDLE CreateFileW(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes,
                          DWORD disposition, DWORD flags, HANDLE template_file) {
  return CreateFileA(path, access, share, attributes, disposition, flags, template_file);
}
inline BOOL DeleteFileW(const char* path) { return ::unlink(path) == 0; }
inline FILE* _wfopen(const char* path, const wchar_t* mode) {
  return std::fopen(path, win32_posix::Utf8(mode).c_str());
}
int _wputenv_s(const wchar_t* name, const wchar_t* value);
int _putenv_s(const char* name, const char* value);
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);

// Modules: the port's own library, and dlopen for the rest.
HMODULE GetModuleHandleW(LPCWSTR name);
HMODULE GetModuleHandleA(LPCSTR name);
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* module);
DWORD GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size);
DWORD GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
HMODULE LoadLibraryW(LPCWSTR name);
HMODULE LoadLibraryA(LPCSTR name);
BOOL FreeLibrary(HMODULE module);
typedef intptr_t(WINAPI* FARPROC)();
FARPROC GetProcAddress(HMODULE module, LPCSTR name);

// Resources built into the executable on Windows (RCDATA); here a table of
// embedded files (resources_posix.cpp, generated by CMake).
HRSRC FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type);
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL data);
DWORD SizeofResource(HMODULE module, HRSRC resource);

// Windows (SDL_Window pointers) and the screen.
BOOL IsWindow(HWND window);
BOOL IsIconic(HWND window);
BOOL IsWindowVisible(HWND window);
BOOL GetClientRect(HWND window, LPRECT rect);
BOOL GetWindowRect(HWND window, LPRECT rect);
BOOL SetWindowPos(HWND window, HWND insert_after, int x, int y, int cx, int cy, UINT flags);
BOOL ShowWindow(HWND window, int command);
BOOL SetForegroundWindow(HWND window);
HWND GetForegroundWindow();
DWORD GetWindowThreadProcessId(HWND window, LPDWORD process_id);
BOOL SetWindowTextW(HWND window, LPCWSTR text);
BOOL PostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
UINT GetDpiForWindow(HWND window);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);
BOOL EnumDisplaySettingsW(LPCWSTR device, DWORD mode, DEVMODEW* devmode);
int GetSystemMetrics(int index);
typedef void* DPI_AWARENESS_CONTEXT;
typedef enum DPI_AWARENESS { DPI_AWARENESS_INVALID = -1, DPI_AWARENESS_UNAWARE = 0,
                             DPI_AWARENESS_SYSTEM_AWARE = 1, DPI_AWARENESS_PER_MONITOR_AWARE = 2 } DPI_AWARENESS;
DPI_AWARENESS_CONTEXT GetWindowDpiAwarenessContext(HWND window);
DPI_AWARENESS GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);
int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);
int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
void OutputDebugStringA(LPCSTR text);
void OutputDebugStringW(LPCWSTR text);

// Keyboard state (SDL's keyboard; the high bit is set while the key is down).
SHORT GetAsyncKeyState(int virtual_key);
SHORT GetKeyState(int virtual_key);

// Sound (the title screen's optional local extra): not available here.
BOOL PlaySoundW(LPCWSTR sound, HMODULE module, DWORD flags);
typedef DWORD MCIERROR;
inline MCIERROR mciSendStringW(LPCWSTR, LPWSTR, UINT, HWND) { return 1; }  // MCI is Windows-only.

// Windows exception handling: never installed here (the SDK and the port's
// POSIX crash handler use signals), so these only report failure.
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
ULONG RemoveVectoredExceptionHandler(PVOID handle);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);
USHORT RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID* trace, PULONG hash);
