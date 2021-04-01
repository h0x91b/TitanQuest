// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <detours.h>

#include <Windows.h>
#include <d3d9.h>
#include <string>
#include <vector>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

//#include "Character.h"
//#include "Engine.dll.h"
//#include "dx.h"
#include "TQ_Types.h"

#pragma region definitions
void log(const char* fmt, ...);
void* ByPtr(DWORD base, DWORD offset, ...);
HRESULT __stdcall _Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pPresentationParameters);
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#pragma endregion

#pragma region old style typedefs
typedef HRESULT(__stdcall* EndScene) (IDirect3DDevice9*);
EndScene realEndScene = nullptr;

typedef HRESULT(__stdcall* Present) (IDirect3DDevice9*, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);
Present realPresent = nullptr;

// virtual HRESULT __stdcall IDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS * pPresentationParameters)
typedef HRESULT(__stdcall* Reset) (IDirect3DDevice9*, D3DPRESENT_PARAMETERS* pPresentationParameters);
Reset realReset = nullptr;

typedef float (__fastcall CharacterAddMoney)(Character* _this, DWORD edx, uint money);
CharacterAddMoney* realCharacterAddMoney = nullptr;

typedef uint (__fastcall GetItemCost)(void* _this, DWORD edx, bool p1);
GetItemCost* realGetItemCost = nullptr;
#pragma endregion

#pragma region macros
/*
will generate something like:

typedef uint (__fastcall GetItemCost)(void* _this, DWORD _edx, bool p1);
GetItemCost* realGetItemCost = nullptr;
float __fastcall _CharacterAddMoney(Character* _this, DWORD edx, uint money)
*/
#define thisCallHook(fnName, thisType, retType, ...) typedef retType (__fastcall fnName)(thisType _this, DWORD _edx, __VA_ARGS__); \
  fnName* real##fnName = nullptr; \
  PDETOUR_TRAMPOLINE trampoline_##fnName = nullptr; \
  void* target_##fnName = nullptr; \
  void* real_##fnName = nullptr; \
  retType __fastcall _##fnName(thisType _this, DWORD _edx, __VA_ARGS__)

#define cdeclCallHook(fnName, retType, ...) typedef retType (__cdecl fnName)(__VA_ARGS__); \
  fnName* real##fnName = nullptr; \
  PDETOUR_TRAMPOLINE trampoline_##fnName = nullptr; \
  void* target_##fnName = nullptr; \
  void* real_##fnName = nullptr; \
  retType __cdecl _##fnName(__VA_ARGS__)

