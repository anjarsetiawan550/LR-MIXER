#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
// ---------- layout constants (1x logical pixels, canvas 1600 x 960) ----------
constexpr int CW = 1600, CH = 960, KL = 104, KS = 72, PAD = 5, BTN_W = 38, BTN_H = 22, ON_W = 36, ON_H = 20, BY_W = 148, BY_H = 50;
constexpr int MX0 = 628, MPITCH = 153, MY = 596, MW = 140;
const juce::Rectangle<int> COMP_GRAPH { 70, 298, 420, 314 }, FFT_BOX { 628, 298, 904, 282 };
const juce::Rectangle<int> METER_IN { 1022, 87, 320, 14 }, METER_OUT { 1022, 123, 320, 14 }, GR_SLOT { 508, 308, 24, 282 }, BYPASS { 1368, 100, BY_W, BY_H };
const int CX[3] = { 153, 320, 487 }, CY[2] = { 672, 812 };

const juce::Colour VIO = juce::Colour::fromRGB (168, 85, 255), VIOB = juce::Colour::fromRGB (204, 148, 255), VIOC = juce::Colour::fromRGB (240, 226, 255),
                   MAGENTA = juce::Colour::fromRGB (236, 76, 255), PINKC = juce::Colour::fromRGB (255, 84, 170);
const juce::Colour BAND[4] = { juce::Colour::fromRGB (214, 132, 255), juce::Colour::fromRGB (150, 112, 255), juce::Colour::fromRGB (255, 112, 226), juce::Colour::fromRGB (120, 162, 255) };

juce::Image resource (const char* name)
{
    int size = 0; auto* d = BinaryData::getNamedResource (name, size);
    return juce::ImageCache::getFromMemory (d, size);
}

// ---------- widgets ----------
class Static : public juce::Component   // background, cached as bitmap at the current scale
{
public:
    explicit Static (juce::Image i) : img (std::move (i)) { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (juce::Graphics& g) override { g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImage (img, getLocalBounds().toFloat()); }
    juce::Image img;
};

class FilmKnob : public juce::Slider
{
public:
    FilmKnob (juce::Image s, int frameLogical, juce::RangedAudioParameter* p)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox), strip (std::move (s)), F (frameLogical), param (p)
    {
        setMouseDragSensitivity (260); setScrollWheelEnabled (true); setSliderSnapsToMousePosition (false);
    }
    void paint (juce::Graphics& g) override
    {
        const int idx = juce::jlimit (0, 63, (int) std::lround (valueToProportionOfLength (getValue()) * 63.0)), fp = strip.getWidth();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (strip, 0, 0, F, F, 0, idx * fp, fp, fp);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        param->beginChangeGesture(); param->setValueNotifyingHost (param->getDefaultValue()); param->endChangeGesture();
    }
    juce::Image strip; int F; juce::RangedAudioParameter* param;
};

struct KnobUnit
{
    juce::RangedAudioParameter* param; FilmKnob knob; juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment att;
    juce::String lastText; float lastNorm = -1.f;

    KnobUnit (juce::AudioProcessorValueTreeState& a, const juce::String& id, const juce::Image& strip, int F,
              juce::Point<int> c, juce::Rectangle<int> vb, juce::Component& parent)
        : param (a.getParameter (id)), knob (strip, F, param), att (a, id, knob)
    {
        parent.addAndMakeVisible (knob); knob.setBounds (c.x - F / 2, c.y - F / 2, F, F);
        parent.addAndMakeVisible (label); label.setBounds (vb);
        label.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred); label.setBorderSize ({ 0, 0, 0, 0 });
        label.setColour (juce::Label::textColourId, juce::Colour::fromRGB (236, 224, 255));
        label.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        label.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour::fromRGB (30, 18, 60));
        label.setEditable (false, true, true);
        label.onTextChange = [this]
        {
            param->beginChangeGesture(); param->setValueNotifyingHost (param->getValueForText (label.getText())); param->endChangeGesture();
            refresh (true);
        };
        refresh (true);
    }
    void refresh (bool force = false)
    {
        auto t = param->getCurrentValueAsText();
        if (force || t != lastText) { lastText = t; if (! label.isBeingEdited()) label.setText (t, juce::dontSendNotification); }
        const float nrm = param->getValue(); if (nrm != lastNorm) { lastNorm = nrm; knob.repaint(); }
    }
};

