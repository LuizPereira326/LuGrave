#pragma once
#include <JuceHeader.h>
#include "SimpleEngine.h"
#include "HybridEngine.h"
#include "AdvancedParams.h"

//==============================================================================
//  MaxxBassAudioProcessor - v50
//  ----------------------
//  Gerencia dois motores de processamento intercambiáveis:
//
//    SIMPLE MODE  — crossover + drive + harmônicos Chebyshev com pesos BASE/RANGE.
//    HYBRID MODE  — motor psicoacústico completo: pesos adaptativos,
//                   controle dinâmico multi-banda, kick-detector.
//
//  O parâmetro "simpleMode" (bool, APVTS) controla qual motor processa.
//==============================================================================
class MaxxBassAudioProcessor : public juce::AudioProcessor
{
public:
    MaxxBassAudioProcessor();
    ~MaxxBassAudioProcessor() override;

    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock    (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Bypass do host: áudio passa intacto, nada é processado
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()       const override { return true; }

    const juce::String getName()       const override { return JucePlugin_Name; }
    bool  acceptsMidi()   const override { return false; }
    bool  producesMidi()  const override { return false; }
    bool  isMidiEffect()  const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int)      override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int)   override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void refreshAdvancedParamsFromAPVTS();

    //==========================================================================
    // v50: Preset / Undo-Redo
    //==========================================================================
    static const juce::Identifier advancedParamsTreeId;

    void saveStateForUndo();
    void savePresetToFile   (const juce::File& file);
    void loadPresetFromFile (const juce::File& file);
    void resetToFactoryDefaults();

    // Callback para notificar a UI após load/reset de preset
    std::function<void()> onPresetLoaded;

    //==========================================================================
    // ATENÇÃO: undoManager deve ser declarado ANTES de apvts.
    // A inicialização inline de apvts usa &undoManager; a ordem de declaração
    // determina a ordem de construção dos membros em C++.
    //==========================================================================
    juce::UndoManager undoManager;

    juce::AudioProcessorValueTreeState apvts {
        *this, &undoManager, "Parameters", createParameterLayout()
    };

    // Meters (lidos pela UI via timer)
    std::atomic<float> inLevel   { 0 };
    std::atomic<float> outLevel  { 0 };
    std::atomic<float> harmMeter { 0 };

    AdvancedParams audioParams;

    double currentSR = 44100.0;
    BiquadFilter subProtectHPF  [2];  // 1º estágio HPF
    BiquadFilter subProtectHPF2 [2];  // 2º estágio HPF → 4ª ordem total, -24 dB/oct

    // Detector e memória do corte inteligente de subgrave
    BiquadFilter subProtectSubLPF  [2];
    BiquadFilter subProtectBodyLPF [2];
    EnvFollower  subProtectSubEnv  [2];
    EnvFollower  subProtectBodyEnv [2];

    float lastSubCutFreq = -1.0f;

    // v50: Suavização one-pole IIR (-1 = não inicializado)
    float smoothedTargetCut = -1.0f;

    // v50: Cache dos parâmetros do detector (evita setLowPass/setTimes todo bloco)
    float cachedSubLPFFreq   = -1.0f;
    float cachedBodyLPFFreq  = -1.0f;
    float cachedSubEnvAtt    = -1.0f;
    float cachedSubEnvRel    = -1.0f;
    float cachedBodyEnvAtt   = -1.0f;
    float cachedBodyEnvRel   = -1.0f;
    int   cachedDecimate     = -1;

private:
    SimpleHarmonicEngine simpleEngine;
    HybridHarmonicEngine hybridEngine;

    // v50: Snapshot capturado ANTES de cada operação undoável
    juce::ValueTree undoStateBefore;
    void commitUndoTransaction (const juce::String& actionName = "Parameter Change");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaxxBassAudioProcessor)
};
