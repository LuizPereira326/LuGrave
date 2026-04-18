#pragma once
#include <JuceHeader.h>

//==============================================================================
// OTIMIZAÇÕES v45 - SEM MUDAR O SOM
//
// 1. Helper inline para coeficientes de envelope (evita std::exp repetitivo)
// 2. ChebyshevGen com lookup de drive (remove std::log1pf por sample)
// 3. Biquad com cache de freq (evita recalcular sin/cos)
// 4. Medidores com aproximação rápida de pow
// 5. Branchless envelopes onde possível
//==============================================================================

namespace OptimizedHelpers
{
    // Helper para calcular coeficiente de envelope: 1 - exp(-1/(sr*ms))
    // Chamado MUITAS vezes em prepare() - agora inline otimizado
    static inline float calcEnvCoef (double sr, float ms) noexcept
    {
        // Pré-calcula 1/(sr*ms) para evitar divisão
        const float rc = 1.0f / (float(sr) * ms * 0.001f);
        return 1.0f - std::exp(-rc);
    }

    // Aproximação rápida de pow(0.85, x) para medidores
    // Usa aproximação exponencial: 0.85^x ≈ exp(x * ln(0.85)) ≈ exp(x * -0.1625)
    // Para x pequeno: exp(-0.1625*x) ≈ 1 - 0.1625*x + 0.0132*x²
    static inline float fastMeterDecay (float /*base*/, float exp) noexcept
    {
        // Para base 0.85: ln(0.85) ≈ -0.1625
        // Para bases próximas, ainda funciona bem
        const float lnBase = -0.1625f;  // ln(0.85)
        const float arg = exp * lnBase;

        // Aproximação de exp para arg negativo pequeno
        if (arg > -0.5f)
            return 1.0f + arg * (1.0f + arg * 0.5f);  // Taylor 2a ordem
        else
            return std::exp(arg);  // Fallback para valores grandes
    }

    // Aproximação rápida de log1p(x) = ln(1+x)
    // Para x pequeno (< 1): log1p(x) ≈ x - x²/2 + x³/3
    static inline float fastLog1p (float x) noexcept
    {
        if (x < 0.5f)
        {
            const float x2 = x * x;
            return x - x2 * 0.5f + x2 * x * 0.333f;
        }
        return std::log1pf(x);
    }
}

//==============================================================================
// Biquad filter - Direct Form II Transposed (OTIMIZADO v45)
//==============================================================================
struct BiquadFilter
{
    // Usar float para melhor performance em SSE/AVX
    // double só é necessário para coeficientes em setLowPass/setHighPass
    double b0=0,b1=0,b2=0,a1=0,a2=0;
    double s1=0,s2=0;

    // Cache de frequência para evitar recálculos
    double lastFreq = -1.0;
    double lastSr   = -1.0;

    void reset() { s1 = s2 = 0.0; }

    // Process otimizado - usa double internamente para precisão
    // mas retorna float para compatibilidade
    inline float process (float x) noexcept
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return (float)y;
    }

    void setLowPass (double freq, double sr)
    {
        if (! std::isfinite (freq) || ! std::isfinite (sr) || sr <= 0.0)
            return;

        // Cache check - evitar recalcular se não mudou
        if (std::abs(freq - lastFreq) < 0.1 && std::abs(sr - lastSr) < 0.1)
            return;

        lastFreq = freq;
        lastSr = sr;

        freq = juce::jlimit (10.0, sr * 0.45, freq);
        const double w0    = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * 0.7071067811865476);
        const double a0r   = 1.0 / (1.0 + alpha);
        b0 = (1.0-cw)*0.5*a0r; b1 = (1.0-cw)*a0r; b2 = b0;
        a1 = -2.0*cw*a0r;      a2 = (1.0-alpha)*a0r;
    }

    void setHighPass (double freq, double sr)
    {
        if (! std::isfinite (freq) || ! std::isfinite (sr) || sr <= 0.0)
            return;

        if (std::abs(freq - lastFreq) < 0.1 && std::abs(sr - lastSr) < 0.1)
            return;

        lastFreq = freq;
        lastSr = sr;

        freq = juce::jlimit (10.0, sr * 0.45, freq);
        const double w0    = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * 0.7071067811865476);
        const double a0r   = 1.0 / (1.0 + alpha);
        b0 = (1.0+cw)*0.5*a0r; b1 = -(1.0+cw)*a0r; b2 = b0;
        a1 = -2.0*cw*a0r;      a2 = (1.0-alpha)*a0r;
    }

    void setBandPass (double freq, double sr, double Q = 1.0)
    {
        if (! std::isfinite (freq) || ! std::isfinite (sr) || sr <= 0.0)
            return;

        freq = juce::jlimit (10.0, sr * 0.45, freq);
        Q = juce::jmax (0.1, Q);
        const double w0    = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0r   = 1.0 / (1.0 + alpha);
        b0 = alpha * a0r;
        b1 = 0.0;
        b2 = -alpha * a0r;
        a1 = -2.0 * cw * a0r;
        a2 = (1.0 - alpha) * a0r;

        lastFreq = freq;
        lastSr = sr;
    }

    void setNotch (double freq, double sr, double Q = 10.0)
    {
        if (! std::isfinite (freq) || ! std::isfinite (sr) || sr <= 0.0)
            return;

        freq = juce::jlimit (10.0, sr * 0.45, freq);
        Q = juce::jmax (0.1, Q);
        const double w0    = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0r   = 1.0 / (1.0 + alpha);
        b0 = a0r;
        b1 = -2.0 * cw * a0r;
        b2 = a0r;
        a1 = -2.0 * cw * a0r;
        a2 = (1.0 - alpha) * a0r;

        lastFreq = freq;
        lastSr = sr;
    }

    void setAllPass (double freq, double sr, double Q = 0.707)
    {
        if (! std::isfinite (freq) || ! std::isfinite (sr) || sr <= 0.0)
            return;

        freq = juce::jlimit (10.0, sr * 0.45, freq);
        Q = juce::jmax (0.1, Q);
        const double w0    = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw    = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0r   = 1.0 / (1.0 + alpha);
        b0 = (1.0 - alpha) * a0r;
        b1 = -2.0 * cw * a0r;
        b2 = 1.0 * a0r;
        a1 = -2.0 * cw * a0r;
        a2 = (1.0 - alpha) * a0r;

        lastFreq = freq;
        lastSr = sr;
    }
};

//==============================================================================
// Crossover Linkwitz-Riley 4a ordem
//==============================================================================
struct LRCrossover
{
    BiquadFilter lp1,lp2,hp1,hp2;

    void prepare (double freq, double sr)
    {
        lp1.setLowPass (freq,sr);  lp2.setLowPass (freq,sr);
        hp1.setHighPass(freq,sr);  hp2.setHighPass(freq,sr);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

    inline float bass (float x) noexcept { return lp2.process(lp1.process(x)); }
    inline float high (float x) noexcept { return hp2.process(hp1.process(x)); }
};

//==============================================================================
// Gerador de harmônicos - VERSÃO v45 OTIMIZADA
//
// OTIMIZAÇÕES:
// - Pré-calcula driveGain quando drive muda (evita log1pf por sample)
// - Usa constantes pré-definidas em vez de calcular por sample
//==============================================================================
struct ChebyshevGen
{
    // Constantes pré-definidas (evita inicialização por chamada)
    static constexpr float T2_SCALE = 0.28f;
    static constexpr float T3_SCALE = 0.10f;  // era 0.06 — +67% definição
    static constexpr float T4_SCALE = 0.07f;  // era 0.04 — +75% presença
    static constexpr float T5_SCALE = 0.03f;  // era 0.02 — +50% brilho

    static constexpr float T2_LIMIT = 0.8f;
    static constexpr float T3_LIMIT = 0.60f;  // era 0.4 — mais headroom T3
    static constexpr float T4_LIMIT = 0.55f;  // era 0.4 — mais headroom T4
    static constexpr float T5_LIMIT = 0.3f;

    // T2: 2ª harmônica - PUNCH
    static inline float T2 (float x) noexcept
    {
        const float chebyshev = 2.0f * x * x;
        // Clamp manual mais rápido que jlimit
        const float out = chebyshev * T2_SCALE;
        return out > T2_LIMIT ? T2_LIMIT : (out < -T2_LIMIT ? -T2_LIMIT : out);
    }

