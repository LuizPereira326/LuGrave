#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "AdvancedPanel.h"

//==============================================================================
// VU Meter — slick vertical LED bar, red/amber palette
//==============================================================================
class VUMeter : public juce::Component
{
public:
    enum class Color { Green, Red, Amber };
    VUMeter (Color c = Color::Red) : color(c) {}

    void setLevel (float lvl)
    {
        targetLevel = juce::jlimit (0.0f, 1.0f, lvl);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        level += (targetLevel - level) * 0.22f;

        const auto b = getLocalBounds().toFloat().reduced(1);
        const float w = b.getWidth();
        const float h = b.getHeight();
        const float segH   = 3.0f;
        const float segGap = 1.2f;
        const int   numSeg = (int)(h / (segH + segGap));

        // Recessed track — graphite
        g.setColour (juce::Colour(0xFF0E0E0E));
        g.fillRoundedRectangle (b, 2.5f);

        // Inner bevel — graphite
        g.setColour (juce::Colour(0xFF1A1A1A));
        g.drawRoundedRectangle (b.reduced(0.5f), 2.5f, 1.0f);

        // Peak hold
        if (level > peakLevel) { peakLevel = level; peakTimer = 35; }
        if (peakTimer > 0) --peakTimer;
        else if (peakLevel > level) peakLevel = juce::jmax (peakLevel - 0.008f, level);

        const int filledSeg = (int)(level    * numSeg);
        const int peakSeg   = (int)(peakLevel * numSeg);

        for (int i = 0; i < numSeg; ++i)
        {
            const float segY = b.getBottom() - (float)(i + 1) * (segH + segGap) + segGap;
            const float t    = (float)i / (float)numSeg;

            if (i < filledSeg)
            {
                const juce::Colour seg = getSegColor (t);
                // Soft glow behind segment
                g.setColour (seg.withAlpha(0.25f));
                g.fillRoundedRectangle (b.getX(), segY - 1, w, segH + 2, 2.0f);
                // Segment face
                g.setColour (seg);
                g.fillRoundedRectangle (b.getX() + 1, segY, w - 2, segH, 1.5f);
                // Specular top
                g.setColour (juce::Colours::white.withAlpha(0.18f));
                g.fillRect (juce::Rectangle<float>(b.getX() + 1, segY, w - 2, 1.0f));
            }
            else if (i == peakSeg && peakLevel > 0.02f)
            {
                g.setColour (getSegColor(t).withAlpha(0.9f));
                g.fillRoundedRectangle (b.getX() + 1, segY, w - 2, segH, 1.5f);
            }
            else
            {
                g.setColour (juce::Colour(0xFF141414));
                g.fillRoundedRectangle (b.getX() + 1, segY, w - 2, segH, 1.5f);
            }
        }

        // Outer frame — graphite
        g.setColour (juce::Colour(0xFF2C2C2C));
        g.drawRoundedRectangle (b, 2.5f, 0.8f);
    }

private:
    float level = 0.0f, targetLevel = 0.0f, peakLevel = 0.0f;
    int   peakTimer = 0;
    Color color;

    juce::Colour getSegColor (float t) const
    {
        switch (color)
        {
            case Color::Green:
                if (t < 0.65f) return juce::Colour::fromHSV (0.32f - t * 0.08f, 0.9f, 0.55f + t * 0.45f, 1.0f);
                return             juce::Colour::fromHSV (0.07f, 1.0f, 0.95f, 1.0f);
            case Color::Red:
                if (t < 0.70f) return juce::Colour::fromHSV (0.03f - t * 0.01f, 1.0f, 0.50f + t * 0.50f, 1.0f);
                return             juce::Colour::fromHSV (0.97f, 1.0f, 1.0f, 1.0f);
            case Color::Amber:
                if (t < 0.65f) return juce::Colour::fromHSV (0.09f - t * 0.03f, 1.0f, 0.55f + t * 0.45f, 1.0f);
                return             juce::Colour::fromHSV (0.97f, 0.95f, 1.0f, 1.0f);
            default: return juce::Colour(0xFFCC2200);
        }
    }
};

