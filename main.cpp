#include <windows.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <fstream>
#include <tlhelp32.h>
#include <cstdint>
#include <commctrl.h>
#include <mmsystem.h>
#include <gdiplus.h>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

using namespace std;
using namespace Gdiplus;

// --- CONFIGURAÇÕES DE MEMÓRIA ---
const uintptr_t ADDR_GAMEMODE_PTR = 0x00E8E304;
const uintptr_t OFF_CWORLD        = 0xCC;
const uintptr_t OFF_LIST_START    = 0x10;
const uintptr_t OFF_NEXT_NODE     = 0x00;
const uintptr_t OFF_ENTITY_PTR    = 0x08;
const uintptr_t OFF_MOB_ID        = 0x10C;
const char* WINDOW_TITLE          = "Myth of Yggdrasil";

struct Target { string label; uint32_t id; };

// --- GLOBAIS ---
vector<Target> g_Targets;
int g_Delay = 200;
WORD g_TpKeys[3] = { VK_F5, VK_F6, VK_F7 };
WORD g_ToggleKey = VK_F4;
bool g_BotAtivo = false;
bool g_SearchAtivo = false;
HANDLE g_hProcess = NULL;

// Handles de controles
HWND g_hMainWnd, g_hList, g_hInput, g_hDelay;
HWND g_hTpWnd[3], g_hToggleWnd;
HWND g_hStatusText, g_hBtnAtivar, g_hBtnAdd, g_hBtnRemover;
HWND g_hChkMinimize;
HFONT g_hFontUI = NULL;
HFONT g_hFontMono = NULL;
HFONT g_hFontSmall = NULL;
ULONG_PTR g_gdiplusToken;

// Paleta de cores (dark theme)
const COLORREF CLR_BG         = RGB(14, 15, 20);
const COLORREF CLR_SURFACE    = RGB(22, 24, 32);
const COLORREF CLR_SURFACE2   = RGB(30, 33, 44);
const COLORREF CLR_BORDER     = RGB(48, 52, 70);
const COLORREF CLR_ACCENT     = RGB(82, 140, 255);
const COLORREF CLR_ACCENT2    = RGB(110, 230, 180);
const COLORREF CLR_TEXT       = RGB(220, 225, 240);
const COLORREF CLR_TEXTMUTED  = RGB(110, 115, 145);
const COLORREF CLR_DANGER     = RGB(255, 90, 100);
const COLORREF CLR_SUCCESS    = RGB(60, 210, 140);
const COLORREF CLR_WARNING    = RGB(255, 185, 60);

// Subclass para captura de tecla
LRESULT CALLBACK KeyInputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uId, DWORD_PTR) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        WORD vKey = (WORD)wp;
        char name[64];
        UINT sc = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
        switch (vKey) {
            case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
            case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
            case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
                sc |= 0x100;
        }
        string keyName = "Key";
        if (GetKeyNameTextA(sc << 16, name, 64)) keyName = name;
        if (uId == 99) g_ToggleKey = vKey;
        else g_TpKeys[uId] = vKey;
        SetWindowText(hwnd, keyName.c_str());
        return 0;
    }
    if (msg == WM_CHAR) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

string KeyCodeToString(WORD vKey) {
    char name[64];
    UINT scanCode = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    switch (vKey) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100;
    }
    if (GetKeyNameTextA(scanCode << 16, name, 64)) return string(name);
    return "Key " + to_string(vKey);
}

void SendKey(WORD vKey) {
    INPUT in[2] = {0};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vKey;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vKey; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

bool ScanMemory() {
    if (!g_hProcess) return false;
    uint32_t gameMode = 0, world = 0, list = 0, node = 0;
    ReadProcessMemory(g_hProcess, (LPCVOID)ADDR_GAMEMODE_PTR, &gameMode, 4, NULL);
    if (!gameMode) return false;
    ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)(gameMode + OFF_CWORLD), &world, 4, NULL);
    ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)(world + OFF_LIST_START), &list, 4, NULL);
    ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)(list + OFF_NEXT_NODE), &node, 4, NULL);
    uintptr_t first = node;
    for (int i = 0; i < 500 && node != 0; i++) {
        uint32_t entity = 0, id = 0;
        ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)(node + OFF_ENTITY_PTR), &entity, 4, NULL);
        if (entity) {
            ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)(entity + OFF_MOB_ID), &id, 4, NULL);
            for (const auto& t : g_Targets) if (t.id == id) return true;
        }
        ReadProcessMemory(g_hProcess, (LPCVOID)(uintptr_t)node, &node, 4, NULL);
        if (node == first) break;
    }
    return false;
}

