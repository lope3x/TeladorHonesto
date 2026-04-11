#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <vector>

using namespace std;

// --- CONFIGURAÇÕES DE MEMÓRIA ---
const uint32_t ADDR_GAMEMODE_PTR = 0x00E8E304;
const uint32_t OFF_CWORLD        = 0xCC;
const uint32_t OFF_LIST_START    = 0x10;
const uint32_t OFF_NEXT_NODE     = 0x00;
const uint32_t OFF_ENTITY_PTR    = 0x08;
const uint32_t OFF_MOB_ID        = 0x10C;
const char* WINDOW_TITLE         = "Myth of Yggdrasil";

HANDLE hProcess = NULL;
HWND hGameWindow = NULL;

// Lista de teclas para o rodízio (F5, F6, F7)
// VK_F5 = 0x74, VK_F6 = 0x75, VK_F7 = 0x76
vector<WORD> listaDeTeclas = { 0x74, 0x75, 0x76 };
int indiceAtual = 0;

void PressionarTecla(WORD vKey) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

bool EncontrouAlvo(uint32_t targetID) {
    uint32_t cGameMode = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)ADDR_GAMEMODE_PTR, &cGameMode, 4, NULL) || !cGameMode) return false;

    uint32_t cWorld = 0, listHeader = 0, currentNode = 0;
    ReadProcessMemory(hProcess, (LPCVOID)(cGameMode + OFF_CWORLD), &cWorld, 4, NULL);
    if (!cWorld) return false;

    ReadProcessMemory(hProcess, (LPCVOID)(cWorld + OFF_LIST_START), &listHeader, 4, NULL);
    ReadProcessMemory(hProcess, (LPCVOID)(listHeader + OFF_NEXT_NODE), &currentNode, 4, NULL);

    uint32_t firstNode = currentNode;
    int safety = 0;

    while (currentNode != 0 && safety < 300) {
        uint32_t entityPtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(currentNode + OFF_ENTITY_PTR), &entityPtr, 4, NULL);

        if (entityPtr != 0) {
            uint32_t currentID = 0;
            ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + OFF_MOB_ID), &currentID, 4, NULL);

            if (currentID == targetID) return true;
        }
        ReadProcessMemory(hProcess, (LPCVOID)currentNode, &currentNode, 4, NULL);
        if (currentNode == firstNode) break;
        safety++;
    }
    return false;
}

int main() {
    uint32_t idAlvo = 1126;

    // Inicialização de Processo e Janela
    DWORD pid = 0;
    while (!pid || !hGameWindow) {
        hGameWindow = FindWindowA(NULL, WINDOW_TITLE);
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (!_stricmp(pe.szExeFile, "Ragexe.exe")) pid = pe.th32ProcessID;
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        if (!pid) Sleep(1000);
    }

    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    cout << "Rodizio de Teleporte Ativo (F5, F6, F7)" << endl;
    cout << "Procurando por ID: " << idAlvo << endl;

    while (true) {
        if (GetForegroundWindow() == hGameWindow) {

            if (!EncontrouAlvo(idAlvo)) {
                // Seleciona a tecla da vez
                WORD teclaParaApertar = listaDeTeclas[indiceAtual];

                PressionarTecla(teclaParaApertar);
                cout << "[Spam] Alvo ausente. Pressionando F" << (teclaParaApertar - 0x6F) << "..." << endl;

                // Rotaciona o índice para a próxima tecla
                indiceAtual = (indiceAtual + 1) % listaDeTeclas.size();
            } else {
                cout << "[!!!] ALVO LOCALIZADO! Parando teleporte." << endl;
                // Opcional: Beep sonoro quando encontrar
                // Beep(750, 300);
            }
        }

        Sleep(200);
    }
    return 0;
}