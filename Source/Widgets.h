#pragma once
#include "PepelStyle.h"
#include "PluginProcessor.h"
#include <functional>
#include <vector>

namespace pepel
{
//==============================================================================
// One parameter row: name | segmented slider (no knob) | value.
// Kinds: Slider, Toggle (click), Choice (click cycles), Button (action).
class SegRow : public juce::Component
{
public:
    enum class Kind { Slider, Toggle, Choice, Button };

    SegRow (Kind k, const juce::String& n, int nameWidth = 100) : kind (k), name (n), nameW (nameWidth)
    {
        setBufferedToImage (true);
    }

    std::function<void()> onClick;

    void setButtonText (const juce::String& t)
    {
        if (t != buttonText) { buttonText = t; repaint(); }
    }

    void bind (juce::RangedAudioParameter* p)
    {
        attach.reset();
        param = p;
        if (p == nullptr) { repaint(); return; }
        attach = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            norm = param->convertTo0to1 (v);
            valueText = param->getText (norm, 16).toUpperCase();
            repaint();
        });
        attach->sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight();
        g.setFont (vt (25.f));
        g.setColour (text);
        g.drawText (name, 0, 0, nameW, h, juce::Justification::centredLeft, false);

        if (kind == Kind::Button)
        {
            const juce::Rectangle<int> r (nameW + 4, 3, w - nameW - 4, h - 6);
            g.setColour (ink);
            g.fillRect (r);
            g.setColour (pressed ? coral : crimson);
            g.fillRect (r.reduced (2));
            g.setColour (pressed ? ink : moon);
            g.drawText (buttonText, r.reduced (4, 0), juce::Justification::centred, true);
            return;
        }

        const int valueW = 80;
        const int sx = nameW + 4, sw = w - nameW - valueW - 8;
        const int nSeg = (sw + 2) / 8;
        const int filled = juce::roundToInt (norm * (float) nSeg);
        const int y = (h - 14) / 2;

        for (int i = 0; i < nSeg; ++i)
        {
            const int x = sx + i * 8;
            if (i < filled)
            {
                const float t = nSeg > 1 ? (float) i / (float) (nSeg - 1) : 0.f;
                const Colour c = t < 0.5f ? blood.interpolatedWith (violet, t * 2.f)
                                          : violet.interpolatedWith (starBlue, (t - 0.5f) * 2.f);
                g.setColour (c);
                g.fillRect (x, y, 6, 14);
                g.setColour (c.brighter (0.45f));
                g.fillRect (x, y, 6, 2);
            }
            else
            {
                g.setColour (track);
                g.fillRect (x, y, 6, 14);
            }
        }

        // end-of-fill marker: moon block with 2 px ink frame
        const int cx = filled > 0 ? sx + filled * 8 - 2 : sx;
        const int bx = juce::jlimit (sx - 5, sx + sw - 5, cx - 5);
        g.setColour (ink);
        g.fillRect (bx, y - 2, 10, 18);
        g.setColour (moon);
        g.fillRect (bx + 2, y, 6, 14);

        g.setColour (textSoft);
        g.drawText (valueText, w - valueW, 0, valueW, h, juce::Justification::centredRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (kind == Kind::Button)
        {
            pressed = true; repaint();
            return;
        }
        if (attach == nullptr) return;

        if (kind == Kind::Slider)
        {
            dragging = true;
            attach->beginGesture();
            setFromX (e.x);
        }
        else if (kind == Kind::Toggle)
            attach->setValueAsCompleteGesture (norm > 0.5f ? 0.f : 1.f);
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            const int n = c->choices.size();
            const int idx = (juce::roundToInt (param->convertFrom0to1 (norm)) + 1) % juce::jmax (1, n);
            attach->setValueAsCompleteGesture ((float) idx);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFromX (e.x); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (kind == Kind::Button)
        {
            const bool inside = getLocalBounds().contains (e.getPosition());
            pressed = false; repaint();
            if (inside && onClick) onClick();
        }
        else if (dragging)
        {
            dragging = false;
            attach->endGesture();
        }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override // reset to default
    {
        if (kind != Kind::Slider || attach == nullptr) return;
        const float d = param->convertFrom0to1 (param->getDefaultValue());
        if (dragging) attach->setValueAsPartOfGesture (d);
        else attach->setValueAsCompleteGesture (d);
    }

private:
    void setFromX (int x)
    {
        if (attach == nullptr) return;
        const int sx = nameW + 4, sw = getWidth() - nameW - 80 - 8;
        const float n = juce::jlimit (0.f, 1.f, (float) (x - sx) / (float) sw);
        attach->setValueAsPartOfGesture (param->convertFrom0to1 (n));
    }

    Kind kind;
    juce::String name, valueText, buttonText;
    int nameW;
    float norm = 0.f;
    bool pressed = false, dragging = false;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attach;
};

//==============================================================================
class WaveView : public juce::Component
{
public:
    explicit WaveView (PepelVoxProcessor& p) : proc (p) {}
    std::function<void (int)> onSelect;
    std::function<void()> onLoadRequest;

