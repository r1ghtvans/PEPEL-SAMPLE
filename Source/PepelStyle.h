#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include <cmath>
#include <cstring>
#include "Sprites.h"

// 8-bit anime + VHS look: palette, fonts, pixel helpers, panels.
namespace pepel
{
using juce::Colour;
inline Colour hex (juce::uint32 v) { return Colour (0xff000000u | v); }

// night
inline const Colour ink = hex (0x040533), night = hex (0x08095B), indigo = hex (0x232284), violetDk = hex (0x362A8E),
                    mist = hex (0x60579E), glow = hex (0xB7A6F0), moon = hex (0xEDE4FF), starBlue = hex (0x4F8FD0);
// blood
inline const Colour crimson = hex (0xC10F1D), blood = hex (0xE93426), coral = hex (0xE8634A), peach = hex (0xEEA380),
                    maroon = hex (0x730426), deepRed = hex (0x530121);
// panels
inline const Colour paper = hex (0x0B0C46), track = hex (0x1D1F78), text = hex (0xE9E4FF), textSoft = hex (0xEEA380);
inline const Colour frameBlue = hex (0x4F72E6), frameViolet = hex (0x7F6FE0), violet = hex (0x7F6FE0);
inline const Colour cyan = hex (0x7FE7FF), pink = hex (0xFF7FBF), redGl = hex (0xFF4A5A);

inline Colour panelColour (int i)
{
    const Colour c[4] = { frameBlue, blood, frameViolet, coral };
    return c[((i % 4) + 4) % 4];
}

// ---- fonts (embedded, OFL)
inline juce::Typeface::Ptr& vtFace()
{
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, (size_t) BinaryData::VT323Regular_ttfSize);
    return t;
}
inline juce::Typeface::Ptr& psFace()
{
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (BinaryData::PressStart2PRegular_ttf, (size_t) BinaryData::PressStart2PRegular_ttfSize);
    return t;
}
inline juce::Font vt (float h) { return juce::Font (juce::FontOptions (vtFace()).withHeight (h)); }
inline juce::Font ps (float h) { return juce::Font (juce::FontOptions (psFace()).withHeight (h)); }

// ---- gradients (banded, never smooth)
struct Stop { float t; Colour c; };

template <size_t N>
inline Colour gradientAt (const Stop (&s)[N], float t)
{
    if (t <= s[0].t) return s[0].c;
    for (size_t i = 1; i < N; ++i)
        if (t <= s[i].t)
            return s[i - 1].c.interpolatedWith (s[i].c, (t - s[i - 1].t) / juce::jmax (1.0e-4f, s[i].t - s[i - 1].t));
    return s[N - 1].c;
}

template <size_t N>
inline void bandedGradient (juce::Graphics& g, juce::Rectangle<int> r, const Stop (&stops)[N], int band = 6, int steps = 14)
{
    for (int y = 0; y < r.getHeight(); y += band)
    {
        const float t = ((float) y + (float) band * 0.5f) / (float) r.getHeight();
        const float q = std::floor (juce::jlimit (0.f, 0.9999f, t) * (float) steps) / (float) (steps - 1);
        g.setColour (gradientAt (stops, juce::jmin (1.f, q)));
        g.fillRect (r.getX(), r.getY() + y, r.getWidth(), juce::jmin (band, r.getHeight() - y));
    }
}

// ---- sprites: array of strings, '.' = empty, integer scale
template <size_t N>
inline int spriteW (const char* const (&rows)[N]) { return (int) std::strlen (rows[0]); }

template <size_t N>
inline void drawSprite (juce::Graphics& g, const char* const (&rows)[N], int x, int y, int scale, Colour c)
{
    g.setColour (c);
    for (size_t j = 0; j < N; ++j)
    {
        const char* row = rows[j];
        const int w = (int) std::strlen (row);
        int i = 0;
        while (i < w)
        {
            if (row[i] == '.') { ++i; continue; }
            const int s = i;
            while (i < w && row[i] != '.') ++i;
            g.fillRect (x + s * scale, y + (int) j * scale, (i - s) * scale, scale);
        }
    }
}

// ---- panels: stepped corners (step 3 px), double frame 3+2+2, hard shadow +4
inline juce::Path stepPath (juce::Rectangle<int> r, int s = 3)
{
    const float x = (float) r.getX(), y = (float) r.getY(), w = (float) r.getWidth(), h = (float) r.getHeight(), k = (float) s;
    juce::Path p;
    p.startNewSubPath (x + 2 * k, y);
    p.lineTo (x + w - 2 * k, y);     p.lineTo (x + w - 2 * k, y + k);
    p.lineTo (x + w - k, y + k);     p.lineTo (x + w - k, y + 2 * k);   p.lineTo (x + w, y + 2 * k);
    p.lineTo (x + w, y + h - 2 * k); p.lineTo (x + w - k, y + h - 2 * k);
    p.lineTo (x + w - k, y + h - k); p.lineTo (x + w - 2 * k, y + h - k); p.lineTo (x + w - 2 * k, y + h);
    p.lineTo (x + 2 * k, y + h);     p.lineTo (x + 2 * k, y + h - k);
    p.lineTo (x + k, y + h - k);     p.lineTo (x + k, y + h - 2 * k);   p.lineTo (x, y + h - 2 * k);
    p.lineTo (x, y + 2 * k);         p.lineTo (x + k, y + 2 * k);
    p.lineTo (x + k, y + k);         p.lineTo (x + 2 * k, y + k);
    p.closeSubPath();
    return p;
}

inline void drawPanelShadow (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (ink.withAlpha (0.55f));
    g.fillPath (stepPath (r.translated (4, 4)));
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, Colour c, const juce::String& title = {},
                       bool header = true, bool shadow = true)
{
    if (shadow) drawPanelShadow (g, r);
    g.setColour (c);     g.fillPath (stepPath (r));
    g.setColour (paper); g.fillPath (stepPath (r.reduced (3)));
    g.setColour (c);     g.fillPath (stepPath (r.reduced (5)));
    g.setColour (paper); g.fillPath (stepPath (r.reduced (7)));

    if (header)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (stepPath (r.reduced (7)));
        const juce::Rectangle<int> strip (r.getX() + 7, r.getY() + 7, r.getWidth() - 14, 29);
        g.setColour (c.withAlpha (0.45f));
        g.fillRect (strip);
        drawSprite (g, kHeart, strip.getX() + 8, strip.getCentreY() - 6, 2, c.brighter (0.5f));
        g.setColour (text);
        g.setFont (ps (13.f));
        g.drawText (title, strip.getX() + 8 + 14 + 8, strip.getY(), strip.getWidth() - 40, strip.getHeight(),
                    juce::Justification::centredLeft, false);
    }
}

inline juce::Rectangle<int> rowRect (juce::Rectangle<int> panel, int k)
{
    return { panel.getX() + 10, panel.getY() + 42 + 30 * k, panel.getWidth() - 20, 30 };
}
} // namespace pepel
