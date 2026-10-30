/* The settings menu and FPS counter (overlay.h): Dear ImGui, laid out like SwitchWakerHD's settings
 * overlay (a centred panel with tabs; L / R switch tabs; a gold controller cursor; teal-style section
 * headings and grey notes; a short explanation on the selected option; Close and Quit Game on every
 * tab, Quit asking first; the Switch has no Quit: HOME closes games there), in Sonic colours.
 * ImGui runs on the render thread (which pumps input). When its picture changes, the draw lists are
 * copied and drawn in software (menu_raster.cpp) by a low-priority helper thread into the image that
 * is not on screen, so the render thread never waits for it; the image is then swapped in. */
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "menu_raster.h"
#include "overlay.h"
#include "runtime.h"
#include "settings.h"
#ifdef __SWITCH__
#include <switch.h>
#endif

extern "C" {
extern const char g_build_version[];
int mods_count(void);
const char* mods_title(int i);
int mods_enabled(int i);
void mods_set_enabled(int i, int on);
}

namespace {

constexpr uint64_t kHoldTicks = 40500000ull / 2;  /* Minus / Back / F1 held this long opens the menu (0.5 s) */
constexpr float kFontSize = 19.0f;

/* two images: g_front is shown, the other is drawn by the helper thread; each with its content
 * rectangle (x0, y0, x1, y1; empty when x0 >= x1), outside which it is transparent */
uint8_t g_img[2][OVERLAY_W * OVERLAY_H * 4];
int g_box[2][4];
int g_front;
unsigned g_version;
bool g_open, g_gate, g_quit_popup;
unsigned g_prev;
uint64_t g_menu_since;
std::atomic<int> g_fps10{0};
int g_shown_fps10 = -1;
int g_tab, g_select_tab = -1;
bool g_mods_changed;
int g_shot_request, g_pads_request, g_quit_request;
bool g_just_opened;
bool g_ready;
uint64_t g_last_ns;
uint64_t g_last_hash;
bool g_had_image;
/* overlay_set_banner: a message across the top (online play: desync, connection lost) */
std::mutex g_banner_lock;
char g_banner_next[256], g_banner[256];
std::atomic<unsigned> g_banner_seq{0};
unsigned g_shown_banner_seq;

/* the helper thread's job: a copy of one frame's draw lists */
struct Job {
    ImDrawData dd;
    int buf;
    int box[4];
    double ms;
};
std::mutex g_mx;
std::condition_variable g_cv;
Job* g_job;        /* waiting for the helper (render thread -> helper) */
Job* g_done;       /* drawn (helper -> render thread) */
bool g_busy;       /* a job is out (render thread only) */
bool g_retry;      /* a changed picture waits for the helper */
bool g_worker_started;

void job_free(Job* j) {
    for (ImDrawList* l : j->dd.CmdLists) IM_DELETE(l);
    delete j;
}

/* the area the draw lists cover: their vertices, inside their clip rectangles */
void content_box(const ImDrawData* dd, int box[4]) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const ImDrawList* l : dd->CmdLists)
        for (const ImDrawCmd& c : l->CmdBuffer) {
            if (c.UserCallback || !c.ElemCount) continue;
            float vx0 = 1e9f, vy0 = 1e9f, vx1 = -1e9f, vy1 = -1e9f;
            const ImDrawIdx* ix = l->IdxBuffer.Data + c.IdxOffset;
            const ImDrawVert* v = l->VtxBuffer.Data + c.VtxOffset;
            for (unsigned i = 0; i < c.ElemCount; i++) {
                const ImVec2 p = v[ix[i]].pos;
                vx0 = fminf(vx0, p.x), vy0 = fminf(vy0, p.y), vx1 = fmaxf(vx1, p.x), vy1 = fmaxf(vy1, p.y);
            }
            vx0 = fmaxf(vx0, c.ClipRect.x), vy0 = fmaxf(vy0, c.ClipRect.y);
            vx1 = fminf(vx1, c.ClipRect.z), vy1 = fminf(vy1, c.ClipRect.w);
            if (vx0 >= vx1 || vy0 >= vy1) continue;
            x0 = fminf(x0, vx0), y0 = fminf(y0, vy0), x1 = fmaxf(x1, vx1), y1 = fmaxf(y1, vy1);
        }
    box[0] = (int)fmaxf(0.0f, floorf(x0) - 1);
    box[1] = (int)fmaxf(0.0f, floorf(y0) - 1);
    box[2] = (int)fminf((float)OVERLAY_W, ceilf(x1) + 1);
    box[3] = (int)fminf((float)OVERLAY_H, ceilf(y1) + 1);
    if (box[0] >= box[2] || box[1] >= box[3]) box[0] = box[1] = box[2] = box[3] = 0;
}