class SpriteChoice : public juce::Component   // BW/LR/BS or 12/18/24 selector built from a 2-state sprite sheet
{
public:
    SpriteChoice (juce::AudioParameterChoice* p, juce::Image sheet_, std::vector<juce::Point<int>> framePos_, std::function<bool (int)> veto_ = {}, std::function<void (int)> after_ = {})
        : param (p), sheet (std::move (sheet_)), fp (std::move (framePos_)), veto (std::move (veto_)), after (std::move (after_))
    {
        origin = fp.front();
        setBounds (origin.x, origin.y, fp.back().x + (BTN_W + 2 * PAD) - origin.x, BTN_H + 2 * PAD);
        last = param->getIndex();
    }
    void paint (juce::Graphics& g) override
    {
        const int fwPx = sheet.getWidth(), fhPx = sheet.getHeight() / (2 * (int) fp.size());
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int j = 0; j < (int) fp.size(); ++j)
            g.drawImage (sheet, fp[(size_t) j].x - origin.x, fp[(size_t) j].y - origin.y, BTN_W + 2 * PAD, BTN_H + 2 * PAD,
                         0, (j * 2 + (param->getIndex() == j ? 1 : 0)) * fhPx, fwPx, fhPx);
    }
    bool hitTest (int x, int y) override { return indexAt (x, y) >= 0; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int j = indexAt (e.x, e.y); if (j < 0 || (veto && ! veto (j))) return;
        param->beginChangeGesture(); *param = j; param->endChangeGesture(); if (after) after (j); repaint();
    }
    void poll() { if (param->getIndex() != last) { last = param->getIndex(); repaint(); } }
private:
    int indexAt (int x, int y) const
    {
        for (int j = 0; j < (int) fp.size(); ++j)
            if (juce::Rectangle<int> (fp[(size_t) j].x - origin.x + PAD, fp[(size_t) j].y - origin.y + PAD, BTN_W, BTN_H).contains (x, y)) return j;
        return -1;
    }
    juce::AudioParameterChoice* param; juce::Image sheet; std::vector<juce::Point<int>> fp; std::function<bool (int)> veto; std::function<void (int)> after;
    juce::Point<int> origin; int last = -1;
};

class SpriteToggle : public juce::Component
{
public:
    SpriteToggle (juce::AudioParameterBool* p, juce::Image sheet_, juce::Rectangle<int> frame, juce::Rectangle<int> visible)
        : param (p), sheet (std::move (sheet_)), vis (visible.translated (-frame.getX(), -frame.getY())) { setBounds (frame); last = param->get(); }
    void paint (juce::Graphics& g) override
    {
        const int fhPx = sheet.getHeight() / 2;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sheet, 0, 0, getWidth(), getHeight(), 0, (param->get() ? 1 : 0) * fhPx, sheet.getWidth(), fhPx);
    }
    bool hitTest (int x, int y) override { return vis.contains (x, y); }
    void mouseDown (const juce::MouseEvent&) override { param->beginChangeGesture(); *param = ! param->get(); param->endChangeGesture(); repaint(); }
    void poll() { if (param->get() != last) { last = param->get(); repaint(); } }
private:
    juce::AudioParameterBool* param; juce::Image sheet; juce::Rectangle<int> vis; bool last = false;
};

