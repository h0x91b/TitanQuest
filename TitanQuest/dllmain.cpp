// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <detours.h>

#include <Windows.h>
#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include "Character.h"
#include "Engine.dll.h"
#include "dx.h"

static VOID(WINAPI* TrueSleep)(DWORD dwMilliseconds) = Sleep;
void* ByPtr(DWORD base, DWORD offset, ...);
HMODULE engineDll = 0;

typedef HRESULT(__stdcall* EndScene) (IDirect3DDevice9*);
EndScene realEndScene = nullptr;

typedef HRESULT(__stdcall* Present) (IDirect3DDevice9*, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
Present realPresent = nullptr;

// virtual HRESULT __stdcall IDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS * pPresentationParameters)
typedef HRESULT(__stdcall* Reset) (IDirect3DDevice9*, D3DPRESENT_PARAMETERS* pPresentationParameters);
Reset realReset = nullptr;

typedef float (__fastcall GetCurrentMana)(Character* _this, DWORD edx);
GetCurrentMana* realGetCurrentMana = nullptr;

typedef float (__fastcall CharacterAddMoney)(Character* _this, DWORD edx, uint money);
CharacterAddMoney* realCharacterAddMoney = nullptr;

typedef uint (__fastcall GetItemCost)(void* _this, DWORD edx, bool p1);
GetItemCost* realGetItemCost = nullptr;


/*
will generate something like:

typedef uint (__fastcall GetItemCost)(void* _this, DWORD edx, bool p1);
GetItemCost* realGetItemCost = nullptr;
float __fastcall _CharacterAddMoney(Character* _this, DWORD edx, uint money)
*/
#define thisCallHook(fnName, thisType, retType, ...) typedef retType (__fastcall fnName)(thisType _this, DWORD _edx, __VA_ARGS__); \
fnName* real##fnName = nullptr; \
retType __fastcall _##fnName(thisType _this, DWORD _edx, __VA_ARGS__)

//realGetCurrentMana = (GetCurrentMana*)GetProcAddress(gameDll, MAKEINTRESOURCEA(8427));
#define ProcAddr(hModule, fnName, ordinal) real##fnName = (fnName*)GetProcAddress(hModule, MAKEINTRESOURCEA(ordinal));

//DetourAttach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
#define Attach(fnName) DetourAttach(&(PVOID&)real##fnName, _##fnName);

//DetourDetach(&(PVOID&)realGetItemCost, _GetItemCost);
#define Detach(fnName) DetourDetach(&(PVOID&)real##fnName, _##fnName);

bool godMode = 0;
int frame = 0;
bool ignoreLevelRequirements = false;

thisCallHook(AreRequirementsMet, void*, bool, void* character) {
    if(ignoreLevelRequirements)
        return true;
    return realAreRequirementsMet(_this, _edx, character);
}

Engine* pEngine = nullptr;
static LPDIRECT3D9              g_pD3D = NULL;
static LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

DWORD* pVTable;
HWND hwnd;

bool show_demo_window = true;
WNDPROC originalWndProc = nullptr;
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT __stdcall _wndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam))
        return true;
    return CallWindowProc(originalWndProc, window, msg, wParam, lParam);
}

