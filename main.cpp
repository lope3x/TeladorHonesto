#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <commctrl.h>
#include <mmsystem.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include "picosha2.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

using namespace std;

// --- ESTRUTURAS DO DEBUGGER QUE FUNCIONOU ---
struct TargetData {
    DWORD pidRagexe;
    DWORD pidMythSharp;
    HWND bestHwnd;
};

struct Target { string label; uint32_t id; };

// --- GLOBAIS ---
vector<Target> g_Targets;
int  g_Delay      = 3000;
WORD g_TpKeys[3]  = { VK_F5, VK_F6, VK_F7 };
WORD g_ToggleKey  = VK_NUMPAD9;
bool g_BotAtivo   = false;
bool g_SearchAtivo = false;
bool g_SoundFound  = true;
bool g_SoundToggle = true;
bool g_Autenticado = false;
HANDLE g_hProcess  = NULL;
HWND g_hGameWnd    = NULL;

// --- HANDLES INTERFACE ---
HWND g_hMainWnd, g_hList, g_hInput, g_hPassField, g_hDelay, g_hTpWnd[3], g_hToggleWnd, g_hStatusBar, g_hBtnAtivar, g_hBtnAdd, g_hBtnRemover, g_hChkSoundFound, g_hChkSoundToggle;
HFONT g_hFont;

// --- LÓGICA DE BUSCA DO DEBUGGER (O PULO DO GATO) ---
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    TargetData* data = (TargetData*)lParam;
    DWORD windowPID;
    GetWindowThreadProcessId(hwnd, &windowPID);

    if (windowPID != 0 && (windowPID == data->pidRagexe || windowPID == data->pidMythSharp)) {
        char className[512] = {0};
        GetClassNameA(hwnd, className, sizeof(className));

        // Mira na classe renomeada 'Myth of Yggdrasil' dentro do processo Ragexe
        if (windowPID == data->pidRagexe && strcmp(className, "Myth of Yggdrasil") == 0) {
            data->bestHwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

// --- LOGICA DE ENVIO DO DEBUGGER (O QUE DEU CERTO) ---
void SendBackgroundKey(HWND hwnd, WORD vKey) {
    if (!hwnd) return;

    DWORD targetThreadID = GetWindowThreadProcessId(hwnd, NULL);
    DWORD myThreadID = GetCurrentThreadId();

    BOOL attached = AttachThreadInput(myThreadID, targetThreadID, TRUE);

    UINT scanCode = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    LPARAM lParamDown = 0x00000001 | (LPARAM)(scanCode << 16);
    LPARAM lParamUp   = 0xC0000001 | (LPARAM)(scanCode << 16);

    PostMessage(hwnd, WM_KEYDOWN, vKey, lParamDown);
    Sleep(60);
    PostMessage(hwnd, WM_KEYUP, vKey, lParamUp);

    if (attached) AttachThreadInput(myThreadID, targetThreadID, FALSE);
}

// --- RESTO DA LOGICA DO PROGRAMA (MEMORIA/LOGIN/CONFIG) ---
string generateTodayKey() {
    time_t t = time(nullptr);
    tm now; localtime_s(&now, &t);
    stringstream date;
    date << setw(2) << setfill('0') << now.tm_mday << setw(2) << setfill('0') << (now.tm_mon + 1) << (now.tm_year + 1900) << "DEVIRUCHI";
    return picosha2::hash256_hex_string(date.str());
}

const uintptr_t ADDR_GAMEMODE_PTR = 0x00E8E304;
const uintptr_t OFF_CWORLD        = 0xCC;
const uintptr_t OFF_LIST_START    = 0x10;
const uintptr_t OFF_NEXT_NODE     = 0x00;
const uintptr_t OFF_ENTITY_PTR    = 0x08;
const uintptr_t OFF_MOB_ID        = 0x10C;

bool ScanMemory() {
    if (!g_hProcess) return false;
    uint32_t gameMode=0, world=0, list=0, node=0;
    if(!ReadProcessMemory(g_hProcess,(LPCVOID)ADDR_GAMEMODE_PTR,&gameMode,4,NULL)) return false;
    if (!gameMode) return false;
    ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(gameMode+OFF_CWORLD),&world,4,NULL);
    ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(world+OFF_LIST_START),&list,4,NULL);
    ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(list+OFF_NEXT_NODE),&node,4,NULL);
    for (int i=0; i<500 && node!=0; i++) {
        uint32_t entity=0, id=0;
        ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(node+OFF_ENTITY_PTR),&entity,4,NULL);
        if (entity) {
            ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(entity+OFF_MOB_ID),&id,4,NULL);
            for (const auto& t : g_Targets) if (t.id==id) return true;
        }
        ReadProcessMemory(g_hProcess,(LPCVOID)(uintptr_t)(node+OFF_NEXT_NODE),&node,4,NULL);
    }
    return false;
}