// Helper: preenche retângulo com cor GDI
void FillRectColor(HDC hdc, RECT r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

// Helper: desenha borda arredondada via GDI+ sobre HDC
void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius,
                   COLORREF fillColor, COLORREF borderColor, int borderW = 1) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    GraphicsPath path;
    path.AddArc(x, y, radius*2, radius*2, 180, 90);
    path.AddArc(x + w - radius*2, y, radius*2, radius*2, 270, 90);
    path.AddArc(x + w - radius*2, y + h - radius*2, radius*2, radius*2, 0, 90);
    path.AddArc(x, y + h - radius*2, radius*2, radius*2, 90, 90);
    path.CloseFigure();

    SolidBrush fill(Color(GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor)));
    g.FillPath(&fill, &path);

    Pen pen(Color(GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)), (REAL)borderW);
    g.DrawPath(&pen, &path);
}

// Desenha texto com GDI+ (antialiased)
void DrawTextGP(HDC hdc, const wchar_t* text, int x, int y, int w, int h,
                COLORREF color, float fontSize, bool bold = false, int align = StringAlignmentNear) {
    Graphics g(hdc);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff(L"Segoe UI");
    Font font(&ff, fontSize, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(Color(GetRValue(color), GetGValue(color), GetBValue(color)));

    StringFormat sf;
    sf.SetAlignment((StringAlignment)align);
    sf.SetLineAlignment(StringAlignmentCenter);

    RectF rect((REAL)x, (REAL)y, (REAL)w, (REAL)h);
    g.DrawString(text, -1, &font, rect, &sf, &brush);
}

// WM_CTLCOLORSTATIC - faz os STATIC ficarem com bg transparente
HBRUSH g_BgBrush = NULL;

// Repaint customizado da janela principal
void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Background principal
    FillRectColor(hdc, rc, CLR_BG);

    // Header bar
    RECT header = {0, 0, W, 48};
    FillRectColor(hdc, header, CLR_SURFACE);

    // Linha divisória header
    RECT divH = {0, 48, W, 49};
    FillRectColor(hdc, divH, CLR_BORDER);

    // Título no header (GDI+ antialiased)
    DrawTextGP(hdc, L"TELEDOR HONESTO", 16, 0, 220, 48, CLR_ACCENT, 13.0f, true);
    DrawTextGP(hdc, L"MVP TRACKER", 236, 0, 140, 48, CLR_TEXTMUTED, 11.0f, false);

    // Versão
    DrawTextGP(hdc, L"v2.0", W - 52, 0, 44, 48, CLR_TEXTMUTED, 10.0f);

    // Painel esquerdo - Alvos
    DrawRoundRect(hdc, 12, 58, 220, 26, 4, CLR_SURFACE2, CLR_BORDER);
    DrawTextGP(hdc, L"ALVOS (NOME:ID)", 18, 58, 170, 26, CLR_TEXTMUTED, 9.5f, true);

    // Label Alvos list
    DrawRoundRect(hdc, 12, 118, 220, 170, 6, CLR_SURFACE, CLR_BORDER);

    // Painel direito - Configurações
    DrawRoundRect(hdc, 244, 58, 230, 316, 6, CLR_SURFACE, CLR_BORDER);

    // Labels internos do painel direito
    DrawTextGP(hdc, L"CONFIGURAÇÕES", 254, 62, 210, 20, CLR_TEXTMUTED, 9.0f, true);

    DrawTextGP(hdc, L"Delay (ms)", 254, 86, 100, 18, CLR_TEXTMUTED, 9.5f);
    DrawTextGP(hdc, L"Teclas de Teleporte", 254, 130, 200, 18, CLR_TEXTMUTED, 9.5f);
    DrawTextGP(hdc, L"Tecla Toggle (Global)", 254, 186, 200, 18, CLR_TEXTMUTED, 9.5f);

    EndPaint(hwnd, &ps);
}

