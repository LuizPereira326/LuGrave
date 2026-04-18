#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// ProLookAndFeel — Graphite Dark Professional Theme
//==============================================================================
ProLookAndFeel::ProLookAndFeel()
{
    accent    = juce::Colour(0xFFCC2200);
    accentBrt = juce::Colour(0xFFFF4422);

    setColour (juce::Slider::textBoxTextColourId,       juce::Colour(0xFFFF8866));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF151515));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour(0xFF303030));
    setColour (juce::Slider::textBoxHighlightColourId,  juce::Colour(0x40CC2200));
}

//==============================================================================
void ProLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                        int x, int y, int w, int h,
                                        float pos, float startA, float endA,
                                        juce::Slider& slider)
{
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    // Geometria base
    const float radius  = juce::jmin (w / 2, h / 2) - 4.0f;
    const float centreX = x + w * 0.5f;
    const float centreY = y + h * 0.5f;
    const float rx      = centreX - radius;
    const float ry      = centreY - radius;
    const float rw      = radius * 2.0f;

    // Ângulo atual do knob
    const float angle = startA + pos * (endA - startA);

    //==========================================================================
    // 1. SOMBRA (Drop Shadow) — profundidade do knob
    //==========================================================================
    g.setColour (juce::Colours::black.withAlpha (0.4f * alpha));
    g.fillEllipse (rx + 2.0f, ry + 3.0f, rw, rw);

    //==========================================================================
    // 2. BORDA EXTERNA — base das ranhuras (knurling) — cinza médio/escuro
    //==========================================================================
    {
        juce::ColourGradient edgeGrad (
            juce::Colour(0xFF777777), centreX, ry,
            juce::Colour(0xFF444444), centreX, ry + rw, false);
        g.setGradientFill (edgeGrad);
        g.setOpacity (alpha);
        g.fillEllipse (rx, ry, rw, rw);
        g.setOpacity (1.0f);
    }

    // Desenhando as ranhuras da lateral (knurling — linhas radiais)
    {
        g.setColour (juce::Colour(0xFF333333).withAlpha (0.8f * alpha));
        const int   numRidges  = 72;   // Quantidade de "dentes" na borda
        const float topRadius  = radius - 3.5f;  // Raio da face superior do knob

        for (int i = 0; i < numRidges; ++i)
        {
            const float ridgeAngle = juce::MathConstants<float>::twoPi
                                     * ((float)i / (float)numRidges);
            const float x1 = centreX + radius    * std::cos (ridgeAngle);
            const float y1 = centreY + radius    * std::sin (ridgeAngle);
            const float x2 = centreX + topRadius * std::cos (ridgeAngle);
            const float y2 = centreY + topRadius * std::sin (ridgeAngle);
            g.drawLine (x1, y1, x2, y2, 1.2f);
        }

        //======================================================================
        // 3. TOPO — Metal Cinza (Alumínio / Prata)
        //======================================================================
        const float trx = centreX - topRadius;
        const float trY = centreY - topRadius;
        const float trw = topRadius * 2.0f;

        // Gradiente diagonal com múltiplos stops — simula reflexo do alumínio escovado
        juce::ColourGradient topGrad (
            juce::Colour(0xFFF5F5F5), trx,       trY,
            juce::Colour(0xFF888888), trx + trw, trY + trw, false);
        topGrad.addColour (0.3, juce::Colour(0xFFCCCCCC)); // Reflexo intermediário
        topGrad.addColour (0.7, juce::Colour(0xFFAAAAAA)); // Sombra intermediária

        g.setGradientFill (topGrad);
        g.setOpacity (alpha);
        g.fillEllipse (trx, trY, trw, trw);
        g.setOpacity (1.0f);

        // Contorno suave — separa o topo das ranhuras
        g.setColour (juce::Colour(0xFF555555).withMultipliedAlpha (alpha));
        g.drawEllipse (trx, trY, trw, trw, 1.0f);

        // Specular highlight — brilho direcional no topo esquerdo do metal
        {
            juce::ColourGradient spec (
                juce::Colour(0x18FFFFFF),
                centreX - topRadius * 0.3f, centreY - topRadius * 0.5f,
                juce::Colour(0x00000000),
                centreX + topRadius * 0.1f, centreY - topRadius * 0.1f, true);
            g.setGradientFill (spec);
            g.fillEllipse (centreX - topRadius * 0.75f, centreY - topRadius * 0.8f,
                           topRadius * 1.5f, topRadius * 0.85f);
        }

        //======================================================================
        // 4. INDICADOR — Sulco em baixo relevo (groove)
        //======================================================================
        const float pointerLength = topRadius * 0.45f;
        const float pointerWidth  = 3.5f;

        // Fundo do sulco — parte escura simulando profundidade
        {
            juce::Path indicatorPath;
            indicatorPath.addRoundedRectangle (-pointerWidth * 0.5f,
                                               -topRadius * 0.85f,
                                               pointerWidth,
                                               pointerLength, 1.5f);
            indicatorPath.applyTransform (
                juce::AffineTransform::rotation (angle).translated (centreX, centreY));

            g.setColour (juce::Colour(0xFF1A1A1A).withMultipliedAlpha (alpha));
            g.fillPath (indicatorPath);
        }

        // Linha interna clara — marcação branca / reflexo dentro do sulco
        {
            juce::Path innerLine;
            innerLine.addRoundedRectangle (-1.0f,
                                           -topRadius * 0.82f,
                                           2.0f,
                                           pointerLength * 0.75f, 1.0f);
            innerLine.applyTransform (
                juce::AffineTransform::rotation (angle).translated (centreX, centreY));

            g.setColour (juce::Colours::white.withAlpha (0.9f * alpha));
            g.fillPath (innerLine);
        }

    }
}