VOID WINAPI TimedSleep(DWORD dwMilliseconds)
{
    WCHAR buf[256];
    wsprintf(buf, L"sleep(%d)\r\n", dwMilliseconds);
    OutputDebugString(buf);
    TrueSleep(dwMilliseconds);
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

HRESULT __stdcall _Present(IDirect3DDevice9* d, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
    //OutputDebugString(L"_Present\r\n");
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Start the Dear ImGui frame
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    //if (show_demo_window) {
    //    ImGui::ShowDemoWindow(&show_demo_window);
    //}

    if (frame++ % 10 == 0) {
        BYTE* pGodMode = (BYTE*)ByPtr((DWORD)engineDll, 0x00365DF0, 0x218, 0x74, 0xc25, -1);
        if (pGodMode) {
            *pGodMode = godMode;
        }
    }

    {
        ImGui::Begin("The tool", 0, ImGuiWindowFlags_MenuBar);

        ImGui::Checkbox("God mode", &godMode);
        ImGui::Checkbox("Ignore items level requirement", &ignoreLevelRequirements);
        //if (ImGui::BeginMenuBar())
        //{
        //    if (ImGui::BeginMenu("File"))
        //    {
        //        if (ImGui::MenuItem("Open..", "Ctrl+O")) { /* Do stuff */ }
        //        if (ImGui::MenuItem("Save", "Ctrl+S")) { /* Do stuff */ }
        //        if (ImGui::MenuItem("Close", "Ctrl+W")) { my_tool_active = false; }
        //        ImGui::EndMenu();
        //    }
        //    ImGui::EndMenuBar();
        //}

        //// Edit a color (stored as ~4 floats)
        //ImGui::ColorEdit4("Color", my_color);

        //// Plot some values
        //const float my_values[] = { 0.2f, 0.1f, 1.0f, 0.5f, 0.9f, 2.2f };
        //ImGui::PlotLines("Frame Times", my_values, IM_ARRAYSIZE(my_values));

        //// Display contents in a scrolling region
        //ImGui::TextColored(ImVec4(1, 1, 0, 1), "Important Stuff");
        //ImGui::BeginChild("Scrolling");
        //for (int n = 0; n < 50; n++)
        //    ImGui::Text("%04d: Some text", n);
        //ImGui::EndChild();
        ImGui::End();
    }

    // Rendering
    ImGui::EndFrame();

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
    // g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
    if (g_pd3dDevice->BeginScene() >= 0)
    {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_pd3dDevice->EndScene();
    }
    HRESULT result = realPresent(d, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
        OutputDebugString(L"D3DERR_DEVICELOST\r\n");
        ResetDevice();
    }
    return result;
}

HRESULT __stdcall _Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    OutputDebugString(L"_Reset\r\n");

    ImGui_ImplDX9_InvalidateDeviceObjects();
    auto result = realReset(d, pPresentationParameters);
    ImGui_ImplDX9_CreateDeviceObjects();
    
    return result;
}

HRESULT __stdcall _EndScene(IDirect3DDevice9* d) {
    //OutputDebugString(L"_EndScene\r\n");
    return realEndScene(d);
}

float __fastcall _GetCurrentMana(Character* _this, DWORD edx) {
    // OutputDebugString(L"_GetCurrentMana\r\n");
    // _this->health = 150.0f;
    return realGetCurrentMana(_this, edx) - 50;
}

float __fastcall _CharacterAddMoney(Character* _this, DWORD edx, uint money) {
    OutputDebugString(L"_CharacterAddMoney\r\n");
    return realCharacterAddMoney(_this, edx, money * 15);
}

uint __fastcall _GetItemCost(void* _this, DWORD edx, bool p1) {
    OutputDebugString(L"_GetItemCost\r\n");

    return 50000;
    //return realCharacterAddMoney(_this, edx, money * 15);
}

