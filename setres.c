#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MODES 100

int main(int argc, char* argv[]) {
    if (argc == 1) {
        // --- LIST MODE ---
        DWORD widths[MAX_MODES];
        DWORD heights[MAX_MODES];
        int count = 0;

        DEVMODE dm = {0};
        dm.dmSize = sizeof(DEVMODE);

        DWORD i = 0;
        while (EnumDisplaySettings(NULL, i, &dm)) {
            DWORD w = dm.dmPelsWidth;
            DWORD h = dm.dmPelsHeight;

            // Skip tiny or invalid modes
            if (w < 800 || h < 600) {
                i++;
                continue;
            }

            // Check for duplicates
            int is_dup = 0;
            for (int j = 0; j < count; j++) {
                if (widths[j] == w && heights[j] == h) {
                    is_dup = 1;
                    break;
                }
            }

            if (!is_dup && count < MAX_MODES) {
                widths[count] = w;
                heights[count] = h;
                count++;
            }
            i++;
        }

        // Simple sort: by width, then height
        for (int a = 0; a < count - 1; a++) {
            for (int b = a + 1; b < count; b++) {
                if (widths[a] > widths[b] ||
                    (widths[a] == widths[b] && heights[a] > heights[b])) {
                    // Swap widths
                    DWORD tmp = widths[a];
                    widths[a] = widths[b];
                    widths[b] = tmp;
                    // Swap heights
                    tmp = heights[a];
                    heights[a] = heights[b];
                    heights[b] = tmp;
                }
            }
        }

        printf("Available resolutions:\n");
        printf("----------------------\n");
        if (count == 0) {
            printf("No valid resolutions found.\n");
        } else {
            for (int idx = 0; idx < count; idx++) {
                printf("%4lu x %-4lu\n", widths[idx], heights[idx]);
            }
        }
        return 0;
    }

    // --- SET MODE ---
    if (argc != 3) {
        printf("Usage:\n");
        printf("  %s                : List available resolutions\n", argv[0]);
        printf("  %s <width> <height> : Set screen resolution\n", argv[0]);
        return 1;
    }

    int w = atoi(argv[1]);
    int h = atoi(argv[2]);

    if (w <= 0 || h <= 0) {
        printf("Error: Width and height must be positive integers.\n");
        return 1;
    }

    DEVMODE dm = {0};
    dm.dmSize = sizeof(DEVMODE);
    dm.dmPelsWidth = (DWORD)w;
    dm.dmPelsHeight = (DWORD)h;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

    LONG result = ChangeDisplaySettings(&dm, 0);

    if (result == DISP_CHANGE_SUCCESSFUL) {
        printf("Resolution set to %dx%d.\n", w, h);
        return 0;
    } else {
        const char* msg = "Unknown error";
        switch (result) {
            case DISP_CHANGE_RESTART:   msg = "Reboot required"; break;
            case DISP_CHANGE_FAILED:    msg = "Failed"; break;
            case DISP_CHANGE_BADMODE:   msg = "Unsupported resolution"; break;
            case DISP_CHANGE_NOTUPDATED: msg = "Registry error"; break;
        }
        printf("Failed: %s (code %ld)\n", msg, result);
        return 1;
    }
}