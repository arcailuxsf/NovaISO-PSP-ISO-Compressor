#include "gui.h"
#include <gdiplus.h>
#include <commctrl.h>
#include <shlobj.h>
#include <process.h>
#include <iostream>
#include <shlwapi.h>
#include <objbase.h>
#include "cso_logic.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

NovaGui* g_pApp = nullptr;

NovaGui::NovaGui() : m_hwnd(NULL), m_isProcessing(false), m_progressVal(0), m_pIcon(NULL) {
    g_pApp = this;
}

NovaGui::~NovaGui() {
    if (m_pIcon) delete (Image*)m_pIcon;
}

bool NovaGui::Create(HINSTANCE hInstance) {
    // Init GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "NovaISOClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 15, 15));
    RegisterClass(&wc);

    m_hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, "NovaISOClass", "NovaISO Toolkit C++", 
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650, 
                           NULL, NULL, hInstance, NULL);

    if (!m_hwnd) return false;

    // Controls
    m_btnLoad = CreateWindow("BUTTON", "Cargar ISO/CSO", WS_VISIBLE | WS_CHILD | BS_FLAT, 
                            20, 20, 180, 45, m_hwnd, (HMENU)101, hInstance, NULL);
    
    m_btnAction = CreateWindow("BUTTON", "PROCESAR", WS_VISIBLE | WS_CHILD | BS_FLAT, 
                              250, 520, 700, 60, m_hwnd, (HMENU)102, hInstance, NULL);
    EnableWindow(m_btnAction, FALSE);

    m_progress = CreateWindow(PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 
                             250, 480, 700, 15, m_hwnd, NULL, hInstance, NULL);
    SendMessage(m_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

LRESULT CALLBACK NovaGui::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: g_pApp->OnPaint(hwnd); return 0;
        case WM_DROPFILES: g_pApp->OnDropFiles((HDROP)wParam); return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == 101) { // Load
                char szFile[MAX_PATH] = {0};
                OPENFILENAME ofn = {sizeof(OPENFILENAME)};
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "PSP Files\0*.iso;*.cso\0All Files\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileName(&ofn)) g_pApp->UpdateMetadata(szFile);
            } else if (LOWORD(wParam) == 102) { // Action
                g_pApp->StartProcessing();
            }
            return 0;
        case WM_SIZE: g_pApp->OnSize(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void NovaGui::OnSize(int w, int h) {
    // Responsive adjustments if needed
}

void NovaGui::OnDropFiles(HDROP hDrop) {
    char szFile[MAX_PATH];
    DragQueryFile(hDrop, 0, szFile, MAX_PATH);
    UpdateMetadata(szFile);
    DragFinish(hDrop);
}

void NovaGui::UpdateMetadata(const std::string& path) {
    m_currentPath = path;
    m_meta = IsoProcessor::get_metadata(path);
    
    if (m_pIcon) { delete (Image*)m_pIcon; m_pIcon = NULL; }
    
    if (!m_meta.icon_data.empty()) {
        IStream* pStream = SHCreateMemStream(m_meta.icon_data.data(), m_meta.icon_data.size());
        if (pStream) {
            m_pIcon = Image::FromStream(pStream);
            pStream->Release();
        }
    }
    
    EnableWindow(m_btnAction, TRUE);
    SendMessage(m_btnAction, WM_SETTEXT, 0, (LPARAM)(m_meta.format == "ISO" ? "CONVERTIR A CSO" : "DESCOMPRIMIR A ISO"));
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void NovaGui::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    FontFamily titleFamily(L"Segoe UI");

    // Sidebar background
    SolidBrush sideBrush(Color(255, 20, 20, 20));
    g.FillRectangle(&sideBrush, Rect(0, 0, 220, 1000));

    // Title NovaISO
    FontFamily fontFamily(L"Impact");
    Font font(&fontFamily, 40, FontStyleRegular, UnitPixel);
    SolidBrush whiteBrush(Color(255, 240, 240, 240));
    g.DrawString(L"NovaISO", -1, &font, PointF(30, 80), &whiteBrush);

    // Main Card area
    Rect cardRect(250, 20, 700, 440);
    SolidBrush cardBrush(Color(255, 18, 18, 18));
    Pen borderPen(Color(255, 45, 45, 45), 1);
    g.FillRectangle(&cardBrush, cardRect);
    g.DrawRectangle(&borderPen, cardRect);

    if (!m_currentPath.empty()) {
        // Icon
        if (m_pIcon) {
            g.DrawImage((Image*)m_pIcon, 250 + (700-240)/2, 60, 240, 140);
        }

        // Title
        Font titleFont(&titleFamily, 24, FontStyleBold, UnitPixel);
        std::wstring wTitle(m_meta.title.begin(), m_meta.title.end());
        RectF layoutRect((REAL)250, 220.0f, 700.0f, 40.0f);
        g.DrawString(wTitle.c_str(), -1, &titleFont, layoutRect, &format, &whiteBrush);

        // Stats
        Font statFont(&titleFamily, 16, FontStyleRegular, UnitPixel);
        // ID Badge
        Rect idRect(250 + 100, 280, 120, 35);
        SolidBrush blackBrush(Color(255, 0, 0, 0));
        g.FillRectangle(&blackBrush, idRect);
        std::wstring wId(m_meta.id.begin(), m_meta.id.end());
        g.DrawString(wId.c_str(), -1, &statFont, RectF(350, 285, 120, 30), &format, &whiteBrush);

        // Size & Region
        std::wstring wSize = L"Size: " + std::to_wstring(m_meta.size / (1024 * 1024)) + L" MB";
        std::wstring wReg = L"Region: ";
        for (char c : m_meta.region) wReg += (wchar_t)c;
        g.DrawString(wSize.c_str(), -1, &statFont, PointF(500, 288), &whiteBrush);
        g.DrawString(wReg.c_str(), -1, &statFont, PointF(700, 288), &whiteBrush);
    } else {
        Font hintFont(&titleFamily, 20, FontStyleRegular, UnitPixel);
        g.DrawString(L"Arrastre su ISO aquí", -1, &hintFont, RectF(250, 200, 700, 40), &format, &whiteBrush);
    }

    EndPaint(hwnd, &ps);
}

struct ThreadParams {
    std::string in, out;
    int mode;
    HWND hwnd;
    HWND prog;
};

void __cdecl CompressionThread(void* p) {
    ThreadParams* tp = (ThreadParams*)p;
    auto cb = [tp](float p) {
        PostMessage(tp->hwnd, WM_USER + 1, (WPARAM)(p * 100), 0);
    };

    bool ok = false;
    if (tp->mode > 0) ok = CsoProcessor::compress(tp->in, tp->out, tp->mode, cb);
    else ok = CsoProcessor::decompress(tp->in, tp->out, cb);

    if (ok) MessageBox(tp->hwnd, "¡Proceso terminado con éxito!", "NovaISO", MB_ICONINFORMATION);
    else MessageBox(tp->hwnd, "Error en el proceso", "Error", MB_ICONERROR);

    PostMessage(tp->hwnd, WM_USER + 2, 0, 0);
    delete tp;
}

void NovaGui::StartProcessing() {
    char szSave[MAX_PATH] = {0};
    strcpy(szSave, m_meta.title.c_str());
    strcat(szSave, m_meta.format == "ISO" ? ".cso" : ".iso");

    OPENFILENAME ofn = {sizeof(OPENFILENAME)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = m_meta.format == "ISO" ? "CSO Files\0*.cso\0" : "ISO Files\0*.iso\0";
    ofn.lpstrFile = szSave;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = m_meta.format == "ISO" ? "cso" : "iso";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (GetSaveFileName(&ofn)) {
        m_isProcessing = true;
        EnableWindow(m_btnAction, FALSE);
        EnableWindow(m_btnLoad, FALSE);

        ThreadParams* tp = new ThreadParams();
        tp->in = m_currentPath;
        tp->out = szSave;
        tp->mode = (m_meta.format == "ISO" ? 9 : 0);
        tp->hwnd = m_hwnd;
        tp->prog = m_progress;

        _beginthread(CompressionThread, 0, tp);
    }
}

int NovaGui::Run() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_USER + 1) {
            SendMessage(m_progress, PBM_SETPOS, msg.wParam, 0);
        } else if (msg.message == WM_USER + 2) {
            m_isProcessing = false;
            EnableWindow(m_btnAction, TRUE);
            EnableWindow(m_btnLoad, TRUE);
            SendMessage(m_progress, PBM_SETPOS, 0, 0);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
