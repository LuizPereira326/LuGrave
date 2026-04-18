#pragma once
#include <JuceHeader.h>
#if JUCE_WINDOWS
#include <windows.h>
#endif

//==============================================================================
// AdvancedParams — Todos os parâmetros internos do MaxxBassClone
//
// Este struct centraliza TODOS os parâmetros que antes eram hardcoded
// nos engines e structs de DSP. Permite ajuste em tempo real via painel UI.
//
// Cada grupo de parâmetros corresponde a uma aba no painel de configuração.
//==============================================================================
struct AdvancedParams
{
    //==========================================================================
    // Motor Simples — Pesos harmônicos e configurações
    //==========================================================================
    // Sistema BASE/RANGE para harmChar (BASE = peso em 0%, BASE+RANGE = peso em 100%)
    float SE_H2_BASE        = 0.85f;    // H2 mínimo (harmChar=0%)
    float SE_H2_RANGE       = 0.20f;    // H2 adicional (harmChar=100% = 1.05)
    float SE_H3_BASE        = 0.58f;    // H3 mínimo
    float SE_H3_RANGE       = 0.20f;    // H3 adicional (100% = 0.78)
    float SE_H4_BASE        = 0.22f;    // H4 mínimo
    float SE_H4_RANGE       = 0.16f;    // H4 adicional (100% = 0.38)
    float SE_H5_BASE        = 0.15f;    // H5 mínimo
    float SE_H5_RANGE       = 0.10f;    // H5 adicional (100% = 0.25)

    // Mix e loudness
    float SE_LOUDNESS_COMP  = 0.30f;
    float SE_MIX_MULTIPLIER = 1.75f;  // era 1.50 — harmônicos mais presentes no mix

    // Filtros de harmônicos
    float SE_HARM_LPF_FREQ  = 350.0f;   // LPF fixo no prepare()
    float SE_HPF_MULT_1     = 2.0f;     // HPF1: crossover * H1
    float SE_HPF_MULT_2     = 2.5f;     // HPF2: crossover * H2
    float SE_LPF_MULT       = 3.0f;     // LPF variável: crossover * LMult
    float SE_LPF_MIN        = 200.0f;   // Freq mínima LPF variável
    float SE_LPF_MAX        = 350.0f;   // Freq máxima LPF variável

    // DC Blockers
    float SE_DC_BLOCK_FREQ       = 10.0f;   // Saída
    float SE_HARM_DC_BLOCK_FREQ  = 20.0f;   // Harmônicos

    // Temporização
    float SE_RAMP_SEC            = 0.020f;

    // Adaptativo
    float SE_ADAPTIVE_THRESH_INIT = 0.60f;
    float SE_VETO_STRENGTH_INIT   = 0.95f;
    float SE_CONF_HIGH            = 0.75f;

    //==========================================================================
    // Gerador Chebyshev
    //==========================================================================
    float CH_T2_SCALE         = 2.5f;
    float CH_T3_CLAMP         = 1.5f;
    float CH_T4_CLAMP         = 2.0f;
    float CH_T5_SCALE         = 0.40f;
    float CH_T5_CLAMP         = 1.8f;
    float CH_DRIVE_LOG_SCALE  = 0.85f;
    float CH_PRE_CLAMP        = 1.5f;
    float CH_OUTPUT_CLAMP_4H  = 2.5f;
    float CH_OUTPUT_CLAMP_5H  = 3.0f;

    //==========================================================================
    // Motor Híbrido
    //==========================================================================
    // Pesos harmônicos (HarmChar=0% base, HarmChar=100% base+range)
    // CORREÇÃO v48: RANGE aumentado significativamente para CHAR ter efeito audível
    // Antes: RANGE muito pequeno = diferença imperceptível entre 0% e 100%
    // Agora: RANGE = ~80-100% do BASE = diferença clara e musical
    //       CHAR=0%   → som limpo, harmônicos sutis (apenas BASE)
    //       CHAR=100% → som agressivo, harmônicos pronunciados (BASE + RANGE)
    float HE_H2_BASE    = 0.45f;   // v48: reduzido para dar headroom ao RANGE
    float HE_H2_RANGE   = 0.45f;   // v48: era 0.12 -> agora +100% de variação (0.45 a 0.90)
    float HE_H3_BASE    = 0.40f;   // v48: reduzido
    float HE_H3_RANGE   = 0.40f;   // v48: era 0.10 -> agora +100% de variação (0.40 a 0.80)
    float HE_H4_BASE    = 0.18f;   // v48: reduzido
    float HE_H4_RANGE   = 0.25f;   // v48: era 0.08 -> agora +139% de variação (0.18 a 0.43)
    float HE_H5_BASE    = 0.06f;   // v48: reduzido
    float HE_H5_RANGE   = 0.14f;   // v48: era 0.05 -> agora +233% de variação (0.06 a 0.20)