void* ByPtr(DWORD base, DWORD offset, ...)
{
    DWORD a = (base + offset);
    DWORD tmp;
    va_list vl;
    va_start(vl, offset);
    while (true)
    {
        tmp = va_arg(vl, DWORD);
        if (tmp == -1) break;
        
        if (IsBadWritePtr((DWORD*)a, 4)) {
            a = 0;
            break;
        }
        
        a = *(DWORD*)a;
        if (a == 0) {
            break;
        }
        a += tmp;
    }
    va_end(vl);
    return (void*)a;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  dwReason,
                       LPVOID lpReserved
                     )
{
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
//        MessageBox(0, L"Injected DLL", L"Inject", 0);
        DetourRestoreAfterWith();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        HMODULE gameDll = GetModuleHandleA("game.dll");
        engineDll = GetModuleHandleA("Engine.dll");
        realGetCurrentMana = (GetCurrentMana*)GetProcAddress(gameDll, MAKEINTRESOURCEA(8427));
        ProcAddr(gameDll, GetCurrentMana, 8427);
        realCharacterAddMoney = (CharacterAddMoney*)GetProcAddress(gameDll, MAKEINTRESOURCEA(5345));
        realGetItemCost = (GetItemCost*)GetProcAddress(gameDll, MAKEINTRESOURCEA(9000));
        pEngine = (Engine*)GetProcAddress(gameDll, MAKEINTRESOURCEA(5377));

        ProcAddr(gameDll, AreRequirementsMet, 5648);

        // "Engine.dll"+00365DF0 - pEngine

        // "Engine.dll"+00366740

        //void *ptr = (void*)(*(DWORD*)((DWORD)engineDll + 0x00366740));

        // way #1
        // dx_s1* dx = (dx_s1*) *(DWORD*) ((DWORD)engineDll + 0x00366740);
        
        // way #2

        dx_s1* dx = *(dx_s1**) ((DWORD)engineDll + 0x00366740);
        dx_s2* dx2 = *(dx_s2**) ((DWORD)engineDll + 0x00365E48);


        //DWORD a = ((DWORD)engineDll + 0x00365DF0);
        //a = *(DWORD*)a;
        //a += 0x218;
        //a = *(DWORD*)a;
        //a += 0x74;
        //a = *(DWORD*)a;
        //a += 0xc25;

        //BYTE* target = (BYTE*)a;

        //BYTE* target2 = (BYTE*)ByPtr((DWORD)engineDll, 0x00365DF0, 0x218, 0x74, 0xc25, -1);

        //a += 0x218;
    //a = *(DWORD*)a;
        
//        b = (*(DWORD*)a + 0x218)
        
        // *(BYTE*)(*(DWORD*)(*(DWORD*) ((DWORD)engineDll + 0x00365DF0) + 0x218) + 0x74) + 0x25c)
        //"Engine.dll" + 00365DF0
        //218
        //74
        //    25c <<<<< byte
//
////        dx_s1* dx = (dx_s1*) *(DWORD*)(engineDll + 0x00366740);
        g_pD3D = (LPDIRECT3D9)dx->g_pD3D;
        g_pd3dDevice = (LPDIRECT3DDEVICE9)dx2->g_pd3dDevice;

        //auto hwnd = CreateWindow(L"STATIC", L"Dummy window", 0, 0, 0, 0, 0, 0, 0, 0, 0);
        hwnd = FindWindow(NULL, L"Titan Quest Anniversary Edition");

        //originalWndProc = WNDPROC(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(_wndProc)));
        originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_wndProc)));

        pVTable = *reinterpret_cast<DWORD**>(g_pd3dDevice);

//        realEndScene = (EndScene)pVTable[42];
        realReset = (Reset)pVTable[16];
        realPresent = (Present)pVTable[17];
  //      pVTable[42] = (DWORD)_EndScene;
        pVTable[17] = (DWORD)_Present;
        pVTable[16] = (DWORD)_Reset;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsClassic();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX9_Init(g_pd3dDevice);

        auto font = io.Fonts->AddFontDefault();

        //io.Fonts->Build();
        
        DetourAttach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
        DetourAttach(&(PVOID&)realCharacterAddMoney, _CharacterAddMoney);
        DetourAttach(&(PVOID&)realGetItemCost, _GetItemCost);

        Attach(AreRequirementsMet);
        //DetourAttach((PVOID*)pVTable[17], _Present);
        //DetourAttach((PVOID*)pVTable[16], _Reset);
        //DetourAttach(&(LPVOID&)pVTable[42], _EndScene);
        DetourTransactionCommit();
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        SetWindowLongA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));

        // SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(originalWndProc));
        // WNDPROC(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(originalWndProc)));

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        // DetourDetach(&(PVOID&)TrueSleep, TimedSleep);
        DetourDetach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
        DetourDetach(&(PVOID&)realCharacterAddMoney, _CharacterAddMoney);
        DetourDetach(&(PVOID&)realGetItemCost, _GetItemCost);
        Detach(AreRequirementsMet);
//        DetourDetach((PVOID*)pVTable[17], _Present);
//        DetourDetach((PVOID*)pVTable[16], _Reset);
        // DetourDetach(&(PVOID&)TrueSleep, TimedSleep);
        DetourTransactionCommit();

    //    pVTable[42] = (DWORD)realEndScene;
        pVTable[17] = (DWORD)realPresent;
        pVTable[16] = (DWORD)realReset;
    }

    return TRUE;
}

