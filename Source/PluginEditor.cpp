#include "PluginEditor.h"

using namespace pepel;

PepelVoxEditor::PepelVoxEditor (PepelVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p), pads (p)
{
    setSize (1190, 864);
    setResizable (false, false);

    addAndMakeVisible (header);
    addAndMakeVisible (wave);
    addAndMakeVisible (pads);
    addAndMakeVisible (weather);
    header.setBounds (0, 0, 1190, 118);
    wave.setBounds (pWave);
    pads.setBounds (pPads);
    weather.setBounds (pWeather);

    SegRow* colA[] = { &fileRow, &modeRow, &slicesRow };
    SegRow* colB[] = { &baseRow, &learnRow, &gateRow, &masterRow };
    SegRow* colS[] = { &pitchRow, &speedRow, &volRow, &panRow, &revRow, &loopRow };
    for (int i = 0; i < 3; ++i) { addAndMakeVisible (colA[i]); colA[i]->setBounds (rowRect (pA, i)); }
    for (int i = 0; i < 4; ++i) { addAndMakeVisible (colB[i]); colB[i]->setBounds (rowRect (pB, i)); }
    for (int i = 0; i < 6; ++i) { addAndMakeVisible (colS[i]); colS[i]->setBounds (rowRect (pSel, i)); }
    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible (actRows[i]);
        actRows[i].setBounds (rowRect (pAct, i));
        actRows[i].setButtonText ("GO");
    }

    auto& ap = proc.apvts;
    modeRow.bind (ap.getParameter ("mode"));
    slicesRow.bind (ap.getParameter ("slices"));
    baseRow.bind (ap.getParameter ("base"));
    gateRow.bind (ap.getParameter ("gate"));
    masterRow.bind (ap.getParameter ("master"));

    fileRow.onClick = [this] { chooseFile(); };
    learnRow.onClick = [this] { proc.learning.store (! proc.learning.load()); };
    wave.onLoadRequest = [this] { chooseFile(); };
    wave.onSelect = [this] (int i) { selectSlice (i); };
    pads.onSelect = [this] (int i) { selectSlice (i); };

    header.onPresetStep = [this] (int d)
    {
        const int n = PepelVoxProcessor::numPresets();
        proc.applyPreset ((proc.currentPreset.load() + d + n) % n);
    };

    actRows[0].onClick = [this] { proc.applyPreset (0); };
    actRows[1].onClick = [this] { setAllSlices ("rev_", 1.f); };
    actRows[2].onClick = [this] { setAllSlices ("rev_", 0.f); };
    actRows[3].onClick = [this] { randomise ("pitch_", -12.f, 12.f, true); };
    actRows[4].onClick = [this] { randomise ("speed_", 0.5f, 2.f, false); };
    actRows[5].onClick = [this]
    {
        for (int i = 0; i < PepelVoxProcessor::kMax; ++i)
            if (auto* q = proc.apvts.getParameter ("rev_" + juce::String (i)))
            {
                q->beginChangeGesture();
                q->setValueNotifyingHost (rng.nextFloat() < 0.4f ? 1.f : 0.f);
                q->endChangeGesture();
            }
    };

    // cached scanlines: every 3rd row, ink @ 0.16
    scanlines = juce::Image (juce::Image::ARGB, 1190, 864, true);
    {
        juce::Graphics g (scanlines);
        g.setColour (ink.withAlpha (0.16f));
        for (int y = 0; y < 864; y += 3) g.fillRect (0, y, 1190, 1);
    }

    bindSliceRows();
    startTimerHz (15); // deliberately choppy retro tempo
}

PepelVoxEditor::~PepelVoxEditor() { stopTimer(); }

void PepelVoxEditor::bindSliceRows()
{
    const int s = proc.selectedSlice.load();
    shownSel = s;
    const auto id = juce::String (s);
    auto& ap = proc.apvts;
    pitchRow.bind (ap.getParameter ("pitch_" + id));
    speedRow.bind (ap.getParameter ("speed_" + id));
    volRow.bind (ap.getParameter ("vol_" + id));
    panRow.bind (ap.getParameter ("pan_" + id));
    revRow.bind (ap.getParameter ("rev_" + id));
    loopRow.bind (ap.getParameter ("loop_" + id));
    repaint();
}

void PepelVoxEditor::selectSlice (int i)
{
    proc.selectedSlice.store (juce::jlimit (0, PepelVoxProcessor::kMax - 1, i));
    bindSliceRows();
}

