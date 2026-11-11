#!/usr/bin/env python3
"""Build-time shader embedding for the shared GX ubershader (runtime/shaders/).

  embed_shaders.py gl  OUT.h                 -> C strings for the OpenGL 3.3 backend
  embed_shaders.py dksh OUT.h UAM WORKDIR    -> deko3d shader binaries (compiled with uam) as C arrays

The GLSL uses a few macros for what differs between the APIs (explicit locations and bindings are
required by deko3d, not available in GL 3.3 core):
  ATTR(n) vertex input location, VARY(n) varying location, SAMPLER(n) sampler binding,
  BIND_UBO / BIND_UBO1 the uniform block bindings 0 (registers) and 1 (XF chunk table).
  XFM_WORD(i) word i of the XF memory chunk pool (declared by the prelude: an SSBO / a buffer texture).
  CLIP_Y the sign applied to clip-space y on top of the GX viewport's own: GL's clip +y is the
  bottom row (flip, so the EFB is stored top row first), deko3d 0.5's upper-left origin puts clip
  +y on the top row already (no flip).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADERS = os.path.join(os.path.dirname(HERE), "runtime", "shaders")

PRELUDE_GL = "#define ATTR(n)\n#define VARY(n)\n#define SAMPLER(n)\n#define BIND_UBO\n#define BIND_UBO1\n#define CLIP_Y 1.0\nuniform usamplerBuffer u_xfm;\n#define XFM_WORD(i) texelFetch(u_xfm, (i)).r\nuniform int u_show_alpha;\n#define DEBUG_SHOW_ALPHA u_show_alpha\nuniform int u_tev_debug;\n#define DEBUG_TEV u_tev_debug\n"
# SR_DK_DEBUG_TEV=<n> at build time: a deko3d build with that SR_TEVDEBUG view fixed on (hardware tests)
PRELUDE_DK = ("#version 460\n#define ATTR(n) layout(location = n)\n#define VARY(n) layout(location = n)\n"
              "#define SAMPLER(n) layout(binding = n)\n#define BIND_UBO , binding = 0\n#define BIND_UBO1 , binding = 1\n#define CLIP_Y (-1.0)\n#define DEBUG_SHOW_ALPHA 0\n#define DEBUG_TEV " + os.environ.get("SR_DK_DEBUG_TEV", "0") + "\nlayout(std430, binding = 0) readonly buffer XFM { uint xfm_pool[]; };\n#define XFM_WORD(i) xfm_pool[(i)]\n")


# (output id, source name, stage, extra defines): the GX ubershader (with gx_common.glsl) and the
# overlay (menu / FPS counter). deko3d gets the fragment ubershader twice: indirect texturing raises
# its register count (56 -> 76 GPRs, fewer pixels in flight), so only draws using it get that variant.
# PERPIXEL_LIGHT: the PC build's per-pixel lighting option (gx_light.glsl in the fragment shader too)
PROGRAMS_GL = (("gx_vert", "gx", "vert", "#define PERPIXEL_LIGHT 1\n"),
               ("gx_frag", "gx", "frag", "#define GX_INDIRECT 1\n#define PERPIXEL_LIGHT 1\n"),
               ("overlay_vert", "overlay", "vert", ""), ("overlay_frag", "overlay", "frag", ""))
# The deko3d vertex shader reads XF memory from a per-state uniform buffer (XF_UBO); gx_vert_ssbo is the
# old storage-buffer form, kept for the GPU cost test (SR_GPU_AB) to compare against.
PROGRAMS_DK = (("gx_vert", "gx", "vert", "#define XF_UBO 1\n"), ("gx_frag", "gx", "frag", "#define GX_INDIRECT 0\n"),
               ("gx_frag_ind", "gx", "frag", "#define GX_INDIRECT 1\n"),
               ("gx_vert_ssbo", "gx", "vert", "#define BIND_UBO_XT , binding = 2\n"),  # GPU cost test (SR_GPU_AB)
               ("overlay_vert", "overlay", "vert", ""), ("overlay_frag", "overlay", "frag", ""))


# Vulkan (platform/android/render_vk.c): set 0 = the uniform buffers (registers, XF memory; dynamic offsets),
# set 1 = the 8 textures. Clip depth 0..w (DEPTH_ZERO_ONE in gx.vert). Vulkan's clip -y is framebuffer row 0,
# so CLIP_Y is +1 as for GL (EFB stored top row first); deko3d's -1 drew the picture upside down and turned
# face culling around (2026-11-03). The vk_* programs are the backend's own screen quads (no prelude, no gx_common).
PRELUDE_VK = ("#version 460\n#define ATTR(n) layout(location = n)\n#define VARY(n) layout(location = n)\n"
              "#define SAMPLER(n) layout(set = 1, binding = n)\n#define BIND_UBO , set = 0, binding = 0\n"
              "#define BIND_UBO1 , set = 0, binding = 1\n#define CLIP_Y 1.0\n#define DEBUG_SHOW_ALPHA 0\n#define DEBUG_TEV 0\n"
              "#define DEPTH_ZERO_ONE 1\n#define XFM_WORD(i) 0u\n#define gl_VertexID gl_VertexIndex\n")
PROGRAMS_VK = (("gx_vert", "gx", "vert", "#define XF_UBO 1\n"), ("gx_frag", "gx", "frag", "#define GX_INDIRECT 0\n"),
               ("gx_frag_ind", "gx", "frag", "#define GX_INDIRECT 1\n"),
               ("vk_quad_vert", "vk_quad", "vert", None), ("vk_tex_frag", "vk_tex", "frag", None),
               ("vk_fill_frag", "vk_fill", "frag", None), ("vk_touch_frag", "vk_touch", "frag", None),
               # picture options: bloom, colour grading, FSR 1 (docs/notes/picture-options-port.md)
               ("vk_bright_frag", "vk_bright", "frag", None), ("vk_blur_frag", "vk_blur", "frag", None),
               ("vk_grade_frag", "vk_grade", "frag", None), ("vk_easu_frag", "vk_easu", "frag", None),
               ("vk_rcas_frag", "vk_rcas", "frag", None))


def spv_main(out, glslc, work):
    """embed_shaders.py spv OUT.h GLSLC WORKDIR: SPIR-V (Vulkan 1.1) as uint32 arrays"""
    os.makedirs(work, exist_ok=True)
    text = "/* generated by tools/embed_shaders.py: SPIR-V of runtime/shaders (glslc) */\n#pragma once\n#include <stdint.h>\n"
    for out_id, name, stage, defines in PROGRAMS_VK:
        if defines is None:
            src_text = "#version 460\n" + open(os.path.join(SHADERS, name + "." + stage)).read()
        else:
            src_text = source(name, stage, PRELUDE_VK, defines)
        glsl = os.path.join(work, out_id + ".glsl")
        spv = os.path.join(work, out_id + ".spv")
        open(glsl, "w").write(src_text)
        r = subprocess.run([glslc, "-fshader-stage=" + stage, "--target-env=vulkan1.1", "-O", "-o", spv, glsl],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("glslc failed for %s:\n%s%s" % (glsl, r.stdout, r.stderr))
        data = open(spv, "rb").read()
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
        text += "static const uint32_t k_%s_spv[%d] = {\n" % (out_id, len(words))
        for i in range(0, len(words), 8):
            text += "    " + ", ".join("0x%08X" % w for w in words[i:i + 8]) + ",\n"
        text += "};\n"
    write_if_changed(out, text)


def source(name, stage, prelude, defines):
    common = open(os.path.join(SHADERS, "gx_common.glsl")).read() + "\n" if name == "gx" else ""
    body = open(os.path.join(SHADERS, name + "." + stage)).read()
    if name == "gx":  # the lighting functions: always in the vertex shader, in the fragment one for PERPIXEL_LIGHT
        light = open(os.path.join(SHADERS, "gx_light.glsl")).read()
        light = light[light.index("/*"):]  # without its own header comment (sources stay as they were)
        body = body.replace("//@gx_light\n", light if stage == "vert" or "PERPIXEL_LIGHT" in defines else "")
    return prelude + defines + common + body


def c_string(text):
    lines = []
    for line in text.splitlines(True):
        lines.append('"' + line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"')
    return "\n".join(lines)


def write_if_changed(path, text):
    if os.path.exists(path) and open(path).read() == text:
        return
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    open(path, "w").write(text)


def main():
    mode, out = sys.argv[1], sys.argv[2]
    if mode == "gl":
        text = "/* generated by tools/embed_shaders.py from runtime/shaders */\n#pragma once\n"
        for out_id, name, stage, defines in PROGRAMS_GL:
            text += "static const char* const k_%s_glsl =\n%s;\n" % (out_id, c_string(source(name, stage, PRELUDE_GL, defines)))
        write_if_changed(out, text)
        return
    if mode == "spv":
        spv_main(out, sys.argv[3], sys.argv[4])
        return
    uam, work = sys.argv[3], sys.argv[4]
    os.makedirs(work, exist_ok=True)
    text = "/* generated by tools/embed_shaders.py: deko3d binaries of runtime/shaders (uam) */\n#pragma once\n#include <stdint.h>\n"
    for out_id, name, stage, defines in PROGRAMS_DK:
        glsl = os.path.join(work, out_id + ".glsl")
        dksh = os.path.join(work, out_id + ".dksh")
        open(glsl, "w").write(source(name, stage, PRELUDE_DK, defines))
        r = subprocess.run([uam, "-s", stage, "-o", dksh, glsl], capture_output=True, text=True)
        if r.returncode:
            sys.exit("uam failed for %s:\n%s%s" % (glsl, r.stdout, r.stderr))
        data = open(dksh, "rb").read()
        text += "static const uint8_t k_%s_dksh[%d] __attribute__((aligned(256))) = {\n" % (out_id, len(data))
        for i in range(0, len(data), 16):
            text += "    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",\n"
        text += "};\n"
    write_if_changed(out, text)


if __name__ == "__main__":
    main()
