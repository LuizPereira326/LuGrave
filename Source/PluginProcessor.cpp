#include "PluginProcessor.h"
#include "PluginEditor.h"

// Definição do ID estático para o ValueTree de parâmetros avançados
const juce::Identifier MaxxBassAudioProcessor::advancedParamsTreeId { "AdvancedParams" };

//==============================================================================
// v50: Ação undoável para preset load / reset
// Armazena snapshots completos do APVTS antes e depois da operação.
// Usa apvts.copyState() (deep copy) para garantir independência entre ações.
//==============================================================================
namespace {

struct PluginStateAction : public juce::UndoableAction
{
    PluginStateAction (MaxxBassAudioProcessor& p,
                       const juce::ValueTree& before,
                       const juce::ValueTree& after)
        : proc       (p),
          stateBefore (before.createCopy()),
          stateAfter  (after.createCopy())
    {}

    bool perform() override
    {
        proc.apvts.replaceState (stateAfter);
        proc.refreshAdvancedParamsFromAPVTS();
        proc.lastSubCutFreq    = -1.0f;
        proc.smoothedTargetCut = -1.0f;
        if (proc.onPresetLoaded) proc.onPresetLoaded();
        return true;
    }

    bool undo() override
    {
        proc.apvts.replaceState (stateBefore);
        proc.refreshAdvancedParamsFromAPVTS();
        proc.lastSubCutFreq    = -1.0f;
        proc.smoothedTargetCut = -1.0f;
        if (proc.onPresetLoaded) proc.onPresetLoaded();
        return true;
    }

    int getSizeInUnits() override { return 1; }

    MaxxBassAudioProcessor& proc;
    juce::ValueTree stateBefore, stateAfter;
};

} // namespace

//==============================================================================
// v50: Métodos de gerenciamento de estado (Undo/Redo, Presets, Reset)
//==============================================================================

// Fase 1 — captura o estado ANTES da operação undoável.
// Deve ser chamado imediatamente antes de qualquer mudança de estado.
void MaxxBassAudioProcessor::saveStateForUndo()
{
    undoStateBefore = apvts.copyState();
}

// Fase 2 — cria a ação undo com antes/depois e empurra no undoManager.
// Chamado internamente depois que a operação foi concluída.
void MaxxBassAudioProcessor::commitUndoTransaction (const juce::String& actionName)
{
    if (! undoStateBefore.isValid())
        return;

    auto stateAfter = apvts.copyState();
    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new PluginStateAction (*this, undoStateBefore, stateAfter));
    undoStateBefore = {};   // libera referência
}

// Salva preset em XML — não altera estado, portanto sem undo
void MaxxBassAudioProcessor::savePresetToFile (const juce::File& file)
{
    auto xml = apvts.state.createXml();
    if (xml != nullptr)
        xml->writeTo (file);
}

void MaxxBassAudioProcessor::loadPresetFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    auto xml = juce::XMLDocument::parse (file);
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        saveStateForUndo();   // Captura ANTES do carregamento

        auto newState = juce::ValueTree::fromXml (*xml);
        apvts.replaceState (newState);
        refreshAdvancedParamsFromAPVTS();

        // Resetar filtros para evitar clicks
        lastSubCutFreq    = -1.0f;
        smoothedTargetCut = -1.0f;
        cachedSubLPFFreq  = -1.0f;
        cachedBodyLPFFreq = -1.0f;
        cachedSubEnvAtt   = -1.0f;
        cachedSubEnvRel   = -1.0f;
        cachedBodyEnvAtt  = -1.0f;
        cachedBodyEnvRel  = -1.0f;
        cachedDecimate    = -1;

        commitUndoTransaction ("Load Preset");  // Registra antes → depois no undoManager

        if (onPresetLoaded != nullptr)
            onPresetLoaded();
    }
}

