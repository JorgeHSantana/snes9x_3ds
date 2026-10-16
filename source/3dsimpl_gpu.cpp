
#include "stereo_shadow.h"
#include "stereo_glow.h"
#include "screen_grade.h"
bool S9xLayerUsedLastFrame(int layer, int prio);   // gfxhw.cpp: the rows the last frame drew
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"
#include "3dsmode7persp.h"
#include "3dslog.h"

SGPU3DSExtended GPU3DSExt;

void gpu3dsDeallocLayers()
{   
    SLayerList *list = &GPU3DSExt.layerList;

    if (list == nullptr)
        return;

    linearFree(list->sections);
    linearFree(list->ibo);
}

void gpu3dsResetLayer(SLayer *layer) {
    layer->sectionsByTarget[TARGET_SNES_MAIN] = 0;
    layer->verticesByTarget[TARGET_SNES_MAIN] = 0;

    layer->sectionsByTarget[TARGET_SNES_SUB] = 0;
    layer->verticesByTarget[TARGET_SNES_SUB] = 0;
    
    layer->sectionsTotal = 0;
    layer->m7Tile0 = false;
}

void gpu3dsResetLayers(SLayerList *list) {
    list->verticesTotal = 0;
            
    for (int i = 0; i < LAYERS_COUNT; i++) {
        gpu3dsResetLayer(&list->layers[i]);
    }
}

// reset to initial state when loading a game
void gpu3dsResetLayerSectionLimits(SLayerList *list) {
    list->sectionsMax = 0;

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        SLayer *layer = &list->layers[i];

        layer->sectionsOffset = list->sectionsMax;

        switch (i) {
            case LAYER_WINDOW_LR:
            case LAYER_BRIGHTNESS:
                layer->sectionsMax = 1; // always 0-1 section
                break;
            case LAYER_BACKDROP:
                layer->sectionsMax = 2; // always 0-2 sections
                break;
            default:
                layer->sectionsMax = 128; 
                break;
        }

        list->sectionsMax += layer->sectionsMax;
    }
}

void gpu3dsAdjustLayerSectionLimits(SLayerList *list) {
    u16 threshold = 16;
    u16 allowedSectionsMax = list->sectionsSizeInBytes / sizeof(SLayerSection);
    u16 newSectionsMax = 0;

    for (int i = 0; i < LAYERS_COUNT; i++) {
        SLayer *layer = &list->layers[i];

        layer->sectionsOffset = newSectionsMax;

        if (layer->sectionsSkipped) {
            layer->sectionsMax += (layer->sectionsSkipped < threshold ? threshold : layer->sectionsSkipped);
        }
        
        newSectionsMax += layer->sectionsMax;
    }

    if (newSectionsMax <= allowedSectionsMax) {
        list->sectionsMax = newSectionsMax;

        return;
    }

    // if newSectionsMax > allowedSectionsMax, we have to reduce layer->sectionsMax for !layer->sectionsSkipped layers
    // so that newSectionsMax still fits into the allocated memory
    // (e.g. color math layer has sectionsMax = 128 but only 7 sections are needed for the current frame)
    //
    // for convinience we only do this for color math,obj and bg0-bg3 in prioritized order
    // because those layers will have the highest number of unused sections
    u16 sectionsToReduce = newSectionsMax - allowedSectionsMax;
    u16 reducedSections = 0;

    LAYER_ID order[6] = {
        LAYER_COLOR_MATH,
        LAYER_OBJ,
        LAYER_BG3,
        LAYER_BG2,
        LAYER_BG1,
        LAYER_BG0,
    };

    for (int i = 0; i < 6; i++) {
        SLayer *layer = &list->layers[order[i]];

        if (!layer->sectionsSkipped) {
            u16 newMax = layer->sectionsTotal < threshold ? threshold : layer->sectionsTotal;
            reducedSections += layer->sectionsMax - newMax;
            layer->sectionsMax = newMax;
        }

        if (sectionsToReduce <= reducedSections)
            break;
    }

    newSectionsMax = 0;
    for (int i = 0; i < LAYERS_COUNT; i++) {
        SLayer *layer = &list->layers[i];

        // just in case if we still exceed the max limit (should never happen)
        if (newSectionsMax + layer->sectionsMax > allowedSectionsMax) {
            layer->sectionsMax = 0;
        }

        layer->sectionsOffset = newSectionsMax;

        if (layer->sectionsSkipped) {
            layer->sectionsSkipped = false;
        }

        newSectionsMax += layer->sectionsMax;
    }

    list->sectionsMax = newSectionsMax;
}

u64 gpu3dsGetLayerPackedMask(LAYER_ID id, bool firstSection) {
    if (id == LAYER_OBJ)
    {
        return firstSection
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST | PACKED_MASK_TEX_OFFSET
            : PACKED_MASK_STENCIL;
    }

    if (id == LAYER_BG0 || id == LAYER_BG1)
    {
        return PACKED_MASK_TEX_BIND
            | PACKED_MASK_STENCIL
            | PACKED_MASK_ALPHA_TEST
            | PACKED_MASK_TEX_OFFSET;
    }

    if (id == LAYER_BG2 || id == LAYER_BG3)
    {
        return firstSection
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST | PACKED_MASK_TEX_OFFSET
            : PACKED_MASK_STENCIL;
    }

    return 0;
}

void gpu3dsInitLayers() {
    SLayerList *list = &GPU3DSExt.layerList;

    // Sized by index references, not unique vertices.
    // A tile drawn on both sub and main is referenced twice, so worst case is 2x MAX_VERTICES.
    list->sizeInBytes = gpu3dsGetNextPowerOf2(2 * MAX_VERTICES * sizeof(u16));
    list->ibo = linearAlloc(list->sizeInBytes);

    gpu3dsResetLayers(list);

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        LAYER_ID id = LAYER_ID(i);
        SLayer *layer = &list->layers[id];

        layer->id = id;
    }

    list->sectionsSizeInBytes = gpu3dsGetNextPowerOf2(list->sectionsMax * sizeof(SLayerSection));
    list->sections = (SLayerSection *)linearAlloc(list->sectionsSizeInBytes);
			
    log3dsWrite("ibo size: %dkb, sections size: %dkb",
        list->sizeInBytes / 1024,
        list->sectionsSizeInBytes / 1024
    );
}

int compareSections(const SLayerSection *a, const SLayerSection *b, bool tile0) {
    // First, compare target: TARGET_SNES_SUB should come first
    if (a->onSub != b->onSub) {
        return a->onSub ? -1 : 1;
    }
    
    // If targets are equal, compare texture: SNES_MODE7_TILE_0 should come first
    if (tile0 && a->state.textureBind != b->state.textureBind) {
        return (a->state.textureBind == SNES_MODE7_TILE_0) ? -1 : 1;
    }

    return 0;
}

void sortSections(SLayerSection *sections, int n, bool tile0) {
    for (int i = 1; i < n; i++) {
        SLayerSection section = sections[i];
        int j = i - 1;
        while (j >= 0 && compareSections(&section, &sections[j], tile0) < 0) {
            sections[j + 1] = sections[j];
            j--;
        }
        sections[j + 1] = section;
    }
}

// window_lr, backdrop, color math, brightness
void gpu3dsDrawVerticalSectionLayer(SLayer *layer, int from, int to) {
    SLayerList *list = &GPU3DSExt.layerList;

    u64 mask = PACKED_MASK_TEX_ENV
        | PACKED_MASK_STENCIL
        | PACKED_MASK_ALPHA_TEST
        | PACKED_MASK_ALPHA_BLEND;

    if (layer->id == LAYER_COLOR_MATH)
        mask |= PACKED_MASK_TEX_BIND;

    for (int i = from; i < to; i++) {
        SLayerSection *section = &list->sections[i];

        GPU3DS.currentRenderState.packed =
            (GPU3DS.currentRenderState.packed & ~mask) | (section->state.packed & mask);
        gpu3dsDraw(&GPU3DS.vertices[section->vboId], NULL, section->count, section->from);
    }
}

// obj, bg0-bg3 - single section per target.
// Only used when list->useDrawArraysForTiledLayers holds, because mixing
// DrawArrays and DrawElements on the OBJ/BG pass seems to freeze real hardware
// (SMK race).
void gpu3dsDrawTiledLayerSingleSection(SLayer *layer, SLayerSection *section) {
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
    GPU3DS.currentRenderState.alphaBlending =
        GPU3DS.stereoGhostPass ? ALPHA_BLENDING_GHOST : ALPHA_BLENDING_DISABLED;

    u64 mask = gpu3dsGetLayerPackedMask(layer->id, true);
    GPU3DS.currentRenderState.packed =
        (GPU3DS.currentRenderState.packed & ~mask) | (section->state.packed & mask);

    // ghost fragments carry low alpha: the sections' >=0.5 alpha test would
    // discard them, but the test must still kill fully transparent texels
    // (sprites lost their transparency with the test fully off)
    if (GPU3DS.stereoGhostPass)
        GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;

    gpu3dsDraw(&GPU3DS.vertices[section->vboId], NULL, section->count, section->from);
}

// obj, bg0-bg3
void gpu3dsDrawTiledLayer(SLayer *layer, u16 *indices, int from, int to) {
    SLayerList *list = &GPU3DSExt.layerList;
    u16 batchFrom = 0;
    u16 batchCount = 0;

    // Those fields are set once before the loop and stay constant
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
    GPU3DS.currentRenderState.alphaBlending =
        GPU3DS.stereoGhostPass ? ALPHA_BLENDING_GHOST : ALPHA_BLENDING_DISABLED;
    
    // restrict the diff to only the properties that actually vary across sections for that layer type
    u64 layerMask[2];
    layerMask[0] = gpu3dsGetLayerPackedMask(layer->id, true);  // first section
    layerMask[1] = gpu3dsGetLayerPackedMask(layer->id, false); // subsequent sections

    // init from first section
    SLayerSection *first = &list->sections[from];
    SGPU_VBO_ID vboId = first->vboId;
    GPU3DS.currentRenderState.packed =
        (GPU3DS.currentRenderState.packed & ~layerMask[0]) | (first->state.packed & layerMask[0]);

    // ghost fragments carry low alpha: the sections' >=0.5 alpha test would
    // discard them, but the test must still kill fully transparent texels
    // (sprites lost their transparency with the test fully off)
    if (GPU3DS.stereoGhostPass)
        GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;

    for (int idx = from; idx < to; idx++) {
        SLayerSection *section = &list->sections[idx];
        u16 sFrom = section->from;
        u16 sCount = section->count;

        // batch break on state change
        if (idx > from) {
            bool changed = ((GPU3DS.currentRenderState.packed ^ section->state.packed) & layerMask[1])
                || section->vboId != vboId;

            if (changed) {
                gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
                vboId = section->vboId;
                GPU3DS.currentRenderState.packed =
                    (GPU3DS.currentRenderState.packed & ~layerMask[1]) | (section->state.packed & layerMask[1]);
                if (GPU3DS.stereoGhostPass)
                    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;
                batchFrom += batchCount;
                batchCount = 0;
            }
        }

        // build sequential indices
        u16 *dst = indices + batchFrom + batchCount;
        int i = 0;
        for (; i <= sCount - 4; i += 4) {
            dst[i]     = sFrom + i;
            dst[i + 1] = sFrom + i + 1;
            dst[i + 2] = sFrom + i + 2;
            dst[i + 3] = sFrom + i + 3;
        }
        for (; i < sCount; i++) {
            dst[i] = sFrom + i;
        }

        batchCount += sCount;
    }

    // draw final batch
    gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
}