//==============================================================================
void ProLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
                                        bool highlighted, bool /*down*/)
{
    const bool on = btn.getToggleState();
    const auto b  = btn.getLocalBounds().toFloat().reduced (1.5f);
    const juce::String id = btn.getComponentID();

    // ---- MODE BUTTON (SIMPLE / HYBRID) ----  [UNCHANGED — buttons are perfect]
    if (id == "modeBtn")
    {
        const juce::Colour bgDark = on ? juce::Colour(0xFF2A1200) : juce::Colour(0xFF1A0808);
        const juce::Colour bgMid  = on ? juce::Colour(0xFF180A00) : juce::Colour(0xFF0E0404);
        const juce::Colour edge   = on ? juce::Colour(0xFFFF8800) : juce::Colour(0xFFCC2200);
        const juce::Colour ledCol = on ? juce::Colour(0xFFFFAA00) : juce::Colour(0xFFFF3300);
        const juce::Colour txt    = on ? juce::Colour(0xFFFFDDA0) : juce::Colour(0xFFFFB0A0);

        if (on || highlighted)
        {
            g.setColour (edge.withAlpha(highlighted ? 0.25f : 0.15f));
            g.fillRoundedRectangle (b.expanded(3), 8.0f);
        }

        juce::ColourGradient bg (bgDark, b.getX(), b.getY(), bgMid, b.getX(), b.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (b, 5.0f);

        // Top glass sheen
        g.setColour (juce::Colour(0x12FFFFFF));
        g.fillRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight() * 0.45f, 5.0f);

        // Border
        g.setColour (edge.withAlpha(highlighted ? 0.9f : 0.65f));
        g.drawRoundedRectangle (b, 5.0f, 1.2f);

        // LED dot
        const float ledR = 3.5f, ledX = b.getX() + 9.0f, ledY = b.getCentreY();
        g.setColour (ledCol.withAlpha(0.25f));
        g.fillEllipse (ledX - ledR * 1.5f, ledY - ledR * 1.5f, ledR * 3, ledR * 3);
        g.setColour (ledCol);
        g.fillEllipse (ledX - ledR, ledY - ledR, ledR * 2, ledR * 2);
        g.setColour (juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse (ledX - 1.2f, ledY - 1.5f, 2.2f, 1.8f);

        g.setFont (juce::FontOptions(10.5f).withStyle("Bold"));
        g.setColour (txt);
        g.drawText (btn.getButtonText(),
                    (int)(b.getX() + 18), (int)b.getY(),
                    (int)(b.getWidth() - 20), (int)b.getHeight(),
                    juce::Justification::centredLeft);
        return;
    }

    // ---- SUB CUT BUTTON ----  [UNCHANGED — buttons are perfect]
    if (id == "subBtn")
    {
        const juce::Colour bgDark = on ? juce::Colour(0xFF0E3020) : juce::Colour(0xFF0E0A08);
        const juce::Colour bgMid  = on ? juce::Colour(0xFF081E12) : juce::Colour(0xFF080604);
        const juce::Colour edge   = on ? juce::Colour(0xFF44CC66) : juce::Colour(0xFF2A2018);
        const juce::Colour ledCol = on ? juce::Colour(0xFF55FF77) : juce::Colour(0xFF223318);
        const juce::Colour txt    = on ? juce::Colour(0xFFCCFFDD) : juce::Colour(0xFF706050);

        if (on || highlighted)
        {
            g.setColour (edge.withAlpha(highlighted ? 0.20f : 0.10f));
            g.fillRoundedRectangle (b.expanded(3), 8.0f);
        }

        juce::ColourGradient bg (bgDark, b.getX(), b.getY(), bgMid, b.getX(), b.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (b, 5.0f);

        g.setColour (juce::Colour(0x10FFFFFF));
        g.fillRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight() * 0.45f, 5.0f);

        g.setColour (edge.withAlpha(highlighted ? 0.85f : 0.55f));
        g.drawRoundedRectangle (b, 5.0f, 1.2f);

        const float ledR = 3.5f, ledX = b.getX() + 9.0f, ledY = b.getCentreY();
        g.setColour (ledCol.withAlpha(on ? 0.30f : 0.10f));
        g.fillEllipse (ledX - ledR * 1.5f, ledY - ledR * 1.5f, ledR * 3, ledR * 3);
        g.setColour (ledCol);
        g.fillEllipse (ledX - ledR, ledY - ledR, ledR * 2, ledR * 2);
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha(0.6f));
            g.fillEllipse (ledX - 1.2f, ledY - 1.5f, 2.2f, 1.8f);
        }

        g.setFont (juce::FontOptions(10.0f).withStyle("Bold"));
        g.setColour (txt);
        g.drawText (btn.getButtonText(),
                    (int)(b.getX() + 18), (int)b.getY(),
                    (int)(b.getWidth() - 20), (int)b.getHeight(),
                    juce::Justification::centredLeft);
        return;
    }

    // ---- BYPASS BUTTON ----  [UNCHANGED — buttons are perfect]
    {
        const juce::Colour bgDark = on ? juce::Colour(0xFF350A0A) : juce::Colour(0xFF0E0A08);
        const juce::Colour bgMid  = on ? juce::Colour(0xFF200505) : juce::Colour(0xFF080604);
        const juce::Colour edge   = on ? juce::Colour(0xFFFF3322) : juce::Colour(0xFF2A1A18);
        const juce::Colour ledCol = on ? juce::Colour(0xFFFF4433) : juce::Colour(0xFF331A18);
        const juce::Colour txt    = on ? juce::Colour(0xFFFFCCCC) : juce::Colour(0xFF706050);

        if (on || highlighted)
        {
            g.setColour (edge.withAlpha(highlighted ? 0.20f : 0.10f));
            g.fillRoundedRectangle (b.expanded(3), 8.0f);
        }

        juce::ColourGradient bg (bgDark, b.getX(), b.getY(), bgMid, b.getX(), b.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (b, 5.0f);

        g.setColour (juce::Colour(0x0EFFFFFF));
        g.fillRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight() * 0.45f, 5.0f);

        g.setColour (edge.withAlpha(highlighted ? 0.85f : 0.50f));
        g.drawRoundedRectangle (b, 5.0f, 1.2f);

        const float ledR = 3.5f, ledX = b.getX() + 9.0f, ledY = b.getCentreY();
        g.setColour (ledCol.withAlpha(on ? 0.28f : 0.10f));
        g.fillEllipse (ledX - ledR * 1.5f, ledY - ledR * 1.5f, ledR * 3, ledR * 3);
        g.setColour (ledCol);
        g.fillEllipse (ledX - ledR, ledY - ledR, ledR * 2, ledR * 2);
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha(0.55f));
            g.fillEllipse (ledX - 1.2f, ledY - 1.5f, 2.2f, 1.8f);
        }

        g.setFont (juce::FontOptions(10.5f).withStyle("Bold"));
        g.setColour (txt);
        g.drawText (btn.getButtonText(),
                    (int)(b.getX() + 18), (int)b.getY(),
                    (int)(b.getWidth() - 20), (int)b.getHeight(),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void ProLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& lbl)
{
    g.fillAll (juce::Colours::transparentBlack);
    const float a = lbl.isEnabled() ? 1.0f : 0.30f;
    g.setColour (juce::Colour(0xFF8A8A8A).withAlpha(a));
    g.setFont (juce::FontOptions(9.5f).withStyle("Bold"));
    g.drawText (lbl.getText(), lbl.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
// Editor Constructor
//==============================================================================
MaxxBassAudioProcessorEditor::MaxxBassAudioProcessorEditor (MaxxBassAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize (820, 420);

    setupKnob (cutFreqKnob,    cutLbl,  "FREQ",      "cutFreq");
    setupKnob (harmMixKnob,    mixLbl,  "BASS FAKE", "harmMix");
    setupKnob (driveKnob,      driveLbl,"DRIVE",      "drive");
    setupKnob (harmCharKnob,   charLbl, "CHAR",       "harmChar");
    setupKnob (dynRespKnob,    dynLbl,  "DYNAMICS",   "dynResp");
    setupKnob (outputGainKnob, outLbl,  "OUTPUT",     "outputGain");

    bypassBtn.setButtonText ("BYPASS");
    bypassBtn.setLookAndFeel (&laf);
    addAndMakeVisible (bypassBtn);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "bypass", bypassBtn);

    subProtectBtn.setComponentID ("subBtn");
    subProtectBtn.setButtonText ("SUB CUT ON");
    subProtectBtn.setLookAndFeel (&laf);
    addAndMakeVisible (subProtectBtn);
    subProtectAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "subProtect", subProtectBtn);

    modeBtn.setComponentID ("modeBtn");
    modeBtn.setLookAndFeel (&laf);
    addAndMakeVisible (modeBtn);
    modeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "simpleMode", modeBtn);

    modeBtn.onClick       = [this] { updateModeVisuals(); };
    subProtectBtn.onClick = [this] { updateSubProtectVisuals(); };

    // SETTINGS button — abre painel de parâmetros avançados
    settingsBtn.setButtonText ("SETTINGS");
    settingsBtn.setColour (juce::TextButton::buttonColourId,        juce::Colour(0xFF1A1212));
    settingsBtn.setColour (juce::TextButton::buttonOnColourId,      juce::Colour(0xFF2A1818));
    settingsBtn.setColour (juce::TextButton::textColourOnId,  juce::Colour(0xFFFF6644));
    settingsBtn.setColour (juce::TextButton::textColourOffId, juce::Colour(0xFF994422));
    settingsBtn.setColour (juce::ComboBox::outlineColourId,        juce::Colour(0xFFCC2200));
    settingsBtn.onClick = [this] { openAdvancedPanel(); };
    addAndMakeVisible (settingsBtn);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (harmMeter);
    addAndMakeVisible (outMeter);

    addAndMakeVisible (spectrumDisplay);

    updateModeVisuals();
    updateSubProtectVisuals();
    startTimerHz (15);
}

