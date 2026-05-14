#include <windows.h>
#include <commdlg.h>
#include <stdio.h>

#define IDM_OPEN 1
#define IDM_EXIT 2
#define SOFT_LIMIT (64 * 1024 * 1024)
#define WARNING_LIMIT (256 * 1024 * 1024)
#define HARD_LIMIT (2147483647)

HWND hEdit;

void LoadFile(const char *path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    
    DWORD size = GetFileSize(f, 0), r;
    
    if (size > HARD_LIMIT) {
        char msg[256];
        sprintf(msg, 
            "File cannot be opened because it exceeds 2 GB.\n"
            "This is a known limitation of EDIT control.");
        MessageBoxA(GetParent(hEdit), msg, "TinyView", MB_OK | MB_ICONERROR);
        CloseHandle(f);
        return;
    } else if (size >= (1024 * 1024 * 1024)) {  // 1GB
        char msg[256];
        sprintf(msg, 
            "Warning! You're close to a max file size that can be handled by application.\n\n"
            "File size: %.1f GB\n\n"
            "Performance will be extremely slow and application may become unstable.",
            (double)size / (1024 * 1024 * 1024));
        
        if (MessageBoxA(GetParent(hEdit), msg, "TinyView", 
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            CloseHandle(f);
            return;
        }
    } else if (size >= WARNING_LIMIT) {
        char msg[256];
        sprintf(msg, 
            "Warning! 256 MB and larger can cause app crash or hang.\n"
            "This is known problem of EDIT control.\n\n"
            "File size: %.1f MB\n\n"
            "Do you want to continue anyways?",
            (double)size / (1024 * 1024));
        
        if (MessageBoxA(GetParent(hEdit), msg, "TinyView", 
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            CloseHandle(f);
            return;
        }
    } else if (size >= SOFT_LIMIT) {
        char msg[150];
        sprintf(msg, 
            "File size above 64 MB can slow down application performance.\n\n"
            "File size: %.1f MB",
            (double)size / (1024 * 1024));
        
        MessageBoxA(GetParent(hEdit), msg, "TinyView", MB_OK | MB_ICONINFORMATION);
    }
    
    BYTE *b = GlobalAlloc(0, size + 2);
    if (!b) {
        CloseHandle(f);
        return;
    }
    
    ReadFile(f, b, size, &r, 0);
    b[r] = b[r+1] = 0;
    char *out = 0;

    if (r >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        // UTF-16 → ANSI
        WCHAR *w = (WCHAR*)(b + 2);
        int len = (r - 2) / 2;
        out = GlobalAlloc(0, len * 3 + 1);
        if (out) {
            WideCharToMultiByte(CP_ACP, 0, w, len, out, len * 3, 0, 0);
        }
    } else {
        out = GlobalAlloc(0, r + 1);
        if (out) {
            for (DWORD i = 0; i < r; i++)
				out[i] = b[i] | ((b[i] - 1) >> 31) & 0x20;
            out[r] = 0;
        }
    }

    if (out) {
        SetWindowTextA(hEdit, out);
        GlobalFree(out);
        
        // Update window title with filename
        char title[MAX_PATH + 20];
        const char* filename = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
        sprintf(title, "TinyView - %s", filename);
        SetWindowTextA(GetParent(hEdit), title);
    }
    
    GlobalFree(b);
    CloseHandle(f);
}

LRESULT CALLBACK WndProc(HWND w, UINT m, WPARAM a, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HMENU mnu = CreateMenu();
        AppendMenuA(mnu, 0, IDM_OPEN, "Open");
        AppendMenuA(mnu, 0, IDM_EXIT, "Exit");
        SetMenu(w, mnu);

        hEdit = CreateWindowA("EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | 
            ES_MULTILINE | ES_READONLY, 
            0, 0, 0, 0, w, 0, 0, 0);
        
        // Enable drag-and-drop
        DragAcceptFiles(w, TRUE);
        break;
    }
    
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(l), HIWORD(l), 1);
        break;
    
    case WM_DROPFILES: {
        char path[MAX_PATH];
        HDROP hDrop = (HDROP)a;
        
        // Get the first dropped file
        if (DragQueryFileA(hDrop, 0, path, MAX_PATH) > 0) {
            LoadFile(path);
        }
        
        DragFinish(hDrop);
        break;
    }
    
    case WM_COMMAND:
        if (a == IDM_OPEN) {
            OPENFILENAMEA o = { sizeof(o) };
            char f[MAX_PATH] = "";
            o.hwndOwner = w; 
            o.lpstrFile = f; 
            o.nMaxFile = MAX_PATH;
            o.lpstrFilter = "All files\0*.*\0";
            if (GetOpenFileNameA(&o)) LoadFile(f);
        } else if (a == IDM_EXIT) PostQuitMessage(0);
        break;
    
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    
    default:
        return DefWindowProcA(w, m, a, l);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int s) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.lpszClassName = "TinyView";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);
    
    HWND w = CreateWindowA("TinyView", "TinyView",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 
        600, 400, 0, 0, h, 0);
    
    ShowWindow(w, s);
    
    // Load file from command line if provided
    if (cmd && *cmd) {
        // Skip past the program name if it's the first argument
        if (cmd[0] == '\"') {
            // Quoted filename - find closing quote
            char* endQuote = strchr(cmd + 1, '\"');
            if (endQuote) cmd = endQuote + 1;
        } else {
            // Unquoted filename - find space
            char* space = strchr(cmd, ' ');
            if (space) cmd = space;
        }
        
        // Skip whitespace
        while (*cmd == ' ') cmd++;
        
        if (*cmd) {
            LoadFile(cmd);
        }
    }
    
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    return msg.wParam;
}