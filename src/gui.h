#ifndef GUI_H
#define GUI_H

#include <windows.h>
#include <string>
#include <vector>
#include "iso_logic.h"

class NovaGui {
public:
    NovaGui();
    ~NovaGui();
    bool Create(HINSTANCE hInstance);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hwnd);
    void OnDropFiles(HDROP hDrop);
    void OnSize(int width, int height);
    void UpdateMetadata(const std::string& path);
    void StartProcessing();

    HWND m_hwnd;
    HWND m_btnLoad, m_btnAction;
    HWND m_progress;
    GameMetadata m_meta;
    std::string m_currentPath;
    bool m_isProcessing;
    float m_progressVal;
    
    // GDI+ Image pointer (as void* to avoid including gdiplus in header)
    void* m_pIcon;
};

#endif