MaxxBassAudioProcessorEditor::~MaxxBassAudioProcessorEditor()
{
    stopTimer();
    for (auto* s : { &cutFreqKnob, &harmMixKnob, &driveKnob,
                     &harmCharKnob, &dynRespKnob, &outputGainKnob })
        s->setLookAndFeel (nullptr);
    bypassBtn    .setLookAndFeel (nullptr);
    subProtectBtn.setLookAndFeel (nullptr);
    modeBtn      .setLookAndFeel (nullptr);
}

//==============================================================================
void MaxxBassAudioProcessorEditor::setupKnob (juce::Slider& k, juce::Label& l,
                                               const juce::String& txt,
                                               const juce::String& paramId)
{
    k.setSliderStyle  (juce::Slider::RotaryVerticalDrag);
    k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
    k.setLookAndFeel  (&laf);
    addAndMakeVisible (k);

    l.setText (txt, juce::dontSendNotification);
    l.setLookAndFeel  (&laf);
    addAndMakeVisible (l);

    if      (paramId == "cutFreq")    cutAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
    else if (paramId == "harmMix")    mixAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
    else if (paramId == "drive")      driveAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
    else if (paramId == "harmChar")   charAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
    else if (paramId == "dynResp")    dynAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
    else if (paramId == "outputGain") outAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, paramId, k);
}