// Atmospheric depth cues (Depth Fade / Depth Haze): texenv stage 2
// interpolates the layer toward a target color by t, where t grows with how
// deep the layer sinks (out = prev*(1-t) + target*t; fade pulls toward
// black, haze toward the fog color, combined in one stage). Stage 2 is
// otherwise unused; stage-0 helpers only ever clear stage 1. t scales with
// the 3D slider, so 2D parity is exact with 3D off.
static u32 s_atmosLastColor;
// >0 while the current layer's blur deserves ghost passes (the ghosts'
// alpha); the offset widens with the blur level so the gauge controls
// both the smear distance and its visibility
static float s_atmosGhostAlpha;
static float s_atmosGhostOffset;

static void gpu3dsResetStereoAtmosphere()
{
    C3D_TexEnvInit(C3D_GetTexEnv(2));
    C3D_TexEnvInit(C3D_GetTexEnv(3));
    s_atmosLastColor = 0xFFFFFFFF;
    s_atmosGhostAlpha = 0.0f;
    s_atmosGhostOffset = 0.0f;
    GPU3DS.stereoGhostPass = false;
}

// Stage 3 modulates the output alpha for the ghost passes (RGB passes
// through); a <= 0 restores passthrough.
static void gpu3dsSetGhostAlpha(float a)
{
    C3D_TexEnv *env = C3D_GetTexEnv(3);
    C3D_TexEnvInit(env);
    if (a <= 0.0f)
        return;
    C3D_TexEnvColor(env, ((u32)(a * 255.0f) << 24) | 0x00FFFFFF);
    // ghost alpha = texture alpha x constant: keeps transparent texels dead
    // while staying immune to the per-scene vertex-alpha class, which
    // color-math scenes set to 0 (that zeroed a PREVIOUS-based modulate)
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
}

// Stage 3 for the drop shadow pass (issue #77): RGB = the constant colour,
// alpha = texture alpha x constant. a <= 0 restores passthrough.
static void gpu3dsSetShadowEnv(float a, uint32_t rgb)
{
    C3D_TexEnv *env = C3D_GetTexEnv(3);
    C3D_TexEnvInit(env);
    if (a <= 0.0f)
        return;
    // C3D_TexEnvColor takes ABGR: swap the table's RGB
    const u32 abgr = ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    C3D_TexEnvColor(env, ((u32)(a * 255.0f) << 24) | abgr);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
}

