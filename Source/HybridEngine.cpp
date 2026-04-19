#include "HybridEngine.h"

//==============================================================================
void HybridHarmonicEngine::prepare (double sampleRate, int maxSamplesPerBlock, AdvancedParams& params)
{
    currentSR   = sampleRate;
    lastCutFreq = -1.0f;
    lastHPFFreq = -1.0f;
    lastFixedLPFFreq = -1.0f;
    lastSplitFreq    = -1.0f;
    lastLPFFreq = -1.0f;
    lastDynRampSec = -1.0f;
    lastSmoothRampSec = -1.0f;
    for (int i = 0; i < MAX_CH; ++i)
        lastCrossoverForPhase[i] = -1.0f;

    // OTIMIZAÇÃO v47: Calcular tamanho de sub-bloco para time-slicing
    subBlockSize = juce::jmin(64, maxSamplesPerBlock);
    
    for (int i = 0; i < MAX_CH; ++i)
    {
        crossover[i].reset();
        bandSplit[i].prepare (sampleRate);
        bandSplit[i].reset();

        subSplit[i].prepare (sampleRate, params.SBS_SPLIT_FREQ);
        subNorm[i].prepare (sampleRate);
        subNorm[i].configure (sampleRate,
                               params.SN_ATT_MS, params.SN_REL_MS, params.SN_GAIN_SMOOTH_MS,
                               params.SN_TARGET_LEVEL, params.SN_MAX_BOOST, params.SN_MAX_REDUCE,
                               params.SN_BOOST_RATIO, params.SN_COMP_SLOPE, params.SN_LPF_FREQ);
        bassLeaner[i].configure (sampleRate,
                                 params.BL_FAST_ATT_MS, params.BL_FAST_REL_MS,
                                 params.BL_SLOW_ATT_MS, params.BL_SLOW_REL_MS,
                                 params.BL_LEAN_SMOOTH_MS,
                                 params.BL_MAX_LEAN, params.BL_CREST_THRESH,
                                 params.BL_CREST_SCALE, params.BL_FULLNESS_SCALE);

        harmDCBlock[i].reset();
        harmDCBlock[i].setHighPass (20.0, sampleRate);

        harmHPF1[i].reset();
        harmHPF2[i].reset();

        harmFixedLPF[i].reset();
        harmFixedLPF[i].setLowPass (params.HE_HARM_LPF_FREQ, sampleRate);

        harmLPF[i].reset();
        harmLPF[i].setLowPass (juce::jlimit (params.HE_LPF_MIN, params.HE_LPF_MAX,
                                             80.0f * params.HE_LPF_MULT), sampleRate);

        dcBlock[i].reset();
        dcBlock[i].setHighPass (params.HE_DC_BLOCK_FREQ, sampleRate);

        outputDCBlock[i].reset();
        outputDCBlock[i].setHighPass (10.0, sampleRate);

        harmPreLPF[i].reset();
        const float preSourceLPF = juce::jmap (juce::jlimit (100.0f, 2000.0f, params.HE_HARM_LPF_FREQ),
                                               100.0f, 2000.0f, 30.0f, 220.0f);
        harmPreLPF[i].setLowPass (preSourceLPF, sampleRate);

        deepVocalProtector[i].prepare (sampleRate, 80.0f);
        vocalProcessor[i].prepare (sampleRate, 80.0f);

        bassEnv[i].prepare (sampleRate, 5.0f, 100.0f);
        bassEnv[i].reset();
        subEnv[i].prepare (sampleRate, 5.0f, 100.0f);
        subEnv[i].reset();
        smartKick[i].prepare (sampleRate);
        smartKick[i].reset();

        dynHarmGain[i].prepare (sampleRate);
        dynHarmGain[i].reset();

        dynBassEnv[i].prepare (sampleRate, 5.0f, 100.0f);
        dynBassEnv[i].reset();

        prevBlockEnvPeak[i] = 0.0f;
        prevBlockEnvAvg[i]  = 0.0f;
        
        // OTIMIZAÇÃO v47: Cache de estado vocal
        cachedVocalDetected[i] = false;
        cachedVocalScore[i] = 0.0f;
    }

    const float dynRampSec = params.HE_DYN_RAMP_SEC;
    dynSmooth.reset (sampleRate, dynRampSec);
    dynSmooth.setCurrentAndTargetValue (0.50f);

    const float smoothRampSec = params.HE_SMOOTH_RAMP_SEC;
    harmToneSmooth.reset (sampleRate, smoothRampSec);
    harmToneSmooth.setCurrentAndTargetValue (1.0f);

    // v51: smoother para SBS_SPLIT_FREQ — rampa de 50 ms evita zipper noise
    splitFreqSmooth.reset (sampleRate, 0.05);
    splitFreqSmooth.setCurrentAndTargetValue (params.SBS_SPLIT_FREQ);

    oversampler.initProcessing ((size_t) maxSamplesPerBlock);
    oversampler.reset();

    bassBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    cleanBassBuffer.setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    highBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    harmBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    subBassBuffer  .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    bodyBassBuffer .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    paramBuf       .setSize (4,      maxSamplesPerBlock, false, true, true);

    h2Buf.setSize (1, maxSamplesPerBlock, false, true, true);
    h3Buf.setSize (1, maxSamplesPerBlock, false, true, true);
    h4Buf.setSize (1, maxSamplesPerBlock, false, true, true);
    h5Buf.setSize (1, maxSamplesPerBlock, false, true, true);

    ctrlBuf.setSize (MAX_CH * 5, maxSamplesPerBlock, false, true, true);
}

