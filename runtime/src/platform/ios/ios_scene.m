/* iOS: put SDL2's window on a UIWindowScene.
 *
 * iOS 27 stops an app at launch (EXC_BREAKPOINT in UIKit) unless it uses the scene life cycle. SDL2 predates
 * scenes and makes a plain UIWindow, which stays invisible without a scene. Info.plist declares one window
 * scene (tools/ios_build.py); render_vk.c calls ios_attach_window() after SDL_CreateWindow, on the main thread. */
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "runtime.h"

static UIWindowScene* find_scene(void) {
    for (UIScene* s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:[UIWindowScene class]]) return (UIWindowScene*)s;
    return nil;
}

void ios_attach_window(SDL_Window* win) {
    @autoreleasepool {
        /* the scene connects through the main run loop, possibly after SDL_main started: wait up to 5 s */
        UIWindowScene* scene = find_scene();
        for (int i = 0; !scene && i < 500; i++) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
            scene = find_scene();
        }
        if (!scene) rt_fatal("iOS: no window scene connected");
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(win, &info)) rt_fatal("SDL_GetWindowWMInfo: %s", SDL_GetError());
        UIWindow* w = info.info.uikit.window;
        w.windowScene = scene;
        w.frame = scene.coordinateSpace.bounds;
        [w makeKeyAndVisible];
        rt_log("[ios] window on scene: %.0f x %.0f points, scale %.1f\n", w.bounds.size.width, w.bounds.size.height,
               (double)w.screen.nativeScale);
    }
}

/* the app's bundle ID (About tab: one ID per player, tools/ios_run.py) */
const char* ios_bundle_id(void) {
    static char id[160];
    if (!id[0]) {
        @autoreleasepool {
            NSString* s = NSBundle.mainBundle.bundleIdentifier;
            snprintf(id, sizeof id, "%s", s ? s.UTF8String : "?");
        }
    }
    return id;
}
