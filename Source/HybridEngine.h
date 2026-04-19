#pragma once
#include "DspStructs.h"
#include "AdvancedParams.h"

//==============================================================================
//  HybridHarmonicEngine - v48 CORRIGIDO
//
//  CORREÇÃO v48: processBlock agora recebe AdvancedParams por referência
//  diretamente da audio thread (audioParams, não advParams).
//
//  OTIMIZAÇÕES v47:
//  1. Time-slicing com sub-blocos de 64 samples
//  2. Análise vocal apenas a cada 4 samples (reduz 75%)
//  3. Cache de decisão vocal - não recalcular por sample
//  4. SKIP inteligente - pular filtros pesados quando desnecessário
//  5. Versão leve do processamento quando vocal não detectado
//==============================================================================
class HybridHarmonicEngine
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

    // Crossover
    LRCrossover   crossover [MAX_CH];

    // Band splitter para análise
    BandSplitter  bandSplit [MAX_CH];

    // SubBassSplitter
    SubBassSplitter subSplit [MAX_CH];

    // SubNormalizer
    SubNormalizer subNorm [MAX_CH];

    // BassLeaner
    BassLeaner bassLeaner [MAX_CH];

    // Filtros para harmônicos
    BiquadFilter  harmDCBlock   [MAX_CH];
    BiquadFilter  harmHPF1      [MAX_CH];
    BiquadFilter  harmHPF2      [MAX_CH];
    BiquadFilter  harmFixedLPF  [MAX_CH];
    BiquadFilter  harmLPF       [MAX_CH];
    BiquadFilter  dcBlock       [MAX_CH];
    BiquadFilter  outputDCBlock [MAX_CH];
    BiquadFilter  harmPreLPF    [MAX_CH];

    // Deep Vocal Protector
    DeepVocalProtector deepVocalProtector [MAX_CH];

    // Vocal processor legado
    VocalAwareHarmonicProcessor vocalProcessor [MAX_CH];

    // Envelope followers
    EnvFollower   bassEnv   [MAX_CH];
    EnvFollower   subEnv    [MAX_CH];
    SmartKickDucker smartKick [MAX_CH];

    // Dynamic harmonic gain
    DynHarmGain   dynHarmGain [MAX_CH];

    // Suavização do parâmetro dynResp
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dynSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> harmToneSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> splitFreqSmooth; // v51: evita zipper noise no SBS_SPLIT_FREQ

    // Lookahead
    float prevBlockEnvPeak [MAX_CH] = { 0.0f };
    float prevBlockEnvAvg  [MAX_CH] = { 0.0f };
    static constexpr float MAX_EFFECTIVE = 2.0f;
    static constexpr float NOISE_GATE_THRESH = 0.02f;

    // Oversampling
    juce::dsp::Oversampling<float> oversampler {
        (size_t) MAX_CH, 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR
    };

    // Buffers
    juce::AudioBuffer<float> subBassBuffer;
    juce::AudioBuffer<float> bodyBassBuffer;
    juce::AudioBuffer<float> bassBuffer;
    juce::AudioBuffer<float> cleanBassBuffer;
    juce::AudioBuffer<float> highBuffer;
    juce::AudioBuffer<float> harmBuffer;
    juce::AudioBuffer<float> paramBuf;
    juce::AudioBuffer<float> h2Buf;
    juce::AudioBuffer<float> h3Buf;
    juce::AudioBuffer<float> h4Buf;
    juce::AudioBuffer<float> h5Buf;
    juce::AudioBuffer<float> ctrlBuf;

    // Estado
    double currentSR   = 44100.0;
    float  lastCutFreq = -1.0f;
    float  lastHPFFreq = -1.0f;
    float  lastFixedLPFFreq = -1.0f;
    float  lastSplitFreq    = -1.0f;
    float  lastLPFFreq = -1.0f;
    float  lastDynRampSec = -1.0f;
    float  lastSmoothRampSec = -1.0f;

    // Modo de proteção vocal
    bool useDeepVocalProtection = true;
    float lastCrossoverForPhase [MAX_CH] = { -1.0f, -1.0f };

    // Envelope dedicado para dinâmica
    EnvFollower   dynBassEnv [MAX_CH];

    // OTIMIZAÇÃO v45: Cache do driveGain
    float cachedDriveGain = 1.0f;
    float lastDrive = -1.0f;

    // OTIMIZAÇÃO v47: Time-slicing
    int subBlockSize = 64;
    
    // OTIMIZAÇÃO v47: Cache de decisão vocal
    bool  cachedVocalDetected [MAX_CH] = { false };
    float cachedVocalScore    [MAX_CH] = { 0.0f };

    void updateSmoothing (AdvancedParams& params);
    void updateFilters (float crossoverFreq, AdvancedParams& params);
};
