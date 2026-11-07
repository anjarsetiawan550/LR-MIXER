#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "Dsp.h"

class LRProProcessor : public juce::AudioProcessor
{
public:
    LRProProcessor();
    ~LRProProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    lr::Settings readSettings() const;

    juce::AudioProcessorValueTreeState apvts;

    // meters / analyzer (audio thread writes, UI thread reads)
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f }, grMax { 0.f }, detDb { -90.f };
    static constexpr int kRing = 16384;
    std::array<float, kRing> ring {};
    std::atomic<int> ringPos { 0 };
    double currentSampleRate = 48000.0;

private:
    struct Raw
    {
        std::atomic<float> *thresh, *ratio, *knee, *attack, *release, *makeup, *bypass;
        std::atomic<float> *hpfOn, *hpfFreq, *hpfType, *hpfSlope, *lpfOn, *lpfFreq, *lpfType, *lpfSlope;
        std::atomic<float> *eqOn[4], *eqFreq[4], *eqGain[4], *eqQ[4];
    } raw {};

    static constexpr int kChunk = 32;   // coefficient-update granularity (samples)
    using MulSm = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    using LinSm = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    MulSm smHpfF, smLpfF, smF[4], smQ[4];
    LinSm smG[4], smHpfMix, smLpfMix, smWet, smMakeup;
    bool needInit = true;
    int lastHpfType = -1, lastHpfSlope = -1, lastLpfType = -1, lastLpfSlope = -1;

    lr::Coef hpfC[lr::kMaxSections], lpfC[lr::kMaxSections], eqC[4];
    int hpfN = 1, lpfN = 1;
    lr::BiquadState hpfS[2][lr::kMaxSections], lpfS[2][lr::kMaxSections], eqS[2][4];
    lr::Compressor comp;
    float hpfMixPrev = 0, lpfMixPrev = 0, wetPrev = 1, makeupPrev = 1;

    void setTargets (const lr::Settings&);
    void resetStates();
    void recomputeCoefs (const lr::Settings&, float hpfF, float lpfF, const float* f, const float* g, const float* q);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LRProProcessor)
};
