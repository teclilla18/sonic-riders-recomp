/* Sonic Riders launcher (Windows): a small window in front of the native port.
 *
 * It holds no game code and no game data, so it can ship ready-made in the release zip. It lives in the
 * port's root folder (next to setup\, build\, baserom\, saves\, mods\) and:
 *   - checks what is there (game built, disc image linked, saves, graphics card),
 *   - "Install and play" the first time: asks for the disc image and runs setup\setup.ps1 for the PC build,
 *     with its steps as a progress bar,
 *   - edits saves\settings.ini (the in-game F1 menu's settings) and mods\mods.ini (order and on/off),
 *   - starts and stops build\host-mingw\sonicriders.exe.
 * As "Install Sonic Riders (Switch|Android).exe" it is the installer for those builds (installer mode below).
 * Banner: launcher\banner.jpg, public fan art chosen by the project owner (not from the game).
 * Win32 + Direct3D 11 + Dear ImGui, linked statically (tools/build_launcher.py). */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../runtime/src/update_core.h"  /* newest release on GitHub (WinHTTP) */
#include "lobby.h"                        /* the Online page (UDP, its own thread) */
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

/* ---------------------------------------------------------------- paths and small helpers */

std::wstring g_root;  /* the folder the launcher lives in (the port's root) */

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring path(const wchar_t* rel) { return g_root + L"\\" + rel; }
bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::string read_text(const std::wstring& p) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}
bool write_text(const std::wstring& p, const std::string& s) {
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (!f) return false;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    return true;
}
std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        std::string l = s.substr(a, b - a);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (b < s.size() || !l.empty()) v.push_back(l);
        a = b + 1;
    }
    return v;
}
void open_in_explorer(const std::wstring& p) { ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

/* the first *.iso in baserom\ (setup links the player's disc image there) */
std::wstring find_disc() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path(L"baserom\\*.iso").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    FindClose(h);
    return path(L"baserom\\") + fd.cFileName;
}

/* ---------------------------------------------------------------- settings.ini (shared with the game) */

/* the keys and meanings of runtime/src/settings.{h,c}; unknown keys are kept as they are */
struct Settings {
    int screen = 0;       /* 0 4:3, 1 16:9 widescreen, 3 fit window (2 was removed: reads as 1) */
    int show_fps = 0;
    int fullscreen = 0;
    int framerate = 1;    /* 0 60 fps, 1 auto, 2 30 fps */
    int pc_fps = 0;       /* pictures per second with in-between pictures: 0 = the game's own, -1 = unlimited */
    int hud_stretch = 0;
    int fov = 100;        /* 80..130 percent */
    int chaos_emerald_shop = 0;
    int pad_background = 1;  /* controllers work while the game window is not focused */
    int sharpness = 0;       /* FSR sharpening of the final picture, 0 (off)..100 percent */
    std::vector<std::pair<std::string, std::string>> other;
} g_set;
bool g_set_dirty;

int* setting_ptr(const std::string& k) {
    if (k == "screen") return &g_set.screen;
    if (k == "show_fps") return &g_set.show_fps;
    if (k == "fullscreen") return &g_set.fullscreen;
    if (k == "framerate") return &g_set.framerate;
    if (k == "pc_fps") return &g_set.pc_fps;
    if (k == "hud_stretch") return &g_set.hud_stretch;
    if (k == "fov") return &g_set.fov;
    if (k == "chaos_emerald_shop") return &g_set.chaos_emerald_shop;
    if (k == "pad_background") return &g_set.pad_background;
    if (k == "sharpness") return &g_set.sharpness;
    return nullptr;
}
void settings_load() {
    g_set = Settings();
    for (const std::string& l : split_lines(read_text(path(L"saves\\settings.ini")))) {
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (int* p = setting_ptr(k)) *p = atoi(v.c_str());
        else g_set.other.push_back({k, v});
    }
    if (g_set.screen == 1 || g_set.screen == 2) g_set.screen = 3;
    g_set.hud_stretch = 0;
    g_set.fov = std::clamp(g_set.fov, 80, 130);
    g_set.sharpness = std::clamp(g_set.sharpness, 0, 100);
}
void settings_save() {
    CreateDirectoryW(path(L"saves").c_str(), nullptr);
    char b[512];
    snprintf(b, sizeof b, "screen=%d\nshow_fps=%d\nchaos_emerald_shop=%d\nfullscreen=%d\nframerate=%d\npc_fps=%d\nhud_stretch=%d\nfov=%d\n"
             "pad_background=%d\nsharpness=%d\n",
             g_set.screen, g_set.show_fps, g_set.chaos_emerald_shop, g_set.fullscreen, g_set.framerate, g_set.pc_fps,
             g_set.hud_stretch, g_set.fov, g_set.pad_background, g_set.sharpness);
    std::string s = b;
    for (auto& kv : g_set.other) s += kv.first + "=" + kv.second + "\n";
    write_text(path(L"saves\\settings.ini"), s);
    g_set_dirty = false;
}

/* ---------------------------------------------------------------- mods\mods.ini (order, "#" = off) */

struct Mod { std::string folder, title; bool on; };
std::vector<Mod> g_mods;
bool g_mods_dirty;

std::string mod_title(const std::string& folder) {
    std::wstring ini = path(L"mods\\") + widen(folder) + L"\\mod.ini";
    for (const std::string& l : split_lines(read_text(ini)))
        if (l.rfind("name=", 0) == 0 && l.size() > 5) return l.substr(5);
    return folder;
}
void mods_load() {
    g_mods.clear();
    std::vector<std::string> seen;
    for (std::string l : split_lines(read_text(path(L"mods\\mods.ini")))) {
        bool on = true;
        while (!l.empty() && (l[0] == ' ' || l[0] == '\t')) l.erase(0, 1);
        if (l.empty()) continue;
        if (l[0] == '#') {
            on = false;
            l.erase(0, 1);
            while (!l.empty() && l[0] == ' ') l.erase(0, 1);
        }
        if (l.empty() || !is_dir(path(L"mods\\") + widen(l))) continue;  /* comments, removed mods */
        seen.push_back(l);
        g_mods.push_back({l, mod_title(l), on});
    }
    WIN32_FIND_DATAW fd;  /* folders not listed yet: at the end, on (as the game does) */
    HANDLE h = FindFirstFileW(path(L"mods\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::string n = narrow(fd.cFileName);
            if (std::find(seen.begin(), seen.end(), n) == seen.end()) g_mods.push_back({n, mod_title(n), true});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
void mods_save() {
    std::string s = "# Sonic Riders mods, applied top to bottom: when two mods replace the same file, the\n"
                    "# lower one wins. \"#\" in front of a folder name turns that mod off. (Saved by the launcher.)\n";
    for (const Mod& m : g_mods) s += (m.on ? "" : "# ") + m.folder + "\n";
    CreateDirectoryW(path(L"mods").c_str(), nullptr);
    write_text(path(L"mods\\mods.ini"), s);
    g_mods_dirty = false;
}

/* ---------------------------------------------------------------- child processes (setup, the game) */

/* runs a command line in the root folder with extra environment variables; output lines go to `sink` */
struct Child {
    HANDLE proc = nullptr;
    std::thread reader;
    std::atomic<bool> done{true};
    DWORD exit_code = 0;
};

bool start_child(Child& c, const std::wstring& cmdline, const std::vector<std::pair<std::wstring, std::wstring>>& env,
                 bool capture, bool hidden, void (*sink)(const std::string&)) {
    for (auto& e : env) SetEnvironmentVariableW(e.first.c_str(), e.second.c_str());  /* inherited by the child */
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (capture && !CreatePipe(&rd, &wr, &sa, 0)) capture = false;
    if (capture) SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    if (capture) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    if (hidden) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION pi = {};
    std::wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, capture, hidden ? CREATE_NO_WINDOW : 0, nullptr,
                             g_root.c_str(), &si, &pi);
    for (auto& e : env) SetEnvironmentVariableW(e.first.c_str(), nullptr);
    if (wr) CloseHandle(wr);
    if (!ok) {
        if (rd) CloseHandle(rd);
        return false;
    }
    CloseHandle(pi.hThread);
    c.proc = pi.hProcess;
    c.done = false;
    if (c.reader.joinable()) c.reader.join();
    c.reader = std::thread([&c, rd, sink]() {
        if (rd) {
            std::string line;
            char buf[1024];
            DWORD n;
            while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) {
                for (DWORD i = 0; i < n; i++) {
                    if (buf[i] == '\n') {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (sink) sink(line);
                        line.clear();
                    } else line += buf[i];
                }
            }
            if (!line.empty() && sink) sink(line);
            CloseHandle(rd);
        }
        WaitForSingleObject(c.proc, INFINITE);
        GetExitCodeProcess(c.proc, &c.exit_code);
        c.done = true;
    });
    return true;
}
bool child_running(Child& c) { return c.proc && !c.done; }
void stop_child(Child& c) {
    if (child_running(c)) TerminateProcess(c.proc, 1);
}

/* ---------------------------------------------------------------- install (setup.ps1) */

std::mutex g_log_mx;
std::vector<std::string> g_log;    /* setup's output */
std::atomic<int> g_step{0};        /* "== N/7" */
std::string g_step_text;           /* under g_log_mx */
void setup_sink(const std::string& l) {
    std::lock_guard<std::mutex> lk(g_log_mx);
    g_log.push_back(l);
    if (g_log.size() > 2000) g_log.erase(g_log.begin(), g_log.begin() + 500);
    if (l.rfind("== ", 0) == 0 && l.size() > 6 && l[3] >= '1' && l[3] <= '9' && l[4] == '/') {
        g_step = l[3] - '0';
        g_step_text = l.substr(3);
    }
}

Child g_setup, g_game;
std::wstring g_disc_choice;  /* picked with "Select disc..." */
enum { INST_IDLE, INST_RUNNING, INST_DONE, INST_FAILED };
int g_install = INST_IDLE;
std::string g_status;  /* bottom bar */

std::wstring pick_disc(HWND owner) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"GameCube disc image (*.iso;*.gcm)\0*.iso;*.gcm\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select your Sonic Riders (USA) disc image (GXEE8P)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : L"";
}

void start_install(const std::wstring& disc) {
    {
        std::lock_guard<std::mutex> lk(g_log_mx);
        g_log.clear();
        g_step_text = "Starting the setup...";
    }
    g_step = 0;
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + path(L"setup\\setup.ps1") +
                       L"\" -Target pc -NoSd -Yes -DesktopShortcut no -Iso \"" + disc + L"\"";
    if (start_child(g_setup, cmd, {{L"SR_NOPAUSE", L"1"}}, true, true, setup_sink)) {
        g_install = INST_RUNNING;
        g_status = "Installing...";
    } else {
        g_install = INST_FAILED;
        g_status = "Could not start the setup (PowerShell).";
    }
}

/* ---------------------------------------------------------------- Direct3D 11 and textures */

ID3D11Device* g_dev;
ID3D11DeviceContext* g_ctx;
IDXGISwapChain* g_swap;
ID3D11RenderTargetView* g_rtv;
std::string g_gpu_name;

void create_rtv() {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}
bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr))  /* no usable GPU driver: software rendering still shows the launcher */
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) return false;
    create_rtv();
    /* the graphics card's name for the ready check (the first adapter: the one the game will use too) */
    IDXGIFactory1* f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) {
        IDXGIAdapter1* a = nullptr;
        if (SUCCEEDED(f->EnumAdapters1(0, &a))) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d))) g_gpu_name = narrow(d.Description);
            a->Release();
        }
        f->Release();
    }
    return true;
}

struct Tex { ID3D11ShaderResourceView* srv = nullptr; int w = 0, h = 0; };
Tex g_banner;