// Stages 3-5 for the glow pass (issue #77): alpha = the texel's luminance
// (76 R + 150 G + 29 B) / 255, so the alpha test keeps bright texels and
// SRC_ALPHA blending adds more for brighter ones; RGB = texel x tint x
// gain (x2 scale for gains above 1). on = false restores passthrough.
static void gpu3dsSetGlowEnv(bool on, float gain, uint32_t rgb)
{
    C3D_TexEnv *e3 = C3D_GetTexEnv(3), *e4 = C3D_GetTexEnv(4), *e5 = C3D_GetTexEnv(5);
    C3D_TexEnvInit(e3); C3D_TexEnvInit(e4); C3D_TexEnvInit(e5);
    if (!on) return;
    const u32 abgr = ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    const u32 g = (u32)stereo_glow_env_const(gain);
    // stage 3: rgb = tex x (tint x gain); alpha = tex.r x 76/255
    C3D_TexEnvColor(e3, ((u32)SCREEN_GRADE_LUMA_R_OP_WEIGHT << 24) | (((abgr >> 16) & 0xFF) * g / 255u << 16) | (((abgr >> 8) & 0xFF) * g / 255u << 8) | ((abgr & 0xFF) * g / 255u));
    C3D_TexEnvSrc(e3, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(e3, C3D_RGB, GPU_MODULATE);
    if (stereo_glow_env_x2(gain)) C3D_TexEnvScale(e3, C3D_RGB, GPU_TEVSCALE_2);
    C3D_TexEnvSrc(e3, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvOpAlpha(e3, GPU_TEVOP_A_SRC_R, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(e3, C3D_Alpha, GPU_MODULATE);
    // stage 4: alpha += tex.g x 150/255
    C3D_TexEnvColor(e4, (u32)SCREEN_GRADE_LUMA_G_OP_WEIGHT << 24);
    C3D_TexEnvSrc(e4, C3D_RGB, GPU_PREVIOUS);
    C3D_TexEnvFunc(e4, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(e4, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpAlpha(e4, GPU_TEVOP_A_SRC_G, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(e4, C3D_Alpha, GPU_MULTIPLY_ADD);
    // stage 5: alpha += tex.b x 29/255
    C3D_TexEnvColor(e5, (u32)SCREEN_GRADE_LUMA_B_OP_WEIGHT << 24);
    C3D_TexEnvSrc(e5, C3D_RGB, GPU_PREVIOUS);
    C3D_TexEnvFunc(e5, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(e5, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpAlpha(e5, GPU_TEVOP_A_SRC_B, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(e5, C3D_Alpha, GPU_MULTIPLY_ADD);
}

// Vertex scratch for the glow spread's clear rect and composite quads
// (issue #77). The GPU reads vertex data when the frame's command list
// runs, so a quad written into a borrowed slot of the scene VBOs and
// "returned" (count -= 2) is read back as whatever was written there
// last: the black clear rect sharing the depth floor's slot made the
// floor write a non-zero depth on the console (artifacts with Spread
// on). Own buffers, a ring deep enough for both eyes of two frames, and
// their own VBO ids so the attribute-buffer cache rebinds.
struct GlowScratch
{
    SVertexList rect, quad;
    int rectIdx, quadIdx;
    bool ready;
    static const int RING = 64;   // entries of 2 vertices
    void init()
    {
        if (ready) return;
        rect = GPU3DS.vertices[VBO_SCENE_RECT];
        rect.id = (SGPU_VBO_ID)(VBO_COUNT + 1);
        rect.sizeInBytes = RING * 2 * rect.vertexSize;
        rect.data = rect.data_base = linearAlloc(rect.sizeInBytes);
        rect.from = 0; rect.count = 0;
        quad = GPU3DS.vertices[VBO_SCENE_TILE];
        quad.id = (SGPU_VBO_ID)(VBO_COUNT + 2);
        quad.sizeInBytes = RING * 2 * quad.vertexSize;
        quad.data = quad.data_base = linearAlloc(quad.sizeInBytes);
        quad.from = 0; quad.count = 0;
        rectIdx = quadIdx = 0;
        ready = rect.data != nullptr && quad.data != nullptr;
    }
    // a black rect over the whole target; returns the vertex index to draw from
    int addRect(u32 color)
    {
        SRectVertex *v = &((SRectVertex *)rect.data)[rectIdx * 2];
        // y stops at 255: the tile shader keeps the depth plane in the bits
        // above the low byte of y, so 256 decodes as y = 0 on plane 1 and
        // the rect collapses to nothing (the glow target was never cleared:
        // trails on the console, overexposure in Azahar's still scenes)
        v[0].Position = (SVector2i){ 0, 0 }; v[1].Position = (SVector2i){ 512, 255 };
        v[0].Color = v[1].Color = __builtin_bswap32(color);
        const int from = rectIdx * 2; rectIdx = (rectIdx + 1) % RING; return from;
    }
    int addQuad(s16 x0, s16 y0, s16 x1, s16 y1, s16 tx0, s16 ty0, s16 tx1, s16 ty1)
    {
        STileVertex *v = &((STileVertex *)quad.data)[quadIdx * 2];
        v[0].Position = (SVector3i){ x0, y0, 0 }; v[1].Position = (SVector3i){ x1, y1, 0 };
        v[0].TexCoord = (STexCoord2i){ tx0, ty0 }; v[1].TexCoord = (STexCoord2i){ tx1, ty1 };
        const int from = quadIdx * 2; quadIdx = (quadIdx + 1) % RING; return from;
    }
};
static GlowScratch s_glowScratch;

// Stage 3 for the glow spread's composite taps: RGB = texel x gain,
// alpha untouched. gain <= 0 restores passthrough.
static void gpu3dsSetGlowTapEnv(float gain)
{
    C3D_TexEnv *env = C3D_GetTexEnv(3);
    C3D_TexEnvInit(env);
    if (gain <= 0.0f) return;
    const u32 g = (u32)(gain * 255.0f + 0.5f);
    C3D_TexEnvColor(env, 0xFF000000u | (g << 16) | (g << 8) | g);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
}

// 3D-tab editor preview (issue #61): while >= 0, every layer except
// this one is dimmed hard so the edited layer reads instantly.
static int s_previewHighlightLayer = -1;
static int s_previewHighlightPrio = -1;   // -1 = whole layer
static int s_previewHighlightPal = -1;    // >= 0: only this sprite palette draws (glow by palette)

void gpu3dsSetStereoPreviewHighlight(int layerId, int prio)
{
    s_previewHighlightLayer = layerId;
    s_previewHighlightPal = prio >= 8 ? prio - 8 : -1;
    s_previewHighlightPrio = prio >= 8 ? -1 : prio;
    s_atmosLastColor = 1;               // poison the dedup so envs re-apply
}

// forceOthers: treat this layer as "one of the others" (dimmed) even when
// it is the highlighted one - the spotlight's base pass uses it so the
// whole layer stays visible under the bright redraw of the edited priority
// The effect math for ONE depth: fade/haze tint + blur ghost strength,
// all anchored on the focus zone. Since the priority split (issue #60)
// a layer's tiers can sit at different depths, so the caller may invoke
// this once per depth GROUP (alpha-hiding the other groups) - that is
// what makes fade, haze and blur per-priority.
// pure math: one depth's effect parameters (tint color, ghost strength),
// no GPU state touched - the group builder uses it to MERGE tiers whose
// effects come out identical (issue #71: split-but-in-zone tiers were
// paying an extra pass for a no-op tint)
static void gpu3dsComputeAtmosphereForDepth(float depth, u32 *outColor,
    float *outGhostAlpha, float *outGhostOffset)
{
    // Relative model: the depth farthest outside the zone gets the full
    // gauge strength, the ones in between scale linearly by their share.
    // Fade/haze count only the distance BEHIND the zone (distance cues).
    float backExcess = depth < GPU3DS.stereoFocusBack ? GPU3DS.stereoFocusBack - depth : 0.0f;
    float depthIn = GPU3DS.stereoMaxBackExcess > 0.0f ? backExcess / GPU3DS.stereoMaxBackExcess : 0.0f;

    float slider = GPU3DS.stereoEyeIOD < 0.0f ? -GPU3DS.stereoEyeIOD : GPU3DS.stereoEyeIOD;

    float f = (GPU3DS.stereoFade / 8.0f) * depthIn * slider * 0.70f;
    float hz = (GPU3DS.stereoHaze / 8.0f) * depthIn * slider * 0.60f;
    float t = f + hz;
    if (t > 0.85f) t = 0.85f;

    u32 color = 0xFFFFFFFF;   // sentinel: passthrough
    if (t >= 0.01f) {
        float w = hz / (f + hz);   // haze share picks the target color
        u8 r = (u8)(200.0f * w), g = (u8)(205.0f * w), b = (u8)(215.0f * w);
        u8 a = (u8)(t * 255.0f);
        color = ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | r;
    }

    // Depth Blur is its own control: ghost passes with soft edges,
    // independent from the haze tint. The level widens the smear
    // (1..3px) and strengthens the ghosts together. Unlike fade/haze it
    // is a focus cue, not a distance cue: it counts the distance to the
    // nearest zone edge in BOTH directions (depth-of-field).
    float excess = 0.0f;
    if (depth < GPU3DS.stereoFocusBack)
        excess = GPU3DS.stereoFocusBack - depth;
    else if (depth > GPU3DS.stereoFocusFront)
        excess = depth - GPU3DS.stereoFocusFront;

    float excessIn = GPU3DS.stereoMaxExcess > 0.0f ? excess / GPU3DS.stereoMaxExcess : 0.0f;
    float blur = (GPU3DS.stereoBlur / 8.0f) * excessIn * slider;
    if (blur > 0.02f) {
        *outGhostAlpha = 0.25f + 0.25f * blur;
        // under Enhanced Resolution the aura grows 1.875x (not the full 2x
        // of the parallax scale) to track the stronger depth there
        *outGhostOffset = (1.0f + 2.0f * blur)
            * (GPU3DSExt.render2x.enabled ? 1.875f : 1.0f);
    } else {
        *outGhostAlpha = 0.0f;
        *outGhostOffset = 0.0f;
    }
    *outColor = color;
}

// applies a computed tint color to the atmosphere TexEnv stage (dedup'd)
static void gpu3dsApplyAtmosphereColor(u32 color)
{
    if (color == s_atmosLastColor)
        return;
    s_atmosLastColor = color;

    C3D_TexEnv *env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    if (color == 0xFFFFFFFF)
        return;

    C3D_TexEnvColor(env, color);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_CONSTANT);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

// Mode 7 by distance: the SAME interpolate stage, but the amount comes
// from the vertex color's blue (the tile vertex shader writes each Mode 7
// row's fog there) instead of the constant's alpha
static void gpu3dsApplyAtmosphereColorPerVertex(u32 color)
{
    s_atmosLastColor = 0;   // the constant variant must re-apply after us
    C3D_TexEnv *env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    C3D_TexEnvColor(env, color | 0xFF000000);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_B);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

static void gpu3dsSetStereoAtmosphereForDepth(float depth)
{
    u32 color;
    gpu3dsComputeAtmosphereForDepth(depth, &color,
        &s_atmosGhostAlpha, &s_atmosGhostOffset);
    gpu3dsApplyAtmosphereColor(color);
}

static void gpu3dsSetStereoLayerAtmosphere(LAYER_ID id, bool forceOthers = false)
{
    if (s_previewHighlightLayer >= 0)
    {
        s_atmosGhostAlpha = 0.0f;
        s_atmosGhostOffset = 0.0f;
        u32 color = 0xFFFFFFFF;                     // highlighted: untouched
        if ((int)id != s_previewHighlightLayer || forceOthers)
            color = 0xB4000000;                     // others: 70% toward black
        if (color == s_atmosLastColor)
            return;
        s_atmosLastColor = color;
        C3D_TexEnv *env = C3D_GetTexEnv(2);
        C3D_TexEnvInit(env);
        if (color == 0xFFFFFFFF)
            return;
        C3D_TexEnvColor(env, color);
        C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_CONSTANT);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
        return;
    }

    gpu3dsSetStereoAtmosphereForDepth(GPU3DS.stereoLayerDepth[id]);
}

void gpu3dsDrawLayers(SLayerList *list) {
    // draw window_lr into depth buffer first (screen-space: no parallax)
    SLayer *layer = &list->layers[LAYER_WINDOW_LR];

    gpu3dsSetStereoParallax(0.0f);
    gpu3dsSetMode7Persp(0.0f, 1.0f, 0.0f, 0.0f);
    // neutral spotlight for the window/depth prepass: a stale dim from
    // the previous frame would alpha-discard the window masks
    gpu3dsSetStereoPrioDim(1.0f, 1.0f);
    gpu3dsResetStereoAtmosphere();

    // 3D: each eye's pass starts from a clean depth floor. The depth
    // texture is shared between the eyes and never cleared - identical
    // 2D redraws hide that, but per-eye parallax means one eye's shifted
    // high-priority tiles leave their depth behind and occlude the other
    // eye's lower layers along their edges (the MMX3 stage-1 pillar
    // hole). Drawn as a full rect into the depth target - the same way
    // the window prepass writes it - because a GX fill cannot join the
    // middle of the frame's command list. Skipped entirely in 2D
    // (IOD 0), so the Old 3DS pays nothing.
    if (GPU3DS.stereoEyeIOD != 0.0f) {
        GPU3DS.currentRenderState.target = TARGET_SNES_DEPTH;
        GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
        GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
        GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;

        SVertexList *rects = &GPU3DS.vertices[VBO_SCENE_RECT];
        int floorFrom = rects->from + rects->count;
        // DELIBERATELY inert: y = 256 decodes as y = 0 on plane 1 in the tile
        // shader and the rect collapses. It was made to run (y 255) on
        // 2026-10-15 and the drop shadows stopped on the console: a colour
        // write into the depth texture while it is also the layers' depth
        // buffer races the depth cache on the PICA (the zeros land after
        // the BGs' planes and EQUAL finds nothing). Azahar shows neither.
        // The shadows never needed it: every drawn pixel writes its plane.
        gpu3dsAddRectangleVertexes(0, 0, 512, 256, 0x00000000);
        gpu3dsDraw(rects, NULL, 2, floorFrom);
        rects->count -= 2;   // paused previews replay this path with no
                             // per-frame VBO flip - don't let it creep
    }

    if (layer->verticesByTarget[0]) {
        GPU3DS.currentRenderState.target = TARGET_SNES_DEPTH;

        gpu3dsDrawVerticalSectionLayer(layer, layer->sectionsOffset, layer->sectionsOffset + layer->sectionsByTarget[TARGET_SNES_MAIN]);
    }

    u8 i0 = list->anythingOnSub ? 1 : 0;

    for (int i = i0; i >= 0; i--) {
        GPU3DS.currentRenderState.target = (SGPU_TARGET_ID)i;

        bool sub = i == TARGET_SNES_SUB;

        // Drop shadows (issue #77), one phase per target after the BGs and
        // before colour math (whose full-screen rects write z = 0 and wipe
        // the depth buffer). By then every pixel's depth holds the plane
        // of its visible surface: sprites write (prio+1)*3, BG tiles their
        // mode's d0/d1 under GEQUAL, transparent texels and windowed
        // pixels never write. So "the receiver is the surface here" is
        // "depth == the receiver's plane": the silhouette is the caster's
        // own geometry drawn once more at the receiver's plane (depth map
        // scale 0, offset plane/32) with depth func EQUAL, shifted by the
        // receiver's parallax plus Shadow X/Y. A layer in front holds a
        // larger plane, the caster's body its own plane, a hole in the
        // receiver a smaller one: none of them passes. No mask, no erase,
        // no redraw of the caster.
        bool shadowPhaseDone = false;
        auto drawShadowPhase = [&]() {
            if (shadowPhaseDone) return;
            shadowPhaseDone = true;
            const bool shadowsOn = GPU3DS.stereoShadowRows != 0 && GPU3DS.stereoEyeIOD != 0.0f;
            const bool glowWhole = GPU3DS.stereoGlowSource == 1 && GPU3DS.glowTexAvailable;
            const bool glowOn = (GPU3DS.stereoGlowRows != 0 || glowWhole) && !sub;   // never into the sub screen: subtractive colour math would darken it
            if (!shadowsOn && !glowOn) return;
            int depthRows[STEREO_SHADOW_ROWS]; unsigned usedRows = 0;
            for (int l = 0; l < 5; l++) {
                depthRows[stereo_shadow_row(l, 0)] = (int)GPU3DS.stereoLayerDepth[l];
                depthRows[stereo_shadow_row(l, 1)] = (int)GPU3DS.stereoLayerDepthP1[l];
            }
            depthRows[10] = (int)GPU3DS.stereoLayerDepthOBJHi[0];
            depthRows[11] = (int)GPU3DS.stereoLayerDepthOBJHi[1];
            for (int r = 0; r < STEREO_SHADOW_ROWS; r++)
                if (S9xLayerUsedLastFrame(r < 8 ? r / 2 : 4, r < 8 ? r & 1 : r - 8)) usedRows |= 1u << r;
            const uint32_t rgb = stereo_shadow_color_rgb(GPU3DS.stereoShadowColor);

            // a layer's sections on this target
            auto sectionsOf = [&](int lid, int &from, int &to) {
                SLayer *ly = &list->layers[lid];
                from = ly->sectionsOffset + (sub ? 0 : ly->sectionsByTarget[TARGET_SNES_SUB]);
                to = from + ly->sectionsByTarget[i];
                return to > from;
            };
            // One vertex range of a caster section with the section's own
            // packed state (its window stencil included: a clipped sprite
            // casts no shadow where it is clipped), then the pass state on
            // top. Indexed mode gets its own index scratch: the layers' IBO
            // regions are still owned by the queued base draws, and the
            // hardware dislikes DrawArrays mixed into a DrawElements frame.
            static u16 *shadowIbo = nullptr; static int shadowIboUsed = 0;
            const int SHADOW_IBO_MAX = 32768;
            if (shadowIbo == nullptr) shadowIbo = (u16 *)linearAlloc(SHADOW_IBO_MAX * sizeof(u16));
            auto drawRange = [&](SLayer *ly, SLayerSection *sec, int from, int count, int plane) {
                if (sec->vboId == VBO_SCENE_MODE7_LINE) return;   // a Mode 7 plane receives, never casts
                GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
                GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
                u64 mask = gpu3dsGetLayerPackedMask(ly->id, true);
                GPU3DS.currentRenderState.packed = (GPU3DS.currentRenderState.packed & ~mask) | (sec->state.packed & mask);
                GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;   // transparent texels cast nothing
                GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
                GPU3DS.currentRenderState.target = (SGPU_TARGET_ID)i;
                GPU3DS.currentRenderState.shader = SPROGRAM_TILES;
                gpu3dsSetMode7Persp(0.0f, 1.0f, 0.0f, 0.0f);
                gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
                gpu3dsShadowPassState(plane);
                GPU3DSExt.shadowDraws++;
                SVertexList *vbo = &GPU3DS.vertices[sec->vboId];
                if (list->useDrawArraysForTiledLayers) {
                    gpu3dsDraw(vbo, NULL, count, from);
                } else {
                    if (shadowIbo == nullptr || shadowIboUsed + count > SHADOW_IBO_MAX) return;
                    u16 *dst = shadowIbo + shadowIboUsed;
                    for (int k = 0; k < count; k++) dst[k] = (u16)(from + k);
                    gpu3dsDraw(vbo, (void *)dst, count);
                    shadowIboUsed += count;
                }
            };
            // the tier parallax of one row of a layer, the other tiers parked off-screen
            // the tier shifts of a layer (4096 parks a tier off-screen), the
            // tier boundaries as the layer's own pass sets them
            auto setTierShifts = [&](int lid, const float sh[4], float shadowY) {
                float b01 = 0.0f, b12 = STEREO_TIER_PARKED, b23 = STEREO_TIER_PARKED;
                if (lid == LAYER_OBJ) { b01 = 4.5f / 32.0f; b12 = 7.5f / 32.0f; b23 = 10.5f / 32.0f; }
                else b01 = GPU3DS.stereoPrioZBoundary[lid];
                gpu3dsSetStereoParallax3(sh[0], sh[1], b01, shadowY);
                gpu3dsSetStereoParallaxHi(sh[2], sh[3], b12, b23);
            };
            auto ownShift = [&](int lid, int tier) {
                float d = tier == 0 ? GPU3DS.stereoLayerDepth[lid] : tier == 1 ? GPU3DS.stereoLayerDepthP1[lid] : GPU3DS.stereoLayerDepthOBJHi[tier - 2];
                float v = GPU3DS.stereoEyeIOD * d * STEREO_PARALLAX_SCALE;
                return settings3DS.StereoShiftMode == 0 ? roundf(v) : v;
            };
            if (i == i0) shadowIboUsed = 0;   // first target of the frame: the scratch starts over

            gpu3dsSetStereoPrioDim4(1.0f, 1.0f, 1.0f, 1.0f);
            gpu3dsApplyAtmosphereColor(0xFFFFFFFF);
            bool anyPass = false;
#ifdef PROBE_SHADOW_LOG
            {
                extern int g_probeFrames;
                if (g_probeFrames >= 598 && g_probeFrames <= 600) {
                    log3dsWrite("[shadow] target=%d used=%03x depth=%d,%d,%d,%d,%d,%d,%d,%d|%d,%d,%d,%d planes=%d,%d,%d,%d,%d,%d,%d,%d mode=%d",
                        i, usedRows, depthRows[0], depthRows[1], depthRows[2], depthRows[3], depthRows[4], depthRows[5], depthRows[6], depthRows[7],
                        depthRows[8], depthRows[9], depthRows[10], depthRows[11],
                        GPU3DS.stereoBgPlane[0], GPU3DS.stereoBgPlane[1], GPU3DS.stereoBgPlane[2], GPU3DS.stereoBgPlane[3],
                        GPU3DS.stereoBgPlane[4], GPU3DS.stereoBgPlane[5], GPU3DS.stereoBgPlane[6], GPU3DS.stereoBgPlane[7], (int)PPU.BGMode);
                    for (int id = LAYER_BG0; id <= LAYER_OBJ; id++) {
                        int f, t; const bool has = sectionsOf(id, f, t);
                        log3dsWrite("[shadow]  layer %d enabled=%d sections=%d", id, settings3DS.LayerEnabled[id] ? 1 : 0, has ? t - f : 0);
                    }
                }
            }
#endif

            for (int id = LAYER_BG0; id <= LAYER_OBJ && shadowsOn; id++) {
                if (!settings3DS.LayerEnabled[id]) continue;
                const int tiers = id == LAYER_OBJ ? 4 : 2;
                int cFrom, cTo;
                if (!sectionsOf(id, cFrom, cTo)) continue;
                SLayer *ly = &list->layers[id];
                auto drawCaster = [&](int plane) {
                    for (int k = cFrom; k < cTo; k++) {
                        SLayerSection *sec = &list->sections[k];
                        drawRange(ly, sec, sec->from, sec->count, plane);
                    }
                };

                // one silhouette per casting tier, at the parallax and planes
                // of the row behind it in the profile's gauges (the nearest
                // smaller Depth); every used row at that depth is the same
                // surface in 3D, so one pass per distinct plane of theirs.
                // Tiers that share the receiver, the depth gap and the planes
                // (sprites P2 and P3 over the same BGs: the common case) draw
                // in ONE pass, every such tier's shift set together.
                bool tierDone[4] = { false, false, false, false };
                for (int t = 0; t < tiers; t++) {
                    if (tierDone[t] || !stereo_shadow_marked((unsigned)GPU3DS.stereoShadowRows, id, t)) continue;
                    const int row = stereo_shadow_row(id, t);
                    // a marked row this scene does not draw has nothing to cast:
                    // skip it, or its whole layer is drawn for no pixel (Jorge's
                    // water stage: BG1 P1 marked in another scene of the profile)
                    if (((usedRows >> row) & 1u) == 0) { tierDone[t] = true; continue; }
                    const int br = stereo_shadow_behind_row(depthRows, usedRows, row);
                    int planes[8];
                    const int n = stereo_shadow_receiver_planes(depthRows, usedRows, GPU3DS.stereoBgPlane, row, planes);
                    tierDone[t] = true;
                    if (br < 0 || n == 0) continue;
                    const int gap = depthRows[row] - depthRows[br];
                    bool group[4] = { false, false, false, false }; group[t] = true;
                    for (int u = t + 1; u < tiers; u++) {
                        if (tierDone[u] || !stereo_shadow_marked((unsigned)GPU3DS.stereoShadowRows, id, u)) continue;
                        const int rowU = stereo_shadow_row(id, u);
                        if (((usedRows >> rowU) & 1u) == 0) continue;
                        if (stereo_shadow_behind_row(depthRows, usedRows, rowU) != br || depthRows[rowU] - depthRows[br] != gap) continue;
                        int planesU[8];
                        const int nU = stereo_shadow_receiver_planes(depthRows, usedRows, GPU3DS.stereoBgPlane, rowU, planesU);
                        bool same = nU == n;
                        for (int k = 0; same && k < n; k++) same = planesU[k] == planes[k];
                        if (!same) continue;
                        group[u] = true; tierDone[u] = true;
                    }
                    const float behindShift = ownShift(br < 8 ? br / 2 : 4, br < 8 ? br & 1 : br - 8);
                    // RGB = the shadow colour, alpha = texture alpha x opacity (lighter with the depth gap when Fade is on)
                    float alpha = stereo_shadow_alpha(GPU3DS.stereoShadowAlpha, GPU3DS.stereoShadowFade != 0, gap);
                    // Shadow Quality (shadow_auto.h): the load controller's Soft Edge
                    // level and fade multiplier for this frame
                    alpha *= GPU3DSExt.shadowAlphaMul;
                    if (alpha < 0.01f) continue;
                    // Soft Edge: a box of copies, each part of the way to the plain alpha
                    const int level = stereo_shadow_clamp_soft(GPU3DSExt.shadowSoftLevel);
                    const int copies = stereo_shadow_soft_copies(level);
                    alpha = stereo_shadow_soft_alpha(alpha, copies);
                    gpu3dsSetShadowEnv(alpha, rgb);
                    for (int c = 0; c < copies; c++) {
                        const float shiftValue = behindShift + (float)(GPU3DS.stereoShadowX + stereo_shadow_soft_dx(level, c));
                        float sh[4];
                        for (int u = 0; u < 4; u++) sh[u] = group[u] ? shiftValue : 4096.0f;
                        setTierShifts(id, sh, (float)(GPU3DS.stereoShadowY + stereo_shadow_soft_dy(level, c)));
                        for (int k = 0; k < n; k++) drawCaster(planes[k]);
                    }
                    anyPass = true;
                }
            }
            if (anyPass) {
                gpu3dsSetShadowEnv(0.0f, 0);
                gpu3dsShadowPassEnd();
            }

            // Glow (issue #77): the marked rows' bright texels drawn again at
            // their own parallax, added to the frame, with a halo of copies.
            // Works in 2D too (shifts are 0). The load controller's tier
            // lowers the halo and its fade multiplier scales the gain.
            if (glowOn && GPU3DSExt.shadowAlphaMul > 0.01f) {
                const int tierDrop = GPU3DS.stereoShadowQuality >= 1 ? GPU3DSExt.shadowLoadTier : 0;
                // Glow Spread: the glow drawn once at quarter resolution into
                // SNES_GLOW and magnified back with bilinear taps (the light
                // leaks ~4-8 px); replaces the halo copies while on
                int spread = GPU3DS.glowTexAvailable ? stereo_glow_clamp_spread(GPU3DS.stereoGlowSpread - tierDrop) : 0;
                if (glowWhole && spread < 1) spread = 1;   // the whole-screen source lives in the spread target
                const int taps = stereo_glow_spread_taps(spread);
                const int soft = taps > 0 ? 0 : stereo_glow_clamp_soft(GPU3DS.stereoGlowSoft - tierDrop);
                const int copies = stereo_glow_copies(soft);
                const float gain = stereo_glow_copy_gain(GPU3DS.stereoGlowStrength, soft) * GPU3DSExt.shadowAlphaMul;
                const uint32_t tint = stereo_glow_color_rgb(GPU3DS.stereoGlowColor);
                const int ref = stereo_glow_alpha_ref(GPU3DS.stereoGlowThresh);
                bool anyGlow = false;
                // one tier at a time: the glow is depth-tested GEQUAL at the
                // tier's own plane (a layer in front - larger plane - covers it,
                // so nothing glows through a wall; the light leaks only onto
                // what is behind the sprite), and the plane is a constant per
                // draw through the depth map, like the shadow's
                if (taps > 0) { s_glowScratch.init(); if (!s_glowScratch.ready) return; }
                // the visible SNES area (256 or 512 wide, 224 or 239 high)
                const int glowW = GPU3DSExt.renderWidth > 0 ? GPU3DSExt.renderWidth : 256;
                const int glowH = PPU.ScreenHeight > 0 && PPU.ScreenHeight <= 240 ? (int)PPU.ScreenHeight : 224;
                // (an edge crop by the per-eye parallax margin was tried for the
                // whole-screen source and dropped: an object half inside the
                // margin lost source in one eye only - more asymmetry, not less)
                const int glowX0 = 0, glowX1 = glowW;
                auto drawGlowGeometry = [&](int id, int plane, SGPU_TARGET_ID target, bool depthTested) {
                    int cFrom, cTo;
                    if (!sectionsOf(id, cFrom, cTo)) return;
                    SLayer *ly = &list->layers[id];
                    for (int k = cFrom; k < cTo; k++) {
                        SLayerSection *sec = &list->sections[k];
                        if (sec->vboId == VBO_SCENE_MODE7_LINE) continue;
                        GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
                        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
                        u64 mask = gpu3dsGetLayerPackedMask(ly->id, true);
                        GPU3DS.currentRenderState.packed = (GPU3DS.currentRenderState.packed & ~mask) | (sec->state.packed & mask);
                        GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;
                        GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
                        GPU3DS.currentRenderState.target = target;
                        GPU3DS.currentRenderState.shader = SPROGRAM_TILES;
                        gpu3dsSetMode7Persp(0.0f, 1.0f, 0.0f, 0.0f);
                        gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
                        gpu3dsGlowPassState(ref, depthTested ? plane : -1);
                        GPU3DSExt.glowDraws++;
                        SVertexList *vbo = &GPU3DS.vertices[sec->vboId];
                        if (list->useDrawArraysForTiledLayers) {
                            gpu3dsDraw(vbo, NULL, sec->count, sec->from);
                        } else if (shadowIbo != nullptr && shadowIboUsed + sec->count <= SHADOW_IBO_MAX) {
                            u16 *dst = shadowIbo + shadowIboUsed;
                            for (int k2 = 0; k2 < sec->count; k2++) dst[k2] = (u16)(sec->from + k2);
                            gpu3dsDraw(vbo, (void *)dst, sec->count);
                            shadowIboUsed += sec->count;
                        }
                        anyGlow = true;
                    }
                };
                // Persistence (Jorge: the two eyes are drawn on alternate frames,
                // so a flash blinking every frame glows in one eye only): instead
                // of clearing, the target keeps k/4 of last frame's glow - a rect
                // with blend (ZERO, CONSTANT) multiplies what is there
                const int persist = taps > 0 ? (GPU3DS.stereoGlowPersist < 0 ? 0 : (GPU3DS.stereoGlowPersist > STEREO_GLOW_PERSIST_MAX ? STEREO_GLOW_PERSIST_MAX : GPU3DS.stereoGlowPersist)) : 0;
                auto clearGlowTarget = [&]() {
                    GPU3DS.currentRenderState.target = TARGET_SNES_GLOW;
                    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
                    GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
                    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
                    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
                    GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
                    GPU3DS.currentRenderState.shader = SPROGRAM_TILES;
                    gpu3dsSetStereoParallax3(0.0f, 0.0f, 0.0f, 0.0f);
                    gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
                    gpu3dsGlowPassEnd();   // plain state for the clear
                    if (persist > 0) {
                        const u32 k = (u32)(persist * 64 - 1);   // 63 / 127 / 191 of 255
                        C3D_BlendingColor((k << 16) | (k << 8) | k);
                        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_CONSTANT_COLOR, GPU_ZERO, GPU_ONE);
                        GPU3DS.appliedRenderState.alphaBlending = (SGPU_ALPHA_BLENDINGMODE)(ALPHA_BLENDING_GHOST + 1);
                    }
                    gpu3dsDraw(&s_glowScratch.rect, NULL, 2, s_glowScratch.addRect(0x000000FF));
                    if (persist > 0) gpu3dsDisableAlphaBlending();
                };
                // composite: the glow texture over the SNES area, bilinear (4x
                // magnified = blurred), added once per tap at 1/taps, the
                // diagonal taps one texel (4 px) away; depth-tested at the tier's plane
                auto compositeGlow = [&](int plane, float shift) {
                    GPU3DS.currentRenderState.target = (SGPU_TARGET_ID)i;
                    GPU3DS.currentRenderState.textureBind = SNES_GLOW;
                    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
                    GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
                    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
                    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
                    GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
                    GPU3DS.currentRenderState.shader = SPROGRAM_TILES;
                    gpu3dsSetStereoParallax3(shift, shift, 0.0f, 0.0f);   // the tile-0 quad lands on tier 1 (plane 0 >= boundary 0): both tiers carry the shift
                    gpu3dsSetStereoParallaxHi(shift, shift, STEREO_TIER_PARKED, STEREO_TIER_PARKED);
                    gpu3dsSetGlowPalette(255, false);
                    gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
                    gpu3dsSetGlowEnv(false, 0.0f, 0);
                    gpu3dsSetGlowTapEnv(1.0f / (float)taps);
                    // the glow target was rendered moments ago in this same command
                    // list: split it so the GPU finishes (and flushes) that render
                    // before the texture unit reads it - on the console the right
                    // eye's composite read the left eye's glow (Jorge's screenshots)
                    C3D_FrameSplit(0);
                    if (plane >= 0) {
                        C3D_DepthMap(true, 0.0f, (float)plane / 32.0f);
                        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
                    } else {
                        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
                    }
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
                    for (int tp = 0; tp < taps; tp++) {
                        const s16 dx = (s16)stereo_glow_tap_dx(tp), dy = (s16)stereo_glow_tap_dy(tp);
                        // tile index 0 = raw texcoords: the quad covers the visible SNES area, sampling the 128x64 texture at a quarter
                        const int qFrom = s_glowScratch.addQuad((s16)glowX0, 0, (s16)glowX1, (s16)glowH, (s16)(glowX0 / 4 + dx), dy, (s16)(glowX1 / 4 + dx), (s16)(glowH / 4 + dy));
                        gpu3dsDraw(&s_glowScratch.quad, NULL, 2, qFrom);
                        GPU3DSExt.glowDraws++;
                    }
                    gpu3dsSetGlowTapEnv(0.0f);
                    gpu3dsGlowPassEnd();
                    GPU3DS.appliedRenderState.target = TARGET_UNSET;
                };
                if (glowWhole) {
                    // Whole screen (Jorge): the frame drawn so far, as one quad,
                    // through the same luminance cut into the spread target,
                    // then composited back - a post-process bloom at quarter
                    // res; no depth test (screen space), the HUD glows too.
                    // PER EYE, from that eye's own texture: a single glow made
                    // from the left eye and shifted into the right can be right
                    // for one depth only (tried 2026-10-15: backgrounds sat 2p
                    // to the left in the right eye). Per eye every object's
                    // glow carries its own parallax, exactly as 2D does; what
                    // made the two eyes differ before were the source edges
                    // (cropped below) and sampling before the render landed
                    // (the frame split).
                    clearGlowTarget();
                    gpu3dsSetGlowEnv(true, gain, tint);
                    gpu3dsSetGlowPalette(255, false);
                    GPU3DS.currentRenderState.target = TARGET_SNES_GLOW;
                    GPU3DS.currentRenderState.textureBind = GPU3DS.stereoRightPass ? SNES_MAIN_RIGHT : SNES_MAIN;
                    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
                    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_NE_ZERO;
                    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
                    GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
                    GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
                    GPU3DS.currentRenderState.shader = SPROGRAM_TILES;
                    gpu3dsSetStereoParallax3(0.0f, 0.0f, 0.0f, 0.0f);
                    gpu3dsSetStereoParallaxHi(0.0f, 0.0f, STEREO_TIER_PARKED, STEREO_TIER_PARKED);
                    gpu3dsSetMode7Persp(0.0f, 1.0f, 0.0f, 0.0f);
                    gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
                    gpu3dsGlowPassState(ref, -1);
                    C3D_FrameSplit(0);   // the eye's texture was just rendered: let it land before sampling it
                    // only the visible area, minus the parallax margin at each
                    // side (the eye texture's corrupted edge columns): what lies
                    // outside would be blurred into the frame's edge
                    int margin = 0;
                    if (GPU3DS.stereoEyeIOD != 0.0f && GPU3DS.stereoMaxAbs > 0.0f) {
                        const float iod = GPU3DS.stereoEyeIOD < 0.0f ? -GPU3DS.stereoEyeIOD : GPU3DS.stereoEyeIOD;
                        margin = (int)(GPU3DS.stereoMaxAbs * (iod / gpu3dsGetIODBase()) * STEREO_PARALLAX_SCALE * (float)glowW / 256.0f + 0.999f);
                        if (margin > glowW / 4) margin = glowW / 4;
                    }
                    // a 4x4 box average, not one texel per glow texel: four
                    // bilinear taps (2x2 each) at +-1 px, a quarter of the gain
                    // each - a nearest pick flickered as the screen scrolled
                    SGPUTexture *src = &GPU3DS.textures[GPU3DS.stereoRightPass ? SNES_MAIN_RIGHT : SNES_MAIN];
                    C3D_TexSetFilter(&src->tex, GPU_LINEAR, GPU_LINEAR);
                    C3D_TexBind(0, &src->tex);
                    gpu3dsSetGlowEnv(true, gain * 0.25f, tint);   // AFTER the render state applies: that resets the stages (2026-10-15: set only before, the glow vanished)
                    for (int tp = 0; tp < 4; tp++) {
                        const float tx = (tp & 1) ? 1.0f : -1.0f, ty = (tp & 2) ? 1.0f : -1.0f;
                        gpu3dsSetStereoParallax3(tx, tx, 0.0f, ty);
                        gpu3dsSetStereoParallaxHi(tx, tx, STEREO_TIER_PARKED, STEREO_TIER_PARKED);
                        const int qFrom = s_glowScratch.addQuad((s16)margin, 1, (s16)(glowW - margin), (s16)glowH, (s16)margin, 1, (s16)(glowW - margin), (s16)glowH);
                        gpu3dsDraw(&s_glowScratch.quad, NULL, 2, qFrom);
                        GPU3DSExt.glowDraws++;
                    }
                    gpu3dsSetStereoParallax3(0.0f, 0.0f, 0.0f, 0.0f);
                    C3D_TexSetFilter(&src->tex, GPU_NEAREST, GPU_NEAREST);   // gpu3dsBindTexture re-decides on the next bind
                    C3D_TexBind(0, &src->tex);
                    anyGlow = true;
                    compositeGlow(-1, 0.0f);
                }
                // with persistence every tier shares the one decaying target:
                // one decay, every marked tier drawn, one composite at the
                // lowest marked plane (occlusion between tiers is approximate)
                const bool shared = taps > 0 && persist > 0 && !glowWhole;
                int sharedPlane = 99; bool sharedDrawn = false;
                if (shared) clearGlowTarget();
                for (int id = LAYER_BG0; id <= LAYER_OBJ && !glowWhole; id++) {
                    if (!settings3DS.LayerEnabled[id]) continue;
                    const int tiers = id == LAYER_OBJ ? 4 : 2;
                    for (int t = 0; t < tiers; t++) {
                        const int row = stereo_shadow_row(id, t);
                        if (!stereo_shadow_marked((unsigned)GPU3DS.stereoGlowRows, id, t)) continue;
                        if (((usedRows >> row) & 1u) == 0) continue;
                        const int plane = stereo_shadow_row_plane(GPU3DS.stereoBgPlane, row);
                        const float shift = ownShift(id, t);
                        gpu3dsSetGlowEnv(true, gain, tint);
                        gpu3dsSetGlowPalette(GPU3DS.stereoGlowPal, id == LAYER_OBJ && (GPU3DS.stereoGlowPal & 0xFF) != 0xFF);
                        if (shared) {
                            float sh[4]; for (int u = 0; u < 4; u++) sh[u] = u == t ? shift : 4096.0f;
                            setTierShifts(id, sh, 0.0f);
                            drawGlowGeometry(id, plane, TARGET_SNES_GLOW, false);
                            if (plane < sharedPlane) sharedPlane = plane;
                            sharedDrawn = true;
                        } else if (taps > 0) {
                            clearGlowTarget();
                            gpu3dsSetGlowEnv(true, gain, tint);
                            gpu3dsSetGlowPalette(GPU3DS.stereoGlowPal, id == LAYER_OBJ && (GPU3DS.stereoGlowPal & 0xFF) != 0xFF);
                            float sh[4]; for (int u = 0; u < 4; u++) sh[u] = u == t ? shift : 4096.0f;
                            setTierShifts(id, sh, 0.0f);
                            drawGlowGeometry(id, plane, TARGET_SNES_GLOW, false);
                            if (anyGlow) compositeGlow(plane, 0.0f);
                        } else {
                            for (int c = 0; c < copies; c++) {
                                float sh[4]; for (int u = 0; u < 4; u++) sh[u] = u == t ? shift + (float)stereo_glow_dx(soft, c) : 4096.0f;
                                setTierShifts(id, sh, (float)stereo_glow_dy(soft, c));
                                drawGlowGeometry(id, plane, (SGPU_TARGET_ID)i, true);
                            }
                        }
                    }
                }
                // the shared (persisting) target composites once; with nothing
                // drawn this frame it still shows last frame's decaying glow
                if (shared) compositeGlow(sharedDrawn ? sharedPlane : -1, 0.0f);
                if (anyGlow || shared) {
                    gpu3dsSetGlowEnv(false, 0.0f, 0);
                    gpu3dsGlowPassEnd();
                }
            }
            gpu3dsSetStereoParallax3(0.0f, 0.0f, 0.0f, 0.0f);
            gpu3dsSetMode7Persp(0.0f, 1.0f, 0.0f, 0.0f);
        };

        for (int j = 0; j < list->layersTotalByTarget[i]; j++) {
            LAYER_ID id = list->layersByTarget[i][j];
            SLayer *layer = &list->layers[id];
            // after every BG, before the colour math / brightness rects
            // zero the depth buffer (the backdrop draws first, before the sprites)
            if (id == LAYER_COLOR_MATH || id == LAYER_BRIGHTNESS) drawShadowPhase();

            // honor the diagnostic layer toggles at replay time too, so
            // the 3D editor's paused preview reflects them live. In
            // gameplay a disabled layer generates no vertices, so this
            // skip changes nothing there.
            if ((int)id <= LAYER_BRIGHTNESS && !settings3DS.LayerEnabled[id])
                continue;

            // per-layer stereo parallax + atmosphere (both neutral for
            // backdrop/color math/etc., whose depth entries are 0).
            // Rounded to a whole SNES pixel (issue #65): the slider is a
            // continuous 0..1, and a fractional shift rasterizes with
            // mixed per-section rounding - half the layer moves, half
            // stays. One rounded value keeps the layer a single block.
            // Discrete mode (default) rounds every shift to a whole SNES
            // pixel so a layer moves as one block (issue #65); Continuous
            // keeps the analog slider feel and accepts that fractional
            // shifts can split a layer at partial slider (user's choice,
            // issue #60 UX).
            // four depth tiers feed the shader cascade. BGs use tiers 0/1
            // (their two tile priorities) with the upper boundaries parked;
            // the OBJ layer maps its four sprite priorities onto all tiers
            // (planes are (priority+1)*3 = 3/6/9/12, see S9xDrawOBJSHardware,
            // so the boundaries sit halfway between them).
            float tierShift[4];
            float tierBnd01 = GPU3DS.stereoPrioZBoundary[id];
            float tierBnd12 = STEREO_TIER_PARKED, tierBnd23 = STEREO_TIER_PARKED;
            {
                tierShift[0] = GPU3DS.stereoEyeIOD * GPU3DS.stereoLayerDepth[id] * STEREO_PARALLAX_SCALE;
                tierShift[1] = GPU3DS.stereoEyeIOD * GPU3DS.stereoLayerDepthP1[id] * STEREO_PARALLAX_SCALE;
                tierShift[2] = tierShift[1];
                tierShift[3] = tierShift[1];
                if (id == LAYER_OBJ) {
                    tierShift[2] = GPU3DS.stereoEyeIOD * GPU3DS.stereoLayerDepthOBJHi[0] * STEREO_PARALLAX_SCALE;
                    tierShift[3] = GPU3DS.stereoEyeIOD * GPU3DS.stereoLayerDepthOBJHi[1] * STEREO_PARALLAX_SCALE;
                    tierBnd01 = 4.5f / 32.0f;
                    tierBnd12 = 7.5f / 32.0f;
                    tierBnd23 = 10.5f / 32.0f;
                }
                if (settings3DS.StereoShiftMode == 0)
                    for (int t = 0; t < 4; t++) tierShift[t] = roundf(tierShift[t]);
                gpu3dsSetStereoParallax3(tierShift[0], tierShift[1], tierBnd01);
                gpu3dsSetStereoParallaxHi(tierShift[2], tierShift[3], tierBnd12, tierBnd23);
            }

            int from = layer->sectionsOffset + (sub ? 0 : layer->sectionsByTarget[TARGET_SNES_SUB]);
            int to = from + layer->sectionsByTarget[i];

            if (to <= from) continue;

            // Mode 7 perspective (issue #62): the strength reaches the
            // shader only for a Mode 7 scanline layer; every other layer
            // draws with 0, which the shader treats as an exact no-op
            bool isMode7Plane = list->sections[from].vboId == VBO_SCENE_MODE7_LINE;
            float m7k = 0.0f, m7gain = 1.0f;
            if (isMode7Plane)
                mode7PerspGaugeSplit((int)GPU3DS.stereoMode7Persp, &m7k, &m7gain);
            gpu3dsSetMode7Persp(m7k, m7gain, 0.0f, 0.0f);

            GPU3DS.currentRenderState.depthTest = id < LAYER_OBJ ? SGPU_STATE_ENABLED : SGPU_STATE_DISABLED;

            // one pass over the layer's sections with whatever uniforms
            // (dims, atmosphere, parallax) the caller just set. Tiled
            // layers add the blur ghost passes; the composed ghost shift
            // is rounded in BOTH slider modes (Jorge's call) - a smear
            // distance gains nothing from a fraction, only the #65 tear.
            // one raw draw of the layer's sections with the CURRENT uniforms
            auto drawPass = [&]() {
                if (id >= LAYER_BACKDROP) {
                    gpu3dsDrawVerticalSectionLayer(layer, from, to);
                } else if (list->useDrawArraysForTiledLayers) {
                    gpu3dsDrawTiledLayerSingleSection(layer, &list->sections[from]);
                } else {
                    u32 bufferOffset = layer->bufferOffset + (sub ? 0 : layer->verticesByTarget[TARGET_SNES_SUB]);
                    u16 *indices = (u16 *)list->ibo + bufferOffset;
                    gpu3dsDrawTiledLayer(layer, indices, from, to);
                }
            };

            // the two blur ghost passes for whatever dims are set - the
            // composed shift rounds in BOTH slider modes (a smear distance
            // gains nothing from a fraction, only the #65 tear)
            // tierMask (per-priority blur, Jorge's report): the ghost TexEnv
            // takes its alpha from TEXTURE0 x constant to survive color-math
            // scenes, so the vertex-alpha tier dims that mask the base
            // passes do nothing here. Tiers outside the group are instead
            // shifted clean off the screen - the parallax is per tier and
            // the clipper drops them for free. NULL = every tier.
            auto drawGhostPasses = [&](float ghost, const float *tierMask) {
                if (GPU3DSExt.blurAutoOff) return;   // "Auto, Off under load" engaged
                const float offscreen = 4096.0f;
                float ghostShift[4][2];
                for (int t = 0; t < 4; t++) {
                    if (tierMask != nullptr && tierMask[t] <= 0.0f) {
                        ghostShift[t][0] = ghostShift[t][1] = offscreen;
                        continue;
                    }
                    ghostShift[t][0] = roundf(tierShift[t] + s_atmosGhostOffset);
                    ghostShift[t][1] = roundf(tierShift[t] - s_atmosGhostOffset);
                    // never collapse onto the crisp base pass
                    if (ghostShift[t][0] == tierShift[t]) ghostShift[t][0] += 1.0f;
                    if (ghostShift[t][1] == tierShift[t]) ghostShift[t][1] -= 1.0f;
                }
                // Light quality (issue #71): ONE ghost instead of two,
                // half the blur's draw cost. The ghost sits on opposite
                // sides in the two eyes - steady in time - so the smear
                // fuses BINOCULARLY. It used to alternate sides per frame
                // as well, betting on LCD persistence; on a detailed
                // background that read as the whole layer wobbling left
                // and right (Jorge's Zero Project dam savestate: 6.5% of
                // the sky changed between consecutive frames, 0% in
                // Full). Alpha is boosted to keep the perceived smear
                // weight close to Full.
                bool light = settings3DS.StereoBlurQuality == 2 ||
                    ((settings3DS.StereoBlurQuality == 0 || settings3DS.StereoBlurQuality == 3) && GPU3DSExt.blurAutoLight);
                int lightSide = GPU3DS.stereoRightPass ? 1 : 0;
                GPU3DS.stereoGhostPass = true;
                if (light) {
                    float a = ghost * 1.4f;
                    gpu3dsSetGhostAlpha(a > 0.55f ? 0.55f : a);
                    gpu3dsSetStereoParallax3(ghostShift[0][lightSide], ghostShift[1][lightSide], tierBnd01);
                    gpu3dsSetStereoParallaxHi(ghostShift[2][lightSide], ghostShift[3][lightSide], tierBnd12, tierBnd23);
                    drawPass();
                } else {
                    gpu3dsSetGhostAlpha(ghost);
                    gpu3dsSetStereoParallax3(ghostShift[0][0], ghostShift[1][0], tierBnd01);
                    gpu3dsSetStereoParallaxHi(ghostShift[2][0], ghostShift[3][0], tierBnd12, tierBnd23);
                    drawPass();
                    gpu3dsSetStereoParallax3(ghostShift[0][1], ghostShift[1][1], tierBnd01);
                    gpu3dsSetStereoParallaxHi(ghostShift[2][1], ghostShift[3][1], tierBnd12, tierBnd23);
                    drawPass();
                }
                GPU3DS.stereoGhostPass = false;
                gpu3dsSetGhostAlpha(0.0f);
                // the base parallax comes back for whoever draws next
                gpu3dsSetStereoParallax3(tierShift[0], tierShift[1], tierBnd01);
                gpu3dsSetStereoParallaxHi(tierShift[2], tierShift[3], tierBnd12, tierBnd23);
            };


            auto drawLayerContent = [&]() {
            drawPass();
            if (id < LAYER_BACKDROP && s_atmosGhostAlpha > 0.0f)
                drawGhostPasses(s_atmosGhostAlpha, nullptr);
            };

            // spotlighting one PRIORITY (issue #61 polish): the whole
            // layer must stay visible, dimmed like the rest of the scene,
            // with only the edited priority at full brightness. The TexEnv
            // dim can't split a draw and the tile TEV ignores vertex RGB,
            // so the layer draws twice: first whole and dimmed, then only
            // the edited priority redrawn bright on top (the other tiers'
            // vertex alpha goes to 0 and the NE_ZERO alpha test discards
            // them; depth GEQUAL lets the equal-depth redraw win, and the
            // OBJ layer draws with no depth test at all). Preview-only
            // cost - gameplay never sets a highlight.
            bool spotlight = s_previewHighlightLayer == (int)id &&
                s_previewHighlightPrio >= 0 && id <= LAYER_OBJ;

            // EFFECT GROUPS (Jorge's report: fade/haze/blur looked
            // per-layer, not per-priority): tiers sharing a depth share
            // one pass; when the depths split and any effect is active,
            // each group draws with ITS OWN tint and ghost strength,
            // alpha-hiding the other groups. The masks partition the
            // tiles, so nothing draws twice and color math stays exact.
            // Only tiled layers have tiers; extra passes appear only in
            // 3D with effects on AND depths split - 2D pays nothing.
            // Mode 7 effects by distance (Jorge's ask): on the plane the
            // per-layer model is wrong - one tint and one ghost width for
            // rows that span from the player's wheels to the horizon. With
            // the profile's Effects by Distance on, fog and blur grow with
            // each row's distance instead: the vertex shader writes the
            // row's fog into color.b (interpolated by the TexEnv variant)
            // and widens the ghost offset by (1 - w/256). Nothing at the
            // nearest row, the full gauge at the horizon.
            if (isMode7Plane && GPU3DS.stereoMode7Fx > 0.0f && !spotlight &&
                s_previewHighlightLayer < 0 && GPU3DS.stereoEyeIOD != 0.0f &&
                (GPU3DS.stereoFade > 0.0f || GPU3DS.stereoHaze > 0.0f || GPU3DS.stereoBlur > 0.0f)) {
                float slider = GPU3DS.stereoEyeIOD < 0.0f ? -GPU3DS.stereoEyeIOD : GPU3DS.stereoEyeIOD;
                float fog = mode7FogAmount((int)GPU3DS.stereoFade, (int)GPU3DS.stereoHaze, slider);
                float hz = GPU3DS.stereoHaze * 0.60f, fd = GPU3DS.stereoFade * 0.70f;
                float share = (fd + hz) > 0.0f ? hz / (fd + hz) : 0.0f;
                u32 fogColor = ((u32)(215.0f * share) << 16) | ((u32)(205.0f * share) << 8) | (u32)(200.0f * share);

                gpu3dsSetStereoPrioDim4(1.0f, 1.0f, 1.0f, 1.0f);
                if (fog >= 0.01f) gpu3dsApplyAtmosphereColorPerVertex(fogColor);
                else              gpu3dsApplyAtmosphereColor(0xFFFFFFFF);
                gpu3dsSetMode7Persp(m7k, m7gain, fog, 0.0f);
                drawPass();

                float blur = (GPU3DS.stereoBlur / 8.0f) * slider;
                if (blur > 0.02f && !GPU3DSExt.blurAutoOff) {
                    float ghostA = 0.25f + 0.25f * blur;
                    float off = (1.0f + 2.0f * blur) * (GPU3DSExt.render2x.enabled ? 1.875f : 1.0f) * 2.0f;
                    bool light = settings3DS.StereoBlurQuality == 2 ||
                        ((settings3DS.StereoBlurQuality == 0 || settings3DS.StereoBlurQuality == 3) && GPU3DSExt.blurAutoLight);
                    int lightSide = GPU3DS.stereoRightPass ? 1 : 0;
                    GPU3DS.stereoGhostPass = true;
                    gpu3dsSetGhostAlpha(light ? (ghostA * 1.4f > 0.55f ? 0.55f : ghostA * 1.4f) : ghostA);
                    if (light) {
                        gpu3dsSetMode7Persp(m7k, m7gain, fog, lightSide == 0 ? off : -off);
                        drawPass();
                    } else {
                        gpu3dsSetMode7Persp(m7k, m7gain, fog, off);
                        drawPass();
                        gpu3dsSetMode7Persp(m7k, m7gain, fog, -off);
                        drawPass();
                    }
                    GPU3DS.stereoGhostPass = false;
                    gpu3dsSetGhostAlpha(0.0f);
                }
                gpu3dsSetMode7Persp(m7k, m7gain, 0.0f, 0.0f);
                gpu3dsApplyAtmosphereColor(0xFFFFFFFF);
                continue;
            }

            float tierDepth[4];
            tierDepth[0] = GPU3DS.stereoLayerDepth[id];
            tierDepth[1] = GPU3DS.stereoLayerDepthP1[id];
            tierDepth[2] = (id == LAYER_OBJ) ? GPU3DS.stereoLayerDepthOBJHi[0] : tierDepth[1];
            tierDepth[3] = (id == LAYER_OBJ) ? GPU3DS.stereoLayerDepthOBJHi[1] : tierDepth[1];
            int nTiers = (id == LAYER_OBJ) ? 4 : ((int)id < LAYER_OBJ ? 2 : 0);

            u32 grpColor[4];
            float grpGhostA[4], grpGhostO[4];
            float grpDim[4][4];
            int grpCount = 0;
            bool effectsOn = GPU3DS.stereoFade > 0.0f || GPU3DS.stereoHaze > 0.0f ||
                GPU3DS.stereoBlur > 0.0f;
            if (!spotlight && s_previewHighlightLayer < 0 && effectsOn &&
                GPU3DS.stereoEyeIOD != 0.0f && nTiers > 0) {
                // merge by the effect's RESULT, not the raw depth: two
                // depths inside the focus zone produce the same no-op
                // tint and no ghosts - one pass covers both (issue #71:
                // Butoden's split screen paid a doubled section-heavy
                // pass for exactly that no-op)
                for (int t = 0; t < nTiers; t++) {
                    u32 col; float ga, go;
                    gpu3dsComputeAtmosphereForDepth(tierDepth[t], &col, &ga, &go);
                    int g;
                    for (g = 0; g < grpCount; g++)
                        if (grpColor[g] == col && grpGhostA[g] == ga &&
                            grpGhostO[g] == go) break;
                    if (g == grpCount) {
                        grpColor[grpCount] = col;
                        grpGhostA[grpCount] = ga;
                        grpGhostO[grpCount] = go;
                        for (int k = 0; k < 4; k++) grpDim[grpCount][k] = 0.0f;
                        grpCount++;
                    }
                    grpDim[g][t] = 1.0f;
                }
            }

            if (!spotlight && grpCount > 0) {
                // base passes first, one per DISTINCT TINT: groups that
                // differ only in ghost strength share their crisp draw
                // (issue #71: split-but-blur-only layers were paying an
                // extra full base pass). Ghost passes then run per ghost
                // parameter set, masked to their own tiers.
                s_atmosGhostAlpha = 0.0f;
                s_atmosGhostOffset = 0.0f;
                for (int g = 0; g < grpCount; g++) {
                    float d0 = grpDim[g][0], d1 = grpDim[g][1];
                    float d2 = grpDim[g][2], d3 = grpDim[g][3];
                    bool merged = false;
                    for (int h = 0; h < g; h++) {
                        if (grpColor[h] == grpColor[g]) { merged = true; break; }
                    }
                    if (merged)
                        continue;   // its base already drew with an earlier group
                    for (int h = g + 1; h < grpCount; h++) {
                        if (grpColor[h] != grpColor[g])
                            continue;
                        d0 = d0 > grpDim[h][0] ? d0 : grpDim[h][0];
                        d1 = d1 > grpDim[h][1] ? d1 : grpDim[h][1];
                        d2 = d2 > grpDim[h][2] ? d2 : grpDim[h][2];
                        d3 = d3 > grpDim[h][3] ? d3 : grpDim[h][3];
                    }
                    gpu3dsSetStereoPrioDim4(d0, d1, d2, d3);
                    gpu3dsApplyAtmosphereColor(grpColor[g]);
                    drawPass();
                }
                for (int g = 0; g < grpCount; g++) {
                    if (grpGhostA[g] <= 0.0f)
                        continue;
                    gpu3dsSetStereoPrioDim4(grpDim[g][0], grpDim[g][1],
                                            grpDim[g][2], grpDim[g][3]);
                    gpu3dsApplyAtmosphereColor(grpColor[g]);
                    s_atmosGhostOffset = grpGhostO[g];
                    drawGhostPasses(grpGhostA[g], grpDim[g]);
                }
                continue;
            }

            int spotlightPasses = spotlight ? 2 : 1;

            for (int sp = 0; sp < spotlightPasses; sp++) {
            if (!spotlight) {
                gpu3dsSetStereoPrioDim4(1.0f, 1.0f, 1.0f, 1.0f);
                gpu3dsSetStereoLayerAtmosphere(id);
            } else if (sp == 0) {
                // base pass: every priority, dimmed like the other layers
                gpu3dsSetStereoPrioDim4(1.0f, 1.0f, 1.0f, 1.0f);
                gpu3dsSetStereoLayerAtmosphere(id, true);
            } else {
                // bright pass: only the edited priority survives
                float dim[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                dim[s_previewHighlightPrio & 3] = 1.0f;
                gpu3dsSetStereoPrioDim4(dim[0], dim[1], dim[2], dim[3]);
                gpu3dsSetStereoLayerAtmosphere(id);
            }

            if (id == LAYER_OBJ && s_previewHighlightPal >= 0) gpu3dsSetGlowPalette(1 << s_previewHighlightPal, true);
            drawLayerContent();
            if (id == LAYER_OBJ && s_previewHighlightPal >= 0) gpu3dsSetGlowPalette(255, false);
            }   // spotlight passes
        }
        drawShadowPhase();   // a frame without colour math or brightness sections
    }

    gpu3dsSetStereoParallax(0.0f);
    gpu3dsSetStereoPrioDim(1.0f, 1.0f);
    gpu3dsResetStereoAtmosphere();   // the composites must not inherit it
}

void gpu3dsDrawMode7Texture()
{
    if (!IPPU.Mode7Prepared || !GPU3DSExt.mode7TilesModified) return;

	t3dsStartTimer(TIMER_DRAW_M7_TEXTURE);
	gpu3dsSetMode7TexturesPixelFormat(IPPU.Mode7EXTBGFlag ? GPU_RGBA4 : GPU_RGBA5551);

	GPU3DS.currentRenderState.textureBind = SNES_MODE7_TILE_CACHE;
	GPU3DS.currentRenderState.shader = SPROGRAM_MODE7;
	GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	SVertexList *list = &GPU3DS.vertices[VBO_MODE7_TILE];
    SGPUTexture *texture = &GPU3DS.textures[SNES_MODE7_FULL];

    // 3DS does not allow rendering to a viewport whose width > 512
    // so our 1024x1024 texture is split into 4 512x512 parts
	GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_FULL;

	for (int section = 0; section < 4; section++)
	{
		if (GPU3DSExt.mode7SectionsModified[section])
		{
			GPU3DSExt.mode7SectionsModified[section] = false;

			// invalidate so next gpu3dsApplyRenderState re-applies the target (framebuf address changes per section)
			GPU3DS.appliedRenderState.target = TARGET_UNSET;
			int addressOffset = ((3 - section) * 0x40000) * gpu3dsGetPixelSize(texture->tex.fmt);
    		texture->target->frameBuf.colorBuf = (void *)((int)texture->tex.data + addressOffset);

			gpu3dsDraw(list, NULL, 4096, 4096 * section);
		}
	}

	GPU3DSExt.mode7TilesModified = false;

	// Citra workaround: 
    // without this, drawing all 4 sections above leaves the Mode 7 texture blank on Citra.
	// Force Citra to flush the surface to memory; 16 bytes seems enough here
	if (!GPU3DS.isReal3DS)
	{
		C3D_SyncTextureCopy(
			(u32 *)texture->tex.data, 0,
			(u32 *)texture->tex.data, 0,
			16, 8);
	}

	// SNES_MODE7_TILE_0 is only sampled when Mode7Repeat is 3 ("repeat tile 0
	// across the plane"). Modes 0 and 2 (wrap / fill with colour 0) don't
	// sample it. Skipping the bake saves one draw + one render-target switch
	// every Mode 7 frame on those modes.
	if (PPU.Mode7Repeat == 3)
	{
		GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_TILE_0;
		gpu3dsDraw(list, NULL, 4, 16384);
	}

	// re-bind our tile shader
	GPU3DS.currentRenderState.shader = SPROGRAM_TILES;

	t3dsStopTimer(TIMER_DRAW_M7_TEXTURE);

    gpu3dsIncrementMode7UpdateFrameCount();
}

void gpu3dsPrepareSnesScreenForNextFrame() {
    SLayerList *list = &GPU3DSExt.layerList;

    if (list->hasSkippedSections) {
        gpu3dsAdjustLayerSectionLimits(list);
        list->hasSkippedSections = false;
    }
    
    gpu3dsResetLayers(list);

    // flip snes VBOs to the alternate half of the buffer
    // make sure this is called BEFORE S9xMainLoop so that vertex writes go to different memory
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_RECT], true);
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_TILE], true);
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_OBJ], true);
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_MODE7_LINE], true);

    if (GPU3DSExt.render2x.dirty) {
        gpu3dsClearTexture(&GPU3DS.textures[SNES_MAIN], 0);
        gpu3dsClearTexture(&GPU3DS.textures[SNES_SUB], 0);
        gpu3dsClearTexture(&GPU3DS.textures[SNES_DEPTH], 0);
        // the retained right-eye texture keeps a stale frame at the OLD
        // render width across Enhanced Resolution changes; the composite
        // then shows it misscaled on the right eye (issue #9)
        if (GPU3DS.stereoTexAvailable)
            gpu3dsClearTexture(&GPU3DS.textures[SNES_MAIN_RIGHT], 0);
        GPU3DSExt.render2x.dirty = false;
    }
}