void clear_box(uint8_t* img, const int b[4]) {
    for (int y = b[1]; y < b[3]; y++) memset(img + ((size_t)y * OVERLAY_W + b[0]) * 4, 0, (size_t)(b[2] - b[0]) * 4);
}

/* off the game's core and below the GP thread there (gx_fifo.c): it only uses spare time */
int raster_thread(void*) {
    rt_thread_pin(2);
    rt_thread_lower_priority();
    rt_thread_lower_priority();
    for (;;) {
        Job* j;
        {
            std::unique_lock<std::mutex> lk(g_mx);
            g_cv.wait(lk, [] { return g_job != nullptr; });
            j = g_job;
            g_job = nullptr;
        }
        uint64_t t0 = platform_now_ns();
        uint8_t* img = g_img[j->buf];
        clear_box(img, g_box[j->buf]);
        menu_raster(&j->dd, img, OVERLAY_W, OVERLAY_H);
        j->ms = (platform_now_ns() - t0) / 1e6;
        std::lock_guard<std::mutex> lk(g_mx);
        g_done = j;
    }
    return 0;
}

/* ---- Sonic colours ---- */
const ImVec4 kGold(1.00f, 0.82f, 0.18f, 1.0f);    /* rings */
const ImVec4 kNote(0.72f, 0.79f, 0.92f, 1.0f);
const ImVec4 kWarn(1.00f, 0.42f, 0.32f, 1.0f);    /* Sonic's shoes */

void setup_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.GrabRounding = 5;
    s.TabRounding = 6;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 2;
    s.AntiAliasedLinesUseTex = false;  /* the software renderer samples without filtering (menu_raster.cpp) */
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.96f, 0.97f, 1.00f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.56f, 0.70f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.05f, 0.17f, 1.00f);   /* deep navy (opaque: plain stores in menu_raster.cpp) */
    c[ImGuiCol_ChildBg] = ImVec4(0.03f, 0.07f, 0.22f, 1.00f);    /* the page area, a shade lighter */
    c[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.08f, 0.24f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.46f, 1.00f, 0.55f);     /* Sonic blue */
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.20f, 0.62f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.30f, 0.86f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.16f, 0.42f, 0.85f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.27f, 0.64f, 0.90f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.35f, 0.80f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.10f, 0.32f, 0.88f, 0.90f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.47f, 1.00f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.56f, 1.00f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.10f, 0.32f, 0.88f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.47f, 1.00f, 0.85f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.56f, 1.00f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.06f, 0.18f, 0.52f, 0.90f);
    c[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.47f, 1.00f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.38f, 0.96f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = kGold;
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.75f, 0.12f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.86f, 0.28f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.46f, 1.00f, 0.40f);
    c[ImGuiCol_NavCursor] = kGold;  /* the controller cursor: gold, easy to follow */
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.05f, 0.55f);
}

void setup_fonts() {
    ImGuiIO& io = ImGui::GetIO();
#ifdef __SWITCH__
    /* the console's own font, as SwitchWakerHD (its overlay_switch.cpp) */
    static bool pl;
    if (!pl && R_SUCCEEDED(plInitialize(PlServiceType_User))) pl = true;
    PlFontData font;
    if (pl && R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;  /* the system's shared memory */
        if (io.Fonts->AddFontFromMemoryTTF(font.address, (int)font.size, kFontSize, &cfg)) {
            rt_log("[menu] font: the system font");
            return;
        }
    }
#else
    static const char* const candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const char* path : candidates) {
        if (FILE* f = fopen(path, "rb")) {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, kFontSize)) {
                rt_log("[menu] font: %s", path);
                return;
            }
        }
    }
#endif
    ImFontConfig cfg;
    cfg.SizePixels = kFontSize;
    io.Fonts->AddFontDefault(&cfg);
    rt_log("[menu] font: Dear ImGui's default");
}

void init() {
    if (g_ready) return;
    g_ready = true;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(OVERLAY_W, OVERLAY_H);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "sonicriders-software";
    io.ConfigNavCursorVisibleAlways = true;
    setup_style();
    setup_fonts();
    g_last_ns = platform_now_ns();
}

