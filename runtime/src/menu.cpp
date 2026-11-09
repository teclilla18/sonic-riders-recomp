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
/* phones (Android, iOS): always full screen, touch controls, no PC picture options */
#if defined(__ANDROID__) || defined(SR_IOS)
#define SR_PHONE 1
#endif
#if !defined(__SWITCH__) && !defined(SR_PHONE)
#define HAVE_BUG_REPORT 1
int platform_bug_report(char* name, size_t n);  /* platform_host.c */
void platform_open_game_folder(void);
#endif
#ifdef _WIN32
const char* update_available(void);  /* update_check.c: the newer release's tag, or NULL */
int SDL_OpenURL(const char* url);
#define UPDATE_PAGE "https://github.com/teclilla18/sonic-riders-recomp/releases/latest"
#endif
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
/* overlay_set_banner: a message across the top (online play: desync, connection lost).
 * The mutexes here are never destroyed: threads still use them while exit() runs the static destructors,
 * and bionic aborts on a destroyed mutex (Android crash report at app exit, 2026-11-07). */
std::mutex& g_banner_lock = *new std::mutex;
char g_banner_next[256], g_banner[256];
std::atomic<unsigned> g_banner_seq{0};
unsigned g_shown_banner_seq;

/* the helper thread's job: a copy of one frame's draw lists */
struct Job {
    ImDrawData dd;
    int buf;
    int box[4];
    double ms;
    bool open;  /* the picture shows the settings menu */
};
bool g_front_open;  /* g_front's picture shows the settings menu */
std::mutex& g_mx = *new std::mutex;
/* never destroyed: the helper thread waits on it for the whole run, and glibc's destructor waits for
 * the waiters, so exit() (window closed, perf test done) hung forever on Linux */
