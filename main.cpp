#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <commctrl.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

using namespace std;

struct Target { string label; uint32_t id; };

// --- GLOBAIS ---
vector<Target> g_Targets;
int  g_Delay      = 3000;
WORD g_TpKey      = VK_F5;
WORD g_ToggleKey  = VK_F12;
bool g_BotAtivo   = false;
bool g_SearchAtivo = false;
bool g_SoundFound  = true;
bool g_SoundToggle = true;
bool g_KeyIsHeld   = false;
HANDLE g_hProcess  = NULL;
HWND g_hGameWnd    = NULL;

// --- PONTEIROS PARA A DLL DO DRIVER ---
typedef uint32_t(*IbSendInit_t)(uint32_t type, uint32_t param1, void* param2);
typedef bool(*IbSendKeybdDown_t)(uint16_t vk);
typedef bool(*IbSendKeybdUp_t)(uint16_t vk);

HMODULE g_hIbDll = NULL;
IbSendInit_t pIbSendInit = nullptr;
IbSendKeybdDown_t pIbSendKeybdDown = nullptr;
IbSendKeybdUp_t pIbSendKeybdUp = nullptr;

// --- HANDLES INTERFACE ---
HWND g_hMainWnd, g_hList, g_hInput, g_hDelay, g_hTpWnd, g_hToggleWnd, g_hStatusBar, g_hBtnAtivar, g_hBtnAdd, g_hBtnRemover, g_hChkSoundFound, g_hChkSoundToggle;
HFONT g_hFont;

// --- INICIALIZAÇÃO DO DRIVER ---
bool InitDriver() {
    g_hIbDll = LoadLibrary("IbInputSimulator.dll");
    if (!g_hIbDll) {
        MessageBox(NULL, "IbInputSimulator.dll nao encontrada na pasta!", "Erro", MB_ICONERROR);
        return false;
    }

    pIbSendInit = (IbSendInit_t)GetProcAddress(g_hIbDll, "IbSendInit");
    pIbSendKeybdDown = (IbSendKeybdDown_t)GetProcAddress(g_hIbDll, "IbSendKeybdDown");
    pIbSendKeybdUp = (IbSendKeybdUp_t)GetProcAddress(g_hIbDll, "IbSendKeybdUp");

    if (pIbSendInit && pIbSendKeybdDown && pIbSendKeybdUp) {
        uint32_t initRes = pIbSendInit(2, 0, nullptr); // 2 = SENDTYPE_LOGITECH
        if (initRes != 0) {
            MessageBox(NULL, "Falha ao iniciar o driver da Logitech. Verifique se esta instalado.", "Erro", MB_ICONERROR);
            return false;
        }
        return true;
    }

    MessageBox(NULL, "Funcoes nao encontradas na DLL.", "Erro", MB_ICONERROR);
    return false;
}

// --- LÓGICA DE BUSCA SIMPLIFICADA ---
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD windowPID;
    GetWindowThreadProcessId(hwnd, &windowPID);
    if (windowPID == (DWORD)lParam && IsWindowVisible(hwnd)) {
        g_hGameWnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

// --- LÓGICA DE ENVIO DO DRIVER LOGITECH ---
void SendDriverKeyState(WORD vKey, bool isDown) {
    if (!pIbSendKeybdDown || !pIbSendKeybdUp) return;

    if (isDown) {
        pIbSendKeybdDown(vKey);
    } else {
        pIbSendKeybdUp(vKey);
    }
}

// --- RESTO DA LOGICA DO PROGRAMA (MEMORIA/CONFIG) ---
const uintptr_t ADDR_GAMEMODE_PTR = 0x00E43F44;
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
            std::cout << id << std::endl;
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
LRESULT CALLBACK KeyInputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uId, DWORD_PTR) {
    if (msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN) {
        WORD vKey=(WORD)wp;
        if(uId==99) g_ToggleKey=vKey; else g_TpKey=vKey;
        SetWindowText(hwnd, KeyCodeToString(vKey).c_str());
        return 0;
    }
    if(msg==WM_CHAR) return 0;
    return DefSubclassProc(hwnd,msg,wp,lp);
}

void TriggerTpKey(WORD vKey) {
    if (!pIbSendKeybdDown || !pIbSendKeybdUp) return;
    pIbSendKeybdDown(vKey);
    Sleep(50); // Delay curto para o jogo registrar o down
    pIbSendKeybdUp(vKey);
}

// --- INTERFACE ---
void SaveConfig() {
    ofstream f("config.dat");
    f << g_Delay << " " << g_TpKey << " " << g_ToggleKey << " " << (int)g_SoundFound << " " << (int)g_SoundToggle << "\n";
    for (const auto& t : g_Targets) f << t.label << ":" << t.id << "\n";
}

