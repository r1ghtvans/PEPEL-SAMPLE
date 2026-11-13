#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

using namespace juce;

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout PepelVoxProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.f)) + "%"; };

    L.add (std::make_unique<AudioParameterInt> (ParameterID { "slices", 1 }, "Slices", 1, kMax, 8));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Slice Mode",
                                                   StringArray { "Equal", "Beats" }, 1));
    L.add (std::make_unique<AudioParameterInt> (ParameterID { "base", 1 }, "Base Note", 0, 115, 36,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
            { return MidiMessage::getMidiNoteName (v, true, true, 5); })));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "gate", 1 }, "Gate Mode", false,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return String (v ? "ON" : "OFF"); })));
    L.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master",
        NormalisableRange<float> (0.f, 2.f, 0.01f), 0.8f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    for (int i = 0; i < kMax; ++i)
    {
        const String n = "Slice " + String (i + 1) + " ";
        const String id = String (i);

        L.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch_" + id, 1 }, n + "Pitch",
            NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const float r = std::round (v * 10.f) / 10.f;
                String t = (r > 0 ? "+" : "") + (std::abs (r - std::round (r)) < 0.01f ? String ((int) std::round (r)) : String (r, 1));
                return t + " st";
            })));

        NormalisableRange<float> sp (0.25f, 4.f, 0.01f);
        sp.setSkewForCentre (1.f);
        L.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed_" + id, 1 }, n + "Speed", sp, 1.f,
            AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

        L.add (std::make_unique<AudioParameterFloat> (ParameterID { "vol_" + id, 1 }, n + "Volume",
            NormalisableRange<float> (0.f, 2.f, 0.01f), 1.f,
            AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

        L.add (std::make_unique<AudioParameterFloat> (ParameterID { "pan_" + id, 1 }, n + "Pan",
            NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const int p = roundToInt (v * 100.f);
                return p == 0 ? String ("C") : String (p < 0 ? "L" : "R") + String (std::abs (p));
            })));

        auto onoff = AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return String (v ? "ON" : "OFF"); });
        L.add (std::make_unique<AudioParameterBool> (ParameterID { "rev_" + id, 1 }, n + "Reverse", false, onoff));
        L.add (std::make_unique<AudioParameterBool> (ParameterID { "loop_" + id, 1 }, n + "Loop", false, onoff));
    }
    return L;
}

//==============================================================================
PepelVoxProcessor::PepelVoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    fm.registerBasicFormats();

    pSlices = apvts.getRawParameterValue ("slices");
    pMode   = apvts.getRawParameterValue ("mode");
    pBase   = apvts.getRawParameterValue ("base");
    pGate   = apvts.getRawParameterValue ("gate");
    pMaster = apvts.getRawParameterValue ("master");
    for (int i = 0; i < kMax; ++i)
    {
        const auto id = String (i);
        const auto k = (size_t) i;
        pPitch[k] = apvts.getRawParameterValue ("pitch_" + id);
        pSpeed[k] = apvts.getRawParameterValue ("speed_" + id);
        pVol[k]   = apvts.getRawParameterValue ("vol_" + id);
        pPan[k]   = apvts.getRawParameterValue ("pan_" + id);
        pRev[k]   = apvts.getRawParameterValue ("rev_" + id);
        pLoop[k]  = apvts.getRawParameterValue ("loop_" + id);
        voiceHead[k].store (-1);
        padHit[k].store (false);
        auditionReq[k].store (false);
    }
    apvts.addParameterListener ("slices", this);
    apvts.addParameterListener ("mode", this);
}

PepelVoxProcessor::~PepelVoxProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("slices", this);
    apvts.removeParameterListener ("mode", this);
}

juce::AudioProcessorEditor* PepelVoxProcessor::createEditor() { return new PepelVoxEditor (*this); }

bool PepelVoxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo()
        || l.getMainOutputChannelSet() == AudioChannelSet::mono();
}

void PepelVoxProcessor::prepareToPlay (double sr, int)
{
    hostSR = sr;
    grainLen = jmax (256, (int) std::round (0.046 * sr));
    grainLen += grainLen & 1;
    window.resize ((size_t) grainLen);
    for (int k = 0; k < grainLen; ++k) // periodic Hann: two grains at 50% overlap sum to 1
        window[(size_t) k] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) k / (float) grainLen);
    killAll.store (true);
}