void gpu3dsDrawSnesScreen() {
    SLayerList *list = &GPU3DSExt.layerList;

    if (!list->verticesTotal || list->hasSkippedSections)
        return;

    list->anythingOnSub = false;
    list->useDrawArraysForTiledLayers = true;
    list->layersTotalByTarget[TARGET_SNES_SUB] = 0;
    list->layersTotalByTarget[TARGET_SNES_MAIN] = 0;

    LAYER_ID drawOrder[8] = {
        LAYER_BACKDROP,
        LAYER_OBJ,
        LAYER_BG0,
        LAYER_BG1,
        LAYER_BG2,
        LAYER_BG3,
        LAYER_COLOR_MATH,
        LAYER_BRIGHTNESS,
    };

    u32 bufferOffset = 0;
    
    for (int i = 0; i < 8; i++) {
        LAYER_ID id = drawOrder[i];

        SLayer *layer = &list->layers[id];
        
        u16 verticesOnSub = layer->verticesByTarget[TARGET_SNES_SUB];
        u16 verticesOnMain = layer->verticesByTarget[TARGET_SNES_MAIN];

        int verticesTotal = verticesOnSub + verticesOnMain;

        if (!verticesTotal) {
            continue;
        }

        if (verticesOnMain) {
            list->layersByTarget[TARGET_SNES_MAIN][list->layersTotalByTarget[TARGET_SNES_MAIN]++] = id;
        }

        if (verticesOnSub) {
            list->layersByTarget[TARGET_SNES_SUB][list->layersTotalByTarget[TARGET_SNES_SUB]++] = id;
            list->anythingOnSub = true;
        }


        if ((verticesOnMain && verticesOnSub) || layer->m7Tile0) {
            sortSections(list->sections + layer->sectionsOffset, layer->sectionsTotal, layer->m7Tile0);
        }
        
        if (id < LAYER_BACKDROP)
        {
            if (layer->sectionsByTarget[TARGET_SNES_MAIN] > 1 || layer->sectionsByTarget[TARGET_SNES_SUB] > 1)
                list->useDrawArraysForTiledLayers = false;

            layer->bufferOffset = bufferOffset;
            bufferOffset += verticesTotal;
        }
    }

    gpu3dsDrawMode7Texture();
    gpu3dsDrawLayers(list);
}