std::condition_variable& g_cv = *new std::condition_variable;
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
    static const int modes[3] = {SCREEN_4_3, SCREEN_WIDE, SCREEN_FIT};
    static const char* const tips[3] = {
        "The original 4:3 picture with black bars at the sides.",
        "The cameras see a wider 16:9 view; the picture is drawn wider, not stretched.",
        "The cameras see as wide as your window or screen (from 4:3 up to 32:9: 16:10, 21:9 ultrawide, 32:9), "
        "drawn wider, not stretched. The HUD moves to the edges of the screen."};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        if (radio(settings_screen_name(modes[i]), g_settings.screen == modes[i], tips[i])) {
            g_settings.screen = modes[i];
            settings_apply();
        }
    }
    if (settings_wide_k_fixed())
        note("Online (or a recording): the picture's shape is fixed for this session so every player's game "
             "matches; changes here apply to how it is shown, and fully after the session.");
    if (g_settings.screen == SCREEN_WIDE) {
        if (check("Stretch the HUD and menus to the width", &g_settings.hud_stretch,
                  "Off: the HUD and menus keep their original 4:3 shape in the middle of the screen. On: they are "
                  "stretched across the whole width like the 16:9 mode of older versions."))
            settings_apply();
    }
    heading("Field of view");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::SliderInt("##fov", &g_settings.fov, FOV_MIN, FOV_MAX, "%d%%");
    help("How much the cameras see; 100% is the game's own view. Higher shows more around you.");
    if (ImGui::IsItemDeactivatedAfterEdit()) settings_apply();
    ImGui::SameLine();
    if (ImGui::Button("Reset") && g_settings.fov != 100) {
        g_settings.fov = 100;
        settings_apply();
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
#elif !defined(SR_PHONE)  /* phones are always full screen */
    heading("Resolution");
    {
        char fmt[48];
        snprintf(fmt, sizeof fmt, "%%dx  (%dx%d)", 640 * g_settings.pc_scale, 528 * g_settings.pc_scale);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        ImGui::SliderInt("##pcscale", &g_settings.pc_scale, PC_SCALE_MIN, PC_SCALE_MAX, fmt);
        help("The size the game is drawn at, in times the GameCube's 640x528. 2x was the only choice before. "
             "Higher is sharper and needs a stronger graphics card; 3x suits 1440p, 4x suits 4K (the most).");
        if (ImGui::IsItemDeactivatedAfterEdit()) settings_apply();
    }
    heading("Anti-aliasing");
    for (int i = 0; i < 4; i++) {
        static const int samples[4] = {1, 2, 4, 8};
        static const char* const names[4] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA"};
        if (i) ImGui::SameLine();
        if (radio(names[i], g_settings.msaa == samples[i],
                  i ? "Smooths jagged edges of 3D models; more samples are smoother and cost more."
                    : "No smoothing (the game's own look).")) {
            g_settings.msaa = samples[i];
            settings_apply();
        }
    }
    /* bloom and colour grading (render_gl.c post_process): the 3D picture only, the HUD stays as drawn */
    auto slider = [](const char* id, int* v, int lo, int hi, const char* fmt, const char* tip) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        ImGui::SliderInt(id, v, lo, hi, fmt);
        help(tip);
        if (ImGui::IsItemDeactivatedAfterEdit()) settings_apply();
    };
    heading("Sharpness (AMD FSR 1)");
    slider("##sharp", &g_settings.sharpness, 0, 100, g_settings.sharpness ? "%d%%" : "Off",
           "Sharpens the final picture (FSR 1 RCAS). When the window is bigger than the resolution above, FSR also "
           "does the upscaling (EASU) instead of a plain stretch. Off = the picture as before.");
    heading("Bloom");
    slider("##bloom", &g_settings.bloom, 0, 100, g_settings.bloom ? "%d%%" : "Off",
           "A soft glow around bright parts of the picture (sky, lights, boost). The HUD does not glow.");
    heading("Colour");
    {
        struct Preset { const char* name; int bright, contrast, sat, warm; const char* tip; };
        static const Preset presets[] = {
            /* the user found the first set barely noticeable (2026-11-07): about twice as strong */
            {"Original", 0, 100, 100, 0, "The game's own colours."},
            {"Vivid", 0, 118, 160, 0, "Much stronger colours and more contrast."},
            {"Warm", 8, 110, 120, 45, "Sunnier, red-orange."},
            {"Cool", 0, 112, 110, -45, "Crisper, blue."},
        };
        ImGui::PushID("grade");  /* "Original" is also a Character lighting choice: separate IDs */
        for (int i = 0; i < 4; i++) {
            const Preset& p = presets[i];
            int on = g_settings.grade_bright == p.bright && g_settings.grade_contrast == p.contrast &&
                     g_settings.grade_sat == p.sat && g_settings.grade_warm == p.warm;
            if (i) ImGui::SameLine();
            if (radio(p.name, on, p.tip)) {
                g_settings.grade_bright = p.bright, g_settings.grade_contrast = p.contrast;
                g_settings.grade_sat = p.sat, g_settings.grade_warm = p.warm;
                settings_apply();
            }
        }
        ImGui::PopID();
    }
    slider("##gbright", &g_settings.grade_bright, -50, 50, "Brightness %+d", "Lighter or darker 3D picture.");
    slider("##gcontrast", &g_settings.grade_contrast, 50, 150, "Contrast %d%%", "Difference between dark and light.");
    slider("##gsat", &g_settings.grade_sat, 0, 200, "Saturation %d%%", "Colour strength; 0% is black and white.");
    slider("##gwarm", &g_settings.grade_warm, -50, 50, "Warmth %+d", "Bluer (left) or redder (right).");
    heading("Character lighting");
    if (radio("Original##light", !g_settings.char_light, "Lighting worked out at the corners of each triangle, as on the GameCube.")) {
        g_settings.char_light = 0;
        settings_apply();
    }
    ImGui::SameLine();
    if (radio("Smooth (per pixel)##light", g_settings.char_light != 0,
              "Lighting worked out for every pixel: smoother shading and highlights on characters and boards.")) {
        g_settings.char_light = 1;
        settings_apply();
    }
    heading("Window");
    if (check("Fullscreen  (F11 / Alt+Enter)", &g_settings.fullscreen, "Borderless fullscreen on the current display."))
        settings_apply();
#endif
}

#if !defined(__SWITCH__) && !defined(SR_PHONE)
/* PC: frame rate presets. 30 = the game's 30 fps mode, 60 = the game's own pacing, the others draw in-between
 * pictures (render_gl.c; the game itself always runs 60 steps a second) */
