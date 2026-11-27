/* FPS drop reports (drop_report.c): when a race slows down, the last seconds are written to a small text file a
 * player can send (F1 > Bug reports > Share the last FPS drop report). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DropFrame {
    float total;   /* ms from the previous presented frame */
    float game;    /* the game's own code (guest CPU) */
    float idle;    /* the guest waiting (timing, VI) */
    float draws;   /* decoding GX draws */
    float tex;     /* finding / decoding / uploading textures */
    float wait;    /* waiting for the render thread (GPU, new shaders) */
    float disc;    /* reading the disc image */
    unsigned short ndraws, newtex;
} DropFrame;

/* every presented game frame (gx_render.c, game thread) */
void drop_report_frame(const DropFrame* f, int in_race, int paused);
/* the renderer names the GPU once (render_vk.c, render_gl.c, render_deko.c) */
void drop_report_set_gpu(const char* name);
/* the newest report this session ("" when none) and how many were saved */
const char* drop_report_last(void);
int drop_report_count(void);
/* the folder the reports go to */
#define DROP_REPORT_DIR "fps-drop-reports"

#ifdef __cplusplus
}
#endif