void gpu3dsCommitLayerSection(SGPU_VBO_ID vboId, LAYER_ID id, SGPURenderState *state, bool sub, bool reuseVertices) {
    SLayerList *list = &GPU3DSExt.layerList;
    SLayer *layer = &list->layers[id];

    if (layer->sectionsTotal >= layer->sectionsMax) {
        // skip current frame + count all the skipped sections 
        // to handle layer section limits for the next frame later on (gpu3dsAdjustLayerSectionLimits())
        //
        // This case should rarely happen (and never for LAYER_WINDOW_LR, LAYER_BRIGHTNESS, LAYER_BACKDROP)
        // If at all, it occurs when "In-Frame Pallete Changes" setting is set to "Enabled"
        layer->sectionsSkipped++;
        list->hasSkippedSections = true;
    }   

    int sectionIdx = layer->sectionsOffset + layer->sectionsTotal;

    if (!reuseVertices) 
    {
        SVertexList *vbo = &GPU3DS.vertices[vboId];
        u16 currentIdx = vbo->from;
        u16 currentVerticesCount = gpu3dsGetValueWithinLimit(vbo->count, list->verticesTotal, MAX_VERTICES);

        vbo->from += vbo->count;
        vbo->count = 0;

        // max sections/vertices overflow
        if (list->hasSkippedSections || !currentVerticesCount) return;

        SLayerSection *section = &list->sections[sectionIdx];

        section->state = *state;
        section->from = currentIdx;
        section->count = currentVerticesCount;
        section->vboId = vboId;
        section->onSub = sub;

        if (state->textureBind == SNES_MODE7_TILE_0) {
            layer->m7Tile0 = true;
        }
        
        layer->verticesByTarget[sub] += currentVerticesCount;
        layer->sectionsByTarget[sub]++;
        layer->sectionsTotal++;
        
        list->verticesTotal += currentVerticesCount;
    
        return;
    }

    int prevSectionIndex = sectionIdx - 1;

    if (prevSectionIndex >= 0 && !list->hasSkippedSections)
    {
        SLayerSection *section = &list->sections[sectionIdx]; // reuse last section properties

        *section = list->sections[prevSectionIndex];
        
        if (!section->count) return;

        section->state = *state;
        section->onSub = false; // reuse only happens on main
        
        layer->sectionsByTarget[TARGET_SNES_MAIN]++;
        layer->verticesByTarget[TARGET_SNES_MAIN] += section->count;
        layer->sectionsTotal++;
    }
}