string KeyCodeToString(WORD vKey) {
    char name[64];
    UINT sc = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
    if (vKey >= VK_LEFT && vKey <= VK_HELP) sc |= 0x100;
    if (GetKeyNameTextA(sc << 16, name, 64)) return string(name);
    return "Key " + to_string(vKey);
}

// Subclasses
LRESULT CALLBACK PassFieldSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uId, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { SendMessage(GetParent(hwnd), WM_COMMAND, 999, 0); return 0; }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK KeyInputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uId, DWORD_PTR) {
    if (msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN) {
        WORD vKey=(WORD)wp;
        if(uId==99) g_ToggleKey=vKey; else g_TpKeys[uId]=vKey;
        SetWindowText(hwnd, KeyCodeToString(vKey).c_str());
        return 0;
    }
    if(msg==WM_CHAR) return 0;
    return DefSubclassProc(hwnd,msg,wp,lp);
}

// --- INTERFACE ---
void SaveConfig() {
    if (!g_Autenticado) return;
    ofstream f("config.dat");
    f << g_Delay << " " << g_TpKeys[0] << " " << g_TpKeys[1] << " " << g_TpKeys[2] << " " << g_ToggleKey << " " << (int)g_SoundFound << " " << (int)g_SoundToggle << "\n";
    for (const auto& t : g_Targets) f << t.label << ":" << t.id << "\n";
}

void LoadConfig() {
    ifstream f("config.dat");
    int sf=1, st=1;
    if (f >> g_Delay >> g_TpKeys[0] >> g_TpKeys[1] >> g_TpKeys[2] >> g_ToggleKey >> sf >> st) {
        g_SoundFound = sf; g_SoundToggle = st;
        char buf[16]; sprintf_s(buf,"%d",g_Delay); SetWindowText(g_hDelay, buf);
        for(int i=0;i<3;i++) SetWindowText(g_hTpWnd[i], KeyCodeToString(g_TpKeys[i]).c_str());
        SetWindowText(g_hToggleWnd, KeyCodeToString(g_ToggleKey).c_str());
        SendMessage(g_hChkSoundFound, BM_SETCHECK, sf?BST_CHECKED:BST_UNCHECKED, 0);
        SendMessage(g_hChkSoundToggle, BM_SETCHECK, st?BST_CHECKED:BST_UNCHECKED, 0);
        string line; getline(f,line);
        while(getline(f,line)){
            if(line.empty()) continue;
            size_t p=line.find(':');
            if(p!=string::npos){
                string lbl=line.substr(0,p); uint32_t id=(uint32_t)stoul(line.substr(p+1));
                g_Targets.push_back({lbl,id});
                SendMessage(g_hList,LB_ADDSTRING,0,(LPARAM)(lbl+" ["+to_string(id)+"]").c_str());
            }
        }
    }
}

