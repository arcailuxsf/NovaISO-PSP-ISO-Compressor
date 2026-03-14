#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <process.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "iso_logic.h"
#include "cso_logic.h"
#include "cso_metadata.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;

// --- Visual Constants ---
const Color COLOR_BG(255, 10, 10, 10);
const Color COLOR_CARD(255, 22, 22, 22);
const Color COLOR_ACCENT(255, 0, 120, 215);
const Color COLOR_TEXT(255, 240, 240, 240);
const Color COLOR_SUBTEXT(255, 160, 160, 160);

#define ID_BTN_LOAD 2001
#define ID_BTN_ACTION 2002
#define ID_COMBO_LEVEL 2003

#define WM_PROG_UPDATE (WM_USER + 10)
#define WM_PROG_DONE (WM_USER + 11)

// --- Global App State ---
struct AppUI {
    HWND hwnd;
    HWND hBtnLoad, hBtnAction;
    HWND hComboLevel;
    
    std::string pathIn, pathOut;
    GameMetadata meta;
    Image* pIcon = nullptr;
    bool isProcessing = false;
    float currentProgress = 0.0f;
    int compressionLevel = 6;
    
    // Timing and Speed
    double startTime = 0;
    std::wstring statsText;
} g_ui;

// --- Helper: String Conversion ---
std::wstring ToWString(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

// --- Background Job ---
struct JobParams {
    std::string in, out;
    int level;
    bool isCompress;
};

void __cdecl CompressionThread(void* p) {
    JobParams* jp = (JobParams*)p;
    g_ui.startTime = GetTickCount64() / 1000.0;
    
    auto cb = [](float progress) {
        g_ui.currentProgress = progress;
        
        double currentTime = GetTickCount64() / 1000.0;
        double elapsed = currentTime - g_ui.startTime;
        
        if (progress > 0.01f && elapsed > 0.5f) {
            double total_size_mb = (double)g_ui.meta.size / (1024.0 * 1024.0);
            double speed = (total_size_mb * progress) / elapsed;
            double remaining = (elapsed / progress) - elapsed;
            
            wchar_t buf[256];
            swprintf(buf, L"Speed: %.2f MB/s  |  Remaining: %d s", speed, (int)remaining);
            g_ui.statsText = buf;
        } else {
            g_ui.statsText = L"Calculating...";
        }
        
        PostMessage(g_ui.hwnd, WM_PROG_UPDATE, 0, 0);
    };

    bool success = false;
    if (jp->isCompress) success = CsoProcessor::compress(jp->in, jp->out, jp->level, cb);
    else success = CsoProcessor::decompress(jp->in, jp->out, cb);

    PostMessage(g_ui.hwnd, WM_PROG_DONE, (WPARAM)success, 0);
    delete jp;
}

// --- Custom Painting ---
void DrawModernUI(HDC hdc) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RECT clientRect; GetClientRect(g_ui.hwnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom;

    // Background
    SolidBrush bgBrush(COLOR_BG);
    g.FillRectangle(&bgBrush, 0, 0, width, height);

    // Header Area
    FontFamily ffTitle(L"Segoe UI");
    Font fTitle(&ffTitle, 32, FontStyleBold, UnitPixel);
    SolidBrush whiteBrush(COLOR_TEXT);
    g.DrawString(L"NovaISO", -1, &fTitle, PointF(30, 30), &whiteBrush);

    Font fSub(&ffTitle, 14, FontStyleRegular, UnitPixel);
    SolidBrush grayBrush(COLOR_SUBTEXT);
    g.DrawString(L"Modern PSP ISO/CSO Compressor", -1, &fSub, PointF(32, 75), &grayBrush);

    // Main Card
    Rect cardRect(30, 110, width - 60, 300);
    SolidBrush cardBrush(COLOR_CARD);
    Pen borderPen(Color(255, 45, 45, 45), 1);
    g.FillRectangle(&cardBrush, cardRect);
    g.DrawRectangle(&borderPen, cardRect);

    if (g_ui.pathIn.empty()) {
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Drag & Drop your ISO/CSO here\nor click 'Load File' to start", -1, &fSub, 
                   RectF(30, 110, width-60, 300), &format, &grayBrush);
    } else {
        // Icon
        if (g_ui.pIcon) {
            g.DrawImage(g_ui.pIcon, 60, 140, 240, 136);
        }

        // Meta Info
        Font fGameTitle(&ffTitle, 22, FontStyleBold, UnitPixel);
        std::wstring wTitle = ToWString(g_ui.meta.title);
        g.DrawString(wTitle.c_str(), -1, &fGameTitle, PointF(320, 145), &whiteBrush);

        Font fGameInfo(&ffTitle, 16, FontStyleRegular, UnitPixel);
        g.DrawString((L"ID: " + ToWString(g_ui.meta.id)).c_str(), -1, &fGameInfo, PointF(320, 185), &grayBrush);
        g.DrawString((L"Region: " + ToWString(g_ui.meta.region)).c_str(), -1, &fGameInfo, PointF(320, 215), &grayBrush);
        g.DrawString((L"Size: " + std::to_wstring(g_ui.meta.size / (1024*1024)) + L" MB").c_str(), -1, &fGameInfo, PointF(320, 245), &grayBrush);
        g.DrawString((L"Format: " + ToWString(g_ui.meta.format)).c_str(), -1, &fGameInfo, PointF(320, 275), &grayBrush);
    }

    // Compression Level Label
    if (g_ui.meta.format == "ISO") {
        g.DrawString(L"Compression Level:", -1, &fSub, PointF(30, 428), &whiteBrush);
    }

    if (g_ui.isProcessing) {
        g.DrawString(g_ui.statsText.c_str(), -1, &fSub, PointF(30, 455), &grayBrush);
    }

    // Progress Bar
    Rect prgBack(30, 485, width - 60, 6);
    SolidBrush prgBackBrush(Color(255, 30, 30, 30));
    g.FillRectangle(&prgBackBrush, prgBack);

    if (g_ui.currentProgress > 0) {
        Rect prgFill(30, 485, (int)((width - 60) * g_ui.currentProgress), 6);
        SolidBrush prgFillBrush(COLOR_ACCENT);
        g.FillRectangle(&prgFillBrush, prgFill);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int width = rc.right;

        NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
        HFONT hFont = CreateFontIndirect(&ncm.lfMessageFont);

        g_ui.hBtnLoad = CreateWindow("BUTTON", "LOAD FILE", WS_CHILD | WS_VISIBLE | BS_FLAT,
            30, 500, 180, 45, hwnd, (HMENU)ID_BTN_LOAD, NULL, NULL);
        SendMessage(g_ui.hBtnLoad, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_ui.hBtnAction = CreateWindow("BUTTON", "PROCESS", WS_CHILD | WS_VISIBLE | BS_FLAT | WS_DISABLED,
            width - 210, 500, 180, 45, hwnd, (HMENU)ID_BTN_ACTION, NULL, NULL);
        SendMessage(g_ui.hBtnAction, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Compression Level Combo
        g_ui.hComboLevel = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            165, 425, 140, 300, hwnd, (HMENU)ID_COMBO_LEVEL, NULL, NULL);
        SendMessage(g_ui.hComboLevel, WM_SETFONT, (WPARAM)hFont, TRUE);
        for(int i=1; i<=9; ++i) {
            std::string s = "Level " + std::to_string(i) + (i==6 ? " (Default)" : "");
            SendMessage(g_ui.hComboLevel, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        }
        SendMessage(g_ui.hComboLevel, CB_SETCURSEL, 5, 0);

        DragAcceptFiles(hwnd, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_COMBO_LEVEL && HIWORD(wParam) == CBN_SELCHANGE) {
            g_ui.compressionLevel = (int)SendMessage(g_ui.hComboLevel, CB_GETCURSEL, 0, 0) + 1;
            return 0;
        }
        if (id == ID_BTN_LOAD) {
            char szFile[MAX_PATH] = {0};
            OPENFILENAME ofn = {sizeof(OPENFILENAME)};
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "PSP Files\0*.iso;*.cso\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)) {
                g_ui.pathIn = szFile;
                std::string fmt = IsoProcessor::detect_format(szFile);
                if (fmt == "ISO") g_ui.meta = IsoProcessor::get_metadata(szFile);
                else if (fmt == "CSO") g_ui.meta = CsoMetadataProcessor::get_metadata(szFile);
                
                if (g_ui.pIcon) delete g_ui.pIcon; g_ui.pIcon = nullptr;
                if (!g_ui.meta.icon_data.empty()) {
                    IStream* s = SHCreateMemStream(g_ui.meta.icon_data.data(), g_ui.meta.icon_data.size());
                    if (s) { g_ui.pIcon = Image::FromStream(s); s->Release(); }
                }

                bool is_iso = (g_ui.meta.format == "ISO");
                ShowWindow(g_ui.hComboLevel, is_iso ? SW_SHOW : SW_HIDE);
                
                EnableWindow(g_ui.hBtnAction, TRUE);
                SetWindowText(g_ui.hBtnAction, is_iso ? "COMPRESS TO CSO" : "DECOMPRESS TO ISO");
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        if (id == ID_BTN_ACTION) {
            char szSave[MAX_PATH] = {0};
            strcpy(szSave, g_ui.pathIn.c_str());
            PathRemoveExtension(szSave);
            strcat(szSave, g_ui.meta.format == "ISO" ? ".cso" : ".iso");

            OPENFILENAME ofn = {sizeof(OPENFILENAME)};
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = g_ui.meta.format == "ISO" ? "CSO Files\0*.cso\0" : "ISO Files\0*.iso\0";
            ofn.lpstrFile = szSave;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileName(&ofn)) {
                JobParams* jp = new JobParams();
                jp->in = g_ui.pathIn; jp->out = szSave;
                jp->level = g_ui.compressionLevel;
                jp->isCompress = (g_ui.meta.format == "ISO");
                g_ui.isProcessing = true;
                g_ui.currentProgress = 0;
                EnableWindow(g_ui.hBtnLoad, FALSE);
                EnableWindow(g_ui.hBtnAction, FALSE);
                EnableWindow(g_ui.hComboLevel, FALSE);
                _beginthread(CompressionThread, 0, jp);
            }
        }
        return 0;
    }

    case WM_DROPFILES: {
        char szFile[MAX_PATH];
        DragQueryFile((HDROP)wParam, 0, szFile, MAX_PATH);
        g_ui.pathIn = szFile;
        std::string fmt = IsoProcessor::detect_format(szFile);
        if (fmt == "ISO") g_ui.meta = IsoProcessor::get_metadata(szFile);
        else if (fmt == "CSO") g_ui.meta = CsoMetadataProcessor::get_metadata(szFile);

        if (g_ui.pIcon) delete g_ui.pIcon; g_ui.pIcon = nullptr;
        if (!g_ui.meta.icon_data.empty()) {
            IStream* s = SHCreateMemStream(g_ui.meta.icon_data.data(), g_ui.meta.icon_data.size());
            if (s) { g_ui.pIcon = Image::FromStream(s); s->Release(); }
        }

        bool is_iso = (g_ui.meta.format == "ISO");
        ShowWindow(g_ui.hComboLevel, is_iso ? SW_SHOW : SW_HIDE);

        EnableWindow(g_ui.hBtnAction, TRUE);
        SetWindowText(g_ui.hBtnAction, is_iso ? "COMPRESS TO CSO" : "DECOMPRESS TO ISO");
        InvalidateRect(hwnd, NULL, TRUE);
        DragFinish((HDROP)wParam);
        return 0;
    }

    case WM_PROG_UPDATE: InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd); return 0;

    case WM_PROG_DONE:
        g_ui.isProcessing = false;
        g_ui.currentProgress = 0;
        g_ui.statsText = L"";
        EnableWindow(g_ui.hBtnLoad, TRUE);
        EnableWindow(g_ui.hBtnAction, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        if (wParam) MessageBox(hwnd, "Operation successful!", "NovaISO", MB_ICONINFORMATION);
        else MessageBox(hwnd, "Operation failed!", "Error", MB_ICONERROR);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC memDC = CreateCompatibleDC(hdc);
        RECT rc; GetClientRect(hwnd, &rc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        SelectObject(memDC, memBM);
        DrawModernUI(memDC);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORBTN: return (LRESULT)CreateSolidBrush(RGB(30, 30, 30));
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    GdiplusStartupInput gsi; ULONG_PTR gToken;
    GdiplusStartup(&gToken, &gsi, NULL);
    InitCommonControls();

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "NovaISOMain";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    // Load Icon for both window and taskbar
    wc.hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON, 
                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(1), IMAGE_ICON, 
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    
    RegisterClassEx(&wc);

    g_ui.hwnd = CreateWindowEx(WS_EX_LAYERED, "NovaISOMain", "NovaISO", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, 
        NULL, NULL, hInst, NULL);
    
    SetLayeredWindowAttributes(g_ui.hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_ui.hwnd, nShow);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    GdiplusShutdown(gToken);
    return 0;
}