void gpu3dsInitializeMode7Vertex(int idx, s16 x, s16 y)
{
    s16 x0 = 0;
    s16 y0 = 0;

    if (x < 64)
    {
        x0 = x * 8;
        y0 = (y * 2 + 1) * 8;
    }
    else
    {
        x0 = (x - 64) * 8;
        y0 = (y * 2) * 8;
    }

    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position = (SVector4i){x0, y0, 0, -1};
}

void gpu3dsInitializeMode7VertexForTile0(int idx, s16 x, s16 y)
{
    s16 x0 = x;
    s16 y0 = y;

    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];
    
    m7vertices[0].Position = (SVector4i){x0, y0, 0, 0x3fff};
}

void gpu3dsInitializeMode7Vertexes()
{
    GPU3DSExt.mode7FrameCount = 3;
    for (int f = 0; f < 2; f++)
    {
        int idx = 0;
        for (int section = 0; section < 4; section++)
        {
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 128; x++)
                    gpu3dsInitializeMode7Vertex(idx++, x, y);
        }

        gpu3dsInitializeMode7VertexForTile0(16384, 0, 0);
        gpu3dsInitializeMode7VertexForTile0(16385, 0, 8);
        gpu3dsInitializeMode7VertexForTile0(16386, 8, 0);
        gpu3dsInitializeMode7VertexForTile0(16387, 8, 8);

        gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_MODE7_TILE], true);
    }

	gpu3dsCopyVRAMTilesIntoMode7TileVertexes(Memory.VRAM);
}