    void refreshData()
    {
        auto s = proc.getSample();
        if (s != sample) { sample = s; buildPeaks(); }
        slices = proc.getSlices();
        selected = proc.selectedSlice.load();
        for (size_t i = 0; i < heads.size(); ++i) heads[i] = proc.voiceHead[i].load();
        repaint();
    }

    void resized() override { buildPeaks(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds();
        const auto title = sample != nullptr ? sample->name.toUpperCase()
                                             : juce::String::fromUTF8 ("ВОЛНА");
        drawPanel (g, r, coral, title, true, false);

        const auto a = area();
        g.setColour (ink);
        g.fillRect (a);

        if (sample == nullptr || slices == nullptr)
        {
            g.setColour (glow);
            g.setFont (ps (13.f));
            g.drawText (juce::String::fromUTF8 ("ПЕРЕТАЩИ ЗВУК СЮДА ИЛИ НАЖМИ"), a, juce::Justification::centred, false);
            return;
        }

        const int total = juce::jmax (1, sample->audio.getNumSamples());
        const auto& b = slices->bounds;
        auto xOf = [&] (int s) { return a.getX() + (int) ((juce::int64) s * a.getWidth() / total); };

        // selected slice highlight
        if (selected + 1 < (int) b.size())
        {
            g.setColour (indigo);
            g.fillRect (xOf (b[(size_t) selected]), a.getY(), xOf (b[(size_t) selected + 1]) - xOf (b[(size_t) selected]), a.getHeight());
        }

        // centre line
        g.setColour (track);
        g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 2);

        // bars (2 px wide)
        const int half = a.getHeight() / 2 - 4;
        for (size_t c = 0; c < mins.size(); ++c)
        {
            const int x = a.getX() + (int) c * 2;
            const int sPos = (int) ((juce::int64) c * total / (juce::int64) juce::jmax<size_t> (1, mins.size()));
            const bool inSel = selected + 1 < (int) b.size() && sPos >= b[(size_t) selected] && sPos < b[(size_t) selected + 1];
            const int top = a.getCentreY() - (int) (maxs[c] * (float) half);
            const int bot = a.getCentreY() - (int) (mins[c] * (float) half);
            g.setColour (inSel ? moon : mist);
            g.fillRect (x, top, 2, juce::jmax (2, bot - top));
        }

        // slice markers + numbers
        g.setFont (vt (22.f));
        for (size_t i = 0; i + 1 < b.size(); ++i)
        {
            const int x = xOf (b[i]);
            g.setColour (blood);
            g.fillRect (x, a.getY(), 2, a.getHeight());
            g.setColour (ink);
            g.drawText (juce::String ((int) i + 1), x + 6, a.getY() + 3, 40, 20, juce::Justification::centredLeft, false);
            g.setColour ((int) i == selected ? moon : peach);
            g.drawText (juce::String ((int) i + 1), x + 5, a.getY() + 2, 40, 20, juce::Justification::centredLeft, false);
        }

        // play heads
        for (int h : heads)
            if (h >= 0)
            {
                g.setColour (glow);
                g.fillRect (xOf (h) - 1, a.getY(), 4, a.getHeight());
                g.setColour (moon);
                g.fillRect (xOf (h), a.getY(), 2, a.getHeight());
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sample == nullptr || slices == nullptr)
        {
            if (onLoadRequest) onLoadRequest();
            return;
        }
        const auto a = area();
        if (! a.contains (e.getPosition())) return;
        const int total = juce::jmax (1, sample->audio.getNumSamples());
        const int pos = (int) ((juce::int64) (e.x - a.getX()) * total / juce::jmax (1, a.getWidth()));
        const auto& b = slices->bounds;
        for (size_t i = 0; i + 1 < b.size(); ++i)
            if (pos >= b[i] && pos < b[i + 1])
            {
                if (onSelect) onSelect ((int) i);
                proc.auditionSlice ((int) i);
                break;
            }
    }

private:
    juce::Rectangle<int> area() const { return { 14, 44, getWidth() - 28, getHeight() - 44 - 14 }; }