/* ---- widgets: SwitchWakerHD's look ---- */
void heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}
void note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kNote);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
void warn(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
/* the explanation of the item just drawn, while it is selected */
void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(520, FLT_MAX));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(500);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
bool radio(const char* label, bool active, const char* tip = nullptr) {
    bool clicked = ImGui::RadioButton(label, active) && !active;
    if (tip) help(tip);
    return clicked;
}
bool check(const char* label, int* value, const char* tip = nullptr) {
    bool v = *value != 0;
    bool changed = ImGui::Checkbox(label, &v);
    if (tip) help(tip);
    if (changed) *value = v;
    return changed;
}

void close_menu() {
    g_open = false;
    g_gate = true;
    g_quit_popup = false;
}

/* ---- tabs ---- */
void tab_display() {
    heading("Screen");
    for (int m = 0; m < SCREEN_MODES; m++) {
        static const char* const tips[SCREEN_MODES] = {
            "The original 4:3 picture with black bars at the sides.",
            "The cameras see a wider 16:9 view (the HUD is stretched a little).",
            "The 4:3 picture stretched to fill a 16:9 screen."};
        if (m) ImGui::SameLine();
        if (radio(settings_screen_name(m), g_settings.screen == m, tips[m])) {
            g_settings.screen = m;
            settings_apply();
        }
    }
#ifdef __SWITCH__
    heading("Resolution");
    for (int r = 0; r < RES_MODES; r++) {
        static const char* const names[RES_MODES] = {"1x  (640x528)", "1.5x  (960x792)", "2x  (1280x1056)"};
        static const char* const tips[RES_MODES] = {
            "The GameCube's own resolution: the fastest, best for playing without an overclock.",
            "Sharper; needs more from the graphics chip.",
            "Sharpest; needs an overclocked console in busy races."};
        if (r) ImGui::SameLine();
        if (radio(names[r], g_settings.resolution == r, tips[r])) {
            g_settings.resolution = r;
            settings_apply();
        }
    }
    note("Higher resolutions make the picture sharper but can lower the frame rate in races.");
#else
    heading("Window");
    if (check("Fullscreen  (F11 / Alt+Enter)", &g_settings.fullscreen, "Borderless fullscreen on the current display."))
        settings_apply();
#endif
}

void tab_performance() {
    heading("Frame rate");
    static const char* const tips[FR_MODES] = {
        "60 fps while the console keeps up. In scenes too busy for that it switches to a steady 30 fps at full "
        "speed, and back to 60 once the scene allows it.",
        "60 fps while the console keeps up; when it cannot, frames are left out instead, so races keep their real speed.",
        "Sonic Riders' own 30 fps mode: a steady 30 fps at full speed. The smoothest choice on a console that is not overclocked."};
    const int order[FR_MODES] = {FR_AUTO, FR_30, FR_60};
    for (int k = 0; k < FR_MODES; k++) {
        int m = order[k];
        if (radio(settings_framerate_name(m), g_settings.framerate == m, tips[m])) {
            g_settings.framerate = m;
            settings_apply();
        }
    }
    note("All three keep the game at its real speed. If Auto looks uneven, 60 fps or 30 fps is steadier.");
    heading("Frame counter");
    if (check("Show the frame rate in the corner", &g_settings.show_fps, "Frames drawn per second, top left."))
        settings_apply();
#ifdef __SWITCH__
    heading("Handheld graphics");
    if (check("Nintendo's 384 MHz graphics setting in handheld", &g_settings.handheld_gpu,
              "One of the clock settings Nintendo offers its own games (the system's default is 307 MHz). "
              "Not an overclock; docked play is not changed."))
        settings_apply();
#endif
}

void tab_controls() {
#ifdef __SWITCH__
    heading("Controllers");
    int pads = 0;
    for (int c = 0; c < 4; c++) pads += c == 0 || input_connected(c);
    ImGui::Text("Connected: %d", pads);
    if (ImGui::Button("Set up controllers")) {
        close_menu();
        g_pads_request = 1;
    }
    help("The system's controller screen: pair or split Joy-Cons and change the player order.");
    heading("Buttons");
    note("A B X Y = A B X Y,  + = START,  ZL / ZR = L / R,  R = Z,  sticks = control stick and C-stick, "
         "D-pad = D-pad. A single Joy-Con held sideways works as a small controller.");
#else
    heading("Keyboard");
    note("WASD = control stick,  J / K / L / I = A / B / X / Y,  Enter = START,  U / O = L / R,  P = Z,  "
         "T F G H = D-pad. F1 held = this menu (Q / E switch its tabs), F11 = fullscreen.");
    heading("Controllers");
    note("Xbox and other controllers work as player 1 (more controllers: players 2 to 4). "
         "Hold Back (View) to open this menu.");
#endif
}