int PepelVoxProcessor::getNumSlices() const { return jlimit (1, kMax, (int) pSlices->load()); }
int PepelVoxProcessor::getBaseNote() const  { return (int) pBase->load(); }

std::shared_ptr<const SampleData> PepelVoxProcessor::getSample() const
{
    const SpinLock::ScopedLockType sl (dataLock);
    return sample;
}
std::shared_ptr<const SliceMap> PepelVoxProcessor::getSlices() const
{
    const SpinLock::ScopedLockType sl (dataLock);
    return slices;
}

//==============================================================================
// Loading, analysis, slicing
bool PepelVoxProcessor::loadFile (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0)
        return false;

    const int64 maxLen = (int64) (r->sampleRate * 60.0 * 10.0);
    const int len = (int) jmin (r->lengthInSamples, maxLen);
    const int ch = jmin (2, (int) r->numChannels);

    auto sd = std::make_shared<SampleData>();
    sd->audio.setSize (ch, len);
    r->read (&sd->audio, 0, len, 0, true, ch > 1);
    sd->sampleRate = r->sampleRate;
    sd->name = f.getFileName();
    sd->path = f.getFullPathName();
    analyseOnsets (*sd);

    auto sm = std::make_shared<SliceMap>();
    sm->bounds = computeBounds (*sd, getNumSlices(), (int) pMode->load());
    {
        const SpinLock::ScopedLockType sl (dataLock);
        sample = sd;
        slices = sm;
    }
    killAll.store (true);
    apvts.state.setProperty ("samplePath", sd->path, nullptr);
    return true;
}

void PepelVoxProcessor::rebuildSlices()
{
    auto s = getSample();
    if (s == nullptr)
        return;
    auto sm = std::make_shared<SliceMap>();
    sm->bounds = computeBounds (*s, getNumSlices(), (int) pMode->load());
    const SpinLock::ScopedLockType sl (dataLock);
    if (sample == s)
        slices = sm;
}

void PepelVoxProcessor::analyseOnsets (SampleData& sd)
{
    const int n = sd.audio.getNumSamples();
    const int win = 1024, hop = 512;
    if (n < win * 4)
        return;

    const int nch = sd.audio.getNumChannels();
    std::vector<float> mono ((size_t) n, 0.f);
    for (int c = 0; c < nch; ++c)
    {
        const float* d = sd.audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += d[i] / (float) nch;
    }

    const int nf = (n - win) / hop;
    std::vector<float> e ((size_t) nf), flux ((size_t) nf, 0.f);
    for (int f = 0; f < nf; ++f)
    {
        double acc = 0.0;
        for (int k = 0; k < win; ++k)
        {
            const float x = mono[(size_t) (f * hop + k)];
            acc += (double) x * x;
        }
        e[(size_t) f] = std::log (1.f + 100.f * (float) std::sqrt (acc / win));
    }
    for (int f = 1; f < nf; ++f)
        flux[(size_t) f] = jmax (0.f, e[(size_t) f] - e[(size_t) f - 1]);

    struct Cand { int pos; float s; };
    std::vector<Cand> c;
    for (int f = 3; f < nf - 3; ++f)
    {
        const float v = flux[(size_t) f];
        if (v < 0.05f) continue;
        bool peak = true;
        for (int d = -3; d <= 3 && peak; ++d)
            if (d != 0 && ((d < 0) ? flux[(size_t) (f + d)] > v : flux[(size_t) (f + d)] >= v)) peak = false;
        if (! peak) continue;

        // refine: first sample in the neighbourhood that reaches 30% of the local peak
        const int a = jmax (0, f * hop - hop), b = jmin (n - 1, f * hop + win);
        float pk = 0.f;
        for (int i = a; i <= b; ++i) pk = jmax (pk, std::abs (mono[(size_t) i]));
        int pos = f * hop;
        for (int i = a; i <= b; ++i)
            if (std::abs (mono[(size_t) i]) >= 0.3f * pk) { pos = jmax (0, i - 48); break; }
        c.push_back ({ pos, v });
    }
    std::sort (c.begin(), c.end(), [] (const Cand& x, const Cand& y) { return x.s > y.s; });
    for (auto& k : c) { sd.onsetPos.push_back (k.pos); sd.onsetStrength.push_back (k.s); }
}

