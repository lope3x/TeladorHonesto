#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <cstdint>

using namespace std;

// Offsets validados
const uint32_t ADDR_GAMEMODE_PTR = 0x00E8E304;
const uint32_t OFF_CWORLD        = 0xCC;
const uint32_t OFF_LIST_START    = 0x10;
const uint32_t OFF_NEXT_NODE     = 0x00;
const uint32_t OFF_ENTITY_PTR    = 0x08;
const uint32_t OFF_MOB_ID        = 0x10C; // O que funcionou no seu log!

HANDLE hProcess = NULL;

// --- FUNÇÃO DE EXECUÇÃO ---
void ExecutarAcao(uint32_t entityPtr) {
    // Coloque aqui o que deve acontecer (ex: apertar tecla, usar skill)
    // Como vai rodar a 200ms, tome cuidado para não floodar comandos
    cout << "[!] Alvo detectado no Ptr: 0x" << hex << entityPtr << " - Executando..." << endl;
}

void UpdateBot(uint32_t targetID) {
    uint32_t cGameMode = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)ADDR_GAMEMODE_PTR, &cGameMode, 4, NULL) || !cGameMode) return;

    uint32_t cWorld = 0;
    ReadProcessMemory(hProcess, (LPCVOID)(cGameMode + OFF_CWORLD), &cWorld, 4, NULL);
    if (!cWorld) return;

    uint32_t listHeader = 0;
    ReadProcessMemory(hProcess, (LPCVOID)(cWorld + OFF_LIST_START), &listHeader, 4, NULL);

    uint32_t currentNode = 0;
    ReadProcessMemory(hProcess, (LPCVOID)(listHeader + OFF_NEXT_NODE), &currentNode, 4, NULL);

    uint32_t firstNode = currentNode;
    int safetyCounter = 0;

    while (currentNode != 0) {
        uint32_t entityPtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(currentNode + OFF_ENTITY_PTR), &entityPtr, 4, NULL);

        if (entityPtr != 0) {
            uint32_t currentID = 0;
            ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + OFF_MOB_ID), &currentID, 4, NULL);

            if (currentID == targetID) {
                ExecutarAcao(entityPtr);
                // Se você quiser que ele pare de procurar assim que achar o primeiro, dê um 'return' aqui.
            }
        }

        // Move para o próximo nó
        ReadProcessMemory(hProcess, (LPCVOID)currentNode, &currentNode, 4, NULL);

        // Trava de segurança para loops circulares ou excesso de entidades
        if (currentNode == firstNode || ++safetyCounter > 300) break;
    }
}

int main() {
    uint32_t idAlvo = 1126; // ID que você validou no log

    // 1. Abre o processo uma única vez
    DWORD pid = 0;
    while (!pid) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (!_stricmp(pe.szExeFile, "Ragexe.exe")) { pid = pe.th32ProcessID; break; }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        if (!pid) { cout << "Aguardando Ragexe.exe..." << endl; Sleep(1000); }
    }

    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    cout << "Bot rodando silenciosamente a 200ms. Alvo: " << idAlvo << endl;

    // 2. Loop de alta performance
    while (true) {
        UpdateBot(idAlvo);
        Sleep(200); // 200ms conforme solicitado
    }

    return 0;
}