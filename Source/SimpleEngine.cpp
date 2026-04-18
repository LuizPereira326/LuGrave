#include "SimpleEngine.h"

//==============================================================================
void SimpleHarmonicEngine::prepare (double sampleRate, int maxSamplesPerBlock, AdvancedParams& params)
{
    currentSR   = sampleRate;
    lastCutFreq = -1.0f;
    lastHPFFreq = -1.0f;
    lastLPFFreq = -1.0f;
    lastFixedLPFFreq = -1.0f;
    lastRampSec = -1.0f;
    lastAdaptiveThreshInit = -1.0f;
    lastVetoStrengthInit   = -1.0f;
    lastSplitFreq          = -1.0f;

    // CORREÇÃO v49: Removidas variáveis membro H2-H5 (código morto)
    // Os pesos harmônicos agora são calculados dinamicamente no processBlock()

    for (int i = 0; i < MAX_CH; ++i)
    {
        crossover[i].reset();

        harmHPF1 [i].reset();
        harmHPF2 [i].reset();
        harmLPF  [i].reset();
        harmLPF  [i].setLowPass (params.SE_HARM_LPF_FREQ, sampleRate);
        harmFixedLPF[i].reset();
        harmFixedLPF[i].setLowPass (params.SE_HARM_LPF_FREQ, sampleRate);

        // v52: anti-IM pre-filter — mesma lógica do HybridEngine
        harmPreLPF[i].reset();
        {
            const float fixedLPF = juce::jlimit (100.0f, 2000.0f, params.SE_HARM_LPF_FREQ);
            const float sourceFromFixed = juce::jmap (fixedLPF, 100.0f, 2000.0f, 22.0f, 360.0f);
            const float preLPFFreq = juce::jlimit (22.0f, 360.0f, sourceFromFixed);
            harmPreLPF[i].setLowPass ((double) preLPFFreq, sampleRate);
        }
        dcBlock  [i].reset();
        dcBlock  [i].setHighPass (params.SE_DC_BLOCK_FREQ, sampleRate);
        harmDCBlock[i].reset();
        harmDCBlock[i].setHighPass (params.SE_HARM_DC_BLOCK_FREQ, sampleRate);

        deepVocalProtector[i].prepare (sampleRate, 80.0f);
        dynHarmGain[i].prepare (sampleRate);
        dynHarmGain[i].reset();

        dynBassEnv[i].prepare (sampleRate, 5.0f, 100.0f);
        dynBassEnv[i].reset();

        // Fix SBS_SPLIT_FREQ: usar parâmetro em vez de 60.0f fixo
        subSplit[i].prepare (sampleRate, params.SBS_SPLIT_FREQ);
        subNorm[i].prepare (sampleRate);
        // Fix BL_*: configure com parâmetros reais
        bassLeaner[i].configure (sampleRate,
                                 params.BL_FAST_ATT_MS, params.BL_FAST_REL_MS,
                                 params.BL_SLOW_ATT_MS, params.BL_SLOW_REL_MS,
                                 params.BL_LEAN_SMOOTH_MS,
                                 params.BL_MAX_LEAN, params.BL_CREST_THRESH,
                                 params.BL_CREST_SCALE, params.BL_FULLNESS_SCALE);
        // Fix SN_*: configure com parâmetros reais
        subNorm[i].configure (sampleRate,
                               params.SN_ATT_MS, params.SN_REL_MS, params.SN_GAIN_SMOOTH_MS,
                               params.SN_TARGET_LEVEL, params.SN_MAX_BOOST, params.SN_MAX_REDUCE,
                               params.SN_BOOST_RATIO, params.SN_COMP_SLOPE, params.SN_LPF_FREQ);
        bassLeaner[i].prepare (sampleRate);

        lastSampleZCR[i] = 0.0f;
        zcrState[i]      = 0.0f;
        env[i]           = 0.0f;
        smoothVeto[i]    = 0.0f;

        intentDet[i].prepare (sampleRate);
        intentDet[i].reset();
        bandSplit[i].prepare (sampleRate);
        bandSplit[i].reset();
        subEnv[i].prepare (sampleRate, 5.0f, 100.0f);
        subEnv[i].reset();
        kickEnv[i].prepare (sampleRate, 2.0f, 40.0f);
        kickEnv[i].reset();
        smartKick[i].prepare (sampleRate);
        smartKick[i].reset();

        prevBlockEnvPeak[i] = 0.0f;
        prevBlockEnvAvg[i]  = 0.0f;
    }

    oversampler.initProcessing ((size_t) maxSamplesPerBlock);
    oversampler.reset();

    bassBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    cleanBassBuffer.setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    highBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    harmBuffer     .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    subBassBuffer  .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    bodyBassBuffer .setSize (MAX_CH, maxSamplesPerBlock, false, true, true);
    paramBuf       .setSize (5,      maxSamplesPerBlock, false, true, true);
    ctrlBuf        .setSize (MAX_CH * 5, maxSamplesPerBlock, false, true, true);

    const float rampSec = params.SE_RAMP_SEC;
    cutSmooth  .reset (sampleRate, rampSec);
    mixSmooth  .reset (sampleRate, rampSec);
    driveSmooth.reset (sampleRate, rampSec);
    gainSmooth .reset (sampleRate, rampSec);
    dynSmooth  .reset (sampleRate, rampSec);

    cutSmooth  .setCurrentAndTargetValue (80.0f);
    mixSmooth  .setCurrentAndTargetValue (0.65f);
    driveSmooth.setCurrentAndTargetValue (4.5f);
    gainSmooth .setCurrentAndTargetValue (1.0f);
    dynSmooth  .setCurrentAndTargetValue (0.5f);

    // v51: smoother para SBS_SPLIT_FREQ — rampa de 50 ms evita zipper noise
    splitFreqSmooth.reset (sampleRate, 0.05);
    splitFreqSmooth.setCurrentAndTargetValue (params.SBS_SPLIT_FREQ);

    adaptiveThreshold = params.SE_ADAPTIVE_THRESH_INIT;
    vetoStrength      = params.SE_VETO_STRENGTH_INIT;
    confidence        = 0.0f;
    avgVoiced         = 0.0f;
}