/* any picture Windows can read (PNG here) through WIC, into a texture */
Tex load_texture(const std::wstring& file) {
    Tex t;
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return t;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT w, h;
        conv->GetSize(&w, &h);
        std::vector<uint8_t> px((size_t)w * h * 4);
        if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) {
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = w;
            d.Height = h;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init = {px.data(), w * 4, 0};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture2D(&d, &init, &tex))) {
                g_dev->CreateShaderResourceView(tex, nullptr, &t.srv);
                tex->Release();
                t.w = (int)w;
                t.h = (int)h;
            }
        }
    }
    if (conv) conv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    wic->Release();
    return t;
}
void reload_banner() {
    if (g_banner.srv) g_banner.srv->Release();
    g_banner = Tex();
    std::wstring p = path(L"launcher\\banner.jpg");
    if (exists(p)) g_banner = load_texture(p);
}

/* ---------------------------------------------------------------- look */

ImFont* g_font;
ImFont* g_font_bold;
ImFont* g_font_title;
ImFont* g_font_small;
float g_dpi = 1.0f;
/* ImGui 1.92 fonts scale freely: these are the sizes used (before the DPI scale) */
constexpr float kSize = 20.0f, kSizeSmall = 16.5f, kSizeTitle = 40.0f, kSizeLogo = 28.0f;

const ImU32 kGold = IM_COL32(242, 190, 60, 255);
const ImVec4 kGoldV = ImVec4(0.95f, 0.75f, 0.24f, 1.0f);
const ImVec4 kText = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
const ImVec4 kDim = ImVec4(0.58f, 0.62f, 0.72f, 1.0f);
const ImVec4 kGreen = ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
const ImVec4 kRed = ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
const ImU32 kBg = IM_COL32(20, 24, 36, 255);
const ImU32 kSide = IM_COL32(14, 17, 27, 255);
const ImU32 kLine = IM_COL32(48, 56, 82, 255);

void load_fonts(float dpi) {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string fonts = narrow(win) + "\\Fonts\\";
    auto font = [&](const char* file, bool merge_symbols) -> ImFont* {
        std::string p = fonts + file;
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), kSize * dpi);
        std::string sym = fonts + "seguisym.ttf";  /* check marks, arrows, dots */
        if (f && merge_symbols && GetFileAttributesA(sym.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig c;
            c.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(sym.c_str(), kSize * dpi, &c);
        }
        return f;
    };
    g_font = font("segoeui.ttf", true);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    g_font_bold = font("seguisb.ttf", true);
    if (!g_font_bold) g_font_bold = g_font;
    g_font_title = font("segoeuib.ttf", false);
    if (!g_font_title) g_font_title = g_font_bold;
    g_font_small = g_font;
    io.FontDefault = g_font;
}

void apply_style(float dpi) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontSizeBase = kSize * dpi;
    s.WindowRounding = 0;
    s.FrameRounding = 3;
    s.GrabRounding = 3;
    s.ScrollbarRounding = 3;
    s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(10, 10);
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.11f, 0.17f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.18f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.24f, 0.36f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.42f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.15f, 0.18f, 0.28f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.25f, 0.38f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.30f, 0.45f, 1.0f);
    c[ImGuiCol_CheckMark] = kGoldV;
    c[ImGuiCol_SliderGrab] = kGoldV;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.82f, 0.35f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.21f, 0.32f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.26f, 0.38f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.30f, 0.44f, 1.0f);
    c[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(kLine);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PlotHistogram] = kGoldV;
    s.ScaleAllSizes(dpi);
}

/* "< value >": a full-width selector stepping through options (the reference launcher's look) */
bool selector(const char* id, int* idx, const char* const* items, int n, float width) {
    bool changed = false;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFrameHeight() * 1.25f;
    ImGui::InvisibleButton("##box", ImVec2(width, h));
    bool hov = ImGui::IsItemHovered();
    ImVec2 mp = ImGui::GetIO().MousePos;
    if (ImGui::IsItemClicked()) {
        *idx = (mp.x < p.x + width * 0.3f) ? (*idx + n - 1) % n : (*idx + 1) % n;
        changed = true;
    }
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { *idx = (*idx + n - 1) % n; changed = true; }
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { *idx = (*idx + 1) % n; changed = true; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), hov ? IM_COL32(44, 52, 80, 255) : IM_COL32(36, 43, 66, 255), 3);
    float pad = ImGui::GetFontSize() * 0.8f;
    dl->AddText(ImVec2(p.x + pad, p.y + (h - ImGui::GetFontSize()) / 2), IM_COL32(170, 178, 200, 255), "<");
    dl->AddText(ImVec2(p.x + width - pad - ImGui::CalcTextSize(">").x, p.y + (h - ImGui::GetFontSize()) / 2),
                IM_COL32(170, 178, 200, 255), ">");
    const char* v = items[std::clamp(*idx, 0, n - 1)];
    ImGui::PushFont(g_font_bold, kSize * g_dpi);
    ImVec2 ts = ImGui::CalcTextSize(v);
    dl->AddText(ImVec2(p.x + (width - ts.x) / 2, p.y + (h - ts.y) / 2), IM_COL32(235, 238, 245, 255), v);
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}
void section(const char* title) {
    ImGui::PushFont(g_font_bold, kSize * g_dpi);
    ImGui::PushStyleColor(ImGuiCol_Text, kGoldV);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
void label(const char* t) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
}
void note(const char* t) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", t);
    ImGui::PopStyleColor();
}
void check_line(bool ok, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? kGreen : kRed);
    ImGui::TextUnformatted(ok ? "\xE2\x9C\x93" : "\xE2\x9C\x97");  /* check / cross */
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(text.c_str());
}
bool big_button(const char* t, ImVec2 size, bool primary, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.72f, 0.24f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.80f, 0.32f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.65f, 0.18f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.10f, 0.14f, 1));
    }
    ImGui::PushFont(g_font_bold, kSize * g_dpi);
    bool r = ImGui::Button(t, size);
    ImGui::PopFont();
    if (primary) ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return r;
}

/* ---------------------------------------------------------------- pages */

enum Page { P_PLAY, P_ONLINE, P_GRAPHICS, P_GAME, P_MODS, P_CONTROLS, P_ABOUT, P_UPDATE, P_COUNT };  /* Update: only with one */
const char* kPages[P_COUNT] = {"Play", "Online", "Graphics", "Game", "Mods", "Controls", "About", "Update"};
int g_page = P_PLAY;

/* two screen options only (user, 2026-11-14): 4:3 and Fit to screen; the old 16:9 modes (1, 2) read as Fit */
const char* kScreens[] = {"4:3 (original)", "Fit to screen"};
int screen_to_idx(int s) { return s == 0 ? 0 : 1; }
int idx_to_screen(int i) { return i ? 3 : 0; }
/* frame rate presets (runtime/src/settings.c settings_pc_fps_presets): 30 = the game's 30 fps mode, 60 = its own
 * pacing, the others in-between pictures (pc_fps) */
const char* kFramerates[] = {"30 FPS", "45 FPS", "60 FPS", "120 FPS", "144 FPS", "180 FPS", "240 FPS", "Unlimited"};
const int kFpsValues[] = {30, 45, 60, 120, 144, 180, 240, -1};
const int kNumFps = 8;
int fps_to_idx() {
    int v = g_set.framerate == 2 ? 30 : g_set.pc_fps == 0 ? 60 : g_set.pc_fps;
    for (int i = 0; i < kNumFps; i++)
        if (kFpsValues[i] == v) return i;
    return 2;
}
void fps_selector(const char* id, float w) {
    int i = fps_to_idx();
    if (!selector(id, &i, kFramerates, kNumFps, w)) return;
    int v = kFpsValues[i];
    if (v == 30) {
        g_set.framerate = 2;
        g_set.pc_fps = 0;
    } else {
        if (g_set.framerate == 2) g_set.framerate = 1;
        g_set.pc_fps = v == 60 ? 0 : v;
    }
    g_set_dirty = true;
}
const char* kWindow[] = {"Window", "Fullscreen borderless"};
const char* kOnOff[] = {"Off", "On"};

struct Ready {
    bool built, disc, saves, setup;
    std::wstring disc_path;
} g_ready;
void refresh_ready() {
    g_ready.built = exists(path(L"build\\host-mingw\\sonicriders.exe"));
    g_ready.disc_path = find_disc();
    g_ready.disc = !g_ready.disc_path.empty();
    g_ready.saves = exists(path(L"saves\\card_a.raw"));
    g_ready.setup = exists(path(L"setup\\setup.ps1"));
}

std::string short_path(const std::wstring& p, size_t max = 46) {
    std::string s = narrow(p);
    if (s.size() <= max) return s;
    return s.substr(0, 14) + "..." + s.substr(s.size() - (max - 17));
}

void quick_settings(float w) {
    section("QUICK SETTINGS");
    label("Screen");
    int si = screen_to_idx(g_set.screen);
    if (selector("screen", &si, kScreens, 2, w)) { g_set.screen = idx_to_screen(si); g_set_dirty = true; }
    label("Window mode");
    if (selector("window", &g_set.fullscreen, kWindow, 2, w)) g_set_dirty = true;
    label("Frame rate");
    fps_selector("fr", w);
    label("Frame counter");
    if (selector("fps", &g_set.show_fps, kOnOff, 2, w)) g_set_dirty = true;
}

/* ---------------------------------------------------------------- installer mode (Switch, Android)
 *
 * The same program, named "Install Sonic Riders (Switch).exe" or "Install Sonic Riders (Android).exe" in those
 * release zips (or SR_INSTALLER=switch|android), is a window in front of setup\setup.ps1 for that target: it asks
 * the questions the console setup asks (disc image, SD card, own icon, HOME-screen icon, Android licence and
 * install), then runs the setup unattended (-Yes plus the answers) with its steps as a progress bar. */
enum Mode { M_LAUNCHER, M_SWITCH, M_ANDROID };
int g_mode = M_LAUNCHER;
bool installer() { return g_mode != M_LAUNCHER; }

struct InstallerChoices {
    std::vector<std::wstring> sd_found;  /* drives with a "switch" or "atmosphere" folder */
    std::wstring sd;                     /* chosen SD card top folder ("" = keep the .nro in build\switch\) */
    bool copy_iso = true;                /* Switch: also put the disc image on the SD card */
    bool own_icon = false;
    std::wstring icon;
    bool home_icon = false;              /* Switch: HOME-screen forwarder (needs prod.keys) */
    std::wstring keys;
    bool phone_install = true;           /* Android: install on the connected phone */
    bool license = false;                /* Android SDK License accepted */
} g_ic;

void find_sd_cards() {
    g_ic.sd_found.clear();
    DWORD drives = GetLogicalDrives();
    for (int i = 2; i < 26; i++) {  /* C: and up */
        if (!(drives & (1u << i))) continue;
        std::wstring root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
        UINT type = GetDriveTypeW(root.c_str());
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED) continue;
        if (exists(root + L"atmosphere") || (type == DRIVE_REMOVABLE && exists(root + L"switch")))
            g_ic.sd_found.push_back(root);
    }
    if (g_ic.sd.empty() && !g_ic.sd_found.empty()) g_ic.sd = g_ic.sd_found[0];
    /* where Lockpick_RCM users keep the keys (the setup looks there too) */
    wchar_t prof[MAX_PATH];
    if (g_ic.keys.empty() && GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH) &&
        exists(std::wstring(prof) + L"\\.switch\\prod.keys"))
        g_ic.keys = std::wstring(prof) + L"\\.switch\\prod.keys";
}

std::wstring pick_folder(HWND owner, const wchar_t* title) {
    std::wstring out;
    IFileOpenDialog* d = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(title);
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->Show(owner)) && SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
            }
            it->Release();
        }
        d->Release();
    }
    return out;
}

std::wstring pick_file(HWND owner, const wchar_t* filter, const wchar_t* title) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : L"";
}