    void buildPeaks()
    {
        mins.clear(); maxs.clear();
        if (sample == nullptr) return;
        const int cols = juce::jmax (1, area().getWidth() / 2);
        const int n = sample->audio.getNumSamples();
        const int nch = sample->audio.getNumChannels();
        for (int c = 0; c < cols; ++c)
        {
            const int s0 = (int) ((juce::int64) c * n / cols), s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * n / cols));
            const int step = juce::jmax (1, (s1 - s0) / 64);
            float mn = 0.f, mx = 0.f;
            for (int i = s0; i < s1 && i < n; i += step)
            {
                float v = 0.f;
                for (int ch = 0; ch < nch; ++ch) v += sample->audio.getSample (ch, i) / (float) nch;
                mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
            }
            mins.push_back (mn); maxs.push_back (mx);
        }
    }

    PepelVoxProcessor& proc;
    std::shared_ptr<const SampleData> sample;
    std::shared_ptr<const SliceMap> slices;
    std::vector<float> mins, maxs;
    std::array<int, PepelVoxProcessor::kMax> heads {};
    int selected = 0;
};

//==============================================================================
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (PepelVoxProcessor& p) : proc (p) { flash.fill (0.f); }
    std::function<void (int)> onSelect;

    void tick()
    {
        for (size_t i = 0; i < flash.size(); ++i)
        {
            if (proc.padHit[i].exchange (false)) flash[i] = 1.f;
            else flash[i] *= 0.6f;
        }
        selected = proc.selectedSlice.load();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds(), frameViolet, juce::String::fromUTF8 ("ПЭДЫ"), true, false);
        const int n = proc.getNumSlices(), base = proc.getBaseNote();
        const auto o = origin();

        for (int i = 0; i < PepelVoxProcessor::kMax; ++i)
        {
            const auto r = padRect (i, o);
            const bool on = i < n;
            g.setColour (ink);
            g.fillRect (r);
            Colour fill = on ? violetDk.interpolatedWith (blood, flash[(size_t) i]) : paper;
            g.setColour (fill);
            g.fillRect (r.reduced (2));
            if (i == selected && on)
            {
                g.setColour (moon);
                g.drawRect (r, 2);
            }
            g.setFont (vt (24.f));
            g.setColour (on ? moon : track);
            g.drawText (juce::String (i + 1), r.getX(), r.getY() + 3, r.getWidth(), 20, juce::Justification::centred, false);
            if (on)
            {
                g.setFont (vt (17.f));
                g.setColour (peach);
                g.drawText (juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, base + i), true, true, 5),
                            r.getX(), r.getY() + 22, r.getWidth(), 16, juce::Justification::centred, false);
            }
        }

        g.setFont (vt (22.f));
        g.setColour (textSoft);
        g.drawText ("KEYS " + juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, base), true, true, 5) + " - "
                        + juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, base + n - 1), true, true, 5),
                    getLocalBounds().withTrimmedTop (getHeight() - 40).withTrimmedBottom (10), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto o = origin();
        const int n = proc.getNumSlices();
        for (int i = 0; i < n; ++i)
            if (padRect (i, o).contains (e.getPosition()))
            {
                if (onSelect) onSelect (i);
                proc.auditionSlice (i);
                return;
            }
    }

private:
    juce::Point<int> origin() const { return { (getWidth() - 341) / 2, 46 }; }
    static juce::Rectangle<int> padRect (int i, juce::Point<int> o)
    {
        const int col = i % 8, row = i / 8; // row 0 is the bottom row
        return { o.x + col * 43, o.y + (3 - row) * 43, 40, 40 };
    }

    PepelVoxProcessor& proc;
    std::array<float, PepelVoxProcessor::kMax> flash {};
    int selected = 0;
};

//==============================================================================
// Weather = "dirt" of the sound (0 clear sky ... 1 thunderstorm), city = output level.
class WeatherView : public juce::Component
{
public:
    WeatherView()
    {
        setInterceptsMouseClicks (false, false);
        for (int i = 0; i < 8; ++i)
            clouds.push_back ({ (float) rng.nextInt (1136), (float) (4 + rng.nextInt (30)), rng.nextInt (3), 0.4f + rng.nextFloat() * 0.8f });
        for (int i = 0; i < 90; ++i)
            drops.push_back ({ (float) rng.nextInt (1180), (float) rng.nextInt (82), 9.f + rng.nextFloat() * 4.f });
        int x = 0;
        while (x < 1136)
        {
            const int w = 12 + 4 * rng.nextInt (5), h = 8 + 4 * rng.nextInt (6);
            city.push_back ({ x, w, h, rng.nextFloat() * 6.28f });
            x += w + (rng.nextBool() ? 0 : 4);
        }
    }