    // Compressão e loudness
    // v35: REDUZIDO para resolver problema de "volume mais alto que a música"
    // Antes: perceptual=0.50, loudness=0.30 → com mix=100% = 1.5x * 1.3x = 1.95x
    // vMIX: perceptual=0.34, loudness=0.18 → com mix=100% = 1.34x * 1.18x = 1.58x
    //       ganho total controlado, sem empilhamento excessivo sobre funk/eletrônica
    float HE_PERCEPTUAL_BOOST = 0.42f;  // era 0.34
    float HE_LOUDNESS_COMP    = 0.18f;

    // Filtros
    float HE_HARM_LPF_FREQ = 300.0f;   // LPF init seguro (v28: era 400)
    float HE_HPF_MULT      = 1.25f;
    float HE_LPF_MULT      = 3.5f;    // v28: era 5.0 (muito largo → buzina)
    float HE_LPF_MIN       = 200.0f;  // v28: era 300 (muito alto)
    float HE_LPF_MAX       = 350.0f;  // v28: era 500 (muito alto → buzina)

    // DC Block
    float HE_DC_BLOCK_FREQ = 10.0f;

    // Temporização
    float HE_DYN_RAMP_SEC     = 0.005f;
    float HE_SMOOTH_RAMP_SEC  = 0.020f;

    //==========================================================================
    // SubBassSplitter + BassLeaner
    //==========================================================================
    float SBS_SUB_MAX_ATTEN   = 0.40f;
    float SBS_BODY_MAX_ATTEN  = 0.80f;  // era 0.62 → body retém 20% no mix máximo (era 38%)
    float SBS_SUB_MIN_RETAIN  = 0.30f;
    float SBS_BODY_MIN_RETAIN = 0.18f;  // era 0.30 → permite o body ir mais fundo
    float SBS_SPLIT_FREQ      = 60.0f;

    float BL_MAX_LEAN       = 0.25f;
    float BL_FAST_ATT_MS    = 2.0f;
    float BL_FAST_REL_MS    = 80.0f;
    float BL_SLOW_ATT_MS    = 30.0f;
    float BL_SLOW_REL_MS    = 300.0f;
    float BL_LEAN_SMOOTH_MS = 20.0f;
    float BL_CREST_THRESH   = 2.0f;
    float BL_CREST_SCALE    = 0.5f;
    float BL_FULLNESS_SCALE = 3.5f;

    //==========================================================================
    // DynHarmGain
    //==========================================================================
    float DHG_BASE_THRESH_LOW  = 0.02f;
    float DHG_BASE_THRESH_HIGH = 0.37f;
    float DHG_BASE_BOOST       = 1.45f;
    float DHG_BASE_CUT         = 0.72f;
    float DHG_MIN_BOOST        = 1.00f;
    float DHG_MIN_CUT          = 0.95f;
    float DHG_CENTER           = 0.15f;
    float DHG_HALF_WIDTH_MIN   = 0.04f;
    float DHG_HALF_WIDTH_RANGE = 0.18f;
    float DHG_ATT_MS           = 8.0f;
    float DHG_REL_MS           = 100.0f;

    //==========================================================================
    // SubNormalizer
    //==========================================================================
    // v35: MAX_BOOST reduzido de 2.50 para 1.80 (+5dB ao invés de +8dB)
    // O boost excessivo de sub estava fazendo o volume ficar desproporcional
    float SN_TARGET_LEVEL    = 0.10f;
    float SN_MAX_BOOST       = 1.80f;   // v35: era 2.50 (+8dB) → agora +5dB
    float SN_MAX_REDUCE      = 0.35f;
    float SN_BOOST_RATIO     = 0.80f;
    float SN_COMP_SLOPE      = 0.60f;
    float SN_ATT_MS          = 250.0f;
    float SN_REL_MS          = 1000.0f;
    float SN_GAIN_SMOOTH_MS  = 50.0f;
    float SN_LPF_FREQ        = 60.0f;

    //==========================================================================
    // AntiDistortionGuard
    //==========================================================================
    float ADG_SAFE_LIMIT          = 2.3f;
    float ADG_HARD_LIMIT          = 3.5f;
    float ADG_SATURATION_START    = 1.5f;
    float ADG_SAT_OVER_NORM_FACTOR = 0.3f;
    float ADG_LIMIT_FACTOR        = 0.2f;