/* what keeps INSTALL from starting, or "" */
std::string installer_blocker() {
    if (!g_ready.setup) return "setup\\setup.ps1 is missing: unpack the whole zip again.";
    if (g_disc_choice.empty()) return "Select your disc image.";
    if (g_ic.own_icon && g_ic.icon.empty()) return "Pick your icon picture (or untick \"Use my own picture\").";
    if (g_mode == M_SWITCH && g_ic.home_icon && g_ic.keys.empty()) return "Pick your prod.keys (or untick the HOME-screen icon).";
    if (g_mode == M_ANDROID && !g_ic.license) return "Accept the Android SDK License to continue.";
    return "";
}

/* a path as one quoted command-line argument: a backslash right before the closing quote would escape it
 * ("F:\" reached the setup as F:", 2026-11-09), so a path ending in one gets "." (the same folder) */
std::wstring qpath(std::wstring p) {
    if (!p.empty() && p.back() == L'\\') p += L'.';
    return L"\"" + p + L"\"";
}

void start_installer() {
    {
        std::lock_guard<std::mutex> lk(g_log_mx);
        g_log.clear();
        g_step_text = "Starting the setup...";
    }
    g_step = 0;
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + path(L"setup\\setup.ps1") +
                       L"\" -Yes -Iso " + qpath(g_disc_choice);
    if (g_mode == M_SWITCH) {
        cmd += L" -Target switch";
        if (g_ic.sd.empty()) cmd += L" -NoSd";
        else cmd += L" -Sd " + qpath(g_ic.sd) + L" -CopyIso " + (g_ic.copy_iso ? L"yes" : L"no");
        if (g_ic.home_icon) cmd += L" -HomeIcon yes -Keys " + qpath(g_ic.keys);
        else cmd += L" -HomeIcon no";
    } else {
        cmd += L" -Target android -AcceptAndroidSdkLicense";
        if (!g_ic.phone_install) cmd += L" -Install no";
    }
    if (g_ic.own_icon && !g_ic.icon.empty()) cmd += L" -Icon " + qpath(g_ic.icon);
    if (start_child(g_setup, cmd, {{L"SR_NOPAUSE", L"1"}}, true, true, setup_sink)) {
        g_install = INST_RUNNING;
        g_status = "Building...";
    } else {
        g_install = INST_FAILED;
        g_status = "Could not start the setup (PowerShell).";
    }
}

