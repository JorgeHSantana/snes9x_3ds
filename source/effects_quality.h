#ifndef EFFECTS_QUALITY_H
#define EFFECTS_QUALITY_H

// One "Effects Quality" picker over the three Auto controllers (Performance
// section of the game tab): the shadow Quality (0 Full, 1 Auto, 2 Auto Off
// under load - stereo profile field), the glow Quality (0 Full, 1 Auto -
// profile field) and the Blur Quality (0 Auto, 3 Auto Off under load,
// 1 Full, 2 Light - settings3DS.StereoBlurQuality). Each preset is one
// triple; a triple that matches none reads as Mixed, and picking Mixed
// changes nothing (the per-effect pickers in the 3D tab keep their say).
enum {
    EFFECTS_QUALITY_FULL = 0,
    EFFECTS_QUALITY_AUTO = 1,
    EFFECTS_QUALITY_AUTO_OFF = 2,   // the controllers may switch an effect off under load
    EFFECTS_QUALITY_LIGHT = 3,      // the lightest fixed look: one blur ghost, Auto elsewhere
    EFFECTS_QUALITY_MIXED = 4,
    EFFECTS_QUALITY_COUNT = 5
};

static inline bool effects_quality_values(int preset, int *shadow, int *glow, int *blur)
{
    switch (preset) {
    case EFFECTS_QUALITY_FULL:     *shadow = 0; *glow = 0; *blur = 1; return true;
    case EFFECTS_QUALITY_AUTO:     *shadow = 1; *glow = 1; *blur = 0; return true;
    case EFFECTS_QUALITY_AUTO_OFF: *shadow = 2; *glow = 1; *blur = 3; return true;
    case EFFECTS_QUALITY_LIGHT:    *shadow = 1; *glow = 1; *blur = 2; return true;
    default: return false;
    }
}

static inline int effects_quality_preset(int shadow, int glow, int blur)
{
    for (int p = 0; p < EFFECTS_QUALITY_MIXED; p++) {
        int s, g, b;
        if (effects_quality_values(p, &s, &g, &b) && s == shadow && g == glow && b == blur) return p;
    }
    return EFFECTS_QUALITY_MIXED;
}

#endif