// Changes the texture pixel format (but it must be the same 
// size as the original pixel format). No errors will be thrown
// if the format is incorrect.
//

void gpu3dsSetMode7TexturesPixelFormat(GPU_TEXCOLOR fmt)
{
    if (GPU3DSExt.mode7TextureFormat == fmt)
        return;

    GPU3DSExt.mode7TextureFormat = fmt;
    GPU3DS.textures[SNES_MODE7_FULL].tex.fmt = fmt;
    GPU3DS.textures[SNES_MODE7_TILE_0].tex.fmt = fmt;
    GPU3DS.textures[SNES_MODE7_TILE_CACHE].tex.fmt = fmt;

    GPU_COLORBUF colorFmt = (GPU_COLORBUF)gpu3dsGetFrameBufferFmt(fmt);
    GPU3DS.textures[SNES_MODE7_FULL].target->frameBuf.colorFmt = colorFmt;
    GPU3DS.textures[SNES_MODE7_TILE_0].target->frameBuf.colorFmt = colorFmt;
}

void gpu3dsCopyVRAMTilesIntoMode7TileVertexes(uint8 *VRAM)
{
    for (int i = 0; i < 16384; i++)
    {
        gpu3dsSetMode7TileModified(i, VRAM[i * 2]);
    }
    IPPU.Mode7CharDirtyFlagCount = 1;
    for (int i = 0; i < 256; i++)
    {
        IPPU.Mode7CharDirtyFlag[i] = 2;
    }
    S9xMode7MapInvalidate ();
}