//==============================================================================
void HybridHarmonicEngine::reset()
{
    oversampler.reset();
    for (int i = 0; i < MAX_CH; ++i)
    {
        crossover[i].reset();
        bandSplit[i].reset();
        subSplit[i].reset();
        subNorm[i].reset();
        bassLeaner[i].reset();
        harmDCBlock[i].reset();
        harmHPF1[i].reset();
        harmHPF2[i].reset();
        harmFixedLPF[i].reset();
        harmLPF[i].reset();
        dcBlock[i].reset();
        outputDCBlock[i].reset();
        harmPreLPF[i].reset();
        deepVocalProtector[i].reset();
        vocalProcessor[i].reset();
        bassEnv[i].reset();
        subEnv[i].reset();
        smartKick[i].reset();
        dynHarmGain[i].reset();
        dynBassEnv[i].reset();
        prevBlockEnvPeak[i] = 0.0f;
        prevBlockEnvAvg[i]  = 0.0f;
        cachedVocalDetected[i] = false;
        cachedVocalScore[i] = 0.0f;
        lastCrossoverForPhase[i] = -1.0f;
    }
    dynSmooth.setCurrentAndTargetValue (0.50f);
    harmToneSmooth.setCurrentAndTargetValue (1.0f);
}

//==============================================================================
void HybridHarmonicEngine::updateSmoothing (AdvancedParams& params)
{
    const float dynRampSec = juce::jlimit (0.001f, 0.5f, params.HE_DYN_RAMP_SEC);
    if (std::abs (dynRampSec - lastDynRampSec) >= 0.0005f)
    {
        const float current = dynSmooth.getCurrentValue();
        const float target  = dynSmooth.getTargetValue();
        dynSmooth.reset (currentSR, dynRampSec);
        dynSmooth.setCurrentAndTargetValue (current);
        dynSmooth.setTargetValue (target);
        lastDynRampSec = dynRampSec;
    }

    const float smoothRampSec = juce::jlimit (0.001f, 0.5f, params.HE_SMOOTH_RAMP_SEC);
    if (std::abs (smoothRampSec - lastSmoothRampSec) >= 0.0005f)
    {
        const float current = harmToneSmooth.getCurrentValue();
        const float target  = harmToneSmooth.getTargetValue();
        harmToneSmooth.reset (currentSR, smoothRampSec);
        harmToneSmooth.setCurrentAndTargetValue (current);
        harmToneSmooth.setTargetValue (target);
        lastSmoothRampSec = smoothRampSec;
    }
}