void tab_gameplay() {
    heading("Unlocks");
    if (check("Unlock the Chaos Emerald gear", &g_settings.chaos_emerald,
              "Makes the Chaos Emerald gear available from the start (Super Sonic). Nothing else is unlocked."))
        settings_apply();
}

void tab_mods() {
    heading("Installed mods");
    int n = mods_count();
    if (!n) note("No mods installed. See docs/MODDING.md for how to add them.");
    for (int i = 0; i < n; i++) {
        bool on = mods_enabled(i) != 0;
        char label[128];
        snprintf(label, sizeof label, "%s##mod%d", mods_title(i), i);
        if (ImGui::Checkbox(label, &on)) {
            mods_set_enabled(i, on);
            g_mods_changed = true;
        }
    }
    if (g_mods_changed) warn("Restart the game to apply the changes.");
    else if (n) note("Mods load in this order (mods/mods.ini).");
}

void tab_about() {
    heading("Sonic Riders native port");
    ImGui::Text("Version: %s", g_build_version);
    note("A fan-made native port built from your own disc. Not affiliated with SEGA or Nintendo.");
    heading("Bug reports");
    if (ImGui::Button("Save screenshot")) {
        close_menu();
        g_shot_request = 1;
    }
    help("Saves the next picture next to the game's log, for bug reports.");
}

void quit_row() {
    const float w_close = ImGui::CalcTextSize("Close").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::PushStyleColor(ImGuiCol_Text, kNote);
    ImGui::TextUnformatted("L / R: tabs    A: choose    B: back");
    ImGui::PopStyleColor();
#ifdef __SWITCH__
    /* no Quit Game: games on the Switch are closed from the HOME menu */
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w_close);
    if (ImGui::Button("Close")) close_menu();
#else
    const float w_quit = ImGui::CalcTextSize("Quit Game").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w_close - w_quit - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Close")) close_menu();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.12f, 0.12f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.20f, 0.18f, 1.0f));
    if (ImGui::Button("Quit Game")) {
        g_quit_popup = true;
        ImGui::OpenPopup("Quit Sonic Riders?");
    }
    ImGui::PopStyleColor(2);
    ImGui::SetNextWindowPos(ImVec2(OVERLAY_W * 0.5f, OVERLAY_H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit Sonic Riders?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        note("Progress since your last save in the game is lost.");
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(140, 0)) || !g_quit_popup) {
            g_quit_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();  /* A straight away cancels */
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(140, 0))) {
            g_quit_popup = false;
            ImGui::CloseCurrentPopup();
            close_menu();
            g_quit_request = 1;
        }
        ImGui::EndPopup();
    }
#endif
}

void settings_window() {
    const ImVec2 size(fminf(980.0f, OVERLAY_W * 0.94f), fminf(620.0f, OVERLAY_H * 0.90f));
    ImGui::SetNextWindowPos(ImVec2(OVERLAY_W * 0.5f, OVERLAY_H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    if (g_just_opened) {
        ImGui::SetNextWindowFocus();
        g_just_opened = false;
    }
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Sonic Riders  -  Settings", nullptr, fl)) {
        struct Tab {
            const char* name;
            void (*draw)();
        };
        static const Tab tabs[] = {{"Display", tab_display}, {"Performance", tab_performance}, {"Controls", tab_controls},
                                   {"Gameplay", tab_gameplay}, {"Mods", tab_mods},          {"About", tab_about}};
        const int ntabs = (int)(sizeof tabs / sizeof tabs[0]);
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (int i = 0; i < ntabs; i++) {
                ImGuiTabItemFlags f = g_select_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tabs[i].name, nullptr, f)) {
                    g_tab = i;
                    /* the page ends one row above the window's bottom: the Close / Quit row */
                    ImGui::BeginChild("page", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_NavFlattened);
                    tabs[i].draw();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            g_select_tab = -1;
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        quit_row();
    }
    ImGui::End();
}

void fps_counter() {
    if (!g_settings.show_fps || g_shown_fps10 < 0) return;
    char t[32];
    snprintf(t, sizeof t, "%d.%d FPS", g_shown_fps10 / 10, g_shown_fps10 % 10);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddRectFilled(ImVec2(8, 8), ImVec2(24 + ts.x, 16 + ts.y + 4), IM_COL32(2, 10, 40, 190), 6.0f);
    dl->AddRect(ImVec2(8, 8), ImVec2(24 + ts.x, 16 + ts.y + 4), IM_COL32(50, 118, 255, 200), 6.0f);
    dl->AddText(ImVec2(16, 12), IM_COL32(255, 210, 46, 255), t);
}

void banner_box() {
    if (!g_banner[0]) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(g_banner, nullptr, false, OVERLAY_W - 160.0f);
    float x0 = (OVERLAY_W - ts.x) * 0.5f - 20, y0 = 40;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + ts.x + 40, y0 + ts.y + 24), IM_COL32(60, 6, 10, 225), 8.0f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + ts.x + 40, y0 + ts.y + 24), IM_COL32(255, 80, 80, 230), 8.0f, 0, 2.0f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x0 + 20, y0 + 12), IM_COL32(255, 240, 240, 255), g_banner,
                nullptr, OVERLAY_W - 160.0f);
}

