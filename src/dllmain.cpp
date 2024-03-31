#include "stdafx.h"
#include "helper.hpp"
#include <inipp/inipp.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <safetyhook.hpp>

HMODULE baseModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Logger and config setup
inipp::Ini<char> ini;
std::shared_ptr<spdlog::logger> logger;
string sFixName = "DDDAFix";
string sFixVer = "0.8.0";
string sLogFile = "DDDAFix.log";
string sConfigFile = "DDDAFix.ini";
string sExeName;
filesystem::path sExePath;
filesystem::path sThisModulePath;
RECT rcDesktop;

// Aspect Ratio
float fPi = (float)3.141592653;
float fNativeAspect = (float)16 / 9;
float fAspectRatio;
float fAspectMultiplier;
float fHUDWidth;
float fHUDHeight;
float fDefaultHUDWidth = (float)1920;
float fDefaultHUDHeight = (float)1080;
float fHUDWidthOffset;
float fHUDHeightOffset;

// Ini variables
int iInjectionDelay;
bool bDisablePauseOnFocusLoss;
bool bUncapFPS;
bool bBorderlessWindowed;
bool bFixHUD;
bool bFixFOV;

// Variables
int iResX = 1920;
int iResY = 1080;
float fDefMinimapMulti = 0.0007812500116f;
int iOrigMinimapWidthOffset = 0;
LPCWSTR sWindowClassName = L"Dragon’s Dogma: Dark Arisen";

HWND hWnd;
WNDPROC OldWndProc;
LRESULT __stdcall NewWndProc(HWND window, UINT message_type, WPARAM w_param, LPARAM l_param) {
    if (bDisablePauseOnFocusLoss)
    {
        if (message_type == WM_ACTIVATEAPP && w_param == FALSE) {
            return 0;
        }
        else if (message_type == WM_KILLFOCUS) {
            return 0;
        }
    }
    return CallWindowProc(OldWndProc, window, message_type, w_param, l_param);
};

void Logging()
{
    // Get this module path
    WCHAR thisModulePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(thisModule, thisModulePath, MAX_PATH);
    sThisModulePath = thisModulePath;
    sThisModulePath = sThisModulePath.remove_filename();

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(baseModule, exePath, MAX_PATH);
    sExePath = exePath;
    sExeName = sExePath.filename().string();
    sExePath = sExePath.remove_filename();

    // spdlog initialisation
    {
        try
        {
            logger = spdlog::basic_logger_st(sFixName.c_str(), sThisModulePath.string() + sLogFile, true);
            spdlog::set_default_logger(logger);

            spdlog::flush_on(spdlog::level::debug);
            spdlog::info("----------");
            spdlog::info("{} v{} loaded.", sFixName.c_str(), sFixVer.c_str());
            spdlog::info("----------");
            spdlog::info("Path to logfile: {}", sThisModulePath.string() + sLogFile);
            spdlog::info("----------");

            // Log module details
            spdlog::info("Module Name: {0:s}", sExeName.c_str());
            spdlog::info("Module Path: {0:s}", sExePath.string());
            spdlog::info("Module Address: 0x{0:x}", (uintptr_t)baseModule);
            spdlog::info("Module Timestamp: {0:d}", Memory::ModuleTimestamp(baseModule));
            spdlog::info("----------");
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            AllocConsole();
            FILE* dummy;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            std::cout << "Log initialisation failed: " << ex.what() << std::endl;
        }
    }
}