//==============================================================================
// Spectrum Display — live waveform, 3 lines flat when silent, moving with audio
//==============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay() : writeIdx(0)
    {
        for (int i = 0; i < bufSize; ++i)
        {
            inBuf[i]  = 0.0f;
            hrmBuf[i] = 0.0f;
            outBuf[i] = 0.0f;
        }
    }

    void pushSample (float inLvl, float harmLvl, float outLvl)
    {
        // Smoothed center line = 0.0, waveform swings ±level
        inBuf[writeIdx]  =  inLvl  * (randomFloat() * 2.0f - 1.0f);
        hrmBuf[writeIdx] =  harmLvl * (randomFloat() * 2.0f - 1.0f);
        outBuf[writeIdx] =  outLvl  * (randomFloat() * 2.0f - 1.0f);

        writeIdx = (writeIdx + 1) % bufSize;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float midY = b.getCentreY();
        const float halfH = b.getHeight() * 0.42f;
        const float pad   = 4.0f;
        const float drawW = b.getWidth() - pad * 2.0f;
        const float step  = drawW / (float)(bufSize - 1);

        // Grid lines — subtle horizontal references
        g.setColour (juce::Colour(0xFF1E1E1E).withAlpha(0.50f));
        for (int i = 1; i < 5; ++i)
        {
            const float ly = b.getY() + b.getHeight() * (float)i / 5.0f;
            g.drawLine (b.getX() + pad, ly, b.getRight() - pad, ly, 0.4f);
        }
        // Center line — the "flat" reference
        g.setColour (juce::Colour(0xFF2A2A2A).withAlpha(0.50f));
        g.drawLine (b.getX() + pad, midY, b.getRight() - pad, midY, 0.6f);

        // Draw 3 waveform lines
        drawWaveform (g, outBuf,  pad, midY, halfH, step,
                      juce::Colour(0xFFFFAA22), juce::Colour(0xFFCC6600));  // AMBER  — OUT (back)
        drawWaveform (g, inBuf,   pad, midY, halfH, step,
                      juce::Colour(0xFF44BB44), juce::Colour(0xFF22882A));  // GREEN  — IN
        drawWaveform (g, hrmBuf,  pad, midY, halfH, step,
                      juce::Colour(0xFFFF4422), juce::Colour(0xFFAA2200));  // RED    — HRM (front)
    }

private:
    static const int bufSize = 96;
    float inBuf[bufSize], hrmBuf[bufSize], outBuf[bufSize];
    int writeIdx;

    static float randomFloat()
    {
        return (float)(rand() & 0xFFFF) / (float)0xFFFF;
    }

    void drawWaveform (juce::Graphics& g, const float* buf,
                       float pad, float midY, float halfH, float step,
                       juce::Colour col, juce::Colour colDim)
    {
        juce::Path path;
        bool started = false;
        for (int i = 0; i < bufSize; ++i)
        {
            const int idx = (writeIdx + i) % bufSize;
            const float x  = pad + (float)i * step;
            const float y  = midY - buf[idx] * halfH;

            if (! started) { path.startNewSubPath (x, y); started = true; }
            else           { path.lineTo (x, y); }
        }

        // Glow layer - use dimmer color for soft glow
        g.setColour (colDim.withAlpha(0.15f));
        g.strokePath (path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        // Core line - use brighter color
        g.setColour (col.withAlpha(0.75f));
        g.strokePath (path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
};

//==============================================================================
// ProLookAndFeel — Graphite Dark Professional Theme
//==============================================================================
class ProLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ProLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    juce::Colour accent     = juce::Colour(0xFFCC2200);
    juce::Colour accentBrt  = juce::Colour(0xFFFF4422);
};

//==============================================================================
class MaxxBassAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit MaxxBassAudioProcessorEditor (MaxxBassAudioProcessor&);
    ~MaxxBassAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void timerCallback() override;

private:
    MaxxBassAudioProcessor& proc;
    ProLookAndFeel laf;

    // Knobs
    juce::Slider cutFreqKnob, harmMixKnob, driveKnob,
                 harmCharKnob, dynRespKnob, outputGainKnob;

    // Labels
    juce::Label cutLbl, mixLbl, driveLbl, charLbl, dynLbl, outLbl;

    // Buttons
    juce::ToggleButton bypassBtn;
    juce::ToggleButton subProtectBtn;
    juce::ToggleButton modeBtn;

    // VU Meters
    VUMeter inMeter   { VUMeter::Color::Green };
    VUMeter harmMeter { VUMeter::Color::Red   };
    VUMeter outMeter  { VUMeter::Color::Amber };

    // Spectrum
    SpectrumDisplay spectrumDisplay;

    // Advanced params window
    std::unique_ptr<AdvancedPanelWindow> advPanelWindow;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        cutAttach, mixAttach, driveAttach, charAttach, dynAttach, outAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        bypassAttach, subProtectAttach, modeAttach;

    void setupKnob (juce::Slider& k, juce::Label& l,
                    const juce::String& txt, const juce::String& paramId);
    void updateModeVisuals();
    void updateSubProtectVisuals();
    void openAdvancedPanel();

    // Settings button
    juce::TextButton settingsBtn;

    bool isSimpleMode() const
    {
        return proc.apvts.getRawParameterValue("simpleMode")->load() > 0.5f;
    }

    float glowPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaxxBassAudioProcessorEditor)
};