    //==========================================================================
    // DeepVocalProtector + SmartKickDucker
    //==========================================================================
    float DVP_MIN_HARM_GAIN          = 0.85f;
    float DVP_MAX_NOTCH_DEPTH        = 0.35f;
    float DVP_PHASE_SHIFT_MAX        = 0.80f;
    float DVP_FREE_BAND_BOOST        = 1.25f;
    float DVP_VOCAL_PHASE_THRESH     = 0.35f;
    float DVP_NOTCH_VOCAL_THRESH     = 0.60f;
    float DVP_BANDED_THRESH          = 0.45f;
    float DVP_CONFLICT_REDUCTION_MAX = 0.15f;
    float DVP_FREE_BOOST_MAX         = 0.25f;
    float DVP_PHASE_FADE             = 0.97f;
    float DVP_KICK_DUCK_AMOUNT       = 0.25f;
    float DVP_PUNCH_REDUCTION_MAX    = 0.20f;
    float DVP_PHASE_MIX_FACTOR       = 0.70f;

    float SKD_TRANSIENT_THRESH  = 0.35f;
    float SKD_DUCK_AMOUNT       = 0.25f;
    float SKD_DUCK_DURATION     = 150.0f;
    float SKD_DUCK_COOLDOWN     = 800.0f;
    float SKD_BAND_CENTER       = 80.0f;
    float SKD_BAND_Q            = 1.5f;
    float SKD_ATTACK_HPF_FREQ   = 100.0f;
    float SKD_ATTACK_MS         = 0.5f;
    float SKD_RELEASE_MS        = 30.0f;

    //==========================================================================
    // Sub Protection (PluginProcessor.cpp)
    //==========================================================================
    float SP_HPF_BASE_CUT    = 88.0f;   // era 85 → ponto -6dB (LR4) em 88 Hz
    float SP_SUB_LPF_FREQ    = 70.0f;
    float SP_BODY_LPF_FREQ   = 150.0f;
    float SP_SUB_ENV_ATT_MS  = 2.0f;
    float SP_SUB_ENV_REL_MS  = 110.0f;
    float SP_BODY_ENV_ATT_MS = 4.0f;
    float SP_BODY_ENV_REL_MS = 80.0f;
    float SP_TARGET_CUT_MIN  = 85.0f;   // era 70 → piso garantido: -6dB em 88Hz, -4dB em 100Hz
    float SP_TARGET_CUT_MAX  = 105.0f;  // era 95 → teto com headroom real
    float SP_DECIMATE_FACTOR = 4.0f;
    
    // MELHORIA v49: Parâmetros para suavização do cutoff (evita clicks/pop)
    float SP_CUTOFF_SMOOTH_MS = 150.0f; // Tempo de interpolação do targetCut
    
    // MELHORIA v49: Redução dos lifts para manter adaptabilidade
    // Antes: lifts somavam +30+ Hz → sempre batia no teto de 105Hz
    // Agora: lifts reduzidos ~40-50% → cutoff varia musicalmente dentro do range
    float SP_MUSIC_LIFT_MAX    = 3.5f;   // era 6.5
    float SP_PRESSURE_LIFT_MAX = 8.0f;   // era 15.0
    float SP_HARD_LIFT_MAX     = 6.0f;   // era 11.0
    float SP_DRIVE_LIFT_MAX    = 2.5f;   // era 4.5
    float SP_MIX_LIFT_MAX      = 1.5f;   // era 3.0
    float SP_GAIN_LIFT_MAX     = 1.0f;   // era 2.0

};


static inline void appendAdvancedParamsDebugLog (const juce::String& message)
{
    constexpr bool enableAdvancedParamsDebugLog = false;
    if (! enableAdvancedParamsDebugLog)
        return;

    auto logFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                       .getChildFile ("MaxxBass_advanced_debug.log");
    logFile.appendText (juce::Time::getCurrentTime().toString (true, true)
                        + " | " + message + "\n");
}

static inline juce::File getAdvancedParamsSharedFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                   .getChildFile ("MaxxBassClone");
    if (! dir.exists())
        dir.createDirectory();
    return dir.getChildFile ("advanced_params_v1.bin");
}

static inline bool saveAdvancedParamsToSharedFile (const AdvancedParams& params)
{
    auto file = getAdvancedParamsSharedFile();
    juce::TemporaryFile temp (file);

    {
        juce::FileOutputStream stream (temp.getFile());
        if (! stream.openedOk())
            return false;

        if (! stream.write (&params, sizeof (AdvancedParams)))
            return false;

        stream.flush();
    }

    return temp.overwriteTargetFileWithTemporary();
}

static inline bool loadAdvancedParamsFromSharedFile (AdvancedParams& params, juce::Time& lastWriteTime)
{
    auto file = getAdvancedParamsSharedFile();
    if (! file.existsAsFile())
        return false;

    const auto modified = file.getLastModificationTime();
    if (modified <= lastWriteTime)
        return false;

    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return false;

    AdvancedParams temp;
    if (stream.read (&temp, sizeof (AdvancedParams)) != sizeof (AdvancedParams))
        return false;

    params = temp;
    lastWriteTime = modified;
    return true;
}
