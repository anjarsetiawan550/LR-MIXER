#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static float parseNumber (const juce::String& t)
{
    auto s = t.trim();
    const bool k = s.containsIgnoreCase ("k");
    const float v = s.retainCharacters ("0123456789.-+").getFloatValue();
    return k ? v * 1000.f : v;
}
static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (juce::jlimit (s, e, v) / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}
static juce::String fmtFreq (float v)
{
    if (v >= 10000.f) return juce::String (v / 1000.f, 1) + "k";
    if (v >= 1000.f)  return juce::String (v / 1000.f, 2) + "k";
    if (v >= 100.f)   return juce::String ((int) std::lround (v));
    return juce::String ((int) std::lround (v)) + " Hz";
}

APVTS::ParameterLayout LRProProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using B = juce::AudioParameterBool; using C = juce::AudioParameterChoice;
    using FA = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto fa = [] (std::function<juce::String (float, int)> toS) { return FA().withStringFromValueFunction (toS).withValueFromStringFunction (parseNumber); };
    auto pid = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    p.push_back (std::make_unique<F> (pid ("comp_thresh"),  "Threshold", juce::NormalisableRange<float> (-60.f, 0.f, 0.1f), -18.f,
                 fa ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<F> (pid ("comp_ratio"),   "Ratio", logRange (1.f, 20.f), 2.f,
                 fa ([] (float v, int) { return juce::String (v, 1) + " : 1"; })));
    p.push_back (std::make_unique<F> (pid ("comp_knee"),    "Knee", juce::NormalisableRange<float> (0.f, 24.f, 0.1f), 6.f,
                 fa ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<F> (pid ("comp_attack"),  "Attack", logRange (0.1f, 200.f), 10.f,
                 fa ([] (float v, int) { return v < 10.f ? juce::String (v, 2) + " ms" : juce::String (v, 1) + " ms"; })));
    p.push_back (std::make_unique<F> (pid ("comp_release"), "Release", logRange (5.f, 2000.f), 120.f,
                 fa ([] (float v, int) { return juce::String ((int) std::lround (v)) + " ms"; })));
    p.push_back (std::make_unique<F> (pid ("comp_makeup"),  "Makeup", juce::NormalisableRange<float> (0.f, 24.f, 0.1f), 0.f,
                 fa ([] (float v, int) { return "+" + juce::String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<B> (pid ("bypass"), "Bypass", false));

    const juce::StringArray types { "BW", "LR", "BS" }, slopes { "12", "18", "24" };
    for (auto n : { "hpf", "lpf" })
    {
        const juce::String s (n); const bool hp = s == "hpf";
        p.push_back (std::make_unique<B> (juce::ParameterID { s + "_on", 1 }, s.toUpperCase() + " On", false));
        p.push_back (std::make_unique<F> (juce::ParameterID { s + "_freq", 1 }, s.toUpperCase() + " Freq", logRange (20.f, 20000.f), hp ? 40.f : 18000.f,
                     fa ([] (float v, int) { return fmtFreq (v); })));
        p.push_back (std::make_unique<C> (juce::ParameterID { s + "_type", 1 }, s.toUpperCase() + " Type", types, 0));
        p.push_back (std::make_unique<C> (juce::ParameterID { s + "_slope", 1 }, s.toUpperCase() + " Slope", slopes, 0));
    }
    const float defF[4] = { 120.f, 450.f, 2500.f, 9000.f };
    for (int i = 0; i < 4; ++i)
    {
        const juce::String s = "eq" + juce::String (i + 1), nm = "EQ" + juce::String (i + 1);
        p.push_back (std::make_unique<B> (juce::ParameterID { s + "_on", 1 }, nm + " On", true));
        p.push_back (std::make_unique<F> (juce::ParameterID { s + "_freq", 1 }, nm + " Freq", logRange (20.f, 20000.f), defF[i],
                     fa ([] (float v, int) { return fmtFreq (v); })));
        p.push_back (std::make_unique<F> (juce::ParameterID { s + "_gain", 1 }, nm + " Gain", juce::NormalisableRange<float> (-18.f, 18.f, 0.1f), 0.f,
                     fa ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1); })));
        p.push_back (std::make_unique<F> (juce::ParameterID { s + "_q", 1 }, nm + " Q", logRange (0.1f, 10.f), 1.f,
                     fa ([] (float v, int) { return juce::String (v, 2); })));
    }
    return { p.begin(), p.end() };
}

LRProProcessor::LRProProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LRPRO_STATE", createLayout())
{
    auto R = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    raw.thresh = R ("comp_thresh"); raw.ratio = R ("comp_ratio"); raw.knee = R ("comp_knee");
    raw.attack = R ("comp_attack"); raw.release = R ("comp_release"); raw.makeup = R ("comp_makeup"); raw.bypass = R ("bypass");
    raw.hpfOn = R ("hpf_on"); raw.hpfFreq = R ("hpf_freq"); raw.hpfType = R ("hpf_type"); raw.hpfSlope = R ("hpf_slope");
    raw.lpfOn = R ("lpf_on"); raw.lpfFreq = R ("lpf_freq"); raw.lpfType = R ("lpf_type"); raw.lpfSlope = R ("lpf_slope");
    for (int i = 0; i < 4; ++i)
    {
        const juce::String s = "eq" + juce::String (i + 1);
        raw.eqOn[i] = R (s + "_on"); raw.eqFreq[i] = R (s + "_freq"); raw.eqGain[i] = R (s + "_gain"); raw.eqQ[i] = R (s + "_q");
    }
}

lr::Settings LRProProcessor::readSettings() const
{
    lr::Settings s;
    s.thresh = raw.thresh->load(); s.ratio = raw.ratio->load(); s.knee = raw.knee->load();
    s.attack = raw.attack->load(); s.release = raw.release->load(); s.makeup = raw.makeup->load(); s.bypass = raw.bypass->load() > 0.5f;
    s.hpf = { raw.hpfOn->load() > 0.5f, raw.hpfFreq->load(), (int) raw.hpfType->load(), (int) raw.hpfSlope->load() };
    s.lpf = { raw.lpfOn->load() > 0.5f, raw.lpfFreq->load(), (int) raw.lpfType->load(), (int) raw.lpfSlope->load() };
    for (int i = 0; i < 4; ++i)
        s.eq[i] = { raw.eqOn[i]->load() > 0.5f, raw.eqFreq[i]->load(), raw.eqGain[i]->load(), raw.eqQ[i]->load() };
    return s;
}

bool LRProProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && o == l.getMainInputChannelSet();
}