/* the setup's steps and output (shared by the launcher's first install and the installer) */
void install_progress(float col, const char* title_running) {
    section(g_install == INST_FAILED ? "INSTALL FAILED" : g_install == INST_DONE ? "DONE" : title_running);
    std::string st;
    {
        std::lock_guard<std::mutex> lk(g_log_mx);
        st = g_step_text;
    }
    float frac = g_install == INST_DONE ? 1.0f : g_step / 7.0f;
    ImGui::ProgressBar(frac, ImVec2(col, 0), "");
    ImGui::TextWrapped("%s", st.c_str());
    ImGui::BeginChild("log", ImVec2(col, 0), ImGuiChildFlags_None);
    ImGui::PushFont(g_font_small, kSizeSmall * g_dpi);
    {
        std::lock_guard<std::mutex> lk(g_log_mx);
        for (const std::string& l : g_log) ImGui::TextUnformatted(l.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();
}

void installer_next_steps() {
    section("NEXT");
    if (g_mode == M_SWITCH) {
        if (g_ic.sd.empty())
            ImGui::TextWrapped("Copy build\\switch\\sonicriders.nro and your disc image to /switch/sonicriders/ on the "
                               "SD card.");
        ImGui::TextWrapped("1. Eject the SD card and put it in the Switch.");
        ImGui::TextWrapped("2. Hold R while starting any game to open the Homebrew Menu, then start Sonic Riders.");
        if (g_ic.home_icon)
            ImGui::TextWrapped("3. HOME-screen icon: install NSP\\SonicRiders-HOME-icon.nsp with DBI or Goldleaf.");
        note("In game: hold Minus for the settings menu.");
    } else {
        ImGui::TextWrapped(g_ic.phone_install ? "Start Sonic Riders from the phone's app list (if the log above says "
                                                "it was installed)."
                                              : "Install the APK from build\\android\\ with adb, or run this again with "
                                                "the phone connected.");
        note("Touch controls on screen or any controller. The top-left button opens the settings menu.");
    }
}

void page_installer(float w) {
    float col = (w - 40) * 0.5f;
    bool idle = g_install == INST_IDLE;
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + col);
    if (!idle) {
        install_progress(col, "BUILDING");
    } else {
        section("1. YOUR DISC IMAGE");
        ImGui::TextWrapped("Sonic Riders (USA), GXEE8P (.iso). The game is built on this PC from it: 10-40 minutes the "
                           "first time.");
        if (ImGui::Button("Select disc...")) {
            std::wstring d = pick_disc(GetActiveWindow());
            if (!d.empty()) g_disc_choice = d;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(g_disc_choice.empty() ? "no disc image selected" : short_path(g_disc_choice, 34).c_str());
        ImGui::Spacing();
        if (g_mode == M_SWITCH) {
            section("2. SD CARD");
            ImGui::TextWrapped("Put your Switch's SD card in this PC. The game goes to /switch/sonicriders/.");
            std::string cur = g_ic.sd.empty() ? "Don't copy (keep it in build\\switch\\)" : narrow(g_ic.sd);
            const ImGuiStyle& st = ImGui::GetStyle();
            float btns = ImGui::CalcTextSize("Refresh").x + ImGui::CalcTextSize("Other...").x + 4 * st.FramePadding.x +
                         2 * st.ItemSpacing.x;
            ImGui::SetNextItemWidth(std::max(col - btns, 80.0f));
            if (ImGui::BeginCombo("##sd", cur.c_str())) {
                for (const std::wstring& d : g_ic.sd_found)
                    if (ImGui::Selectable((narrow(d) + "  (Switch SD card)").c_str(), d == g_ic.sd)) g_ic.sd = d;
                if (ImGui::Selectable("Don't copy (keep it in build\\switch\\)", g_ic.sd.empty())) g_ic.sd.clear();
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) find_sd_cards();
            ImGui::SameLine();
            if (ImGui::Button("Other...")) {
                std::wstring f = pick_folder(GetActiveWindow(), L"Select your Switch SD card (its top folder)");
                if (!f.empty()) {
                    if (exists(f + L"\\atmosphere") || exists(f + L"\\switch")) g_ic.sd = f;
                    else MessageBoxW(GetActiveWindow(), L"That folder has no 'switch' or 'atmosphere' folder. Pick the SD "
                                     L"card's top folder.", L"Sonic Riders", MB_ICONWARNING);
                }
            }
            if (g_ic.sd_found.empty() && g_ic.sd.empty())
                ImGui::TextColored(kRed, "No Switch SD card found. Insert it and press Refresh.");
            if (!g_ic.sd.empty()) {
                ImGui::Checkbox("Copy the disc image too", &g_ic.copy_iso);
                note("1.4 GB; needed once (skipped if it is already on the card).");
            }
        } else {
            section("2. YOUR PHONE");
            ImGui::TextWrapped("64-bit Android 11 or newer with Vulkan. Turn on USB debugging (Settings > About phone > "
                               "tap Build number 7 times; then Developer options > USB debugging), connect the phone by "
                               "USB and tap Allow when it asks.");
            ImGui::Checkbox("Install on the connected phone", &g_ic.phone_install);
            note("Also copies your disc image to the phone (about a minute over USB).");
            ImGui::Checkbox("I accept the Android SDK License", &g_ic.license);
            ImGui::SameLine();
            if (ImGui::SmallButton("Read it"))
                ShellExecuteW(nullptr, L"open", L"https://developer.android.com/studio/terms", nullptr, nullptr, SW_SHOWNORMAL);
            note("The first build downloads about 1.1 GB of Android tools (8 GB free space needed).");
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0, 40);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + col);
    if (g_install == INST_DONE) {
        installer_next_steps();
    } else {
        section("3. ICON (OPTIONAL)");
        ImGui::BeginDisabled(!idle);
        ImGui::Checkbox("Use my own picture", &g_ic.own_icon);
        if (g_ic.own_icon) {
            if (ImGui::Button("Pick picture...")) {
                std::wstring f = pick_file(GetActiveWindow(),
                                           L"Pictures (*.png;*.jpg;*.jpeg;*.ico;*.bmp)\0*.png;*.jpg;*.jpeg;*.ico;*.bmp\0", L"Your icon picture (square)");
                if (!f.empty()) g_ic.icon = f;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(g_ic.icon.empty() ? "none" : short_path(g_ic.icon, 30).c_str());
        }
        note("Otherwise the Sonic Riders fan icon by cedry2kio (DeviantArt), credited in the README. Square pictures of 256 x 256 or more look best.");
        if (g_mode == M_SWITCH) {
            ImGui::Spacing();
            ImGui::Checkbox("Make a HOME-screen icon", &g_ic.home_icon);
            note("Starts the game from the HOME menu, without holding R.");
            if (g_ic.home_icon) {
                if (ImGui::Button("Pick prod.keys...")) {
                    std::wstring f = pick_file(GetActiveWindow(), L"prod.keys\0*.keys\0All files (*.*)\0*.*\0",
                                               L"Your console's prod.keys (Lockpick_RCM)");
                    if (!f.empty()) g_ic.keys = f;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(g_ic.keys.empty() ? "none" : short_path(g_ic.keys, 30).c_str());
                note("Needs your console's prod.keys (dumped with Lockpick_RCM; read on this PC only, never copied), "
                     "and DBI or Goldleaf plus up-to-date sigpatches to install the icon on the console.");
            }
        }
        ImGui::EndDisabled();
        if (g_install == INST_FAILED) {
            ImGui::Spacing();
            ImGui::TextColored(kRed, "Something went wrong: the messages on the left say what. setup.log in this folder "
                                     "has all of them (attach it to a bug report).");
            if (ImGui::Button("Try again")) g_install = INST_IDLE;
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

void page_play(float w) {
    float col = (w - 40) * 0.5f;
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + col);
    if (g_install == INST_RUNNING || g_install == INST_FAILED) {
        install_progress(col, "INSTALLING");
    } else if (!g_ready.built) {
        section("FIRST TIME");
        ImGui::TextWrapped("The game is built on this PC from your own disc image: Sonic Riders (USA), GXEE8P. "
                           "This takes 10-30 minutes once and downloads the build tools (about 300 MB).");
        ImGui::Spacing();
        if (ImGui::Button("Select disc...")) {
            std::wstring d = pick_disc(GetActiveWindow());
            if (!d.empty()) g_disc_choice = d;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(g_disc_choice.empty() ? "no disc image selected" : short_path(g_disc_choice).c_str());
        if (!g_ready.setup) ImGui::TextColored(kRed, "setup\\setup.ps1 is missing: unpack the whole zip again.");
    } else {
        if (exists(path(L"build\\launcher\\rebuild_after_update"))) {
            section("UPDATED");
            ImGui::TextColored(kGoldV, "The port was updated: build the game again from your disc image (a few minutes).");
            if (ImGui::Button("Rebuild now") && g_ready.disc && !child_running(g_setup) && !child_running(g_game)) {
                DeleteFileW(path(L"build\\launcher\\rebuild_after_update").c_str());
                start_install(g_ready.disc_path);
            }
            ImGui::Spacing();
        }
        section("READY CHECK");
        check_line(true, "Game built (build\\host-mingw\\sonicriders.exe)");
        check_line(g_ready.disc, g_ready.disc ? "Disc image: " + short_path(g_ready.disc_path, 40)
                                              : std::string("No disc image in baserom\\ (run the install again)"));
        check_line(true, g_ready.saves ? "Saves found (saves\\card_a.raw)" : "No saves yet (made when you first save)");
        check_line(!g_gpu_name.empty(), "Graphics card: " + (g_gpu_name.empty() ? std::string("unknown") : g_gpu_name));
        std::string sum = std::string(kScreens[screen_to_idx(g_set.screen)]) + "  \xC2\xB7  " + kWindow[g_set.fullscreen ? 1 : 0] +
                          "  \xC2\xB7  " + kFramerates[fps_to_idx()];
        check_line(true, sum);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0, 40);
    ImGui::BeginGroup();
    quick_settings(col);
    ImGui::EndGroup();
}

void page_graphics(float w) {
    float cw = std::min(w, 520.0f);
    section("SCREEN");
    int si = screen_to_idx(g_set.screen);
    if (selector("screen2", &si, kScreens, 2, cw)) { g_set.screen = idx_to_screen(si); g_set_dirty = true; }
    note("Fit to screen uses your window's or monitor's shape (16:9, 16:10, up to 32:9 ultrawide), shows more of "
         "the track at the sides and moves the race HUD to the edges.");
    ImGui::Spacing();
    section("FIELD OF VIEW");
    ImGui::SetNextItemWidth(cw);
    if (ImGui::SliderInt("##fov", &g_set.fov, 80, 130, "%d%%")) g_set_dirty = true;
    if (ImGui::Button("Reset to 100%")) { g_set.fov = 100; g_set_dirty = true; }
    ImGui::Spacing();
    section("SHARPNESS (AMD FSR 1)");
    ImGui::SetNextItemWidth(cw);
    if (ImGui::SliderInt("##sharp", &g_set.sharpness, 0, 100, g_set.sharpness ? "%d%%" : "Off")) g_set_dirty = true;
    note("Sharpens the final picture. When the window is bigger than the game's resolution (F1 menu > Display), "
         "FSR also does the upscaling. Off = the picture as before.");
    ImGui::Spacing();
    section("WINDOW AND FRAME RATE");
    if (selector("window2", &g_set.fullscreen, kWindow, 2, cw)) g_set_dirty = true;
    fps_selector("fr2", cw);
    note("The game always runs at its own speed. Above 60 FPS the PC draws extra pictures between the game's own "
         "for smoother motion (the monitor has to refresh that fast to show them; high rates may need a lower "
         "resolution in the game's F1 menu). Unlimited turns vsync off. "
         "F11 or Alt+Enter switches fullscreen in the game.");
    bool fps = g_set.show_fps != 0;
    if (ImGui::Checkbox("Frame counter in the corner", &fps)) { g_set.show_fps = fps; g_set_dirty = true; }
}

void page_game(float w) {
    (void)w;
    section("UNLOCKS");
    bool ce = g_set.chaos_emerald_shop != 0;
    if (ImGui::Checkbox("Chaos Emerald gear in the shop (Super Sonic)", &ce)) { g_set.chaos_emerald_shop = ce; g_set_dirty = true; }
    note("Normally earned by finishing the story. When on, the Shop sells it for 0 rings; nothing else is unlocked.");
    ImGui::Spacing();
    section("SAVES");
    note("Your memory card and settings live in the saves folder here. Back it up before trying mods.");
    if (ImGui::Button("Open the saves folder")) {
        CreateDirectoryW(path(L"saves").c_str(), nullptr);
        open_in_explorer(path(L"saves"));
    }
    ImGui::Spacing();
    section("ONLINE PLAY");
    note("Two-player online races are being tested (docs\\ONLINE.md). They will get their own page here.");
}

void page_mods(float w) {
    section("INSTALLED MODS");
    if (g_mods.empty()) note("No mods installed. Put each mod in its own folder inside mods\\ (docs\\MODDING.md).");
    float cw = std::min(w, 620.0f);
    for (int i = 0; i < (int)g_mods.size(); i++) {
        ImGui::PushID(i);
        bool on = g_mods[i].on;
        if (ImGui::Checkbox("##on", &on)) { g_mods[i].on = on; g_mods_dirty = true; }
        ImGui::SameLine();
        ImGui::TextUnformatted(g_mods[i].title.c_str());
        ImGui::SameLine(cw - 90);
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) { std::swap(g_mods[i], g_mods[i - 1]); g_mods_dirty = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i == (int)g_mods.size() - 1);
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) { std::swap(g_mods[i], g_mods[i + 1]); g_mods_dirty = true; }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    note("Applied top to bottom: when two mods replace the same file, the lower one wins.");
    if (ImGui::Button("Open the mods folder")) {
        CreateDirectoryW(path(L"mods").c_str(), nullptr);
        open_in_explorer(path(L"mods"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Look again")) mods_load();
}

/* ---------------------------------------------------------------- saves\controls.ini (the game's button map) */

/* the inputs, names and defaults of runtime/src/platform/host/input_sdl.c (controls_load). Keys are SDL key names,
 * controller buttons SDL controller button names (+ lefttrigger / righttrigger); pad nullptr = keyboard only */
struct Bind { const char* id; const char* label; const char* key; const char* pad; };
const Bind kBinds[] = {
    {"A", "A", "J", "a"}, {"B", "B", "K", "b"}, {"X", "X", "L", "x"}, {"Y", "Y", "I", "y"},
    {"Z", "Z", "P", "rightshoulder"}, {"L", "L", "U", "lefttrigger"}, {"R", "R", "O", "righttrigger"},
    {"START", "START", "Return", "start"}, {"DPAD_UP", "D-pad up", "T", "dpup"},
    {"DPAD_DOWN", "D-pad down", "G", "dpdown"}, {"DPAD_LEFT", "D-pad left", "F", "dpleft"},
    {"DPAD_RIGHT", "D-pad right", "H", "dpright"}, {"STICK_UP", "Stick up", "W", nullptr},
    {"STICK_DOWN", "Stick down", "S", nullptr}, {"STICK_LEFT", "Stick left", "A", nullptr},
    {"STICK_RIGHT", "Stick right", "D", nullptr}, {"C_UP", "C-stick up", "Up", nullptr},
    {"C_DOWN", "C-stick down", "Down", nullptr}, {"C_LEFT", "C-stick left", "Left", nullptr},
    {"C_RIGHT", "C-stick right", "Right", nullptr}};
constexpr int kNBinds = (int)(sizeof kBinds / sizeof kBinds[0]);
std::string g_key[kNBinds], g_pad[kNBinds];
bool g_ctl_dirty;

/* controller buttons: SDL name, what the launcher shows (Xbox layout), XInput bit (0: read from the triggers) */
struct PadButton { const char* sdl; const char* shown; unsigned xinput; };
const PadButton kPadButtons[] = {
    {"a", "A (bottom)", 0x1000}, {"b", "B (right)", 0x2000}, {"x", "X (left)", 0x4000}, {"y", "Y (top)", 0x8000},
    {"leftshoulder", "LB", 0x0100}, {"rightshoulder", "RB", 0x0200}, {"lefttrigger", "LT", 0},
    {"righttrigger", "RT", 0}, {"start", "Start / Menu", 0x0010}, {"back", "Back / View", 0x0020},
    {"leftstick", "Left stick press", 0x0040}, {"rightstick", "Right stick press", 0x0080},
    {"dpup", "D-pad up", 0x0001}, {"dpdown", "D-pad down", 0x0002}, {"dpleft", "D-pad left", 0x0004},
    {"dpright", "D-pad right", 0x0008}, {"none", "(none)", 0}};
const char* pad_shown(const std::string& sdl) {
    for (const PadButton& b : kPadButtons)
        if (sdl == b.sdl) return b.shown;
    return sdl.c_str();
}

void controls_defaults() {
    for (int i = 0; i < kNBinds; i++) {
        g_key[i] = kBinds[i].key;
        g_pad[i] = kBinds[i].pad ? kBinds[i].pad : "";
    }
}
void controls_load() {
    controls_defaults();
    for (const std::string& l : split_lines(read_text(path(L"saves\\controls.ini")))) {
        size_t eq = l.find('=');
        if (eq == std::string::npos || l.size() < 4 || (l.compare(0, 4, "key.") && l.compare(0, 4, "pad."))) continue;
        std::string id = l.substr(4, eq - 4), v = l.substr(eq + 1);
        for (int i = 0; i < kNBinds; i++)
            if (id == kBinds[i].id) {
                if (l[0] == 'k') g_key[i] = v;
                else if (kBinds[i].pad) g_pad[i] = v;
            }
    }
}
void controls_save() {
    CreateDirectoryW(path(L"saves").c_str(), nullptr);
    std::string s = "# Button map for the PC version (saved by the launcher's Controls page). key.* = SDL key names,\n"
                    "# pad.* = SDL controller buttons (a b x y leftshoulder rightshoulder lefttrigger righttrigger start\n"
                    "# back leftstick rightstick dpup dpdown dpleft dpright) or none. Delete this file for the defaults.\n";
    for (int i = 0; i < kNBinds; i++) s += std::string("key.") + kBinds[i].id + "=" + g_key[i] + "\n";
    for (int i = 0; i < kNBinds; i++)
        if (kBinds[i].pad) s += std::string("pad.") + kBinds[i].id + "=" + g_pad[i] + "\n";
    write_text(path(L"saves\\controls.ini"), s);
    g_ctl_dirty = false;
}

/* the SDL key name of an ImGui key ("" for keys the game cannot use here) */
std::string sdl_key_name(ImGuiKey k) {
    if ((k >= ImGuiKey_A && k <= ImGuiKey_Z) || (k >= ImGuiKey_0 && k <= ImGuiKey_9) || (k >= ImGuiKey_F2 && k <= ImGuiKey_F10) ||
        k == ImGuiKey_F12)
        return ImGui::GetKeyName(k);  /* F1 (menu) and F11 (fullscreen) are the port's own */
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return "Keypad " + std::to_string(k - ImGuiKey_Keypad0);
    static const struct { ImGuiKey k; const char* n; } t[] = {
        {ImGuiKey_Enter, "Return"}, {ImGuiKey_Space, "Space"}, {ImGuiKey_Tab, "Tab"}, {ImGuiKey_Backspace, "Backspace"},
        {ImGuiKey_LeftShift, "Left Shift"}, {ImGuiKey_RightShift, "Right Shift"}, {ImGuiKey_LeftCtrl, "Left Ctrl"},
        {ImGuiKey_RightCtrl, "Right Ctrl"}, {ImGuiKey_LeftAlt, "Left Alt"}, {ImGuiKey_RightAlt, "Right Alt"},
        {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"}, {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"},
        {ImGuiKey_Comma, ","}, {ImGuiKey_Period, "."}, {ImGuiKey_Slash, "/"}, {ImGuiKey_Semicolon, ";"},
        {ImGuiKey_Apostrophe, "'"}, {ImGuiKey_LeftBracket, "["}, {ImGuiKey_RightBracket, "]"}, {ImGuiKey_Backslash, "\\"},
        {ImGuiKey_Minus, "-"}, {ImGuiKey_Equal, "="}, {ImGuiKey_GraveAccent, "`"}, {ImGuiKey_Insert, "Insert"},
        {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Home, "Home"}, {ImGuiKey_End, "End"}, {ImGuiKey_PageUp, "PageUp"},
        {ImGuiKey_PageDown, "PageDown"}, {ImGuiKey_KeypadEnter, "Keypad Enter"}, {ImGuiKey_CapsLock, "CapsLock"}};
    for (auto& e : t)
        if (e.k == k) return e.n;
    return "";
}

/* XInput (Xbox-style controllers), loaded when present, for "press a controller button" */
struct XInputPad { WORD buttons; BYTE lt, rt; };
DWORD(WINAPI* g_xinput_get)(DWORD, void*);
bool read_xinput(DWORD user, XInputPad* p) {
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        if (!m) m = LoadLibraryW(L"xinput9_1_0.dll");
        if (m) g_xinput_get = (DWORD(WINAPI*)(DWORD, void*))GetProcAddress(m, "XInputGetState");
    }
    struct { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } st = {};
    if (!g_xinput_get || g_xinput_get(user, &st) != 0) return false;
    *p = {st.buttons, st.lt, st.rt};
    return true;
}
/* the controller button pressed since the last call (SDL name), or "" */
std::string xinput_new_press(bool reset) {
    static XInputPad prev[4];
    std::string hit;
    for (DWORD u = 0; u < 4; u++) {
        XInputPad p = {};
        if (!read_xinput(u, &p)) continue;
        if (!reset && hit.empty()) {
            WORD down = p.buttons & ~prev[u].buttons;
            for (const PadButton& b : kPadButtons)
                if (b.xinput && (down & b.xinput)) { hit = b.sdl; break; }
            if (hit.empty() && p.lt > 128 && prev[u].lt <= 128) hit = "lefttrigger";
            if (hit.empty() && p.rt > 128 && prev[u].rt <= 128) hit = "righttrigger";
        }
        prev[u] = p;
    }
    return hit;
}

int g_cap_row = -1;   /* the cell waiting for a press */
bool g_cap_pad;
double g_cap_t0;

void page_controls(float w) {
    section("BUTTONS");
    note("Click a box, then press the key or the controller button you want (Escape cancels). Xbox-style controllers "
         "are read here; for others pick the button from the list (\xE2\x96\xBE). Used the next time the game starts.");
    ImGui::Spacing();
    /* a cell waiting for a press */
    if (g_cap_row >= 0) {
        bool done = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::GetTime() - g_cap_t0 > 8) done = true;
        else if (!g_cap_pad) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !done; k++)
                if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
                    std::string n = sdl_key_name((ImGuiKey)k);
                    if (!n.empty()) {
                        g_key[g_cap_row] = n;
                        g_ctl_dirty = done = true;
                    }
                }
        } else {
            std::string b = xinput_new_press(false);
            if (!b.empty()) {
                g_pad[g_cap_row] = b;
                g_ctl_dirty = done = true;
            }
        }
        if (done) g_cap_row = -1;
    }
    float colw = std::min(220 * g_dpi, (w - 150 * g_dpi) / 2);
    auto dup = [](const std::string* arr, int row) {
        if (arr[row].empty() || arr[row] == "none") return false;
        for (int j = 0; j < kNBinds; j++)
            if (j != row && arr[j] == arr[row]) return true;
        return false;
    };
    if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("GameCube", ImGuiTableColumnFlags_WidthFixed, 130 * g_dpi);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthFixed, colw);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthFixed, colw);
        ImGui::TableHeadersRow();
        for (int i = 0; i < kNBinds; i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kBinds[i].label);
            for (int col = 0; col < 2; col++) {
                ImGui::TableNextColumn();
                bool pad = col == 1;
                if (pad && !kBinds[i].pad) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(kDim, pad && i >= 16 ? "right stick" : "left stick");
                    continue;
                }
                ImGui::PushID(col);
                bool waiting = g_cap_row == i && g_cap_pad == pad;
                const std::string& v = pad ? g_pad[i] : g_key[i];
                std::string shown = waiting ? (pad ? "press a button..." : "press a key...")
                                            : pad ? pad_shown(v) : (v == "Return" ? std::string("Enter") : v);
                bool clash = !waiting && dup(pad ? g_pad : g_key, i);
                if (clash) ImGui::PushStyleColor(ImGuiCol_Text, kRed);
                if (ImGui::Button(shown.c_str(), ImVec2(colw - (pad ? 34 * g_dpi : 0), 0))) {
                    g_cap_row = waiting ? -1 : i;
                    g_cap_pad = pad;
                    g_cap_t0 = ImGui::GetTime();
                    if (pad) xinput_new_press(true);
                }
                if (clash) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also used by another button.");
                }
                if (pad) {
                    ImGui::SameLine(0, 4 * g_dpi);
                    if (ImGui::Button("\xE2\x96\xBE", ImVec2(30 * g_dpi, 0))) ImGui::OpenPopup("list");
                    if (ImGui::BeginPopup("list")) {
                        for (const PadButton& b : kPadButtons)
                            if (ImGui::Selectable(b.shown, g_pad[i] == b.sdl)) {
                                g_pad[i] = b.sdl;
                                g_ctl_dirty = true;
                            }
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Reset to the defaults")) {
        controls_defaults();
        g_ctl_dirty = true;
    }
    ImGui::Spacing();
    section("BACKGROUND");
    bool bg = g_set.pad_background != 0;
    if (ImGui::Checkbox("Controllers work in the background", &bg)) { g_set.pad_background = bg; g_set_dirty = true; }
    note("Controllers keep playing while another window has focus (a second screen, a chat window).");
    ImGui::Spacing();
    section("ALWAYS");
    note("F1 held (or the controller's Back held) = the in-game settings menu; in it arrows / WASD move, Enter / J choose, "
         "Escape / K go back, Q / E switch tabs. F11 = fullscreen. Controller sticks stay the sticks. Controllers 2 to 4 "
         "use the same map as player 1.");
}

std::string g_version;
/* the update check: the newer release once known (tag "" = none, or not checked), with its notes and the
 * download address of this zip's flavour (PC, Switch or Android); the Update page downloads and unpacks it */
std::mutex g_update_mx;
std::string g_update_tag, g_update_notes, g_update_url;
std::atomic<bool> g_update_done{true};  /* no check running (SR_LAUNCHER_SHOT waits for it) */
std::string update_tag() {
    std::lock_guard<std::mutex> lk(g_update_mx);
    return g_update_tag;
}
const char* zip_flavour() { return g_mode == M_SWITCH ? "Switch" : g_mode == M_ANDROID ? "Android" : "PC"; }
void start_update_check() {
    /* off with update_check=0 in saves\settings.ini (the game's F1 > About), and for builds without a release tag;
     * SR_LAUNCHER_FAKE_VERSION (testing) pretends to be that version */
    if (const char* fv = getenv("SR_LAUNCHER_FAKE_VERSION")) g_version = fv;
    std::string ini = read_text(path(L"saves\\settings.ini"));
    int v[3];
    if (ini.find("update_check=0") != std::string::npos || !update_parse_version(g_version.c_str(), v)) return;
    g_update_done = false;
    std::thread([]() {
        char fmt[64];
        snprintf(fmt, sizeof fmt, "SonicRiders-%s-Setup-%%s.zip", zip_flavour());
        UpdateInfo info;
        if (update_fetch_info(&info, fmt) == 0 && update_is_newer(info.tag, g_version.c_str())) {
            std::lock_guard<std::mutex> lk(g_update_mx);
            g_update_tag = info.tag;
            g_update_notes = info.notes ? info.notes : "";
            g_update_url = info.zip_url;
        }
        free(info.notes);
        g_update_done = true;
    }).detach();
}
void open_update_page() { ShellExecuteA(nullptr, "open", UPDATE_PAGE, nullptr, nullptr, SW_SHOWNORMAL); }

/* download -> unpack over this folder (build\update\apply.ps1: the zip holds only the port's own files, so the
 * disc image, saves, mods and builds stay) -> restart. The running exe is renamed to .old first (a running exe
 * cannot be overwritten, but can be renamed); the next start deletes it. After a PC update the game must be
 * rebuilt from the new source: build\launcher\rebuild_after_update marks that for the Play page. */
enum { UPD_IDLE, UPD_DOWNLOADING, UPD_UNPACKING, UPD_DONE, UPD_FAILED };
std::atomic<int> g_upd_state{UPD_IDLE};
volatile long long g_upd_got, g_upd_total;
volatile int g_upd_cancel;
std::string g_upd_error;  /* set before g_upd_state becomes UPD_FAILED */
Child g_upd_child;
std::wstring self_exe() {
    wchar_t me[MAX_PATH];
    GetModuleFileNameW(nullptr, me, MAX_PATH);
    return me;
}
const char* kApplyScript =
    "param([string]$Zip, [string]$Root, [string]$Exe)\r\n"
    "$ErrorActionPreference = 'Stop'\r\n"
    "$tmp = Join-Path $Root 'build\\update\\unpacked'\r\n"
    "if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }\r\n"
    "Write-Output 'Unpacking the update...'\r\n"
    "Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force\r\n"
    "$top = Get-ChildItem -LiteralPath $tmp -Directory | Select-Object -First 1\r\n"
    "if (-not $top -or -not (Test-Path -LiteralPath (Join-Path $top.FullName 'setup\\setup.ps1'))) { throw 'The download is not a release zip.' }\r\n"
    "$old = $Exe + '.old'\r\n"
    "if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force }\r\n"
    "Move-Item -LiteralPath $Exe -Destination $old\r\n"
    "Write-Output 'Copying the new files...'\r\n"
    "& robocopy $top.FullName $Root /E /IS /IT /IM /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null\r\n"
    "if ($LASTEXITCODE -ge 8) { Move-Item -LiteralPath $old -Destination $Exe -Force; throw \"Copying failed (robocopy $LASTEXITCODE).\" }\r\n"
    "if (-not (Test-Path -LiteralPath $Exe)) { Move-Item -LiteralPath $old -Destination $Exe -Force }\r\n"
    "Remove-Item -LiteralPath $tmp -Recurse -Force\r\n"
    "Remove-Item -LiteralPath $Zip -Force\r\n"
    "Write-Output 'Updated.'\r\n";

void start_update_download() {
    std::string url;
    {
        std::lock_guard<std::mutex> lk(g_update_mx);
        url = g_update_url;
    }
    if (url.empty()) return;
    CreateDirectoryW(path(L"build").c_str(), nullptr);
    CreateDirectoryW(path(L"build\\update").c_str(), nullptr);
    g_upd_got = g_upd_total = 0;
    g_upd_cancel = 0;
    g_upd_state = UPD_DOWNLOADING;
    std::thread([url]() {
        std::wstring zip = path(L"build\\update\\update.zip");
        if (update_download(url.c_str(), zip.c_str(), &g_upd_got, &g_upd_total, &g_upd_cancel)) {
            g_upd_error = g_upd_cancel ? "Download cancelled." : "The download failed (no internet, or GitHub did not answer).";
            g_upd_state = UPD_FAILED;
            return;
        }
        std::wstring ps1 = path(L"build\\update\\apply.ps1");
        write_text(ps1, kApplyScript);
        {
            std::lock_guard<std::mutex> lk(g_log_mx);
            g_log.clear();
        }
        std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + ps1 + L"\" -Zip \"" + zip +
                           L"\" -Root \"" + g_root + L"\" -Exe \"" + self_exe() + L"\"";
        g_upd_state = UPD_UNPACKING;
        if (!start_child(g_upd_child, cmd, {}, true, true, setup_sink)) {
            g_upd_error = "Could not start PowerShell to unpack the update.";
            g_upd_state = UPD_FAILED;
        }
    }).detach();
}

/* every frame: the unpacking step's end */
void poll_update() {
    if (g_upd_state != UPD_UNPACKING || !g_upd_child.proc || !g_upd_child.done) return;
    if (g_upd_child.exit_code == 0) {
        if (g_mode == M_LAUNCHER) {
            CreateDirectoryW(path(L"build\\launcher").c_str(), nullptr);
            write_text(path(L"build\\launcher\\rebuild_after_update"), update_tag() + "\n");
        }
        g_upd_state = UPD_DONE;
    } else {
        std::lock_guard<std::mutex> lk(g_log_mx);
        g_upd_error = "Unpacking the update failed";
        for (auto it = g_log.rbegin(); it != g_log.rend(); ++it)
            if (!it->empty()) { g_upd_error += ": " + *it; break; }
        g_upd_state = UPD_FAILED;
    }
}

void restart_self() {
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    std::wstring cl = L"\"" + self_exe() + L"\"";
    if (CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    PostQuitMessage(0);
}

void page_update(float w) {
    std::string tag, notes, url;
    {
        std::lock_guard<std::mutex> lk(g_update_mx);
        tag = g_update_tag;
        notes = g_update_notes;
        url = g_update_url;
    }
    if (tag.empty()) {
        section("UPDATES");
        ImGui::TextWrapped("You have the newest version (%s).", g_version.c_str());
        return;
    }
    char head[96];
    snprintf(head, sizeof head, "UPDATE AVAILABLE: %s", tag.c_str());
    section(head);
    ImGui::Text("You have %s.", g_version.c_str());
    ImGui::Spacing();
    float notes_h = std::max(ImGui::GetContentRegionAvail().y - 120 * g_dpi, 100.0f);
    ImGui::BeginChild("notes", ImVec2(w, notes_h), ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(notes.empty() ? "(no release notes)" : notes.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Spacing();
    int st = g_upd_state;
    bool busy = child_running(g_setup) || child_running(g_game);
    if (st == UPD_IDLE || st == UPD_FAILED) {
        ImGui::BeginDisabled(url.empty() || busy);
        if (big_button("DOWNLOAD AND INSTALL", ImVec2(260 * g_dpi, 40 * g_dpi), true, !url.empty() && !busy))
            start_update_download();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Open the release page")) open_update_page();
        if (st == UPD_FAILED) ImGui::TextColored(kRed, "%s", g_upd_error.c_str());
        else if (busy) note("Close the game (or wait for the build) first.");
        else if (url.empty()) note("This release has no download for this version; use the release page.");
        else note("Downloads the new version (a few MB) and puts it over this folder. Your disc image, saves, mods "
                  "and settings stay.");
    } else if (st == UPD_DOWNLOADING) {
        long long got = g_upd_got, total = g_upd_total;
        char lbl[64];
        snprintf(lbl, sizeof lbl, "%.1f / %.1f MB", got / 1048576.0, total / 1048576.0);
        ImGui::ProgressBar(total ? (float)got / (float)total : 0.0f, ImVec2(w * 0.6f, 0), lbl);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) g_upd_cancel = 1;
    } else if (st == UPD_UNPACKING) {
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(w * 0.6f, 0), "Unpacking...");
    } else {  /* UPD_DONE */
        ImGui::TextColored(kGreen, "Updated to %s.", tag.c_str());
        if (big_button("RESTART", ImVec2(200 * g_dpi, 40 * g_dpi), true, true)) restart_self();
        note(g_mode == M_LAUNCHER ? "After the restart, press Rebuild on the Play page: the game is built again from your "
                                    "disc image with the new version (a few minutes)."
                                  : "After the restart, press BUILD AND INSTALL again to build the new version.");
    }
}
void page_about(float w) {
    (void)w;
    section("SONIC RIDERS NATIVE PORT");
    ImGui::Text("Version: %s", g_version.empty() ? "development build" : g_version.c_str());
    std::string upd = update_tag();
    if (!upd.empty()) {
        ImGui::TextColored(kGoldV, "Update available: %s", upd.c_str());
        ImGui::SameLine();
        if (ImGui::Button("See what's new")) g_page = P_UPDATE;
    }
    ImGui::TextWrapped("A fan-made native port, built on your PC from your own disc. Not affiliated with SEGA or Nintendo. "
                       "This launcher contains no game code or data.");
    ImGui::Spacing();
    section("HELP");
    if (ImGui::Button("Project page")) ShellExecuteW(nullptr, L"open", L"https://github.com/teclilla18/sonic-riders-recomp", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button("Report a problem")) ShellExecuteW(nullptr, L"open", L"https://github.com/teclilla18/sonic-riders-recomp/issues", nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button("Open the game folder")) open_in_explorer(g_root);
    note("For a bug report, attach sonicriders.log from the game folder (or use F1 > About > Save a bug report in "
         "the game).");
    ImGui::Spacing();
    section("REBUILD");
    note("Builds the game again from the disc image in baserom\\ (after an update of the port).");
    ImGui::BeginDisabled(!g_ready.disc || child_running(g_setup) || child_running(g_game));
    if (ImGui::Button("Rebuild the game")) {
        g_page = P_PLAY;
        DeleteFileW(path(L"build\\launcher\\rebuild_after_update").c_str());
        start_install(g_ready.disc_path);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    section("LICENSES");
    note("Launcher: Dear ImGui (MIT). Fonts: Windows' Segoe UI.");
}

/* ---------------------------------------------------------------- Online (lobby.cpp) */

/* the player's name, kept in saves\online.ini (the game rewrites settings.ini without keys it doesn't know) */
char g_online_name[lobby::kNameMax + 1];
char g_online_code[16];
bool g_online_started;      /* the game was started from the lobby; the lobby resets when it closes */
std::string g_online_peer;  /* the other player's name in that race */
void online_load() {
    std::string n;
    for (const std::string& l : split_lines(read_text(path(L"saves\\online.ini"))))
        if (l.rfind("name=", 0) == 0) n = lobby::clean_name(l.substr(5));
    /* no default: the Windows account name is often a real name, which other players would see */
    snprintf(g_online_name, sizeof g_online_name, "%s", n.c_str());
}
void online_save() {
    CreateDirectoryW(path(L"saves").c_str(), nullptr);
    write_text(path(L"saves\\online.ini"), std::string("name=") + lobby::clean_name(g_online_name) + "\n");
}
void play_online(const lobby::View& v) {
    if (g_set_dirty) settings_save();
    if (g_mods_dirty) mods_save();
    if (g_ctl_dirty) controls_save();
    bool host = v.me == 0;
    g_online_peer = v.players.size() > 1 ? v.players[host ? 1 : 0] : std::string("the other player");
    std::wstring cmd = L"\"" + path(L"build\\host-mingw\\sonicriders.exe") + L"\"";
    std::vector<std::pair<std::wstring, std::wstring>> env = {
        {L"SR_NET", host ? L"host:" + std::to_wstring(lobby::kGamePort)
                         : L"join:" + widen(v.host_ip) + L":" + std::to_wstring(lobby::kGamePort)},
        {L"SR_NET_TIMEOUT", L"30"},
        {L"SR_NET_NAME", widen(v.players.size() > (size_t)v.me ? v.players[v.me] : std::string())},
        {L"SR_NET_PEER", widen(g_online_peer)}};
    if (start_child(g_game, cmd, env, false, false, nullptr)) g_status = "Online race with " + g_online_peer;
    else g_status = "Could not start the game.";
    g_online_started = true;
}
void player_list(const lobby::View& v) {
    for (size_t i = 0; i < v.players.size(); i++) {
        std::string line = "  " + v.players[i];
        if (i == 0) line += "   (host)";
        if ((int)i == v.me) line += "   - you";
        ImGui::TextColored(i == 0 ? kGoldV : ImVec4(0.86f, 0.89f, 0.95f, 1), "%s", line.c_str());
    }
    for (size_t i = v.players.size(); i < (size_t)lobby::kMaxPlayers; i++) ImGui::TextColored(kDim, "  (open)");
}
bool g_online_inited;  /* the lobby thread runs once the Online page was opened */
/* every frame, on any page: start the game when the host starts the race; back to the lobby when it closes */
void online_poll() {
    if (!g_online_inited) return;
    lobby::View v = lobby::view();
    bool game_on = child_running(g_game);
    if (v.state == lobby::STARTING && !g_online_started && !game_on) {
        play_online(v);
        g_page = P_ONLINE;
    } else if (g_online_started && !game_on && v.state == lobby::STARTING) {
        lobby::leave();
        g_online_started = false;
    }
}
void page_online(float w) {
    if (!g_online_inited) {
        g_online_inited = true;
        online_load();
        lobby::init(path(L"build\\host-mingw\\sonicriders.exe"));
    }
    lobby::View v = lobby::view();
    bool game_on = child_running(g_game);
    float cw = std::min(w, 620.0f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    if (!g_ready.built || !g_ready.disc) {
        section("ONLINE");
        ImGui::TextWrapped("Install the game first (Play page).");
        ImGui::PopTextWrapPos();
        return;
    }
    section("YOUR NAME");
    ImGui::BeginDisabled(v.state != lobby::IDLE);
    ImGui::SetNextItemWidth(cw * 0.6f);
    ImGui::InputTextWithHint("##name", "Type a name", g_online_name, sizeof g_online_name);
    if (ImGui::IsItemDeactivatedAfterEdit()) online_save();
    ImGui::EndDisabled();
    note("Other players see this name in the lobby and in the race.");
    std::string name = lobby::clean_name(g_online_name);
    ImGui::Spacing();
    if (!v.error.empty()) {
        ImGui::TextColored(kRed, "%s", v.error.c_str());
        ImGui::Spacing();
    }
    switch (v.state) {
    case lobby::IDLE: {
        section("HOST A RACE");
        ImGui::BeginDisabled(name.empty() || game_on || !v.ready);
        if (ImGui::Button("Host a lobby")) { lobby::clear_error(); online_save(); lobby::host(name); }
        ImGui::EndDisabled();
        if (name.empty()) ImGui::TextColored(kDim, "Type a name first.");
        ImGui::Spacing();
        section("JOIN A RACE");
        label("Lobbies on your network");
        if (v.found.empty()) ImGui::TextColored(kDim, "  Searching... (none found yet)");
        for (size_t i = 0; i < v.found.size(); i++) {
            const lobby::Found& f = v.found[i];
            ImGui::PushID((int)i);
            ImGui::Text("  %s's lobby   %d/%d", f.host_name.c_str(), f.players, f.max_players);
            ImGui::SameLine();
            bool can = f.open && f.same_game && !name.empty() && !game_on;
            ImGui::BeginDisabled(!can);
            if (ImGui::SmallButton("Join")) { lobby::clear_error(); online_save(); lobby::join(f.ip, name); }
            ImGui::EndDisabled();
            if (!f.same_game) { ImGui::SameLine(); ImGui::TextColored(kRed, "different game version"); }
            else if (!f.open) { ImGui::SameLine(); ImGui::TextColored(kDim, "full"); }
            ImGui::PopID();
        }
        ImGui::Spacing();
        label("Or type a join code");
        ImGui::SetNextItemWidth(cw * 0.35f);
        ImGui::InputTextWithHint("##code", "ABC-DEFG", g_online_code, sizeof g_online_code, ImGuiInputTextFlags_CharsUppercase);
        ImGui::SameLine();
        std::string ip;
        bool ok = lobby::code_to_ip(g_online_code, ip);
        ImGui::BeginDisabled(!ok || name.empty() || game_on || !v.ready);
        if (ImGui::Button("Join with code")) { lobby::clear_error(); online_save(); lobby::join(ip, name); }
        ImGui::EndDisabled();
        ImGui::Spacing();
        note("Both computers must be on the same network for now (playing over the internet comes later). The first "
             "time, Windows may ask to let the launcher and the game through the firewall: allow it (Private networks). "
             "Online races are two players, one per computer, each on their own screen; everyone races with every "
             "character and board unlocked, and your own saves are not used or changed.");
        break;
    }
    case lobby::HOSTING:
        section("YOUR LOBBY");
        ImGui::Text("Join code:");
        ImGui::SameLine();
        ImGui::PushFont(g_font_bold, 28 * g_dpi);
        ImGui::TextColored(kGoldV, "%s", v.code.empty() ? "-" : v.code.c_str());
        ImGui::PopFont();
        note("Players on your network also see your lobby in their list, so they can join without the code.");
        ImGui::Spacing();
        label("Players");
        player_list(v);
        ImGui::Spacing();
        if (big_button("Start race", ImVec2(180 * g_dpi, 40 * g_dpi), true, v.players.size() >= 2 && !game_on)) lobby::start_race();
        ImGui::SameLine();
        if (ImGui::Button("Close lobby")) lobby::leave();
        if (v.players.size() < 2) ImGui::TextColored(kDim, "Waiting for a player to join...");
        break;
    case lobby::JOINING:
        section("JOINING");
        ImGui::TextWrapped("Looking for the lobby at %s...", v.host_ip.c_str());
        if (ImGui::Button("Cancel")) lobby::leave();
        break;
    case lobby::JOINED:
        section("LOBBY");
        ImGui::Text("%s's lobby", v.players.empty() ? "?" : v.players[0].c_str());
        ImGui::Spacing();
        label("Players");
        player_list(v);
        ImGui::Spacing();
        ImGui::TextColored(kDim, "Waiting for %s to start the race...", v.players.empty() ? "the host" : v.players[0].c_str());
        if (ImGui::Button("Leave lobby")) lobby::leave();
        break;
    case lobby::STARTING:
        section("RACING");
        ImGui::TextWrapped("The game is running: racing %s. Close the game to come back here.", g_online_peer.c_str());
        break;
    }
    ImGui::PopTextWrapPos();
}

/* ---------------------------------------------------------------- the frame */

void play() {
    if (g_set_dirty) settings_save();
    if (g_mods_dirty) mods_save();
    if (g_ctl_dirty) controls_save();
    std::wstring cmd = L"\"" + path(L"build\\host-mingw\\sonicriders.exe") + L"\"";
    if (start_child(g_game, cmd, {}, false, false, nullptr)) g_status = "Running";
    else g_status = "Could not start the game.";
}

void draw_ui(float W, float H) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = g_dpi;
    float side = 220 * s, banner = 260 * s, bar = 64 * s;

    /* sidebar */
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(side, H), kSide);
    dl->AddLine(ImVec2(side, 0), ImVec2(side, H), kLine);
    ImGui::SetCursorPos(ImVec2(26 * s, 30 * s));
    ImGui::TextColored(kDim, "Sonic");
    ImGui::SetCursorPos(ImVec2(26 * s, 50 * s));
    ImGui::PushFont(g_font_title, kSizeLogo * g_dpi);
    ImGui::TextColored(kGoldV, "RIDERS");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(26 * s, 84 * s));
    ImGui::PushFont(g_font_bold, kSize * g_dpi);
    ImGui::TextColored(ImVec4(0.55f, 0.68f, 0.95f, 1), "%s",
                       g_mode == M_SWITCH ? "Switch installer" : g_mode == M_ANDROID ? "Android installer" : "PC recompilation");
    ImGui::PopFont();
    float y = 130 * s;
    for (int i = 0; i < P_COUNT; i++) {
        if (installer() && i != P_PLAY && i != P_ABOUT && i != P_UPDATE) continue;
        if (i == P_UPDATE && update_tag().empty()) continue;
        ImGui::SetCursorPos(ImVec2(0, y));
        ImGui::PushID(i);
        bool sel = g_page == i;
        if (ImGui::InvisibleButton("nav", ImVec2(side - 1, 40 * s))) g_page = i;
        bool hov = ImGui::IsItemHovered();
        if (sel || hov) dl->AddRectFilled(ImVec2(0, y), ImVec2(side - 1, y + 40 * s), sel ? IM_COL32(28, 33, 50, 255) : IM_COL32(22, 26, 40, 255));
        if (sel) dl->AddRectFilled(ImVec2(0, y), ImVec2(4 * s, y + 40 * s), kGold);
        dl->AddText(ImVec2(28 * s, y + (40 * s - ImGui::GetFontSize()) / 2), sel ? IM_COL32(242, 190, 60, 255) : IM_COL32(215, 220, 232, 255),
                    installer() && i == P_PLAY ? "Install" : kPages[i]);
        ImGui::PopID();
        y += 40 * s;
    }
    /* call for Sonic Heroes alpha testers (the sister project), under the page list */
    y += 18 * s;
    ImGui::SetCursorPos(ImVec2(18 * s, y));
    ImGui::PushTextWrapPos(side - 18 * s);
    ImGui::TextColored(kGoldV, "Alpha testers needed:");
    ImGui::SetCursorPosX(18 * s);
    ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1), "the Sonic Heroes native port has its first alpha build.");
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosX(18 * s);
    if (ImGui::Button("Sonic Heroes recomp", ImVec2(side - 36 * s, 0)))
        ShellExecuteW(nullptr, L"open", L"https://github.com/teclilla18/sonic-heroes-recomp", nullptr, nullptr, SW_SHOWNORMAL);
    std::string upd = update_tag();
    if (!upd.empty()) {  /* a newer release: a gold button to its download page, above Quit */
        ImGui::SetCursorPos(ImVec2(18 * s, H - bar - 64 * s));
        ImGui::PushTextWrapPos(side - 18 * s);
        ImGui::TextColored(kGoldV, "Update available: %s", upd.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos(ImVec2(18 * s, H - bar - 38 * s));
        ImGui::PushStyleColor(ImGuiCol_Button, kGoldV);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.12f, 1));
        if (ImGui::Button("See what's new", ImVec2(side - 36 * s, 0))) g_page = P_UPDATE;
        ImGui::PopStyleColor(2);
    }
    dl->AddLine(ImVec2(0, H - bar), ImVec2(side, H - bar), kLine);
    ImGui::SetCursorPos(ImVec2(26 * s, H - bar + (bar - ImGui::GetFrameHeight()) / 2));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button("Quit")) PostQuitMessage(0);
    ImGui::PopStyleColor();

    /* banner: the picture from the player's disc (or a plain gradient), darkened on the left under the title */
    float bx = side + 1, bw = W - bx;
    if (g_banner.srv) {
        float u_h = (float)g_banner.w / g_banner.h * (banner / bw);  /* part of the picture's height that fits */
        float v0 = (1 - u_h) * 0.5f;
        dl->AddImage((ImTextureID)(intptr_t)g_banner.srv, ImVec2(bx, 0), ImVec2(W, banner), ImVec2(0, v0), ImVec2(1, v0 + u_h));
    } else {
        dl->AddRectFilledMultiColor(ImVec2(bx, 0), ImVec2(W, banner), IM_COL32(24, 58, 140, 255), IM_COL32(40, 120, 220, 255),
                                    IM_COL32(20, 40, 100, 255), IM_COL32(12, 22, 60, 255));
    }
    dl->AddRectFilledMultiColor(ImVec2(bx, 0), ImVec2(bx + bw * 0.62f, banner), IM_COL32(10, 12, 20, 235), IM_COL32(10, 12, 20, 0),
                                IM_COL32(10, 12, 20, 0), IM_COL32(10, 12, 20, 235));
    dl->AddLine(ImVec2(bx, banner), ImVec2(W, banner), kGold, 2 * s);
    ImGui::SetCursorPos(ImVec2(bx + 30 * s, banner - 98 * s));
    ImGui::PushFont(g_font_title, kSizeTitle * g_dpi);
    ImGui::TextUnformatted("Sonic Riders");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(bx + 30 * s, banner - 46 * s));
    ImGui::PushFont(g_font_small, kSizeSmall * g_dpi);
    ImGui::TextColored(kGoldV, "%s", g_mode == M_SWITCH ? "Static recompilation for Nintendo Switch  \xC2\xB7  GXEE8P"
                                     : g_mode == M_ANDROID ? "Static recompilation for Android  \xC2\xB7  GXEE8P"
                                                           : "Static recompilation for PC  \xC2\xB7  GXEE8P");
    ImGui::PopFont();

    /* page */
    float pad = 30 * s;
    ImGui::SetCursorPos(ImVec2(bx + pad, banner + 26 * s));
    ImGui::BeginChild("page", ImVec2(bw - 2 * pad, H - banner - bar - 36 * s), ImGuiChildFlags_None);
    float pw = ImGui::GetContentRegionAvail().x;
    switch (g_page) {
    case P_PLAY: installer() ? page_installer(pw) : page_play(pw); break;
    case P_ONLINE: page_online(pw); break;
    case P_GRAPHICS: page_graphics(pw); break;
    case P_GAME: page_game(pw); break;
    case P_MODS: page_mods(pw); break;
    case P_CONTROLS: page_controls(pw); break;
    case P_ABOUT: page_about(pw); break;
    case P_UPDATE: page_update(pw); break;
    }
    ImGui::EndChild();

    /* bottom bar: status, Stop, PLAY / INSTALL AND PLAY */
    dl->AddLine(ImVec2(bx, H - bar), ImVec2(W, H - bar), kLine);
    float bh = 42 * s;
    ImGui::SetCursorPos(ImVec2(bx + pad, H - bar + (bar - ImGui::GetFontSize()) / 2));
    bool game_on = child_running(g_game), setup_on = child_running(g_setup);
    std::string blocker = installer() ? installer_blocker() : std::string();
    if (installer() && g_status.empty() && g_install == INST_IDLE)
        ImGui::TextColored(blocker.empty() ? kGreen : kDim, "%s", blocker.empty() ? "\xE2\x9C\x93 Ready to build" : blocker.c_str());
    else if (game_on) ImGui::TextColored(kGreen, "\xE2\x97\x8F Running");
    else if (!g_status.empty()) ImGui::TextColored(g_install == INST_FAILED ? kRed : kGreen, "%s", g_status.c_str());
    else if (g_ready.built && g_ready.disc) ImGui::TextColored(kGreen, "\xE2\x9C\x93 Ready to play");
    else if (g_ready.built) ImGui::TextColored(kRed, "The disc image is missing from baserom\\");
    else ImGui::TextColored(kDim, "Not installed yet");
    float playw = 190 * s, stopw = 80 * s;
    ImGui::SetCursorPos(ImVec2(W - pad - playw - 10 * s - stopw, H - bar + (bar - bh) / 2));
    if (big_button("Stop", ImVec2(stopw, bh), false, game_on || setup_on)) {
        stop_child(g_game);
        stop_child(g_setup);
    }
    ImGui::SameLine(0, 10 * s);
    if (installer()) {
        if (g_install == INST_DONE) {
            if (big_button("CLOSE", ImVec2(playw, bh), true, true)) PostQuitMessage(0);
        } else if (big_button("BUILD AND INSTALL", ImVec2(playw, bh), true,
                              !setup_on && g_install != INST_RUNNING && blocker.empty()))
            start_installer();
    } else if (!g_ready.built) {
        if (big_button("INSTALL AND PLAY", ImVec2(playw, bh), true, !setup_on && !g_disc_choice.empty() && g_ready.setup))
            start_install(g_disc_choice);
    } else if (big_button("PLAY", ImVec2(playw, bh), true, !game_on && !setup_on && g_ready.disc)) {
        play();
    }
    ImGui::End();
}