void MaxxBassAudioProcessor::resetToFactoryDefaults()
{
    saveStateForUndo();   // Captura ANTES do reset

    // getParameters() é a API pública do AudioProcessor para iterar parâmetros.
    // apvts.parameters não é membro público documentado do APVTS.
    for (auto* param : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    refreshAdvancedParamsFromAPVTS();

    // Resetar filtros
    lastSubCutFreq    = -1.0f;
    smoothedTargetCut = -1.0f;
    cachedSubLPFFreq  = -1.0f;
    cachedBodyLPFFreq = -1.0f;
    cachedSubEnvAtt   = -1.0f;
    cachedSubEnvRel   = -1.0f;
    cachedBodyEnvAtt  = -1.0f;
    cachedBodyEnvRel  = -1.0f;
    cachedDecimate    = -1;

    commitUndoTransaction ("Reset to Defaults");  // Registra antes → depois

    if (onPresetLoaded != nullptr)
        onPresetLoaded();
}
static void syncAdvancedParamsFromApvts (juce::AudioProcessorValueTreeState& apvts, AdvancedParams& params)
{
    #define SYNC_ADV(name) if (auto* value = apvts.getRawParameterValue (#name)) params.name = value->load();

    // CORREÇÃO: Parâmetros BASE/RANGE do SimpleEngine (v48)
    SYNC_ADV(SE_H2_BASE)     SYNC_ADV(SE_H2_RANGE)     SYNC_ADV(SE_H3_BASE)     SYNC_ADV(SE_H3_RANGE)
    SYNC_ADV(SE_H4_BASE)     SYNC_ADV(SE_H4_RANGE)     SYNC_ADV(SE_H5_BASE)     SYNC_ADV(SE_H5_RANGE)
    SYNC_ADV(SE_LOUDNESS_COMP)     SYNC_ADV(SE_MIX_MULTIPLIER)     SYNC_ADV(SE_HARM_LPF_FREQ)     SYNC_ADV(SE_HPF_MULT_1)
    SYNC_ADV(SE_HPF_MULT_2)     SYNC_ADV(SE_LPF_MULT)     SYNC_ADV(SE_LPF_MIN)     SYNC_ADV(SE_LPF_MAX)
    SYNC_ADV(SE_DC_BLOCK_FREQ)     SYNC_ADV(SE_HARM_DC_BLOCK_FREQ)     SYNC_ADV(SE_RAMP_SEC)
    SYNC_ADV(SE_ADAPTIVE_THRESH_INIT)     SYNC_ADV(SE_VETO_STRENGTH_INIT)     SYNC_ADV(SE_CONF_HIGH)
    SYNC_ADV(CH_T2_SCALE)     SYNC_ADV(CH_T3_CLAMP)     SYNC_ADV(CH_T4_CLAMP)     SYNC_ADV(CH_T5_SCALE)
    SYNC_ADV(CH_T5_CLAMP)     SYNC_ADV(CH_DRIVE_LOG_SCALE)     SYNC_ADV(CH_PRE_CLAMP)
    SYNC_ADV(CH_OUTPUT_CLAMP_4H)     SYNC_ADV(CH_OUTPUT_CLAMP_5H)     SYNC_ADV(HE_H2_BASE)     SYNC_ADV(HE_H2_RANGE)
    SYNC_ADV(HE_H3_BASE)     SYNC_ADV(HE_H3_RANGE)     SYNC_ADV(HE_H4_BASE)     SYNC_ADV(HE_H4_RANGE)
    SYNC_ADV(HE_H5_BASE)     SYNC_ADV(HE_H5_RANGE)     SYNC_ADV(HE_PERCEPTUAL_BOOST)     SYNC_ADV(HE_LOUDNESS_COMP)
    SYNC_ADV(HE_HARM_LPF_FREQ)     SYNC_ADV(HE_HPF_MULT)     SYNC_ADV(HE_LPF_MULT)     SYNC_ADV(HE_LPF_MIN)
    SYNC_ADV(HE_LPF_MAX)     SYNC_ADV(HE_DC_BLOCK_FREQ)     SYNC_ADV(HE_DYN_RAMP_SEC)     SYNC_ADV(HE_SMOOTH_RAMP_SEC)
    SYNC_ADV(SBS_SUB_MAX_ATTEN)     SYNC_ADV(SBS_BODY_MAX_ATTEN)     SYNC_ADV(SBS_SUB_MIN_RETAIN)     SYNC_ADV(SBS_BODY_MIN_RETAIN)
    SYNC_ADV(SBS_SPLIT_FREQ)     SYNC_ADV(BL_MAX_LEAN)     SYNC_ADV(BL_FAST_ATT_MS)     SYNC_ADV(BL_FAST_REL_MS)
    SYNC_ADV(BL_SLOW_ATT_MS)     SYNC_ADV(BL_SLOW_REL_MS)     SYNC_ADV(BL_LEAN_SMOOTH_MS)     SYNC_ADV(BL_CREST_THRESH)
    SYNC_ADV(BL_CREST_SCALE)     SYNC_ADV(BL_FULLNESS_SCALE)     SYNC_ADV(DHG_BASE_THRESH_LOW)     SYNC_ADV(DHG_BASE_THRESH_HIGH)
    SYNC_ADV(DHG_BASE_BOOST)     SYNC_ADV(DHG_BASE_CUT)     SYNC_ADV(DHG_MIN_BOOST)     SYNC_ADV(DHG_MIN_CUT)
    SYNC_ADV(DHG_CENTER)     SYNC_ADV(DHG_HALF_WIDTH_MIN)     SYNC_ADV(DHG_HALF_WIDTH_RANGE)     SYNC_ADV(DHG_ATT_MS)
    SYNC_ADV(DHG_REL_MS)     SYNC_ADV(SN_TARGET_LEVEL)     SYNC_ADV(SN_MAX_BOOST)     SYNC_ADV(SN_MAX_REDUCE)
    SYNC_ADV(SN_BOOST_RATIO)     SYNC_ADV(SN_COMP_SLOPE)     SYNC_ADV(SN_ATT_MS)     SYNC_ADV(SN_REL_MS)
    SYNC_ADV(SN_GAIN_SMOOTH_MS)     SYNC_ADV(SN_LPF_FREQ)     SYNC_ADV(ADG_SAFE_LIMIT)     SYNC_ADV(ADG_HARD_LIMIT)
    SYNC_ADV(ADG_SATURATION_START)     SYNC_ADV(ADG_SAT_OVER_NORM_FACTOR)     SYNC_ADV(ADG_LIMIT_FACTOR)     SYNC_ADV(DVP_MIN_HARM_GAIN)
    SYNC_ADV(DVP_MAX_NOTCH_DEPTH)     SYNC_ADV(DVP_PHASE_SHIFT_MAX)     SYNC_ADV(DVP_FREE_BAND_BOOST)     SYNC_ADV(DVP_VOCAL_PHASE_THRESH)
    SYNC_ADV(DVP_NOTCH_VOCAL_THRESH)     SYNC_ADV(DVP_BANDED_THRESH)     SYNC_ADV(DVP_CONFLICT_REDUCTION_MAX)     SYNC_ADV(DVP_FREE_BOOST_MAX)
    SYNC_ADV(DVP_PHASE_FADE)     SYNC_ADV(DVP_KICK_DUCK_AMOUNT)     SYNC_ADV(DVP_PUNCH_REDUCTION_MAX)     SYNC_ADV(DVP_PHASE_MIX_FACTOR)
    SYNC_ADV(SKD_TRANSIENT_THRESH)     SYNC_ADV(SKD_DUCK_AMOUNT)     SYNC_ADV(SKD_DUCK_DURATION)     SYNC_ADV(SKD_DUCK_COOLDOWN)
    SYNC_ADV(SKD_BAND_CENTER)     SYNC_ADV(SKD_BAND_Q)     SYNC_ADV(SKD_ATTACK_HPF_FREQ)     SYNC_ADV(SKD_ATTACK_MS)
    SYNC_ADV(SKD_RELEASE_MS)     SYNC_ADV(SP_HPF_BASE_CUT)     SYNC_ADV(SP_SUB_LPF_FREQ)     SYNC_ADV(SP_BODY_LPF_FREQ)
    SYNC_ADV(SP_SUB_ENV_ATT_MS)     SYNC_ADV(SP_SUB_ENV_REL_MS)     SYNC_ADV(SP_BODY_ENV_ATT_MS)     SYNC_ADV(SP_BODY_ENV_REL_MS)
    SYNC_ADV(SP_TARGET_CUT_MIN)     SYNC_ADV(SP_TARGET_CUT_MAX)     SYNC_ADV(SP_DECIMATE_FACTOR)
    SYNC_ADV(SP_CUTOFF_SMOOTH_MS)   SYNC_ADV(SP_MUSIC_LIFT_MAX)   SYNC_ADV(SP_PRESSURE_LIFT_MAX)  SYNC_ADV(SP_HARD_LIFT_MAX)
    SYNC_ADV(SP_DRIVE_LIFT_MAX)     SYNC_ADV(SP_MIX_LIFT_MAX)     SYNC_ADV(SP_GAIN_LIFT_MAX)

    #undef SYNC_ADV
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
MaxxBassAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const AdvancedParams defaults;

    auto addAdvancedFloat = [&layout] (const juce::String& id, float min, float max, float step, float def, const juce::String& label = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            id, id,
            juce::NormalisableRange<float> (min, max, step),
            def, juce::AudioParameterFloatAttributes{}.withLabel (label)));
    };

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "cutFreq", "Frequencia",
        juce::NormalisableRange<float>(20.0f, 300.0f, 0.5f, 0.35f),
        80.0f, juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "harmMix", "Bass Fake",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        65.0f, juce::AudioParameterFloatAttributes{}.withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "drive", "Drive",
        juce::NormalisableRange<float>(1.0f, 16.0f, 0.1f, 0.45f),
        4.5f, juce::AudioParameterFloatAttributes{}.withLabel("x")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "harmChar", "Caracter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f),
        40.0f, juce::AudioParameterFloatAttributes{}.withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "dynResp", "Dinamica",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f),
        50.0f, juce::AudioParameterFloatAttributes{}.withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output",
        juce::NormalisableRange<float>(-18.0f, 12.0f, 0.1f),
        0.0f, juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));

    // Novo: seleciona o motor de processamento
    layout.add (std::make_unique<juce::AudioParameterBool>(
        "simpleMode", "Modo Simples", false));   // false = Hybrid (default)

    layout.add (std::make_unique<juce::AudioParameterBool>(
        "subProtect", "Cortar Sub", true));      // proteção para caixas pequenas

    // CORREÇÃO: Parâmetros BASE/RANGE do SimpleEngine (v48) - substituem os legados SE_H2/SE_H3/SE_H4
    addAdvancedFloat ("SE_H2_BASE", 0.0f, 2.0f, 0.01f, defaults.SE_H2_BASE);
    addAdvancedFloat ("SE_H2_RANGE", 0.0f, 1.0f, 0.01f, defaults.SE_H2_RANGE);
    addAdvancedFloat ("SE_H3_BASE", 0.0f, 2.0f, 0.01f, defaults.SE_H3_BASE);
    addAdvancedFloat ("SE_H3_RANGE", 0.0f, 1.0f, 0.01f, defaults.SE_H3_RANGE);
    addAdvancedFloat ("SE_H4_BASE", 0.0f, 2.0f, 0.01f, defaults.SE_H4_BASE);
    addAdvancedFloat ("SE_H4_RANGE", 0.0f, 1.0f, 0.01f, defaults.SE_H4_RANGE);
    addAdvancedFloat ("SE_H5_BASE", 0.0f, 2.0f, 0.01f, defaults.SE_H5_BASE);
    addAdvancedFloat ("SE_H5_RANGE", 0.0f, 1.0f, 0.01f, defaults.SE_H5_RANGE);
    addAdvancedFloat ("SE_LOUDNESS_COMP", 0.0f, 1.0f, 0.01f, defaults.SE_LOUDNESS_COMP);
    addAdvancedFloat ("SE_MIX_MULTIPLIER", 0.1f, 5.0f, 0.01f, defaults.SE_MIX_MULTIPLIER, "x");
    addAdvancedFloat ("SE_HARM_LPF_FREQ", 100.0f, 1000.0f, 1.0f, defaults.SE_HARM_LPF_FREQ, "Hz");
    addAdvancedFloat ("SE_HPF_MULT_1", 1.0f, 5.0f, 0.1f, defaults.SE_HPF_MULT_1, "x");
    addAdvancedFloat ("SE_HPF_MULT_2", 1.0f, 5.0f, 0.1f, defaults.SE_HPF_MULT_2, "x");
    addAdvancedFloat ("SE_LPF_MULT", 1.0f, 10.0f, 0.1f, defaults.SE_LPF_MULT, "x");
    addAdvancedFloat ("SE_LPF_MIN", 50.0f, 500.0f, 1.0f, defaults.SE_LPF_MIN, "Hz");
    addAdvancedFloat ("SE_LPF_MAX", 100.0f, 1000.0f, 1.0f, defaults.SE_LPF_MAX, "Hz");
    addAdvancedFloat ("SE_DC_BLOCK_FREQ", 1.0f, 50.0f, 0.5f, defaults.SE_DC_BLOCK_FREQ, "Hz");
    addAdvancedFloat ("SE_HARM_DC_BLOCK_FREQ", 1.0f, 100.0f, 0.5f, defaults.SE_HARM_DC_BLOCK_FREQ, "Hz");
    addAdvancedFloat ("SE_RAMP_SEC", 0.001f, 0.5f, 0.001f, defaults.SE_RAMP_SEC, "s");
    addAdvancedFloat ("SE_ADAPTIVE_THRESH_INIT", 0.0f, 1.0f, 0.01f, defaults.SE_ADAPTIVE_THRESH_INIT);
    addAdvancedFloat ("SE_VETO_STRENGTH_INIT", 0.0f, 1.0f, 0.01f, defaults.SE_VETO_STRENGTH_INIT);
    addAdvancedFloat ("SE_CONF_HIGH", 0.0f, 1.0f, 0.01f, defaults.SE_CONF_HIGH);
    addAdvancedFloat ("CH_T2_SCALE", 0.1f, 10.0f, 0.1f, defaults.CH_T2_SCALE);
    addAdvancedFloat ("CH_T3_CLAMP", 0.1f, 5.0f, 0.1f, defaults.CH_T3_CLAMP);
    addAdvancedFloat ("CH_T4_CLAMP", 0.1f, 5.0f, 0.1f, defaults.CH_T4_CLAMP);
    addAdvancedFloat ("CH_T5_SCALE", 0.0f, 2.0f, 0.01f, defaults.CH_T5_SCALE);
    addAdvancedFloat ("CH_T5_CLAMP", 0.1f, 5.0f, 0.1f, defaults.CH_T5_CLAMP);
    addAdvancedFloat ("CH_DRIVE_LOG_SCALE", 0.1f, 3.0f, 0.01f, defaults.CH_DRIVE_LOG_SCALE);
    addAdvancedFloat ("CH_PRE_CLAMP", 0.5f, 5.0f, 0.1f, defaults.CH_PRE_CLAMP);
    addAdvancedFloat ("CH_OUTPUT_CLAMP_4H", 0.5f, 10.0f, 0.1f, defaults.CH_OUTPUT_CLAMP_4H);
    addAdvancedFloat ("CH_OUTPUT_CLAMP_5H", 0.5f, 10.0f, 0.1f, defaults.CH_OUTPUT_CLAMP_5H);
    addAdvancedFloat ("HE_H2_BASE", 0.0f, 2.0f, 0.01f, defaults.HE_H2_BASE);
    addAdvancedFloat ("HE_H2_RANGE", 0.0f, 1.0f, 0.01f, defaults.HE_H2_RANGE);
    addAdvancedFloat ("HE_H3_BASE", 0.0f, 2.0f, 0.01f, defaults.HE_H3_BASE);
    addAdvancedFloat ("HE_H3_RANGE", 0.0f, 1.0f, 0.01f, defaults.HE_H3_RANGE);
    addAdvancedFloat ("HE_H4_BASE", 0.0f, 2.0f, 0.01f, defaults.HE_H4_BASE);
    addAdvancedFloat ("HE_H4_RANGE", 0.0f, 1.0f, 0.01f, defaults.HE_H4_RANGE);
    addAdvancedFloat ("HE_H5_BASE", 0.0f, 2.0f, 0.01f, defaults.HE_H5_BASE);
    addAdvancedFloat ("HE_H5_RANGE", 0.0f, 1.0f, 0.01f, defaults.HE_H5_RANGE);
    addAdvancedFloat ("HE_PERCEPTUAL_BOOST", 0.0f, 2.0f, 0.01f, defaults.HE_PERCEPTUAL_BOOST);
    addAdvancedFloat ("HE_LOUDNESS_COMP", 0.0f, 1.0f, 0.01f, defaults.HE_LOUDNESS_COMP);
    addAdvancedFloat ("HE_HARM_LPF_FREQ", 100.0f, 2000.0f, 1.0f, defaults.HE_HARM_LPF_FREQ, "Hz");
    addAdvancedFloat ("HE_HPF_MULT", 0.5f, 5.0f, 0.05f, defaults.HE_HPF_MULT, "x");
    addAdvancedFloat ("HE_LPF_MULT", 1.0f, 10.0f, 0.1f, defaults.HE_LPF_MULT, "x");
    addAdvancedFloat ("HE_LPF_MIN", 50.0f, 1000.0f, 1.0f, defaults.HE_LPF_MIN, "Hz");
    addAdvancedFloat ("HE_LPF_MAX", 100.0f, 2000.0f, 1.0f, defaults.HE_LPF_MAX, "Hz");
    addAdvancedFloat ("HE_DC_BLOCK_FREQ", 1.0f, 50.0f, 0.5f, defaults.HE_DC_BLOCK_FREQ, "Hz");
    addAdvancedFloat ("HE_DYN_RAMP_SEC", 0.001f, 0.5f, 0.001f, defaults.HE_DYN_RAMP_SEC, "s");
    addAdvancedFloat ("HE_SMOOTH_RAMP_SEC", 0.001f, 0.5f, 0.001f, defaults.HE_SMOOTH_RAMP_SEC, "s");
    addAdvancedFloat ("SBS_SUB_MAX_ATTEN", 0.0f, 1.0f, 0.01f, defaults.SBS_SUB_MAX_ATTEN);
    addAdvancedFloat ("SBS_BODY_MAX_ATTEN", 0.0f, 1.0f, 0.01f, defaults.SBS_BODY_MAX_ATTEN);
    addAdvancedFloat ("SBS_SUB_MIN_RETAIN", 0.0f, 1.0f, 0.01f, defaults.SBS_SUB_MIN_RETAIN);
    addAdvancedFloat ("SBS_BODY_MIN_RETAIN", 0.0f, 1.0f, 0.01f, defaults.SBS_BODY_MIN_RETAIN);
    addAdvancedFloat ("SBS_SPLIT_FREQ", 20.0f, 150.0f, 1.0f, defaults.SBS_SPLIT_FREQ, "Hz");
    addAdvancedFloat ("BL_MAX_LEAN", 0.0f, 0.8f, 0.01f, defaults.BL_MAX_LEAN);
    addAdvancedFloat ("BL_FAST_ATT_MS", 0.5f, 50.0f, 0.5f, defaults.BL_FAST_ATT_MS, "ms");
    addAdvancedFloat ("BL_FAST_REL_MS", 10.0f, 500.0f, 5.0f, defaults.BL_FAST_REL_MS, "ms");
    addAdvancedFloat ("BL_SLOW_ATT_MS", 1.0f, 200.0f, 1.0f, defaults.BL_SLOW_ATT_MS, "ms");
    addAdvancedFloat ("BL_SLOW_REL_MS", 50.0f, 2000.0f, 10.0f, defaults.BL_SLOW_REL_MS, "ms");
    addAdvancedFloat ("BL_LEAN_SMOOTH_MS", 1.0f, 200.0f, 1.0f, defaults.BL_LEAN_SMOOTH_MS, "ms");
    addAdvancedFloat ("BL_CREST_THRESH", 0.5f, 5.0f, 0.1f, defaults.BL_CREST_THRESH);
    addAdvancedFloat ("BL_CREST_SCALE", 0.1f, 2.0f, 0.05f, defaults.BL_CREST_SCALE);
    addAdvancedFloat ("BL_FULLNESS_SCALE", 0.5f, 10.0f, 0.1f, defaults.BL_FULLNESS_SCALE);
    addAdvancedFloat ("DHG_BASE_THRESH_LOW", 0.01f, 0.5f, 0.01f, defaults.DHG_BASE_THRESH_LOW);
    addAdvancedFloat ("DHG_BASE_THRESH_HIGH", 0.1f, 1.0f, 0.01f, defaults.DHG_BASE_THRESH_HIGH);
    addAdvancedFloat ("DHG_BASE_BOOST", 1.0f, 3.0f, 0.01f, defaults.DHG_BASE_BOOST);
    addAdvancedFloat ("DHG_BASE_CUT", 0.3f, 1.0f, 0.01f, defaults.DHG_BASE_CUT);
    addAdvancedFloat ("DHG_MIN_BOOST", 1.0f, 2.0f, 0.01f, defaults.DHG_MIN_BOOST);
    addAdvancedFloat ("DHG_MIN_CUT", 0.5f, 1.0f, 0.01f, defaults.DHG_MIN_CUT);
    addAdvancedFloat ("DHG_CENTER", 0.05f, 0.5f, 0.01f, defaults.DHG_CENTER);
    addAdvancedFloat ("DHG_HALF_WIDTH_MIN", 0.01f, 0.2f, 0.01f, defaults.DHG_HALF_WIDTH_MIN);
    addAdvancedFloat ("DHG_HALF_WIDTH_RANGE", 0.01f, 0.5f, 0.01f, defaults.DHG_HALF_WIDTH_RANGE);
    addAdvancedFloat ("DHG_ATT_MS", 1.0f, 100.0f, 0.5f, defaults.DHG_ATT_MS, "ms");
    addAdvancedFloat ("DHG_REL_MS", 10.0f, 1000.0f, 5.0f, defaults.DHG_REL_MS, "ms");
    addAdvancedFloat ("SN_TARGET_LEVEL", 0.01f, 0.5f, 0.01f, defaults.SN_TARGET_LEVEL);
    addAdvancedFloat ("SN_MAX_BOOST", 1.0f, 5.0f, 0.1f, defaults.SN_MAX_BOOST);
    addAdvancedFloat ("SN_MAX_REDUCE", 0.1f, 1.0f, 0.05f, defaults.SN_MAX_REDUCE);
    addAdvancedFloat ("SN_BOOST_RATIO", 0.1f, 2.0f, 0.05f, defaults.SN_BOOST_RATIO);
    addAdvancedFloat ("SN_COMP_SLOPE", 0.1f, 2.0f, 0.05f, defaults.SN_COMP_SLOPE);
    addAdvancedFloat ("SN_ATT_MS", 10.0f, 2000.0f, 10.0f, defaults.SN_ATT_MS, "ms");
    addAdvancedFloat ("SN_REL_MS", 100.0f, 5000.0f, 50.0f, defaults.SN_REL_MS, "ms");
    addAdvancedFloat ("SN_GAIN_SMOOTH_MS", 5.0f, 500.0f, 5.0f, defaults.SN_GAIN_SMOOTH_MS, "ms");
    addAdvancedFloat ("SN_LPF_FREQ", 20.0f, 200.0f, 1.0f, defaults.SN_LPF_FREQ, "Hz");
    addAdvancedFloat ("ADG_SAFE_LIMIT", 1.0f, 10.0f, 0.1f, defaults.ADG_SAFE_LIMIT);
    addAdvancedFloat ("ADG_HARD_LIMIT", 1.0f, 10.0f, 0.1f, defaults.ADG_HARD_LIMIT);
    addAdvancedFloat ("ADG_SATURATION_START", 0.5f, 5.0f, 0.1f, defaults.ADG_SATURATION_START);
    addAdvancedFloat ("ADG_SAT_OVER_NORM_FACTOR", 0.0f, 1.0f, 0.01f, defaults.ADG_SAT_OVER_NORM_FACTOR);
    addAdvancedFloat ("ADG_LIMIT_FACTOR", 0.0f, 1.0f, 0.01f, defaults.ADG_LIMIT_FACTOR);
    addAdvancedFloat ("DVP_MIN_HARM_GAIN", 0.5f, 1.0f, 0.01f, defaults.DVP_MIN_HARM_GAIN);
    addAdvancedFloat ("DVP_MAX_NOTCH_DEPTH", 0.0f, 1.0f, 0.01f, defaults.DVP_MAX_NOTCH_DEPTH);
    addAdvancedFloat ("DVP_PHASE_SHIFT_MAX", 0.0f, 1.0f, 0.01f, defaults.DVP_PHASE_SHIFT_MAX);
    addAdvancedFloat ("DVP_FREE_BAND_BOOST", 1.0f, 2.0f, 0.01f, defaults.DVP_FREE_BAND_BOOST);
    addAdvancedFloat ("DVP_VOCAL_PHASE_THRESH", 0.0f, 1.0f, 0.01f, defaults.DVP_VOCAL_PHASE_THRESH);
    addAdvancedFloat ("DVP_NOTCH_VOCAL_THRESH", 0.0f, 1.0f, 0.01f, defaults.DVP_NOTCH_VOCAL_THRESH);
    addAdvancedFloat ("DVP_BANDED_THRESH", 0.0f, 1.0f, 0.01f, defaults.DVP_BANDED_THRESH);
    addAdvancedFloat ("DVP_CONFLICT_REDUCTION_MAX", 0.0f, 0.5f, 0.01f, defaults.DVP_CONFLICT_REDUCTION_MAX);
    addAdvancedFloat ("DVP_FREE_BOOST_MAX", 0.0f, 1.0f, 0.01f, defaults.DVP_FREE_BOOST_MAX);
    addAdvancedFloat ("DVP_PHASE_FADE", 0.9f, 1.0f, 0.001f, defaults.DVP_PHASE_FADE);
    addAdvancedFloat ("DVP_KICK_DUCK_AMOUNT", 0.0f, 0.8f, 0.01f, defaults.DVP_KICK_DUCK_AMOUNT);
    addAdvancedFloat ("DVP_PUNCH_REDUCTION_MAX", 0.0f, 0.5f, 0.01f, defaults.DVP_PUNCH_REDUCTION_MAX);
    addAdvancedFloat ("DVP_PHASE_MIX_FACTOR", 0.0f, 1.0f, 0.01f, defaults.DVP_PHASE_MIX_FACTOR);
    addAdvancedFloat ("SKD_TRANSIENT_THRESH", 0.05f, 1.0f, 0.01f, defaults.SKD_TRANSIENT_THRESH);
    addAdvancedFloat ("SKD_DUCK_AMOUNT", 0.0f, 0.8f, 0.01f, defaults.SKD_DUCK_AMOUNT);
    addAdvancedFloat ("SKD_DUCK_DURATION", 10.0f, 1000.0f, 1.0f, defaults.SKD_DUCK_DURATION, "samples");
    addAdvancedFloat ("SKD_DUCK_COOLDOWN", 100.0f, 5000.0f, 10.0f, defaults.SKD_DUCK_COOLDOWN, "samples");
    addAdvancedFloat ("SKD_BAND_CENTER", 30.0f, 200.0f, 1.0f, defaults.SKD_BAND_CENTER, "Hz");
    addAdvancedFloat ("SKD_BAND_Q", 0.5f, 10.0f, 0.1f, defaults.SKD_BAND_Q);
    addAdvancedFloat ("SKD_ATTACK_HPF_FREQ", 30.0f, 500.0f, 1.0f, defaults.SKD_ATTACK_HPF_FREQ, "Hz");
    addAdvancedFloat ("SKD_ATTACK_MS", 0.1f, 10.0f, 0.1f, defaults.SKD_ATTACK_MS, "ms");
    addAdvancedFloat ("SKD_RELEASE_MS", 1.0f, 200.0f, 1.0f, defaults.SKD_RELEASE_MS, "ms");
    addAdvancedFloat ("SP_HPF_BASE_CUT", 30.0f, 200.0f, 1.0f, defaults.SP_HPF_BASE_CUT, "Hz");
    addAdvancedFloat ("SP_SUB_LPF_FREQ", 20.0f, 200.0f, 1.0f, defaults.SP_SUB_LPF_FREQ, "Hz");
    addAdvancedFloat ("SP_BODY_LPF_FREQ", 50.0f, 500.0f, 1.0f, defaults.SP_BODY_LPF_FREQ, "Hz");
    addAdvancedFloat ("SP_SUB_ENV_ATT_MS", 0.5f, 50.0f, 0.5f, defaults.SP_SUB_ENV_ATT_MS, "ms");
    addAdvancedFloat ("SP_SUB_ENV_REL_MS", 10.0f, 500.0f, 5.0f, defaults.SP_SUB_ENV_REL_MS, "ms");
    addAdvancedFloat ("SP_BODY_ENV_ATT_MS", 0.5f, 50.0f, 0.5f, defaults.SP_BODY_ENV_ATT_MS, "ms");
    addAdvancedFloat ("SP_BODY_ENV_REL_MS", 10.0f, 500.0f, 5.0f, defaults.SP_BODY_ENV_REL_MS, "ms");
    addAdvancedFloat ("SP_TARGET_CUT_MIN", 30.0f, 100.0f, 1.0f, defaults.SP_TARGET_CUT_MIN, "Hz");
    addAdvancedFloat ("SP_TARGET_CUT_MAX", 50.0f, 150.0f, 1.0f, defaults.SP_TARGET_CUT_MAX, "Hz");
    addAdvancedFloat ("SP_DECIMATE_FACTOR", 1.0f, 16.0f, 1.0f, defaults.SP_DECIMATE_FACTOR, "x");
    
    // MELHORIA v49: Parâmetros de suavização e lifts do Sub Cut
    addAdvancedFloat ("SP_CUTOFF_SMOOTH_MS", 10.0f, 500.0f, 5.0f, defaults.SP_CUTOFF_SMOOTH_MS, "ms");
    addAdvancedFloat ("SP_MUSIC_LIFT_MAX", 0.0f, 15.0f, 0.5f, defaults.SP_MUSIC_LIFT_MAX, "Hz");
    addAdvancedFloat ("SP_PRESSURE_LIFT_MAX", 0.0f, 30.0f, 1.0f, defaults.SP_PRESSURE_LIFT_MAX, "Hz");
    addAdvancedFloat ("SP_HARD_LIFT_MAX", 0.0f, 20.0f, 1.0f, defaults.SP_HARD_LIFT_MAX, "Hz");
    addAdvancedFloat ("SP_DRIVE_LIFT_MAX", 0.0f, 10.0f, 0.5f, defaults.SP_DRIVE_LIFT_MAX, "Hz");
    addAdvancedFloat ("SP_MIX_LIFT_MAX", 0.0f, 10.0f, 0.5f, defaults.SP_MIX_LIFT_MAX, "Hz");
    addAdvancedFloat ("SP_GAIN_LIFT_MAX", 0.0f, 10.0f, 0.5f, defaults.SP_GAIN_LIFT_MAX, "Hz");

    return layout;
}