//==============================================================================
void MaxxBassAudioProcessorEditor::updateModeVisuals()
{
    const bool simple = isSimpleMode();
    modeBtn.setButtonText (simple ? "SIMPLE" : "HYBRID");

    if (simple)
    {
        laf.accent    = juce::Colour(0xFFCC7700);
        laf.accentBrt = juce::Colour(0xFFFFAA22);
    }
    else
    {
        laf.accent    = juce::Colour(0xFFCC2200);
        laf.accentBrt = juce::Colour(0xFFFF4422);
    }

    laf.setColour (juce::Slider::textBoxTextColourId,
                   laf.accentBrt.withAlpha(0.85f));
    laf.setColour (juce::Slider::textBoxBackgroundColourId,
                   juce::Colour(0xFF151515));
    laf.setColour (juce::Slider::textBoxOutlineColourId,
                   laf.accent.withAlpha(0.25f));

    harmCharKnob.setEnabled (true);
    dynRespKnob .setEnabled (true);
    charLbl     .setEnabled (true);
    dynLbl      .setEnabled (true);
    repaint();
}

//==============================================================================
void MaxxBassAudioProcessorEditor::updateSubProtectVisuals()
{
    const bool on = proc.apvts.getRawParameterValue ("subProtect")->load() > 0.5f;
    subProtectBtn.setButtonText (on ? "SUB CUT ON" : "SUB CUT OFF");
}