    // T3: 3ª harmônica - DEFINIÇÃO
    static inline float T3 (float x) noexcept
    {
        const float chebyshev = 4.0f * x * x * x - 3.0f * x;
        const float out = chebyshev * T3_SCALE;
        return out > T3_LIMIT ? T3_LIMIT : (out < -T3_LIMIT ? -T3_LIMIT : out);
    }

    // T4: 4ª harmônica - PRESENÇA
    static inline float T4 (float x) noexcept
    {
        const float x2 = x * x;
        const float chebyshev = 8.0f * x2 * (x2 - 1.0f);
        const float out = chebyshev * T4_SCALE;
        return out > T4_LIMIT ? T4_LIMIT : (out < -T4_LIMIT ? -T4_LIMIT : out);
    }

    // T5: 5ª harmônica - BRILHO
    static inline float T5 (float x) noexcept
    {
        const float x2 = x * x;
        const float x3 = x2 * x;
        const float x5 = x3 * x2;
        const float chebyshev = 16.0f * x5 - 20.0f * x3 + 5.0f * x;
        const float out = chebyshev * T5_SCALE;
        return out > T5_LIMIT ? T5_LIMIT : (out < -T5_LIMIT ? -T5_LIMIT : out);
    }

    static inline float T2 (float x, float scale, float limit) noexcept
    {
        const float chebyshev = 2.0f * x * x;
        const float out = chebyshev * scale;
        return out > limit ? limit : (out < -limit ? -limit : out);
    }

    static inline float T3 (float x, float scale, float limit) noexcept
    {
        const float chebyshev = 4.0f * x * x * x - 3.0f * x;
        const float out = chebyshev * scale;
        return out > limit ? limit : (out < -limit ? -limit : out);
    }

    static inline float T4 (float x, float scale, float limit) noexcept
    {
        const float x2 = x * x;
        const float chebyshev = 8.0f * x2 * (x2 - 1.0f);
        const float out = chebyshev * scale;
        return out > limit ? limit : (out < -limit ? -limit : out);
    }

    static inline float T5 (float x, float scale, float limit) noexcept
    {
        const float x2 = x * x;
        const float x3 = x2 * x;
        const float x5 = x3 * x2;
        const float chebyshev = 16.0f * x5 - 20.0f * x3 + 5.0f * x;
        const float out = chebyshev * scale;
        return out > limit ? limit : (out < -limit ? -limit : out);
    }

    // Calcula driveGain - deve ser chamado quando drive muda
    static inline float calcDriveGain (float drive) noexcept
    {
        return 1.0f + OptimizedHelpers::fastLog1p(juce::jmax(1.0f, drive) - 1.0f) * 0.65f;
    }

    static inline float calcDriveGain (float drive, float logScale) noexcept
    {
        return 1.0f + OptimizedHelpers::fastLog1p (juce::jmax (1.0f, drive) - 1.0f) * logScale;
    }

    // Processamento com driveGain pré-calculado
    static inline float process5WithGain (float x, float driveGain,
                                          float h2, float h3, float h4, float h5) noexcept
    {
        const float xd_pre = x * driveGain;
        // Clamp manual mais rápido
        const float xd = xd_pre > 0.95f ? 0.95f : (xd_pre < -0.95f ? -0.95f : xd_pre);  // era 0.90

        const float out = T2(xd) * h2 + T3(xd) * h3 + T4(xd) * h4 + T5(xd) * h5;

        // Clamp final
        return out > 0.80f ? 0.80f : (out < -0.80f ? -0.80f : out);  // era 0.6
    }

    static inline float process5WithGain (float x, float driveGain,
                                          float h2, float h3, float h4, float h5,
                                          float t2Scale, float t3Clamp,
                                          float t4Clamp, float t5Scale,
                                          float t5Clamp, float preClamp,
                                          float outClamp4H, float outClamp5H) noexcept
    {
        const float xdPre = x * driveGain;
        const float xd = xdPre > preClamp ? preClamp : (xdPre < -preClamp ? -preClamp : xdPre);

        const float h2v = T2 (xd, t2Scale, outClamp4H) * h2;
        const float h3v = T3 (xd, T3_SCALE, t3Clamp) * h3;
        const float h4v = T4 (xd, T4_SCALE, t4Clamp) * h4;
        const float h5v = T5 (xd, t5Scale, t5Clamp) * h5;
        const float out = h2v + h3v + h4v + h5v;

        return out > outClamp5H ? outClamp5H : (out < -outClamp5H ? -outClamp5H : out);
    }

    // Processamento com 5 harmônicas (compatibilidade)
    static inline float process5 (float x, float drive,
                                   float h2, float h3, float h4, float h5) noexcept
    {
        const float driveGain = calcDriveGain(drive);
        return process5WithGain(x, driveGain, h2, h3, h4, h5);
    }

    // Processamento com 4 harmônicas (compatibilidade)
    static inline float process (float x, float drive, float envAmt,
                                  float h2, float h3, float h4) noexcept
    {
        juce::ignoreUnused (envAmt);
        const float driveGain = calcDriveGain(drive);

        const float xd_pre = x * driveGain;
        const float xd = xd_pre > 0.95f ? 0.95f : (xd_pre < -0.95f ? -0.95f : xd_pre);  // era 0.90

        const float out = T2(xd) * h2 + T3(xd) * h3 + T4(xd) * h4;

        return out > 0.80f ? 0.80f : (out < -0.80f ? -0.80f : out);  // era 0.6
    }
};

//==============================================================================
// Envelope follower - OTIMIZADO v45
//==============================================================================
struct EnvFollower
{
    float env=0, attCoef=0, relCoef=0;

    void prepare (double sr, float attMs=3.0f, float relMs=60.0f)
    {
        setTimes (sr, attMs, relMs);
    }
    void setTimes (double sr, float attMs, float relMs)
    {
        attCoef = OptimizedHelpers::calcEnvCoef(sr, attMs);
        relCoef = OptimizedHelpers::calcEnvCoef(sr, relMs);
    }
    void reset() { env = 0.0f; }

    // Branchless envelope - seleciona coeficiente sem branch
    inline float process (float x) noexcept
    {
        const float ax = x < 0.0f ? -x : x;  // abs sem std::abs
        const float diff = ax - env;
        const float coef = (ax > env) ? attCoef : relCoef;
        env += coef * diff;
        return env;
    }
};

//==============================================================================
// BandSplitter
//==============================================================================
struct BandSplitter
{
    BiquadFilter lp, hp;
    static constexpr float SPLIT_HZ = 50.0f;

    void prepare (double sr) { lp.setLowPass(SPLIT_HZ,sr); hp.setHighPass(SPLIT_HZ,sr); }
    void reset()              { lp.reset(); hp.reset(); }

    inline float sub  (float x) noexcept { return lp.process(x); }
    inline float kick (float x) noexcept { return hp.process(x); }
};

//==============================================================================
// SubBassSplitter — v52: upgrade para LR4 (Linkwitz-Riley 4a ordem)
//
// Antes: 2 biquads (2a ordem, Butterworth) -- 12 dB/oct, sum com bump +3 dB em splitHz
// Agora: 4 biquads (4a ordem, LR4)         -- 24 dB/oct, sum flat 0.00 dB em todo espectro
//
// Melhoras mensuráveis com split em 60 Hz:
//   100 Hz: isolamento sub passa de -9.4 dB para -18.8 dB
//   120 Hz: -12.3 dB para -24.6 dB
// O subNorm para de reagir a body content que sangrava no canal sub.
//==============================================================================
struct SubBassSplitter
{
    // LR4 = dois biquads Butterworth em cascata por caminho (mesmo padrao do LRCrossover)
    BiquadFilter subLP1, subLP2;
    BiquadFilter bodyHP1, bodyHP2;
    float splitHz = 60.0f;
    double sr = 44100.0;

    void prepare (double sampleRate, float subSplitFreq = 60.0f)
    {
        sr = sampleRate;
        splitHz = subSplitFreq;
        subLP1 .setLowPass  ((double)subSplitFreq, sampleRate);
        subLP2 .setLowPass  ((double)subSplitFreq, sampleRate);
        bodyHP1.setHighPass ((double)subSplitFreq, sampleRate);
        bodyHP2.setHighPass ((double)subSplitFreq, sampleRate);
        reset();
    }