//==============================================================================
void HybridHarmonicEngine::updateFilters (float crossoverFreq, AdvancedParams& params)
{
    const float fixedLPFFreq = juce::jlimit (100.0f, 2000.0f, params.HE_HARM_LPF_FREQ);
    if (std::abs (fixedLPFFreq - lastFixedLPFFreq) >= 1.0f)
    {
        lastFixedLPFFreq = fixedLPFFreq;
        for (int i = 0; i < MAX_CH; ++i)
            harmFixedLPF[i].setLowPass ((double) fixedLPFFreq, currentSR);
    }

    if (std::abs (crossoverFreq - lastCutFreq) >= 0.5f)
    {
        lastCutFreq = crossoverFreq;

        for (int i = 0; i < MAX_CH; ++i)
        {
            crossover[i].prepare ((double)crossoverFreq, currentSR);

            const float sourceFromFixed = juce::jmap (fixedLPFFreq, 100.0f, 2000.0f, 22.0f, 360.0f);
            const float sourceFromCross = juce::jlimit (22.0f, 360.0f, crossoverFreq * 1.15f);
            const float preLPFFreq = juce::jmin (sourceFromFixed, sourceFromCross);
            harmPreLPF[i].setLowPass ((double) preLPFFreq, currentSR);
        }
    }

    const float hpfMult1 = params.HE_HPF_MULT;
    const float hpfMult2 = params.HE_HPF_MULT + 0.5f;
    const float hpfFreq1 = juce::jmax (40.0f, crossoverFreq * hpfMult1);
    const float hpfFreq2 = juce::jmax (50.0f, crossoverFreq * hpfMult2);

    if (std::abs (hpfFreq1 - lastHPFFreq) >= 0.5f)
    {
        lastHPFFreq = hpfFreq1;
        for (int i = 0; i < MAX_CH; ++i)
        {
            harmHPF1[i].setHighPass ((double)hpfFreq1, currentSR);
            harmHPF2[i].setHighPass ((double)hpfFreq2, currentSR);
        }
    }

    const float lpfMultH = params.HE_LPF_MULT;
    const float lpfMinH  = params.HE_LPF_MIN;
    const float lpfMaxH  = params.HE_LPF_MAX;
    const float lpfFreq = juce::jlimit (lpfMinH, lpfMaxH, crossoverFreq * lpfMultH);
    if (std::abs (lpfFreq - lastLPFFreq) >= 1.0f)
    {
        lastLPFFreq = lpfFreq;
        for (int i = 0; i < MAX_CH; ++i)
            harmLPF[i].setLowPass ((double)lpfFreq, currentSR);
    }
}