void ReadConfig()
{
    // Initialise config
    std::ifstream iniFile(sThisModulePath.string() + sConfigFile);
    if (!iniFile)
    {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVer.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sThisModulePath.string().c_str() << std::endl;
    }
    else
    {
        spdlog::info("Path to config file: {}", sThisModulePath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Read ini file
    inipp::get_value(ini.sections["DDDAFix Parameters"], "InjectionDelay", iInjectionDelay);
    inipp::get_value(ini.sections["Raise Framerate Cap"], "Enabled", bUncapFPS);
    inipp::get_value(ini.sections["DisablePause on Focus Loss"], "Enabled", bDisablePauseOnFocusLoss);
    inipp::get_value(ini.sections["Borderless Windowed Mode"], "Enabled", bBorderlessWindowed);
    inipp::get_value(ini.sections["Fix HUD"], "Enabled", bFixHUD);
    inipp::get_value(ini.sections["Fix FOV"], "Enabled", bFixFOV);

    // Log config parse
    spdlog::info("Config Parse: iInjectionDelay: {}ms", iInjectionDelay);
    spdlog::info("Config Parse: bUncapFPS: {}", bUncapFPS);
    spdlog::info("Config Parse: bDisablePauseOnFocusLoss: {}", bDisablePauseOnFocusLoss);
    spdlog::info("Config Parse: bBorderlessWindowed: {}", bBorderlessWindowed);
    spdlog::info("Config Parse: bFixHUD: {}", bFixHUD);
    spdlog::info("Config Parse: bFixFOV: {}", bFixFOV);

    spdlog::info("----------");
}

void GetResolution()
{
    // Get current resolution
    uint8_t* CurrentResolutionScanResult = Memory::PatternScan(baseModule, "74 ?? 8B ?? ?? ?? ?? 00 8B ?? ?? ?? ?? 00 89 ?? ?? ?? 89 ?? ?? ?? EB ??");
    if (CurrentResolutionScanResult)
    {
        spdlog::info("Current Resolution: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)CurrentResolutionScanResult - (uintptr_t)baseModule);

        static SafetyHookMid CurrentResolutionMidHook{};
        CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult + 0xE,
            [](SafetyHookContext& ctx)
            {
                iResX = (int)ctx.ecx;
                iResY = (int)ctx.edx;

                fAspectRatio = (float)iResX / iResY;
                fAspectMultiplier = fAspectRatio / fNativeAspect;

                // HUD variables
                fHUDWidth = (float)iResY * fNativeAspect;
                fHUDHeight = (float)iResY;
                fHUDWidthOffset = (float)(iResX - fHUDWidth) / 2;
                fHUDHeightOffset = 0;
                if (fAspectRatio < fNativeAspect)
                {
                    fHUDWidth = (float)iResX;
                    fHUDHeight = (float)iResX / fNativeAspect;
                    fHUDWidthOffset = 0;
                    fHUDHeightOffset = (float)(iResY - fHUDHeight) / 2;
                }
            });
    }
    else if (!CurrentResolutionScanResult)
    {
        spdlog::error("Current Resolution: Pattern scan failed.");
    }
}

void HUD()
{
    // HUD Size
    uint8_t* HUDSizeScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? ?? 0F 57 ?? F3 0F ?? ?? 0F 28 ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 0F 28 ??");
    if (HUDSizeScanResult)
    {
        spdlog::info("HUD: HUDSize: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)HUDSizeScanResult - (uintptr_t)baseModule);

        static SafetyHookMid HUDWidthMidHook{};
        HUDWidthMidHook = safetyhook::create_mid(HUDSizeScanResult + 0x8,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)iResY;
                }
            });
    }
    else if (!HUDSizeScanResult)
    {
        spdlog::error("HUD: HUDSize: Pattern scan failed.");
    }

    // HUD Offset
    uint8_t* HUDOffsetScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? F3 0F ?? ?? ?? 83 ?? ?? FD 8B ?? ?? ?? 48 74 ?? 48 74 ??");
    if (HUDOffsetScanResult)
    {
        spdlog::info("HUDOffset: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)HUDOffsetScanResult - (uintptr_t)baseModule);

        static SafetyHookMid HUDOffsetMidHook{};
        HUDOffsetMidHook = safetyhook::create_mid(HUDOffsetScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)-1 / fAspectMultiplier;
                }
                else if (fAspectRatio < fNativeAspect)
                {
                    //ctx.xmm1.f32[0] = (float)1 * fAspectMultiplier;
                }
            });
    }
    else if (!HUDOffsetScanResult)
    {
        spdlog::error("HUD: HUDOffset: Pattern scan failed.");
    }

    uint8_t* HUDBackgrounds1ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? F3 0F ?? ?? ?? 0C 0F ?? ?? EB ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 8B ?? ??");
    uint8_t* HUDBackgrounds2ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? F3 0F ?? ?? ?? 10 0F ?? ?? EB ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 8B ?? ??");
    if (HUDBackgrounds1ScanResult && HUDBackgrounds2ScanResult)
    {
        spdlog::info("HUD: HUDBackgrounds: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)HUDBackgrounds1ScanResult - (uintptr_t)baseModule);
        spdlog::info("HUD: HUDBackgrounds: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)HUDBackgrounds2ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid HUDBackgrounds1MidHook1{};
        HUDBackgrounds1MidHook1 = safetyhook::create_mid(HUDBackgrounds1ScanResult + 0x8,
            [](SafetyHookContext& ctx)
            {
                if (ctx.esi + 0xD0)
                {
                    if ((ctx.xmm1.f32[0] == (float)1280 && *reinterpret_cast<float*>(ctx.esi + 0xD4) == (float)720))
                    {
                        if (fAspectRatio > fNativeAspect)
                        {
                            ctx.xmm1.f32[0] = (float)720 * fAspectRatio;
                        }
                    }
                }
            });

        static SafetyHookMid HUDBackgrounds1MidHook2{};
        HUDBackgrounds1MidHook2 = safetyhook::create_mid(HUDBackgrounds1ScanResult + 0x1F,
            [](SafetyHookContext& ctx)
            {
                if (ctx.esi + 0xD0)
                {
                    if ((ctx.xmm1.f32[0] == (float)1280 && *reinterpret_cast<float*>(ctx.esi + 0xD4) == (float)720))
                    {
                        if (fAspectRatio > fNativeAspect)
                        {
                            ctx.xmm0.f32[0] = -fHUDWidthOffset;
                            ctx.xmm1.f32[0] = (float)720 * fAspectRatio;
                        }
                    }
                }
            });

        static SafetyHookMid HUDBackgrounds2MidHook{};
        HUDBackgrounds2MidHook = safetyhook::create_mid(HUDBackgrounds2ScanResult + 0x8,
            [](SafetyHookContext& ctx)
            {
                if (ctx.edi + 0xD0)
                {
                    if ((ctx.xmm1.f32[0] == (float)1280 && *reinterpret_cast<float*>(ctx.edi + 0xD4) == (float)720))
                    {
                        if (fAspectRatio > fNativeAspect)
                        {
                            ctx.xmm1.f32[0] *= fAspectMultiplier;
                        }
                    }
                }
            });
    }
    else if (!HUDBackgrounds1ScanResult || !HUDBackgrounds2ScanResult)
    {
        spdlog::error("HUD: HUDBackgrounds: Pattern scan failed.");
    }
    
    // Subtitles 
    uint8_t* SubtitlesLayerScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? 2B ?? 8B ?? 2B ?? ?? ?? F3 0F ?? ?? ?? ??");
    if (SubtitlesLayerScanResult)
    {
        spdlog::info("HUD: SubtitlesLayer: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)SubtitlesLayerScanResult - (uintptr_t)baseModule);

        static SafetyHookMid SubtitlesLayerMidHook{};
        SubtitlesLayerMidHook = safetyhook::create_mid(SubtitlesLayerScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm1.f32[0] = fHUDWidth;
                } 
            });
    }
    else if (!SubtitlesLayerScanResult)
    {
        spdlog::error("HUD: SubtitlesLayer: Pattern scan failed.");
    }

    // Title background
    uint8_t* TitleBackgroundScanResult = Memory::PatternScan(baseModule, "0F 57 ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? 14 F3 0F ?? ?? ?? 10 F3 0F ?? ?? ?? F3 0F ?? ?? ?? ??");
    if (TitleBackgroundScanResult)
    {
        spdlog::info("HUD: TitleBackground: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)TitleBackgroundScanResult - (uintptr_t)baseModule);

        static SafetyHookMid TitleBackgroundMidHook{};
        TitleBackgroundMidHook = safetyhook::create_mid(TitleBackgroundScanResult + 0x17,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = 1.0f;
                }
            });
    }
    else if (!TitleBackgroundScanResult)
    {
        spdlog::error("HUD: TitleBackground: Pattern scan failed.");
    }

}