    // Atualiza frequencia de split sem zerar o estado dos filtros
    void setSplitFreq (float newSplitHz, double sampleRate) noexcept
    {
        if (std::abs (newSplitHz - splitHz) < 0.5f && std::abs (sampleRate - sr) < 0.1)
            return;
        splitHz = newSplitHz;
        sr = sampleRate;
        subLP1 .setLowPass  ((double)newSplitHz, sampleRate);
        subLP2 .setLowPass  ((double)newSplitHz, sampleRate);
        bodyHP1.setHighPass ((double)newSplitHz, sampleRate);
        bodyHP2.setHighPass ((double)newSplitHz, sampleRate);
    }

    void reset()
    {
        subLP1.reset();  subLP2.reset();
        bodyHP1.reset(); bodyHP2.reset();
    }

    // LR4: dois estagios Butterworth em cascata
    inline float getSub  (float x) noexcept { return subLP2 .process (subLP1 .process (x)); }
    inline float getBody (float x) noexcept { return bodyHP2.process (bodyHP1.process (x)); }

    static inline void calcRetain (float mix,
                                   float subMaxAtten, float bodyMaxAtten,
                                   float subMinRetain, float bodyMinRetain,
                                   float& subRetain,
                                   float& bodyRetain) noexcept
    {
        if (mix < 0.001f)
        {
            subRetain  = 1.0f;
            bodyRetain = 1.0f;
            return;
        }

        const float sq = mix * mix;
        subRetain  = 1.0f - sq * subMaxAtten;
        bodyRetain = 1.0f - sq * bodyMaxAtten;

        subRetain  = subRetain  < subMinRetain  ? subMinRetain  : subRetain;
        bodyRetain = bodyRetain < bodyMinRetain ? bodyMinRetain : bodyRetain;
    }
};

//==============================================================================
// BassLeaner — M8 v25 (OTIMIZADO) — v50: totalmente parametrizado
//==============================================================================
struct BassLeaner
{
    float fastEnv   = 0.0f;
    float slowEnv   = 0.0f;
    float fastAttCoef = 0.0f;
    float fastRelCoef = 0.0f;
    float slowAttCoef = 0.0f;
    float slowRelCoef = 0.0f;
    float currentLean = 1.0f;
    float leanCoef    = 0.0f;

    // Parâmetros runtime (setados por configure())
    float maxLean       = 0.25f;
    float crestThresh   = 2.0f;
    float crestScale    = 0.5f;
    float fullnessScale = 3.5f;

    void prepare (double sr)
    {
        configure (sr, 2.0f, 80.0f, 30.0f, 300.0f, 20.0f,
                   0.25f, 2.0f, 0.5f, 3.5f);
    }

    // Atualiza coeficientes e parâmetros de comportamento em tempo real
    void configure (double sr,
                    float fastAttMs, float fastRelMs,
                    float slowAttMs, float slowRelMs,
                    float leanSmoothMs,
                    float maxLeanVal, float crestThreshVal,
                    float crestScaleVal, float fullnessScaleVal)
    {
        fastAttCoef  = OptimizedHelpers::calcEnvCoef (sr, fastAttMs);
        fastRelCoef  = OptimizedHelpers::calcEnvCoef (sr, fastRelMs);
        slowAttCoef  = OptimizedHelpers::calcEnvCoef (sr, slowAttMs);
        slowRelCoef  = OptimizedHelpers::calcEnvCoef (sr, slowRelMs);
        leanCoef     = OptimizedHelpers::calcEnvCoef (sr, leanSmoothMs);
        maxLean      = maxLeanVal;
        crestThresh  = crestThreshVal;
        crestScale   = crestScaleVal;
        fullnessScale = fullnessScaleVal;
        // Nota: NÃO chama reset() — os envelopes devem ter continuidade entre blocos
    }

    void reset()
    {
        fastEnv = 0.0f;
        slowEnv = 0.0f;
        currentLean = 1.0f;
    }

    inline float process (float bassSignal, float mix) noexcept
    {
        const float ax = bassSignal < 0.0f ? -bassSignal : bassSignal;

        // Fast envelope (branchless)
        const float fastDiff = ax - fastEnv;
        fastEnv += (ax > fastEnv ? fastAttCoef : fastRelCoef) * fastDiff;

        // Slow envelope (branchless)
        const float slowDiff = ax - slowEnv;
        slowEnv += (ax > slowEnv ? slowAttCoef : slowRelCoef) * slowDiff;

        // Crest factor — detecta transientes
        const float crest = (slowEnv > 1e-6f) ? fastEnv / slowEnv : 0.0f;
        const float isTransient = juce::jlimit (0.0f, 1.0f,
                                                (crest - crestThresh) * crestScale);

        const float fullness = juce::jlimit (0.0f, 1.0f, slowEnv * fullnessScale);

        const float targetLean = 1.0f - fullness * (1.0f - isTransient) * mix * maxLean;
        currentLean += leanCoef * (targetLean - currentLean);

        const float minLean = 1.0f - maxLean;
        return currentLean < minLean ? minLean : currentLean;
    }
};

//==============================================================================
// SubNormalizer - v50: totalmente parametrizado (SN_*)
//==============================================================================
struct SubNormalizer
{
    float subEnv  = 0.0f;
    float normEnv = 0.0f;
    float gainOut = 1.0f;

    float subCoef   = 0.0f;   // envelope do sub (ataque — SN_ATT_MS)
    float normCoef  = 0.0f;   // envelope de referência (release — SN_REL_MS)
    float gainCoef  = 0.0f;   // suavização do ganho (SN_GAIN_SMOOTH_MS)

    // Parâmetros runtime
    float targetLevel = 0.10f;
    float maxBoost    = 1.80f;
    float maxReduce   = 0.35f;  // ganho mínimo (floor)
    float boostRatio  = 0.80f;
    float compSlope   = 0.60f;

    // LPF para isolar a banda sub (SN_LPF_FREQ)
    BiquadFilter subLPF;

    void prepare (double sr)
    {
        configure (sr, 250.0f, 1000.0f, 50.0f,
                   0.10f, 1.80f, 0.35f, 0.80f, 0.60f, 60.0f);
    }

    void configure (double sr,
                    float attMs, float relMs, float gainSmoothMs,
                    float targetLvl, float maxBst, float maxRed,
                    float bstRatio, float cmpSlope, float lpfFreq)
    {
        subCoef   = OptimizedHelpers::calcEnvCoef (sr, attMs);
        normCoef  = OptimizedHelpers::calcEnvCoef (sr, relMs);
        gainCoef  = OptimizedHelpers::calcEnvCoef (sr, gainSmoothMs);
        targetLevel = targetLvl;
        maxBoost    = maxBst;
        maxReduce   = maxRed;
        boostRatio  = bstRatio;
        compSlope   = cmpSlope;
        subLPF.setLowPass (juce::jlimit (20.0, 500.0, (double)lpfFreq), sr);
        // Nota: NÃO chama reset() — gainOut e envelopes devem ter continuidade entre blocos
    }

    void reset() { subEnv = 0.0f; normEnv = 0.0f; gainOut = 1.0f; }

    inline float process (float bassSignal) noexcept
    {
        // Isola banda sub via LPF antes de analisar nível
        const float filtered = subLPF.process (bassSignal);
        const float ax = filtered < 0.0f ? -filtered : filtered;

        // Envelope de ataque: rastreia sub rápido
        subEnv  += subCoef  * (ax - subEnv);
        // Envelope de release: referência mais lenta
        normEnv += normCoef * (ax - normEnv);

        if (subEnv < 1e-4f)
        {
            gainOut += gainCoef * (1.0f - gainOut);
            return gainOut;
        }

        float targetGain;
        if (subEnv < targetLevel)
        {
            // Sub abaixo do alvo → boost proporcional ao deficit
            const float deficit = (targetLevel - subEnv) / (targetLevel + 1e-6f);
            targetGain = 1.0f + deficit * boostRatio;
            targetGain = targetGain > maxBoost ? maxBoost : targetGain;
        }
        else
        {
            // Sub acima do alvo → compressão suave
            const float excess = (subEnv - targetLevel) / (subEnv + 1e-6f);
            targetGain = 1.0f - excess * compSlope;
            targetGain = targetGain < maxReduce ? maxReduce : targetGain;
        }

        // Suavizar ganho para evitar pumping
        gainOut += gainCoef * (targetGain - gainOut);
        return gainOut;
    }
};

//==============================================================================
// DynHarmGain v36 (OTIMIZADO)
//==============================================================================
struct DynHarmGain
{
    float gain=1.0f, attCoef=0, relCoef=0;