class HMeter : public juce::Component
{
public:
    HMeter (std::atomic<float>& s, juce::Rectangle<int> r, juce::Image off_, juce::Image on_, juce::Image peak_) : src (s), off (std::move (off_)), on (std::move (on_)), peak (std::move (peak_))
    { setBounds (r); setInterceptsMouseClicks (false, false); }
    void poll()
    {
        const float db = 20.f * std::log10 (std::max (src.exchange (0.f), 1e-6f));
        level = std::max (db, level - 1.8f);
        if (db >= holdDb) { holdDb = db; holdT = 45; } else if (holdT > 0) --holdT; else holdDb = std::max (-60.f, holdDb - 1.2f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto r = getLocalBounds().toFloat(); g.drawImage (off, r);
        const int n = juce::jlimit (0, 40, (int) std::lround ((level + 60.f) / 60.f * 40.f));
        if (n > 0) { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (0, 0, n * 8 - 2, getHeight()); g.drawImage (on, r); }
        if (holdDb > -59.f) { const float x = juce::jlimit (0.f, 314.f, (holdDb + 60.f) / 60.f * 320.f - 3.f); g.drawImage (peak, juce::Rectangle<float> (x, 0.f, 6.f, 14.f)); }
    }
private:
    std::atomic<float>& src; juce::Image off, on, peak; float level = -90.f, holdDb = -90.f; int holdT = 0;
};

class GRMeter : public juce::Component
{
public:
    GRMeter (std::atomic<float>& s, juce::Rectangle<int> r, juce::Image off_, juce::Image on_) : src (s), off (std::move (off_)), on (std::move (on_))
    { setBounds (r); setInterceptsMouseClicks (false, false); }
    void poll() { level = std::max (src.exchange (0.f), level - 1.0f); repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto r = getLocalBounds().toFloat(); g.drawImage (off, r);
        const int n = juce::jlimit (0, 24, (int) std::lround (level));
        if (n > 0) { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (0, 0, getWidth(), (int) std::ceil (n * 282.0 / 24.0) - 1); g.drawImage (on, r); }
    }
    float level = 0.f;
private:
    std::atomic<float>& src; juce::Image off, on;
};

class CompGraph : public juce::Component
{
public:
    CompGraph (LRProProcessor& p, juce::Image glass_) : proc (p), glass (std::move (glass_)) { setBounds (COMP_GRAPH); setInterceptsMouseClicks (false, false); }
    void poll() { dot = std::max (proc.detDb.load(), dot - 1.5f); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto s = proc.readSettings();
        const juce::Rectangle<float> plot (30.f, 10.f, 378.f, 282.f);
        auto X = [&] (double db) { return plot.getX() + (float) ((db + 60.0) / 60.0) * plot.getWidth(); };
        auto Y = [&] (double db) { return plot.getBottom() - (float) ((db + 60.0) / 60.0) * plot.getHeight(); };
        juce::Path curve, area;
        for (int i = 0; i <= 240; ++i)
        {
            const double x = -60.0 + 60.0 * i / 240.0, y = std::max (-60.0, lr::Compressor::outDb (x, s.thresh, s.ratio, s.knee));
            if (i == 0) curve.startNewSubPath (X (x), Y (y)); else curve.lineTo (X (x), Y (y));
        }
        area.startNewSubPath (X (-60), Y (-60));
        for (int i = 0; i <= 240; ++i) { const double x = -60.0 + 60.0 * i / 240.0; area.lineTo (X (x), Y (x)); }
        for (int i = 240; i >= 0; --i) { const double x = -60.0 + 60.0 * i / 240.0; area.lineTo (X (x), Y (std::max (-60.0, lr::Compressor::outDb (x, s.thresh, s.ratio, s.knee)))); }
        area.closeSubPath();
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (plot.toNearestInt());
        g.setColour (MAGENTA.withAlpha (0.20f)); g.fillPath (area);
        juce::Path dash; const float dl[] = { 4.f, 4.f };
        juce::PathStrokeType (1.f).createDashedStroke (dash, [&] { juce::Path l; l.startNewSubPath (X (s.thresh), plot.getY()); l.lineTo (X (s.thresh), plot.getBottom()); return l; }(), dl, 2);
        g.setColour (VIOB.withAlpha (0.9f)); g.fillPath (dash);
        g.setColour (VIO.withAlpha (0.14f)); g.strokePath (curve, juce::PathStrokeType (9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (VIO.withAlpha (0.32f)); g.strokePath (curve, juce::PathStrokeType (5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (VIOC);                  g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const double din = juce::jlimit (-60.0, 0.0, (double) dot), dout = std::max (-60.0, lr::Compressor::outDb (din, s.thresh, s.ratio, s.knee));
        if (dot > -59.f)
        {
            g.setColour (PINKC.withAlpha (0.25f)); g.fillEllipse (X (din) - 9.f, Y (dout) - 9.f, 18.f, 18.f);
            g.setColour (juce::Colour::fromRGB (255, 225, 240)); g.fillEllipse (X (din) - 4.f, Y (dout) - 4.f, 8.f, 8.f);
        }
    }
    void paintOverChildren (juce::Graphics& g) override { g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImage (glass, getLocalBounds().toFloat()); }
private:
    LRProProcessor& proc; juce::Image glass; float dot = -90.f;
};

class FftDisplay : public juce::Component
{
public:
    static constexpr int N = 8192;
    FftDisplay (LRProProcessor& p, juce::Image glass_)
        : proc (p), glass (std::move (glass_)), fft (13), window (N, juce::dsp::WindowingFunction<float>::hann, false), cols ((size_t) plot.getWidth(), -90.f)
    {
        setBounds (FFT_BOX);
        for (int i = 0; i < 4; ++i)
        {
            const juce::String s = "eq" + juce::String (i + 1);
            pf[i] = p.apvts.getParameter (s + "_freq"); pg[i] = p.apvts.getParameter (s + "_gain"); pq[i] = p.apvts.getParameter (s + "_q");
            pon[i] = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (s + "_on"));
        }
    }
    void poll() { computeSpectrum(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto s = proc.readSettings(); const double fs = proc.currentSampleRate > 0 ? proc.currentSampleRate : 48000.0;
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (plot);
        const float W = (float) plot.getWidth(), H = (float) plot.getHeight(), x0 = (float) plot.getX(), y0 = (float) plot.getY();
        // spectrum
        juce::Path line, fill;
        for (int i = 0; i < (int) cols.size(); ++i)
        {
            const float y = y0 + juce::jlimit (0.f, 72.f, -cols[(size_t) i]) / 72.f * H, x = x0 + (float) i + 0.5f;
            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, y0 + H); fill.lineTo (x, y); } else { line.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (x0 + W, y0 + H); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (VIO.withAlpha (0.62f), 0.f, y0, VIO.withAlpha (0.0f), 0.f, y0 + H, false)); g.fillPath (fill);
        g.setColour (VIO.withAlpha (0.22f)); g.strokePath (line, juce::PathStrokeType (3.5f));
        g.setColour (VIOB); g.strokePath (line, juce::PathStrokeType (1.3f));
        // EQ curve
        lr::ResponseModel model; model.build (s, fs);
        const float cy = y0 + H * 0.5f; juce::Path eq, eqFill;
        auto fx = [&] (float freq) { return x0 + W * std::log10 (freq / 20.f) / 3.f; };
        for (int px = 0; px <= (int) W; px += 2)
        {
            const double f = 20.0 * std::pow (1000.0, (double) px / W);
            const float y = cy - (float) juce::jlimit (-18.0, 18.0, model.db (fs, f)) / 18.f * H * 0.5f, x = x0 + (float) px;
            if (px == 0) { eq.startNewSubPath (x, y); eqFill.startNewSubPath (x, cy); eqFill.lineTo (x, y); } else { eq.lineTo (x, y); eqFill.lineTo (x, y); }
        }
        eqFill.lineTo (x0 + W, cy); eqFill.closeSubPath();
        g.setColour (juce::Colour::fromRGB (190, 110, 255).withAlpha (0.15f)); g.fillPath (eqFill);
        g.setColour (juce::Colour::fromRGB (214, 150, 255).withAlpha (0.22f)); g.strokePath (eq, juce::PathStrokeType (6.f));
        g.setColour (juce::Colour::fromRGB (246, 236, 255)); g.strokePath (eq, juce::PathStrokeType (2.f));
        // band handles
        for (int b = 0; b < 4; ++b)
        {
            const auto c = handlePos (b, s); const bool on = s.eq[b].on; const bool hot = hover == b || drag == b;
            g.setColour (BAND[b].withAlpha (on ? (hot ? 0.45f : 0.28f) : 0.10f)); g.fillEllipse (c.x - 11.f, c.y - 11.f, 22.f, 22.f);
            g.setColour (on ? BAND[b] : BAND[b].withAlpha (0.45f)); g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.f, 11.f);
            g.setColour (on ? juce::Colours::white : juce::Colour::fromRGB (30, 18, 60)); g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.setFont (juce::Font (juce::FontOptions (10.f, juce::Font::bold)));
            g.drawText (juce::String (b + 1), (int) c.x - 10, (int) c.y - 24, 20, 12, juce::Justification::centred);
        }
        (void) fx;
    }
    void paintOverChildren (juce::Graphics& g) override { g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); g.drawImage (glass, getLocalBounds().toFloat()); }

    void mouseMove (const juce::MouseEvent& e) override { const int h = nearest (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = nearest (e.position); if (drag < 0) return;
        pf[drag]->beginChangeGesture(); pg[drag]->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const float W = (float) plot.getWidth(), H = (float) plot.getHeight();
        const float nx = juce::jlimit (0.f, 1.f, (e.position.x - (float) plot.getX()) / W);
        const float f = 20.f * std::pow (1000.f, nx);
        const float gdb = juce::jlimit (-18.f, 18.f, -(e.position.y - ((float) plot.getY() + H * 0.5f)) / (H * 0.5f) * 18.f);
        pf[drag]->setValueNotifyingHost (pf[drag]->convertTo0to1 (f));
        if (pon[drag] != nullptr && ! pon[drag]->get()) *pon[drag] = true;
        pg[drag]->setValueNotifyingHost (pg[drag]->convertTo0to1 (gdb));
    }
    void mouseUp (const juce::MouseEvent&) override { if (drag >= 0) { pf[drag]->endChangeGesture(); pg[drag]->endChangeGesture(); drag = -1; } }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = nearest (e.position); if (h < 0 || pon[h] == nullptr) return;
        pon[h]->beginChangeGesture(); *pon[h] = ! pon[h]->get(); pon[h]->endChangeGesture();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int h = nearest (e.position); if (h < 0) { Component::mouseWheelMove (e, w); return; }
        const float q = pq[h]->convertFrom0to1 (pq[h]->getValue()) * std::exp (w.deltaY * 0.9f);
        pq[h]->beginChangeGesture(); pq[h]->setValueNotifyingHost (pq[h]->convertTo0to1 (juce::jlimit (0.1f, 10.f, q))); pq[h]->endChangeGesture();
    }

private:
    juce::Point<float> handlePos (int b, const lr::Settings& s) const
    {
        const float W = (float) plot.getWidth(), H = (float) plot.getHeight();
        return { (float) plot.getX() + W * std::log10 (s.eq[b].freq / 20.f) / 3.f, (float) plot.getY() + H * 0.5f - s.eq[b].gain / 18.f * H * 0.5f };
    }
    int nearest (juce::Point<float> p) const
    {
        const auto s = proc.readSettings(); int best = -1; float bd = 14.f;
        for (int b = 0; b < 4; ++b) { const float d = handlePos (b, s).getDistanceFrom (p); if (d < bd) { bd = d; best = b; } }
        return best;
    }
    void computeSpectrum()
    {
        const int pos = proc.ringPos.load (std::memory_order_acquire);
        for (int i = 0; i < N; ++i) data[(size_t) i] = proc.ring[(size_t) ((pos - N + i + LRProProcessor::kRing) & (LRProProcessor::kRing - 1))];
        std::fill (data.begin() + N, data.end(), 0.f);
        window.multiplyWithWindowingTable (data.data(), N);
        fft.performFrequencyOnlyForwardTransform (data.data());
        const double fs = proc.currentSampleRate > 0 ? proc.currentSampleRate : 48000.0, binHz = fs / N; const int W = (int) cols.size();
        for (int px = 0; px < W; ++px)
        {
            const double k0 = 20.0 * std::pow (1000.0, (double) px / W) / binHz, k1 = 20.0 * std::pow (1000.0, (double) (px + 1) / W) / binHz;
            float m;
            if (k1 - k0 < 1.0) { const double kc = 0.5 * (k0 + k1); const int a = (int) kc; const float fr = (float) (kc - a); m = data[(size_t) a] * (1.f - fr) + data[(size_t) std::min (a + 1, N / 2 - 1)] * fr; }
            else { m = 0.f; for (int k = (int) k0; k <= std::min ((int) k1, N / 2 - 1); ++k) m = std::max (m, data[(size_t) k]); }
            const float db = 20.f * std::log10 (std::max (m / (N * 0.25f), 1e-6f));
            float& c = cols[(size_t) px]; c = db > c ? db : c * 0.82f + db * 0.18f;
        }
    }
    LRProProcessor& proc; juce::Image glass;
    const juce::Rectangle<int> plot { 42, 10, 818, 246 };
    juce::dsp::FFT fft; juce::dsp::WindowingFunction<float> window;
    std::array<float, 2 * N> data {}; std::vector<float> cols;
    juce::RangedAudioParameter *pf[4] {}, *pg[4] {}, *pq[4] {}; juce::AudioParameterBool* pon[4] {};
    int hover = -1, drag = -1;
};
} // namespace

// ================= content (1600 x 960 logical) =================
struct LRProEditor::Impl : public juce::Component
{
    explicit Impl (LRProProcessor& p) : proc (p)
    {
        setSize (CW, CH);
        bg = std::make_unique<Static> (resource ("background_png")); addAndMakeVisible (*bg); bg->setBounds (0, 0, CW, CH);
        auto& a = p.apvts;
        const auto kL = resource ("knob_large_64f_png"), kS = resource ("knob_small_64f_png"), kB = resource ("knob_small_bipolar_64f_png");
        const char* cid[6] = { "comp_thresh", "comp_ratio", "comp_knee", "comp_attack", "comp_release", "comp_makeup" };
        for (int i = 0; i < 6; ++i)
        {
            const int cx = CX[i % 3], cy = CY[i / 3];
            knobs.push_back (std::make_unique<KnobUnit> (a, cid[i], kL, KL, juce::Point<int> (cx, cy), juce::Rectangle<int> (cx - 38, cy + 66, 76, 20), *this));
        }
        const auto bType = resource ("btn_filter_type_png"), bSlope = resource ("btn_filter_slope_png"), bOn = resource ("btn_band_on_png");
        const juce::String mod[6] = { "hpf", "eq1", "eq2", "eq3", "eq4", "lpf" };
        for (int m = 0; m < 6; ++m)
        {
            const int mx = MX0 + MPITCH * m; const bool pass = (m == 0 || m == 5);
            knobs.push_back (std::make_unique<KnobUnit> (a, mod[m] + "_freq", kS, KS, juce::Point<int> (mx + 44, MY + 80), juce::Rectangle<int> (mx + 82, MY + 78, 50, 20), *this));
            if (! pass)
            {
                knobs.push_back (std::make_unique<KnobUnit> (a, mod[m] + "_gain", kB, KS, juce::Point<int> (mx + 44, MY + 160), juce::Rectangle<int> (mx + 82, MY + 158, 50, 20), *this));
                knobs.push_back (std::make_unique<KnobUnit> (a, mod[m] + "_q", kS, KS, juce::Point<int> (mx + 44, MY + 240), juce::Rectangle<int> (mx + 82, MY + 238, 50, 20), *this));
            }
            const juce::Rectangle<int> onFrame (mx + MW - 6 - (ON_W + 2 * PAD), MY + 5, ON_W + 2 * PAD, ON_H + 2 * PAD);
            toggles.push_back (std::make_unique<SpriteToggle> (dynamic_cast<juce::AudioParameterBool*> (a.getParameter (mod[m] + "_on")), bOn, onFrame,
                                                               juce::Rectangle<int> (onFrame.getX() + PAD, onFrame.getY() + PAD, ON_W, ON_H)));
            if (pass)
            {
                std::vector<juce::Point<int>> pt, ps;
                for (int j = 0; j < 3; ++j) { pt.push_back ({ mx + 8 + 43 * j - PAD, MY + 142 - PAD }); ps.push_back ({ mx + 8 + 43 * j - PAD, MY + 194 - PAD }); }
                auto* typeP  = dynamic_cast<juce::AudioParameterChoice*> (a.getParameter (mod[m] + "_type"));
                auto* slopeP = dynamic_cast<juce::AudioParameterChoice*> (a.getParameter (mod[m] + "_slope"));
                // Linkwitz-Riley has no 18 dB/oct variant: block it, and move 18 -> 24 when LR is chosen
                choices.push_back (std::make_unique<SpriteChoice> (typeP, bType, pt, std::function<bool (int)>{},
                                   [typeP, slopeP] (int j) { if (j == 1 && slopeP->getIndex() == 1) *slopeP = 2; }));
                choices.push_back (std::make_unique<SpriteChoice> (slopeP, bSlope, ps, [typeP] (int j) { return ! (typeP->getIndex() == 1 && j == 1); }));
            }
        }
        for (auto& c : choices) addAndMakeVisible (*c);
        for (auto& t : toggles) addAndMakeVisible (*t);
        bypass = std::make_unique<SpriteToggle> (dynamic_cast<juce::AudioParameterBool*> (a.getParameter ("bypass")), resource ("btn_bypass_png"),
                                                 juce::Rectangle<int> (BYPASS.getX() - PAD, BYPASS.getY() - PAD, BY_W + 2 * PAD, BY_H + 2 * PAD), BYPASS);
        addAndMakeVisible (*bypass);
        const auto mOff = resource ("meter_h_off_png"), mOn = resource ("meter_h_on_png"), mPk = resource ("meter_peak_h_png");
        meterIn  = std::make_unique<HMeter> (p.inPeak,  METER_IN,  mOff, mOn, mPk);
        meterOut = std::make_unique<HMeter> (p.outPeak, METER_OUT, mOff, mOn, mPk);
        gr = std::make_unique<GRMeter> (p.grMax, GR_SLOT, resource ("meter_gr_off_png"), resource ("meter_gr_on_png"));
        graph = std::make_unique<CompGraph> (p, resource ("glass_comp_png"));
        fftView = std::make_unique<FftDisplay> (p, resource ("glass_fft_png"));
        for (juce::Component* c : { (juce::Component*) meterIn.get(), (juce::Component*) meterOut.get(), (juce::Component*) gr.get(), (juce::Component*) graph.get(), (juce::Component*) fftView.get() })
            addAndMakeVisible (*c);
    }
    void poll()
    {
        for (auto& k : knobs) k->refresh();
        for (auto& c : choices) c->poll();
        for (auto& t : toggles) t->poll();
        bypass->poll(); meterIn->poll(); meterOut->poll(); gr->poll(); graph->poll(); fftView->poll();
    }
    LRProProcessor& proc;
    std::unique_ptr<Static> bg;
    std::vector<std::unique_ptr<KnobUnit>> knobs; std::vector<std::unique_ptr<SpriteChoice>> choices; std::vector<std::unique_ptr<SpriteToggle>> toggles;
    std::unique_ptr<SpriteToggle> bypass; std::unique_ptr<HMeter> meterIn, meterOut; std::unique_ptr<GRMeter> gr;
    std::unique_ptr<CompGraph> graph; std::unique_ptr<FftDisplay> fftView;
};

LRProEditor::LRProEditor (LRProProcessor& p) : juce::AudioProcessorEditor (p), impl (new Impl (p))
{
    addAndMakeVisible (*impl);
    setResizable (true, true);
    setResizeLimits (800, 480, 2400, 1440);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) CW / CH);
    setSize (1280, 768);
    startTimerHz (30);
}
LRProEditor::~LRProEditor() { stopTimer(); }
void LRProEditor::resized()
{
    impl->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) CW));
    impl->setBounds (0, 0, CW, CH);
}
void LRProEditor::timerCallback() { impl->poll(); }