/* SR_LAUNCHER_TEST="<seconds>[;key=value,...]" (testing): sets those settings as the pages would, presses PLAY,
 * presses Stop after <seconds> (unless the game closed itself), and writes build\launcher\test.txt: start, stop,
 * exit code, and the settings read back after the game closed (the F1 menu's changes) */
const char* g_test;
std::string g_test_log;
void test_note(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g_test_log += b;
    g_test_log += "\n";
}
std::string settings_text() {
    char b[256];
    snprintf(b, sizeof b, "screen=%d show_fps=%d chaos_emerald_shop=%d fullscreen=%d framerate=%d pc_fps=%d hud_stretch=%d fov=%d pad_background=%d sharpness=%d",
             g_set.screen, g_set.show_fps, g_set.chaos_emerald_shop, g_set.fullscreen, g_set.framerate, g_set.pc_fps,
             g_set.hud_stretch, g_set.fov, g_set.pad_background, g_set.sharpness);
    return b;
}
/* called once a frame; true when the test is over */
bool test_step(int frame) {
    static double t0;
    static int phase;  /* 0 waiting, 1 game running, 2 game closed */
    if (phase == 0 && frame == 20) {
        std::string spec = g_test;
        size_t semi = spec.find(';');
        if (semi != std::string::npos) {
            std::string sets = spec.substr(semi + 1) + ",";
            for (size_t a = 0, b; (b = sets.find(',', a)) != std::string::npos; a = b + 1) {
                std::string kv = sets.substr(a, b - a);
                size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                if (int* p = setting_ptr(kv.substr(0, eq))) {
                    *p = atoi(kv.c_str() + eq + 1);
                    g_set_dirty = true;
                }
                for (int i = 0; i < kNBinds; i++)  /* key.A=Space, pad.Z=leftshoulder: as the Controls page would */
                    if (kv.compare(4, eq - 4, kBinds[i].id) == 0 && eq > 4) {
                        if (!kv.compare(0, 4, "key.")) g_key[i] = kv.substr(eq + 1);
                        else if (!kv.compare(0, 4, "pad.") && kBinds[i].pad) g_pad[i] = kv.substr(eq + 1);
                        else continue;
                        g_ctl_dirty = true;
                    }
            }
        }
        test_note("before PLAY: %s", settings_text().c_str());
        play();
        test_note("PLAY: %s", g_status.c_str());
        t0 = ImGui::GetTime();
        phase = 1;
    } else if (phase == 1) {
        bool on = child_running(g_game);
        if (on && ImGui::GetTime() - t0 > atof(g_test)) {
            test_note("still running after %.0f s: Stop", ImGui::GetTime() - t0);
            stop_child(g_game);
        } else if (!on) {
            test_note("game closed after %.1f s, exit code %lu", ImGui::GetTime() - t0, (unsigned long)g_game.exit_code);
            phase = 2;
        }
    } else if (phase == 2) {  /* poll_children has read the settings again */
        test_note("after the game: %s; status \"%s\"", settings_text().c_str(), g_status.c_str());
        write_text(path(L"build\\launcher\\test.txt"), g_test_log);
        return true;
    }
    return false;
}