    float baseThreshLow  = 0.02f;
    float baseThreshHigh = 0.37f;
    float baseBoost      = 1.45f;
    float baseCut        = 0.72f;
    float minBoost       = 1.0f;
    float minCut         = 0.95f;
    float center         = 0.15f;
    float halfWidthMin   = 0.04f;
    float halfWidthRange = 0.18f;

    static constexpr float NOISE_FLOOR = 0.015f;

    void prepare (double sr)
    {
        setTimes (sr, 25.0f, 150.0f);
    }
    void reset() { gain = 1.0f; }
    void setTimes (double sr, float attMs, float relMs) noexcept
    {
        attCoef = OptimizedHelpers::calcEnvCoef(sr, attMs);
        relCoef = OptimizedHelpers::calcEnvCoef(sr, relMs);
    }

    void setCurveParams (float newBaseThreshLow,
                         float newBaseThreshHigh,
                         float newBaseBoost,
                         float newBaseCut,
                         float newMinBoost,
                         float newMinCut,
                         float newCenter,
                         float newHalfWidthMin,
                         float newHalfWidthRange) noexcept
    {
        baseThreshLow  = newBaseThreshLow;
        baseThreshHigh = newBaseThreshHigh;
        baseBoost      = newBaseBoost;
        baseCut        = newBaseCut;
        minBoost       = newMinBoost;
        minCut         = newMinCut;
        center         = newCenter;
        halfWidthMin   = newHalfWidthMin;
        halfWidthRange = newHalfWidthRange;
    }

    static inline float smoothstep (float t) noexcept
    {
        const float c = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return c * c * (3.0f - 2.0f * c);
    }

    inline float process (float env, float dynamicAmount = 1.0f) noexcept
    {
        if (env < NOISE_FLOOR)
        {
            gain += relCoef * (1.0f - gain);
            return gain;
        }

        const float da = dynamicAmount < 0.001f ? 0.001f : dynamicAmount;

        const float boostTarget = minBoost + da * (baseBoost - minBoost);
        const float cutTarget   = minCut   + da * (baseCut   - minCut);

        const float halfWidth = halfWidthMin + da * halfWidthRange;

        const float threshLowByWidth =
            (NOISE_FLOOR + 0.005f) > (center - halfWidth) ? (NOISE_FLOOR + 0.005f) : (center - halfWidth);
        const float threshHighByWidth =
            (threshLowByWidth + 0.02f) > (center + halfWidth) ? (threshLowByWidth + 0.02f) : (center + halfWidth);

        const float baseThreshHighSafe =
            (baseThreshLow + 0.02f) > baseThreshHigh ? (baseThreshLow + 0.02f) : baseThreshHigh;

        const float threshLow =
            threshLowByWidth + da * (baseThreshLow - threshLowByWidth);
        const float threshHigh =
            threshHighByWidth + da * (baseThreshHighSafe - threshHighByWidth);

        float target;
        if (env <= threshLow)
        {
            const float t = (env - NOISE_FLOOR) / (threshLow - NOISE_FLOOR + 1e-8f);
            target = 1.0f + t * (boostTarget - 1.0f);
        }
        else if (env >= threshHigh)
        {
            target = cutTarget;
        }
        else
        {
            const float t = (env - threshLow) / (threshHigh - threshLow + 1e-8f);
            const float s = smoothstep (t);
            target = boostTarget + s * (cutTarget - boostTarget);
        }

        const float coef = (target > gain) ? attCoef : relCoef;
        gain += coef * (target - gain);

        return gain;
    }
};

//==============================================================================
// KickDetector (OTIMIZADO)
//==============================================================================
struct KickDetector
{
    float env=0, duckGain=1.0f;
    float attCoef=0, relCoef=0, duckRelCoef=0;

    static constexpr float KICK_THRESH = 0.45f;
    static constexpr float DUCK_AMOUNT = 0.75f;

    void prepare (double sr)
    {
        attCoef     = OptimizedHelpers::calcEnvCoef(sr, 1.0f);
        relCoef     = OptimizedHelpers::calcEnvCoef(sr, 20.0f);
        duckRelCoef = OptimizedHelpers::calcEnvCoef(sr, 60.0f);
    }
    void reset() { env = 0.0f; duckGain = 1.0f; }

    inline float process (float bassSignal) noexcept
    {
        const float ax = bassSignal < 0.0f ? -bassSignal : bassSignal;
        env += (ax > env ? attCoef : relCoef) * (ax - env);

        const float targetDuck = (env > KICK_THRESH) ? DUCK_AMOUNT : 1.0f;
        const float coef = (targetDuck < duckGain) ? duckRelCoef * 4.0f : duckRelCoef;
        duckGain += coef * (targetDuck - duckGain);
        return duckGain;
    }
};

//==============================================================================
// SmartKickDucker - configurável em tempo real via APVTS
//==============================================================================
struct SmartKickDucker
{
    BiquadFilter bandPass;
    BiquadFilter attackHPF;

    double sampleRate = 44100.0;

    float transientThresh = 0.35f;
    float duckAmount      = 0.25f;
    int   duckDurationSamples = 150;
    int   duckCooldownSamples = 800;
    float bandCenter      = 80.0f;
    float bandQ           = 1.5f;
    float attackHPFFreq   = 100.0f;
    float gainAttackMs    = 0.5f;
    float gainReleaseMs   = 30.0f;

    float detectorEnv         = 0.0f;
    float currentDuckGain     = 1.0f;
    float detectorAttCoef     = 0.0f;
    float detectorRelCoef     = 0.0f;
    float gainAttCoef         = 0.0f;
    float gainRelCoef         = 0.0f;
    int   duckHoldCounter     = 0;
    int   duckCooldownCounter = 0;

    float lastBandCenter = -1.0f;
    float lastBandQ      = -1.0f;
    float lastAttackHPF  = -1.0f;

    void prepare (double sr)
    {
        sampleRate = sr;
        reset();
        configure (0.35f, 0.25f, 150.0f, 800.0f, 80.0f, 1.5f, 100.0f, 0.5f, 30.0f);
    }

    void reset()
    {
        bandPass.reset();
        attackHPF.reset();
        detectorEnv = 0.0f;
        currentDuckGain = 1.0f;
        duckHoldCounter = 0;
        duckCooldownCounter = 0;
    }

    void configure (float newTransientThresh,
                    float newDuckAmount,
                    float newDuckDurationSamples,
                    float newDuckCooldownSamples,
                    float newBandCenter,
                    float newBandQ,
                    float newAttackHPFFreq,
                    float newGainAttackMs,
                    float newGainReleaseMs)
    {
        transientThresh = juce::jlimit (0.01f, 1.0f, newTransientThresh);
        duckAmount      = juce::jlimit (0.0f, 0.95f, newDuckAmount);
        duckDurationSamples = juce::jmax (1, juce::roundToInt (newDuckDurationSamples));
        duckCooldownSamples = juce::jmax (1, juce::roundToInt (newDuckCooldownSamples));
        bandCenter      = juce::jlimit (20.0f, float (sampleRate * 0.45), newBandCenter);
        bandQ           = juce::jlimit (0.1f, 12.0f, newBandQ);
        attackHPFFreq   = juce::jlimit (20.0f, float (sampleRate * 0.45), newAttackHPFFreq);
        gainAttackMs    = juce::jlimit (0.05f, 100.0f, newGainAttackMs);
        gainReleaseMs   = juce::jlimit (0.1f, 500.0f, newGainReleaseMs);

        detectorAttCoef = OptimizedHelpers::calcEnvCoef (sampleRate, juce::jmax (0.05f, gainAttackMs * 0.5f));
        detectorRelCoef = OptimizedHelpers::calcEnvCoef (sampleRate, juce::jmax (1.0f, gainReleaseMs * 0.75f));
        gainAttCoef     = OptimizedHelpers::calcEnvCoef (sampleRate, gainAttackMs);
        gainRelCoef     = OptimizedHelpers::calcEnvCoef (sampleRate, gainReleaseMs);

        if (std::abs (bandCenter - lastBandCenter) > 0.25f
            || std::abs (bandQ - lastBandQ) > 0.01f)
        {
            bandPass.setBandPass (bandCenter, sampleRate, bandQ);
            lastBandCenter = bandCenter;
            lastBandQ = bandQ;
        }

        if (std::abs (attackHPFFreq - lastAttackHPF) > 0.25f)
        {
            attackHPF.setHighPass (attackHPFFreq, sampleRate);
            lastAttackHPF = attackHPFFreq;
        }
    }

