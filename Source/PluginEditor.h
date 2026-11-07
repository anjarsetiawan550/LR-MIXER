#pragma once
#include "PluginProcessor.h"

class LRProEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LRProEditor (LRProProcessor&);
    ~LRProEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    void timerCallback() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LRProEditor)
};
