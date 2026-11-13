#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <vector>

// Immutable decoded audio + onset candidates (strongest first).
struct SampleData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String name, path;
    std::vector<int> onsetPos;
    std::vector<float> onsetStrength;
};

struct SliceMap
{
    std::vector<int> bounds; // size = numSlices + 1, in samples
};

class PepelVoxProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
{
public:
    static constexpr int kMax = 32;

    PepelVoxProcessor();
    ~PepelVoxProcessor() override;

    // --- AudioProcessor
    const juce::String getName() const override { return "PEPEL SAMPLE"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- API for the editor (message thread)
    bool loadFile (const juce::File& f);
    std::shared_ptr<const SampleData> getSample() const;
    std::shared_ptr<const SliceMap> getSlices() const;
    void auditionSlice (int i) { if (i >= 0 && i < kMax) auditionReq[(size_t) i].store (true); }
    int getNumSlices() const;
    int getBaseNote() const;

    static int numPresets() { return 6; }
    static juce::String presetName (int i);
    void applyPreset (int idx);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<int> selectedSlice { 0 };
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> learning { false };
    std::atomic<int> learnedNote { -1 };
    std::atomic<float> dirt { 0.f };      // smoothed "harshness" 0..1 (tau ~0.35 s)
    std::atomic<float> outLevel { 0.f };  // smoothed output level 0..1
    std::array<std::atomic<int>, kMax> voiceHead;   // sample index or -1
    std::array<std::atomic<bool>, kMax> padHit;

private:
    struct Grain { double anchor = 0.0; int k = 0; };
    enum class VState { Idle, Playing, Releasing };
    struct Voice
    {
        VState state = VState::Idle;
        bool pending = false, fast = false, reverse = false, loop = false;
        float pendingVel = 1.f, vel = 1.f, env = 0.f;
        double head = 0.0;
        int tail = 0;
        Grain g[2];
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildSlices(); }
    void rebuildSlices();
    static void analyseOnsets (SampleData&);
    static std::vector<int> computeBounds (const SampleData&, int n, int mode);

    void triggerSlice (int i, float vel);
    void releaseSlice (int i);
    void beginVoice (int i, float vel);
    void renderVoices (juce::AudioBuffer<float>&, int start, int num);
    int renderVoiceSegment (int i, float* outL, float* outR, int num, bool& restarted);

    juce::AudioFormatManager fm;
    mutable juce::SpinLock dataLock;
    std::shared_ptr<const SampleData> sample;
    std::shared_ptr<const SliceMap> slices;
    std::shared_ptr<const SampleData> aSample; // audio-thread copies
    std::shared_ptr<const SliceMap> aSlices;

    std::array<std::atomic<bool>, kMax> auditionReq;
    std::atomic<bool> killAll { false };

    std::atomic<float> *pSlices = nullptr, *pMode = nullptr, *pBase = nullptr, *pGate = nullptr, *pMaster = nullptr;
    std::array<std::atomic<float>*, kMax> pPitch {}, pSpeed {}, pVol {}, pPan {}, pRev {}, pLoop {};

    Voice voices[kMax];
    double hostSR = 44100.0;
    int grainLen = 2048;
    std::vector<float> window;
    float dirtState = 0.f, levelState = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PepelVoxProcessor)
};
