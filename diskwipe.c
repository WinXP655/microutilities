// partition_wipe.c
// Компиляция: gcc partition_wipe.c -o partition_wipe.exe -O2 -s -municode
// Запуск: обязательно от администратора

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Получить букву системного диска (например 'C')
char GetSystemDriveLetter(void) {
    char sysRoot[MAX_PATH];
    GetSystemDirectoryA(sysRoot, MAX_PATH);
    return sysRoot[0]; // первая буква
}

// Проверить, является ли диск с указанной буквой системным
int IsSystemDrive(char driveLetter) {
    char sysLetter = GetSystemDriveLetter();
    return (driveLetter == sysLetter);
}

// Получить список доступных несъемных томов, исключая системный диск
void ListNonSystemVolumes(void) {
    printf("\nAvailable partitions (system is hidden):\n");
    printf("------------------------------------------\n");
    DWORD drives = GetLogicalDrives();
    char letter;
    int found = 0;
    for (letter = 'A'; letter <= 'Z'; letter++) {
        if (drives & (1 << (letter - 'A'))) {
            // Определяем тип диска
            char root[4] = {letter, ':', '\\', 0};
            UINT dtype = GetDriveTypeA(root);
            if (dtype == DRIVE_FIXED) {
                if (IsSystemDrive(letter)) {
                    continue; // скрываем системный
                }
                // Дополнительно проверяем, можно ли открыть том
                char volPath[16];
                snprintf(volPath, sizeof(volPath), "\\\\.\\%c:", letter);
                HANDLE h = CreateFileA(volPath, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    CloseHandle(h);
                    printf("  %c:  (Fixed drive)\n", letter);
                    found++;
                }
            }
        }
    }
    if (found == 0) {
        printf("  No another partitions.\n");
    }
    printf("\n");
}