std::vector<int> PepelVoxProcessor::computeBounds (const SampleData& sd, int n, int mode)
{
    const int len = sd.audio.getNumSamples();
    std::vector<int> cuts;

    if (mode == 1) // beats: strongest transients, minimum 60 ms apart
    {
        const int minGap = (int) (0.06 * sd.sampleRate);
        for (size_t i = 0; i < sd.onsetPos.size() && (int) cuts.size() < n - 1; ++i)
        {
            const int p = sd.onsetPos[i];
            if (p < minGap || p > len - minGap) continue;
            bool ok = true;
            for (int q : cuts) if (std::abs (q - p) < minGap) { ok = false; break; }
            if (ok) cuts.push_back (p);
        }
        std::sort (cuts.begin(), cuts.end());
        while ((int) cuts.size() < n - 1) // not enough transients: bisect the longest piece
        {
            std::vector<int> b { 0 };
            b.insert (b.end(), cuts.begin(), cuts.end());
            b.push_back (len);
            size_t best = 0;
            for (size_t i = 0; i + 1 < b.size(); ++i)
                if (b[i + 1] - b[i] > b[best + 1] - b[best]) best = i;
            cuts.push_back ((b[best] + b[best + 1]) / 2);
            std::sort (cuts.begin(), cuts.end());
        }
    }
    else
    {
        for (int i = 1; i < n; ++i)
            cuts.push_back ((int) ((int64) len * i / n));
    }

    std::vector<int> bounds { 0 };
    bounds.insert (bounds.end(), cuts.begin(), cuts.end());
    bounds.push_back (len);
    return bounds;
}

//==============================================================================
// Voices: every slice owns one voice; a granular "two-grain" engine gives
// independent pitch and speed (pitch=speed=1 reproduces the source exactly).
void PepelVoxProcessor::beginVoice (int i, float vel)
{
    if (aSample == nullptr || aSlices == nullptr) return;
    const auto& b = aSlices->bounds;
    if (i + 1 >= (int) b.size()) return;
    const int s0 = b[(size_t) i], s1 = jmin (b[(size_t) i + 1], aSample->audio.getNumSamples());
    if (s1 - s0 < 2) return;

    auto& v = voices[i];
    v.reverse = pRev[(size_t) i]->load() > 0.5f;
    v.loop = pLoop[(size_t) i]->load() > 0.5f;
    const double dir = v.reverse ? -1.0 : 1.0;
    const double rs = std::pow (2.0, (double) pPitch[(size_t) i]->load() / 12.0) * aSample->sampleRate / hostSR;

    v.head = v.reverse ? (double) (s1 - 1) : (double) s0;
    v.g[0].k = grainLen / 2;
    v.g[0].anchor = v.head - dir * (double) (grainLen / 2) * rs;
    v.g[1].k = 0;
    v.g[1].anchor = v.head;
    v.tail = 0;
    v.env = 0.f;
    v.vel = vel;
    v.fast = false;
    v.pending = false;
    v.state = VState::Playing;
}

void PepelVoxProcessor::triggerSlice (int i, float vel)
{
    if (aSlices == nullptr || i < 0 || i + 1 >= (int) aSlices->bounds.size()) return;
    padHit[(size_t) i].store (true);
    auto& v = voices[i];
    if (v.state != VState::Idle && v.env > 0.001f)
    {
        v.pending = true;
        v.pendingVel = vel;
        v.state = VState::Releasing;
        v.fast = true;
    }
    else
        beginVoice (i, vel);
}

void PepelVoxProcessor::releaseSlice (int i)
{
    if (i < 0 || i >= kMax) return;
    auto& v = voices[i];
    v.pending = false;
    if (v.state == VState::Playing) { v.state = VState::Releasing; v.fast = false; }
}