// --- SAVE / LOAD ---
void SaveConfig() {
    ofstream f("config.dat");
    f << g_Delay << " " << g_TpKeys[0] << " " << g_TpKeys[1] << " " << g_TpKeys[2] << " " << g_ToggleKey << endl;
    for (const auto& t : g_Targets) f << t.label << ":" << t.id << endl;
}

void LoadConfig() {
    ifstream f("config.dat"); string line;
    if (f >> g_Delay >> g_TpKeys[0] >> g_TpKeys[1] >> g_TpKeys[2] >> g_ToggleKey) {
        char buf[10]; sprintf(buf, "%d", g_Delay); SetWindowText(g_hDelay, buf);
        for (int i = 0; i < 3; i++) SetWindowText(g_hTpWnd[i], KeyCodeToString(g_TpKeys[i]).c_str());
        SetWindowText(g_hToggleWnd, KeyCodeToString(g_ToggleKey).c_str());
        while (f >> line) {
            size_t p = line.find(':');
            if (p != string::npos) g_Targets.push_back({line.substr(0,p), (uint32_t)stoul(line.substr(p+1))});
        }
        SendMessage(g_hList, LB_RESETCONTENT, 0, 0);
        for (const auto& t : g_Targets) {
            string s = t.label + "  :  " + to_string(t.id);
            SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        }
    }
}

// --- BOT LOOP ---
DWORD WINAPI BotLoop(LPVOID) {
    int currentIdx = 0;

    while (true) {
        // Toggle
        if (GetAsyncKeyState(g_ToggleKey) & 0x0001) {
            if (g_BotAtivo) {
                g_SearchAtivo = !g_SearchAtivo;
                if (!g_SearchAtivo) currentIdx = 0; // reseta ao parar
                SetWindowText(g_hStatusText, g_SearchAtivo ? "PROCURANDO..." : "PARADO");
                if (g_SearchAtivo) PlaySound(TEXT("sound/altert.wav"), NULL, SND_FILENAME | SND_ASYNC);
                else               PlaySound(TEXT("sound/stop.wav"),   NULL, SND_FILENAME | SND_ASYNC);
            }
        }

        if (g_BotAtivo && g_SearchAtivo) {
            if (GetForegroundWindow() == FindWindowA(NULL, WINDOW_TITLE)) {

                // Manda o TP atual
                SendKey(g_TpKeys[currentIdx]);

                // Espera o delay (tempo do TP processar antes de scannar)
                Sleep(g_Delay);

                // Scanna
                if (ScanMemory()) {
                    g_SearchAtivo = false;
                    currentIdx = 0;
                    SetWindowText(g_hStatusText, "ALVO ENCONTRADO!");
                    PlaySound(TEXT("sound/mvp.wav"), NULL, SND_FILENAME | SND_ASYNC);
                    Sleep(30);
                    continue;
                }

                currentIdx = (currentIdx + 1) % 3;
            }
        }

        Sleep(30);
    }
    return 0;
}

// Estilo de controles customizados (subclass genérica p/ edits e botões)
HBRUSH hSurface2Brush = NULL;
HBRUSH hSurfaceBrush  = NULL;
HBRUSH hAccentBrush   = NULL;
HBRUSH hDangerBrush   = NULL;

// Subclass para botões owner-draw não precisamos; usamos WM_DRAWITEM
// Usamos BS_OWNERDRAW para os botões principais

