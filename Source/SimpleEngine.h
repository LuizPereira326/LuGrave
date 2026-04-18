#pragma once
#include "DspStructs.h"
#include "AdvancedParams.h"

//==============================================================================
//  SimpleHarmonicEngine - v49 CORRIGIDO
//
//  CORREÇÃO v49:
//  - Removidas variáveis membro H2-H5 (código morto)
//  - Pesos harmônicos calculados dinamicamente no processBlock() usando
//    SE_H*_BASE e SE_H*_RANGE do AdvancedParams
//
//  OTIMIZAÇÕES v45:
//  1. Pré-cálculo de driveGain
//  2. Medidores otimizados
//  3. Cache de parâmetros
//==============================================================================
class SimpleHarmonicEngine
{
public:
    void prepare (double sampleRate, int maxSamplesPerBlock, AdvancedParams& params);
    void reset   ();

    // CORREÇÃO v48: Recebe AdvancedParams por referência (audioParams thread-safe)
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessorValueTreeState& apvts,
                       AdvancedParams& params,
                       std::atomic<float>& inLevelAtomic,
                       std::atomic<float>& harmMeterAtomic,
                       std::atomic<float>& outLevelAtomic);

private:
    static constexpr int MAX_CH = 2;

    LRCrossover   crossover [MAX_CH];
    SubBassSplitter subSplit [MAX_CH];
    SubNormalizer subNorm [MAX_CH];
    BassLeaner bassLeaner [MAX_CH];

    BiquadFilter  harmHPF1  [MAX_CH];
    BiquadFilter  harmHPF2  [MAX_CH];
    BiquadFilter  harmLPF   [MAX_CH];      // LPF variável (crossover-derivado)
    BiquadFilter  harmFixedLPF [MAX_CH];   // LPF fixo (SE_HARM_LPF_FREQ)
    BiquadFilter  harmPreLPF   [MAX_CH];   // v52: anti-IM pre-filter (igual ao HybridEngine)
    BiquadFilter  dcBlock   [MAX_CH];
    BiquadFilter  harmDCBlock[MAX_CH];

    DeepVocalProtector deepVocalProtector [MAX_CH];
    DynHarmGain   dynHarmGain [MAX_CH];
    EnvFollower   dynBassEnv [MAX_CH];

    float prevBlockEnvPeak [MAX_CH] = { 0.0f };
    float prevBlockEnvAvg  [MAX_CH] = { 0.0f };
    static constexpr float MAX_EFFECTIVE    = 2.0f;
    static constexpr float NOISE_GATE_THRESH = 0.02f;  // v52: gate global (herda HybridEngine)
    static constexpr float HARM_NOISE_FLOOR  = 5e-4f;  // v52: gate por amostra

    juce::dsp::Oversampling<float> oversampler {
        (size_t) MAX_CH, 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR
    };

    juce::AudioBuffer<float> subBassBuffer;
    juce::AudioBuffer<float> bodyBassBuffer;
    juce::AudioBuffer<float> bassBuffer;
    juce::AudioBuffer<float> cleanBassBuffer;
    juce::AudioBuffer<float> highBuffer;
    juce::AudioBuffer<float> harmBuffer;
    juce::AudioBuffer<float> paramBuf;
    juce::AudioBuffer<float> ctrlBuf;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> cutSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dynSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> splitFreqSmooth; // v51: evita zipper noise no SBS_SPLIT_FREQ

    double currentSR   = 44100.0;
    float  lastCutFreq = -1.0f;
    float  lastHPFFreq = -1.0f;
    float  lastLPFFreq = -1.0f;
    float  lastFixedLPFFreq = -1.0f;
    float  lastPreLPFFreq   = -1.0f;  // v52: cache para harmPreLPF
    float  lastSplitFreq    = -1.0f;

    // Cache para detectar mudança nos parâmetros init-only e re-seed o estado adaptativo
    float lastRampSec             = -1.0f;
    float lastAdaptiveThreshInit  = -1.0f;
    float lastVetoStrengthInit    = -1.0f;

    void updateCrossover (float freq, AdvancedParams& params);

    float adaptiveThreshold = 0.6f;
    float vetoStrength      = 0.95f;
    float confidence        = 0.0f;
    float avgVoiced         = 0.0f;

    float smoothVeto[MAX_CH] = { 0.0f };
    float lastSampleZCR[MAX_CH] = { 0.0f };
    float zcrState[MAX_CH]      = { 0.0f };
    float env[MAX_CH]           = { 0.0f };

    IntentDetector intentDet [MAX_CH];
    BandSplitter   bandSplit [MAX_CH];
    EnvFollower    subEnv    [MAX_CH];
    EnvFollower    kickEnv   [MAX_CH];
    SmartKickDucker smartKick [MAX_CH];

    bool useDeepVocalProtection = true;
    float lastCrossoverForPhase = -1.0f;

    // OTIMIZAÇÃO v45: Cache de driveGain
    float cachedDriveGain = 1.0f;
    float lastDrive = -1.0f;
};