void MouseInput()
{
    // Reported mouse position
    uint8_t* MousePosScanResult = Memory::PatternScan(baseModule, "99 F7 ?? 8B ?? ?? ?? 89 ?? 8B ?? ?? ?? 2B ?? 0F ?? ?? ?? ?? 99 F7 ??") + 0x7;
    uint8_t* MapMousePosScanResult = Memory::PatternScan(baseModule, "75 ?? 8B ?? ?? 8B ?? ?? 89 ?? ?? ?? 8D ?? ?? ?? 8B ??") + 0x5;
    if (MousePosScanResult)
    {
        spdlog::info("MouseInput: MousePos: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MousePosScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: MapMousePos: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapMousePosScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MousePosXMidHook{};
        MousePosXMidHook = safetyhook::create_mid(MousePosScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.eax -= (int)fHUDWidthOffset;
                }
            });

        static SafetyHookMid MapMousePosXMidHook{};
        MapMousePosXMidHook = safetyhook::create_mid(MapMousePosScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.ecx += (int)fHUDWidthOffset;
                }
            });
    }
    else if (!MousePosScanResult)
    {
        spdlog::error("MouseInput: MousePos: Pattern scan failed.");
    }

    // Mouse position in menus
    uint8_t* MenuMouse1ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 8B ?? ?? ?? ?? 00 8B ?? ?? ?? ?? 00 0F 57 ?? F3 0F ?? ?? F3 0F ?? ??");
    uint8_t* MenuMouse2ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 56 8B ?? 8B ?? ?? ?? ?? 00 8B ?? ?? ?? ?? 00 0F 57 ?? F3 0F ?? ??");
    uint8_t* MenuMouse3ScanResult = Memory::PatternScan(baseModule, "F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? 0F 57 ?? 8B ?? ?? 83 ?? FF");
    if (MenuMouse1ScanResult && MenuMouse2ScanResult && MenuMouse3ScanResult)
    {
        spdlog::info("MouseInput: MenuMouse: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MenuMouse1ScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: MenuMouse: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MenuMouse2ScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: MenuMouse: 3: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MenuMouse3ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MenuMouse1MidHook1{};
        MenuMouse1MidHook1 = safetyhook::create_mid(MenuMouse1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)720 * fAspectRatio;
                }
            });

        static SafetyHookMid MenuMouse1MidHook2{};
        MenuMouse1MidHook2 = safetyhook::create_mid(MenuMouse1ScanResult + 0x2F,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] = fHUDWidth;
                }
            });

        static SafetyHookMid MenuMouse2MidHook1{};
        MenuMouse2MidHook1 = safetyhook::create_mid(MenuMouse2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)720 * fAspectRatio;
                }
            });

        static SafetyHookMid MenuMouse2MidHook2{};
        MenuMouse2MidHook2 = safetyhook::create_mid(MenuMouse2ScanResult + 0x32,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] = fHUDWidth;
                }
            });

        static SafetyHookMid MenuMouse3MidHook{};
        MenuMouse3MidHook = safetyhook::create_mid(MenuMouse3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm4.f32[0] = fHUDWidth;
                }
            });
    }
    else if (!MenuMouse1ScanResult || !MenuMouse2ScanResult || !MenuMouse3ScanResult)
    {
        spdlog::error("MouseInput: MenuMouse: Pattern scan failed.");
    }
 
    uint8_t* Scrollbar1ScanResult = Memory::PatternScan(baseModule, "0F 28 ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? 75 ??") + 0x3;
    uint8_t* Scrollbar2ScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? ?? ?? 8B ?? ?? ?? 0F 5B ??") + 0x10;
    uint8_t* Scrollbar3ScanResult = Memory::PatternScan(baseModule, "0F 57 ?? F3 0F ?? ?? ?? ?? ?? 00 8B ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F 59 ?? ?? ?? ?? ??");
    uint8_t* Scrollbar4ScanResult = Memory::PatternScan(baseModule, "0F 28 ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? 75 ?? 85 ??") + 0x3;
    if (Scrollbar1ScanResult && Scrollbar2ScanResult && Scrollbar3ScanResult && Scrollbar4ScanResult)
    {
        spdlog::info("MouseInput: Scrollbar: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)Scrollbar1ScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: Scrollbar: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)Scrollbar2ScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: Scrollbar: 3: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)Scrollbar3ScanResult - (uintptr_t)baseModule);
        spdlog::info("MouseInput: Scrollbar: 4: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)Scrollbar3ScanResult - (uintptr_t)baseModule);

        // Vert scroll bar 1
        static SafetyHookMid Scrollbar1MidHook1{};
        Scrollbar1MidHook1 = safetyhook::create_mid(Scrollbar1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = fHUDWidth;
                }
            });

        // Vert scroll bar 2
        static SafetyHookMid Scrollbar1MidHook2{};
        Scrollbar1MidHook2 = safetyhook::create_mid(Scrollbar1ScanResult + 0x25,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] = (float)720 * fAspectRatio;
                }
            });

        // Horizontal scroll bars
        static SafetyHookMid Scrollbar2MidHook{};
        Scrollbar2MidHook = safetyhook::create_mid(Scrollbar2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)720 * fAspectRatio;
                }
            });

        // Vert scroll bar 3
        static SafetyHookMid Scrollbar3MidHook1{};
        Scrollbar1MidHook1 = safetyhook::create_mid(Scrollbar3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm1.f32[0] = (float)720 * fAspectRatio;
                }
            });

        // Vert scroll bar 4
        static SafetyHookMid Scrollbar3MidHook2{};
        Scrollbar1MidHook2 = safetyhook::create_mid(Scrollbar3ScanResult + 0x17,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = fHUDWidth;
                }
            });

        // Vert scroll bars again
        static SafetyHookMid Scrollbar4MidHook1{};
        Scrollbar4MidHook1 = safetyhook::create_mid(Scrollbar4ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = fHUDWidth;
                }
            });

        // Vert scroll bars again
        static SafetyHookMid Scrollbar4MidHook2{};
        Scrollbar4MidHook2 = safetyhook::create_mid(Scrollbar4ScanResult + 0x25,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] = (float)720 * fAspectRatio;
                }
            });
    }
    else if (!Scrollbar1ScanResult || !Scrollbar2ScanResult || !Scrollbar3ScanResult || !Scrollbar4ScanResult)
    {
        spdlog::error("MouseInput: Scrollbar: Pattern scan failed.");
    }
}