uint64_t draw_hash(const ImDrawData* dd) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t v;
            memcpy(&v, b + i, 8);
            h = (h ^ v) * 1099511628211ull;
        }
        for (; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    };
    for (const ImDrawList* l : dd->CmdLists) {
        mix(l->VtxBuffer.Data, (size_t)l->VtxBuffer.Size * sizeof(ImDrawVert));
        mix(l->IdxBuffer.Data, (size_t)l->IdxBuffer.Size * sizeof(ImDrawIdx));
        for (const ImDrawCmd& c : l->CmdBuffer) mix(&c.ClipRect, sizeof c.ClipRect);
    }
    return h;
}

/* a finished image becomes the shown one */
void collect() {
    Job* j;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        j = g_done;
        g_done = nullptr;
    }
    if (!j) return;
    memcpy(g_box[j->buf], j->box, sizeof j->box);
    g_front = j->buf;
    g_version++;
    g_busy = false;
    static int logged;
    if (logged < 3 && g_open) {
        logged++;
        rt_log("[menu] picture drawn in %.2f ms (helper thread)", j->ms);
    }
    job_free(j);
}

/* one ImGui frame; the image is drawn again only when ImGui's output changed */
void frame() {
    g_retry = false;
    init();
    ImGuiIO& io = ImGui::GetIO();
    uint64_t now = platform_now_ns();
    float dt = (float)((now - g_last_ns) / 1e9);
    g_last_ns = now;
    io.DeltaTime = dt > 0.0f && dt < 0.5f ? dt : 1.0f / 60.0f;
    ImGui::NewFrame();
    if (g_open) settings_window();
    fps_counter();
    banner_box();
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    uint64_t h = draw_hash(dd);
    bool tex_pending = false;
    if (dd->Textures)
        for (ImTextureData* t : *dd->Textures) tex_pending |= t->Status != ImTextureStatus_OK && t->Status != ImTextureStatus_Destroyed;
    if (h == g_last_hash && g_had_image && !tex_pending) return;
    if (g_busy) {  /* the helper is still drawing: this picture is looked at again next frame */
        g_retry = true;
        return;
    }
    /* texture requests (the font atlas) are answered here, while the helper is idle: it reads the
     * pixels of textures in use, and ImGui frees a texture's pixels only after it was answered */
    if (tex_pending) menu_raster_textures(dd);
    g_last_hash = h;
    g_had_image = true;
    Job* j = new Job();
    j->dd.Valid = true;
    j->dd.DisplayPos = dd->DisplayPos;
    j->dd.DisplaySize = dd->DisplaySize;
    j->dd.FramebufferScale = dd->FramebufferScale;
    j->dd.Textures = nullptr;
    for (ImDrawList* l : dd->CmdLists) j->dd.CmdLists.push_back(l->CloneOutput());
    j->dd.CmdListsCount = j->dd.CmdLists.Size;
    j->buf = g_front ^ 1;
    content_box(dd, j->box);
    if (!g_worker_started) {
        g_worker_started = true;
        rt_thread_start(raster_thread, nullptr);
    }
    g_busy = true;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_job = j;
    }
    g_cv.notify_one();
}