void SetupMainInterface(HWND hwnd) {
    g_hInput = CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT","", WS_CHILD|WS_VISIBLE|ES_LEFT|ES_AUTOHSCROLL, 8,8,138,20,hwnd,NULL,NULL,NULL);
    g_hBtnAdd = CreateWindow("BUTTON","Adicionar", WS_CHILD|WS_VISIBLE, 8,32,66,22,hwnd,(HMENU)1,NULL,NULL);
    g_hBtnRemover = CreateWindow("BUTTON","Remover", WS_CHILD|WS_VISIBLE, 78,32,66,22,hwnd,(HMENU)3,NULL,NULL);
    g_hList = CreateWindowEx(WS_EX_CLIENTEDGE,"LISTBOX",NULL, WS_CHILD|WS_VISIBLE|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL, 8,58,138,230,hwnd,(HMENU)2,NULL,NULL);
    CreateWindow("BUTTON","Config", WS_CHILD|WS_VISIBLE|BS_GROUPBOX, 154,4,190,120,hwnd,NULL,NULL,NULL);
    CreateWindow("STATIC","Delay:", WS_CHILD|WS_VISIBLE, 162,22,36,16,hwnd,NULL,NULL,NULL);
    g_hDelay = CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT","3000", WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_CENTER, 202,20,60,20,hwnd,NULL,NULL,NULL);
    CreateWindow("STATIC","Teleporte:", WS_CHILD|WS_VISIBLE, 162,48,58,16,hwnd,NULL,NULL,NULL);
    for(int i=0;i<3;i++){
        g_hTpWnd[i]=CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT", KeyCodeToString(g_TpKeys[i]).c_str(), WS_CHILD|WS_VISIBLE|ES_CENTER|ES_READONLY, 222+i*38,46,34,20,hwnd,NULL,NULL,NULL);
        SetWindowSubclass(g_hTpWnd[i],KeyInputSubclass,i,0);
    }
    CreateWindow("STATIC","Ativar:", WS_CHILD|WS_VISIBLE, 162,74,38,16,hwnd,NULL,NULL,NULL);
    g_hToggleWnd=CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT", KeyCodeToString(g_ToggleKey).c_str(), WS_CHILD|WS_VISIBLE|ES_CENTER|ES_READONLY, 202,72,80,20,hwnd,NULL,NULL,NULL);
    SetWindowSubclass(g_hToggleWnd,KeyInputSubclass,99,0);
    CreateWindow("BUTTON","Som", WS_CHILD|WS_VISIBLE|BS_GROUPBOX, 154,128,190,44,hwnd,NULL,NULL,NULL);
    g_hChkSoundFound=CreateWindow("BUTTON","Encontrado", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 162,144,84,18,hwnd,(HMENU)7,NULL,NULL);
    g_hChkSoundToggle=CreateWindow("BUTTON","Ativar", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 252,144,60,18,hwnd,(HMENU)8,NULL,NULL);
    g_hBtnAtivar=CreateWindow("BUTTON","Ativar", WS_CHILD|WS_VISIBLE, 154,178,190,58,hwnd,(HMENU)6,NULL,NULL);
    g_hStatusBar=CreateWindow("STATIC","  Parado", WS_CHILD|WS_VISIBLE|SS_LEFT|SS_SUNKEN, 0,294,352,18,hwnd,NULL,NULL,NULL);
    EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessage(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)g_hFont);
    LoadConfig();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        g_hFont = CreateFont(-MulDiv(8, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72), 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,0,0,DEFAULT_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Tahoma");
        CreateWindow("STATIC", "Insira a chave de acesso:", WS_CHILD|WS_VISIBLE, 80, 80, 200, 20, hwnd, NULL, NULL, NULL);
        g_hPassField = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|ES_PASSWORD|ES_CENTER|ES_AUTOHSCROLL, 80, 105, 180, 22, hwnd, NULL, NULL, NULL);
        SetWindowSubclass(g_hPassField, PassFieldSubclass, 0, 0);
        CreateWindow("BUTTON", "Entrar", WS_CHILD|WS_VISIBLE, 120, 135, 100, 30, hwnd, (HMENU)999, NULL, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == 999) {
            char pass[256]; GetWindowText(g_hPassField, pass, 256);
            if (string(pass) == generateTodayKey()) {
                g_Autenticado = true;
                EnumChildWindows(hwnd, [](HWND c, LPARAM) -> BOOL { DestroyWindow(c); return TRUE; }, 0);
                SetupMainInterface(hwnd);
                SetWindowText(hwnd, "Teledor Honesto - Master Loader");
            } else MessageBox(hwnd, "Chave Incorreta!", "Erro", MB_ICONERROR);
        }
        if (!g_Autenticado) break;
        if(LOWORD(wp)==1){ // ADD
            char buf[128]; GetWindowText(g_hInput,buf,128); string s=buf; size_t p=s.find(':');
            if(p!=string::npos){
                string lbl=s.substr(0,p); uint32_t id=(uint32_t)stoul(s.substr(p+1));
                g_Targets.push_back({lbl,id});
                SendMessage(g_hList,LB_ADDSTRING,0,(LPARAM)(lbl+" ["+to_string(id)+"]").c_str());
                SetWindowText(g_hInput,""); SaveConfig();
            }
        }
        if(LOWORD(wp)==3){ // REMOVE
            int sel=SendMessage(g_hList,LB_GETCURSEL,0,0);
            if(sel!=LB_ERR){ g_Targets.erase(g_Targets.begin()+sel); SendMessage(g_hList,LB_DELETESTRING,sel,0); SaveConfig(); }
        }
        if(LOWORD(wp)==6){ // TOGGLE BOT
            g_BotAtivo=!g_BotAtivo;
            if(!g_BotAtivo) g_SearchAtivo=false;
            SetWindowText(g_hBtnAtivar,g_BotAtivo?"Desativar":"Ativar");
            if(g_BotAtivo){
                DWORD pidRagexe=0, pidMyth=0;
                HANDLE hS=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
                PROCESSENTRY32 pe={sizeof(pe)};
                if(Process32First(hS,&pe)) do {
                    if(!_stricmp(pe.szExeFile,"Ragexe.exe")) pidRagexe=pe.th32ProcessID;
                    if(!_stricmp(pe.szExeFile,"MythSharp.exe")) pidMyth=pe.th32ProcessID;
                } while(Process32Next(hS,&pe));
                CloseHandle(hS);
                if(pidRagexe) {
                    g_hProcess=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pidRagexe);
                    // AQUI A LOGICA DO DEBUGGER:
                    TargetData data = { pidRagexe, pidMyth, NULL };
                    EnumChildWindows(GetDesktopWindow(), EnumWindowsProc, (LPARAM)&data);
                    g_hGameWnd = data.bestHwnd;
                }
            }
        }
        break;

    case WM_DESTROY: SaveConfig(); DeleteObject(g_hFont); PostQuitMessage(0); break;
    default: return DefWindowProc(hwnd,msg,wp,lp);
    }
    return 0;
}