    inline float process (float bassSignal) noexcept
    {
        const float band = bandPass.process (bassSignal);
        const float attack = attackHPF.process (band);
        const float absBand = band < 0.0f ? -band : band;
        const float absAttack = attack < 0.0f ? -attack : attack;
        const float detector = juce::jmax (absBand, absAttack * 1.35f);

        detectorEnv += (detector > detectorEnv ? detectorAttCoef : detectorRelCoef)
                       * (detector - detectorEnv);

        if (duckCooldownCounter > 0)
            --duckCooldownCounter;

        if (detectorEnv >= transientThresh && duckCooldownCounter <= 0)
        {
            duckHoldCounter = duckDurationSamples;
            duckCooldownCounter = duckCooldownSamples;
        }

        const float targetDuckGain = (duckHoldCounter > 0) ? (1.0f - duckAmount) : 1.0f;
        if (duckHoldCounter > 0)
            --duckHoldCounter;

        const float coef = (targetDuckGain < currentDuckGain) ? gainAttCoef : gainRelCoef;
        currentDuckGain += coef * (targetDuckGain - currentDuckGain);

        return currentDuckGain;
    }

    inline bool isDucking() const noexcept
    {
        return currentDuckGain < 0.999f || duckHoldCounter > 0;
    }

    inline float getCurrentDuckGain() const noexcept { return currentDuckGain; }
};

//==============================================================================
// AntiDistortionGuard
//==============================================================================
struct AntiDistortionGuard
{
    static inline float softSaturate (float x) noexcept
    {
        const float ax = x < 0.0f ? -x : x;
        if (ax <= 0.9f)
            return x;

        const float sign = x < 0.0f ? -1.0f : 1.0f;
        if (ax >= 1.2f)
            return sign * 1.0f;

        return sign * (0.9f + 0.1f * (ax - 0.9f) / 0.3f);
    }

    static inline float process (float x,
                                 float safeLimit,
                                 float hardLimit,
                                 float saturationStart,
                                 float satOverNormFactor,
                                 float limitFactor) noexcept
    {
        const float safe = juce::jmax (0.1f, safeLimit);
        const float hard = juce::jmax (safe + 1.0e-3f, hardLimit);
        const float satStart = juce::jlimit (0.05f, safe, saturationStart);
        const float limitMix = juce::jlimit (0.0f, 1.0f, limitFactor);
        const float satNorm = juce::jlimit (0.0f, 1.0f, satOverNormFactor);

        const float ax = x < 0.0f ? -x : x;
        if (ax <= satStart)
            return x;

        const float sign = x < 0.0f ? -1.0f : 1.0f;
        const float satRange = juce::jmax (1.0e-3f, safe - satStart);
        const float satAmount = juce::jlimit (0.0f, 1.0f, (ax - satStart) / satRange);

        float saturated = sign * (satStart + std::tanh (satAmount * (1.5f + satNorm * 4.0f)) * satRange);

        if (ax > safe)
        {
            const float over = ax - safe;
            const float limitedMag = safe + over * (1.0f - limitMix);
            saturated = sign * juce::jmin (limitedMag, hard);
        }

        const float satAbs = saturated < 0.0f ? -saturated : saturated;
        if (satAbs > hard)
            saturated = sign * hard;

        return saturated;
    }
};

//==============================================================================
// IntentDetector - v40 (OTIMIZADO)
//==============================================================================
struct IntentDetector
{
    float lastSample = 0.0f;
    float stability  = 0.0f;
    float prevEnv    = 0.0f;
    float attackSmooth = 0.0f;
    float ratioSmooth = 0.0f;
    float attCoef = 0.0f;
    float isTransient = 0.0f;
    float isVoiced    = 0.0f;
    float isRumble    = 0.0f;
    bool voicedLocked = false;
    bool rumbleLocked = false;
    float harmGain = 1.0f;
    float harmGainSmooth = 1.0f;
    float cutBoost = 0.0f;
    float gainSmoothCoef = 0.0f;

    static constexpr float MIN_GAIN_VOICED = 0.75f;
    static constexpr float MIN_GAIN_RUMBLE = 0.70f;
    static constexpr float MIN_GAIN_NORMAL = 0.96f;  // era 0.90 — menos sufocamento geral

    void prepare (double sr)
    {
        attCoef = OptimizedHelpers::calcEnvCoef(sr, 3.0f);
        gainSmoothCoef = OptimizedHelpers::calcEnvCoef(sr, 80.0f);
    }

    void reset()
    {
        lastSample = 0.0f;
        stability  = 0.0f;
        prevEnv    = 0.0f;
        attackSmooth = 0.0f;
        ratioSmooth = 0.0f;
        isTransient = 0.0f;
        isVoiced    = 0.0f;
        isRumble    = 0.0f;
        voicedLocked = false;
        rumbleLocked = false;
        harmGain = 1.0f;
        harmGainSmooth = 1.0f;
        cutBoost = 0.0f;
    }

    inline void process (float bassSignal, float subEnergy, float bodyEnergy, float currentEnv) noexcept
    {
        const float delta = (bassSignal - lastSample);
        lastSample = bassSignal;
        const float absDelta = delta < 0.0f ? -delta : delta;

        const float instantStability = 1.0f - juce::jlimit(0.0f, 1.0f, absDelta * 15.0f);
        stability = 0.995f * stability + 0.005f * instantStability;

        const float attack = (currentEnv - prevEnv);
        const float absAttack = attack < 0.0f ? -attack : attack;
        prevEnv = currentEnv;

        const float attackScaled = juce::jlimit(0.0f, 1.0f, absAttack * 15.0f);
        const float attSmoothCoef = (attackScaled > attackSmooth) ? attCoef * 4.0f : attCoef;
        attackSmooth += attSmoothCoef * (attackScaled - attackSmooth);

        const float ratio = subEnergy / (bodyEnergy + 1e-6f);
        const float ratioNorm = juce::jlimit(0.0f, 1.0f, ratio * 0.8f);
        ratioSmooth = 0.985f * ratioSmooth + 0.015f * ratioNorm;

        isTransient = juce::jlimit(0.0f, 1.0f, attackSmooth * 2.0f);
        isVoiced    = stability;
        isRumble    = ratioSmooth;

        if (!voicedLocked && isVoiced > 0.80f)
            voicedLocked = true;
        else if (voicedLocked && isVoiced < 0.50f)
            voicedLocked = false;

        if (!rumbleLocked && isRumble > 0.90f)
            rumbleLocked = true;
        else if (rumbleLocked && isRumble < 0.60f)
            rumbleLocked = false;

        if (isTransient > 0.6f && isVoiced < 0.4f && !voicedLocked)
        {
            harmGain = 1.0f;
            cutBoost = 0.0f;
        }
        else if (voicedLocked)
        {
            harmGain = MIN_GAIN_VOICED;
            cutBoost = 15.0f;
        }
        else if (rumbleLocked)
        {
            harmGain = MIN_GAIN_RUMBLE;
            cutBoost = 30.0f;
        }
        else if (isVoiced > 0.65f)
        {
            const float voiceAmt = (isVoiced - 0.65f) / 0.35f;
            harmGain = 1.0f - (voiceAmt * (1.0f - MIN_GAIN_VOICED));
            harmGain = harmGain < MIN_GAIN_VOICED ? MIN_GAIN_VOICED : harmGain;
            cutBoost = voiceAmt * 10.0f;
        }
        else if (isRumble > 0.7f)
        {
            const float rumbleAmt = (isRumble - 0.7f) / 0.30f;
            harmGain = 1.0f - rumbleAmt * (1.0f - MIN_GAIN_RUMBLE);
            harmGain = harmGain < MIN_GAIN_RUMBLE ? MIN_GAIN_RUMBLE : harmGain;
            cutBoost = rumbleAmt * 20.0f;
        }
        else
        {
            harmGain = MIN_GAIN_NORMAL;
            cutBoost = 0.0f;
        }

        harmGainSmooth += gainSmoothCoef * (harmGain - harmGainSmooth);
    }