void LoadConfig() {
    ifstream f("config.dat");
    int sf=1, st=1;
    if (f >> g_Delay >> g_TpKey >> g_ToggleKey >> sf >> st) {
        g_SoundFound = sf; g_SoundToggle = st;
        char buf[16]; sprintf_s(buf,"%d",g_Delay); SetWindowText(g_hDelay, buf);
        SetWindowText(g_hTpWnd, KeyCodeToString(g_TpKey).c_str());
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
    g_hTpWnd=CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT", KeyCodeToString(g_TpKey).c_str(), WS_CHILD|WS_VISIBLE|ES_CENTER|ES_READONLY, 222,46,80,20,hwnd,NULL,NULL,NULL);
    SetWindowSubclass(g_hTpWnd,KeyInputSubclass,1,0);

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
        if (!InitDriver()) {
            PostQuitMessage(0); // Fecha se a DLL não carregar
            return -1;
        }
        SetupMainInterface(hwnd);
        break;

    case WM_COMMAND:
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
            if(!g_BotAtivo) {
                g_SearchAtivo=false;
                if (g_KeyIsHeld) {
                    SendDriverKeyState(g_TpKey, false);
                    g_KeyIsHeld = false;
                }
            }
            SetWindowText(g_hBtnAtivar,g_BotAtivo?"Desativar":"Ativar");

            if(g_BotAtivo){
                DWORD pidCcRO = 0;
                HANDLE hS = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                PROCESSENTRY32 pe = {sizeof(pe)};

                if(Process32First(hS, &pe)) do {
                    if(!_stricmp(pe.szExeFile, "ccRO.exe")) {
                        pidCcRO = pe.th32ProcessID;
                        break;
                    }
                } while(Process32Next(hS, &pe));
                CloseHandle(hS);

                if(pidCcRO) {
                    g_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pidCcRO);
                    g_hGameWnd = NULL;
                    EnumWindows(EnumWindowsProc, (LPARAM)pidCcRO);
                } else {
                    MessageBox(hwnd, "Processo ccRO.exe não encontrado!", "Aviso", MB_ICONWARNING);
                    g_BotAtivo = false;
                    SetWindowText(g_hBtnAtivar, "Ativar");
                }
            }
        }
        if(LOWORD(wp)==7) { g_SoundFound = SendMessage(g_hChkSoundFound, BM_GETCHECK, 0, 0) == BST_CHECKED; SaveConfig(); }
        if(LOWORD(wp)==8) { g_SoundToggle = SendMessage(g_hChkSoundToggle, BM_GETCHECK, 0, 0) == BST_CHECKED; SaveConfig(); }
        break;

    case WM_DESTROY:
        SaveConfig();
        if(g_KeyIsHeld) SendDriverKeyState(g_TpKey, false);
        if(g_hProcess) CloseHandle(g_hProcess);
        if(g_hIbDll) FreeLibrary(g_hIbDll);
        DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;

    default: return DefWindowProc(hwnd,msg,wp,lp);
    }
    return 0;
}

DWORD WINAPI BotLoop(LPVOID) {
    while (true) {
        if (GetAsyncKeyState(g_ToggleKey) & 0x0001) {
            if (g_BotAtivo) {
                g_SearchAtivo = !g_SearchAtivo;
                SetWindowText(g_hStatusBar, g_SearchAtivo ? "  Procurando..." : "  Parado");
            }
        }

        if (g_BotAtivo && g_SearchAtivo) {
            // 1. Envia o clique (Down/Up)
            TriggerTpKey(g_TpKey);

            // 2. Aguarda o delay definido na interface
            char buf[16]; GetWindowText(g_hDelay, buf, 16); g_Delay = atoi(buf);
            Sleep(g_Delay);

            // 3. Scan após o teleporte
            if (ScanMemory()) {
                g_SearchAtivo = false;
                SetWindowText(g_hStatusBar, "  Alvo encontrado!");
                if (g_SoundFound) PlaySound(TEXT("sound/mvp.wav"),NULL,SND_FILENAME|SND_ASYNC);
            }
        } else {
            Sleep(50);
        }
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int n) {
    WNDCLASS wc={}; wc.lpfnWndProc=WndProc; wc.hInstance=h; wc.lpszClassName="TDHClass";
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1); wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClass(&wc);

    g_hMainWnd = CreateWindowEx(WS_EX_APPWINDOW,"TDHClass","", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, CW_USEDEFAULT,CW_USEDEFAULT,352,352, NULL,NULL,h,NULL);
    ShowWindow(g_hMainWnd,n);

    CreateThread(NULL,0,BotLoop,NULL,0,NULL);
    MSG m; while(GetMessage(&m,NULL,0,0)){ TranslateMessage(&m); DispatchMessage(&m); }
    return 0;
}