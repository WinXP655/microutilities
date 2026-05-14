/*
 * Advanced File Tool for Windows
 * Compile: gcc filetool.c -o filetool.exe -lbcrypt
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

// === FUNCTION PROTOTYPES (REQUIRED IN C) ===
int cmd_info(const wchar_t* wpath);
int cmd_create(const wchar_t* wpath, const char* size_str);
int cmd_clean(const wchar_t* wpath);
int cmd_hex(const wchar_t* wpath);
int cmd_compare(const wchar_t* wpath1, const wchar_t* wpath2);
int cmd_hash(const wchar_t* wpath);
int cmd_type(const wchar_t* wpath);
int cmd_fill(const wchar_t* wpath, const char* byte_str);
int cmd_wipe(const wchar_t* wpath, int passes);
int cmd_hexpart(const wchar_t* wpath, const char* start_str, const char* end_str);

// === Helper Functions ===
void WideToUtf8(const wchar_t* wstr, char* astr, int size) {
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, astr, size, NULL, NULL);
}

void PrintFileTime(FILETIME ft) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    printf("%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
}

int GetFileAttrsAndData(const wchar_t* wpath, DWORD* attrs, WIN32_FILE_ATTRIBUTE_DATA* fad) {
    *attrs = GetFileAttributesW(wpath);
    if (*attrs == INVALID_FILE_ATTRIBUTES) return 1;
    if (*attrs & FILE_ATTRIBUTE_DIRECTORY) return 2;
    return GetFileAttributesExW(wpath, GetFileExInfoStandard, fad) ? 0 : 3;
}

void print_file_info(const wchar_t* wpath, const char* label) {
    DWORD attrs;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    int err = GetFileAttrsAndData(wpath, &attrs, &fad);
    if (err) {
        printf("%-12s: [Error]\n", label);
        return;
    }

    wchar_t fullpath[MAX_PATH];
    GetFullPathNameW(wpath, MAX_PATH, fullpath, NULL);
    wchar_t* fname = wcsrchr(fullpath, L'\\');
    fname = fname ? fname + 1 : fullpath;
    char fnameA[256];
    WideToUtf8(fname, fnameA, sizeof(fnameA));

    ULARGE_INTEGER size;
    size.LowPart = fad.nFileSizeLow;
    size.HighPart = fad.nFileSizeHigh;

    printf("%-12s: %s\n", label, fnameA);
    printf("%-12s: %llu bytes\n", "Size", size.QuadPart);
    printf("%-12s: ", "Created");
    PrintFileTime(fad.ftCreationTime); printf("\n");
    printf("%-12s: ", "Modified");
    PrintFileTime(fad.ftLastWriteTime); printf("\n");
}

// === COMMAND IMPLEMENTATIONS ===

int cmd_info(const wchar_t* wpath) {
    DWORD attrs;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    int err = GetFileAttrsAndData(wpath, &attrs, &fad);
    if (err == 1) { fprintf(stderr, "Error: File not found.\n"); return 1; }
    if (err == 2) { fprintf(stderr, "Error: Path is a directory.\n"); return 1; }
    if (err == 3) { fprintf(stderr, "Error: Cannot read attributes.\n"); return 1; }

    wchar_t fullpath[MAX_PATH];
    GetFullPathNameW(wpath, MAX_PATH, fullpath, NULL);
    wchar_t* fname = wcsrchr(fullpath, L'\\');
    fname = fname ? fname + 1 : fullpath;
    char fnameA[256], fullpathA[MAX_PATH];
    WideToUtf8(fname, fnameA, sizeof(fnameA));
    WideToUtf8(fullpath, fullpathA, sizeof(fullpathA));

    ULARGE_INTEGER size;
    size.LowPart = fad.nFileSizeLow;
    size.HighPart = fad.nFileSizeHigh;

    char attrStr[256] = "";
    if (attrs & FILE_ATTRIBUTE_READONLY)      strcat(attrStr, "READONLY ");
    if (attrs & FILE_ATTRIBUTE_HIDDEN)       strcat(attrStr, "HIDDEN ");
    if (attrs & FILE_ATTRIBUTE_SYSTEM)       strcat(attrStr, "SYSTEM ");
    if (attrs & FILE_ATTRIBUTE_ARCHIVE)      strcat(attrStr, "ARCHIVE ");
    if (attrs & FILE_ATTRIBUTE_COMPRESSED)   strcat(attrStr, "COMPRESSED ");
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED)    strcat(attrStr, "ENCRYPTED ");

    printf("Filename:   %s\n", fnameA);
    printf("Type:       File\n");
    printf("Location:   %s\n", fullpathA);
    printf("Size:       %llu bytes\n", size.QuadPart);
    printf("Created:    "); PrintFileTime(fad.ftCreationTime); printf("\n");
    printf("Modified:   "); PrintFileTime(fad.ftLastWriteTime); printf("\n");
    printf("Attributes: %s\n", attrStr[0] ? attrStr : "(none)");
    return 0;
}

int cmd_create(const wchar_t* wpath, const char* size_str) {
    ULONGLONG size = strtoull(size_str, NULL, 10);
    if (size == 0 && strcmp(size_str, "0") != 0) {
        fprintf(stderr, "Error: Invalid size.\n");
        return 1;
    }

    HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot create file.\n");
        return 1;
    }

    LARGE_INTEGER li;
    li.QuadPart = size;
    BOOL success = SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) && SetEndOfFile(hFile);
    CloseHandle(hFile);

    if (!success) {
        DeleteFileW(wpath);
        fprintf(stderr, "Error: Failed to set file size.\n");
        return 1;
    }

    printf("Created file with size %llu bytes.\n", size);
    return 0;
}

int cmd_clean(const wchar_t* wpath) {
    HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Cannot get file size.\n");
        return 1;
    }

    if (size.QuadPart == 0) {
        CloseHandle(hFile);
        printf("File is empty.\n");
        return 0;
    }

    char* buf = calloc(1, 1024*1024);
    ULONGLONG written = 0;
    while (written < size.QuadPart) {
        DWORD to_write = (DWORD)min(size.QuadPart - written, 1024*1024);
        DWORD done;
        WriteFile(hFile, buf, to_write, &done, NULL);
        written += done;
    }
    free(buf);
    CloseHandle(hFile);
    printf("File overwritten with zeros.\n");
    return 0;
}

int cmd_hex(const wchar_t* wpath) {
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart > 100*1024*1024) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: File too large or unreadable.\n");
        return 1;
    }

    char* buf = malloc((size_t)size.QuadPart);
    DWORD bytesRead;
    BOOL ok = ReadFile(hFile, buf, (DWORD)size.QuadPart, &bytesRead, NULL) && (bytesRead == size.QuadPart);
    CloseHandle(hFile);

    if (!ok) {
        free(buf);
        fprintf(stderr, "Error: Failed to read file.\n");
        return 1;
    }

    for (ULONGLONG i = 0; i < size.QuadPart; i += 16) {
        printf("%08llX  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < size.QuadPart)
                printf("%02X ", (unsigned char)buf[i + j]);
            else
                printf("   ");
        }
        printf(" ");
        for (int j = 0; j < 16; j++) {
            if (i + j < size.QuadPart) {
                unsigned char c = buf[i + j];
                putchar((c >= 32 && c <= 126) ? c : '.');
            } else {
                putchar(' ');
            }
        }
        printf("\n");
    }
    free(buf);
    return 0;
}

int cmd_compare(const wchar_t* wpath1, const wchar_t* wpath2) {
    printf("=== File Comparison ===\n");
    print_file_info(wpath1, "File1");
    printf("------------------------\n");
    print_file_info(wpath2, "File2");
    return 0;
}

int cmd_hash(const wchar_t* wpath) {
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart == 0) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Empty or unreadable file.\n");
        return 1;
    }

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status;
    DWORD hashLen = 32;
    BYTE* hash = malloc(hashLen);
    BYTE* buffer = malloc(65536);

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (BCRYPT_SUCCESS(status))
        status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (BCRYPT_SUCCESS(status)) {
        DWORD bytesRead;
        while (ReadFile(hFile, buffer, 65536, &bytesRead, NULL) && bytesRead > 0) {
            BCryptHashData(hHash, buffer, bytesRead, 0);
        }
        status = BCryptFinishHash(hHash, hash, hashLen, 0);
    }

    if (BCRYPT_SUCCESS(status)) {
        printf("SHA256: ");
        for (DWORD i = 0; i < hashLen; i++) printf("%02x", hash[i]);
        printf("\n");
    }

    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    free(hash); free(buffer);
    CloseHandle(hFile);
    return BCRYPT_SUCCESS(status) ? 0 : 1;
}

const char* detect_file_type(const unsigned char* buf, size_t len) {
    if (len >= 4) {
        if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return "JPEG Image";
        if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) return "PNG Image";
        if (buf[0] == 0x47 && buf[1] == 0x49 && buf[2] == 0x46) return "GIF Image";
        if (buf[0] == 0x25 && buf[1] == 0x50 && buf[2] == 0x44 && buf[3] == 0x46) return "PDF Document";
        if (buf[0] == 0x50 && buf[1] == 0x4B && buf[2] == 0x03 && buf[3] == 0x04) return "ZIP Archive";
        if (buf[0] == 0x4D && buf[1] == 0x5A) return "Windows Executable";
        if (len >= 8 && memcmp(buf, "%PDF-", 5) == 0) return "PDF Document";
        if (len >= 2 && buf[0] == 0x1F && buf[1] == 0x8B) return "GZIP Compressed";
    }
    if (len > 0) {
        for (size_t i = 0; i < (len < 1024 ? len : 1024); i++) {
            if (buf[i] < 0x09 || (buf[i] > 0x0D && buf[i] < 0x20) || buf[i] == 0x7F)
                return "Binary Data";
        }
        return "Plain Text";
    }
    return "Unknown";
}

int cmd_type(const wchar_t* wpath) {
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    DWORD size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        printf("Type: Empty\n");
        return 0;
    }

    size = min(size, 1024);
    unsigned char* buf = malloc(size);
    DWORD bytesRead;
    ReadFile(hFile, buf, size, &bytesRead, NULL);
    CloseHandle(hFile);

    const char* type = detect_file_type(buf, bytesRead);
    printf("Type: %s\n", type);
    free(buf);
    return 0;
}

int cmd_fill(const wchar_t* wpath, const char* byte_str) {
    unsigned int val = strtoul(byte_str, NULL, 0);
    if (val > 0xFF) {
        fprintf(stderr, "Error: Byte must be 0x00-0xFF or 0-255.\n");
        return 1;
    }
    unsigned char pattern = (unsigned char)val;

    HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Cannot get file size.\n");
        return 1;
    }

    unsigned char* buf = malloc(65536);
    memset(buf, pattern, 65536);

    LARGE_INTEGER offset = {0};
    SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);

    ULONGLONG written = 0;
    while (written < size.QuadPart) {
        DWORD to_write = (DWORD)min(size.QuadPart - written, 65536);
        DWORD done;
        WriteFile(hFile, buf, to_write, &done, NULL);
        written += done;
    }

    free(buf);
    CloseHandle(hFile);
    printf("File overwritten with 0x%02X.\n", pattern);
    return 0;
}

int cmd_wipe(const wchar_t* wpath, int passes) {
    if (passes <= 0) passes = 3;

    for (int pass = 0; pass < passes; pass++) {
        unsigned char pattern = (pass == passes - 1) ? 0x00 : (unsigned char)(0xFF ^ pass);
        HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error: Cannot open file.\n");
            return 1;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile, &size)) {
            CloseHandle(hFile);
            fprintf(stderr, "Error: Cannot get file size.\n");
            return 1;
        }

        unsigned char* buf = malloc(65536);
        memset(buf, pattern, 65536);

        LARGE_INTEGER offset = {0};
        SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);

        ULONGLONG written = 0;
        while (written < size.QuadPart) {
            DWORD to_write = (DWORD)min(size.QuadPart - written, 65536);
            DWORD done;
            WriteFile(hFile, buf, to_write, &done, NULL);
            written += done;
        }

        free(buf);
        CloseHandle(hFile);
    }

    if (!DeleteFileW(wpath)) {
        fprintf(stderr, "Warning: Failed to delete file after wipe.\n");
        return 1;
    }

    printf("File securely wiped (%d passes) and deleted.\n", passes);
    return 0;
}

ULONGLONG parse_offset(const char* str) {
    if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0) {
        return strtoull(str + 2, NULL, 16);
    }
    return strtoull(str, NULL, 10);
}

int cmd_hexpart(const wchar_t* wpath, const char* start_str, const char* end_str) {
    ULONGLONG start = parse_offset(start_str);
    ULONGLONG end = parse_offset(end_str);

    if (start >= end) {
        fprintf(stderr, "Error: Start offset must be less than end.\n");
        return 1;
    }

    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open file.\n");
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Cannot get file size.\n");
        return 1;
    }

    if (start >= (ULONGLONG)size.QuadPart) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Start offset beyond file size.\n");
        return 1;
    }

    if (end > (ULONGLONG)size.QuadPart) {
        end = (ULONGLONG)size.QuadPart;
    }

    ULONGLONG length = end - start;

    // Seek to start
    LARGE_INTEGER offset;
    offset.QuadPart = start;
    if (!SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN)) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Failed to seek in file.\n");
        return 1;
    }

    // Read and display in chunks
    const DWORD CHUNK_SIZE = 4096;
    char* buf = malloc(CHUNK_SIZE);
    if (!buf) {
        CloseHandle(hFile);
        fprintf(stderr, "Error: Out of memory.\n");
        return 1;
    }

    ULONGLONG current = start;
    while (current < end) {
        DWORD to_read = (DWORD)min((ULONGLONG)CHUNK_SIZE, end - current);
        DWORD bytesRead;
        if (!ReadFile(hFile, buf, to_read, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        // Hex dump this chunk
        for (DWORD i = 0; i < bytesRead; i += 16) {
            ULONGLONG line_addr = current + i;
            printf("%08llX  ", line_addr);

            // Hex bytes
            for (int j = 0; j < 16; j++) {
                if (i + j < bytesRead) {
                    printf("%02X ", (unsigned char)buf[i + j]);
                } else {
                    printf("   ");
                }
            }
            printf(" ");

            // ASCII
            for (int j = 0; j < 16; j++) {
                if (i + j < bytesRead) {
                    unsigned char c = buf[i + j];
                    putchar((c >= 32 && c <= 126) ? c : '.');
                } else {
                    putchar(' ');
                }
            }
            printf("\n");
        }

        current += bytesRead;
    }

    free(buf);
    CloseHandle(hFile);
    return 0;
}

// === MAIN ===
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  filetool /info <file>\n");
        fprintf(stderr, "  filetool /create <file> <size>\n");
        fprintf(stderr, "  filetool /clean <file>\n");
        fprintf(stderr, "  filetool /hex <file>\n");
        fprintf(stderr, "  filetool /compare <f1> <f2>\n");
        fprintf(stderr, "  filetool /hash <file>\n");
        fprintf(stderr, "  filetool /type <file>\n");
        fprintf(stderr, "  filetool /fill <file> <byte>\n");
        fprintf(stderr, "  filetool /wipe <file> [passes]\n");
		fprintf(stderr, "  filetool /hexpart <file> <offset 1> <offset 2>\n");
        return 1;
    }

    const char* cmd = argv[1];
    wchar_t wpath1[MAX_PATH], wpath2[MAX_PATH];

    if (strcmp(cmd, "/info") == 0) {
        if (argc != 3) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_info(wpath1);
    }
    else if (strcmp(cmd, "/create") == 0) {
        if (argc != 4) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH); // ✅ FIXED: was 'wpath'
        return cmd_create(wpath1, argv[3]);
    }
    else if (strcmp(cmd, "/clean") == 0) {
        if (argc != 3) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_clean(wpath1);
    }
    else if (strcmp(cmd, "/hex") == 0) {
        if (argc != 3) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_hex(wpath1);
    }
    else if (strcmp(cmd, "/compare") == 0) {
        if (argc != 4) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, wpath2, MAX_PATH);
        return cmd_compare(wpath1, wpath2);
    }
    else if (strcmp(cmd, "/hash") == 0) {
        if (argc != 3) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_hash(wpath1);
    }
    else if (strcmp(cmd, "/type") == 0) {
        if (argc != 3) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_type(wpath1);
    }
    else if (strcmp(cmd, "/fill") == 0) {
        if (argc != 4) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        return cmd_fill(wpath1, argv[3]);
    }
    else if (strcmp(cmd, "/wipe") == 0) {
        if (argc < 3 || argc > 4) return 1;
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath1, MAX_PATH);
        int passes = (argc == 4) ? atoi(argv[3]) : 3;
        return cmd_wipe(wpath1, passes);
    }
	else if (strcmp(cmd, "/hexpart") == 0) {
		if (argc != 5) {
			fprintf(stderr, "Usage: filetool /hexpart <file> <start> <end>\n");
			fprintf(stderr, "Offsets: decimal (1024) or hex (0x400)\n");
			return 1;
		}
		wchar_t wpath[MAX_PATH];
		MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, wpath, MAX_PATH);
		return cmd_hexpart(wpath, argv[3], argv[4]);
	}
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return 1;
    }
}