void PepelVoxEditor::setAllSlices (const juce::String& prefix, float value)
{
    for (int i = 0; i < PepelVoxProcessor::kMax; ++i)
        if (auto* q = proc.apvts.getParameter (prefix + juce::String (i)))
        {
            q->beginChangeGesture();
            q->setValueNotifyingHost (q->convertTo0to1 (value));
            q->endChangeGesture();
        }
}

void PepelVoxEditor::randomise (const juce::String& prefix, float lo, float hi, bool snapSemitone)
{
    for (int i = 0; i < PepelVoxProcessor::kMax; ++i)
        if (auto* q = proc.apvts.getParameter (prefix + juce::String (i)))
        {
            float v = lo + rng.nextFloat() * (hi - lo);
            if (snapSemitone) v = std::round (v);
            else v = std::pow (2.f, std::log2 (lo) + rng.nextFloat() * (std::log2 (hi) - std::log2 (lo)));
            q->beginChangeGesture();
            q->setValueNotifyingHost (q->convertTo0to1 (v));
            q->endChangeGesture();
        }
}

void PepelVoxEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Select audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFromFile (f);
                          });
}

void PepelVoxEditor::loadFromFile (const juce::File& f) { proc.loadFile (f); }

void PepelVoxEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0) loadFromFile (juce::File (files[0]));
}

void PepelVoxEditor::timerCallback()
{
    dirtUI += (proc.dirt.load() - dirtUI) * 0.1f;
    weather.tick (dirtUI, proc.outLevel.load());

    // VHS glitch: roughly every 4 s for 2-4 frames
    if (glitchFrames > 0)
    {
        if (--glitchFrames == 0) nextGlitch = 45 + rng.nextInt (30);
    }
    else if (--nextGlitch <= 0)
    {
        glitchFrames = 2 + rng.nextInt (3);
        glitchY = rng.nextInt (juce::jmax (1, getHeight() - 30));
        glitchH = 6 + rng.nextInt (14);
    }

    const int pr = proc.currentPreset.load();
    header.setPreset (pr, PepelVoxProcessor::presetName (pr));
    header.setGlitch (glitchFrames > 0);
    header.tick();
    wave.refreshData();
    pads.tick();

    const int ln = proc.learnedNote.exchange (-1);
    if (ln >= 0)
        if (auto* q = proc.apvts.getParameter ("base"))
        {
            q->beginChangeGesture();
            q->setValueNotifyingHost (q->convertTo0to1 ((float) juce::jlimit (0, 115, ln)));
            q->endChangeGesture();
        }
    learnRow.setButtonText (proc.learning.load() ? "PRESS A PAD..." : "LEARN");

    auto s = proc.getSample();
    const juce::String fn = s != nullptr ? s->name.toUpperCase() : juce::String ("LOAD...");
    if (fn != shownFile) { shownFile = fn; fileRow.setButtonText (fn); }

    if (proc.selectedSlice.load() != shownSel) bindSliceRows();
    repaint();
}

void PepelVoxEditor::paint (juce::Graphics& g)
{
    static const Stop bg[] = { { 0.f, ink }, { 0.34f, hex (0x1E2181) }, { 0.68f, maroon }, { 1.f, crimson } };
    bandedGradient (g, getLocalBounds(), bg);

    for (auto r : { pPads, pWave, pWeather }) drawPanelShadow (g, r);

    drawPanel (g, pA, panelColour (0), juce::String::fromUTF8 ("ФАЙЛ И НАРЕЗКА"));
    drawPanel (g, pB, panelColour (1), juce::String::fromUTF8 ("MIDI И ЗВУК"));
    drawPanel (g, pSel, panelColour (0),
               juce::String::fromUTF8 ("СЭМПЛ ") + juce::String (proc.selectedSlice.load() + 1).paddedLeft ('0', 2));
    drawPanel (g, pAct, panelColour (1), juce::String::fromUTF8 ("ДЕЙСТВИЯ"));
}

void PepelVoxEditor::paintOverChildren (juce::Graphics& g)
{
    g.drawImageAt (scanlines, 0, 0);

    // film grain
    g.setColour (moon.withAlpha (0.35f));
    for (int i = 0; i < 24; ++i)
        g.fillRect (rng.nextInt (getWidth()), rng.nextInt (getHeight()), 2, 1);

    // rare glitch: translucent white band with cyan / pink tears
    if (glitchFrames > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillRect (0, glitchY, getWidth(), glitchH);
        g.setColour (cyan.withAlpha (0.5f));
        g.fillRect (rng.nextInt (getWidth() - 300), glitchY + 1, 120 + rng.nextInt (180), 3);
        g.setColour (pink.withAlpha (0.5f));
        g.fillRect (rng.nextInt (getWidth() - 300), glitchY + glitchH - 4, 120 + rng.nextInt (180), 3);
    }
}