void LRProProcessor::resetStates()
{
    for (int c = 0; c < 2; ++c)
    {
        for (auto& s : hpfS[c]) s = {};
        for (auto& s : lpfS[c]) s = {};
        for (auto& s : eqS[c]) s = {};
    }
    comp.reset();
}

void LRProProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate = sr;
    for (auto* s : { &smHpfF, &smLpfF }) s->reset (sr, 0.02);
    for (int i = 0; i < 4; ++i) { smF[i].reset (sr, 0.02); smQ[i].reset (sr, 0.02); smG[i].reset (sr, 0.02); }
    for (auto* s : { &smHpfMix, &smLpfMix, &smWet }) s->reset (sr, 0.01);
    smMakeup.reset (sr, 0.02);
    resetStates(); needInit = true;
    setLatencySamples (0);          // zero added latency: IIR only, no lookahead, no oversampling, no FFT in the audio path
}

void LRProProcessor::setTargets (const lr::Settings& s)
{
    const float hm = s.hpf.on ? 1.f : 0.f, lm = s.lpf.on ? 1.f : 0.f, wet = s.bypass ? 0.f : 1.f;
    if (needInit)
    {
        smHpfF.setCurrentAndTargetValue (s.hpf.freq); smLpfF.setCurrentAndTargetValue (s.lpf.freq);
        for (int i = 0; i < 4; ++i) { smF[i].setCurrentAndTargetValue (s.eq[i].freq); smQ[i].setCurrentAndTargetValue (s.eq[i].q);
                                      smG[i].setCurrentAndTargetValue (s.eq[i].on ? s.eq[i].gain : 0.f); }
        smHpfMix.setCurrentAndTargetValue (hm); smLpfMix.setCurrentAndTargetValue (lm); smWet.setCurrentAndTargetValue (wet);
        smMakeup.setCurrentAndTargetValue (s.makeup);
        hpfMixPrev = hm; lpfMixPrev = lm; wetPrev = wet; makeupPrev = juce::Decibels::decibelsToGain (s.makeup);
        recomputeCoefs (s, s.hpf.freq, s.lpf.freq, nullptr, nullptr, nullptr);
        needInit = false;
    }
    smHpfF.setTargetValue (s.hpf.freq); smLpfF.setTargetValue (s.lpf.freq);
    for (int i = 0; i < 4; ++i) { smF[i].setTargetValue (s.eq[i].freq); smQ[i].setTargetValue (s.eq[i].q); smG[i].setTargetValue (s.eq[i].on ? s.eq[i].gain : 0.f); }
    smHpfMix.setTargetValue (hm); smLpfMix.setTargetValue (lm); smWet.setTargetValue (wet); smMakeup.setTargetValue (s.makeup);
}

void LRProProcessor::recomputeCoefs (const lr::Settings& s, float hpfF, float lpfF, const float* f, const float* g, const float* q)
{
    const double fs = currentSampleRate;
    const int hs = s.hpf.slope, ls = s.lpf.slope;
    hpfN = lr::designPass (true,  s.hpf.type, hs, fs, hpfF, hpfC);
    lpfN = lr::designPass (false, s.lpf.type, ls, fs, lpfF, lpfC);
    for (int i = 0; i < 4; ++i)
        eqC[i] = lr::peaking (fs, f ? f[i] : s.eq[i].freq, g ? g[i] : (s.eq[i].on ? s.eq[i].gain : 0.f), q ? q[i] : s.eq[i].q);
}

void LRProProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples(), nCh = juce::jmin (2, buf.getNumChannels());
    for (int c = nCh; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    if (nCh == 0 || n == 0) return;

    const auto s = readSettings();
    setTargets (s);
    comp.setup (currentSampleRate, s.attack, s.release);
    const bool discreteChanged = s.hpf.type != lastHpfType || s.hpf.slope != lastHpfSlope || s.lpf.type != lastLpfType || s.lpf.slope != lastLpfSlope;
    lastHpfType = s.hpf.type; lastHpfSlope = s.hpf.slope; lastLpfType = s.lpf.type; lastLpfSlope = s.lpf.slope;

    float* d[2] = { buf.getWritePointer (0), nCh > 1 ? buf.getWritePointer (1) : nullptr };
    float inPk = 0.f, outPk = 0.f, grPk = 0.f, detPk = 0.f;
    int wp = ringPos.load (std::memory_order_relaxed);
    bool force = discreteChanged;

    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int len = juce::jmin (kChunk, n - pos);
        const bool active = force || smHpfF.isSmoothing() || smLpfF.isSmoothing() || smF[0].isSmoothing() || smF[1].isSmoothing() || smF[2].isSmoothing() || smF[3].isSmoothing()
                          || smQ[0].isSmoothing() || smQ[1].isSmoothing() || smQ[2].isSmoothing() || smQ[3].isSmoothing()
                          || smG[0].isSmoothing() || smG[1].isSmoothing() || smG[2].isSmoothing() || smG[3].isSmoothing();
        const float hF = smHpfF.skip (len), lF = smLpfF.skip (len);
        float f[4], g[4], q[4];
        for (int i = 0; i < 4; ++i) { f[i] = smF[i].skip (len); g[i] = smG[i].skip (len); q[i] = smQ[i].skip (len); }
        if (active) { recomputeCoefs (s, hF, lF, f, g, q); force = false; }

        const float hm1 = smHpfMix.skip (len), lm1 = smLpfMix.skip (len), w1 = smWet.skip (len);
        const float mk1 = juce::Decibels::decibelsToGain (smMakeup.skip (len));
        const float dH = (hm1 - hpfMixPrev) / (float) len, dL = (lm1 - lpfMixPrev) / (float) len,
                    dW = (w1 - wetPrev) / (float) len, dM = (mk1 - makeupPrev) / (float) len;
        float hm = hpfMixPrev, lm = lpfMixPrev, wet = wetPrev, mk = makeupPrev;

        for (int i = 0; i < len; ++i)
        {
            hm += dH; lm += dL; wet += dW; mk += dM;
            double dry[2] = { 0, 0 }, y[2] = { 0, 0 };
            for (int ch = 0; ch < nCh; ++ch)
            {
                const double x = d[ch][pos + i]; dry[ch] = x;
                inPk = std::max (inPk, (float) std::fabs (x));
                double h = x; for (int k = 0; k < hpfN; ++k) h = lr::run (hpfC[k], hpfS[ch][k], h);
                double v = x + (h - x) * hm;
                for (int b = 0; b < 4; ++b) v = lr::run (eqC[b], eqS[ch][b], v);
                double l = v; for (int k = 0; k < lpfN; ++k) l = lr::run (lpfC[k], lpfS[ch][k], l);
                y[ch] = v + (l - v) * lm;
            }
            const double det = nCh > 1 ? std::max (std::fabs (y[0]), std::fabs (y[1])) : std::fabs (y[0]);
            const double gr = comp.process (det, s.thresh, s.ratio, s.knee);
            const double gain = std::exp (-gr * 0.11512925464970229) * mk;
            grPk = std::max (grPk, (float) gr);
            detPk = std::max (detPk, (float) det);
            double mono = 0;
            for (int ch = 0; ch < nCh; ++ch)
            {
                const double o = dry[ch] + (y[ch] * gain - dry[ch]) * wet;
                d[ch][pos + i] = (float) o; mono += o;
                outPk = std::max (outPk, (float) std::fabs (o));
            }
            ring[(size_t) ((wp + pos + i) & (kRing - 1))] = (float) (mono / nCh);
        }
        hpfMixPrev = hm1; lpfMixPrev = lm1; wetPrev = w1; makeupPrev = mk1;
    }
    ringPos.store ((wp + n) & (kRing - 1), std::memory_order_release);
    inPeak.store (std::max (inPeak.load(), inPk)); outPeak.store (std::max (outPeak.load(), outPk));
    grMax.store (std::max (grMax.load(), grPk));
    detDb.store (20.f * std::log10 (std::max (detPk, 1e-6f)));
}

juce::AudioProcessorEditor* LRProProcessor::createEditor() { return new LRProEditor (*this); }

void LRProProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void LRProProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LRProProcessor(); }
