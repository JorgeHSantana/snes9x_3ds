
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"
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
// which of the layer's depth tiers smear in the ghost passes: since the
// priority split (issue #60) each tier sits at its own depth, so only
// the tiers actually outside the focus zone blur - the in-zone ones are
// alpha-hidden during the ghost passes and stay crisp
static bool s_atmosGhostTierOn[4] = { true, true, true, true };

static void gpu3dsResetStereoAtmosphere()
{
    C3D_TexEnvInit(C3D_GetTexEnv(2));
    C3D_TexEnvInit(C3D_GetTexEnv(3));
    s_atmosLastColor = 0xFFFFFFFF;
    s_atmosGhostAlpha = 0.0f;
    s_atmosGhostOffset = 0.0f;
    for (int t = 0; t < 4; t++) s_atmosGhostTierOn[t] = true;
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

// 3D-tab editor preview (issue #61): while >= 0, every layer except
// this one is dimmed hard so the edited layer reads instantly.
static int s_previewHighlightLayer = -1;
static int s_previewHighlightPrio = -1;   // -1 = whole layer

void gpu3dsSetStereoPreviewHighlight(int layerId, int prio)
{
    s_previewHighlightLayer = layerId;
    s_previewHighlightPrio = prio;
    s_atmosLastColor = 1;               // poison the dedup so envs re-apply
}

// forceOthers: treat this layer as "one of the others" (dimmed) even when
// it is the highlighted one - the spotlight's base pass uses it so the
// whole layer stays visible under the bright redraw of the edited priority
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

    // all cues anchor on the focus zone: layers inside it are untouched.
    // Relative model: the layer farthest outside gets the full gauge
    // strength, the ones in between scale linearly by their share.
    // Fade/haze count only the distance BEHIND the zone (distance cues).
    float depth = GPU3DS.stereoLayerDepth[id];
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
    // Per-TIER since the priority split (issue #60): each tier's own
    // distance to the zone decides whether ITS tiles smear (a front
    // P1 blurs while its in-zone P0 stays crisp); the pass strength
    // follows the farthest tier.
    float tierDepth[4];
    tierDepth[0] = depth;
    tierDepth[1] = GPU3DS.stereoLayerDepthP1[id];
    tierDepth[2] = (id == LAYER_OBJ) ? GPU3DS.stereoLayerDepthOBJHi[0] : tierDepth[1];
    tierDepth[3] = (id == LAYER_OBJ) ? GPU3DS.stereoLayerDepthOBJHi[1] : tierDepth[1];

    float excess = 0.0f;
    for (int t = 0; t < 4; t++) {
        float ex = 0.0f;
        if (tierDepth[t] < GPU3DS.stereoFocusBack)
            ex = GPU3DS.stereoFocusBack - tierDepth[t];
        else if (tierDepth[t] > GPU3DS.stereoFocusFront)
            ex = tierDepth[t] - GPU3DS.stereoFocusFront;
        s_atmosGhostTierOn[t] = ex > 0.001f;
        if (ex > excess) excess = ex;
    }

    float excessIn = GPU3DS.stereoMaxExcess > 0.0f ? excess / GPU3DS.stereoMaxExcess : 0.0f;
    float blur = (GPU3DS.stereoBlur / 8.0f) * excessIn * slider;
    if (blur > 0.02f) {
        s_atmosGhostAlpha = 0.25f + 0.25f * blur;
        // under Enhanced Resolution the aura grows 1.875x (not the full 2x
        // of the parallax scale) to track the stronger depth there
        s_atmosGhostOffset = (1.0f + 2.0f * blur)
            * (GPU3DSExt.render2x.enabled ? 1.875f : 1.0f);
    } else {
        s_atmosGhostAlpha = 0.0f;
        s_atmosGhostOffset = 0.0f;
    }

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

void gpu3dsDrawLayers(SLayerList *list) {
    // draw window_lr into depth buffer first (screen-space: no parallax)
    SLayer *layer = &list->layers[LAYER_WINDOW_LR];

    gpu3dsSetStereoParallax(0.0f);
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

        for (int j = 0; j < list->layersTotalByTarget[i]; j++) {
            LAYER_ID id = list->layersByTarget[i][j];
            SLayer *layer = &list->layers[id];

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

            GPU3DS.currentRenderState.depthTest = id < LAYER_OBJ ? SGPU_STATE_ENABLED : SGPU_STATE_DISABLED;

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

            if (id < LAYER_BACKDROP) {
                // hazy layers draw 2 extra ghost passes shifted +-1px with
                // reduced alpha (a cheap box blur: soft "smoky" edges)
                float ghost = s_atmosGhostAlpha;
                int passes = ghost > 0.0f ? 3 : 1;

                // the ghost offset is fractional (1..3px growing with the
                // blur level), and a fractional shift rasterizes with mixed
                // per-section rounding - the #65 split, showing here as
                // torn/doubled pixels at some blur levels. The COMPOSED
                // ghost shift is therefore rounded in BOTH slider modes
                // (Jorge's call): Continuous exists for the analog feel of
                // layer placement, but a smear distance gains nothing from
                // a fraction - only the tear.
                float ghostShift[4][2];
                for (int t = 0; t < 4; t++) {
                    ghostShift[t][0] = roundf(tierShift[t] + s_atmosGhostOffset);
                    ghostShift[t][1] = roundf(tierShift[t] - s_atmosGhostOffset);
                    // never collapse onto the crisp base pass
                    if (ghostShift[t][0] == tierShift[t]) ghostShift[t][0] += 1.0f;
                    if (ghostShift[t][1] == tierShift[t]) ghostShift[t][1] -= 1.0f;
                }

                for (int gp = 0; gp < passes; gp++) {
                    if (gp == 1) {
                        GPU3DS.stereoGhostPass = true;
                        gpu3dsSetGhostAlpha(ghost);
                        // only the tiers outside the focus zone smear;
                        // the in-zone ones already drew crisp in pass 0
                        gpu3dsSetStereoPrioDim4(
                            s_atmosGhostTierOn[0] ? 1.0f : 0.0f,
                            s_atmosGhostTierOn[1] ? 1.0f : 0.0f,
                            s_atmosGhostTierOn[2] ? 1.0f : 0.0f,
                            s_atmosGhostTierOn[3] ? 1.0f : 0.0f);
                        gpu3dsSetStereoParallax3(ghostShift[0][0],
                            ghostShift[1][0], tierBnd01);
                        gpu3dsSetStereoParallaxHi(ghostShift[2][0],
                            ghostShift[3][0], tierBnd12, tierBnd23);
                    } else if (gp == 2) {
                        gpu3dsSetStereoParallax3(ghostShift[0][1],
                            ghostShift[1][1], tierBnd01);
                        gpu3dsSetStereoParallaxHi(ghostShift[2][1],
                            ghostShift[3][1], tierBnd12, tierBnd23);
                    }

                    if (list->useDrawArraysForTiledLayers) {
                        gpu3dsDrawTiledLayerSingleSection(layer, &list->sections[from]);
                    }
                    else {
                        u32 bufferOffset = layer->bufferOffset + (sub ? 0 : layer->verticesByTarget[TARGET_SNES_SUB]);
                        u16 *indices = (u16 *)list->ibo + bufferOffset;
                        gpu3dsDrawTiledLayer(layer, indices, from, to);
                    }
                }

                if (passes > 1) {
                    GPU3DS.stereoGhostPass = false;
                    gpu3dsSetGhostAlpha(0.0f);
                }
            }
            else {
                gpu3dsDrawVerticalSectionLayer(layer, from, to);
            }
            }   // spotlight passes
        }
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