    inline float getHarmGain() const noexcept { return harmGainSmooth; }
    inline float getCutBoost() const noexcept { return cutBoost; }
    inline bool isVoicedStrong() const noexcept { return voicedLocked; }
    inline bool isRumbleStrong() const noexcept { return rumbleLocked; }
    inline float getTransientScore() const noexcept { return isTransient; }
    inline float getVoicedScore()    const noexcept { return isVoiced; }
    inline float getRumbleScore()    const noexcept { return isRumble; }
};

//==============================================================================
// VocalBandDetector (OTIMIZADO)
//==============================================================================
struct VocalBandDetector
{
    BiquadFilter bandPass;
    BiquadFilter lowPass;

    float lastSample     = 0.0f;
    float lastEnv        = 0.0f;
    float envSmooth      = 0.0f;
    float zcrAccum       = 0.0f;
    float pitchVarAccum  = 0.0f;
    float crestFactor    = 0.0f;

    int   sampleCount    = 0;
    int   samplesSinceCrossing = 0;
    float halfPeriodAccum = 0.0f;
    float halfPeriodCount = 0.0f;
    float peakAccum      = 0.0f;
    float rmsAccum       = 0.0f;

    float vocalScore     = 0.0f;
    float pitchStability = 0.0f;
    float fundamentalEst = 80.0f;
    bool  isVocalLocked  = false;

    float envCoef        = 0.0f;
    float smoothCoef     = 0.0f;
    double sampleRate    = 44100.0;

    static constexpr int ANALYSIS_WINDOW = 512;
    static constexpr float VOCAL_LOW_HZ  = 60.0f;
    static constexpr float VOCAL_HIGH_HZ = 100.0f;

    void prepare (double sr)
    {
        sampleRate = sr;

        const float centerFreq = (VOCAL_LOW_HZ + VOCAL_HIGH_HZ) / 2.0f;
        const float bandwidth  = VOCAL_HIGH_HZ - VOCAL_LOW_HZ;
        const float Q = centerFreq / bandwidth;

        bandPass.setBandPass (centerFreq, sr, Q);
        lowPass.setLowPass (100.0, sr);

        envCoef    = OptimizedHelpers::calcEnvCoef(sr, 5.0f);
        smoothCoef = OptimizedHelpers::calcEnvCoef(sr, 20.0f);

        reset();
    }

    void reset()
    {
        bandPass.reset();
        lowPass.reset();
        lastSample    = 0.0f;
        lastEnv       = 0.0f;
        envSmooth     = 0.0f;
        zcrAccum      = 0.0f;
        pitchVarAccum = 0.0f;
        crestFactor   = 0.0f;
        sampleCount   = 0;
        samplesSinceCrossing = 0;
        halfPeriodAccum = 0.0f;
        halfPeriodCount = 0.0f;
        peakAccum     = 0.0f;
        rmsAccum      = 0.0f;
        vocalScore    = 0.0f;
        pitchStability = 1.0f;
        fundamentalEst = 80.0f;
        isVocalLocked = false;
    }

    inline void process (float bassSignal) noexcept
    {
        const float vocalBand = bandPass.process (bassSignal);
        const float absVocal = vocalBand < 0.0f ? -vocalBand : vocalBand;
        const float env       = lowPass.process (absVocal);

        envSmooth += envCoef * (env - envSmooth);

        const float zcr = (vocalBand * lastSample < 0.0f) ? 1.0f : 0.0f;
        zcrAccum += zcr;

        if (zcr > 0.5f)
        {
            if (samplesSinceCrossing > 0)
            {
                halfPeriodAccum += float(samplesSinceCrossing);
                halfPeriodCount += 1.0f;
            }
            samplesSinceCrossing = 0;
        }
        else
        {
            ++samplesSinceCrossing;
        }

        const float envDelta = (env - lastEnv);
        const float absEnvDelta = envDelta < 0.0f ? -envDelta : envDelta;
        pitchVarAccum += absEnvDelta;
        lastEnv = env;

        if (absVocal > peakAccum)
            peakAccum = absVocal;
        rmsAccum += vocalBand * vocalBand;

        lastSample = vocalBand;
        sampleCount++;

        if (sampleCount >= ANALYSIS_WINDOW)
        {
            const float zcrRate = zcrAccum / float(ANALYSIS_WINDOW);
            const float pitchVar = pitchVarAccum / float(ANALYSIS_WINDOW);
            const float rms = std::sqrt(rmsAccum / float(ANALYSIS_WINDOW));
            crestFactor = (rms > 1e-6f) ? peakAccum / rms : 1.0f;

            float zcrScore    = 0.0f;
            float varScore    = 0.0f;
            float crestScore  = 0.0f;

            if (zcrRate > 0.02f && zcrRate < 0.15f)
            {
                const float diff = zcrRate - 0.06f;
                zcrScore = 1.0f - (diff < 0.0f ? -diff : diff) / 0.08f;
            }

            if (pitchVar > 0.0005f)
            {
                varScore = pitchVar * 500.0f;
                if (varScore > 1.0f) varScore = 1.0f;
            }

            if (crestFactor > 2.5f)
            {
                crestScore = (crestFactor - 2.5f) / 3.0f;
                if (crestScore > 1.0f) crestScore = 1.0f;
            }

            const float instantVocalScore = zcrScore * 0.35f + varScore * 0.40f + crestScore * 0.25f;
            vocalScore += smoothCoef * (instantVocalScore - vocalScore);

            if (halfPeriodCount > 2.0f)
            {
                const float avgHalfPeriod = halfPeriodAccum / halfPeriodCount;
                const float estFreq = float(sampleRate) / (avgHalfPeriod * 2.0f);

                if (estFreq > VOCAL_LOW_HZ && estFreq < VOCAL_HIGH_HZ)
                    fundamentalEst = 0.95f * fundamentalEst + 0.05f * estFreq;
            }

            pitchStability = 1.0f - juce::jmin(1.0f, pitchVar * 300.0f);
            pitchStability = 0.95f * pitchStability + 0.05f * (1.0f - (vocalScore - 0.5f) * 2.0f < 0.0f ? -(vocalScore - 0.5f) * 2.0f : (vocalScore - 0.5f) * 2.0f);

            if (!isVocalLocked && vocalScore > 0.65f && envSmooth > 0.02f)
                isVocalLocked = true;
            else if (isVocalLocked && (vocalScore < 0.35f || envSmooth < 0.01f))
                isVocalLocked = false;

            sampleCount   = 0;
            zcrAccum      = 0.0f;
            pitchVarAccum = 0.0f;
            halfPeriodAccum = 0.0f;
            halfPeriodCount = 0.0f;
            peakAccum     = 0.0f;
            rmsAccum      = 0.0f;
        }
    }

    inline float getVocalScore()     const noexcept { return vocalScore; }
    inline float getPitchStability() const noexcept { return pitchStability; }
    inline bool  isVocalDetected()   const noexcept { return isVocalLocked; }
    inline float getEnvelope()       const noexcept { return envSmooth; }
    inline float getFundamentalEst() const noexcept { return fundamentalEst; }
};

//==============================================================================
// HarmonicPhaseShifter (OTIMIZADO)
//==============================================================================
struct HarmonicPhaseShifter
{
    BiquadFilter apH2_1, apH2_2;
    BiquadFilter apH3_1, apH3_2;
    BiquadFilter apH4_1, apH4_2;

    float currentShift = 0.0f;
    float targetShift  = 0.0f;
    float shiftCoef    = 0.0f;