//==============================================================================
// CORREÇÃO v48: processBlock recebe AdvancedParams por referência
//==============================================================================
void HybridHarmonicEngine::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::AudioProcessorValueTreeState& apvts,
                                          AdvancedParams& params,
                                          std::atomic<float>& inLevelAtomic,
                                          std::atomic<float>& harmMeterAtomic,
                                          std::atomic<float>& outLevelAtomic)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), MAX_CH);
    const int N     = buffer.getNumSamples();
    if (N <= 0 || numCh <= 0)
        return;

    // ========================================================================
    // OTIMIZAÇÃO v47: Cache de parâmetros no início do bloco
    // ========================================================================
    const float cutFreq = apvts.getRawParameterValue("cutFreq")->load();
    const float mix     = apvts.getRawParameterValue("harmMix")->load() * 0.01f;
    const float drive   = apvts.getRawParameterValue("drive")->load();
    const float gain    = juce::Decibels::decibelsToGain (
                           apvts.getRawParameterValue("outputGain")->load());
    const float harmChar = apvts.getRawParameterValue("harmChar")->load() * 0.01f;

    updateSmoothing (params);

    dynSmooth.setTargetValue (apvts.getRawParameterValue("dynResp")->load() * 0.01f);
    splitFreqSmooth.setTargetValue (params.SBS_SPLIT_FREQ);

    float* dynAmountData = paramBuf.getWritePointer (0);
    float* splitFreqData = paramBuf.getWritePointer (1);
    float* toneGainData  = paramBuf.getWritePointer (2);

    for (int i = 0; i < N; ++i)
    {
        dynAmountData[i] = dynSmooth.getNextValue();
        splitFreqData[i] = splitFreqSmooth.getNextValue();
    }

    // CORREÇÃO v50: configurar BL_* e SN_* em tempo real a cada bloco
    for (int ch = 0; ch < numCh; ++ch)
    {
        bassLeaner[ch].configure (currentSR,
                                  params.BL_FAST_ATT_MS, params.BL_FAST_REL_MS,
                                  params.BL_SLOW_ATT_MS, params.BL_SLOW_REL_MS,
                                  params.BL_LEAN_SMOOTH_MS,
                                  params.BL_MAX_LEAN, params.BL_CREST_THRESH,
                                  params.BL_CREST_SCALE, params.BL_FULLNESS_SCALE);
        subNorm[ch].configure (currentSR,
                                params.SN_ATT_MS, params.SN_REL_MS, params.SN_GAIN_SMOOTH_MS,
                                params.SN_TARGET_LEVEL, params.SN_MAX_BOOST, params.SN_MAX_REDUCE,
                                params.SN_BOOST_RATIO, params.SN_COMP_SLOPE, params.SN_LPF_FREQ);
    }

    // CORREÇÃO v48: Usar params passado por referência (thread-safe)
    for (int ch = 0; ch < numCh; ++ch)
    {
        dynHarmGain[ch].setTimes (currentSR, params.DHG_ATT_MS, params.DHG_REL_MS);
        dcBlock[ch].setHighPass (params.HE_DC_BLOCK_FREQ, currentSR);
        harmDCBlock[ch].setHighPass (params.HE_DC_BLOCK_FREQ, currentSR);
        outputDCBlock[ch].setHighPass (juce::jlimit (1.0f, 50.0f, params.HE_DC_BLOCK_FREQ), currentSR);
        deepVocalProtector[ch].configure (params.DVP_MIN_HARM_GAIN,
                                          params.DVP_MAX_NOTCH_DEPTH,
                                          params.DVP_PHASE_SHIFT_MAX,
                                          params.DVP_FREE_BAND_BOOST,
                                          params.DVP_VOCAL_PHASE_THRESH,
                                          params.DVP_NOTCH_VOCAL_THRESH,
                                          params.DVP_BANDED_THRESH,
                                          params.DVP_CONFLICT_REDUCTION_MAX,
                                          params.DVP_FREE_BOOST_MAX,
                                          params.DVP_PHASE_MIX_FACTOR,
                                          params.DVP_PHASE_FADE);
        smartKick[ch].configure (params.SKD_TRANSIENT_THRESH,
                                 juce::jmax (params.SKD_DUCK_AMOUNT, params.DVP_KICK_DUCK_AMOUNT),
                                 params.SKD_DUCK_DURATION,
                                 params.SKD_DUCK_COOLDOWN,
                                 params.SKD_BAND_CENTER,
                                 params.SKD_BAND_Q,
                                 params.SKD_ATTACK_HPF_FREQ,
                                 params.SKD_ATTACK_MS,
                                 params.SKD_RELEASE_MS);

        dynHarmGain[ch].setCurveParams (
            params.DHG_BASE_THRESH_LOW,
            params.DHG_BASE_THRESH_HIGH,
            params.DHG_BASE_BOOST,
            params.DHG_BASE_CUT,
            params.DHG_MIN_BOOST,
            params.DHG_MIN_CUT,
            params.DHG_CENTER,
            params.DHG_HALF_WIDTH_MIN,
            params.DHG_HALF_WIDTH_RANGE);
    }

    // Pré-calcular driveGain quando drive muda
    if (std::abs(drive - lastDrive) > 0.01f)
    {
        cachedDriveGain = ChebyshevGen::calcDriveGain(drive, params.CH_DRIVE_LOG_SCALE);
        lastDrive = drive;
    }

    // Pesos harmônicos (usando params thread-safe)
    const float h2b = params.HE_H2_BASE;
    const float h2r = params.HE_H2_RANGE;
    const float h3b = params.HE_H3_BASE;
    const float h3r = params.HE_H3_RANGE;
    const float h4b = params.HE_H4_BASE;
    const float h4r = params.HE_H4_RANGE;
    const float h5b = params.HE_H5_BASE;
    const float h5r = params.HE_H5_RANGE;

    float h2Weight = h2b + harmChar * h2r;
    float h3Weight = h3b + harmChar * h3r;
    float h4Weight = h4b + harmChar * h4r;
    float h5Weight = h5b + harmChar * h5r;

    const AdvancedParams defaultParams;
    const float defaultH2 = defaultParams.HE_H2_BASE + harmChar * defaultParams.HE_H2_RANGE;
    const float defaultH3 = defaultParams.HE_H3_BASE + harmChar * defaultParams.HE_H3_RANGE;
    const float defaultH4 = defaultParams.HE_H4_BASE + harmChar * defaultParams.HE_H4_RANGE;
    const float defaultH5 = defaultParams.HE_H5_BASE + harmChar * defaultParams.HE_H5_RANGE;
    const float defaultWeightAvg = (defaultH2 + defaultH3 + defaultH4 + defaultH5) * 0.25f;
    float currentWeightAvg = (h2Weight + h3Weight + h4Weight + h5Weight) * 0.25f;
    float harmonicWeightGain = juce::jlimit (0.20f, 3.00f,
                                             currentWeightAvg / juce::jmax (0.01f, defaultWeightAvg));
    float bassFocusGain = juce::jlimit (0.75f, 3.00f, h2Weight / juce::jmax (0.05f, defaultH2));
    float oddHarshness = juce::jlimit (0.0f, 1.0f,
                                       (h3Weight + h4Weight + h5Weight)
                                       / juce::jmax (0.05f, h2Weight + h3Weight + h4Weight + h5Weight));
    float tunedH2Weight = h2Weight * 1.90f;
    float tunedH3Weight = h3Weight * 0.75f;
    float tunedH4Weight = h4Weight * 0.38f;
    float tunedH5Weight = h5Weight * 0.18f;
    const float chebyT2Scale = ChebyshevGen::T2_SCALE * (params.CH_T2_SCALE / defaultParams.CH_T2_SCALE);
    const float chebyT3Clamp = ChebyshevGen::T3_LIMIT * (params.CH_T3_CLAMP / defaultParams.CH_T3_CLAMP);
    const float chebyT4Clamp = ChebyshevGen::T4_LIMIT * (params.CH_T4_CLAMP / defaultParams.CH_T4_CLAMP);
    const float chebyT5Scale = ChebyshevGen::T5_SCALE * (params.CH_T5_SCALE / defaultParams.CH_T5_SCALE);
    const float chebyT5Clamp = ChebyshevGen::T5_LIMIT * (params.CH_T5_CLAMP / defaultParams.CH_T5_CLAMP);
    const float chebyPreClamp = 0.92f * (params.CH_PRE_CLAMP / defaultParams.CH_PRE_CLAMP);
    const float chebyOutClamp4 = 0.80f * (params.CH_OUTPUT_CLAMP_4H / defaultParams.CH_OUTPUT_CLAMP_4H);
    const float chebyOutClamp5 = 0.95f * (params.CH_OUTPUT_CLAMP_5H / defaultParams.CH_OUTPUT_CLAMP_5H);
    const float spectralHPF = juce::jmax (20.0f, cutFreq * params.HE_HPF_MULT);
    const float spectralLPF = juce::jlimit (params.HE_LPF_MIN, params.HE_LPF_MAX, cutFreq * params.HE_LPF_MULT);
    const float spectralFixedLPF = juce::jlimit (100.0f, 2000.0f, params.HE_HARM_LPF_FREQ);
    const float spectralTop = juce::jmin (spectralLPF, spectralFixedLPF);
    const float spectralWindow = juce::jmax (25.0f, spectralTop - spectralHPF);
    const float spectralWindowGain = juce::jlimit (0.20f, 3.40f, spectralWindow / 120.0f);
    const float lowEndFocusGain = juce::jlimit (0.30f, 2.60f, 240.0f / juce::jmax (50.0f, spectralHPF));
    const float spectralTiltGain = juce::jlimit (0.35f, 2.40f,
                                                 juce::jmax (120.0f, spectralFixedLPF)
                                                 / juce::jmax (90.0f, spectralTop));
    float toneTargetGain = juce::jlimit (0.10f, 6.00f,
                                         harmonicWeightGain
                                         * bassFocusGain
                                         * spectralWindowGain
                                         * lowEndFocusGain
                                         * spectralTiltGain);

    float inPeak = 0.0f, outPeak = 0.0f, hPeak = 0.0f;

    updateFilters (cutFreq, params);

    if (useDeepVocalProtection)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (std::abs (cutFreq - lastCrossoverForPhase[ch]) > 1.0f)
            {
                deepVocalProtector[ch].phaseShifter.updateFrequencies (cutFreq, currentSR);
                lastCrossoverForPhase[ch] = cutFreq;
            }
        }
    }

    // Estatísticas de envelope
    float blockEnvPeak [MAX_CH] = { 0.0f };
    float blockEnvSum  [MAX_CH] = { 0.0f };

    // ========================================================================
    // Fase 1: Separar bandas e analisar (com sub-blocos)
    // ========================================================================
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src      = buffer.getReadPointer(ch);
        float*       bassData = bassBuffer.getWritePointer(ch);
        float*       highData = highBuffer.getWritePointer(ch);

        // OTIMIZAÇÃO v47: Processar em sub-blocos com "respiro"
        for (int subStart = 0; subStart < N; subStart += subBlockSize)
        {
            const int subEnd = juce::jmin(subStart + subBlockSize, N);
            
            for (int i = subStart; i < subEnd; ++i)
            {
                const float x = src[i];

                const float absX = x < 0.0f ? -x : x;
                if (absX > inPeak) inPeak = absX;

                const float bass = crossover[ch].bass(x);
                const float high = crossover[ch].high(x);
                bassData[i] = bass;
                highData[i] = high;

                // OTIMIZAÇÃO v47: Análise vocal apenas a cada 4 samples
                if (useDeepVocalProtection && (i & 3) == 0)
                {
                    deepVocalProtector[ch].analyzeBass (bass);
                }

                bassEnv[ch].process (bass);

                const float dynEnvelope = dynBassEnv[ch].process (bass);

                if (dynEnvelope > blockEnvPeak[ch]) blockEnvPeak[ch] = dynEnvelope;
                blockEnvSum[ch] += dynEnvelope;

                float envForDyn;
                if (i == 0 && prevBlockEnvPeak[ch] > 0.01f)
                {
                    envForDyn = prevBlockEnvPeak[ch] * 0.7f + prevBlockEnvAvg[ch] * 0.3f;
                }
                else
                {
                    envForDyn = dynEnvelope;
                }
                const float dynRaw  = dynHarmGain[ch].process (envForDyn, 1.0f);
                const float dynCtrl = 1.0f + dynAmountData[i] * (dynRaw - 1.0f);

                // Gravar sinais de controle
                const int ctrlBase = ch * 5;
                ctrlBuf.getWritePointer(ctrlBase + 0)[i] = dynCtrl;
                ctrlBuf.getWritePointer(ctrlBase + 1)[i] = useDeepVocalProtection
                                                          ? deepVocalProtector[ch].getTargetHarmGain() : 1.0f;
                ctrlBuf.getWritePointer(ctrlBase + 2)[i] = useDeepVocalProtection
                                                          ? deepVocalProtector[ch].getTargetFreeGain() : 1.0f;
                ctrlBuf.getWritePointer(ctrlBase + 3)[i] = useDeepVocalProtection
                                                          ? deepVocalProtector[ch].getTargetSubGain()  : 1.0f;
                ctrlBuf.getWritePointer(ctrlBase + 4)[i] = smartKick[ch].process (bass);
            }
        }

        prevBlockEnvPeak[ch] = blockEnvPeak[ch];
        prevBlockEnvAvg[ch]  = blockEnvSum[ch] / float(N);
        
        // OTIMIZAÇÃO v47: Cache do estado vocal para Fase 3
        cachedVocalDetected[ch] = deepVocalProtector[ch].isVocalDetected();
        cachedVocalScore[ch] = deepVocalProtector[ch].getVocalScore();
    }

    // ========================================================================
    // Atualizar pesos adaptativos depois da análise do bloco atual
    // ========================================================================
    if (useDeepVocalProtection)
    {
        bool anyVocalDetected = false;
        float maxVocalScore = 0.0f;
        int weightSourceCh = 0;

        for (int ch = 0; ch < numCh; ++ch)
        {
            if (deepVocalProtector[ch].isVocalDetected())
            {
                anyVocalDetected = true;
                const float score = deepVocalProtector[ch].getVocalScore();
                if (score > maxVocalScore)
                {
                    maxVocalScore = score;
                    weightSourceCh = ch;
                }
            }
        }

        if (anyVocalDetected)
        {
            deepVocalProtector[weightSourceCh].getAdaptiveWeights (
                h2Weight, h3Weight, h4Weight, h5Weight,
                h2Weight, h3Weight, h4Weight, h5Weight);
        }
    }

    currentWeightAvg = (h2Weight + h3Weight + h4Weight + h5Weight) * 0.25f;
    harmonicWeightGain = juce::jlimit (0.20f, 3.00f,
                                       currentWeightAvg / juce::jmax (0.01f, defaultWeightAvg));
    bassFocusGain = juce::jlimit (0.75f, 3.00f, h2Weight / juce::jmax (0.05f, defaultH2));
    oddHarshness = juce::jlimit (0.0f, 1.0f,
                                 (h3Weight + h4Weight + h5Weight)
                                 / juce::jmax (0.05f, h2Weight + h3Weight + h4Weight + h5Weight));
    tunedH2Weight = h2Weight * 1.90f;
    tunedH3Weight = h3Weight * 0.75f;
    tunedH4Weight = h4Weight * 0.38f;
    tunedH5Weight = h5Weight * 0.18f;

    toneTargetGain = juce::jlimit (0.10f, 6.00f,
                                   harmonicWeightGain
                                   * bassFocusGain
                                   * spectralWindowGain
                                   * lowEndFocusGain
                                   * spectralTiltGain);
    harmToneSmooth.setTargetValue (toneTargetGain);
    for (int i = 0; i < N; ++i)
        toneGainData[i] = harmToneSmooth.getNextValue();

    // Copiar baixo limpo
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = bassBuffer.getReadPointer(ch);
        float*       dst = cleanBassBuffer.getWritePointer(ch);
        std::copy (src, src + N, dst);
    }

    // Pré-LPF anti-IM (com sub-blocos)
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* bassData = bassBuffer.getWritePointer(ch);
        
        for (int subStart = 0; subStart < N; subStart += subBlockSize)
        {
            const int subEnd = juce::jmin(subStart + subBlockSize, N);
            for (int i = subStart; i < subEnd; ++i)
                bassData[i] = harmPreLPF[ch].process (bassData[i]);
        }
    }

    // ========================================================================
    // Fase 2: Geração de harmônicos com oversampling
    // ========================================================================
    {
        juce::dsp::AudioBlock<float> bassBlock (bassBuffer);
        auto upBlock = oversampler.processSamplesUp (bassBlock);
        const size_t upN = upBlock.getNumSamples();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* upData = upBlock.getChannelPointer ((size_t)ch);

            // OTIMIZAÇÃO v47: Sub-blocos no oversampling também
            const size_t subSize = static_cast<size_t>(subBlockSize) * 2;
            for (size_t subStart = 0; subStart < upN; subStart += subSize)
            {
                const size_t subEnd = juce::jmin(subStart + subSize, upN);
                
                for (size_t i = subStart; i < subEnd; ++i)
                {
                    const float xdPre = upData[i] * cachedDriveGain;
                    const float xd = juce::jlimit (-chebyPreClamp, chebyPreClamp, xdPre);

                    const float h2 = ChebyshevGen::T2 (xd, chebyT2Scale, chebyOutClamp4) * tunedH2Weight;
                    const float h3 = ChebyshevGen::T3 (xd, ChebyshevGen::T3_SCALE, chebyT3Clamp) * tunedH3Weight;
                    const float h4 = ChebyshevGen::T4 (xd, ChebyshevGen::T4_SCALE, chebyT4Clamp) * tunedH4Weight;
                    const float h5 = ChebyshevGen::T5 (xd, chebyT5Scale, chebyT5Clamp) * tunedH5Weight;

                    float harmSample = h2 + h3 + h4 + h5;

                    // Tira o "chiado" quando H3/H4/H5 sobem demais e reforça H2 como grave percebido.
                    harmSample *= bassFocusGain;
                    harmSample /= (1.0f + std::abs (harmSample) * (0.18f + oddHarshness * 0.55f));

                    upData[i] = juce::jlimit (-chebyOutClamp5, chebyOutClamp5, harmSample);
                }
            }
        }

        oversampler.processSamplesDown (bassBlock);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = bassBuffer.getReadPointer(ch);
            float*       dst = harmBuffer.getWritePointer(ch);
            std::copy (src, src + N, dst);
        }
    }

    // Noise gate duplo
    if (inPeak < NOISE_GATE_THRESH)
    {
        for (int ch = 0; ch < numCh; ++ch)
            harmBuffer.clear (ch, 0, N);
    }
    else
    {
        constexpr float HARM_NOISE_FLOOR = 5e-4f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* harmData = harmBuffer.getWritePointer(ch);
            for (int i = 0; i < N; ++i)
            {
                const float absHarm = harmData[i] < 0.0f ? -harmData[i] : harmData[i];
                if (absHarm < HARM_NOISE_FLOOR)
                    harmData[i] = 0.0f;
            }
        }
    }

    // ========================================================================
    // Fase 3: Processamento dos harmônicos (com sub-blocos)
    // ========================================================================
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* bassData = cleanBassBuffer.getReadPointer(ch);
        const float* highData = highBuffer.getReadPointer(ch);
        const float* harmData = harmBuffer.getReadPointer(ch);
        float*       outData  = buffer.getWritePointer(ch);

        const float* ctrlDynGain  = ctrlBuf.getReadPointer(ch * 5 + 0);
        const float* ctrlHarmGain = ctrlBuf.getReadPointer(ch * 5 + 1);
        const float* ctrlFreeGain = ctrlBuf.getReadPointer(ch * 5 + 2);
        const float* ctrlSubGain  = ctrlBuf.getReadPointer(ch * 5 + 3);
        const float* ctrlKickDuck = ctrlBuf.getReadPointer(ch * 5 + 4);

        // Processar em sub-blocos
        for (int subStart = 0; subStart < N; subStart += subBlockSize)
        {
            const int subEnd = juce::jmin(subStart + subBlockSize, N);
            
            for (int i = subStart; i < subEnd; ++i)
            {
                float harm = harmData[i];

                // DC blocker
                harm = harmDCBlock[ch].process (harm);
                harm = harmHPF1[ch].process (harm);
                harm = harmHPF2[ch].process (harm);

                // Proteção vocal/kick coerente com o estado real do DSP
                if (useDeepVocalProtection)
                {
                    if (deepVocalProtector[ch].needsProcessing())
                        harm = deepVocalProtector[ch].processHarmonic (harm);

                    harm = harmFixedLPF[ch].process (harm);
                    harm = harmLPF[ch].process (harm);
                }
                else
                {
                    harm = harmFixedLPF[ch].process (harm);
                    harm = harmLPF[ch].process (harm);
                    harm = vocalProcessor[ch].processSimple (harm);
                }

                // Dinâmica
                float dynGain = ctrlDynGain[i];

                if (useDeepVocalProtection)
                {
                    const float hGain = ctrlHarmGain[i];
                    const float freeG = ctrlFreeGain[i];

                    if (hGain < 0.98f)
                    {
                        const float compAmount = (1.0f - hGain) * (freeG - 1.0f);
                        dynGain += compAmount * dynAmountData[i];
                    }
                }

                harm *= dynGain;
                harm *= ctrlKickDuck[i];

                // Clamp de segurança
                float absHarm = harm < 0.0f ? -harm : harm;
                if (absHarm > MAX_EFFECTIVE)
                {
                    harm *= MAX_EFFECTIVE / absHarm;
                    absHarm = harm < 0.0f ? -harm : harm;
                }

                // Compressão de harmônicos
                harm = harm / (1.0f + absHarm * 0.45f);

                // Faz os pesos HE_H2/3/4/5 refletirem no nível final dos harmônicos.
                harm *= toneGainData[i];

                const float absHarmOut = harm < 0.0f ? -harm : harm;
                if (absHarmOut > hPeak) hPeak = absHarmOut;

                // ========================================================================
                // M7 v23: DRY BASS ATENUATION (MaxxBass Style)
                // ========================================================================
                const float bass = bassData[i];
                const float high = highData[i];

                const float shapedBass = dcBlock[ch].process (bass);
                subSplit[ch].setSplitFreq (splitFreqData[i], currentSR);
                const float subPart  = subSplit[ch].getSub  (shapedBass);
                const float bodyPart = subSplit[ch].getBody (shapedBass);

                float subRetain, bodyRetain;
                SubBassSplitter::calcRetain (mix,
                                             params.SBS_SUB_MAX_ATTEN,  params.SBS_BODY_MAX_ATTEN,
                                             params.SBS_SUB_MIN_RETAIN, params.SBS_BODY_MIN_RETAIN,
                                             subRetain, bodyRetain);

                float subGain = ctrlSubGain[i];
                if (subGain < 0.90f) subGain = 0.90f;

                const float subNormGain  = subNorm[ch].process (shapedBass);
                const float subGainFinal = subGain * subNormGain;

                const float leanFactor = bassLeaner[ch].process (shapedBass, mix);
                const float punchGain = 1.0f - (1.0f - ctrlKickDuck[i])
                                                * juce::jlimit (0.0f, 1.0f, params.DVP_PUNCH_REDUCTION_MAX);
                const float dryBass = subPart * subRetain * subGainFinal
                                    + bodyPart * bodyRetain * leanFactor * punchGain;

                // Compensação perceptual (usando params thread-safe)
                const float percBoost = params.HE_PERCEPTUAL_BOOST;
                const float perceptualBoost = 1.0f + percBoost * mix;
                const float filterPresenceBoost = juce::jlimit (0.20f, 3.50f,
                                                                toneTargetGain * 0.85f
                                                                + spectralWindowGain * 0.35f);

                // Mix final
                float out = dryBass + high + (harm * mix * perceptualBoost * filterPresenceBoost);

                // Compensação de loudness (usando params thread-safe)
                const float loudComp = params.HE_LOUDNESS_COMP;
                const float loudnessComp = 1.0f + mix * loudComp;
                out *= loudnessComp;

                // Output gain
                out *= gain;

                // Anti-distorcao parametrica
                out = AntiDistortionGuard::process (out,
                                                    params.ADG_SAFE_LIMIT,
                                                    params.ADG_HARD_LIMIT,
                                                    params.ADG_SATURATION_START,
                                                    params.ADG_SAT_OVER_NORM_FACTOR,
                                                    params.ADG_LIMIT_FACTOR);

                // DC blocker no output
                out = outputDCBlock[ch].process (out);

                // Soft clamp no output
                const float absOut = out < 0.0f ? -out : out;
                out = out / (1.0f + absOut * 0.1f);

                outData[i] = out;

                if (absOut > outPeak) outPeak = absOut;
            }
        }
    }

    // ========================================================================
    // Medidores
    // ========================================================================
    const float meterDecay = OptimizedHelpers::fastMeterDecay (0.85f, static_cast<float>(N) / 256.0f);

    const float prevIn   = inLevelAtomic.load();
    const float prevOut  = outLevelAtomic.load();
    const float prevHarm = harmMeterAtomic.load();

    inLevelAtomic  .store (inPeak  > prevIn  * meterDecay ? inPeak  : prevIn  * meterDecay);
    outLevelAtomic .store (outPeak > prevOut * meterDecay ? outPeak : prevOut * meterDecay);
    harmMeterAtomic.store (hPeak   > prevHarm* meterDecay ? hPeak   : prevHarm* meterDecay);
}
