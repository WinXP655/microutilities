#include <stdio.h>
#include <conio.h>
#include <windows.h>

int main(void) {
    int count = 0;

    SetConsoleTitleA("TinyClicker");
    printf("TinyClicker\n");
    printf("Press SPACE to click.\n");
    printf("Press ESC to exit.\n\n");

    while (1) {
        if (_kbhit()) {
            int key = _getch();

            if (key == 27) { // ESC
                printf("\nExiting...\n");
                break;
            }

            if (key == ' ') {
                count++;
                printf("\rClicks: %d", count);
                fflush(stdout);
            }
        }

        Sleep(1);
    }

    return 0;
}