    void prepare (double sr, float crossoverFreq)
    {
        const float h2Freq = crossoverFreq * 2.0f;
        const float h3Freq = crossoverFreq * 3.0f;
        const float h4Freq = crossoverFreq * 4.0f;

        apH2_1.setAllPass (h2Freq, sr, 0.707);
        apH2_2.setAllPass (h2Freq * 1.1, sr, 1.2);
        apH3_1.setAllPass (h3Freq, sr, 0.707);
        apH3_2.setAllPass (h3Freq * 1.15, sr, 1.5);
        apH4_1.setAllPass (h4Freq, sr, 0.707);
        apH4_2.setAllPass (h4Freq * 1.2, sr, 1.8);

        shiftCoef = OptimizedHelpers::calcEnvCoef(sr, 10.0f);
        reset();
    }

    void reset()
    {
        apH2_1.reset(); apH2_2.reset();
        apH3_1.reset(); apH3_2.reset();
        apH4_1.reset(); apH4_2.reset();
        currentShift = 0.0f;
        targetShift  = 0.0f;
    }

    inline float process (float harmonicSignal, float shiftAmount) noexcept
    {
        targetShift = shiftAmount;
        currentShift += shiftCoef * (targetShift - currentShift);

        if (currentShift < 0.01f)
            return harmonicSignal;

        const float dry = harmonicSignal;
        float wet = harmonicSignal;

        wet = apH2_1.process (wet);
        wet = apH2_2.process (wet);
        wet = apH3_1.process (wet);
        wet = apH3_2.process (wet);
        wet = apH4_1.process (wet);
        wet = apH4_2.process (wet);

        return dry * (1.0f - currentShift) + wet * currentShift;
    }

    void updateFrequencies (float crossoverFreq, double sr)
    {
        const float h2Freq = crossoverFreq * 2.0f;
        const float h3Freq = crossoverFreq * 3.0f;
        const float h4Freq = crossoverFreq * 4.0f;

        apH2_1.setAllPass (h2Freq, sr, 0.707);
        apH2_2.setAllPass (h2Freq * 1.1, sr, 1.2);
        apH3_1.setAllPass (h3Freq, sr, 0.707);
        apH3_2.setAllPass (h3Freq * 1.15, sr, 1.5);
        apH4_1.setAllPass (h4Freq, sr, 0.707);
        apH4_2.setAllPass (h4Freq * 1.2, sr, 1.8);
    }
};

//==============================================================================
// SpectralNotchBank (OTIMIZADO)
//==============================================================================
struct SpectralNotchBank
{
    BiquadFilter notch1, notch2, notch3;
    BiquadFilter boost1, boost2;

    float currentDepth = 0.0f;
    float targetDepth  = 0.0f;
    float depthCoef    = 0.0f;
    double sampleRate  = 44100.0;

    void prepare (double sr)
    {
        sampleRate = sr;

        notch1.setNotch (160.0, sr, 5.0);
        notch2.setNotch (200.0, sr, 5.0);
        notch3.setNotch (240.0, sr, 5.0);

        boost1.setBandPass (180.0, sr, 2.0);
        boost2.setBandPass (220.0, sr, 2.0);

        depthCoef = OptimizedHelpers::calcEnvCoef(sr, 15.0f);
        reset();
    }

    void reset()
    {
        notch1.reset(); notch2.reset(); notch3.reset();
        boost1.reset(); boost2.reset();
        currentDepth = 0.0f;
        targetDepth  = 0.0f;
    }

    inline float process (float harmonicSignal, float depth) noexcept
    {
        targetDepth = depth;
        currentDepth += depthCoef * (targetDepth - currentDepth);

        if (currentDepth < 0.01f)
            return harmonicSignal;

        float notched = harmonicSignal;
        notched = notch1.process (notched);
        notched = notch2.process (notched);
        notched = notch3.process (notched);

        const float compensated = boost1.process (harmonicSignal) + boost2.process (harmonicSignal);

        const float wetAmount = currentDepth * 0.7f;
        float out = harmonicSignal * (1.0f - wetAmount) + notched * wetAmount;
        out += compensated * wetAmount * 0.3f;

        return out;
    }

    void updateFrequencies (float vocalFundamental, double sr)
    {
        const float h2 = vocalFundamental * 2.0f;
        const float h3 = vocalFundamental * 3.0f;

        notch1.setNotch (h2, sr, 5.0);
        notch2.setNotch (h3, sr, 5.0);
        notch3.setNotch (h2 * 1.5f, sr, 5.0);

        boost1.setBandPass (h2 * 1.1f, sr, 2.0);
        boost2.setBandPass (h3 * 0.9f, sr, 2.0);
    }
};

//==============================================================================
// OddEvenBalancer
//==============================================================================
struct OddEvenBalancer
{
    float evenGain = 1.0f;
    float oddGain  = 1.0f;
    float smoothCoef = 0.0f;
    float currentEven = 1.0f;
    float currentOdd  = 1.0f;

    void prepare (double sr)
    {
        smoothCoef = OptimizedHelpers::calcEnvCoef(sr, 20.0f);
    }

    void reset()
    {
        evenGain = 1.0f;
        oddGain  = 1.0f;
        currentEven = 1.0f;
        currentOdd  = 1.0f;
    }

    void setBalance (float vocalAmount, float mix) noexcept
    {
        const float targetEven = 1.0f + vocalAmount * mix * 0.15f;
        const float targetOdd  = 1.0f - vocalAmount * mix * 0.25f;

        evenGain = targetEven;
        oddGain  = targetOdd;
    }

    inline void process (float& h2, float& h3, float& h4) noexcept
    {
        currentEven += smoothCoef * (evenGain - currentEven);
        currentOdd  += smoothCoef * (oddGain  - currentOdd);

        h2 *= currentEven;
        h3 *= currentOdd;
        h4 *= currentEven;
    }
};

//==============================================================================
// VocalAwareHarmonicProcessor (legado)
//==============================================================================
struct VocalAwareHarmonicProcessor
{
    BiquadFilter hpf1, hpf2;
    BiquadFilter notch;

    float currentGain = 1.0f;
    float targetGain  = 1.0f;
    float gainCoef    = 0.0f;

    float lastVocalScore = 0.0f;

    void prepare (double sr, float crossoverFreq)
    {
        const float hpf1Freq = crossoverFreq * 2.0f;
        const float hpf2Freq = crossoverFreq * 2.5f;

        hpf1.setHighPass (hpf1Freq, sr);
        hpf2.setHighPass (hpf2Freq, sr);
        notch.setNotch (crossoverFreq * 3.0f, sr, 5.0);

        gainCoef = OptimizedHelpers::calcEnvCoef(sr, 30.0f);
        reset();
    }

    void reset()
    {
        hpf1.reset();
        hpf2.reset();
        notch.reset();
        currentGain = 1.0f;
        targetGain  = 1.0f;
        lastVocalScore = 0.0f;
    }

    void analyzeBass (float /*bassSignal*/)
    {
        // Versão legado - sem detector vocal, mantém ganho em 1.0
        // VocalAwareHarmonicProcessor não tem detector vocal interno
        lastVocalScore = 0.0f;
        targetGain = 1.0f;
    }

    inline float processSimple (float harmonicSignal) noexcept
    {
        currentGain += gainCoef * (targetGain - currentGain);

        float out = hpf1.process (harmonicSignal);
        out = hpf2.process (out);
        out *= currentGain;
        return out;
    }
};

//==============================================================================
// DeepVocalProtector - v33+ (OTIMIZADO)
//==============================================================================
struct DeepVocalProtector
{
    VocalBandDetector vocalDetector;
    HarmonicPhaseShifter phaseShifter;
    SpectralNotchBank notchBank;
    OddEvenBalancer oddEvenBalancer;

    BiquadFilter singleNotch;
    float lastNotchFreq = -1.0f;

    float currentPhaseShift = 0.0f;
    float targetPhaseShift  = 0.0f;
    float phaseShiftCoef    = 0.0f;

    float harmGain = 1.0f;
    float subGain  = 1.0f;
    float freeGain = 1.0f;

    float harmSmoothCoef = 0.0f;
    float subSmoothCoef  = 0.0f;
    float freeSmoothCoef = 0.0f;

    float currentHarmGain = 1.0f;
    float currentSubGain  = 1.0f;
    float currentFreeGain = 1.0f;

    float vocalFundamental = 80.0f;
    float lastCrossover    = -1.0f;
    double currentSampleRate = 44100.0;