int PepelVoxProcessor::renderVoiceSegment (int i, float* outL, float* outR, int num, bool& restarted)
{
    auto& v = voices[i];
    const auto& audio = aSample->audio;
    const auto& b = aSlices->bounds;
    if (i + 1 >= (int) b.size()) { v.state = VState::Idle; return num; }
    const int s0 = b[(size_t) i], s1 = jmin (b[(size_t) i + 1], audio.getNumSamples());
    const int len = s1 - s0;
    if (len < 2) { v.state = VState::Idle; return num; }

    const float* srcL = audio.getReadPointer (0);
    const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
    const double srRatio = aSample->sampleRate / hostSR;
    const double dir = v.reverse ? -1.0 : 1.0;
    const double readStep = std::pow (2.0, (double) pPitch[(size_t) i]->load() / 12.0) * srRatio;
    const double headStep = (double) pSpeed[(size_t) i]->load() * srRatio;
    const float gain = pVol[(size_t) i]->load() * pMaster->load() * (0.35f + 0.65f * v.vel);
    const float pan = pPan[(size_t) i]->load();
    const float gl = pan > 0.f ? 1.f - pan : 1.f, gr = pan < 0.f ? 1.f + pan : 1.f;
    const float atk = 1.f / (0.002f * (float) hostSR);
    const float rel = 1.f / ((v.fast ? 0.003f : 0.02f) * (float) hostSR);
    const bool loop = v.loop;

    auto at = [&] (int idx, const float* src) -> float
    {
        if (loop) { if (idx >= s1) idx -= len; if (idx < s0) idx += len; return src[idx]; }
        return (idx >= s0 && idx < s1) ? src[idx] : 0.f;
    };
    auto read = [&] (double pos, const float* src) -> float
    {
        if (loop)
        {
            pos = std::fmod (pos - s0, (double) len);
            if (pos < 0) pos += len;
            pos += s0;
        }
        const int i0 = (int) std::floor (pos);
        const float fr = (float) (pos - i0);
        const float a = at (i0, src), c = at (i0 + 1, src);
        return a + (c - a) * fr;
    };

    for (int n = 0; n < num; ++n)
    {
        float l = 0.f, r = 0.f;
        for (auto& gn : v.g)
        {
            const float w = window[(size_t) gn.k];
            const double pos = gn.anchor + dir * (double) gn.k * readStep;
            l += w * read (pos, srcL);
            r += w * read (pos, srcR);
            if (++gn.k >= grainLen) { gn.k = 0; gn.anchor = v.head; }
        }

        v.head += dir * headStep;
        const bool beyond = v.reverse ? (v.head < s0) : (v.head >= s1);
        if (beyond)
        {
            if (loop) v.head += v.reverse ? (double) len : -(double) len;
            else if (++v.tail > grainLen) { v.state = VState::Idle; v.env = 0.f; return n; }
        }

        if (v.state == VState::Playing)
            v.env = jmin (1.f, v.env + atk);
        else
        {
            v.env -= rel;
            if (v.env <= 0.f)
            {
                v.env = 0.f;
                v.state = VState::Idle;
                if (v.pending)
                {
                    v.pending = false;
                    beginVoice (i, v.pendingVel);
                    restarted = true;
                }
                return n + 1;
            }
        }

        const float e = v.env * gain;
        outL[n] += l * e * gl;
        if (outR != nullptr) outR[n] += r * e * gr;
    }
    return num;
}

void PepelVoxProcessor::renderVoices (AudioBuffer<float>& buffer, int start, int num)
{
    if (num <= 0 || aSample == nullptr || aSlices == nullptr) return;
    float* outL = buffer.getWritePointer (0) + start;
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) + start : nullptr;

    for (int i = 0; i < kMax; ++i)
    {
        int done = 0;
        while (done < num && voices[i].state != VState::Idle)
        {
            bool restarted = false;
            done += renderVoiceSegment (i, outL + done, outR != nullptr ? outR + done : nullptr, num - done, restarted);
            if (! restarted) break;
        }
    }
}