void Markers()
{
    // Interaction markers
    uint8_t* MarkersScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 00 0F 57 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ??");
    if (MarkersScanResult)
    {
        spdlog::info("Markers: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MarkersScanResult - (uintptr_t)baseModule);
        
        static SafetyHookMid MarkersMidHook{};
        MarkersMidHook = safetyhook::create_mid(MarkersScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] /= (float)1280;
                    ctx.xmm0.f32[0] *= (float)720 * fAspectRatio;
                }
            });

        static SafetyHookMid MarkersOffsetMidHook{};
        MarkersOffsetMidHook = safetyhook::create_mid(MarkersScanResult + 0x30,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });   
    }
    else if (!MarkersScanResult)
    {
        spdlog::error("Markers: Pattern scan failed.");
    }
}

void Minimap()
{
    // Minimap width multiplier
    uint8_t* MinimapWidthMultiScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? 00 D9 ?? ??") + 0xB;
    if (MinimapWidthMultiScanResult)
    {
        spdlog::info("Minimap: MinimapWidthMulti: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapWidthMultiScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapWidthMultiMidHook{};
        MinimapWidthMultiMidHook = safetyhook::create_mid(MinimapWidthMultiScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = fHUDWidth;
                }
            });
    }
    else if (!MinimapWidthMultiScanResult)
    {
        spdlog::error("Minimap: MinimapWidthMulti: Pattern scan failed.");
    }

    // Minimap texture size and position
    uint8_t* MinimapTextureScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 8B ?? ?? ?? 89 ?? ?? ??") + 0x8;
    uint8_t* MinimapTexturePositionScanResult = Memory::PatternScan(baseModule, "D9 ?? ?? 8B ?? ?? ?? ?? 00 8B ?? 68 ?? ?? ?? ?? 57") + 0x3;
    if (MinimapTextureScanResult && MinimapTexturePositionScanResult)
    {
        spdlog::info("Minimap: MinimapTexture: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapTextureScanResult - (uintptr_t)baseModule);
        spdlog::info("Minimap: MinimapTexture: Position: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapTexturePositionScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapTexture1MidHook{};
        MinimapTexture1MidHook = safetyhook::create_mid(MinimapTextureScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)iResX * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MinimapTexture2MidHook{};
        MinimapTexture2MidHook = safetyhook::create_mid(MinimapTextureScanResult + 0x92, // Big gap, maybe do a second pattern?
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)iResX * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MinimapTexturePositionMidHook{};
        MinimapTexturePositionMidHook = safetyhook::create_mid(MinimapTexturePositionScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    if (ctx.edx + 0x24)
                    {
                        *reinterpret_cast<float*>(ctx.edx + 0x24) = (float)iResX * fDefMinimapMulti;
                    }
                }
            });
    }
    else if (!MinimapTextureScanResult || !MinimapTexturePositionScanResult)
    {
        spdlog::error("Minimap: MinimapTexture: Pattern scan failed.");
    }

    // Minimap fog
    uint8_t* MinimapFog1ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ??  0F 28 ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ??");
    uint8_t* MinimapFog2ScanResult = Memory::PatternScan(baseModule, "F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F 59 ?? ?? ?? ?? ??");
    if (MinimapFog1ScanResult && MinimapFog2ScanResult)
    {
        spdlog::info("Minimap: MinimapFog: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapFog1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Minimap: MinimapFog: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapFog2ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapFog1MidHook{};
        MinimapFog1MidHook = safetyhook::create_mid(MinimapFog1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm1.f32[0] = (float)iResX * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MinimapFog2MidHook1{};
        MinimapFog2MidHook1 = safetyhook::create_mid(MinimapFog2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm4.f32[0] = (float)iResX * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MinimapFog2MidHook2{};
        MinimapFog2MidHook2 = safetyhook::create_mid(MinimapFog2ScanResult + 0x21,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm1.f32[0] = (float)iResX * fDefMinimapMulti;
                }
            });
    }
    else if (!MinimapFog1ScanResult || !MinimapFog2ScanResult)
    {
        spdlog::error("Minimap: MinimapFog: Pattern scan failed.");
    }

    // Minimap icons height offset
    uint8_t* MinimapIconHeightOffsetScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? 00 83 ?? ?? 01 8B ?? ?? F3 0F ?? ?? F3 0F ?? ??");
    if (MinimapIconHeightOffsetScanResult)
    {
        spdlog::info("Minimap: MinimapIconHeightOffset: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapIconHeightOffsetScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapIconHeightOffsetMidHook{};
        MinimapIconHeightOffsetMidHook = safetyhook::create_mid(MinimapIconHeightOffsetScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    if (ctx.esi + 0x500)
                    {
                        *reinterpret_cast<float*>(ctx.esi + 0x500) = 0.0f;
                    }
                }
            });
    }
    else if (!MinimapIconHeightOffsetScanResult)
    {
        spdlog::error("Minimap: MinimapIconHeightOffset: Pattern scan failed.");
    }

    // Minimap height offset
    uint8_t* MinimapHeightOffsetScanResult = Memory::PatternScan(baseModule, "0F 57 ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 0F 57 ?? 0F 57 ??") + 0x7;
    if (MinimapHeightOffsetScanResult)
    {
        spdlog::info("Minimap: MinimapHeightOffset: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapHeightOffsetScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapHeightOffsetMidHook{};
        MinimapHeightOffsetMidHook = safetyhook::create_mid(MinimapHeightOffsetScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = fHUDWidth;
                }
            });
    }
    else if (!MinimapHeightOffsetScanResult)
    {
        spdlog::error("Minimap: MinimapHeightOffset: Pattern scan failed.");
    }

    // Minimap width offset
    uint8_t* MinimapWidthOffset1ScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? ?? ?? 0F 28 ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 0F 28 ?? 0F 57 ?? ?? ?? ?? ??");
    uint8_t* MinimapWidthOffset2ScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 0F 5B ?? F3 0F ?? ?? ?? ?? 66 0F ?? ?? ?? ?? ?? 00 0F 5B ??");
    uint8_t* MinimapWidthOffset3ScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 8B ?? ?? ?? 0F 28 ?? F3 0F 59 ?? ?? ?? F3 0F 59 ?? ?? ??");
    uint8_t* MinimapWidthOffset4ScanResult = Memory::PatternScan(baseModule, "66 0F ?? ?? ?? ?? ?? 00 F3 0F 10 ?? ?? ?? ?? ?? F3 0F 5E ?? ?? ?? ?? 00 F3 0F 10 ?? ?? ?? ?? ??");
    if (MinimapWidthOffset1ScanResult && MinimapWidthOffset2ScanResult && MinimapWidthOffset3ScanResult && MinimapWidthOffset4ScanResult)
    {
        spdlog::info("Minimap: MinimapWidthOffset: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapWidthOffset1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Minimap: MinimapWidthOffset: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapWidthOffset2ScanResult - (uintptr_t)baseModule);
        spdlog::info("Minimap: MinimapWidthOffset: 3: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapWidthOffset3ScanResult - (uintptr_t)baseModule);
        spdlog::info("Minimap: MinimapWidthOffset: 4: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MinimapWidthOffset4ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MinimapWidthOffset1MidHook{};
        MinimapWidthOffset1MidHook = safetyhook::create_mid(MinimapWidthOffset1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    iOrigMinimapWidthOffset = (((iResX * fDefMinimapMulti) * 256) / 2) + ((iResX * fDefMinimapMulti) * 40);
                    if (ctx.edi + 0x468)
                    {
                        *reinterpret_cast<int*>(ctx.edi + 0x468) = (int)floorf(iOrigMinimapWidthOffset / fAspectMultiplier);
                    }
                }
            });
        
        static SafetyHookMid MinimapWidthOffset2MidHook{};
        MinimapWidthOffset2MidHook = safetyhook::create_mid(MinimapWidthOffset2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    if (ctx.eax + 0x468)
                    {
                        *reinterpret_cast<int*>(ctx.eax + 0x468) = (int)floorf((iOrigMinimapWidthOffset / fAspectMultiplier) + fHUDWidthOffset);
                    }
                }
            });

        static SafetyHookMid MinimapWidthOffset3MidHook{};
        MinimapWidthOffset3MidHook = safetyhook::create_mid(MinimapWidthOffset3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    iOrigMinimapWidthOffset = (((iResX * fDefMinimapMulti) * 256) / 2) + ((iResX * fDefMinimapMulti) * 40);
                    if (ctx.edi + 0x468)
                    {
                        *reinterpret_cast<int*>(ctx.edi + 0x468) = (int)floorf(iOrigMinimapWidthOffset / fAspectMultiplier);
                    }
                }
            });  

        static SafetyHookMid MinimapWidthOffset4MidHook{};
        MinimapWidthOffset4MidHook = safetyhook::create_mid(MinimapWidthOffset4ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    iOrigMinimapWidthOffset = (((iResX * fDefMinimapMulti) * 256) / 2) + ((iResX * fDefMinimapMulti) * 40);
                    if (ctx.edi + 0x468)
                    {
                        *reinterpret_cast<int*>(ctx.edi + 0x468) = (int)floorf(iOrigMinimapWidthOffset / fAspectMultiplier);
                    }
                }
            });
    }
    else if (!MinimapWidthOffset1ScanResult || !MinimapWidthOffset2ScanResult || !MinimapWidthOffset3ScanResult || !MinimapWidthOffset4ScanResult)
    {
        spdlog::error("Minimap: MinimapWidthOffset: Pattern scan failed.");
    }
}

