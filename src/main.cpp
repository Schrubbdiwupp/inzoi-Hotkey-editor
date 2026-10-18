// F6-Editor: edits the inZOI key guide (WBP_HUD_ShoulderViewControlGuide) and installs it as an override pak.
// Command line (no window, for scripts): F6Editor.exe --load | --install | --remove | --hotkeys
//   (German aliases: --laden | --ins-spiel | --entfernen)
//   -> log in <daten>\letzter_lauf.log, exit code 0 = ok.
#include <d3d11.h>
#include <shellapi.h>
#include <tchar.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "editor_ui.h"
#include "hotkeys.h"
#include "widget_asset.h"

#include <filesystem>
#include <fstream>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;

    void CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            device->CreateRenderTargetView(backBuffer, nullptr, &renderTarget);
            backBuffer->Release();
        }
    }

    void CleanupRenderTarget()
    {
        if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; }
    }

    bool CreateDeviceD3D(HWND window)
    {
        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL level;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                                 &desc, &swapChain, &device, &level, &deviceContext)))
            return false;
        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (swapChain) { swapChain->Release(); swapChain = nullptr; }
        if (deviceContext) { deviceContext->Release(); deviceContext = nullptr; }
        if (device) { device->Release(); device = nullptr; }
    }

    LRESULT WINAPI WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return TRUE;
        switch (message)
        {
        case WM_SIZE:
            if (device && wParam != SIZE_MINIMIZED)
            {
                CleanupRenderTarget();
                swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { 900, 560 };
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(window, message, wParam, lParam);
    }

    // Command line mode: same steps as the buttons, synchronous.
    int RunCommandLine(const std::wstring& command)
    {
        LoadSettings();
        const Settings s = AppSettings();
        std::vector<std::string> log;
        bool ok = false;
        if (command == L"--laden" || command == L"--load")
            ok = Pipeline::ExtractOriginal(s, log);
        else if (command == L"--entfernen" || command == L"--remove")
            ok = Pipeline::RemoveOverride(s, log);
        else if (command == L"--hotkeys")
        {
            std::string error;
            const std::vector<HotkeyEntry> entries = Hotkeys::Scan(s, error);
            if (!error.empty()) log.push_back("FEHLER: " + error);
            for (const HotkeyEntry& e : entries)
                log.push_back(e.combo.Display() + " | " + e.modName + (e.Active() ? "" : " (aus)") + " | " + e.app + " | " + e.mountId);
            ok = error.empty();
        }
        else if (command == L"--ins-spiel" || command == L"--install" || command == L"--build")
        {
            const bool install = command != L"--build";   // --build: pak in <data>\out, game folder untouched
            WidgetAsset asset;
            ChangeMap changes;
            NewRows newRows;
            std::string error;
            if (!asset.Load(s.OriginalJson(), s.EnglishTexts(), error)) log.push_back("FEHLER: " + error);
            else
            {
                LoadChanges(s.ChangesJson(), changes, newRows);
                const std::string modified = s.dataDir + "\\geaendert.json";
                const int n = asset.WriteModified(changes, newRows, modified, error);
                if (n < 0) log.push_back("FEHLER: " + error);
                else
                {
                    log.push_back(std::to_string(n) + " Zeile(n) geändert.");
                    ok = Pipeline::BuildAndInstall(s, modified, log, install);
                }
            }
        }
        else
            log.push_back("Unknown command. Allowed: --load, --install, --build, --remove, --hotkeys");

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(s.dataDir), ec);
        std::ofstream out(std::filesystem::u8path(s.dataDir + "\\letzter_lauf.log"), std::ios::trunc);
        for (const std::string& line : log) out << line << "\n";
        out << (ok ? "OK" : "FEHLGESCHLAGEN") << "\n";
        return ok ? 0 : 1;
    }
}

int APIENTRY _tWinMain(HINSTANCE instance, HINSTANCE, LPTSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1)
    {
        const int code = RunCommandLine(argv[1]);
        LocalFree(argv);
        return code;
    }
    LocalFree(argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // file/folder dialogs
    LoadSettings();

    const HICON icon = LoadIcon(instance, MAKEINTRESOURCE(1));
    WNDCLASSEX windowClass = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProcedure, 0, 0, instance, icon,
                               LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, _T("F6EditorWindow"), icon };
    RegisterClassEx(&windowClass);
    HWND window = CreateWindow(windowClass.lpszClassName, _T("F6-Editor  -  inZOI Tastenhilfe"), WS_OVERLAPPEDWINDOW,
                               120, 80, 1280, 820, nullptr, nullptr, instance, nullptr);
    if (!CreateDeviceD3D(window))
    {
        CleanupDeviceD3D();
        MessageBox(nullptr, _T("Direct3D 11 konnte nicht gestartet werden."), _T("F6-Editor"), MB_ICONERROR);
        return 1;
    }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Segoe UI: Latin + Latin-1 (umlauts) plus dashes, quotes, euro and arrows that people type into texts.
    static ImVector<ImWchar> glyphRanges;
    ImFontGlyphRangesBuilder rangeBuilder;
    rangeBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    static const ImWchar extraRanges[] = { 0x2010, 0x205E, 0x20AC, 0x20AC, 0x2190, 0x21FF, 0 };
    rangeBuilder.AddRanges(extraRanges);
    rangeBuilder.BuildRanges(&glyphRanges);
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 19.0f, nullptr, glyphRanges.Data))
        io.Fonts->AddFontDefault();
    EditorUi::ApplyTheme();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, deviceContext);
    EditorUi::Init(window);

    bool running = true;
    while (running)
    {
        MSG message;
        while (PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(window)) { Sleep(50); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        EditorUi::Draw();
        ImGui::Render();
        const float clear[4] = { 0.055f, 0.060f, 0.078f, 1.0f };
        deviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);
        deviceContext->ClearRenderTargetView(renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(1, 0);
    }

    // Wait for a running job so no half-written pak is left behind.
    while (Pipeline::Busy()) Sleep(50);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(window);
    UnregisterClass(windowClass.lpszClassName, instance);
    return 0;
}