//==============================================================================
void MaxxBassAudioProcessorEditor::openAdvancedPanel()
{
    proc.refreshAdvancedParamsFromAPVTS();

    if (advPanelWindow != nullptr)
        advPanelWindow.reset();

    // CORREÇÃO v49: Construtor simplificado
    advPanelWindow = std::make_unique<AdvancedPanelWindow> (
        proc.audioParams,
        &proc.apvts,
        isSimpleMode(),
        *this);

    advPanelWindow->toFront (true);
}

//==============================================================================
void MaxxBassAudioProcessorEditor::timerCallback()
{
    glowPhase += 0.03f;
    if (glowPhase > 1.0f) glowPhase -= 1.0f;

    const float decay  = 0.88f;
    const float inLvl  = proc.inLevel  .load() * decay;
    const float hLvl   = proc.harmMeter.load() * decay * 2.0f;
    const float outLvl = proc.outLevel .load() * decay;

    inMeter  .setLevel (inLvl);
    harmMeter.setLevel (hLvl);
    outMeter .setLevel (outLvl);

    spectrumDisplay.pushSample (inLvl, hLvl, outLvl);

    modeBtn.setButtonText (isSimpleMode() ? "SIMPLE" : "HYBRID");
    updateSubProtectVisuals();
}

//==============================================================================
// PAINT — Full Background & Panels (Midnight Blue-Black Theme)
//==============================================================================
void MaxxBassAudioProcessorEditor::paint (juce::Graphics& g)
{
    const bool simple   = isSimpleMode();
    const int  W        = getWidth();
    const int  H        = getHeight();
    const auto accentCol = simple ? juce::Colour(0xFFCC7700) : juce::Colour(0xFFCC2200);
    const auto accentBrt = simple ? juce::Colour(0xFFFFAA22) : juce::Colour(0xFFFF4422);

    //==========================================================================
    // BACKGROUND — clean dark graphite (NO color contamination)
    //==========================================================================
    {
        juce::ColourGradient bg (
            juce::Colour(0xFF181818), 0, 0,
            juce::Colour(0xFF111111), (float)W, (float)H, false);
        g.setGradientFill (bg);
        g.fillAll();
    }

    //==========================================================================
    // FINE WOVEN TEXTURE — crosshatch like SlickEQ fabric
    //==========================================================================
    {
        // Horizontal thread lines
        g.setColour (juce::Colour(0xFFFFFFFF).withAlpha(0.012f));
        for (int y = 0; y < H; y += 3)
            g.drawLine (0, (float)y, (float)W, (float)y, 0.5f);

        // Vertical thread lines (lighter to give weave depth)
        g.setColour (juce::Colour(0xFF000000).withAlpha(0.018f));
        for (int x = 0; x < W; x += 3)
            g.drawLine ((float)x, 0, (float)x, (float)H, 0.5f);

        // Diagonal micro-weave overlay
        g.setColour (juce::Colour(0xFFFFFFFF).withAlpha(0.006f));
        for (int i = -H; i < W; i += 5)
            g.drawLine ((float)i, 0, (float)(i + H), (float)H, 0.5f);
    }

    //==========================================================================
    // VIGNETTE
    //==========================================================================
    {
        juce::ColourGradient vig (
            juce::Colour(0x00000000), W * 0.5f, H * 0.5f,
            juce::Colour(0x60000000), W * 0.5f, H * 0.5f, true);
        g.setGradientFill (vig);
        g.fillAll();
    }

    //==========================================================================
    // TOP INDICATOR BAR — thin accent line (unchanged — uses accent)
    //==========================================================================
    {
        const float pulse = 0.65f + 0.35f * std::sin (glowPhase * juce::MathConstants<float>::twoPi);
        // Glow halo
        g.setColour (accentCol.withAlpha(0.12f * pulse));
        g.fillRect (0, 0, W, 8);
        // Core line
        juce::ColourGradient bar (
            accentCol.withAlpha(0.0f), 0.0f, 0.0f,
            accentBrt.withAlpha(0.85f * pulse), (float)(W / 2), 0.0f, false);
        g.setGradientFill (bar);
        g.fillRect (0, 0, W / 2, 2);
        juce::ColourGradient bar2 (
            accentBrt.withAlpha(0.85f * pulse), (float)(W / 2), 0.0f,
            accentCol.withAlpha(0.0f), (float)W, 0.0f, false);
        g.setGradientFill (bar2);
        g.fillRect (W / 2, 0, W / 2, 2);
        // White hot center
        g.setColour (juce::Colours::white.withAlpha(0.30f * pulse));
        g.fillRect (0, 0, W, 1);
    }

    //==========================================================================
    // HEADER PANEL — midnight steel
    //==========================================================================
    {
        const float hX = 10.0f, hY = 6.0f, hW = (float)(W - 20), hH = 52.0f;

        // Main header body — graphite steel
        juce::ColourGradient hBg (
            juce::Colour(0xFF1C1C1C), hX, hY,
            juce::Colour(0xFF161616), hX, hY + hH, false);
        g.setGradientFill (hBg);
        g.fillRoundedRectangle (hX, hY, hW, hH, 7.0f);

        // Top specular sheen
        g.setColour (juce::Colour(0x0DFFFFFF));
        g.fillRoundedRectangle (hX, hY, hW, hH * 0.38f, 7.0f);

        // Bottom inner shadow line
        g.setColour (juce::Colour(0x20000000));
        g.fillRect (hX + 1, hY + hH - 2, hW - 2, 2.0f);

        // Top border accent
        g.setColour (accentCol.withAlpha(0.45f));
        g.drawLine (hX + 8, hY + 1, hX + hW - 8, hY + 1, 0.8f);

        // Full border — graphite
        g.setColour (juce::Colour(0xFF2E2E2E));
        g.drawRoundedRectangle (hX, hY, hW, hH, 7.0f, 0.8f);
    }

    //==========================================================================
    // TITLE NAMEPLATE — stamped metal badge (premium branding)
    //==========================================================================
    {
        const float npX = 16.0f, npY = 9.0f, npW = 252.0f, npH = 32.0f;

        // Nameplate body — slightly lighter than header, brushed metal feel
        juce::ColourGradient npBg (
            juce::Colour(0xFF202020), npX, npY,
            juce::Colour(0xFF181818), npX, npY + npH, false);
        g.setGradientFill (npBg);
        g.fillRoundedRectangle (npX, npY, npW, npH, 4.0f);

        // Inner brushed-metal sheen (top half)
        g.setColour (juce::Colour(0x0CFFFFFF));
        g.fillRoundedRectangle (npX, npY, npW, npH * 0.45f, 4.0f);

        // Top bevel highlight — light hitting the raised edge
        g.setColour (juce::Colour(0xFF333333).withAlpha(0.45f));
        g.drawLine (npX + 4, npY + 0.8f, npX + npW - 4, npY + 0.8f, 0.6f);
        // Left bevel highlight
        g.drawLine (npX + 0.8f, npY + 4, npX + 0.8f, npY + npH - 4, 0.4f);

        // Bottom bevel shadow — darker edge below
        g.setColour (juce::Colour(0xFF0A0A0A).withAlpha(0.45f));
        g.drawLine (npX + 4, npY + npH - 0.8f, npX + npW - 4, npY + npH - 0.8f, 0.6f);
        // Right bevel shadow
        g.drawLine (npX + npW - 0.8f, npY + 4, npX + npW - 0.8f, npY + npH - 4, 0.4f);

        // Thin metal border
        g.setColour (juce::Colour(0xFF333333).withAlpha(0.55f));
        g.drawRoundedRectangle (npX, npY, npW, npH, 4.0f, 0.6f);

        // Inner recessed line (inset shadow)
        g.setColour (juce::Colour(0xFF0E0E0E).withAlpha(0.30f));
        g.drawRoundedRectangle (npX + 1.0f, npY + 1.0f, npW - 2.0f, npH - 2.0f, 3.0f, 0.4f);
    }

    //==========================================================================
    // LOGO TEXT — silk-screen printed, bold with letter-spacing & glow
    //==========================================================================
    {
        auto titleFont = juce::Font(juce::FontOptions(30.0f).withStyle("Bold"));
        titleFont.setHorizontalScale(1.10f);  // letter-spacing / tracking
        g.setFont (titleFont);

        const int tx = 24, ty = 8, tw = 240, th = 32;

        // Layer 1: Outer glow halo — accent color spread
        g.setColour (accentCol.withAlpha(0.10f));
        g.drawText ("MAXX BASS", tx - 1, ty + 2, tw + 2, th, juce::Justification::centredLeft);
        g.setColour (accentCol.withAlpha(0.06f));
        g.drawText ("MAXX BASS", tx - 2, ty + 3, tw + 4, th, juce::Justification::centredLeft);

        // Layer 2: Drop shadow — silk-screen printed letters cast shadow below
        g.setColour (juce::Colour(0xFF000000).withAlpha(0.60f));
        g.drawText ("MAXX BASS", tx + 1, ty + 1, tw, th, juce::Justification::centredLeft);

        // Layer 3: Main text — bright accent
        g.setColour (accentBrt);
        g.drawText ("MAXX BASS", tx, ty, tw, th, juce::Justification::centredLeft);

        // Layer 4: Bottom-edge specular — light catching the bottom of printed letters
        g.setColour (juce::Colours::white.withAlpha(0.14f));
        g.drawText ("MAXX BASS", tx, ty + 1, tw, th, juce::Justification::centredLeft);

        // Layer 5: Top-edge shadow — engraved bevel on top edge of letters
        g.setColour (juce::Colour(0xFF000000).withAlpha(0.10f));
        g.drawText ("MAXX BASS", tx, ty - 1, tw, th, juce::Justification::centredLeft);
    }

    // SUBTITLE — subdued, clear hierarchy (NOT accent — neutral gray)
    {
        auto subFont = juce::Font(juce::FontOptions(7.0f));
        subFont.setHorizontalScale(1.05f);
        g.setFont (subFont);
        g.setColour (juce::Colour(0xFF606060));
        const juce::String sub = simple
            ? "SIMPLE  \u00B7  CHARACTER & DYNAMICS  \u00B7  SUB CUT  \u00B7  LR4  \u00B7  2X OS"
            : "HYBRID  \u00B7  PSYCHOACOUSTIC HARMONICS  \u00B7  LR4  \u00B7  DYNAMIC CTRL  \u00B7  2X OS";
        g.drawText (sub, 22, 42, W - 220, 12, juce::Justification::centredLeft);
    }

    //==========================================================================
    // KNOB PANEL — sunken inset look (midnight finish)
    //==========================================================================
    {
        const float pX = 10.0f, pY = 66.0f;
        const float pW = (float)(W - 20 - 114);
        const float pH = (float)(H - 76);

        // Outer shadow ring (beveled look)
        g.setColour (juce::Colour(0x40000000));
        g.fillRoundedRectangle (pX - 1, pY + 2, pW + 2, pH + 1, 7.0f);

        // Panel body — graphite gradient
        juce::ColourGradient pBg (
            juce::Colour(0xFF161616), pX, pY,
            juce::Colour(0xFF101010), pX, pY + pH, false);
        g.setGradientFill (pBg);
        g.fillRoundedRectangle (pX, pY, pW, pH, 7.0f);

        // Woven texture on panel
        g.setColour (juce::Colour(0xFFFFFFFF).withAlpha(0.008f));
        for (int y = (int)pY; y < (int)(pY + pH); y += 4)
            g.drawLine (pX + 2, (float)y, pX + pW - 2, (float)y, 0.4f);
        g.setColour (juce::Colour(0xFF000000).withAlpha(0.014f));
        for (int x = (int)pX; x < (int)(pX + pW); x += 4)
            g.drawLine ((float)x, pY + 2, (float)x, pY + pH - 2, 0.4f);

        // Top specular edge — lifted bevel (graphite)
        g.setColour (juce::Colour(0xFF303030));
        g.drawLine (pX + 6, pY + 0.8f, pX + pW - 6, pY + 0.8f, 0.8f);
        g.setColour (juce::Colour(0x08FFFFFF));
        g.drawLine (pX + 6, pY + 1.5f, pX + pW - 6, pY + 1.5f, 0.5f);

        // Panel border — graphite
        g.setColour (juce::Colour(0xFF242424));
        g.drawRoundedRectangle (pX, pY, pW, pH, 7.0f, 0.8f);

        // Colored left accent stripe (uses accent color)
        g.setColour (accentCol.withAlpha(0.18f));
        g.fillRoundedRectangle (pX + 2, pY + 8, 1.5f, pH - 16, 1.0f);
    }

    //==========================================================================
    // COLUMN SEPARATORS — subtle etched lines (blue-gray)
    //==========================================================================
    {
        const float pX = 10.0f, pY = 66.0f, pH = (float)(H - 76);
        const float pW = (float)(W - 20 - 114);
        const float kw = pW / 6.0f;

        for (int i = 1; i < 6; ++i)
        {
            const float lx = pX + kw * (float)i;
            // Dark left edge
            g.setColour (juce::Colour(0xFF0C0C0C));
            g.drawLine (lx, pY + 8, lx, pY + pH - 8, 0.8f);
            // Light right edge — graphite
            g.setColour (juce::Colour(0xFF1E1E1E));
            g.drawLine (lx + 1, pY + 8, lx + 1, pY + pH - 8, 0.5f);
        }
    }

    //==========================================================================
    // METER LABELS — color-coded, drawn directly below each VU bar
    //==========================================================================
    {
        const int mx   = W - 104;
        const int mw   = 18;
        const int gap  = 26;
        const int mH   = (int)((H - 76) * 0.56f);
        const int mTop = 70;
        const float lblY = (float)(mTop + mH + 2);

        auto drawMeterLabel = [&](float x, float w, const juce::String& txt, juce::Colour col)
        {
            g.setFont (juce::FontOptions(7.5f).withStyle("Bold"));
            g.setColour (col.withAlpha(0.55f));
            g.drawText (txt, (int)x, (int)lblY, (int)w, 12, juce::Justification::centred);
        };

        drawMeterLabel ((float)mx,          (float)mw, "IN",  juce::Colour(0xFF44BB44));
        drawMeterLabel ((float)(mx + gap),  (float)mw, "HRM", juce::Colour(0xFFFF4422));
        drawMeterLabel ((float)(mx + gap*2),(float)mw, "OUT", juce::Colour(0xFFFFAA22));
    }

    //==========================================================================
    // SPECTRUM LABEL — subtle
    //==========================================================================
    {
        const int mH   = (int)((H - 76) * 0.56f);
        const float specLblY = (float)(70 + mH + 16);
        g.setFont (juce::FontOptions(6.5f).withStyle("Bold"));
        g.setColour (juce::Colour(0xFF555555).withAlpha(0.50f));
        g.drawText ("SPECTRUM", (int)(W - 110), (int)specLblY, 96, 10, juce::Justification::centred);
    }

    //==========================================================================
    // BOTTOM STATUS BAR
    //==========================================================================
    {
        // Subtle divider — graphite
        g.setColour (juce::Colour(0xFF262626));
        g.drawLine (12, (float)(H - 16), (float)(W - 12), (float)(H - 16), 0.5f);

        g.setFont (juce::FontOptions(7.5f));
        g.setColour (accentCol.withAlpha(0.22f));
        g.drawText ("MAXX BASS  v3.0", 16, H - 15, 120, 13, juce::Justification::centredLeft);
        g.setColour (accentCol.withAlpha(0.14f));
        g.drawText ("PSYCHOACOUSTIC BASS ENHANCER", W - 260, H - 15, 244, 13,
                    juce::Justification::centredRight);
    }

    //==========================================================================
    // CORNER BRACKET ACCENTS (uses accent color — unchanged)
    //==========================================================================
    {
        g.setColour (accentCol.withAlpha(0.28f));
        // Top-left
        g.fillRect (10.0f, 4.0f, 28.0f, 1.2f);
        g.fillRect (10.0f, 4.0f, 1.2f, 10.0f);
        // Top-right
        g.fillRect ((float)(W - 38), 4.0f, 28.0f, 1.2f);
        g.fillRect ((float)(W - 12), 4.0f, 1.2f, 10.0f);
        // Bottom-left
        g.fillRect (10.0f, (float)(H - 5), 28.0f, 1.2f);
        g.fillRect (10.0f, (float)(H - 14), 1.2f, 10.0f);
        // Bottom-right
        g.fillRect ((float)(W - 38), (float)(H - 5), 28.0f, 1.2f);
        g.fillRect ((float)(W - 12), (float)(H - 14), 1.2f, 10.0f);
    }
}