void gpu3dsIncrementMode7UpdateFrameCount()
{
    gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_MODE7_TILE], true);
    GPU3DSExt.mode7FrameCount ++;

    if (GPU3DSExt.mode7FrameCount == 0x3fff)
    {
        GPU3DSExt.mode7FrameCount = 1;
    }

    // Bug fix: Clears the updateFrameCount of both sets
    // of mode7TileVertexes!
    //
    if (GPU3DSExt.mode7FrameCount <= 2)
    {
        SMode7TileVertex* vertices = (SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data;

        for (int i = 0; i < 16384; )
        {
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;

            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
        }
    }
}

void gpu3dsAddQuadRect(float x0, float y0, float x1, float y1, u16 wx, u16 wy, int z, u32 fillColor, u32 borderColor, u8 borderSize) 
{
    if (borderSize > 0) {
        float cx0 = x0 + borderSize;
        float cy0 = y0 + borderSize;
        float cx1 = x1 - borderSize;
        float cy1 = y1 - borderSize;
        
        // top, bottom left, right
        gpu3dsAddSimpleQuadVertexes(x0, y0, x1, cy0, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(x0, cy1, x1, y1, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(x0, cy0, cx0, cy1, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(cx1, cy0, x1, cy1, wx, wy, wx, wy, z, borderColor);

        gpu3dsAddSimpleQuadVertexes(cx0, cy0, cx1, cy1, wx, wy, wx, wy, z, fillColor);
    } else {
        gpu3dsAddSimpleQuadVertexes(x0, y0, x1, y1, wx, wy, wx, wy, z, fillColor);
    }
}
