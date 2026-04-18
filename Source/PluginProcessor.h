#pragma once
#include <JuceHeader.h>
#include "SimpleEngine.h"
#include "HybridEngine.h"
#include "AdvancedParams.h"

//==============================================================================
//  MaxxBassAudioProcessor - v49 CORRIGIDO
//  ----------------------
//  CORREÇÃO v49: Sincronização simplificada
//
//  Todos os parâmetros avançados agora estão registrados no APVTS.
//  O APVTS é a fonte única de verdade, sincronizado a cada bloco.
//  Isso elimina a necessidade de double-buffer e SpinLock.
//
//  Gerencia dois motores de processamento intercambiáveis:
//
//    SIMPLE MODE  — crossover + drive + harmônicos Chebyshev com pesos BASE/RANGE.
//                   Usa SE_H*_BASE e SE_H*__RANGE controlados pelo harmChar.
//
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

    // Sobrescreve o bypass do host: sem buffer.clear(), áudio passa intacto
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

    juce::AudioProcessorValueTreeState apvts {
        *this, nullptr, "Parameters", createParameterLayout()
    };

    // Meters (lidos pela UI via timer)
    std::atomic<float> inLevel   { 0 };
    std::atomic<float> outLevel  { 0 };
    std::atomic<float> harmMeter { 0 };

    //==========================================================================
    // CORREÇÃO v49: Parâmetros avançados sincronizados via APVTS
    //==========================================================================
    // Todos os parâmetros estão registrados no APVTS.
    // audioParams é atualizado a cada processBlock() via syncAdvancedParamsFromApvts().
    //==========================================================================
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
    
    // MELHORIA v49: Suavização do targetCut para evitar clicks/pop
    float smoothedTargetCut = -1.0f;  // Valor atual interpolado
    int   smoothCounter = 0;          // Contador para interpolação

private:
    SimpleHarmonicEngine simpleEngine;
    HybridHarmonicEngine hybridEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaxxBassAudioProcessor)
};