void Map()
{
    // Map frame
    uint8_t* MapFrameScanResult = Memory::PatternScan(baseModule, "A1 ?? ?? ?? ?? 8B ?? ?? ?? ?? 00 8B ?? ?? ?? ?? 00 0F ?? ?? F3 0F ?? ?? 0F 28 ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F 59 0D ?? ?? ?? ??");
    if (MapFrameScanResult)
    {
        spdlog::info("Map: MapFrame: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapFrameScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapFrameMidHook{};
        MapFrameMidHook = safetyhook::create_mid(MapFrameScanResult + 0x11,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    int iHUDWidth = (int)fHUDWidth;
                    ctx.ecx = *(uint32_t*)&iHUDWidth;
                }
            });
    }
    else if (!MapFrameScanResult)
    {
        spdlog::error("Map: MapFrame: Pattern scan failed.");
    }

    // Map location menu
    uint8_t* MapLocationMenuScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? 0F 28 ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? ?? ?? ?? ?? 89 ?? ?? ?? 8B ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 85 ?? 74 ?? 8B ?? ?? ?? ?? 00 EB ?? 33 ??");
    if (MapLocationMenuScanResult)
    {
        spdlog::info("Map: MapLocationMenu: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapLocationMenuScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapLocationMenuMidHook{};
        MapLocationMenuMidHook = safetyhook::create_mid(MapLocationMenuScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    int iHUDWidth = (int)fHUDWidth;
                    ctx.ecx = *(uint32_t*)&iHUDWidth;
                }
            });
    }
    else if (!MapLocationMenuScanResult)
    {
        spdlog::error("Map: MapLocationMenu: Pattern scan failed.");
    }

    // Map position hor offset
    uint8_t* MapPosOffsetScanResult = Memory::PatternScan(baseModule, "E9 ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? 00 33 ?? BE ?? ?? ?? 00") + 0x5;
    if (MapPosOffsetScanResult)
    {
        spdlog::info("Map: MapPosOffset: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapPosOffsetScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapPosOffsetMidHook{};
        MapPosOffsetMidHook = safetyhook::create_mid(MapPosOffsetScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm6.f32[0] = fHUDWidthOffset;
                }
            });
    }
    else if (!MapPosOffsetScanResult)
    {
        spdlog::error("Map: MapPosOffset: Pattern scan failed.");
    }

    // Map cursor boundary
    uint8_t* MapCursor1ScanResult = Memory::PatternScan(baseModule, "F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F 10 ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? F3 0F 10 ?? ?? ?? ?? 00");
    uint8_t* MapCursor2ScanResult = Memory::PatternScan(baseModule, "F3 0F 59 ?? ?? ?? ?? ?? 0F 28 ?? 0F 57 ?? 89 ?? ?? ?? 8B ?? ?? 8B ?? ??");
    if (MapCursor1ScanResult && MapCursor2ScanResult)
    {
        spdlog::info("Map: MapCursor: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapCursor1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapCursor: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapCursor2ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapCursor1MidHook{};
        MapCursor1MidHook = safetyhook::create_mid(MapCursor1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = fHUDWidth;
                }
            });

        static SafetyHookMid MapCursor2MidHook{};
        MapCursor2MidHook = safetyhook::create_mid(MapCursor2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm5.f32[0] = fHUDWidth;
                }
            });
    }
    else if (!MapCursor1ScanResult || !MapCursor2ScanResult)
    {
        spdlog::error("Map: MapCursor: Pattern scan failed.");
    }

    // Map cursor offset
    uint8_t* MapCursorOffset1ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? 66 0F ?? ?? ?? ?? ?? 00 0F 5B ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? 0F 57 ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 85 ?? 74 ??") + 0x3;
    uint8_t* MapCursorOffset2ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? EB ??") + 0x3;
    uint8_t* MapCursorOffset3ScanResult = Memory::PatternScan(baseModule, "0F ?? ?? F3 0F ?? ?? ?? ?? ?? 00 33 ?? 8D ?? ?? ?? ?? 00 8B ??") + 0x3;
    if (MapCursorOffset1ScanResult && MapCursorOffset2ScanResult && MapCursorOffset3ScanResult)
    {
        spdlog::info("Map: MapCursorOffset: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapCursorOffset1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapCursorOffset: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapCursorOffset2ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapCursorOffset: 3: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapCursorOffset3ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapCursorOffset1MidHook{};
        MapCursorOffset1MidHook = safetyhook::create_mid(MapCursorOffset1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] -= fHUDWidthOffset;
                }
            });

        static SafetyHookMid MapCursorOffset2MidHook{};
        MapCursorOffset2MidHook = safetyhook::create_mid(MapCursorOffset2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] -= fHUDWidthOffset;
                }
            });

        static SafetyHookMid MapCursorOffset3MidHook{};
        MapCursorOffset3MidHook = safetyhook::create_mid(MapCursorOffset3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] -= fHUDWidthOffset;
                }
            });
    }
    else if (!MapCursorOffset1ScanResult || !MapCursorOffset2ScanResult || !MapCursorOffset3ScanResult)
    {
        spdlog::error("Map: MapCursorOffset: Pattern scan failed.");
    }

    // Map icons width offset
    uint8_t* MapIconWidthOffset1ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? ?? ?? ?? ?? 51 F3 0F ?? ?? F3 0F ?? ?? 8B ??");
    uint8_t* MapIconWidthOffset2ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? 8D ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? 00 52 8B ??");
    uint8_t* MapIconWidthOffset3ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? 00 51 8B ?? ?? ??");
    uint8_t* MapIconWidthOffset4ScanResult = Memory::PatternScan(baseModule, "51 8B ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? 00 51 8B ??");
    uint8_t* MapIconWidthOffset5ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? 00 F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 85 ??");
    if (MapIconWidthOffset1ScanResult && MapIconWidthOffset2ScanResult && MapIconWidthOffset3ScanResult && MapIconWidthOffset4ScanResult && MapIconWidthOffset5ScanResult)
    {
        spdlog::info("Map: MapIconWidthOffset: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapIconWidthOffset1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapIconWidthOffset: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapIconWidthOffset2ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapIconWidthOffset: 4: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapIconWidthOffset4ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapIconWidthOffset: 5: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapIconWidthOffset5ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapIconWidthOffset1MidHook{};
        MapIconWidthOffset1MidHook = safetyhook::create_mid(MapIconWidthOffset1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });

        static SafetyHookMid MapIconWidthOffset2MidHook{};
        MapIconWidthOffset2MidHook = safetyhook::create_mid(MapIconWidthOffset2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });

        static SafetyHookMid MapIconWidthOffset3MidHook{};
        MapIconWidthOffset3MidHook = safetyhook::create_mid(MapIconWidthOffset3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });

        static SafetyHookMid MapIconWidthOffset4MidHook{};
        MapIconWidthOffset4MidHook = safetyhook::create_mid(MapIconWidthOffset4ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm2.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });

        static SafetyHookMid MapIconWidthOffset5MidHook{};
        MapIconWidthOffset5MidHook = safetyhook::create_mid(MapIconWidthOffset5ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm1.f32[0] -= (float)((720 * fAspectRatio) - 1280) / 2;
                }
            });
    }
    else if (!MapIconWidthOffset1ScanResult || !MapIconWidthOffset2ScanResult || !MapIconWidthOffset3ScanResult || !MapIconWidthOffset4ScanResult || !MapIconWidthOffset5ScanResult)
    {
        spdlog::error("Map: MapIconWidthOffset: Pattern scan failed.");
    }

    // Map Area
    uint8_t* MapArea1ScanResult = Memory::PatternScan(baseModule, "C1 ?? 02 2B ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? D1 F8");
    uint8_t* MapArea2ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F ?? ?? 84 ?? 74 ?? 66 0F ?? ?? ?? ?? ?? 00 0F 28 ??");
    uint8_t* MapArea3ScanResult = Memory::PatternScan(baseModule, "0F 5B ?? F3 0F 59 ?? ?? ?? ?? ?? F3 0F 59 ?? F3 0F ?? ?? ?? ?? ?? 00") + 0xB;
    uint8_t* MapArea4ScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? 00 8B ?? ?? 8B ?? ?? 8B ?? ?? 89 ?? ?? ?? 89 ?? ?? ?? 74 ??");
    uint8_t* MapArea5ScanResult = Memory::PatternScan(baseModule, "DB ?? ?? ?? ?? 00 D8 ?? ?? ?? ?? ?? D9 ?? ?? D8 ?? ?? ?? ?? ??");
    if (MapArea1ScanResult)
    {
        spdlog::info("Map: MapArea: 1: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapArea1ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapArea: 2: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapArea2ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapArea: 3: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapArea3ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapArea: 4: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapArea4ScanResult - (uintptr_t)baseModule);
        spdlog::info("Map: MapArea: 5: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MapArea5ScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MapArea1MidHook1{};
        MapArea1MidHook1 = safetyhook::create_mid(MapArea1ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)fHUDWidth * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MapArea1MidHook2{};
        MapArea1MidHook2 = safetyhook::create_mid(MapArea1ScanResult + 0x3F,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm3.f32[0] = (float)fHUDWidth * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MapArea2MidHook1{};
        MapArea2MidHook1 = safetyhook::create_mid(MapArea2ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)fHUDWidth * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MapArea3MidHook1{};
        MapArea3MidHook1 = safetyhook::create_mid(MapArea3ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)fHUDWidth * fDefMinimapMulti;
                }
            });

        static SafetyHookMid MapArea4MidHook1{};
        MapArea4MidHook1 = safetyhook::create_mid(MapArea4ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm6.f32[0] = fDefMinimapMulti / fAspectMultiplier;
                }
            });

        // Patch offset to match empty data location
        // There's no way to access the FPU registers with SafetyHook (that I know of), hence this somewhat hacky solution.
        Memory::PatchBytes((uintptr_t)MapArea5ScanResult + 0x2, "\xB4", 1);

        static SafetyHookMid MapArea5MidHook1{};
        MapArea5MidHook1 = safetyhook::create_mid(MapArea5ScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    if (ctx.eax + 0xB4)
                    {
                        // Insert fHUDWidth into empty space
                        *reinterpret_cast<int*>(ctx.eax + 0xB4) = (int)fHUDWidth;
                    }
                }
            });
    }
    else if (!MapArea1ScanResult || !MapArea2ScanResult || !MapArea3ScanResult || !MapArea4ScanResult || !MapArea5ScanResult)
    {
        spdlog::error("Map: MapArea: Pattern scan failed.");
    }

}