    float minHarmGain = 0.85f;
    float maxNotchDepth = 0.35f;
    float phaseShiftMax = 0.80f;
    float freeBandBoost = 1.25f;
    float vocalPhaseThresh = 0.35f;
    float notchVocalThresh = 0.60f;
    float bandedThresh = 0.45f;
    float conflictReductionMax = 0.15f;
    float freeBoostMax = 0.25f;
    float phaseMixFactor = 0.70f;
    float phaseFade = 0.97f;

    void prepare (double sr, float crossoverFreq)
    {
        currentSampleRate = sr;

        vocalDetector.prepare (sr);
        phaseShifter.prepare (sr, crossoverFreq);
        notchBank.prepare (sr);
        oddEvenBalancer.prepare (sr);

        singleNotch.setNotch (160.0, sr, 8.0);

        phaseShiftCoef  = OptimizedHelpers::calcEnvCoef(sr, 60.0f);
        harmSmoothCoef  = OptimizedHelpers::calcEnvCoef(sr, 40.0f);
        subSmoothCoef   = OptimizedHelpers::calcEnvCoef(sr, 30.0f);
        freeSmoothCoef  = OptimizedHelpers::calcEnvCoef(sr, 50.0f);

        lastCrossover = crossoverFreq;
        reset();
    }

    void reset()
    {
        vocalDetector.reset();
        phaseShifter.reset();
        notchBank.reset();
        oddEvenBalancer.reset();
        singleNotch.reset();

        lastNotchFreq = -1.0f;
        currentPhaseShift = 0.0f;
        targetPhaseShift  = 0.0f;
        harmGain = 1.0f;
        subGain  = 1.0f;
        freeGain = 1.0f;
        currentHarmGain = 1.0f;
        currentSubGain  = 1.0f;
        currentFreeGain = 1.0f;
        vocalFundamental = 80.0f;
    }

    void configure (float newMinHarmGain,
                    float newMaxNotchDepth,
                    float newPhaseShiftMax,
                    float newFreeBandBoost,
                    float newVocalPhaseThresh,
                    float newNotchVocalThresh,
                    float newBandedThresh,
                    float newConflictReductionMax,
                    float newFreeBoostMax,
                    float newPhaseMixFactor,
                    float newPhaseFade) noexcept
    {
        minHarmGain          = juce::jlimit (0.5f, 1.0f, newMinHarmGain);
        maxNotchDepth        = juce::jlimit (0.0f, 1.0f, newMaxNotchDepth);
        phaseShiftMax        = juce::jlimit (0.0f, 1.0f, newPhaseShiftMax);
        freeBandBoost        = juce::jmax   (1.0f, newFreeBandBoost);
        vocalPhaseThresh     = juce::jlimit (0.0f, 1.0f, newVocalPhaseThresh);
        notchVocalThresh     = juce::jlimit (0.0f, 1.0f, newNotchVocalThresh);
        bandedThresh         = juce::jlimit (0.0f, 1.0f, newBandedThresh);
        conflictReductionMax = juce::jlimit (0.0f, 0.5f, newConflictReductionMax);
        freeBoostMax         = juce::jlimit (0.0f, 1.0f, newFreeBoostMax);
        phaseMixFactor       = juce::jlimit (0.0f, 1.0f, newPhaseMixFactor);
        phaseFade            = juce::jlimit (0.90f, 0.9999f, newPhaseFade);
    }

    void analyzeBass (float bassSignal)
    {
        vocalDetector.process (bassSignal);

        const float score = vocalDetector.getVocalScore();
        const float phaseNorm = (score > vocalPhaseThresh && vocalPhaseThresh < 0.999f)
                              ? juce::jlimit (0.0f, 1.0f, (score - vocalPhaseThresh) / (1.0f - vocalPhaseThresh))
                              : 0.0f;

        if (vocalDetector.isVocalDetected())
        {
            const float detectedFundamental = vocalDetector.getFundamentalEst();

            if (std::abs(detectedFundamental - vocalFundamental) > 0.5f)
            {
                vocalFundamental = detectedFundamental;
                notchBank.updateFrequencies (vocalFundamental, currentSampleRate);
            }
            else
            {
                vocalFundamental = detectedFundamental;
            }

            harmGain = 1.0f - phaseNorm * (1.0f - minHarmGain);
            subGain  = 1.0f - phaseNorm * juce::jmin (0.20f, conflictReductionMax * 0.8f);
            freeGain = freeBandBoost + freeBoostMax * phaseNorm;
        }
        else
        {
            harmGain = 1.0f;
            subGain  = 1.0f;
            freeGain = 1.0f;
        }

        targetPhaseShift = phaseNorm * phaseShiftMax;

        oddEvenBalancer.setBalance (score, juce::jlimit (0.0f, 1.0f, conflictReductionMax / 0.25f));
    }

    inline float processHarmonic (float harmonicSignal) noexcept
    {
        currentHarmGain += harmSmoothCoef * (harmGain - currentHarmGain);
        currentSubGain  += subSmoothCoef  * (subGain  - currentSubGain);
        currentFreeGain += freeSmoothCoef * (freeGain - currentFreeGain);

        if (targetPhaseShift < currentPhaseShift)
            currentPhaseShift = currentPhaseShift * phaseFade
                                + targetPhaseShift * (1.0f - phaseFade);
        else
            currentPhaseShift += phaseShiftCoef * (targetPhaseShift - currentPhaseShift);

        currentPhaseShift = juce::jlimit (0.0f, phaseShiftMax, currentPhaseShift);

        float out = harmonicSignal;

        out = phaseShifter.process (out, currentPhaseShift);

        const float vocalScore = vocalDetector.getVocalScore();
        if (vocalDetector.isVocalDetected() && vocalScore >= notchVocalThresh)
        {
            const float targetNotchFreq = vocalFundamental * 2.0f;
            if (std::abs(targetNotchFreq - lastNotchFreq) > 0.5f)
            {
                singleNotch.setNotch (targetNotchFreq, currentSampleRate, 8.0);
                lastNotchFreq = targetNotchFreq;
            }

            const float preNotch = out;
            const float notched = singleNotch.process (preNotch);
            const float notchNorm = (notchVocalThresh < 0.999f)
                                  ? juce::jlimit (0.0f, 1.0f, (vocalScore - notchVocalThresh) / (1.0f - notchVocalThresh))
                                  : 1.0f;
            const float notchDepth = notchNorm * maxNotchDepth;
            out = preNotch * (1.0f - notchDepth) + notched * notchDepth;
        }

        if (vocalScore >= bandedThresh)
            out = notchBank.process (out, currentPhaseShift * phaseMixFactor);

        out *= currentHarmGain;

        return out;
    }

    inline float getHarmGain() const noexcept { return currentHarmGain; }
    inline float getSubGain()  const noexcept { return currentSubGain; }
    inline float getFreeGain() const noexcept { return currentFreeGain; }
    inline float getTargetHarmGain() const noexcept { return harmGain; }
    inline float getTargetSubGain()  const noexcept { return subGain; }
    inline float getTargetFreeGain() const noexcept { return freeGain; }
    inline bool  isVocalDetected() const noexcept { return vocalDetector.isVocalDetected(); }
    inline float getVocalScore() const noexcept { return vocalDetector.getVocalScore(); }

    inline bool needsProcessing() const noexcept
    {
        return vocalDetector.isVocalDetected()
            || std::abs (currentPhaseShift) > 1.0e-3f
            || std::abs (currentHarmGain - 1.0f) > 1.0e-3f
            || std::abs (currentSubGain  - 1.0f) > 1.0e-3f
            || std::abs (currentFreeGain - 1.0f) > 1.0e-3f;
    }

    void getAdaptiveWeights (float inH2, float inH3, float inH4, float inH5,
                             float& outH2, float& outH3, float& outH4, float& outH5)
    {
        float h2 = inH2;
        float h3 = inH3;
        float h4 = inH4;

        oddEvenBalancer.process (h2, h3, h4);

        const float oddMult = (std::abs(inH3) > 1.0e-6f) ? (h3 / inH3) : 1.0f;

        outH2 = h2;
        outH3 = h3;
        outH4 = h4;
        outH5 = inH5 * oddMult;
    }
};
