#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"

class PepelVoxEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit PepelVoxEditor (PepelVoxProcessor&);
    ~PepelVoxEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override; // VHS effects
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void chooseFile();
    void loadFromFile (const juce::File&);
    void selectSlice (int i);
    void bindSliceRows();
    void setAllSlices (const juce::String& prefix, float value);
    void randomise (const juce::String& prefix, float lo, float hi, bool snapSemitone);

    PepelVoxProcessor& proc;

    // geometry (fixed 1190 x 864)
    const juce::Rectangle<int> pA { 20, 132, 370, 138 }, pB { 20, 284, 370, 168 }, pPads { 20, 466, 370, 268 },
        pWave { 410, 132, 760, 360 }, pSel { 410, 506, 370, 228 }, pAct { 800, 506, 370, 228 },
        pWeather { 20, 748, 1150, 96 };

    pepel::HeaderView header;
    pepel::WaveView wave;
    pepel::PadGrid pads;
    pepel::WeatherView weather;

    pepel::SegRow fileRow  { pepel::SegRow::Kind::Button, "FILE", 60 };
    pepel::SegRow modeRow  { pepel::SegRow::Kind::Choice, "MODE" };
    pepel::SegRow slicesRow{ pepel::SegRow::Kind::Slider, "SLICES" };
    pepel::SegRow baseRow  { pepel::SegRow::Kind::Slider, "BASE NOTE" };
    pepel::SegRow learnRow { pepel::SegRow::Kind::Button, "MIDI LEARN", 140 };
    pepel::SegRow gateRow  { pepel::SegRow::Kind::Toggle, "GATE" };
    pepel::SegRow masterRow{ pepel::SegRow::Kind::Slider, "MASTER" };

    pepel::SegRow pitchRow { pepel::SegRow::Kind::Slider, "PITCH" };
    pepel::SegRow speedRow { pepel::SegRow::Kind::Slider, "SPEED" };
    pepel::SegRow volRow   { pepel::SegRow::Kind::Slider, "VOLUME" };
    pepel::SegRow panRow   { pepel::SegRow::Kind::Slider, "PAN" };
    pepel::SegRow revRow   { pepel::SegRow::Kind::Toggle, "REVERSE" };
    pepel::SegRow loopRow  { pepel::SegRow::Kind::Toggle, "LOOP" };

    pepel::SegRow actRows[6] {
        { pepel::SegRow::Kind::Button, "RESET ALL", 150 }, { pepel::SegRow::Kind::Button, "REVERSE ALL", 150 },
        { pepel::SegRow::Kind::Button, "FORWARD ALL", 150 }, { pepel::SegRow::Kind::Button, "RAND PITCH", 150 },
        { pepel::SegRow::Kind::Button, "RAND SPEED", 150 }, { pepel::SegRow::Kind::Button, "RAND REVERSE", 150 } };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image scanlines;
    juce::Random rng;
    float dirtUI = 0.f;
    int shownSel = -1, glitchFrames = 0, nextGlitch = 60, glitchY = 0, glitchH = 8;
    juce::String shownFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PepelVoxEditor)
};