//==============================================================================
void MaxxBassAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    const int panelX = 10;
    const int panelW = W - 20 - 114;
    const int kTop   = 70;
    const float kw   = (float)panelW / 6.0f;

    auto placeKnob = [&](juce::Slider& k, juce::Label& l, int idx)
    {
        const int x = panelX + (int)(kw * (float)idx);
        const int w = (int)kw;
        l.setBounds (x + 2, kTop + 4, w - 4, 14);
        k.setBounds (x + 4, kTop + 18, w - 8, H - kTop - 36);
    };

    placeKnob (cutFreqKnob,    cutLbl,  0);
    placeKnob (harmMixKnob,    mixLbl,  1);
    placeKnob (driveKnob,      driveLbl,2);
    placeKnob (harmCharKnob,   charLbl, 3);
    placeKnob (dynRespKnob,    dynLbl,  4);
    placeKnob (outputGainKnob, outLbl,  5);

    // Buttons in header
    subProtectBtn.setBounds (W - 370, 17, 108, 28);
    bypassBtn    .setBounds (W - 256, 17, 100, 28);
    modeBtn      .setBounds (W - 150, 17, 110, 28);

    // Settings button in status bar
    settingsBtn.setBounds (W - 256, H - 20, 100, 18);

    // VU Meters — start at y=70, no container, sit on raw background
    const int mx   = W - 104;
    const int mw   = 18;
    const int mTop = 70;
    const int mH   = (int)((H - 76) * 0.56f);

    inMeter  .setBounds (mx,          mTop, mw, mH);
    harmMeter.setBounds (mx + 26,     mTop, mw, mH);
    outMeter .setBounds (mx + 52,     mTop, mw, mH);

    // Spectrum display — right below meter labels
    const int specY = mTop + mH + 14;
    spectrumDisplay.setBounds (W - 110, specY, 96, H - specY - 20);
}