    void tick (float d, float lvl)
    {
        dirt = d; level = lvl; ++frame;
        for (auto& c : clouds) { c.x += c.speed; if (c.x > 1136.f + 20.f) c.x = -120.f; }
        for (auto& r : drops) { r.x -= 3.f; r.y += r.v; if (r.y > 82.f) { r.y = -10.f; r.x = (float) rng.nextInt (1180); } }
        if (lightFrames > 0) --lightFrames;
        else if (dirt > 0.85f && rng.nextInt (18) == 0)
        {
            lightFrames = 2;
            bolt.clear();
            int x = 120 + rng.nextInt (900), y = 0;
            while (y < 62) { bolt.push_back ({ x, y }); x += (rng.nextBool() ? -1 : 1) * (6 + 4 * rng.nextInt (3)); y += 12; }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds(), frameViolet, {}, false, false);
        g.reduceClipRegion (juce::Rectangle<int> (7, 7, getWidth() - 14, getHeight() - 14));
        g.setOrigin (7, 7);
        const int iw = getWidth() - 14, ih = getHeight() - 14;
        const float d = dirt;

        // sky
        static const Stop topS[] = { { 0.f, hex (0x9FD8F5) }, { 0.2f, hex (0x4F6FD0) }, { 0.4f, hex (0x5A3FA0) },
                                     { 0.6f, hex (0x3A0A30) }, { 0.85f, hex (0x2A0210) }, { 1.f, hex (0x1C0108) } };
        static const Stop botS[] = { { 0.f, hex (0xD6F0FF) }, { 0.2f, hex (0xF07840) }, { 0.4f, hex (0xE8634A) },
                                     { 0.6f, hex (0x730426) }, { 0.85f, hex (0xC10F1D) }, { 1.f, hex (0xE93426) } };
        const Colour top = gradientAt (topS, d), bot = gradientAt (botS, d);
        for (int y = 0; y < ih; y += 4)
        {
            const float t = std::floor ((float) y / (float) ih * 10.f) / 9.f;
            g.setColour (top.interpolatedWith (bot, juce::jlimit (0.f, 1.f, t)));
            g.fillRect (0, y, iw, 4);
        }

        // sun (sets as the sound gets dirtier)
        const float sunA = 1.f - juce::jlimit (0.f, 1.f, (d - 0.42f) / 0.2f);
        if (sunA > 0.f)
        {
            static const Stop sunS[] = { { 0.f, hex (0xFFF6C8) }, { 0.2f, hex (0xFFC060) }, { 0.4f, hex (0xFF6A3A) } };
            const float k = juce::jlimit (0.f, 1.f, d / 0.45f);
            const int cy = 18 + (int) ((ih - 12.f) * k * k);
            pixelDisc (g, 170, cy, 3, gradientAt (sunS, d).withAlpha (sunA));
        }

        // blood moon
        const float moonA = juce::jlimit (0.f, 1.f, (d - 0.8f) / 0.15f);
        if (moonA > 0.f)
        {
            pixelDisc (g, iw - 200, 24, 4, crimson.withAlpha (moonA));
            pixelDisc (g, iw - 204, 20, 2, blood.withAlpha (moonA));
            g.setColour (maroon.withAlpha (moonA));
            g.fillRect (iw - 196, 28, 4, 4);
            g.fillRect (iw - 208, 32, 4, 4);
        }

        // clouds
        static const Stop cloudS[] = { { 0.f, hex (0xEAF6FF) }, { 0.3f, hex (0xB7A6F0) }, { 0.45f, hex (0x8B6FD0) },
                                       { 0.65f, hex (0x5A2A60) }, { 0.9f, hex (0x3B0A24) } };
        const Colour cc = gradientAt (cloudS, d).withAlpha (0.55f + 0.4f * d);
        for (auto& c : clouds)
        {
            if (c.type == 0) drawSprite (g, kCloudS, (int) c.x, (int) c.y, 4, cc);
            else if (c.type == 1) drawSprite (g, kCloudM, (int) c.x, (int) c.y, 4, cc);
            else drawSprite (g, kCloudL, (int) c.x, (int) c.y, 4, cc);
        }

        // rain
        if (d > 0.5f)
        {
            const int n = (int) ((float) drops.size() * juce::jlimit (0.f, 1.f, (d - 0.5f) / 0.4f));
            g.setColour ((d > 0.8f ? peach : glow).withAlpha (0.6f));
            for (int i = 0; i < n; ++i)
            {
                const auto& r = drops[(size_t) i];
                g.fillRect ((int) r.x, (int) r.y, 2, 8);
                g.fillRect ((int) r.x + 2, (int) r.y + 6, 2, 4);
            }
        }

        // lightning
        if (lightFrames > 0)
        {
            if (lightFrames == 2) { g.setColour (moon.withAlpha (0.35f)); g.fillRect (0, 0, iw, ih); }
            for (size_t i = 0; i + 1 < bolt.size(); ++i)
                for (int s = 0; s < 3; ++s)
                {
                    const int x = bolt[i].x + (bolt[i + 1].x - bolt[i].x) * s / 3;
                    const int y = bolt[i].y + 4 * s;
                    g.setColour (glow.withAlpha (0.5f));  g.fillRect (x - 2, y, 8, 4);
                    g.setColour (moon);                   g.fillRect (x, y, 4, 4);
                }
        }

        // city (EQ columns dance to the output level)
        for (auto& b : city)
        {
            const float bounce = level * 30.f * (0.5f + 0.5f * std::sin ((float) frame * 0.9f + b.phase));
            const int h = b.h + 4 * (int) (bounce / 4.f);
            g.setColour (ink);
            g.fillRect (b.x, ih - h, b.w, h);
            if (d > 0.25f)
                for (int j = 0; j < 8; ++j)
                {
                    const int wx = b.x + 4 + (j % 2) * 6, wy = ih - h + 5 + (j / 2) * 8;
                    if (wy > ih - 6 || (b.x * 31 + j * 17) % 5 != 0) continue;
                    g.setColour (peach);
                    g.fillRect (wx, wy, 2, 2);
                }
        }
        g.setColour (ink);
        g.fillRect (0, ih - 4, iw, 4);
    }

private:
    static void pixelDisc (juce::Graphics& g, int cx, int cy, int R, Colour c)
    {
        g.setColour (c);
        for (int by = -R; by <= R; ++by)
            for (int bx = -R; bx <= R; ++bx)
                if (bx * bx + by * by <= R * R)
                    g.fillRect (cx + bx * 4 - 2, cy + by * 4 - 2, 4, 4);
    }