//realGetCurrentMana = (GetCurrentMana*)GetProcAddress(gameDll, MAKEINTRESOURCEA(8427));
#define ProcAddr(hModule, fnName, ordinal) real##fnName = (fnName*)GetProcAddress(hModule, MAKEINTRESOURCEA(ordinal)); \
  log("ProcAddr of %s - 0x%08X", #fnName, real##fnName)

//DetourAttach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
#define Attach(fnName) DetourAttachEx(&(PVOID&)real##fnName, _##fnName, &trampoline_##fnName, &target_##fnName, &real_##fnName); \
  log("Attach of %s target: 0x%08X real: 0x%08X", #fnName, target_##fnName, real_##fnName)

//DetourDetach(&(PVOID&)realGetItemCost, _GetItemCost);
#define Detach(fnName) DetourDetach(&(PVOID&)real##fnName, _##fnName);
#pragma endregion

#pragma region vars
    bool godMode = 0;
    int frame = 0;
    bool ignoreLevelRequirements = false;
    bool freezeMana = false;
    ImFont* defFont = nullptr;
    int expMultiplier = 1;
    Engine* pEngine = nullptr;
    static LPDIRECT3D9 g_pD3D = NULL;
    static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
    static D3DPRESENT_PARAMETERS g_d3dpp = {};

    DWORD* pVTable;
    HWND hwnd;
    bool showUIDemo = false;

    std::vector<std::string> _log;
    WNDPROC originalWndProc = nullptr;
    HMODULE engineDll = 0;

    void* d3dDevice[119];
    bool isExiting = false;
#pragma endregion

#pragma region thisCalls
// 9843 -  float __thiscall GAME::Character::GetManaLimit(Character *this)
thisCallHook(GetManaLimit, Character *, float) {
    return realGetManaLimit(_this, _edx);
}

thisCallHook(GetCurrentMana, Character *, float) {
    if (freezeMana)
        return realGetManaLimit(_this, _edx);
    return realGetCurrentMana(_this, _edx);
}

thisCallHook(AreRequirementsMet, void*, bool, void* character) {
    if(ignoreLevelRequirements)
        return true;
    return realAreRequirementsMet(_this, _edx, character);
}

// 9102 - uint __thiscall GAME::ItemEquipment::GetLevelRequirement(ItemEquipment *this)
thisCallHook(GetLevelRequirement, void*, uint) {
    //log("GetLevelRequirement 0x%08X", _this);
    if (ignoreLevelRequirements)
        return 1;
    return realGetLevelRequirement(_this, _edx);
}

// 11320 - uint __thiscall GAME::ItemEquipment::GetStrengthRequirement(ItemEquipment *this)
thisCallHook(GetStrengthRequirement, void*, uint) {
    //log("GetStrengthRequirement 0x%08X", _this);
    if (ignoreLevelRequirements)
        return 1;
    return realGetStrengthRequirement(_this, _edx);
}

// 8591 - uint __thiscall GAME::ItemEquipment::GetDexterityRequirement(ItemEquipment *this)
thisCallHook(GetDexterityRequirement, void*, uint) {
    //log("GetDexterityRequirement 0x%08X", _this);
    if (ignoreLevelRequirements)
        return 1;
    return realGetDexterityRequirement(_this, _edx);
}

// 8959 - uint __thiscall GAME::ItemEquipment::GetIntelligenceRequirement(ItemEquipment *this)
thisCallHook(GetIntelligenceRequirement, void*, uint) {
    //log("GetIntelligenceRequirement 0x%08X", _this);
    if (ignoreLevelRequirements)
        return 1;
    return realGetIntelligenceRequirement(_this, _edx);
}

// 9931 - uint __thiscall GAME::Character::GetModifierPoints(Character *this)
thisCallHook(GetModifierPoints, Character*, uint) {
    //log("GetModifierPoints 0x%08X", _this);
    return realGetModifierPoints(_this, _edx);
}

// 15499 - void __thiscall GAME::CharAttribute::SetBaseValue(CharAttribute *this,float param_1)
thisCallHook(SetBaseValue, void*, void, float val) {
    log("SetBaseValue - on %08X = %.f", _this, val);
    realSetBaseValue(_this, _edx, val);
}

// 14523 - void __thiscall GAME::Character::ReceiveExperience(Character *this,uint exp,bool param_2)
thisCallHook(ReceiveExperience, Character*, void, uint exp, bool someBool) {
    log("ReceiveExperience - %d bool %d", exp, someBool);
    realReceiveExperience(_this, _edx, exp * expMultiplier, someBool);
}

// 9837 - Player * __thiscall GAME::GameEngine::GetMainPlayer(GameEngine *this)
thisCallHook(GetMainPlayer, GameEngine*, Player*) {
    // log("GetMainPlayer");
    return realGetMainPlayer(_this, _edx);
}

// 2 - DWORD __cdecl CreateRenderDevice(Engine *param_1)
cdeclCallHook(CreateRenderDevice, DWORD, Engine* param_1) {
    log("CreateRenderDevice");
    return realCreateRenderDevice(param_1);
}

// 4 - uint __cdecl ResetRenderDevice(int *param_1,undefined4 param_2)
cdeclCallHook(ResetRenderDevice, uint, int* param_1, undefined4 param_2) {
    log("ResetRenderDevice");
    return realResetRenderDevice(param_1, param_2);
}
#pragma endregion

void log(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    auto len = vsnprintf(buf, 1024, fmt, args);
    va_end(args);
    _log.push_back(buf);

    if (_log.size() > 100) {
        _log.erase(_log.begin());
    }
}

static LRESULT CALLBACK _wndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam);

    ImGuiIO& io = ImGui::GetIO();

    if (io.WantCaptureMouse
        || io.WantCaptureKeyboard
    ) {
        return TRUE;
    }
    return CallWindowProc(originalWndProc, window, msg, wParam, lParam);
}