/* SR_LAUNCHER_INSTALL=<disc image> (testing, e.g. tools/sandbox_test.py --launcher): picks that disc and presses
 * INSTALL AND PLAY, waits for the setup, writes launcher_test.txt in the game folder (result, last setup lines,
 * the ready check) and quits with exit code 0 when the game is installed and PLAY is ready, else 1. In installer mode
 * it presses BUILD AND INSTALL with the defaults (Switch: no SD card unless SR_INSTALLER_SD; Android: no phone install,
 * licence accepted) and passes when the setup succeeds */
const char* g_install_test;
int g_install_test_exit = 1;
bool install_test_step(int frame) {
    if (frame == 20) {
        g_disc_choice = widen(g_install_test);
        if (installer()) {
            g_ic.sd = getenv("SR_INSTALLER_SD") ? widen(getenv("SR_INSTALLER_SD")) : L"";
            g_ic.copy_iso = false;
            g_ic.phone_install = false;
            g_ic.license = true;
            start_installer();
        } else start_install(g_disc_choice);
        return false;
    }
    if (frame < 20 || g_install == INST_RUNNING) return false;
    refresh_ready();
    bool ok = g_install == INST_DONE && (installer() || (g_ready.built && g_ready.disc));
    std::string rep = std::string(ok ? "PASS" : "FAIL") + ": install " +
                      (g_install == INST_DONE ? "done" : g_install == INST_FAILED ? "failed" : "not started") +
                      ", setup exit code " + std::to_string(g_setup.exit_code) + ", game built " + (g_ready.built ? "yes" : "no") +
                      ", disc in baserom " + (g_ready.disc ? "yes" : "no") + ", status \"" + g_status + "\"\n";
    {
        std::lock_guard<std::mutex> lk(g_log_mx);
        for (size_t i = g_log.size() > 15 ? g_log.size() - 15 : 0; i < g_log.size(); i++) rep += "  " + g_log[i] + "\n";
    }
    write_text(path(L"launcher_test.txt"), rep);
    g_install_test_exit = ok ? 0 : 1;
    return true;
}