    struct Cloud { float x, y; int type; float speed; };
    struct Drop { float x, y, v; };
    struct Bld { int x, w, h; float phase; };
    std::vector<Cloud> clouds;
    std::vector<Drop> drops;
    std::vector<Bld> city;
    std::vector<juce::Point<int>> bolt;
    juce::Random rng { 20260613 };
    float dirt = 0.f, level = 0.f;
    int frame = 0, lightFrames = 0;
};

//==============================================================================
class HeaderView : public juce::Component
{
public:
    HeaderView()
    {
        for (int i = 0; i < 46; ++i)
            stars.push_back ({ rng.nextInt (1190), 4 + rng.nextInt (96), rng.nextInt (20), 8 + rng.nextInt (14), i % 6 == 0 });
        for (int i = 0; i < 5; ++i)
            clouds.push_back ({ (float) rng.nextInt (1190), (float) (52 + rng.nextInt (44)), i % 3, 0.25f + rng.nextFloat() * 0.3f });
    }

    std::function<void (int)> onPresetStep; // -1 / +1

    void setPreset (int idx, const juce::String& name) { presetIdx = idx; presetName = name; }
    void setGlitch (bool g) { glitch = g; }

    void tick()
    {
        ++frame;
        for (auto& c : clouds) { c.x += c.v; if (c.x > 1190.f) c.x = -110.f; }
        for (auto& c : comets)
        {
            c.trail.insert (c.trail.begin(), { c.x, c.y });
            if (c.trail.size() > 9) c.trail.pop_back();
            c.x += 11.f; c.y += 5.f;
        }
        comets.erase (std::remove_if (comets.begin(), comets.end(), [] (const Comet& c) { return c.y > 130.f || c.x > 1250.f; }), comets.end());
        if (comets.size() < 2 && rng.nextInt (110) == 0)
            comets.push_back ({ (float) rng.nextInt (900) + 100.f, -4.f, {} });
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight();
        static const Stop sky[] = { { 0.f, hex (0x00003E) }, { 1.f, hex (0x2A2490) } };
        bandedGradient (g, { 0, 0, w, h - 6 }, sky);

        // stars
        for (auto& s : stars)
        {
            const bool on = ((frame + s.phase) % s.period) < (int) ((float) s.period * 0.7f);
            if (! on) continue;
            if (s.spark) drawSprite (g, kSpark, s.x, s.y, 1, moon);
            else { g.setColour (starBlue); g.fillRect (s.x, s.y, 2, 2); }
        }

        drawSprite (g, kMoon, w - 150, 16, 4, moon);

        // clouds
        for (auto& c : clouds)
        {
            const Colour col = c.type == 0 ? indigo : (c.type == 1 ? violetDk : mist);
            if (c.type == 0) drawSprite (g, kCloudS, (int) c.x, (int) c.y, 4, col);
            else if (c.type == 1) drawSprite (g, kCloudM, (int) c.x, (int) c.y, 4, col);
            else drawSprite (g, kCloudL, (int) c.x, (int) c.y, 3, col);
        }

        // comets
        for (auto& c : comets)
        {
            for (size_t i = 0; i < c.trail.size(); ++i)
            {
                const float a = 1.f - (float) i / (float) c.trail.size();
                g.setColour (glow.withAlpha (a * 0.7f));
                g.fillRect ((int) c.trail[i].x, (int) c.trail[i].y, 3, 3);
            }
            g.setColour (moon);
            g.fillRect ((int) c.x, (int) c.y, 4, 4);
        }

        // kanji 灰 愛 with shadow
        const int kx = w / 2 - 52, ky = 32;
        drawSprite (g, kKanjiHai, kx + 3, ky + 3, 3, ink);
        drawSprite (g, kKanjiAi, kx + 56 + 3, ky + 3, 3, ink);
        drawSprite (g, kKanjiHai, kx, ky, 3, moon);
        drawSprite (g, kKanjiAi, kx + 56, ky, 3, blood);

        // title with chromatic aberration
        const int sh = glitch ? 4 : 2;
        g.setFont (ps (30.f));
        const juce::Rectangle<int> tr (24, 14, 420, 40);
        g.setColour (cyan.withAlpha (0.85f));  g.drawText ("PEPEL SAMPLE", tr.translated (-sh, 0), juce::Justification::centredLeft, false);
        g.setColour (redGl.withAlpha (0.85f)); g.drawText ("PEPEL SAMPLE", tr.translated (sh, 0), juce::Justification::centredLeft, false);
        g.setColour (ink);                     g.drawText ("PEPEL SAMPLE", tr.translated (3, 3), juce::Justification::centredLeft, false);
        g.setColour (moon);                    g.drawText ("PEPEL SAMPLE", tr, juce::Justification::centredLeft, false);

        // preset plaque
        const auto pr = plaque();
        drawPanel (g, pr, frameViolet, {}, false, true);
        g.setFont (ps (13.f));
        g.setColour (peach);
        g.drawText ("<", pr.getX() + 12, pr.getY(), 24, pr.getHeight(), juce::Justification::centred, false);
        g.drawText (">", pr.getRight() - 36, pr.getY(), 24, pr.getHeight(), juce::Justification::centred, false);
        g.setColour (text);
        g.drawText (juce::String (presetIdx + 1).paddedLeft ('0', 2) + " " + presetName,
                    pr.reduced (36, 0), juce::Justification::centred, false);

        // bottom lines
        g.setColour (blood); g.fillRect (0, h - 6, w, 3);
        g.setColour (ink);   g.fillRect (0, h - 3, w, 3);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pr = plaque();
        if (! pr.contains (e.getPosition()) || ! onPresetStep) return;
        onPresetStep (e.x < pr.getX() + 40 ? -1 : 1);
    }

private:
    juce::Rectangle<int> plaque() const { return { 24, 64, 300, 34 }; }

    struct Star { int x, y, phase, period; bool spark; };
    struct PCloud { float x, y; int type; float v; };
    struct Comet { float x, y; std::vector<juce::Point<float>> trail; };
    std::vector<Star> stars;
    std::vector<PCloud> clouds;
    std::vector<Comet> comets;
    juce::Random rng { 777 };
    juce::String presetName;
    int presetIdx = 0, frame = 0;
    bool glitch = false;
};
} // namespace pepel
