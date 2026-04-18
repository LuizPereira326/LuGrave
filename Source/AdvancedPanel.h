#pragma once
#include <JuceHeader.h>
#include "AdvancedParams.h"

//==============================================================================
// ParamDef - shared parameter definition struct
// Used by both TabContent and PanelContent
//==============================================================================
struct AdvParamDef
{
    const char* id;
    const char* label;
    float minVal, maxVal, step;
    const char* unit;
    const char* tooltip;
};

//==============================================================================
// AdvancedPanelWindow - v49 CORRIGIDO
//
// CORREÇÃO v49: Simplificação da sincronização.
// - Todos os parâmetros avançados estão no APVTS
// - Removido double-buffer, SpinLock e paramsDirty (não mais necessários)
// - Removido AdvancedParamsSharedState (era no-op)
// - UI atualiza APVTS diretamente, que sincroniza com audioParams
//==============================================================================
class AdvancedPanelWindow : public juce::DocumentWindow
{
public:
    // CORREÇÃO v49: Construtor simplificado - sem dirty flag, sem SpinLock
    AdvancedPanelWindow (AdvancedParams& params, juce::AudioProcessorValueTreeState* state, bool isSimpleMode, juce::Component& parentToCentreOn)
        : DocumentWindow ("MAXX BASS - Advanced Parameters",
                          juce::Colour (0xFF1A1A1A),
                          DocumentWindow::allButtons),
          panelContent (params, state, isSimpleMode)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&panelContent, true);

        const int w = 720;
        const int h = 580;
        centreAroundComponent (&parentToCentreOn, w, h);
        setResizable (true, true);
        setResizeLimits (600, 400, 1200, 900);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }

private:
    //======================================================================
    // TabContent - wrapper managing Viewport + ParamGrid per tab
    //======================================================================
    class TabContent : public juce::Component
    {
    public:
        // CORREÇÃO v49: Construtor simplificado
        TabContent (AdvancedParams& p, juce::AudioProcessorValueTreeState* state, const AdvParamDef* d, int n)
            : grid (p, state, d, n)
        {
            viewport.setScrollBarsShown (true, true);
            viewport.setViewedComponent (&grid, false);
            addAndMakeVisible (viewport);
        }

        void resized() override
        {
            viewport.setBounds (getLocalBounds());
        }

    private:
        //======================================================================
        // ParamGrid - Grid of sliders with labels per tab
        //======================================================================
        class ParamGrid : public juce::Component,
                          public juce::AudioProcessorValueTreeState::Listener
        {
        public:
            // CORREÇÃO v49: Construtor simplificado - atualiza APVTS diretamente
            ParamGrid (AdvancedParams& p, juce::AudioProcessorValueTreeState* state, const AdvParamDef* d, int n)
                : params (p), apvtsState (state), defs (d, d + n), numDefs (n)
            {
                sliders  .ensureStorageAllocated (n);
                labels   .ensureStorageAllocated (n);
                valLabels.ensureStorageAllocated (n);

                for (int i = 0; i < n; ++i)
                {
                    auto& sl = *sliders  .add (new juce::Slider());
                    auto& lb = *labels   .add (new juce::Label());
                    auto& vl = *valLabels.add (new juce::Label());

                    sl.setRange (defs[i].minVal, defs[i].maxVal, defs[i].step);
                    sl.setSliderStyle (juce::Slider::LinearHorizontal);
                    sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
                    sl.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xFF222222));
                    sl.setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xFFCC8866));
                    sl.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0xFF444444));
                    sl.setColour (juce::Slider::trackColourId,              juce::Colour (0xFFCC2200));
                    sl.setColour (juce::Slider::thumbColourId,             juce::Colour (0xFFFF4422));
                    sl.setTooltip (defs[i].tooltip);

                    float currentVal = getParamValue (defs[i].id);
                    sl.setValue (currentVal, juce::dontSendNotification);

                    sl.onValueChange = [this, i]()
                    {
                        float newVal = static_cast<float> (sliders[i]->getValue());
                        setParamValue (defs[i].id, newVal);
                        valLabels[i]->setText (formatValue (newVal, defs[i]),
                                               juce::dontSendNotification);
                    };

                    lb.setText (defs[i].label, juce::dontSendNotification);
                    lb.setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
                    lb.setFont (juce::FontOptions (11.0f));
                    lb.setMinimumHorizontalScale (1.0f);

                    vl.setText (formatValue (currentVal, defs[i]), juce::dontSendNotification);
                    vl.setColour (juce::Label::textColourId, juce::Colour (0xFF888888));
                    vl.setFont (juce::FontOptions (10.0f));
                    vl.setJustificationType (juce::Justification::centredRight);

                    addAndMakeVisible (sl);
                    addAndMakeVisible (lb);
                    addAndMakeVisible (vl);

                    // CORREÇÃO v50: registrar listener para detectar mudanças externas
                    // (automação do host, carregamento de preset)
                    if (apvtsState != nullptr)
                        apvtsState->addParameterListener (defs[i].id, this);
                }

                setSize (680, juce::jmax (400, numDefs * 38 + 20));
            }

            ~ParamGrid() override
            {
                // Remover todos os listeners registrados
                if (apvtsState != nullptr)
                    for (int i = 0; i < numDefs; ++i)
                        apvtsState->removeParameterListener (defs[i].id, this);
            }

            // CORREÇÃO v51: chamado pelo APVTS quando um parâmetro muda externamente.
            // ATENÇÃO: este callback NÃO é garantido na message thread — pode vir do
            // audio thread, da thread do host (automação), ou de qualquer outra.
            // Tocar em componentes JUCE diretamente aqui é race condition garantida.
            // Solução: capturar os dados por valor e despachar para a message thread
            // via callAsync. SafePointer evita use-after-free se o componente for
            // destruído antes do lambda executar.
            void parameterChanged (const juce::String& paramId, float newValue) override
            {
                juce::Component::SafePointer<ParamGrid> safeThis (this);
                juce::MessageManager::callAsync ([safeThis, paramId, newValue]()
                {
                    if (auto* self = safeThis.getComponent())
                    {
                        for (int i = 0; i < self->numDefs; ++i)
                        {
                            if (paramId == self->defs[i].id)
                            {
                                const float clamped = juce::jlimit (self->defs[i].minVal,
                                                                    self->defs[i].maxVal,
                                                                    newValue);
                                self->sliders   [i]->setValue (clamped, juce::dontSendNotification);
                                self->valLabels [i]->setText  (self->formatValue (clamped, self->defs[i]),
                                                               juce::dontSendNotification);
                                break;
                            }
                        }
                    }
                });
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xFF161616));
            }

            void resized() override
            {
                const int labelW = 170;
                const int sliderW = getWidth() - labelW - 120;
                const int valW   = 50;
                const int rowH   = 36;
                const int padY   = 8;

                for (int i = 0; i < numDefs; ++i)
                {
                    int y = padY + i * rowH;
                    labels   [i]->setBounds (4, y, labelW, 20);
                    sliders  [i]->setBounds (labelW + 4, y, sliderW, 20);
                    valLabels[i]->setBounds (labelW + sliderW + 8, y, valW, 20);
                }
            }

        private:
            AdvancedParams& params;
            juce::AudioProcessorValueTreeState* apvtsState = nullptr;
            std::vector<AdvParamDef> defs;
            int numDefs;
            juce::OwnedArray<juce::Slider> sliders;
            juce::OwnedArray<juce::Label>  labels;
            juce::OwnedArray<juce::Label>  valLabels;

            float getParamValue (const char* id) const
            {
                #define GET_PARAM(name) \
                    if (std::strcmp (id, #name) == 0) return params.name;

                // CORREÇÃO v49: Parâmetros BASE/RANGE do SimpleEngine
                GET_PARAM(SE_H2_BASE)       GET_PARAM(SE_H2_RANGE)
                GET_PARAM(SE_H3_BASE)       GET_PARAM(SE_H3_RANGE)
                GET_PARAM(SE_H4_BASE)       GET_PARAM(SE_H4_RANGE)
                GET_PARAM(SE_H5_BASE)       GET_PARAM(SE_H5_RANGE)
                GET_PARAM(SE_LOUDNESS_COMP) GET_PARAM(SE_MIX_MULTIPLIER) GET_PARAM(SE_HARM_LPF_FREQ)
                GET_PARAM(SE_HPF_MULT_1)    GET_PARAM(SE_HPF_MULT_2)     GET_PARAM(SE_LPF_MULT)
                GET_PARAM(SE_LPF_MIN)       GET_PARAM(SE_LPF_MAX)        GET_PARAM(SE_DC_BLOCK_FREQ)
                GET_PARAM(SE_HARM_DC_BLOCK_FREQ) GET_PARAM(SE_RAMP_SEC)  GET_PARAM(SE_ADAPTIVE_THRESH_INIT)
                GET_PARAM(SE_VETO_STRENGTH_INIT) GET_PARAM(SE_CONF_HIGH)

                GET_PARAM(CH_T2_SCALE)        GET_PARAM(CH_T3_CLAMP)       GET_PARAM(CH_T4_CLAMP)
                GET_PARAM(CH_T5_SCALE)        GET_PARAM(CH_T5_CLAMP)       GET_PARAM(CH_DRIVE_LOG_SCALE)
                GET_PARAM(CH_PRE_CLAMP)       GET_PARAM(CH_OUTPUT_CLAMP_4H) GET_PARAM(CH_OUTPUT_CLAMP_5H)

                GET_PARAM(HE_H2_BASE)    GET_PARAM(HE_H2_RANGE)   GET_PARAM(HE_H3_BASE)   GET_PARAM(HE_H3_RANGE)
                GET_PARAM(HE_H4_BASE)    GET_PARAM(HE_H4_RANGE)   GET_PARAM(HE_H5_BASE)   GET_PARAM(HE_H5_RANGE)
                GET_PARAM(HE_PERCEPTUAL_BOOST) GET_PARAM(HE_LOUDNESS_COMP) GET_PARAM(HE_HARM_LPF_FREQ)
                GET_PARAM(HE_HPF_MULT)   GET_PARAM(HE_LPF_MULT)   GET_PARAM(HE_LPF_MIN)   GET_PARAM(HE_LPF_MAX)
                GET_PARAM(HE_DC_BLOCK_FREQ) GET_PARAM(HE_DYN_RAMP_SEC) GET_PARAM(HE_SMOOTH_RAMP_SEC)

                GET_PARAM(SBS_SUB_MAX_ATTEN)   GET_PARAM(SBS_BODY_MAX_ATTEN)  GET_PARAM(SBS_SUB_MIN_RETAIN)
                GET_PARAM(SBS_BODY_MIN_RETAIN) GET_PARAM(SBS_SPLIT_FREQ)      GET_PARAM(BL_MAX_LEAN)
                GET_PARAM(BL_FAST_ATT_MS)      GET_PARAM(BL_FAST_REL_MS)      GET_PARAM(BL_SLOW_ATT_MS)
                GET_PARAM(BL_SLOW_REL_MS)      GET_PARAM(BL_LEAN_SMOOTH_MS)   GET_PARAM(BL_CREST_THRESH)
                GET_PARAM(BL_CREST_SCALE)      GET_PARAM(BL_FULLNESS_SCALE)

                GET_PARAM(DHG_BASE_THRESH_LOW)  GET_PARAM(DHG_BASE_THRESH_HIGH) GET_PARAM(DHG_BASE_BOOST)
                GET_PARAM(DHG_BASE_CUT)        GET_PARAM(DHG_MIN_BOOST)        GET_PARAM(DHG_MIN_CUT)
                GET_PARAM(DHG_CENTER)          GET_PARAM(DHG_HALF_WIDTH_MIN)   GET_PARAM(DHG_HALF_WIDTH_RANGE)
                GET_PARAM(DHG_ATT_MS)          GET_PARAM(DHG_REL_MS)

                GET_PARAM(SN_TARGET_LEVEL)   GET_PARAM(SN_MAX_BOOST)      GET_PARAM(SN_MAX_REDUCE)
                GET_PARAM(SN_BOOST_RATIO)    GET_PARAM(SN_COMP_SLOPE)     GET_PARAM(SN_ATT_MS)
                GET_PARAM(SN_REL_MS)         GET_PARAM(SN_GAIN_SMOOTH_MS) GET_PARAM(SN_LPF_FREQ)

                GET_PARAM(ADG_SAFE_LIMIT)          GET_PARAM(ADG_HARD_LIMIT)
                GET_PARAM(ADG_SATURATION_START)    GET_PARAM(ADG_SAT_OVER_NORM_FACTOR)
                GET_PARAM(ADG_LIMIT_FACTOR)

                GET_PARAM(DVP_MIN_HARM_GAIN)          GET_PARAM(DVP_MAX_NOTCH_DEPTH)
                GET_PARAM(DVP_PHASE_SHIFT_MAX)        GET_PARAM(DVP_FREE_BAND_BOOST)
                GET_PARAM(DVP_VOCAL_PHASE_THRESH)     GET_PARAM(DVP_NOTCH_VOCAL_THRESH)
                GET_PARAM(DVP_BANDED_THRESH)          GET_PARAM(DVP_CONFLICT_REDUCTION_MAX)
                GET_PARAM(DVP_FREE_BOOST_MAX)         GET_PARAM(DVP_PHASE_FADE)
                GET_PARAM(DVP_KICK_DUCK_AMOUNT)       GET_PARAM(DVP_PUNCH_REDUCTION_MAX)
                GET_PARAM(DVP_PHASE_MIX_FACTOR)

                GET_PARAM(SKD_TRANSIENT_THRESH) GET_PARAM(SKD_DUCK_AMOUNT)   GET_PARAM(SKD_DUCK_DURATION)
                GET_PARAM(SKD_DUCK_COOLDOWN)   GET_PARAM(SKD_BAND_CENTER)   GET_PARAM(SKD_BAND_Q)
                GET_PARAM(SKD_ATTACK_HPF_FREQ) GET_PARAM(SKD_ATTACK_MS)     GET_PARAM(SKD_RELEASE_MS)

                GET_PARAM(SP_HPF_BASE_CUT)    GET_PARAM(SP_SUB_LPF_FREQ)   GET_PARAM(SP_BODY_LPF_FREQ)
                GET_PARAM(SP_SUB_ENV_ATT_MS)  GET_PARAM(SP_SUB_ENV_REL_MS) GET_PARAM(SP_BODY_ENV_ATT_MS)
                GET_PARAM(SP_BODY_ENV_REL_MS) GET_PARAM(SP_TARGET_CUT_MIN) GET_PARAM(SP_TARGET_CUT_MAX)
                GET_PARAM(SP_DECIMATE_FACTOR)

                #undef GET_PARAM
                return 0.0f;
            }

            void setParamValue (const char* id, float value)
            {
                auto pushToHost = [this] (const char* paramId, float newValue)
                {
                    if (apvtsState == nullptr)
                        return;

                    if (auto* parameter = apvtsState->getParameter (paramId))
                    {
                        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                            ranged->setValueNotifyingHost (ranged->convertTo0to1 (newValue));
                    }
                };

                // CORREÇÃO v49: Simplificado - apenas atualiza params local e notifica APVTS
                #define SET_PARAM(name) \
                    if (std::strcmp (id, #name) == 0) { params.name = value; pushToHost (#name, value); return; }

                // CORREÇÃO v49: Parâmetros BASE/RANGE do SimpleEngine
                SET_PARAM(SE_H2_BASE)       SET_PARAM(SE_H2_RANGE)
                SET_PARAM(SE_H3_BASE)       SET_PARAM(SE_H3_RANGE)
                SET_PARAM(SE_H4_BASE)       SET_PARAM(SE_H4_RANGE)
                SET_PARAM(SE_H5_BASE)       SET_PARAM(SE_H5_RANGE)
                SET_PARAM(SE_LOUDNESS_COMP) SET_PARAM(SE_MIX_MULTIPLIER) SET_PARAM(SE_HARM_LPF_FREQ)
                SET_PARAM(SE_HPF_MULT_1)    SET_PARAM(SE_HPF_MULT_2)     SET_PARAM(SE_LPF_MULT)
                SET_PARAM(SE_LPF_MIN)       SET_PARAM(SE_LPF_MAX)        SET_PARAM(SE_DC_BLOCK_FREQ)
                SET_PARAM(SE_HARM_DC_BLOCK_FREQ) SET_PARAM(SE_RAMP_SEC)  SET_PARAM(SE_ADAPTIVE_THRESH_INIT)
                SET_PARAM(SE_VETO_STRENGTH_INIT) SET_PARAM(SE_CONF_HIGH)

                SET_PARAM(CH_T2_SCALE)        SET_PARAM(CH_T3_CLAMP)       SET_PARAM(CH_T4_CLAMP)
                SET_PARAM(CH_T5_SCALE)        SET_PARAM(CH_T5_CLAMP)       SET_PARAM(CH_DRIVE_LOG_SCALE)
                SET_PARAM(CH_PRE_CLAMP)       SET_PARAM(CH_OUTPUT_CLAMP_4H) SET_PARAM(CH_OUTPUT_CLAMP_5H)

                SET_PARAM(HE_H2_BASE)    SET_PARAM(HE_H2_RANGE)   SET_PARAM(HE_H3_BASE)   SET_PARAM(HE_H3_RANGE)
                SET_PARAM(HE_H4_BASE)    SET_PARAM(HE_H4_RANGE)   SET_PARAM(HE_H5_BASE)   SET_PARAM(HE_H5_RANGE)
                SET_PARAM(HE_PERCEPTUAL_BOOST) SET_PARAM(HE_LOUDNESS_COMP) SET_PARAM(HE_HARM_LPF_FREQ)
                SET_PARAM(HE_HPF_MULT)   SET_PARAM(HE_LPF_MULT)   SET_PARAM(HE_LPF_MIN)   SET_PARAM(HE_LPF_MAX)
                SET_PARAM(HE_DC_BLOCK_FREQ) SET_PARAM(HE_DYN_RAMP_SEC) SET_PARAM(HE_SMOOTH_RAMP_SEC)

                SET_PARAM(SBS_SUB_MAX_ATTEN)   SET_PARAM(SBS_BODY_MAX_ATTEN)  SET_PARAM(SBS_SUB_MIN_RETAIN)
                SET_PARAM(SBS_BODY_MIN_RETAIN) SET_PARAM(SBS_SPLIT_FREQ)      SET_PARAM(BL_MAX_LEAN)
                SET_PARAM(BL_FAST_ATT_MS)      SET_PARAM(BL_FAST_REL_MS)      SET_PARAM(BL_SLOW_ATT_MS)
                SET_PARAM(BL_SLOW_REL_MS)      SET_PARAM(BL_LEAN_SMOOTH_MS)   SET_PARAM(BL_CREST_THRESH)
                SET_PARAM(BL_CREST_SCALE)      SET_PARAM(BL_FULLNESS_SCALE)

                SET_PARAM(DHG_BASE_THRESH_LOW)  SET_PARAM(DHG_BASE_THRESH_HIGH) SET_PARAM(DHG_BASE_BOOST)
                SET_PARAM(DHG_BASE_CUT)        SET_PARAM(DHG_MIN_BOOST)        SET_PARAM(DHG_MIN_CUT)
                SET_PARAM(DHG_CENTER)          SET_PARAM(DHG_HALF_WIDTH_MIN)   SET_PARAM(DHG_HALF_WIDTH_RANGE)
                SET_PARAM(DHG_ATT_MS)          SET_PARAM(DHG_REL_MS)

                SET_PARAM(SN_TARGET_LEVEL)   SET_PARAM(SN_MAX_BOOST)      SET_PARAM(SN_MAX_REDUCE)
                SET_PARAM(SN_BOOST_RATIO)    SET_PARAM(SN_COMP_SLOPE)     SET_PARAM(SN_ATT_MS)
                SET_PARAM(SN_REL_MS)         SET_PARAM(SN_GAIN_SMOOTH_MS) SET_PARAM(SN_LPF_FREQ)

                SET_PARAM(ADG_SAFE_LIMIT)          SET_PARAM(ADG_HARD_LIMIT)
                SET_PARAM(ADG_SATURATION_START)    SET_PARAM(ADG_SAT_OVER_NORM_FACTOR)
                SET_PARAM(ADG_LIMIT_FACTOR)

                SET_PARAM(DVP_MIN_HARM_GAIN)          SET_PARAM(DVP_MAX_NOTCH_DEPTH)
                SET_PARAM(DVP_PHASE_SHIFT_MAX)        SET_PARAM(DVP_FREE_BAND_BOOST)
                SET_PARAM(DVP_VOCAL_PHASE_THRESH)     SET_PARAM(DVP_NOTCH_VOCAL_THRESH)
                SET_PARAM(DVP_BANDED_THRESH)          SET_PARAM(DVP_CONFLICT_REDUCTION_MAX)
                SET_PARAM(DVP_FREE_BOOST_MAX)         SET_PARAM(DVP_PHASE_FADE)
                SET_PARAM(DVP_KICK_DUCK_AMOUNT)       SET_PARAM(DVP_PUNCH_REDUCTION_MAX)
                SET_PARAM(DVP_PHASE_MIX_FACTOR)

                SET_PARAM(SKD_TRANSIENT_THRESH) SET_PARAM(SKD_DUCK_AMOUNT)   SET_PARAM(SKD_DUCK_DURATION)
                SET_PARAM(SKD_DUCK_COOLDOWN)   SET_PARAM(SKD_BAND_CENTER)   SET_PARAM(SKD_BAND_Q)
                SET_PARAM(SKD_ATTACK_HPF_FREQ) SET_PARAM(SKD_ATTACK_MS)     SET_PARAM(SKD_RELEASE_MS)

                SET_PARAM(SP_HPF_BASE_CUT)    SET_PARAM(SP_SUB_LPF_FREQ)   SET_PARAM(SP_BODY_LPF_FREQ)
                SET_PARAM(SP_SUB_ENV_ATT_MS)  SET_PARAM(SP_SUB_ENV_REL_MS) SET_PARAM(SP_BODY_ENV_ATT_MS)
                SET_PARAM(SP_BODY_ENV_REL_MS) SET_PARAM(SP_TARGET_CUT_MIN) SET_PARAM(SP_TARGET_CUT_MAX)
                SET_PARAM(SP_DECIMATE_FACTOR)

                #undef SET_PARAM
            }

            static juce::String formatValue (float val, const AdvParamDef& def)
            {
                juce::String s;
                if (def.step >= 1.0f)
                    s = juce::String (static_cast<int> (val));
                else if (def.step >= 0.1f)
                    s = juce::String (val, 1);
                else if (def.step >= 0.01f)
                    s = juce::String (val, 2);
                else
                    s = juce::String (val, 3);

                if (def.unit[0] != '\0')
                    s << " " << def.unit;
                return s;
            }

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamGrid)
        };

        // Members
        ParamGrid grid;
        juce::Viewport viewport;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabContent)
    };

    //======================================================================
    // PanelContent - main container with TabbedComponent + Toolbar
    //======================================================================
    class PanelContent : public juce::Component,
                         public juce::ToolbarItemFactory
    {
    public:
        // NOVIDADE v50: Construtor com toolbar de presets e undo/redo
        PanelContent (AdvancedParams& p, juce::AudioProcessorValueTreeState* state, bool isSimpleMode)
            : params (p), apvtsState (state)
        {
            // Configurar toolbar
            setupToolbar();
            
            infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFFFB080));
            infoLabel.setFont (juce::FontOptions (12.0f));
            infoLabel.setJustificationType (juce::Justification::centredLeft);
            infoLabel.setInterceptsMouseClicks (false, false);
            infoLabel.setText (isSimpleMode
                                ? "Modo SIMPLE ativo. Use os controles BASE/RANGE para ajustar os pesos harmonicos via harmChar."
                                : "Modo HYBRID ativo. Controles BASE/RANGE afetam o motor Hibrido.",
                               juce::dontSendNotification);
            addAndMakeVisible (infoLabel);
            addAndMakeVisible (toolbar);
            addAndMakeVisible (tabbedComponent);
            buildTabs();
            tabbedComponent.setCurrentTabIndex (isSimpleMode ? 0 : 2);
            setSize (700, 600); // Aumentado para acomodar toolbar
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xFF181818));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (6);
            infoLabel.setBounds (area.removeFromTop (26));
            toolbar.setBounds (area.removeFromTop (40));
            tabbedComponent.setBounds (area);
        }

    private:
        AdvancedParams& params;
        juce::AudioProcessorValueTreeState* apvtsState = nullptr;
        juce::Label infoLabel;
        juce::Toolbar toolbar;
        juce::TabbedComponent tabbedComponent { juce::TabbedButtonBar::Orientation::TabsAtTop };
        
        //======================================================================
        void setupToolbar()
        {
            toolbar.setStyle (juce::Toolbar::ToolbarStyle::iconsOnly);
            toolbar.addDefaultItems (*this);
        }
        
        //======================================================================
        // ToolbarItemFactory implementation
        //======================================================================
        juce::ToolbarItemComponent* createItem (int itemId) override
        {
            juce::String name, tooltip;
            juce::Colour colour;
            
            switch (itemId)
            {
                case 1: name = "Undo";       tooltip = "Desfazer (Ctrl+Z)"; colour = juce::Colours::orange; break;
                case 2: name = "Redo";       tooltip = "Refazer (Ctrl+Y)"; colour = juce::Colours::lightgreen; break;
                case 3: name = "Save";       tooltip = "Salvar preset";     colour = juce::Colours::skyblue; break;
                case 4: name = "Load";       tooltip = "Carregar preset";   colour = juce::Colours::cyan; break;
                case 5: name = "Reset";      tooltip = "Resetar padrao";    colour = juce::Colours::red; break;
                default: return nullptr;
            }
            
            auto* btn = new juce::ToolbarButton (name, tooltip, createIcon (itemId, colour), *this);
            btn->setTooltip (tooltip);
            return btn;
        }
        
        // Criar ícone simples desenhado programaticamente
        std::unique_ptr<juce::Drawable> createIcon (int itemId, juce::Colour col)
        {
            auto shape = std::make_unique<juce::DrawableRectangle>();
            shape->setFill (col);
            
            juce::Rectangle<float> bounds (0, 0, 24, 24);
            
            // Desenhar formas diferentes para cada botão
            switch (itemId)
            {
                case 1: // Undo - seta esquerda
                {
                    auto path = juce::Path();
                    path.startNewSubPath (16, 4);
                    path.lineTo (8, 12);
                    path.lineTo (16, 20);
                    path.lineTo (14, 22);
                    path.lineTo (4, 12);
                    path.lineTo (14, 2);
                    path.closeSubPath();
                    
                    auto* drawablePath = new juce::DrawablePath();
                    drawablePath->setPath (path);
                    drawablePath->setFill (col);
                    return std::unique_ptr<juce::Drawable> (drawablePath);
                }
                case 2: // Redo - seta direita
                {
                    auto path = juce::Path();
                    path.startNewSubPath (8, 4);
                    path.lineTo (16, 12);
                    path.lineTo (8, 20);
                    path.lineTo (10, 22);
                    path.lineTo (20, 12);
                    path.lineTo (10, 2);
                    path.closeSubPath();
                    
                    auto* drawablePath = new juce::DrawablePath();
                    drawablePath->setPath (path);
                    drawablePath->setFill (col);
                    return std::unique_ptr<juce::Drawable> (drawablePath);
                }
                case 3: // Save - disquete
                {
                    auto path = juce::Path();
                    path.addRectangle (4, 2, 16, 20);
                    path.addRectangle (8, 14, 8, 6);
                    path.addRectangle (6, 4, 8, 6);
                    
                    auto* drawablePath = new juce::DrawablePath();
                    drawablePath->setPath (path);
                    drawablePath->setFill (col);
                    return std::unique_ptr<juce::Drawable> (drawablePath);
                }
                case 4: // Load - pasta
                {
                    auto path = juce::Path();
                    path.startNewSubPath (2, 6);
                    path.lineTo (2, 18);
                    path.lineTo (22, 18);
                    path.lineTo (22, 6);
                    path.lineTo (12, 6);
                    path.lineTo (10, 4);
                    path.lineTo (2, 4);
                    path.closeSubPath();
                    
                    auto* drawablePath = new juce::DrawablePath();
                    drawablePath->setPath (path);
                    drawablePath->setFill (col);
                    return std::unique_ptr<juce::Drawable> (drawablePath);
                }
                case 5: // Reset - lixa/círculo com X
                {
                    auto path = juce::Path();
                    path.addEllipse (4, 4, 16, 16);
                    
                    auto* drawablePath = new juce::DrawablePath();
                    drawablePath->setPath (path);
                    drawablePath->setFill (col);
                    return std::unique_ptr<juce::Drawable> (drawablePath);
                }
                default:
                    return nullptr;
            }
        }
        
        void getAllToolbarItemIds (juce::Array<int>& ids) override
        {
            ids.addArray ({1, 2, 3, 4, 5}); // Undo, Redo, Save, Load, Reset
        }
        
        void toolbarItemClicked (int itemId) override
        {
            if (apvtsState == nullptr)
                return;
                
            auto* processor = dynamic_cast<MaxxBassAudioProcessor*> (apvtsState->processor);
            if (processor == nullptr)
                return;
            
            switch (itemId)
            {
                case 1: // Undo
                    if (processor->canUndo())
                        processor->performUndo();
                    break;
                case 2: // Redo
                    if (processor->canRedo())
                        processor->performRedo();
                    break;
                case 3: // Save Preset
                {
                    juce::FileChooser chooser ("Salvar Preset",
                                               juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                               "*.maxxbasspreset", true);
                    if (chooser.browseForFileToSave (true))
                        processor->savePresetToFile (chooser.getResult());
                    break;
                }
                case 4: // Load Preset
                {
                    juce::FileChooser chooser ("Carregar Preset",
                                               juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                               "*.maxxbasspreset", true);
                    if (chooser.browseForFileToOpen (true))
                        processor->loadPresetFromFile (chooser.getResult());
                    break;
                }
                case 5: // Reset
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                            "Resetar Parametros",
                                                            "Tem certeza que deseja resetar TODOS os parametros para os valores padrao de fabrica?",
                                                            "Cancelar|Resetar",
                                                            nullptr,
                                                            [processor] (int result)
                                                            {
                                                                if (result == 2)
                                                                    processor->resetToFactoryDefaults();
                                                            });
                    break;
                }
            }
        }

        //======================================================================
        void buildTabs()
        {
            tabbedComponent.setOutline (0);

            // ---- Motor Simples ----
            // CORREÇÃO v49: Agora usa parâmetros BASE/RANGE (funcionais)
            {
                AdvParamDef defs[] = {
                    {"SE_H2_BASE",         "H2 Base",                  0.0f, 2.0f,  0.01f, "",     "Peso H2 quando harmChar=0%"},
                    {"SE_H2_RANGE",        "H2 Range",                 0.0f, 1.0f,  0.01f, "",     "Faixa H2 (harmChar=100% = BASE+RANGE)"},
                    {"SE_H3_BASE",         "H3 Base",                  0.0f, 2.0f,  0.01f, "",     "Peso H3 quando harmChar=0%"},
                    {"SE_H3_RANGE",        "H3 Range",                 0.0f, 1.0f,  0.01f, "",     "Faixa H3"},
                    {"SE_H4_BASE",         "H4 Base",                  0.0f, 2.0f,  0.01f, "",     "Peso H4 quando harmChar=0%"},
                    {"SE_H4_RANGE",        "H4 Range",                 0.0f, 1.0f,  0.01f, "",     "Faixa H4"},
                    {"SE_H5_BASE",         "H5 Base",                  0.0f, 2.0f,  0.01f, "",     "Peso H5 quando harmChar=0%"},
                    {"SE_H5_RANGE",        "H5 Range",                 0.0f, 1.0f,  0.01f, "",     "Faixa H5"},
                    {"SE_LOUDNESS_COMP",   "Comp. Loudness",           0.0f, 1.0f,  0.01f, "",     "Fator de compensacao de loudness no mix"},
                    {"SE_MIX_MULTIPLIER",  "Multiplicador de Mix",     0.1f, 5.0f,  0.01f, "x",    "Multiplicador do sinal harmonico no mix"},
                    {"SE_HARM_LPF_FREQ",   "LPF Harmonicos (fixo)",    100.f,1000.f, 1.0f,  "Hz",   "Freq do LPF de harmonicos no prepare()"},
                    {"SE_HPF_MULT_1",      "HPF Harm Mult 1",         1.0f, 5.0f,  0.1f,  "x",    "Multiplicador do HPF1: crossover * H1"},
                    {"SE_HPF_MULT_2",      "HPF Harm Mult 2",         1.0f, 5.0f,  0.1f,  "x",    "Multiplicador do HPF2: crossover * H2"},
                    {"SE_LPF_MULT",        "LPF Multiplicador",        1.0f,10.0f,  0.1f,  "x",    "LPF variavel: crossover * LMult"},
                    {"SE_LPF_MIN",         "LPF Minimo",               50.f, 500.f,  1.0f,  "Hz",   "Freq minima LPF variavel"},
                    {"SE_LPF_MAX",         "LPF Maximo",               100.f,1000.f, 1.0f,  "Hz",   "Freq maxima LPF variavel"},
                    {"SE_DC_BLOCK_FREQ",   "DC Block Freq (saida)",    1.0f, 50.0f, 0.5f,  "Hz",   "Freq do DC blocker na saida"},
                    {"SE_HARM_DC_BLOCK_FREQ","DC Block (harm.)",       1.0f,100.0f, 0.5f,  "Hz",   "DC blocker nos harmonicos"},
                    {"SE_RAMP_SEC",        "Tempo de Ramp",            0.001f,0.5f,  0.001f,"s",    "Ramp para suavizacao"},
                    {"SE_ADAPTIVE_THRESH_INIT","Thresh Adapt. Inicial", 0.0f, 1.0f,  0.01f, "",     "Threshold adaptativo inicial"},
                    {"SE_VETO_STRENGTH_INIT","Veto Strength Inicial",  0.0f, 1.0f,  0.01f, "",     "Veto strength inicial"},
                    {"SE_CONF_HIGH",       "Confianca Alta",           0.0f, 1.0f,  0.01f, "",     "Limiar de confianca adaptativa"},
                };
                addTab ("Motor Simples", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Chebyshev ----
            {
                AdvParamDef defs[] = {
                    {"CH_T2_SCALE",        "T2 Scale (2a harm.)",      0.1f,10.0f, 0.1f,  "",     "Escala do polinomio T2"},
                    {"CH_T3_CLAMP",        "T3 Clamp (3a harm.)",      0.1f, 5.0f,  0.1f,  "",     "Clamp do polinomio T3"},
                    {"CH_T4_CLAMP",        "T4 Clamp (4a harm.)",      0.1f, 5.0f,  0.1f,  "",     "Clamp do polinomio T4"},
                    {"CH_T5_SCALE",        "T5 Scale (5a harm.)",      0.0f, 2.0f,  0.01f, "",     "Escala do polinomio T5"},
                    {"CH_T5_CLAMP",        "T5 Clamp (5a harm.)",      0.1f, 5.0f,  0.1f,  "",     "Clamp do polinomio T5"},
                    {"CH_DRIVE_LOG_SCALE", "Drive Log Scale",         0.1f, 3.0f,  0.01f, "",     "Escala logaritmica do drive"},
                    {"CH_PRE_CLAMP",       "Pre-Clamp",                0.5f, 5.0f,  0.1f,  "",     "Clamp antes dos polinomios"},
                    {"CH_OUTPUT_CLAMP_4H", "Output Clamp (4 harm.)",   0.5f,10.0f,  0.1f,  "",     "Clamp com 4 harmonicos"},
                    {"CH_OUTPUT_CLAMP_5H", "Output Clamp (5 harm.)",   0.5f,10.0f,  0.1f,  "",     "Clamp com 5 harmonicos"},
                };
                addTab ("Chebyshev", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Motor Hibrido ----
            {
                AdvParamDef defs[] = {
                    {"HE_H2_BASE",    "H2 Base",                         0.0f, 2.0f, 0.01f, "",  "Peso base H2 (HarmChar=0%)"},
                    {"HE_H2_RANGE",   "H2 Range",                        0.0f, 1.0f, 0.01f, "",  "Faixa H2"},
                    {"HE_H3_BASE",    "H3 Base",                         0.0f, 2.0f, 0.01f, "",  "Peso base H3"},
                    {"HE_H3_RANGE",   "H3 Range",                        0.0f, 1.0f, 0.01f, "",  "Faixa H3"},
                    {"HE_H4_BASE",    "H4 Base",                         0.0f, 2.0f, 0.01f, "",  "Peso base H4"},
                    {"HE_H4_RANGE",   "H4 Range",                        0.0f, 1.0f, 0.01f, "",  "Faixa H4"},
                    {"HE_H5_BASE",    "H5 Base",                         0.0f, 2.0f, 0.01f, "",  "Peso base H5"},
                    {"HE_H5_RANGE",   "H5 Range",                        0.0f, 1.0f, 0.01f, "",  "Faixa H5"},
                    {"HE_PERCEPTUAL_BOOST","Boost Perceptual",           0.0f, 2.0f, 0.01f, "",  "Compensacao perceptual"},
                    {"HE_LOUDNESS_COMP","Comp. Loudness",                 0.0f, 1.0f, 0.01f, "",  "Fator de loudness"},
                    {"HE_HARM_LPF_FREQ","LPF Harmonicos (fixo)",         100.f,2000.f,1.0f,"Hz", "LPF harmonicos prepare()"},
                    {"HE_HPF_MULT",    "HPF Multiplicador",              0.5f, 5.0f, 0.05f,"x",  "HPF: crossover * HMult"},
                    {"HE_LPF_MULT",    "LPF Multiplicador",              1.0f,10.0f, 0.1f, "x",  "LPF variavel"},
                    {"HE_LPF_MIN",     "LPF Minimo",                      50.f,1000.f,1.0f,"Hz", "Freq minima LPF"},
                    {"HE_LPF_MAX",     "LPF Maximo",                     100.f,2000.f,1.0f,"Hz", "Freq maxima LPF"},
                    {"HE_DC_BLOCK_FREQ","DC Block Freq",                 1.0f, 50.0f,0.5f,"Hz", "DC blocker"},
                    {"HE_DYN_RAMP_SEC","Ramp Dinamica",                  0.001f,0.5f,0.001f,"s", "Ramp suavizacao dynResp"},
                    {"HE_SMOOTH_RAMP_SEC","Ramp Smooth",                  0.001f,0.5f,0.001f,"s", "Ramp geral"},
                };
                addTab ("Motor Hibrido", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Sub Bass / Bass Leaner ----
            {
                AdvParamDef defs[] = {
                    {"SBS_SUB_MAX_ATTEN",   "Sub Aten. Max.",           0.0f, 1.0f, 0.01f, "",  "Aten. maxima do sub"},
                    {"SBS_BODY_MAX_ATTEN",  "Body Aten. Max.",          0.0f, 1.0f, 0.01f, "",  "Aten. maxima do body"},
                    {"SBS_SUB_MIN_RETAIN",  "Sub Reten. Min.",          0.0f, 1.0f, 0.01f, "",  "Retencao minima do sub"},
                    {"SBS_BODY_MIN_RETAIN", "Body Reten. Min.",         0.0f, 1.0f, 0.01f, "",  "Retencao minima do body"},
                    {"SBS_SPLIT_FREQ",      "Freq Split",               20.f,150.f,  1.0f,  "Hz", "Separacao sub/body"},
                    {"BL_MAX_LEAN",         "Lean Maximo",              0.0f, 0.8f, 0.01f, "",  "Reducao maxima do body"},
                    {"BL_FAST_ATT_MS",      "Fast Attack",              0.5f, 50.f,  0.5f,  "ms","Attack envelope rapido"},
                    {"BL_FAST_REL_MS",      "Fast Release",             10.f,500.f,  5.0f,  "ms","Release rapido"},
                    {"BL_SLOW_ATT_MS",      "Slow Attack",              1.0f,200.f,  1.0f,  "ms","Attack lento"},
                    {"BL_SLOW_REL_MS",      "Slow Release",             50.f,2000.f,10.0f,  "ms","Release lento"},
                    {"BL_LEAN_SMOOTH_MS",   "Lean Smoothing",           1.0f,200.f,  1.0f,  "ms","Suavizacao do lean"},
                    {"BL_CREST_THRESH",     "Crest Threshold",          0.5f, 5.0f,  0.1f,  "",  "Threshold crest factor"},
                    {"BL_CREST_SCALE",      "Crest Scale",              0.1f, 2.0f,  0.05f, "",  "Escala crest factor"},
                    {"BL_FULLNESS_SCALE",   "Fullness Scale",           0.5f,10.0f,  0.1f,  "",  "Escala fullness"},
                };
                addTab ("Sub Bass / Leaner", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Ganho Dinamico ----
            {
                AdvParamDef defs[] = {
                    {"DHG_BASE_THRESH_LOW",  "Threshold Baixo Base", 0.01f,0.5f, 0.01f, "", "Limiar baixo (din=100%)"},
                    {"DHG_BASE_THRESH_HIGH", "Threshold Alto Base",  0.1f, 1.0f, 0.01f, "", "Limiar alto (din=100%)"},
                    {"DHG_BASE_BOOST",       "Boost Base",           1.0f, 3.0f, 0.01f, "", "Boost maximo (+80%)"},
                    {"DHG_BASE_CUT",         "Cut Base",             0.3f, 1.0f, 0.01f, "", "Corte maximo (-25%)"},
                    {"DHG_MIN_BOOST",        "Boost Minimo",          1.0f, 2.0f, 0.01f, "", "Boost minimo (+12%)"},
                    {"DHG_MIN_CUT",          "Cut Minimo",            0.5f, 1.0f, 0.01f, "", "Corte minimo (-4%)"},
                    {"DHG_CENTER",           "Centro Faixa",          0.05f,0.5f, 0.01f, "", "Centro da faixa envelope"},
                    {"DHG_HALF_WIDTH_MIN",   "Meia-Larg. Min.",      0.01f,0.2f, 0.01f, "", "Meia-largura minima"},
                    {"DHG_HALF_WIDTH_RANGE", "Meia-Larg. Faixa",    0.01f,0.5f, 0.01f, "", "Variacao meia-largura"},
                    {"DHG_ATT_MS",           "Attack",               1.0f,100.f,  0.5f,  "ms","Ataque ganho dinamico"},
                    {"DHG_REL_MS",           "Release",             10.f,1000.f, 5.0f,  "ms","Release ganho dinamico"},
                };
                addTab ("Ganho Dinamico", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Normalizador Sub ----
            {
                AdvParamDef defs[] = {
                    {"SN_TARGET_LEVEL",   "Nivel Alvo",       0.01f,0.5f, 0.01f, "",  "Nivel referencia do sub"},
                    {"SN_MAX_BOOST",      "Boost Maximo",      1.0f, 5.0f, 0.1f,  "",  "Ganho maximo de boost"},
                    {"SN_MAX_REDUCE",     "Reducao Max.",      0.1f, 1.0f,0.05f,"",  "Ganho minimo"},
                    {"SN_BOOST_RATIO",    "Razao de Boost",    0.1f, 2.0f, 0.05f, "",  "Correcao ideal aplicar"},
                    {"SN_COMP_SLOPE",     "Slope Compressao",  0.1f, 2.0f, 0.05f, "",  "Slope da compressao"},
                    {"SN_ATT_MS",         "Attack",            10.f,2000.f,10.0f,"ms", "Ataque do envelope"},
                    {"SN_REL_MS",         "Release",           100.f,5000.f,50.f,"ms", "Release do envelope"},
                    {"SN_GAIN_SMOOTH_MS", "Suav. Ganho",       5.0f,500.0f, 5.0f,"ms", "Suavizacao do ganho"},
                    {"SN_LPF_FREQ",       "LPF Freq",          20.f,200.0f, 1.0f,"Hz", "LPF para isolar sub"},
                };
                addTab ("Normalizador Sub", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Anti-Distorcao ----
            {
                AdvParamDef defs[] = {
                    {"ADG_SAFE_LIMIT",           "Limite Seguro",     1.0f,10.0f, 0.1f, "", "Inicio limitacao"},
                    {"ADG_HARD_LIMIT",           "Limite Absoluto",   1.0f,10.0f, 0.1f, "", "Clamp absoluto saida"},
                    {"ADG_SATURATION_START",     "Inicio Saturacao",  0.5f,5.0f,  0.1f,"",  "Saturacao suave"},
                    {"ADG_SAT_OVER_NORM_FACTOR", "Fator Sat/Norm",    0.0f, 1.0f, 0.01f,"", "Normalizacao saturacao"},
                    {"ADG_LIMIT_FACTOR",         "Fator Limit.",      0.0f, 1.0f, 0.01f,"", "Fracao que passa"},
                };
                addTab ("Anti-Distorcao", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Protecao Vocal / Kick ----
            {
                AdvParamDef defs[] = {
                    {"DVP_MIN_HARM_GAIN",          "Ganho Harm. Min. (R1)", 0.5f, 1.0f, 0.01f, "", "Minimo harmGain"},
                    {"DVP_MAX_NOTCH_DEPTH",        "Profund. Max. Notch (R2)", 0.0f, 1.0f, 0.01f, "", "Mix max. do notch"},
                    {"DVP_PHASE_SHIFT_MAX",        "Phase Shift Maximo",       0.0f, 1.0f, 0.01f, "", "Phase shift (0-1)"},
                    {"DVP_FREE_BAND_BOOST",        "Boost Bandas Livres",      1.0f, 2.0f, 0.01f, "", "Ganho maximo bandas livres"},
                    {"DVP_VOCAL_PHASE_THRESH",     "Threshold Phase Vocal",    0.0f, 1.0f, 0.01f, "", "vocalScore min. phase shift"},
                    {"DVP_NOTCH_VOCAL_THRESH",     "Threshold Notch Vocal",    0.0f, 1.0f, 0.01f, "", "vocalScore min. notch"},
                    {"DVP_BANDED_THRESH",          "Threshold Banded",        0.0f, 1.0f, 0.01f, "", "vocalScore min. banded"},
                    {"DVP_CONFLICT_REDUCTION_MAX", "Reducao Conflito Max.",    0.0f, 0.5f, 0.01f, "", "Reducao H2/H3 por conflito"},
                    {"DVP_FREE_BOOST_MAX",         "Boost Livre Maximo",       0.0f, 1.0f, 0.01f, "", "Boost H4/H5 bandas livres"},
                    {"DVP_PHASE_FADE",             "Phase Fade",              0.9f, 1.0f, 0.001f,"", "Fade-out do phase shift"},
                    {"DVP_KICK_DUCK_AMOUNT",       "Kick Duck Amount",        0.0f, 0.8f, 0.01f, "", "Reducao ao detectar kick"},
                    {"DVP_PUNCH_REDUCTION_MAX",    "Punch Reduction Max.",     0.0f, 0.5f, 0.01f, "", "Reducao banda punch"},
                    {"DVP_PHASE_MIX_FACTOR",       "Phase Mix Factor",        0.0f, 1.0f, 0.01f, "", "Mix processado vs banded"},
                    {"SKD_TRANSIENT_THRESH",       "Transient Threshold",     0.05f,1.0f, 0.01f, "", "Limiar transiente kick"},
                    {"SKD_DUCK_AMOUNT",            "Duck Amount",             0.0f, 0.8f, 0.01f, "", "Reducao nos harmonicos"},
                    {"SKD_DUCK_DURATION",          "Duck Duration",           10.f,1000.f,1.0f,  "samples", "Duracao ducking"},
                    {"SKD_DUCK_COOLDOWN",          "Duck Cooldown",           100.f,5000.f,10.f, "samples", "Intervalo min. ducks"},
                    {"SKD_BAND_CENTER",            "Banda Center",            30.f,200.f, 1.0f,  "Hz", "Freq central bandpass kick"},
                    {"SKD_BAND_Q",                 "Banda Q",                 0.5f,10.0f, 0.1f,  "",  "Q do bandpass kick"},
                    {"SKD_ATTACK_HPF_FREQ",        "Attack HPF Freq",         30.f,500.f, 1.0f,  "Hz", "HPF detectar attack"},
                    {"SKD_ATTACK_MS",              "Attack",                  0.1f,10.0f, 0.1f,  "ms", "Ataque do duck"},
                    {"SKD_RELEASE_MS",             "Release",                 1.0f,200.0f,1.0f,  "ms", "Release do duck"},
                };
                addTab ("Protecao Vocal / Kick", defs, sizeof(defs)/sizeof(defs[0]));
            }

            // ---- Protecao Sub (Processor) ----
            {
                AdvParamDef defs[] = {
                    {"SP_HPF_BASE_CUT",     "HPF Base Cut",        30.f,200.f,  1.0f,"Hz", "Freq base HPF protecao sub"},
                    {"SP_SUB_LPF_FREQ",     "Sub LPF Freq",        20.f,200.f,  1.0f,"Hz", "LPF isolar banda sub"},
                    {"SP_BODY_LPF_FREQ",    "Body LPF Freq",       50.f,500.f,  1.0f,"Hz", "LPF isolar banda body"},
                    {"SP_SUB_ENV_ATT_MS",   "Sub Envelope Attack",  0.5f, 50.f, 0.5f,"ms", "Attack envelope sub"},
                    {"SP_SUB_ENV_REL_MS",   "Sub Envelope Release",10.f,500.f,  5.0f,"ms", "Release envelope sub"},
                    {"SP_BODY_ENV_ATT_MS",  "Body Envelope Attack", 0.5f, 50.f, 0.5f,"ms", "Attack envelope body"},
                    {"SP_BODY_ENV_REL_MS",  "Body Envelope Release",10.f,500.f,  5.0f,"ms", "Release envelope body"},
                    {"SP_TARGET_CUT_MIN",   "Target Cut Min.",      30.f,100.f,  1.0f,"Hz", "Freq min. HPF adaptativo"},
                    {"SP_TARGET_CUT_MAX",   "Target Cut Max.",      50.f,150.f,  1.0f,"Hz", "Freq max. HPF adaptativo"},
                    {"SP_DECIMATE_FACTOR",  "Fator Decimacao",      1.0f,  16.0f,  1.0f,  "x", "Decimacao da analise"},
                };
                addTab ("Protecao Sub (Proc.)", defs, sizeof(defs)/sizeof(defs[0]));
            }
        }

        //======================================================================
        void addTab (const juce::String& tabName, const AdvParamDef* defs, int numDefs)
        {
            // CORREÇÃO v49: Construtor simplificado
            auto* tabContent = new TabContent (params, apvtsState, defs, numDefs);
            tabbedComponent.addTab (tabName, juce::Colour (0xFF1A1A1A), tabContent, true);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelContent)
    };

    // Class member
    PanelContent panelContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedPanelWindow)
};