//==============================================================================
MaxxBassAudioProcessor::MaxxBassAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    refreshAdvancedParamsFromAPVTS();
}

MaxxBassAudioProcessor::~MaxxBassAudioProcessor() {}

void MaxxBassAudioProcessor::refreshAdvancedParamsFromAPVTS()
{
    syncAdvancedParamsFromApvts (apvts, audioParams);
}

//==============================================================================
void MaxxBassAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSR = sampleRate;
    lastSubCutFreq    = -1.0f;
    smoothedTargetCut = -1.0f;  // v50: -1 sinaliza "primeiro bloco" para o IIR
    // Cache de detecção: invalida para forçar re-setup no primeiro bloco
    cachedSubLPFFreq  = -1.0f;
    cachedBodyLPFFreq = -1.0f;
    cachedSubEnvAtt   = -1.0f;
    cachedSubEnvRel   = -1.0f;
    cachedBodyEnvAtt  = -1.0f;
    cachedBodyEnvRel  = -1.0f;
    cachedDecimate    = -1;
    refreshAdvancedParamsFromAPVTS();

    for (int ch = 0; ch < 2; ++ch)
    {
        subProtectHPF[ch].reset();
        subProtectHPF[ch].setHighPass (audioParams.SP_HPF_BASE_CUT, sampleRate);

        subProtectHPF2[ch].reset();
        subProtectHPF2[ch].setHighPass (audioParams.SP_HPF_BASE_CUT, sampleRate);

        subProtectSubLPF [ch].reset();
        subProtectBodyLPF[ch].reset();
        subProtectSubLPF [ch].setLowPass (audioParams.SP_SUB_LPF_FREQ, sampleRate);
        subProtectBodyLPF[ch].setLowPass (audioParams.SP_BODY_LPF_FREQ, sampleRate);

        subProtectSubEnv [ch].prepare (sampleRate, audioParams.SP_SUB_ENV_ATT_MS,  audioParams.SP_SUB_ENV_REL_MS);
        subProtectBodyEnv[ch].prepare (sampleRate, audioParams.SP_BODY_ENV_ATT_MS, audioParams.SP_BODY_ENV_REL_MS);
        subProtectSubEnv [ch].reset();
        subProtectBodyEnv[ch].reset();
    }

    // Prepara ambos os motores usando audioParams (thread-safe)
    simpleEngine.prepare (sampleRate, samplesPerBlock, audioParams);
    hybridEngine.prepare (sampleRate, samplesPerBlock, audioParams);
}