DWORD WINAPI BotLoop(LPVOID) {
    int currentIdx = 0;
    while (true) {
        if (g_Autenticado) {
            if (GetAsyncKeyState(g_ToggleKey) & 0x0001) {
                if (g_BotAtivo) {
                    g_SearchAtivo = !g_SearchAtivo;
                    if (!g_SearchAtivo) currentIdx = 0;
                    SetWindowText(g_hStatusBar, g_SearchAtivo ? "  Procurando..." : "  Parado");
                    if (g_SoundToggle) PlaySound(g_SearchAtivo ? TEXT("sound/alert.wav") : TEXT("sound/stop.wav"), NULL, SND_FILENAME|SND_ASYNC);
                }
            }
            if (g_BotAtivo && g_SearchAtivo && g_hGameWnd) {
                char buf[16]; GetWindowText(g_hDelay, buf, 16); g_Delay = atoi(buf);
-
                SendBackgroundKey(g_hGameWnd, g_TpKeys[cu-rrentIdx]);
                Sleep(g_Delay);

                if (ScanMemory()) {
                    g_SearchAtivo = false; currentIdx = 0;
                    SetWindowText(g_hStatusBar, "  Alvo encontrado!");
                    if (g_SoundFound) PlaySound(TEXT("sound/mvp.wav"),NULL,SND_FILENAME|SND_ASYNC);
                } else currentIdx = (currentIdx+1) % 3;
            }
        }
        Sleep(30);
    }-
    return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int n) {
    WNDCLASS wc={}; wc.lpfnWndProc=WndProc; wc.hInstance=h; wc.lpszClassName="TDHClass";
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClass(&wc);
    g_hMainWnd = CreateWindowEx(WS_EX_APPWINDOW,"TDHClass","Teledor Honesto - Autenticacao", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, CW_USEDEFAULT,CW_USEDEFAULT,352,316, NULL,NULL,h,NULL);
    ShowWindow(g_hMainWnd,n);
    CreateThread(NULL,0,BotLoop,NULL,0,NULL);
    MSG m; while(GetMessage(&m,NULL,0,0)){ TranslateMessage(&m); DispatchMessage(&m); }
    return 0;
}