/* the install's next steps, checked every frame */
void poll_children() {
    online_poll();  /* the Online lobby: start the game when the race starts (any page, minimized too) */
    static bool game_was;
    bool game_on = child_running(g_game);
    if (game_was && !game_on) {  /* the game closed: it may have changed its settings (F1 menu) */
        g_status.clear();
        settings_load();
        refresh_ready();
    }
    game_was = game_on;
    if (g_install == INST_RUNNING && g_setup.done && installer()) {
        g_install = g_setup.exit_code == 0 ? INST_DONE : INST_FAILED;
        g_status = g_install == INST_DONE ? "Done." : "The build failed: see the messages (setup.log has all of them).";
        return;
    }
    if (g_install == INST_RUNNING && g_setup.done) {
        refresh_ready();
        if (g_setup.exit_code == 0 && g_ready.built) {
            g_install = INST_DONE;
            g_status = "Installed. Press PLAY.";
            settings_load();
        }
        else {
            g_install = INST_FAILED;
            g_status = "The install failed: see the messages above (setup.log has all of them).";
        }
    }
}

/* SR_LAUNCHER_SHOT=<file.png> (testing): the back buffer as a PNG through WIC */
const char* g_shot_file;
void save_backbuffer(const std::wstring& file) {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (!bb) return;
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st))) {
        g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        IWICImagingFactory* wic = nullptr;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)) &&
            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
            IWICStream* s = nullptr;
            IWICBitmapEncoder* enc = nullptr;
            IWICBitmapFrameEncode* fr = nullptr;
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;  /* the PNG encoder's own format */
            std::vector<uint8_t> px((size_t)m.RowPitch * d.Height);
            memcpy(px.data(), m.pData, px.size());
            for (UINT y = 0; y < d.Height; y++)
                for (UINT x = 0; x < d.Width; x++) {
                    uint8_t* p = &px[(size_t)y * m.RowPitch + x * 4];
                    std::swap(p[0], p[2]);
                    p[3] = 255;
                }
            if (SUCCEEDED(wic->CreateStream(&s)) && SUCCEEDED(s->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                SUCCEEDED(enc->Initialize(s, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
                SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(d.Width, d.Height)) &&
                SUCCEEDED(fr->SetPixelFormat(&fmt))) {
                fr->WritePixels(d.Height, m.RowPitch, (UINT)px.size(), px.data());
                fr->Commit();
                enc->Commit();
            }
            if (fr) fr->Release();
            if (enc) enc->Release();
            if (s) s->Release();
            wic->Release();
            g_ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

/* ---------------------------------------------------------------- window */

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    switch (m) {
    case WM_SIZE:
        if (g_dev && w != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            create_rtv();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 620;
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_root = exe;
    g_root = g_root.substr(0, g_root.find_last_of(L"\\/"));
    /* started from a build folder (e.g. build\launcher\): use the nearest parent that holds the port (setup\) */
    for (std::wstring d = g_root; !exists(g_root + L"\\setup\\setup.ps1");) {
        size_t cut = d.find_last_of(L"\\/");
        if (cut == std::wstring::npos) break;
        d = d.substr(0, cut);
        if (exists(d + L"\\setup\\setup.ps1")) { g_root = d; break; }
    }
    SetCurrentDirectoryW(g_root.c_str());
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::string v = read_text(path(L"VERSION"));
    g_version = v.substr(0, v.find_first_of("\r\n"));
    {  /* installer mode: by the program's name ("Install Sonic Riders (Switch).exe"), or SR_INSTALLER for tests */
        wchar_t me[MAX_PATH];
        GetModuleFileNameW(nullptr, me, MAX_PATH);
        std::string name = narrow(me);
        name = name.substr(name.find_last_of("\\/") + 1);
        if (const char* e = getenv("SR_INSTALLER")) name = e;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (name.find("switch") != std::string::npos) g_mode = M_SWITCH;
        else if (name.find("android") != std::string::npos) g_mode = M_ANDROID;
        if (g_mode == M_SWITCH) find_sd_cards();
    }
    start_update_check();
    DeleteFileW((self_exe() + L".old").c_str());  /* left by an update (the exe that ran it) */

    WNDCLASSEXW wc = {sizeof wc, CS_CLASSDC, wndproc, 0, 0, inst, LoadIconW(inst, L"APPICON"), LoadCursorW(nullptr, IDC_ARROW),
                      nullptr, nullptr, L"SonicRidersLauncher", nullptr};
    RegisterClassExW(&wc);
    float dpi = GetDpiForSystem() / 96.0f;
    g_dpi = dpi;
    int ww = (int)(1280 * dpi), wh = (int)(820 * dpi);
    RECT work;  /* not bigger than the screen's free area (taskbar excluded) */
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        ww = std::min(ww, (int)(work.right - work.left));
        wh = std::min(wh, (int)(work.bottom - work.top));
    }
    const wchar_t* title = g_mode == M_SWITCH ? L"Sonic Riders - Switch installer"
                           : g_mode == M_ANDROID ? L"Sonic Riders - Android installer" : L"Sonic Riders";
    HWND hwnd = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                              nullptr, nullptr, inst, nullptr);
    BOOL dark = TRUE;  /* dark title bar (Windows 10 2004+ / 11) */
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    if (!create_device(hwnd)) {
        MessageBoxW(hwnd, L"Could not start Direct3D 11.", L"Sonic Riders", MB_ICONERROR);
        return 1;
    }
    g_shot_file = getenv("SR_LAUNCHER_SHOT");  /* testing: save the window's picture and quit */
    g_test = getenv("SR_LAUNCHER_TEST");
    g_install_test = getenv("SR_LAUNCHER_INSTALL");
    const char* upd_test = getenv("SR_LAUNCHER_UPDATE_TEST");
    if (const char* pg = getenv("SR_LAUNCHER_PAGE")) g_page = std::clamp(atoi(pg), 0, P_COUNT - 1);
    if (g_shot_file || g_test || g_install_test || upd_test) {  /* drawn off-screen, without taking the focus */
        SetWindowPos(hwnd, nullptr, -10000, -10000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(hwnd, show);
        UpdateWindow(hwnd);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    load_fonts(dpi);
    apply_style(dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    settings_load();
    mods_load();
    controls_load();
    refresh_ready();
    reload_banner();

    bool quit = false;
    while (!quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;
        if (IsIconic(hwnd)) {
            Sleep(50);
            poll_children();
            continue;
        }
        poll_children();
        poll_update();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RECT rc;
        GetClientRect(hwnd, &rc);
        draw_ui((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
        ImGui::Render();
        const float clear[4] = {20 / 255.0f, 24 / 255.0f, 36 / 255.0f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        static int frames;
        ++frames;
        if (g_test && test_step(frames)) break;
        if (g_install_test && install_test_step(frames)) break;
        /* SR_LAUNCHER_UPDATE_TEST=1 (testing): once the check found an update, presses DOWNLOAD AND INSTALL, waits,
         * writes build\update_test.txt (result, the steps' last lines) and quits (exit code 0 = updated) */
        if (upd_test && g_update_done && frames > 20) {
            static bool started;
            if (!started) {
                started = true;
                if (update_tag().empty()) {
                    write_text(path(L"build\\update_test.txt"), "FAIL: no update found\n");
                    g_install_test_exit = 1;
                    break;
                }
                start_update_download();
            }
            int st = g_upd_state;
            if (st == UPD_DONE || st == UPD_FAILED) {
                std::string rep = std::string(st == UPD_DONE ? "PASS" : "FAIL") + ": update to " + update_tag() + ", " +
                                  std::to_string(g_upd_got) + " bytes" + (st == UPD_FAILED ? ", " + g_upd_error : "") + "\n";
                {
                    std::lock_guard<std::mutex> lk(g_log_mx);
                    for (const std::string& l : g_log) rep += "  " + l + "\n";
                }
                write_text(path(L"build\\update_test.txt"), rep);
                g_install_test_exit = st == UPD_DONE ? 0 : 1;
                break;
            }
        }
        if (g_shot_file && frames >= 20 && (g_update_done || frames >= 600)) {  /* fonts, textures, update check settled */
            save_backbuffer(widen(g_shot_file));
            break;
        }
        g_swap->Present(1, 0);
        /* settings are written when they change (and before PLAY), so the game always reads the latest */
        static double last_save;
        if ((g_set_dirty || g_mods_dirty || g_ctl_dirty) && ImGui::GetTime() - last_save > 0.5) {
            if (g_set_dirty && !child_running(g_game)) settings_save();
            if (g_mods_dirty) mods_save();
            if (g_ctl_dirty) controls_save();
            last_save = ImGui::GetTime();
        }
    }
    if (g_set_dirty) settings_save();
    if (g_mods_dirty) mods_save();
    if (g_ctl_dirty) controls_save();
    lobby::shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_setup.reader.joinable()) g_setup.reader.detach();
    if (g_game.reader.joinable()) g_game.reader.detach();
    if (g_upd_child.reader.joinable()) g_upd_child.reader.detach();
    return g_install_test || upd_test ? g_install_test_exit : 0;
}