HRESULT __stdcall _Present(IDirect3DDevice9* d, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
    if(isExiting) return realPresent(d, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    //OutputDebugString(L"_Present\r\n");
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    if (!defFont) {
        OutputDebugString(L"_Present defFont is not built\r\n");
        defFont = io.Fonts->AddFontDefault();
        io.Fonts->Build();
        IM_ASSERT(defFont && defFont->IsLoaded());
    }

    if (!io.Fonts->IsBuilt()) {
        OutputDebugString(L"_Present io.Fonts->IsBuilt()\r\n");
        defFont = io.Fonts->AddFontDefault();
        io.Fonts->Build();
        IM_ASSERT(defFont && defFont->IsLoaded());
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (frame++ % 10 == 0) {
        BYTE* pGodMode = (BYTE*)ByPtr((DWORD)engineDll, 0x00365DF0, 0x218, 0x74, 0xc25, -1);
        if (pGodMode) {
            *pGodMode = godMode;
        }
    }

    if (showUIDemo) {
        ImGui::ShowDemoWindow(&showUIDemo);
    }

    {
        ImGui::PushFont(defFont);
        ImGuiWindowFlags windowFlags = 0;
        ImGui::Begin("The tool", 0, windowFlags);

        ImGui::Checkbox("God mode", &godMode);
        ImGui::Checkbox("Freeze mana", &freezeMana);
        ImGui::Checkbox("Ignore items level requirement", &ignoreLevelRequirements);
        ImGui::Checkbox("Demo UI", &showUIDemo);

        static ImGuiSliderFlags flags = ImGuiSliderFlags_None;
        ImGui::SliderInt("Experience multiplier", &expMultiplier, 1, 50, "%d", flags);

        ImGui::BeginChild("Log");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        for (auto i = _log.rbegin(); i != _log.rend(); i++) {
            ImGui::Text("> %s", i->c_str());
        }
            
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::End();
        ImGui::PopFont();
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
        // ResetDevice();
        _Reset(d, &g_d3dpp);
    }
    return result;
}

HRESULT __stdcall _Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (isExiting) return realReset(d, pPresentationParameters);
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

HWND window = 0;

BOOL CALLBACK enumWnd(HWND handle, LPARAM lp) {
    DWORD procId = 0;
    GetWindowThreadProcessId(handle, &procId);
    if (GetCurrentProcessId() != procId)
        return TRUE;

    window = handle;
    return false;
}

HWND GetProcessWindow() {
    window = 0;
    EnumWindows(enumWnd, NULL);
    return window;
}

bool GetD3D9Device(void** pTable, size_t size) {
    if (!pTable) return false;

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;

    IDirect3DDevice9* pDummyDevice = nullptr;

    D3DPRESENT_PARAMETERS d3dpp = {};

    d3dpp.Windowed = false;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetProcessWindow();

    HRESULT res = pD3D->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        d3dpp.hDeviceWindow,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &d3dpp,
        &pDummyDevice
    );

    if (res != S_OK) {
        d3dpp.Windowed = !d3dpp.Windowed;
        res = pD3D->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            d3dpp.hDeviceWindow,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &d3dpp,
            &pDummyDevice
        );
        if (res != S_OK) {
            pD3D->Release();
            return false;
        }
    }

    memcpy(pTable, *(void***)(pDummyDevice), size);
    pDummyDevice->Release();
    pD3D->Release();
    return true;
}

