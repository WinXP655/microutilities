#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int rand_range(int min, int max) {
    return min + (rand() % (max - min + 1));
}

void randomize_legacy_colors() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Colors", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        const char* value_names[] = {
            "ActiveBorder", "ActiveTitle", "AppWorkSpace", "Background", "ButtonAlternateFace",
            "ButtonDkShadow", "ButtonFace", "ButtonHilight", "ButtonLight", "ButtonShadow",
            "ButtonText", "GradientActiveTitle", "GradientInactiveTitle", "GrayText", "Hilight",
            "HilightText", "HotTrackingColor", "InactiveBorder", "InactiveTitle", "InactiveTitleText",
            "InfoText", "InfoWindow", "Menu", "MenuText", "Scrollbar", "TitleText", "Window",
            "WindowFrame", "WindowText", "MenuHilight", "MenuBar"
        };
        int count = sizeof(value_names) / sizeof(value_names[0]);

        for (int i = 0; i < count; i++) {
            char val[20];
            int r = rand_range(0, 255);
            int g = rand_range(0, 255);
            int b = rand_range(0, 255);
            snprintf(val, sizeof(val), "%d %d %d", r, g, b);
            RegSetValueEx(hKey, value_names[i], 0, REG_SZ, (BYTE*)val, strlen(val) + 1);
        }
        RegCloseKey(hKey);
    }
}

void randomize_modern_accent() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\DWM", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        int r = rand_range(0, 255);
        int g = rand_range(0, 255);
        int b = rand_range(0, 255);
        DWORD color = (255 << 24) | (r << 16) | (g << 8) | b;
        RegSetValueEx(hKey, "ColorizationColor", 0, REG_DWORD, (BYTE*)&color, sizeof(DWORD));

        DWORD prevalence = 1; 
        RegSetValueEx(hKey, "ColorPrevalence", 0, REG_DWORD, (BYTE*)&prevalence, sizeof(DWORD));

        DWORD transparency = rand() % 2;
        RegSetValueEx(hKey, "EnableTransparency", 0, REG_DWORD, (BYTE*)&transparency, sizeof(DWORD));

        RegCloseKey(hKey);
    }
}

void randomize_uwp_accent() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        int r = rand_range(0, 255);
        int g = rand_range(0, 255);
        int b = rand_range(0, 255);
        DWORD uwp_color = (255 << 24) | (r << 16) | (g << 8) | b;
        RegSetValueEx(hKey, "AccentColorMenu", 0, REG_DWORD, (BYTE*)&uwp_color, sizeof(DWORD));
        RegSetValueEx(hKey, "StartColorMenu", 0, REG_DWORD, (BYTE*)&uwp_color, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

int main() {
    srand((unsigned)time(NULL));
    
    printf("Colorizer - make your experience more fun.\n");
    
    randomize_legacy_colors();
    randomize_modern_accent();
    randomize_uwp_accent();
    
    printf("Recommended to logoff for changes to apply.\n");
    
    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG, 5000, NULL);
    
    Sleep(1000);
    return 0;
}