void feed(ImGuiKey k, unsigned held, unsigned changed, unsigned bit) {
    if (changed & bit) ImGui::GetIO().AddKeyEvent(k, (held & bit) != 0);
}

}  // namespace

extern "C" {

void overlay_set_fps(float fps) { g_fps10.store((int)(fps * 10.0f + 0.5f)); }

void overlay_set_banner(const char* text) {
    std::lock_guard<std::mutex> l(g_banner_lock);
    snprintf(g_banner_next, sizeof g_banner_next, "%s", text ? text : "");
    g_banner_seq.fetch_add(1);
}

int overlay_take_controllers_request(void) {
    int r = g_pads_request;
    g_pads_request = 0;
    return r;
}

void overlay_request_screenshot(void) { g_shot_request = 1; }  /* SR_SHOTSTEP (frame_pacing.c) */

int overlay_take_screenshot_request(void) {
    int r = g_shot_request;
    g_shot_request = 0;
    return r;
}

int overlay_take_quit_request(void) {
    int r = g_quit_request;
    g_quit_request = 0;
    return r;
}

int overlay_input(unsigned held) {
    static int tested;
    if (!tested++ && getenv("SR_TEST_MENU")) {  /* debugging: start with the menu open (=N: on tab N) */
        g_open = g_just_opened = true;
        g_select_tab = atoi(getenv("SR_TEST_MENU"));
    }
    unsigned pressed = held & ~g_prev;
    unsigned changed = held ^ g_prev;
    g_prev = held;
    if (!g_open) {
        if (g_gate) {  /* closed, but buttons still held from the menu: kept from the game */
            if (held) return 1;
            g_gate = false;
        }
        if (held & OV_MENU) {
            uint64_t now = rt_ticks();
            if (!g_menu_since) g_menu_since = now;
            if (now - g_menu_since >= kHoldTicks) {
                g_open = g_just_opened = true;
                g_menu_since = 0;
                g_prev = held | OV_MENU;  /* the held Minus does not close it again */
                if (g_ready) {
                    /* start from a clean input state */
                    ImGuiIO& io = ImGui::GetIO();
                    io.ClearInputKeys();
                }
                return 1;
            }
        } else {
            g_menu_since = 0;
        }
        return 0;
    }
    init();
    if ((pressed & OV_MENU) && !g_quit_popup) {
        close_menu();
        return 1;
    }
    if (pressed & OV_BACK) {
        if (g_quit_popup) g_quit_popup = false;  /* the dialog closes itself next frame */
        else if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            close_menu();
            return 1;
        }
    }
    if (!g_quit_popup) {
        if (pressed & OV_TAB_L) g_select_tab = (g_tab + 5) % 6;
        if (pressed & OV_TAB_R) g_select_tab = (g_tab + 1) % 6;
    }
    feed(ImGuiKey_GamepadDpadUp, held, changed, OV_UP);
    feed(ImGuiKey_GamepadDpadDown, held, changed, OV_DOWN);
    feed(ImGuiKey_GamepadDpadLeft, held, changed, OV_LEFT);
    feed(ImGuiKey_GamepadDpadRight, held, changed, OV_RIGHT);
    feed(ImGuiKey_GamepadFaceDown, held, changed, OV_ACCEPT);  /* ImGui's "activate" */
    return 1;
}

const uint8_t* overlay_image(unsigned* version, int box[4]) {
    int fps10 = g_settings.show_fps ? g_fps10.load() : -1;
    bool fps_changed = fps10 != g_shown_fps10;
    g_shown_fps10 = fps10;
    unsigned bseq = g_banner_seq.load();
    bool banner_changed = bseq != g_shown_banner_seq;
    if (banner_changed) {
        std::lock_guard<std::mutex> l(g_banner_lock);
        memcpy(g_banner, g_banner_next, sizeof g_banner);
        g_shown_banner_seq = bseq;
    }
    static bool was_open;
    bool shown = g_open || g_settings.show_fps || g_banner[0];
    /* the menu runs every frame while open (navigation, tooltips); the counter only when it changes */
    collect();
    /* a picture left waiting because the helper was busy is made again too */
    if (g_open || fps_changed || banner_changed || was_open != g_open || g_retry) frame();
    was_open = g_open;
    *version = g_version;
    if (box) memcpy(box, g_box[g_front], sizeof g_box[g_front]);
    return shown && g_had_image ? g_img[g_front] : nullptr;
}

}  // extern "C"