void Movie()
{
    // FMVs
    uint8_t* MovieScanResult = Memory::PatternScan(baseModule, "8B ?? ?? ?? 89 ?? ?? 89 ?? ?? 8B ?? E8 ?? ?? ?? ?? 8B ?? E8 ?? ?? ?? ?? 5E");
    if (MovieScanResult)
    {
        spdlog::info("Movie: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)MovieScanResult - (uintptr_t)baseModule);

        static SafetyHookMid MovieMidHook{};
        MovieMidHook = safetyhook::create_mid(MovieScanResult,
            [](SafetyHookContext& ctx)
            {
                    if (ctx.eax)
                    {
                        if (fAspectRatio > fNativeAspect)
                        {
                            *reinterpret_cast<float*>(ctx.eax) = (float)-1 / fAspectMultiplier;
                            *reinterpret_cast<float*>(ctx.eax + 0x10) = (float)-1 / fAspectMultiplier;
                            *reinterpret_cast<float*>(ctx.eax + 0x20) = (float)1 / fAspectMultiplier;
                            *reinterpret_cast<float*>(ctx.eax + 0x30) = (float)1 / fAspectMultiplier;
                        }
                    }
            });
    }
    else if (!MovieScanResult)
    {
        spdlog::error("Movie: Pattern scan failed.");
    }
}

void FOV()
{
    if (bFixFOV)
    {
        // Cutscene FOV
        uint8_t* CutsceneFOVScanResult = Memory::PatternScan(baseModule, "76 ?? 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? 8B ?? ?? ?? 83 ?? ??");
        if (CutsceneFOVScanResult)
        {
            spdlog::info("FOV: CutsceneFOV: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)CutsceneFOVScanResult - (uintptr_t)baseModule);

            static SafetyHookMid CutsceneFOVMidHook{};
            CutsceneFOVMidHook = safetyhook::create_mid(CutsceneFOVScanResult + 0x5,
                [](SafetyHookContext& ctx)
                {
                    if (fAspectRatio > fNativeAspect)
                    {
                        ctx.xmm0.f32[0] = atanf(tanf(ctx.xmm0.f32[0] * (fPi / 360)) / fNativeAspect * fAspectRatio) * (360 / fPi);
                    }
                });
        }
        else if (!CutsceneFOVScanResult)
        {
            spdlog::error("FOV: CutsceneFOV: Pattern scan failed.");
        }
    }
}

void Miscellaneous()
{
    // Fix broken depth of field
    uint8_t* DOFFixScanResult = Memory::PatternScan(baseModule, "F3 0F ?? ?? ?? ?? ?? ?? 0F ?? ?? 0F ?? ?? 56 57 8B ??");
    if (DOFFixScanResult)
    {
        spdlog::info("DOFFix: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)DOFFixScanResult - (uintptr_t)baseModule);

        static SafetyHookMid DOFFixMidHook{};
        DOFFixMidHook = safetyhook::create_mid(DOFFixScanResult,
            [](SafetyHookContext& ctx)
            {
                if (fAspectRatio > fNativeAspect)
                {
                    ctx.xmm0.f32[0] = (float)720;
                }
            });
    }
    else if (!DOFFixScanResult)
    {
        spdlog::error("DOFFix: Pattern scan failed.");
    }

    if (bUncapFPS)
    {
        // Variable FPS cap
        uint8_t* FPSCapScanResult = Memory::PatternScan(baseModule, "8B ?? ?? 83 ?? 00 74 ?? 48 74 ?? 48 75 ?? F3 0F ?? ?? ?? ?? ?? ?? EB ?? F3 0F ?? ?? ?? ?? ?? ?? EB ?? F3 0F ?? ?? ?? ?? ?? ??") + 0xE;
        if (FPSCapScanResult)
        {
            spdlog::info("FPSCap: Address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)FPSCapScanResult - (uintptr_t)baseModule);
            DWORD VariableFPSValue = Memory::GetAbsolute32((uintptr_t)FPSCapScanResult + 0x4);
            spdlog::info("FPSCap: Value address is {:s}+{:x}", sExeName.c_str(), (uintptr_t)VariableFPSValue - (uintptr_t)baseModule);

            if (VariableFPSValue)
            {
                Memory::Write((uintptr_t)VariableFPSValue, (float)1000);
            }
        }
        else if (!FPSCapScanResult)
        {
            spdlog::error("FPSCap: Pattern scan failed.");
        }
    }
}

void WindowFocus()
{
    int i = 0;
    while (i < 30 && !IsWindow(hWnd))
    {
        // Wait 1 sec then try again
        Sleep(1000);
        i++;
        hWnd = FindWindowW(sWindowClassName, nullptr);
    }

    // If 30 seconds have passed and we still dont have the handle, give up
    if (i == 30)
    {
        spdlog::error("Window Focus: Failed to find window handle.");
        return;
    }
    else
    {
        // Set new wnd proc
        OldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)NewWndProc);
        spdlog::info("Window Focus: Set new WndProc.");

        if (bBorderlessWindowed)
        {
            LONG lStyle = GetWindowLong(hWnd, GWL_STYLE);
            LONG lExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);

            lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZE | WS_MINIMIZE);
            lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_COMPOSITED | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_LAYERED | WS_EX_STATICEDGE | WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);

            SetWindowLong(hWnd, GWL_STYLE, lStyle);
            SetWindowLong(hWnd, GWL_EXSTYLE, lExStyle);

            GetWindowRect(GetDesktopWindow(), &rcDesktop);
            SetWindowPos(hWnd, HWND_TOP, 0, 0, rcDesktop.right, rcDesktop.bottom, NULL);
            spdlog::info("Window Focus: Set borderless windowed mode.");
        }
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    ReadConfig();
    Sleep(iInjectionDelay);
    GetResolution();
    if (bFixHUD)
    {
        HUD();
        MouseInput();
        Markers();
        Minimap();
        Map();
        Movie();
    }
    FOV();
    Miscellaneous();
    WindowFocus();
    return true;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        thisModule = hModule;
        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST); // set our Main thread priority higher than the games thread
            CloseHandle(mainHandle);
        }
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

