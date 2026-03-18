#define UNICODE
#define _UNICODE
#define INITGUID

#include <windows.h>
#include <evntcons.h>
#include <evntrace.h>
#include <tdh.h>
#include <map>
#include <vector>
#include <algorithm>
#include <tlhelp32.h>
#include <string>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYICON (WM_USER + 1)

NOTIFYICONDATA nid;
std::map<DWORD, ULONG64> usage;

TRACEHANDLE sessionHandle = 0;
TRACEHANDLE traceHandle = 0;

// Lấy tên exe từ PID
std::string GetProcessName(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do {
            if (pe.th32ProcessID == pid)
            {
                CloseHandle(snap);

                // convert WCHAR -> string
                char name[260];
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, 260, NULL, NULL);

                return std::string(name);
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return "Unknown";
}

// Callback ETW
VOID WINAPI EventCallback(PEVENT_RECORD record)
{
    DWORD pid = record->EventHeader.ProcessId;

    ULONG size = 0;

    if (record->UserDataLength >= 4)
    {
        size = *(ULONG*)(record->UserData);
    }

    usage[pid] += size;
}

// Cập nhật tray
void UpdateTray()
{
    std::vector<std::pair<DWORD, ULONG64>> list(usage.begin(), usage.end());

    std::sort(list.begin(), list.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    std::string text = "Top Network:\n";

    int count = 0;
    for (auto& item : list)
    {
        if (count++ >= 5) break;

        std::string name = GetProcessName(item.first);

        char buffer[64];
        sprintf_s(buffer, "%llu", item.second / 1024);

        text += name + ": " + buffer + " KB\n";
    }

    // Convert sang wchar để gán vào tray
    wchar_t wtext[128];
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext, 128);

    wcsncpy_s(nid.szTip, wtext, _TRUNCATE);

    Shell_NotifyIcon(NIM_MODIFY, &nid);

    usage.clear();
}

// Start ETW
void StartETW()
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 1024;

    EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)malloc(size);
    ZeroMemory(props, size);

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;

    // Unicode chuẩn
    StartTraceW(&sessionHandle, L"MyNetSession", props);

    EnableTraceEx2(
        sessionHandle,
        &SystemTraceControlGuid,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION,
        0x10, // Network
        0,
        0,
        NULL
    );

    EVENT_TRACE_LOGFILEW trace = {};
    trace.LoggerName = (LPWSTR)L"MyNetSession";
    trace.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    trace.EventRecordCallback = EventCallback;

    traceHandle = OpenTraceW(&trace);

    CreateThread(NULL, 0, [](LPVOID)->DWORD {
        ProcessTrace(&traceHandle, 1, NULL, NULL);
        return 0;
    }, NULL, 0, NULL);
}

// Window proc
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        UpdateTray();
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Main
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"NetTray";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(L"NetTray", L"", 0, 0, 0, 0, 0, 0, 0, hInstance, 0);

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    Shell_NotifyIcon(NIM_ADD, &nid);

    SetTimer(hwnd, 1, 2000, NULL);

    StartETW();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