//==============================================================================
void SimpleHarmonicEngine::reset()
{
    for (int i = 0; i < MAX_CH; ++i)
    {
        crossover[i].reset();
        harmHPF1  [i].reset();
        harmHPF2  [i].reset();
        harmLPF  [i].reset();
        harmFixedLPF[i].reset();
        harmPreLPF[i].reset();
        dcBlock  [i].reset();
        harmDCBlock[i].reset();
        deepVocalProtector[i].reset();
        dynHarmGain[i].reset();
        dynBassEnv[i].reset();
        subSplit[i].reset();
        subNorm[i].reset();
        bassLeaner[i].reset();
        prevBlockEnvPeak[i] = 0.0f;
        prevBlockEnvAvg[i]  = 0.0f;
        lastSampleZCR[i] = 0.0f;
        zcrState[i]      = 0.0f;
        env[i]           = 0.0f;
        smoothVeto[i]    = 0.0f;

        intentDet[i].reset();
        bandSplit[i].reset();
        subEnv[i].reset();
        kickEnv[i].reset();
        smartKick[i].reset();
    }
    oversampler.reset();

    adaptiveThreshold = 0.6f;
    vetoStrength      = 0.95f;
    confidence        = 0.0f;
    avgVoiced         = 0.0f;
}

//==============================================================================
void SimpleHarmonicEngine::updateCrossover (float freq, AdvancedParams& params)
{
    if (std::abs (freq - lastCutFreq) >= 0.25f)
    {
        lastCutFreq = freq;
        for (int i = 0; i < MAX_CH; ++i)
            crossover[i].prepare ((double)freq, currentSR);
    }

    const float hpfMult1 = params.SE_HPF_MULT_1;
    const float hpfMult2 = params.SE_HPF_MULT_2;
    const float lpfMult  = params.SE_LPF_MULT;
    const float lpfMin   = params.SE_LPF_MIN;
    const float lpfMax   = params.SE_LPF_MAX;

    const float hpfFreq1 = juce::jmax (40.0f, freq * hpfMult1);
    const float hpfFreq2 = juce::jmax (50.0f, freq * hpfMult2);

    if (std::abs (hpfFreq1 - lastHPFFreq) >= 0.25f)
    {
        lastHPFFreq = hpfFreq1;
        for (int i = 0; i < MAX_CH; ++i)
        {
            harmHPF1[i].setHighPass ((double)hpfFreq1, currentSR);
            harmHPF2[i].setHighPass ((double)hpfFreq2, currentSR);
        }
    }

    const float lpfFreq = juce::jlimit (lpfMin, lpfMax, freq * lpfMult);
    if (std::abs (lpfFreq - lastLPFFreq) >= 0.5f)
    {
        lastLPFFreq = lpfFreq;
        for (int i = 0; i < MAX_CH; ++i)
            harmLPF[i].setLowPass ((double)lpfFreq, currentSR);
    }

    // v52: harmPreLPF anti-IM — corta conteúdo que vira chiado no Chebyshev
    // Frequência = min(derivada do LPF fixo, crossover*1.15) — espelha HybridEngine
    {
        const float fixedLPF = juce::jlimit (100.0f, 2000.0f, params.SE_HARM_LPF_FREQ);
        const float sourceFromFixed = juce::jmap (fixedLPF, 100.0f, 2000.0f, 22.0f, 360.0f);
        const float sourceFromCross = juce::jlimit (22.0f, 360.0f, freq * 1.15f);
        const float preLPFFreq = juce::jmin (sourceFromFixed, sourceFromCross);
        if (std::abs (preLPFFreq - lastPreLPFFreq) >= 0.5f)
        {
            lastPreLPFFreq = preLPFFreq;
            for (int i = 0; i < MAX_CH; ++i)
                harmPreLPF[i].setLowPass ((double) preLPFFreq, currentSR);
        }
    }
}

