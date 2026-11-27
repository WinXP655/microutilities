#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <sddl.h>

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

BOOL EnablePrivilege(LPCSTR privilegeName) {
	HANDLE hToken;
	LUID luid;
	TOKEN_PRIVILEGES tp;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return FALSE;
	if (!LookupPrivilegeValueA(NULL, privilegeName, &luid)) { CloseHandle(hToken); return FALSE; }
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
	CloseHandle(hToken);
	return result && (GetLastError() == ERROR_SUCCESS);
}

DWORD GetPidByName(const char* processName) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) return 0;
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

HANDLE GetProcessToken(DWORD pid, DWORD desiredAccess) {
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (!hProcess) return NULL;
	HANDLE hToken = NULL;
	if (!OpenProcessToken(hProcess, desiredAccess, &hToken)) {
		CloseHandle(hProcess);
		return NULL;
	}
	CloseHandle(hProcess);
	return hToken;
}

void IsTrustedInstallerRunning() {
	SC_HANDLE schSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
	if (!schSCManager) return;

	SC_HANDLE schService = OpenServiceA(schSCManager, "TrustedInstaller", SERVICE_QUERY_STATUS | SERVICE_START);
	if (schService) {
		SERVICE_STATUS_PROCESS ssStatus;
		DWORD dwBytesNeeded;
		if (QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
			if (ssStatus.dwCurrentState != SERVICE_RUNNING) {
				printf("[*] TrustedInstaller service is stopped. Starting it...\n");
				StartServiceA(schService, 0, NULL);
				Sleep(2000);
			} else {
				printf("[*] TrustedInstaller service is already running.\n");
			}
		}
		CloseServiceHandle(schService);
	}
	CloseServiceHandle(schSCManager);
}

int main(int argc, char* argv[]) {
	if (!IsAdmin()) {
		printf("[!] Must be run as Administrator.\n");
		return 1;
	}

	if (!EnablePrivilege("SeDebugPrivilege")) {
		printf("[!] Failed to enable SeDebugPrivilege\n");
		return 1;
	}

	printf("[*] Stage 1: Getting SYSTEM token from winlogon.exe...\n");
	DWORD winlogonPid = GetPidByName("winlogon.exe");
	if (!winlogonPid) {
		printf("[!] winlogon.exe not found.\n");
		return 1;
	}

	HANDLE hSystemToken = GetProcessToken(winlogonPid, TOKEN_DUPLICATE | TOKEN_QUERY);
	if (!hSystemToken) {
		printf("[!] Failed to get winlogon token. Error: %lu\n", GetLastError());
		return 1;
	}

	HANDLE hDupSystemToken = NULL;
	if (!DuplicateTokenEx(hSystemToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &hDupSystemToken)) {
		printf("[!] DuplicateTokenEx for System failed. Error: %lu\n", GetLastError());
		return 1;
	}
	CloseHandle(hSystemToken);

	printf("[*] Stage 2: Impersonating SYSTEM to access TrustedInstaller...\n");
	if (!ImpersonateLoggedOnUser(hDupSystemToken)) {
		printf("[!] ImpersonateLoggedOnUser failed. Error: %lu\n", GetLastError());
		return 1;
	}

	IsTrustedInstallerRunning();

	DWORD tiPid = GetPidByName("TrustedInstaller.exe");
	if (!tiPid) {
		printf("[!] TrustedInstaller.exe is not running. Try starting the service 'TrustedInstaller'.\n");
		RevertToSelf();
		return 1;
	}

	HANDLE hTiToken = GetProcessToken(tiPid, TOKEN_DUPLICATE | TOKEN_QUERY);
	if (!hTiToken) {
		printf("[!] Failed to open TI token even as SYSTEM. Error: %lu\n", GetLastError());
		RevertToSelf();
		return 1;
	}

	RevertToSelf();

	HANDLE hDupTiToken = NULL;
	if (!DuplicateTokenEx(hTiToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &hDupTiToken)) {
		printf("[!] DuplicateTokenEx for TI failed. Error: %lu\n", GetLastError());
		CloseHandle(hTiToken);
		return 1;
	}
	CloseHandle(hTiToken);

	printf("[*] Stage 3: Launching process as TrustedInstaller...\n");

	STARTUPINFOW si = { sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION pi;

	wchar_t cmdLine[512] = L"cmd.exe";
	if (argc > 1) {
		cmdLine[0] = L'\0';
		for (int i = 1; i < argc; i++) {
			wchar_t wideArg[MAX_PATH];
			mbstowcs(wideArg, argv[i], MAX_PATH);
			wcscat(cmdLine, L"\"");
			wcscat(cmdLine, wideArg);
			wcscat(cmdLine, L"\" ");
		}
	}

	if (!CreateProcessWithTokenW(hDupTiToken, 0, NULL, cmdLine, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		printf("[!] CreateProcessWithTokenW failed. Error: %lu\n", GetLastError());
		CloseHandle(hDupTiToken);
		return 1;
	}

	printf("[+] Success! TrustedInstaller process created (PID: %lu).\n", pi.dwProcessId);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(hDupTiToken);
	CloseHandle(hDupSystemToken);

	return 0;
}