static void pc_frame_rate() {
    int cur = g_settings.framerate == FR_30 ? 30 : g_settings.pc_fps == 0 ? 60 : g_settings.pc_fps;
    for (int i = 0; settings_pc_fps_presets[i]; i++) {
        int v = settings_pc_fps_presets[i];
        char label[32];
        if (v == PC_FPS_UNLIMITED) snprintf(label, sizeof label, "Unlimited");
        else snprintf(label, sizeof label, "%d fps", v);
        const char* tip = v == 30   ? "Sonic Riders' own 30 fps mode, at full speed."
                          : v == 60 ? "The game's own frame rate, as on the GameCube."
                          : v == PC_FPS_UNLIMITED
                              ? "As many pictures as the PC can draw, without vsync (the picture may tear)."
                              : "The game still runs at 60 steps a second; the PC draws extra pictures in between "
                                "for smoother motion. Vsync is used when the display runs at this rate.";
        if (i % 4) ImGui::SameLine(0, 18);
        if (radio(label, cur == v, tip)) {
            if (v == 30) {
                g_settings.framerate = FR_30;
                g_settings.pc_fps = 0;
            } else {
                if (g_settings.framerate == FR_30) g_settings.framerate = FR_AUTO;
                g_settings.pc_fps = v == 60 ? 0 : v;
            }
            settings_apply();
        }
    }
    note("Above 60 fps the PC draws extra pictures between the game's own; a display that refreshes at least that "
         "fast shows them. The game's speed and timing do not change. If the counter stays below the choice, lower the "
         "resolution or anti-aliasing (Display).");
}
#endif