//==============================================================================
// CORREÇÃO v48: processBlock recebe AdvancedParams por referência
//==============================================================================
void SimpleHarmonicEngine::processBlock (juce::AudioBuffer<float>& buffer,
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

    // Cache de parâmetros suavizados
    cutSmooth  .setTargetValue (apvts.getRawParameterValue("cutFreq")->load());
    mixSmooth  .setTargetValue (apvts.getRawParameterValue("harmMix")->load() * 0.01f);
    driveSmooth.setTargetValue (apvts.getRawParameterValue("drive")->load());
    gainSmooth .setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue("outputGain")->load()));
    dynSmooth  .setTargetValue (apvts.getRawParameterValue("dynResp")->load() * 0.01f);

    // CORREÇÃO v48: Ler harmChar do APVTS
    const float harmChar = apvts.getRawParameterValue("harmChar")->load() * 0.01f;

    // OTIMIZAÇÃO: Pré-calcular valores suavizados para o bloco
    const float cutVal  = cutSmooth.getCurrentValue();
    const float mixVal  = mixSmooth.getCurrentValue();
    const float driveVal = driveSmooth.getCurrentValue();
    const float gainVal  = gainSmooth.getCurrentValue();
    const float dynVal   = dynSmooth.getCurrentValue();
    
    // CORREÇÃO v48: Calcular pesos harmônicos baseado no harmChar
    // Fórmula: peso = BASE + harmChar * RANGE
    const float h2Weight = params.SE_H2_BASE + harmChar * params.SE_H2_RANGE;
    const float h3Weight = params.SE_H3_BASE + harmChar * params.SE_H3_RANGE;
    const float h4Weight = params.SE_H4_BASE + harmChar * params.SE_H4_RANGE;
    const float h5Weight = params.SE_H5_BASE + harmChar * params.SE_H5_RANGE;
    const AdvancedParams defaultParams;
    const float chebyT2Scale = ChebyshevGen::T2_SCALE * (params.CH_T2_SCALE / defaultParams.CH_T2_SCALE);
    const float chebyT3Clamp = ChebyshevGen::T3_LIMIT * (params.CH_T3_CLAMP / defaultParams.CH_T3_CLAMP);
    const float chebyT4Clamp = ChebyshevGen::T4_LIMIT * (params.CH_T4_CLAMP / defaultParams.CH_T4_CLAMP);
    const float chebyT5Scale = ChebyshevGen::T5_SCALE * (params.CH_T5_SCALE / defaultParams.CH_T5_SCALE);
    const float chebyT5Clamp = ChebyshevGen::T5_LIMIT * (params.CH_T5_CLAMP / defaultParams.CH_T5_CLAMP);
    const float chebyPreClamp = 0.95f * (params.CH_PRE_CLAMP / defaultParams.CH_PRE_CLAMP);
    const float chebyOutClamp4 = 0.80f * (params.CH_OUTPUT_CLAMP_4H / defaultParams.CH_OUTPUT_CLAMP_4H);
    const float chebyOutClamp5 = 0.80f * (params.CH_OUTPUT_CLAMP_5H / defaultParams.CH_OUTPUT_CLAMP_5H);

    // Avançar smoothed values
    for (int i = 0; i < N; ++i)
    {
        cutSmooth.getNextValue();
        mixSmooth.getNextValue();
        driveSmooth.getNextValue();
        gainSmooth.getNextValue();
        dynSmooth.getNextValue();
    }

    // Reset para próximos valores
    cutSmooth  .setCurrentAndTargetValue (cutSmooth.getTargetValue());
    mixSmooth  .setCurrentAndTargetValue (mixSmooth.getTargetValue());
    driveSmooth.setCurrentAndTargetValue (driveSmooth.getTargetValue());
    gainSmooth .setCurrentAndTargetValue (gainSmooth.getTargetValue());
    dynSmooth  .setCurrentAndTargetValue (dynSmooth.getTargetValue());

    float inPeak = 0.0f, outPeak = 0.0f, hPeak = 0.0f;

    // CORREÇÃO v48: Usar params passado por referência (thread-safe)
    for (int ch = 0; ch < numCh; ++ch)
    {
        dynHarmGain[ch].setTimes (currentSR, params.DHG_ATT_MS, params.DHG_REL_MS);
        dcBlock[ch].setHighPass (params.SE_DC_BLOCK_FREQ, currentSR);
        harmDCBlock[ch].setHighPass (params.SE_HARM_DC_BLOCK_FREQ, currentSR);
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

    // CORREÇÃO v50: SE_HARM_LPF_FREQ agora tem efeito real via harmFixedLPF separado
    if (std::abs (params.SE_HARM_LPF_FREQ - lastFixedLPFFreq) >= 1.0f)
    {
        lastFixedLPFFreq = params.SE_HARM_LPF_FREQ;
        for (int ch = 0; ch < numCh; ++ch)
            harmFixedLPF[ch].setLowPass ((double)params.SE_HARM_LPF_FREQ, currentSR);
    }

    // v51: SBS_SPLIT_FREQ com ramp de 50 ms — evita zipper noise e cliques
    // ao automatizar ou mexer no parâmetro em tempo real.
    // skip(N) avança o smoother pelo tamanho do bloco sem processar amostra a amostra.
    splitFreqSmooth.setTargetValue (params.SBS_SPLIT_FREQ);
    splitFreqSmooth.skip (N);
    {
        const float smoothedSplit = splitFreqSmooth.getCurrentValue();
        if (std::abs (smoothedSplit - lastSplitFreq) >= 0.1f)
        {
            lastSplitFreq = smoothedSplit;
            for (int ch = 0; ch < numCh; ++ch)
                subSplit[ch].setSplitFreq (smoothedSplit, currentSR);
        }
    }

    // CORREÇÃO v50: SE_RAMP_SEC em tempo real — reseta smoothers se mudar
    if (std::abs (params.SE_RAMP_SEC - lastRampSec) >= 0.001f)
    {
        lastRampSec = params.SE_RAMP_SEC;
        cutSmooth  .reset (currentSR, params.SE_RAMP_SEC);
        mixSmooth  .reset (currentSR, params.SE_RAMP_SEC);
        driveSmooth.reset (currentSR, params.SE_RAMP_SEC);
        gainSmooth .reset (currentSR, params.SE_RAMP_SEC);
        dynSmooth  .reset (currentSR, params.SE_RAMP_SEC);
    }

    // CORREÇÃO v50: SE_ADAPTIVE_THRESH_INIT e SE_VETO_STRENGTH_INIT re-seed o estado
    // adaptativo quando o usuário os muda — assim têm efeito real como set-point
    if (std::abs (params.SE_ADAPTIVE_THRESH_INIT - lastAdaptiveThreshInit) >= 0.005f)
    {
        lastAdaptiveThreshInit = params.SE_ADAPTIVE_THRESH_INIT;
        adaptiveThreshold = params.SE_ADAPTIVE_THRESH_INIT;
    }
    if (std::abs (params.SE_VETO_STRENGTH_INIT - lastVetoStrengthInit) >= 0.005f)
    {
        lastVetoStrengthInit = params.SE_VETO_STRENGTH_INIT;
        vetoStrength = params.SE_VETO_STRENGTH_INIT;
    }

    updateCrossover (cutVal, params);

    // OTIMIZAÇÃO v45: Pré-calcular driveGain
    if (std::abs(driveVal - lastDrive) > 0.01f)
    {
        cachedDriveGain = ChebyshevGen::calcDriveGain(driveVal, params.CH_DRIVE_LOG_SCALE);
        lastDrive = driveVal;
    }

    float blockEnvPeak [MAX_CH] = { 0.0f };
    float blockEnvSum  [MAX_CH] = { 0.0f };

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src  = buffer.getReadPointer(ch);
        float* bass       = bassBuffer.getWritePointer(ch);
        float* high       = highBuffer.getWritePointer(ch);

        for (int i = 0; i < N; ++i)
        {
            const float x = src[i];
            const float absX = x < 0.0f ? -x : x;
            if (absX > inPeak) inPeak = absX;

            const float bassSignal = crossover[ch].bass (x);
            const float highSignal = crossover[ch].high (x);
            bass[i] = bassSignal;
            high[i] = highSignal;

            if (useDeepVocalProtection)
            {
                deepVocalProtector[ch].analyzeBass (bassSignal);
            }

            if (useDeepVocalProtection && std::abs(cutVal - lastCrossoverForPhase) > 1.0f)
            {
                deepVocalProtector[ch].phaseShifter.updateFrequencies (cutVal, currentSR);
                lastCrossoverForPhase = cutVal;
            }

            const float subBand  = bandSplit[ch].sub (bassSignal);
            const float kickBand = bandSplit[ch].kick (bassSignal);
            const float subLevel  = subEnv[ch].process (subBand);
            const float kickLevel = kickEnv[ch].process (kickBand);
            const float currentEnv = 0.99f * env[ch] + 0.01f * absX;
            env[ch] = currentEnv;

            intentDet[ch].process (bassSignal, subLevel, kickLevel, currentEnv);

            const float dynEnvelope = dynBassEnv[ch].process (bassSignal);

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
            const float dynCtrl = 1.0f + dynVal * (dynRaw - 1.0f);

            const int ctrlBase = ch * 5;
            ctrlBuf.getWritePointer(ctrlBase + 0)[i] = dynCtrl;
            ctrlBuf.getWritePointer(ctrlBase + 1)[i] = useDeepVocalProtection
                                                                  ? deepVocalProtector[ch].getTargetHarmGain() : 1.0f;
            ctrlBuf.getWritePointer(ctrlBase + 2)[i] = useDeepVocalProtection
                                                                  ? deepVocalProtector[ch].getTargetFreeGain() : 1.0f;
            ctrlBuf.getWritePointer(ctrlBase + 3)[i] = useDeepVocalProtection
                                                                  ? deepVocalProtector[ch].getTargetSubGain()  : 1.0f;
            ctrlBuf.getWritePointer(ctrlBase + 4)[i] = smartKick[ch].process (bassSignal);
        }

        prevBlockEnvPeak[ch] = blockEnvPeak[ch];
        prevBlockEnvAvg[ch]  = blockEnvSum[ch] / float(N);
    }

    for (int ch = numCh; ch < MAX_CH; ++ch)
        bassBuffer.clear (ch, 0, N);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = bassBuffer.getReadPointer(ch);
        float*       dst = cleanBassBuffer.getWritePointer(ch);
        std::copy (src, src + N, dst);
    }

    // v52: Pré-LPF anti-IM — filtrar antes do oversampling evita chiado de IM
    // nos polinômios Chebyshev quando o grave aparece (igual ao HybridEngine)
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* bassData = bassBuffer.getWritePointer(ch);
        for (int i = 0; i < N; ++i)
            bassData[i] = harmPreLPF[ch].process (bassData[i]);
    }

    // Geração de harmônicos OTIMIZADA
    {
        juce::dsp::AudioBlock<float> bassBlock (bassBuffer);
        auto upBlock = oversampler.processSamplesUp (bassBlock);

        const size_t upN = upBlock.getNumSamples();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* upData = upBlock.getChannelPointer ((size_t)ch);

            // v52: inline com soft-limiting igual ao HybridEngine
            // Doma picos de T3/T4/T5 no onset do grave sem perder corpo do H2
            for (size_t i = 0; i < upN; ++i)
            {
                const float xdPre = upData[i] * cachedDriveGain;
                const float xd = juce::jlimit (-chebyPreClamp, chebyPreClamp, xdPre);

                const float h2v = ChebyshevGen::T2 (xd, chebyT2Scale, chebyOutClamp4) * h2Weight;
                const float h3v = ChebyshevGen::T3 (xd, ChebyshevGen::T3_SCALE, chebyT3Clamp) * h3Weight;
                const float h4v = ChebyshevGen::T4 (xd, ChebyshevGen::T4_SCALE, chebyT4Clamp) * h4Weight;
                const float h5v = ChebyshevGen::T5 (xd, chebyT5Scale, chebyT5Clamp) * h5Weight;

                float harmSample = h2v + h3v + h4v + h5v;

                // Soft-limiting: reforça H2 como grave percebido e doma chiado de H3-H5
                const float oddRatio = (h3Weight + h4Weight + h5Weight)
                                       / juce::jmax (0.05f, h2Weight + h3Weight + h4Weight + h5Weight);
                harmSample /= (1.0f + std::abs (harmSample) * (0.18f + oddRatio * 0.55f));

                upData[i] = juce::jlimit (-chebyOutClamp5, chebyOutClamp5, harmSample);
            }
        }

        oversampler.processSamplesDown (bassBlock);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = bassBuffer.getReadPointer(ch);
            float* dst       = harmBuffer.getWritePointer(ch);
            std::copy (src, src + N, dst);
        }
    }

    // v52: Noise gate duplo — igual ao HybridEngine
    // Elimina chiado residual de T3/T4/T5 em sinais fracos
    if (inPeak < NOISE_GATE_THRESH)
    {
        for (int ch = 0; ch < numCh; ++ch)
            harmBuffer.clear (ch, 0, N);
    }
    else
    {
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

    float voicedScoreGlobal = 0.0f;
    const float CONF_HIGH = params.SE_CONF_HIGH;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* bass = cleanBassBuffer.getReadPointer(ch);
        const float* high = highBuffer.getReadPointer(ch);
        const float* harm = harmBuffer.getReadPointer(ch);
        float* out        = buffer.getWritePointer(ch);

        const float* ctrlDynGain  = ctrlBuf.getReadPointer(ch * 5 + 0);
        const float* ctrlHarmGain = ctrlBuf.getReadPointer(ch * 5 + 1);
        const float* ctrlFreeGain = ctrlBuf.getReadPointer(ch * 5 + 2);
        const float* ctrlSubGain  = ctrlBuf.getReadPointer(ch * 5 + 3);
        const float* ctrlKickDuck = ctrlBuf.getReadPointer(ch * 5 + 4);

        for (int i = 0; i < N; ++i)
        {
            const float x = bass[i];

            if (ch == 0)
            {
                const float prev  = lastSampleZCR[ch];
                const float delta = (x - prev);
                const float absDelta = delta < 0.0f ? -delta : delta;
                lastSampleZCR[ch] = x;

                const float zcr = (x * prev < 0.0f) ? 1.0f : 0.0f;
                zcrState[ch] = 0.995f * zcrState[ch] + 0.005f * zcr;
                const float zcrNorm = juce::jlimit (0.0f, 1.0f, zcrState[ch] * 10.0f);

                const float absX = x < 0.0f ? -x : x;
                float energyNorm = absX / (env[ch] + 1e-6f);
                energyNorm = juce::jlimit (0.0f, 1.0f, energyNorm * 2.0f);

                const float stability = 1.0f - juce::jlimit (0.0f, 1.0f, absDelta * 10.0f);

                voicedScoreGlobal = (1.0f - zcrNorm) * energyNorm * stability;

                avgVoiced = 0.995f * avgVoiced + 0.005f * voicedScoreGlobal;
                const float diff = (voicedScoreGlobal - avgVoiced);
                const float absDiff = diff < 0.0f ? -diff : diff;
                const float instantConfidence = 1.0f - juce::jlimit (0.0f, 1.0f, absDiff * 5.0f);
                confidence = 0.98f * confidence + 0.02f * instantConfidence;

                if (confidence > CONF_HIGH)
                {
                    adaptiveThreshold = 0.999f * adaptiveThreshold + 0.001f * (avgVoiced * 0.85f);
                    vetoStrength      = 0.999f * vetoStrength      + 0.001f * juce::jlimit (0.7f, 0.98f, avgVoiced);
                }

                if (!(voicedScoreGlobal > 0.9f && confidence > 0.8f))
                    vetoStrength = juce::jmin (vetoStrength, 0.9f);
            }

            float h = harmDCBlock[ch].process (harm[i]);

            float harmGainVal = 1.0f;
            if (useDeepVocalProtection)
            {
                if (deepVocalProtector[ch].needsProcessing())
                    h = deepVocalProtector[ch].processHarmonic (h);
            }
            else
            {
                h = harmHPF1[ch].process (h);
                h = harmHPF2[ch].process (h);
            }

            // CORREÇÃO v50: aplicar LPF fixo (SE_HARM_LPF_FREQ) e LPF variável de crossover
            h = harmFixedLPF[ch].process (h);
            h = harmLPF[ch].process (h);

            float dynGain = ctrlDynGain[i];

            if (useDeepVocalProtection)
            {
                const float hGain = ctrlHarmGain[i];
                const float freeG = ctrlFreeGain[i];

                if (hGain < 0.98f)
                {
                    const float compAmount = (1.0f - hGain) * (freeG - 1.0f);
                    dynGain += compAmount * dynVal;
                }
            }

            h *= dynGain;
            h *= ctrlKickDuck[i];

            const float absHarm = h < 0.0f ? -h : h;
            if (absHarm > MAX_EFFECTIVE)
            {
                h *= MAX_EFFECTIVE / absHarm;
            }

            // CORREÇÃO v50 / Bug #1: harmGainVal era calculado mas nunca aplicado
            if (!useDeepVocalProtection)
            {
                float targetVeto = 0.0f;
                if (voicedScoreGlobal > adaptiveThreshold)
                {
                    const float diff = voicedScoreGlobal - adaptiveThreshold;
                    targetVeto = diff * diff * 6.0f;
                }
                targetVeto = juce::jlimit (0.0f, 1.0f, targetVeto);

                smoothVeto[ch] = 0.97f * smoothVeto[ch] + 0.03f * targetVeto;

                harmGainVal = 1.0f - (vetoStrength * smoothVeto[ch] * 0.30f);

                if (confidence > 0.9f && voicedScoreGlobal > adaptiveThreshold + 0.15f)
                    harmGainVal = 0.85f;

                const float intentHarmGain = intentDet[ch].getHarmGain();
                const bool  voicedStrong   = intentDet[ch].isVoicedStrong();
                const bool  rumbleStrong   = intentDet[ch].isRumbleStrong();
                const bool  isKick         = intentDet[ch].getTransientScore() > 0.6f;

                if (isKick && !voicedStrong)
                {
                    harmGainVal = harmGainVal > 0.95f ? harmGainVal : 0.95f;
                }
                else if (voicedStrong || rumbleStrong)
                {
                    harmGainVal = rumbleStrong ? 0.84f : 0.91f;
                }
                else
                {
                    harmGainVal = harmGainVal < intentHarmGain ? harmGainVal : intentHarmGain;
                    harmGainVal = harmGainVal < 0.88f ? 0.88f : harmGainVal;
                }
            }

            // Bug #1 fix: aplicar o ganho calculado (estava sendo computado mas nunca multiplicado)
            h *= harmGainVal;

            const float absH = h < 0.0f ? -h : h;
            if (absH > hPeak) hPeak = absH;

            // v52: subSplit alimentado com bass[i] (cleanBassBuffer)
            // Nota: SimpleEngine tem apenas um dcBlock[ch] que e aplicado no output completo.
            // Para replicar o shapedBass do HybridEngine com isolamento total seria necessario
            // um segundo filtro (outputDCBlock). Por ora, passa o sinal direto para o subSplit
            // e mantem o dcBlock no output — o conteudo DC e minimo pois bass[i] vem do
            // LRCrossover LP que atenua DC naturalmente pelo estado do filtro.
            const float subPart  = subSplit[ch].getSub  (bass[i]);
            const float bodyPart = subSplit[ch].getBody (bass[i]);

            float subRetain, bodyRetain;
            SubBassSplitter::calcRetain (mixVal,
                                         params.SBS_SUB_MAX_ATTEN,  params.SBS_BODY_MAX_ATTEN,
                                         params.SBS_SUB_MIN_RETAIN, params.SBS_BODY_MIN_RETAIN,
                                         subRetain, bodyRetain);

            float subGain = ctrlSubGain[i];
            if (subGain < 0.90f) subGain = 0.90f;

            const float subNormGain = subNorm[ch].process (bass[i]);
            const float subGainFinal = subGain * subNormGain;

            const float leanFactor = bassLeaner[ch].process (x, mixVal);
            const float punchGain = 1.0f - (1.0f - ctrlKickDuck[i])
                                            * juce::jlimit (0.0f, 1.0f, params.DVP_PUNCH_REDUCTION_MAX);
            const float dryBass = (subPart * subRetain * subGainFinal)
                                + (bodyPart * bodyRetain * leanFactor * punchGain);

            const float loudComp = params.SE_LOUDNESS_COMP;
            const float loudnessComp = 1.0f + mixVal * loudComp;

            const float mixMult = params.SE_MIX_MULTIPLIER;
            float o = dryBass + high[i] + (h * mixVal * mixMult * loudnessComp);

            o = dcBlock[ch].process (o);
            o *= gainVal;

            o = AntiDistortionGuard::process (o,
                                              params.ADG_SAFE_LIMIT,
                                              params.ADG_HARD_LIMIT,
                                              params.ADG_SATURATION_START,
                                              params.ADG_SAT_OVER_NORM_FACTOR,
                                              params.ADG_LIMIT_FACTOR);

            out[i]  = o;
            const float absOut = o < 0.0f ? -o : o;
            if (absOut > outPeak) outPeak = absOut;
        }
    }

    // Medidores OTIMIZADOS
    const float meterDecay = OptimizedHelpers::fastMeterDecay (0.85f, static_cast<float>(N) / 256.0f);

    const float prevIn   = inLevelAtomic.load();
    const float prevOut  = outLevelAtomic.load();
    const float prevHarm = harmMeterAtomic.load();

    inLevelAtomic  .store (inPeak  > prevIn  * meterDecay ? inPeak  : prevIn  * meterDecay);
    outLevelAtomic .store (outPeak > prevOut * meterDecay ? outPeak : prevOut * meterDecay);
    harmMeterAtomic.store (hPeak   > prevHarm* meterDecay ? hPeak   : prevHarm* meterDecay);
}