DWORD WINAPI MainThread(HMODULE hModule) {

    // DirectX dummy method to find vt
    if (GetD3D9Device(d3dDevice, sizeof(d3dDevice))) {

    }

    DetourRestoreAfterWith();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    log("DLL_PROCESS_ATTACH");

    HMODULE gameDll = GetModuleHandleA("game.dll");
    log("gameDll %08X", gameDll);
    HMODULE direct3d_dll = GetModuleHandleA("direct3d.dll");
    log("direct3d %08X", direct3d_dll);
    engineDll = GetModuleHandleA("Engine.dll");
    log("engineDll %08X", engineDll);

    realCharacterAddMoney = (CharacterAddMoney*)GetProcAddress(gameDll, MAKEINTRESOURCEA(5345));
    realGetItemCost = (GetItemCost*)GetProcAddress(gameDll, MAKEINTRESOURCEA(9000));
    pEngine = (Engine*)GetProcAddress(gameDll, MAKEINTRESOURCEA(5377));

    ProcAddr(gameDll, GetCurrentMana, 8427);
    ProcAddr(gameDll, GetManaLimit, 9843);
    ProcAddr(gameDll, AreRequirementsMet, 5648);
    ProcAddr(gameDll, GetLevelRequirement, 9102);
    ProcAddr(gameDll, GetStrengthRequirement, 11320);
    ProcAddr(gameDll, GetDexterityRequirement, 8591);
    ProcAddr(gameDll, GetIntelligenceRequirement, 8959);
    ProcAddr(gameDll, GetModifierPoints, 9931);
    ProcAddr(gameDll, SetBaseValue, 15499);
    ProcAddr(gameDll, ReceiveExperience, 14523);
    ProcAddr(gameDll, GetMainPlayer, 9837);
    ProcAddr(direct3d_dll, CreateRenderDevice, 2);
    ProcAddr(direct3d_dll, ResetRenderDevice, 4);

    log("getting dx");

    // "Engine.dll"+00365DF0 - pEngine

    // "Engine.dll"+00366740

    //void *ptr = (void*)(*(DWORD*)((DWORD)engineDll + 0x00366740));

    // way #1
    // dx_s1* dx = (dx_s1*) *(DWORD*) ((DWORD)engineDll + 0x00366740);

    // way #2

    dx_s1* dx = *(dx_s1**)((DWORD)engineDll + 0x00366740);
    log("getting dx2");
    dx_s2* dx2 = *(dx_s2**)((DWORD)engineDll + 0x00365E48);

    //DWORD a = ((DWORD)engineDll + 0x00365DF0);
    //a = *(DWORD*)a;
    //a += 0x218;
    //a = *(DWORD*)a;
    //a += 0x74;
    //a = *(DWORD*)a;
    //a += 0xc25;
    //BYTE* target = (BYTE*)a;

    //BYTE* target2 = (BYTE*)ByPtr((DWORD)engineDll, 0x00365DF0, 0x218, 0x74, 0xc25, -1);

    log("getting g_pD3D");
    g_pD3D = (LPDIRECT3D9)dx->g_pD3D;
    log("getting g_pd3dDevice");
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

            //pVTable[17] = (DWORD)_Present;
            //pVTable[16] = (DWORD)_Reset;

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

    DetourAttach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
    DetourAttach(&(PVOID&)realCharacterAddMoney, _CharacterAddMoney);
    DetourAttach(&(PVOID&)realGetItemCost, _GetItemCost);

    Attach(GetCurrentMana);
    Attach(GetManaLimit);
    Attach(AreRequirementsMet);
    Attach(GetLevelRequirement);
    Attach(GetStrengthRequirement);
    Attach(GetDexterityRequirement);
    Attach(GetIntelligenceRequirement);
    Attach(GetModifierPoints);
    Attach(SetBaseValue);
    Attach(ReceiveExperience);
    Attach(GetMainPlayer);
    Attach(CreateRenderDevice);
    Attach(ResetRenderDevice);

    //DetourAttach((PVOID*)pVTable[17], _Present);
    //DetourAttach(&(PVOID&)pVTable[17], _Present);
    DetourAttach(&(PVOID&)realPresent, _Present);
    DetourAttach(&(PVOID&)realReset, _Reset);
    //DetourAttach((PVOID*)pVTable[16], _Reset);
    //DetourAttach(&(LPVOID&)pVTable[42], _EndScene);
    DetourTransactionCommit();

    while (!GetAsyncKeyState(VK_END)) {
        Sleep(50);
    }

    OutputDebugString(L"Dettach and shutdown everything\r\n");

    isExiting = true;

    // SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(originalWndProc));
    // WNDPROC(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(originalWndProc)));

    OutputDebugString(L"DetourTransactionBegin\r\n");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    OutputDebugString(L"ImGui_ImplDX9_Shutdown\r\n");
    ImGui_ImplDX9_Shutdown();
    OutputDebugString(L"ImGui_ImplWin32_Shutdown\r\n");
    ImGui_ImplWin32_Shutdown();
    OutputDebugString(L"DestroyContext\r\n");
    ImGui::DestroyContext();

    OutputDebugString(L"SetWindowLongA\r\n");
    SetWindowLongA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));

    OutputDebugString(L"DetourDetach all\r\n");
    DetourDetach(&(PVOID&)realGetCurrentMana, _GetCurrentMana);
    DetourDetach(&(PVOID&)realCharacterAddMoney, _CharacterAddMoney);
    DetourDetach(&(PVOID&)realGetItemCost, _GetItemCost);
    Detach(GetCurrentMana);
    Detach(GetManaLimit);
    Detach(AreRequirementsMet);
    Detach(GetLevelRequirement);
    Detach(GetStrengthRequirement);
    Detach(GetDexterityRequirement);
    Detach(GetIntelligenceRequirement);
    Detach(GetModifierPoints);
    Detach(SetBaseValue);
    Detach(ReceiveExperience);
    Detach(GetMainPlayer);
    Detach(CreateRenderDevice);
    Detach(ResetRenderDevice);
    //        DetourDetach((PVOID*)pVTable[17], _Present);
    //        DetourDetach((PVOID*)pVTable[16], _Reset);
    DetourDetach(&(PVOID&)realPresent, _Present);
    DetourDetach(&(PVOID&)realReset, _Reset);
    DetourTransactionCommit();

    //    pVTable[42] = (DWORD)realEndScene;
        //pVTable[17] = (DWORD)realPresent;
        //pVTable[16] = (DWORD)realReset;
    FreeLibraryAndExitThread(hModule, 0);
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
// 
        CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0));
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        
    }

    return TRUE;
}

