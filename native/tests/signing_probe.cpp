#include <windows.h>
// Test-only marker proves signature rejection happens even before DllMain.
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"HA_SIGNATURE_PROBE_MARKER",path,32768);
        if(length && length<32768){
            HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        }
    }
    return TRUE;
}
extern "C" __declspec(dllexport) const void* __cdecl HA_Query(unsigned){return nullptr;}