void DrawButton(LPDRAWITEMSTRUCT dis, bool isActive, bool isDanger, const char* label) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    int rx = rc.left, ry = rc.top;
    int rw = rc.right - rc.left;
    int rh = rc.bottom - rc.top;

    COLORREF bg, border, textC;
    if (isDanger) {
        bg     = pressed ? RGB(180, 50, 60) : CLR_DANGER;
        border = RGB(255, 120, 130);
        textC  = RGB(255, 255, 255);
    } else if (isActive) {
        bg     = pressed ? RGB(50, 110, 200) : CLR_ACCENT;
        border = RGB(130, 175, 255);
        textC  = RGB(255, 255, 255);
    } else {
        bg     = pressed ? CLR_SURFACE : CLR_SURFACE2;
        border = CLR_BORDER;
        textC  = CLR_TEXT;
    }

    DrawRoundRect(hdc, rx + 1, ry + 1, rw - 2, rh - 2, 6, bg, border, 1);

    // Text
    wchar_t wLabel[64];
    MultiByteToWideChar(CP_ACP, 0, label, -1, wLabel, 64);
    DrawTextGP(hdc, wLabel, rx, ry, rw, rh, textC, 10.5f, true, StringAlignmentCenter);
}

// WNDPROC
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // Fonts
        g_hFontUI   = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hFontMono = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
        g_hFontSmall = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

        hSurface2Brush = CreateSolidBrush(CLR_SURFACE2);
        hSurfaceBrush  = CreateSolidBrush(CLR_SURFACE);
        hAccentBrush   = CreateSolidBrush(CLR_ACCENT);
        hDangerBrush   = CreateSolidBrush(CLR_DANGER);
        g_BgBrush      = CreateSolidBrush(CLR_BG);

        // --- Input alvo ---
        g_hInput = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_LEFT,
            12, 88, 160, 24, hwnd, NULL, NULL, NULL);
        SendMessage(g_hInput, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        // Botão ADD
        g_hBtnAdd = CreateWindow("BUTTON", "ADD",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            176, 88, 56, 24, hwnd, (HMENU)1, NULL, NULL);

        // ListBox alvos
        g_hList = CreateWindowEx(0, "LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            12, 118, 220, 170, hwnd, (HMENU)2, NULL, NULL);
        SendMessage(g_hList, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        // Botão Remover
        g_hBtnRemover = CreateWindow("BUTTON", "REMOVER SELECIONADO",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            12, 295, 220, 28, hwnd, (HMENU)3, NULL, NULL);

        // --- Painel direito ---
        // Delay
        g_hDelay = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "200",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            354, 86, 108, 24, hwnd, NULL, NULL, NULL);
        SendMessage(g_hDelay, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        // Teclas teleporte
        for (int i = 0; i < 3; i++) {
            g_hTpWnd[i] = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT",
                KeyCodeToString(g_TpKeys[i]).c_str(),
                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
                254 + i * 68, 150, 62, 24, hwnd, NULL, NULL, NULL);
            SendMessage(g_hTpWnd[i], WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            SetWindowSubclass(g_hTpWnd[i], KeyInputSubclass, i, 0);
        }

        // Tecla Toggle
        g_hToggleWnd = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT",
            KeyCodeToString(g_ToggleKey).c_str(),
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
            254, 206, 218, 24, hwnd, NULL, NULL, NULL);
        SendMessage(g_hToggleWnd, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
        SetWindowSubclass(g_hToggleWnd, KeyInputSubclass, 99, 0);

        // Status text
        g_hStatusText = CreateWindow("STATIC", "PARADO",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            244, 238, 232, 22, hwnd, NULL, NULL, NULL);
        SendMessage(g_hStatusText, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        // Checkbox minimizar
        g_hChkMinimize = CreateWindow("BUTTON", "  Minimizar ao fechar",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            248, 265, 200, 20, hwnd, (HMENU)5, NULL, NULL);
        SendMessage(g_hChkMinimize, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        // Botão ATIVAR
        g_hBtnAtivar = CreateWindow("BUTTON", "ATIVAR",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            248, 292, 226, 54, hwnd, (HMENU)6, NULL, NULL);

        LoadConfig();
        break;
    }

    case WM_PAINT:
        PaintMain(hwnd);
        return 0;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        char label[64];
        GetWindowText(dis->hwndItem, label, 64);
        int id = LOWORD(wp);
        bool isDanger = (id == 3);
        bool isActive = (id == 6 && g_BotAtivo) || (id == 1);
        DrawButton(dis, isActive, isDanger, label);
        return TRUE;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_SURFACE2);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)hSurface2Brush;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_SURFACE);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)hSurfaceBrush;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND hCtrl = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (hCtrl == g_hStatusText) {
            char buf[64]; GetWindowText(hCtrl, buf, 64);
            string s(buf);
            if (s.find("ENCONTRADO") != string::npos)
                SetTextColor(hdc, CLR_SUCCESS);
            else if (s.find("PROCURANDO") != string::npos)
                SetTextColor(hdc, CLR_WARNING);
            else
                SetTextColor(hdc, CLR_TEXTMUTED);
        } else {
            SetTextColor(hdc, CLR_TEXTMUTED);
        }
        return (LRESULT)g_BgBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_BgBrush;
    }

    case WM_COMMAND: {
        if (LOWORD(wp) == 1) { // ADD
            char buf[128]; GetWindowText(g_hInput, buf, 128);
            string s = buf; size_t p = s.find(':');
            if (p != string::npos) {
                g_Targets.push_back({s.substr(0, p), (uint32_t)stoul(s.substr(p+1))});
                string display = s.substr(0, p) + "  :  " + s.substr(p+1);
                SendMessage(g_hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
                SetWindowText(g_hInput, "");
                SaveConfig();
            }
        }
        if (LOWORD(wp) == 3) { // REMOVER
            int sel = SendMessage(g_hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                g_Targets.erase(g_Targets.begin() + sel);
                SendMessage(g_hList, LB_DELETESTRING, sel, 0);
                SaveConfig();
            }
        }
        if (LOWORD(wp) == 6) { // ATIVAR/DESATIVAR
            g_BotAtivo = !g_BotAtivo;
            if (!g_BotAtivo) { g_SearchAtivo = false; }
            SetWindowText(g_hBtnAtivar, g_BotAtivo ? "DESATIVAR" : "ATIVAR");
            if (!g_hStatusText == FALSE)
                SetWindowText(g_hStatusText, g_BotAtivo ? "PARADO" : "PARADO");
            InvalidateRect(hwnd, NULL, TRUE);

            if (g_BotAtivo && !g_hProcess) {
                DWORD pid = 0;
                HANDLE hS = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                PROCESSENTRY32 pe = {sizeof(pe)};
                if (Process32First(hS, &pe)) do {
                    if (!_stricmp(pe.szExeFile, "Ragexe.exe")) pid = pe.th32ProcessID;
                } while (Process32Next(hS, &pe));
                CloseHandle(hS);
                if (pid) g_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
            }
        }
        InvalidateRect(g_hBtnAtivar, NULL, TRUE);
        break;
    }

    case WM_CLOSE:
        if (SendMessage(g_hChkMinimize, BM_GETCHECK, 0, 0) == BST_CHECKED)
            ShowWindow(hwnd, SW_HIDE);
        else
            DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        SaveConfig();
        GdiplusShutdown(g_gdiplusToken);
        DeleteObject(g_hFontUI);
        DeleteObject(g_hFontMono);
        DeleteObject(g_hFontSmall);
        DeleteObject(hSurface2Brush);
        DeleteObject(hSurfaceBrush);
        DeleteObject(hAccentBrush);
        DeleteObject(hDangerBrush);
        DeleteObject(g_BgBrush);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int n) {
    // Init GDI+
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL);

    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = h;
    wc.lpszClassName = "TDHClass";
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);

    g_hMainWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        "TDHClass", "Teledor Honesto",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 380,
        NULL, NULL, h, NULL);

    ShowWindow(g_hMainWnd, n);
    UpdateWindow(g_hMainWnd);

    CreateThread(NULL, 0, BotLoop, NULL, 0, NULL);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}