// Блокировка, размонтирование и стирание раздела нулями
int WipePartition(char driveLetter) {
    char volumePath[16];
    snprintf(volumePath, sizeof(volumePath), "\\\\.\\%c:", driveLetter);
    
    // 1. Открываем том с монопольными правами на запись
    HANDLE hVol = CreateFileA(volumePath,
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE) {
        printf("Error: failed to open volume %c: (run as administrator?)\n", driveLetter);
        return 0;
    }
    
    // 2. Блокируем том (эксклюзивный доступ)
    DWORD bytesRet;
    if (!DeviceIoControl(hVol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL)) {
        printf("Error: failed to lock %c:.\n", driveLetter);
        printf("Some apps may use it.\n");
        CloseHandle(hVol);
        return 0;
    }
    printf("Volume %c: locked (exclusive access).\n", driveLetter);
    
    // 3. Размонтируем том (убираем связь с файловой системой)
    if (!DeviceIoControl(hVol, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL)) {
        printf("Warning: failed to unmount %c: (may be already unmounted)\n", driveLetter);
        // Продолжаем, так как блокировка уже есть
    } else {
        printf("Volume %c: unmounted.\n", driveLetter);
    }
    
    // 4. Получаем размер тома в байтах
    GET_LENGTH_INFORMATION lenInfo;
    if (!DeviceIoControl(hVol, IOCTL_DISK_GET_LENGTH_INFO,
                         NULL, 0, &lenInfo, sizeof(lenInfo), &bytesRet, NULL)) {
        printf("Error: failed to get volume size.\n");
        DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
        CloseHandle(hVol);
        return 0;
    }
    LONGLONG totalBytes = lenInfo.Length.QuadPart;
    if (totalBytes <= 0) {
        printf("Error: volume size is zero.\n");
        DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
        CloseHandle(hVol);
        return 0;
    }
    
    // Определяем сектор (обычно 512 байт, но можно запросить у диска)
    DISK_GEOMETRY dg;
    if (DeviceIoControl(hVol, IOCTL_DISK_GET_DRIVE_GEOMETRY,
                        NULL, 0, &dg, sizeof(dg), &bytesRet, NULL)) {
        // используем dg.BytesPerSector
    } else {
        dg.BytesPerSector = 512; // стандарт
    }
    DWORD sectorSize = dg.BytesPerSector;
    LONGLONG totalSectors = totalBytes / sectorSize;
    
    printf("\nStart wiping of %c:\n", driveLetter);
    printf("Size: %I64u MB, sectors: %I64u, sector size: %u bytes\n",
           totalBytes / (1024*1024), totalSectors, sectorSize);
    printf("WARNING: ALL DATA ON %c: WILL BE PERMANENTLY LOST!\n", driveLetter);
    printf("To continue, enter and press ENTER: ERASE ALL\n>> ");
    
    char confirm[32];
    fgets(confirm, sizeof(confirm), stdin);
    confirm[strcspn(confirm, "\n")] = 0;
    if (strcmp(confirm, "ERASE ALL") != 0) {
        printf("Wiping aborted.\n");
        DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
        CloseHandle(hVol);
        return 0;
    }
    
    // Выделяем буфер нулей (размером в один сектор)
    BYTE *zeroBuffer = (BYTE*)calloc(sectorSize, 1);
    if (!zeroBuffer) {
        printf("Error allocating memory.\n");
        DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
        CloseHandle(hVol);
        return 0;
    }
    
    printf("\nWiping... 0%%");
    SetFilePointer(hVol, 0, NULL, FILE_BEGIN);
    LONGLONG sector;
    int lastPercent = -1;
    for (sector = 0; sector < totalSectors; sector++) {
        DWORD written;
        if (!WriteFile(hVol, zeroBuffer, sectorSize, &written, NULL) || written != sectorSize) {
            printf("\nWrite error at sector %I64u\n", sector);
            break;
        }
        // Отображение процентов
        int percent = (int)((sector * 100) / totalSectors);
        if (percent != lastPercent) {
            printf("\rWiping... %d%%", percent);
            lastPercent = percent;
            fflush(stdout);
        }
    }
    printf("\rWiping... 100%% completed.\n");
    
    free(zeroBuffer);
    
    // 5. Разблокируем том
    DeviceIoControl(hVol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
    CloseHandle(hVol);
    
    printf("\nVolume %c: successfullt wiped.\n", driveLetter);
    printf("Note: it can be displayed as not formatted.\n");
    return 1;
}

int main() {
    // Проверка прав администратора
    BOOL isAdmin = FALSE;
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev;
        DWORD size = sizeof(elev);
        if (GetTokenInformation(hToken, TokenElevation, &elev, size, &size)) {
            isAdmin = elev.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    if (!isAdmin) {
        printf("Error: adminisrative privileges required.\n");
        system("pause");
        return 1;
    }
    
    printf("=== Disk Wiping Utility ===\n");
    printf("System disk (%c:) is not displayed and cannot be wiped.\n\n",
           GetSystemDriveLetter());
    
    ListNonSystemVolumes();
    
    printf("Enter a drive letter to wipe (e.g. D): ");
    char drive[4];
    fgets(drive, sizeof(drive), stdin);
    // Очистка ввода
    drive[strcspn(drive, "\n")] = 0;
    if (strlen(drive) != 1 || drive[0] < 'A' || drive[0] > 'Z') {
        printf("Invalid letter.\n");
        system("pause");
        return 1;
    }
    char letter = drive[0];
    
    // Проверка на системный диск
    if (IsSystemDrive(letter)) {
        printf("Error: you are trying to wipe system disk %c:.\n", letter);
        printf("This action is disallowed for safety.\n");
        system("pause");
        return 1;
    }
    
    // Проверка, существует ли такой фиксированный диск
    char root[4] = {letter, ':', '\\', 0};
    if (GetDriveTypeA(root) != DRIVE_FIXED) {
        printf("Disk %c: is not a local hard drive.\n", letter);
        system("pause");
        return 1;
    }
    
    WipePartition(letter);
    
    printf("\nDone. Press any key to exit...");
    getchar();
    return 0;
}