void MaxxBassAudioProcessor::releaseResources()
{
    simpleEngine.reset();
    hybridEngine.reset();
    lastSubCutFreq    = -1.0f;
    smoothedTargetCut = -1.0f;  // v50: reset IIR
    cachedSubLPFFreq  = -1.0f;
    cachedBodyLPFFreq = -1.0f;
    cachedSubEnvAtt   = -1.0f;
    cachedSubEnvRel   = -1.0f;
    cachedBodyEnvAtt  = -1.0f;
    cachedBodyEnvRel  = -1.0f;
    cachedDecimate    = -1;

    for (int ch = 0; ch < 2; ++ch)
    {
        subProtectHPF[ch].reset();
        subProtectHPF2[ch].reset();
        subProtectSubLPF [ch].reset();
        subProtectBodyLPF[ch].reset();
        subProtectSubEnv [ch].reset();
        subProtectBodyEnv[ch].reset();
    }
}

//==============================================================================
void MaxxBassAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    //==========================================================================
    // CORREÇÃO v49: Sincronização simplificada
    //==========================================================================
    // Todos os parâmetros avançados agora estão registrados no APVTS.
    // O APVTS é a fonte única de verdade - sincroniza a cada bloco para
    // suportar automação do host e mudanças da UI.
    //==========================================================================
    syncAdvancedParamsFromApvts (apvts, audioParams);

    // Bypass global: passthrough limpo, sem processar nada
    if (apvts.getRawParameterValue("bypass")->load() > 0.5f)
        return;

    const bool isSimple    = apvts.getRawParameterValue("simpleMode")->load() > 0.5f;
    const bool subProtectOn = apvts.getRawParameterValue("subProtect")->load() > 0.5f;

    // Passar audioParams para os engines
    if (isSimple)
        simpleEngine.processBlock (buffer, apvts, audioParams, inLevel, harmMeter, outLevel);
    else
        hybridEngine.processBlock (buffer, apvts, audioParams, inLevel, harmMeter, outLevel);

    if (! subProtectOn)
        return;

    const float cutFreq  = apvts.getRawParameterValue("cutFreq")->load();
    const float harmMix  = apvts.getRawParameterValue("harmMix")->load();
    const float drive    = apvts.getRawParameterValue("drive")->load();
    const float outputDb = apvts.getRawParameterValue("outputGain")->load();

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int N     = buffer.getNumSamples();
    const int decimate = juce::jmax (1, juce::roundToInt (audioParams.SP_DECIMATE_FACTOR));

    // v50: Correção da decimação — LPFs e envelopes devem usar SR efetivo (currentSR / decimate)
    // Chamadas de setup são caras: só executar quando os parâmetros realmente mudarem
    const double detectorSR = currentSR / (double)decimate;
    if (decimate            != cachedDecimate      ||
        audioParams.SP_SUB_LPF_FREQ   != cachedSubLPFFreq  ||
        audioParams.SP_BODY_LPF_FREQ  != cachedBodyLPFFreq ||
        audioParams.SP_SUB_ENV_ATT_MS != cachedSubEnvAtt   ||
        audioParams.SP_SUB_ENV_REL_MS != cachedSubEnvRel   ||
        audioParams.SP_BODY_ENV_ATT_MS!= cachedBodyEnvAtt  ||
        audioParams.SP_BODY_ENV_REL_MS!= cachedBodyEnvRel)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            subProtectSubLPF [ch].setLowPass (audioParams.SP_SUB_LPF_FREQ,  detectorSR);
            subProtectBodyLPF[ch].setLowPass (audioParams.SP_BODY_LPF_FREQ, detectorSR);
            subProtectSubEnv [ch].setTimes (detectorSR, audioParams.SP_SUB_ENV_ATT_MS,  audioParams.SP_SUB_ENV_REL_MS);
            subProtectBodyEnv[ch].setTimes (detectorSR, audioParams.SP_BODY_ENV_ATT_MS, audioParams.SP_BODY_ENV_REL_MS);
        }
        cachedDecimate     = decimate;
        cachedSubLPFFreq   = audioParams.SP_SUB_LPF_FREQ;
        cachedBodyLPFFreq  = audioParams.SP_BODY_LPF_FREQ;
        cachedSubEnvAtt    = audioParams.SP_SUB_ENV_ATT_MS;
        cachedSubEnvRel    = audioParams.SP_SUB_ENV_REL_MS;
        cachedBodyEnvAtt   = audioParams.SP_BODY_ENV_ATT_MS;
        cachedBodyEnvRel   = audioParams.SP_BODY_ENV_REL_MS;
    }

    auto smoothStep01 = [] (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    };

    float subEnergy  = 0.0f;
    float bodyEnergy = 0.0f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        float subAcc  = 0.0f;
        float bodyAcc = 0.0f;
        int   count   = 0;

        for (int i = 0; i < N; i += decimate)
        {
            const float x        = src[i];
            const float subBand  = subProtectSubLPF [ch].process (x);
            const float bodyLP   = subProtectBodyLPF[ch].process (x);
            const float bodyBand = bodyLP - subBand;

            subAcc  += subProtectSubEnv [ch].process (subBand);
            bodyAcc += subProtectBodyEnv[ch].process (bodyBand);
            ++count;
        }

        subEnergy  += (count > 0 ? subAcc  / (float) count : 0.0f);
        bodyEnergy += (count > 0 ? bodyAcc / (float) count : 0.0f);
    }

    const float subRatio = subEnergy / (bodyEnergy + 1.0e-6f);

    const float balanceSweet = 1.0f - juce::jlimit (0.0f, 1.0f, std::abs (subRatio - 0.65f) / 0.65f);
    const float pressure     = smoothStep01 (juce::jlimit (0.0f, 1.0f, (subRatio - 0.48f) * 1.85f));
    const float aggressive   = smoothStep01 (juce::jlimit (0.0f, 1.0f, (subRatio - 0.98f) * 2.30f));

    const float baseCut     = juce::jmap (cutFreq, 20.0f, 300.0f,
                                          audioParams.SP_HPF_BASE_CUT,
                                          audioParams.SP_TARGET_CUT_MAX);
    const float musicLift   = juce::jmap (balanceSweet, 0.0f, 1.0f, 0.0f, audioParams.SP_MUSIC_LIFT_MAX);
    const float pressureLift= juce::jmap (pressure,     0.0f, 1.0f, 0.0f, audioParams.SP_PRESSURE_LIFT_MAX);
    const float hardLift    = juce::jmap (aggressive,   0.0f, 1.0f, 0.0f, audioParams.SP_HARD_LIFT_MAX);
    // v50: jmax(0) garante que nenhum lift vira negativo e reduz o cutoff abaixo do baseCut
    const float driveLift   = juce::jmax (0.0f, juce::jmap (drive,    1.0f, 16.0f,   0.0f, audioParams.SP_DRIVE_LIFT_MAX));
    const float mixLift     = juce::jmax (0.0f, juce::jmap (harmMix,  0.0f, 100.0f,  0.0f, audioParams.SP_MIX_LIFT_MAX));
    const float gainLift    = juce::jmax (0.0f, juce::jmap (outputDb, -18.0f, 12.0f, 0.0f, audioParams.SP_GAIN_LIFT_MAX));

    const float rawTargetCut = juce::jlimit (audioParams.SP_TARGET_CUT_MIN,
                                             audioParams.SP_TARGET_CUT_MAX,
                                             baseCut + musicLift
                                                    + pressureLift
                                                    + hardLift
                                                    + driveLift * 0.35f
                                                    + mixLift   * 0.20f
                                                    + gainLift  * 0.20f);

    // v50: Suavização one-pole IIR — substitui interpolação linear com janela fixa
    // Vantagens: sem snap periódico, sem overshoot por samplesPerBlock > smoothTotalSamples,
    // rastreia target em movimento continuamente, independe do tamanho do bloco.
    //
    // coeff = 1 - exp(-N / (tau * SR))
    //   N   = amostras por bloco
    //   tau = tempo de suavização em segundos
    //   SR  = sample rate
    // coeff próximo de 0 → suavização lenta; próximo de 1 → quase sem suavização
    {
        const float tau   = juce::jmax (1.0f, audioParams.SP_CUTOFF_SMOOTH_MS) / 1000.0f;
        const float coeff = 1.0f - std::exp (-(float)N / (tau * (float)currentSR));

        if (smoothedTargetCut < 0.0f)
            smoothedTargetCut = rawTargetCut;  // Inicializa no primeiro bloco (sem snap)
        else
            smoothedTargetCut += (rawTargetCut - smoothedTargetCut) * coeff;

        smoothedTargetCut = juce::jlimit (audioParams.SP_TARGET_CUT_MIN,
                                          audioParams.SP_TARGET_CUT_MAX,
                                          smoothedTargetCut);
    }
    const float finalTargetCut = smoothedTargetCut;

    // Atualiza filtros apenas quando houver mudança significativa (> 0.5 Hz para precisão)
    if (std::abs (finalTargetCut - lastSubCutFreq) > 0.5f)
    {
        lastSubCutFreq = finalTargetCut;
        for (int ch = 0; ch < numCh; ++ch)
        {
            subProtectHPF [ch].setHighPass ((double) finalTargetCut, currentSR);
            subProtectHPF2[ch].setHighPass ((double) finalTargetCut, currentSR);
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < N; ++i)
        {
            data[i] = subProtectHPF [ch].process (data[i]);
            data[i] = subProtectHPF2[ch].process (data[i]);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* MaxxBassAudioProcessor::createEditor()
{
    return new MaxxBassAudioProcessorEditor (*this);
}

void MaxxBassAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MaxxBassAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        refreshAdvancedParamsFromAPVTS();
        lastSubCutFreq    = -1.0f;
        smoothedTargetCut = -1.0f;
        cachedSubLPFFreq  = -1.0f;
        cachedBodyLPFFreq = -1.0f;
        cachedSubEnvAtt   = -1.0f;
        cachedSubEnvRel   = -1.0f;
        cachedBodyEnvAtt  = -1.0f;
        cachedBodyEnvRel  = -1.0f;
        cachedDecimate    = -1;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MaxxBassAudioProcessor();
}