void tab_performance() {
    heading("Frame rate");
#if !defined(__SWITCH__) && !defined(SR_PHONE)
    pc_frame_rate();
#else
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
#endif
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
#elif defined(SR_PHONE)
    heading("Touch screen");
    note("Left half: the control stick (it appears where your thumb lands). Right side: JUMP (A, hold to charge), "
         "BOOST (B), DRIFT (R, hold), TORNADO (L + R). Top right: START (pause). Top left: this menu. "
         "Slide your thumb between buttons without lifting it.");
    heading("Controllers");
    note("Any Bluetooth or USB controller works (Xbox, PlayStation, Switch Pro and others): it is player 1, more "
         "controllers are players 2 to 4. Using one hides the touch controls; touch the screen to bring them back.");
    note("A B X Y = A B X Y,  Start = START,  left stick = control stick,  right stick = C-stick,  "
         "triggers = L / R (pressed fully they click, as on a GameCube),  right shoulder = Z,  D-pad = D-pad. "
         "Hold Back (View / Select / Share) or press the phone's Back to open this menu.");
#else
    heading("Buttons");
    if (ImGui::BeginTable("map", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("GameCube");
        ImGui::TableSetupColumn("Keyboard");
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        const char *gc, *key, *pad;
        for (int i = 0; input_map_row(i, &gc, &key, &pad); i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(gc);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(key);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(pad);
        }
        ImGui::EndTable();
    }
    note("F1 held = this menu (Q / E switch its tabs), F11 = fullscreen. Controllers: Xbox and others; more "
         "controllers are players 2 to 4 (the controller column is for all of them). Hold Back (View) to open "
         "this menu.");
    note("Change the buttons in the launcher (SonicRidersLauncher.exe, Controls page); they apply from the "
         "next start.");
    heading("Background");
    if (check("Controllers work in the background", &g_settings.pad_background,
              "Controllers keep playing while another window has focus (a second screen, a chat window). "
              "Turn off to ignore them then."))
        settings_apply();
#endif
}

void tab_gameplay() {
    heading("Unlocks");
    if (check("Chaos Emerald gear in the Shop for 0 rings", &g_settings.chaos_emerald,
              "Off by default. When on, the Chaos Emerald gear (Super Sonic) shows up in the Shop for 0 rings: "
              "buy it there once and it is yours, also in that save. Nothing else is "
              "unlocked."))
        settings_apply();
    note("Buy it in the Shop, then pick it for Sonic in Player Entry.");
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
#ifdef _WIN32
    if (const char* latest = update_available()) {
        char line[96];
        snprintf(line, sizeof line, "Update available: %s", latest);
        warn(line);
        if (ImGui::Button("Open the download page")) SDL_OpenURL(UPDATE_PAGE);
        help("Opens the project's newest release on GitHub in your browser.");
    }
    bool check = g_settings.update_check != 0;
    if (ImGui::Checkbox("Check for updates at start-up", &check)) {
        g_settings.update_check = check;
        settings_apply();
    }
    help("Asks GitHub once at start-up whether a newer version of the port exists and says so at the top of the "
         "screen. Only the newest version number is downloaded.");
#endif
    note("A fan-made native port built from your own disc. Not affiliated with SEGA or Nintendo.");
    heading("Bug reports");
    if (ImGui::Button("Save screenshot")) {
        close_menu();
        g_shot_request = 1;
    }
    help("Saves the next picture next to the game's log, for bug reports.");
#ifdef HAVE_BUG_REPORT
    static char report[64];
    static int report_failed;
    ImGui::SameLine();
    if (ImGui::Button("Save a bug report")) {
        report_failed = platform_bug_report(report, sizeof report) != 0;
        if (!report_failed) platform_open_game_folder();
    }
    help("Puts the game's log, the setup log, your settings and system information into one .zip in the game's "
         "folder and opens that folder. Attach the .zip (and screenshots) to your report on GitHub. It holds "
         "folder names from your PC (in the logs): look through it first if that matters to you.");
    if (report_failed) note("Could not write the bug report in the game's folder.");
    else if (report[0]) {
        char line[160];
        snprintf(line, sizeof line, "Saved %s in the game's folder (the folder is open now).", report);
        note(line);
    }
#endif
}

void quit_row() {
    const float w_close = ImGui::CalcTextSize("Close").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::PushStyleColor(ImGuiCol_Text, kNote);
#ifdef SR_PHONE
    ImGui::TextUnformatted("Tap to choose.   Controller: LB / RB tabs, A choose, B back");
#else
    ImGui::TextUnformatted("L / R: tabs    A: choose    B: back");
#endif
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
    g_front_open = j->open;
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
    j->open = g_open;
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

/* Switch and Android draw the overlay from the helper thread's last finished picture, which can still be the
 * menu's for a frame or two after "Save screenshot" closed it (seen with the FPS counter on: shot_1756, v0.5.3
 * OLED). The request waits until the shown picture is one drawn with the menu closed. */
int overlay_take_screenshot_request(void) {
    if (g_shot_request && (g_open || g_front_open)) return 0;
    int r = g_shot_request;
    g_shot_request = 0;
    return r;
}

int overlay_menu_open(void) { return g_open; }

/* a touch control's menu button: open or close at once (no hold) */
void overlay_menu_toggle(void) {
    if (g_open) {
        close_menu();
        return;
    }
    g_open = g_just_opened = true;
    if (g_ready) ImGui::GetIO().ClearInputKeys();
}

/* a finger on the open menu, in overlay pixels (OVERLAY_W x OVERLAY_H); down = 0 when lifted */
void overlay_pointer(float x, float y, int down) {
    init();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, down != 0);
    if (!down) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  /* no hover left behind after the finger lifts */
}

/* the PC mouse (render_gl.c, the render thread, like the menu itself): point, click, drag sliders,
 * scroll; events while the menu is closed are dropped, except releases (no button left held) */
void overlay_mouse_pos(float x, float y) {
    if (!g_open) return;
    init();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(x, y);
}

void overlay_mouse_button(int button, int down) {
    if ((!g_open && down) || button < 0 || button > 2) return;
    init();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseButtonEvent(button, down != 0);
}

void overlay_mouse_wheel(float dx, float dy) {
    if (!g_open) return;
    init();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseWheelEvent(dx, dy);
}

void overlay_request_quit(void) { g_quit_request = 1; }

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
    bool open_before = g_open;
    if (g_open || fps_changed || banner_changed || was_open != g_open || g_retry) frame();
    /* a touch on Close (or Quit) closes the menu inside frame(): that picture still shows the menu,
     * so the next call draws again without it */
    was_open = open_before;
    *version = g_version;
    if (box) memcpy(box, g_box[g_front], sizeof g_box[g_front]);
    return shown && g_had_image ? g_img[g_front] : nullptr;
}

}  // extern "C"