//==============================================================================
void PepelVoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    {
        const SpinLock::ScopedTryLockType sl (dataLock);
        if (sl.isLocked())
        {
            if (aSample != sample) aSample = sample;
            if (aSlices != slices) aSlices = slices;
        }
    }

    if (killAll.exchange (false))
        for (auto& v : voices) { v.state = VState::Idle; v.pending = false; v.env = 0.f; }

    for (int i = 0; i < kMax; ++i)
        if (auditionReq[(size_t) i].exchange (false))
            triggerSlice (i, 1.f);

    const int base = (int) pBase->load();
    const int nSl = aSlices != nullptr ? (int) aSlices->bounds.size() - 1 : 0;
    const bool gate = pGate->load() > 0.5f;

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int p = jlimit (pos, numSamples, meta.samplePosition);
        renderVoices (buffer, pos, p - pos);
        pos = p;

        if (m.isNoteOn())
        {
            if (learning.load())
            {
                learnedNote.store (m.getNoteNumber());
                learning.store (false);
            }
            else
            {
                const int i = m.getNoteNumber() - base;
                if (i >= 0 && i < nSl) triggerSlice (i, m.getFloatVelocity());
            }
        }
        else if (m.isNoteOff())
        {
            if (gate) releaseSlice (m.getNoteNumber() - base);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices) { v.state = VState::Idle; v.pending = false; v.env = 0.f; }
        }
    }
    renderVoices (buffer, pos, numSamples - pos);

    for (int i = 0; i < kMax; ++i)
        voiceHead[(size_t) i].store (voices[i].state != VState::Idle ? (int) voices[i].head : -1);

    // --- output metrics: level and "dirt" (harshness = brightness * loudness)
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    double sq = 0.0, sd = 0.0;
    float prev = 0.f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        sq += (double) m * m;
        const float d = m - prev;
        sd += (double) d * d;
        prev = m;
    }
    const float rms = numSamples > 0 ? (float) std::sqrt (sq / numSamples) : 0.f;
    const float drms = numSamples > 0 ? (float) std::sqrt (sd / numSamples) : 0.f;
    const float hf = rms > 1.0e-5f ? jlimit (0.f, 1.f, drms / rms) : 0.f;
    const float loud = jlimit (0.f, 1.f, rms * 3.5f);
    const float target = jlimit (0.f, 1.f, 0.35f * loud + 0.75f * hf * loud);
    const float alpha = 1.f - std::exp (-(float) numSamples / (0.35f * (float) hostSR));
    dirtState += (target - dirtState) * alpha;
    levelState += (loud - levelState) * 0.5f;
    dirt.store (dirtState);
    outLevel.store (levelState);
}

//==============================================================================
juce::String PepelVoxProcessor::presetName (int i)
{
    switch (i)
    {
        case 0:  return String::fromUTF8 ("ЧИСТЫЙ ЛИСТ");
        case 1:  return String::fromUTF8 ("ОКТАВА ВНИЗ");
        case 2:  return String::fromUTF8 ("ЗАМЕДЛЕНИЕ");
        case 3:  return String::fromUTF8 ("ПЕНТАТОНИКА");
        case 4:  return String::fromUTF8 ("ВСЕ НАОБОРОТ");
        default: return String::fromUTF8 ("ХАОС");
    }
}

void PepelVoxProcessor::applyPreset (int idx)
{
    idx = jlimit (0, numPresets() - 1, idx);
    currentPreset.store (idx);
    Random rnd ((int64) idx * 7919 + Time::getMillisecondCounter());
    static const float penta[] = { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24 };

    auto setP = [this] (const String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };

    for (int i = 0; i < kMax; ++i)
    {
        float pitch = 0.f, speed = 1.f, rev = 0.f;
        switch (idx)
        {
            case 1: pitch = -12.f; break;
            case 2: speed = 0.5f; break;
            case 3: pitch = penta[i % 11] + (i / 11) * 12.f; if (pitch > 24.f) pitch -= 24.f; break;
            case 4: rev = 1.f; break;
            case 5:
            {
                pitch = (float) (rnd.nextInt (25) - 12);
                static const float sp[] = { 0.5f, 1.f, 1.f, 2.f };
                speed = sp[rnd.nextInt (4)];
                rev = rnd.nextFloat() < 0.3f ? 1.f : 0.f;
                break;
            }
            default: break;
        }
        const String id (i);
        setP ("pitch_" + id, pitch);
        setP ("speed_" + id, speed);
        setP ("rev_" + id, rev);
        setP ("vol_" + id, 1.f);
        setP ("pan_" + id, 0.f);
        setP ("loop_" + id, 0.f);
    }
}

//==============================================================================
void PepelVoxProcessor::getStateInformation (MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("preset", currentPreset.load(), nullptr);
    st.setProperty ("selected", selectedSlice.load(), nullptr);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, dest);
}

void PepelVoxProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (ValueTree::fromXml (*xml));
    currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
    selectedSlice.store (jlimit (0, kMax - 1, (int) apvts.state.getProperty ("selected", 0)));

    const String path = apvts.state.getProperty ("samplePath").toString();
    if (path.isNotEmpty() && File (path).existsAsFile())
        loadFile (File (path));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PepelVoxProcessor(); }
