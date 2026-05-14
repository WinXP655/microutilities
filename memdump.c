// memdump_smart.c
// compact memory dumper for current process using VirtualQuery
// Usage: memdump_smart.exe <outfile> <start> <end>
// Addresses may be hex (0x...) or decimal.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <outfile> <start> <end>\n", argv[0]);
        return 1;
    }

    const char *outname = argv[1];
    uintptr_t start = (uintptr_t)strtoull(argv[2], NULL, 0);
    uintptr_t end   = (uintptr_t)strtoull(argv[3], NULL, 0);

    if (end <= start) {
        printf("end must be > start\n");
        return 1;
    }

    FILE *log = fopen("memdump_errors.txt", "w");
    if (!log) {
        printf("Cannot open memdump_errors.txt for writing\n");
        return 1;
    }

    HANDLE hOut = CreateFileA(outname, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        fprintf(log, "Failed to open output file %s\n", outname);
        fclose(log);
        return 1;
    }

    const SIZE_T chunk = 0x1000; // write in 4KB chunks (region may be larger)
    unsigned char *buf = (unsigned char*)malloc(chunk);
    if (!buf) {
        fprintf(log, "malloc failed\n");
        CloseHandle(hOut);
        fclose(log);
        return 1;
    }

    uintptr_t addr = start;
    unsigned long long bytes_written = 0;
    unsigned long long regions_dumped = 0;
    unsigned long long regions_denied = 0;
    unsigned long long query_fail = 0;

    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T q = VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi));
        if (q == 0) {
            // If VirtualQuery fails, advance by one page to avoid infinite loop.
            fprintf(log, "Failed to query memory at 0x%p\n", (void*)addr);
            query_fail++;
            addr += 0x1000;
            continue;
        }

        uintptr_t region_base = (uintptr_t)mbi.BaseAddress;
        SIZE_T region_size = mbi.RegionSize;
        uintptr_t region_end = region_base + region_size;
        if (region_end > end) region_end = end;

        // If region is not commit or has noaccess/guard, log as denied (one line per region)
        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD)) {
            fprintf(log, "Denied: 0x%p - 0x%p (State=0x%08x Protect=0x%08x)\n",
                    (void*)region_base, (void*)(region_end - 1), (unsigned)mbi.State, (unsigned)mbi.Protect);
            regions_denied++;
            addr = region_base + region_size;
            continue;
        }

        // Dump the readable region in chunks
        SIZE_T to_dump = region_end - region_base;
        SIZE_T offset = 0;
        while (offset < to_dump) {
            SIZE_T want = (to_dump - offset) > chunk ? chunk : (to_dump - offset);
            // memcpy from our own process memory (same process)
            memcpy(buf, (const void*)(region_base + offset), want);
            DWORD written = 0;
            if (!WriteFile(hOut, buf, (DWORD)want, &written, NULL)) {
                fprintf(log, "WriteFile failed at region 0x%p offset 0x%Ix\n",
                        (void*)region_base, (SIZE_T)offset);
                break;
            }
            bytes_written += written;
            offset += want;
        }

        regions_dumped++;
        // Periodic short progress
        if ((regions_dumped & 0x3FF) == 0) {
            printf("Dump progress: wrote %llu KB, dumped regions %llu\r",
                   (unsigned long long)(bytes_written / 1024), regions_dumped);
        }

        addr = region_base + region_size;
    }

    // final summary
    printf("\nDump complete: %llu bytes written to %s\n", (unsigned long long)bytes_written, outname);
    printf("Regions dumped: %llu, denied: %llu, query failures: %llu\n",
           regions_dumped, regions_denied, query_fail);

    fprintf(log, "SUMMARY: bytes=%llu regions_dumped=%llu regions_denied=%llu query_fail=%llu\n",
            (unsigned long long)bytes_written, regions_dumped, regions_denied, query_fail);

    free(buf);
    CloseHandle(hOut);
    fclose(log);
    return 0;
}
