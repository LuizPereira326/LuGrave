================================================================================
  MAXX BASS CLONE - v48 CORRIGIDO
  Correção da Race Condition que causava freeze/crash
================================================================================

PROBLEMA ORIGINAL
-----------------
O plugin travava/crashava aleatoriamente, especialmente ao mover sliders no
painel de configuração avançada (SETTINGS). O áudio ficava distorcido ou 
produzia ruídos de "buzina", e o WASAPI resetava a cadeia de áudio.

CAUSA RAIZ
----------
Race condition clássica entre UI Thread e Audio Thread:

  - A UI thread (AdvancedPanel) escrevia diretamente em `advParams.xxx = value`
  - Ao mesmo tempo, a audio thread lia os mesmos campos em `processBlock()`
  - SEM NENHUMA SINCRONIZAÇÃO entre as threads!

Isso causava leitura inconsistente de grupos de parâmetros (ex: os 9 campos do
DHG lidos em sequência), resultando em:
  1. Coeficientes de filtro IIR instáveis
  2. NaN/Inf nas amostras de áudio  
  3. Driver WASAPI reseta
  4. APO EQ e VST congelam

SOLUÇÃO IMPLEMENTADA
--------------------
Double-buffer com SpinLock e flag atômica:

  1. PluginProcessor.h: Adicionados novos membros:
     - `AdvancedParams audioParams;`      // Audio thread lê daqui
     - `juce::SpinLock paramsSpinLock;`   // Sincronização (~100ns)
     - `std::atomic<bool> paramsDirty;`   // Flag para sinalizar mudanças

  2. PluginProcessor.cpp: No início de processBlock():
     - Se paramsDirty == true, copia advParams → audioParams sob SpinLock
     - Audio thread NUNCA toca advParams, só lê audioParams
     - Se o lock falhar (UI escrevendo), usa audioParams anterior (seguro)

  3. SimpleEngine/HybridEngine: Assinatura de processBlock() alterada:
     - Agora recebe `AdvancedParams& params` por referência
     - Usa params thread-safe passado pelo processBlock()

  4. AdvancedPanel.h: Construtor alterado:
     - Recebe `std::atomic<bool>& paramsDirty`
     - setParamValue() agora faz: `paramsDirty.store(true)` após cada escrita

  5. PluginEditor.cpp: openAdvancedPanel() alterado:
     - Passa `proc.paramsDirty` para AdvancedPanelWindow

ARQUIVOS MODIFICADOS
--------------------
  * PluginProcessor.h     - Double-buffer + SpinLock + atomic flag
  * PluginProcessor.cpp   - Sincronização no processBlock() e prepareToPlay()
  * SimpleEngine.h        - Nova assinatura de processBlock()
  * SimpleEngine.cpp      - Usa params passado por referência
  * HybridEngine.h        - Nova assinatura de processBlock()
  * HybridEngine.cpp      - Usa params passado por referência
  * AdvancedPanel.h       - Recebe e seta paramsDirty
  * PluginEditor.cpp      - Passa paramsDirty para AdvancedPanelWindow

ARQUIVOS INALTERADOS (copiados do original)
--------------------------------------------
  * PluginEditor.h        - Sem alterações necessárias
  * DspStructs.h          - Sem alterações necessárias

================================================================================
  CORREÇÃO ADICIONAL: CHAR (Character) NÃO FUNCIONAVA NO SIMPLE ENGINE
================================================================================

PROBLEMA
--------
O parâmetro "CHAR" (harmChar) não tinha efeito audível ao mover de 0% a 100%.

CAUSA
-----
O SimpleEngine usava valores FIXOS para os pesos harmônicos (H2, H3, H4, H5)
que eram setados apenas uma vez no prepare() e NUNCA eram atualizados pelo
parâmetro harmChar. O HybridEngine já tinha a lógica correta com BASE/RANGE.

SOLUÇÃO
-------
1. AdvancedParams.h: Adicionados novos parâmetros SE_H*_BASE e SE_H*_RANGE:
   - SE_H2_BASE = 0.85, SE_H2_RANGE = 0.20 (total 1.05 quando harmChar=100%)
   - SE_H3_BASE = 0.58, SE_H3_RANGE = 0.20 (total 0.78 quando harmChar=100%)
   - SE_H4_BASE = 0.22, SE_H4_RANGE = 0.16 (total 0.38 quando harmChar=100%)
   - SE_H5_RANGE = 0.10 (SE_H5_BASE já existia = 0.15)

2. SimpleEngine.cpp: Agora lê harmChar do APVTS e calcula pesos dinamicamente:
   - const float harmChar = apvts.getRawParameterValue("harmChar")->load() * 0.01f;
   - h2Weight = params.SE_H2_BASE + harmChar * params.SE_H2_RANGE;
   - (mesma lógica para H3, H4, H5)

3. ChebyshevGen::process5WithGain agora recebe h2Weight, h3Weight, h4Weight,
   h5Weight calculados dinamicamente em vez dos valores fixos H2, H3, H4, H5.

RESULTADO
---------
Agora o knob CHAR funciona no SimpleEngine igual ao HybridEngine:
  - CHAR = 0%   -> harmônicos mais sutis (apenas BASE)
  - CHAR = 100% -> harmônicos mais agressivos (BASE + RANGE)

================================================================================
  CORREÇÃO ADICIONAL: CHAR SEM EFEITO AUDÍVEL NO HYBRID ENGINE
================================================================================

PROBLEMA
--------
O parâmetro "CHAR" (harmChar) não tinha efeito perceptível ao mover de 0% a 100%
no modo Híbrido. O usuário não notava diferença sonora.

CAUSA
-----
Os valores de HE_H*_RANGE eram MUITO PEQUENOS comparados aos HE_H*_BASE:

  ANTES (quase imperceptível):
  | Harmônico | BASE | RANGE | Total@100% | Variação |
  |-----------|------|-------|------------|----------|
  | H2        | 0.62 | 0.12  | 0.74       | +19%     |
  | H3        | 0.58 | 0.10  | 0.68       | +17%     |
  | H4        | 0.30 | 0.08  | 0.38       | +27%     |
  | H5        | 0.11 | 0.05  | 0.16       | +45%     |

A diferença entre CHAR=0% e CHAR=100% era de apenas 17-45%, insuficiente para
o ouvido perceber como mudança significativa de timbre.

SOLUÇÃO
-------
AdvancedParams.h: RANGE aumentado para ~80-100% do valor BASE:

  DEPOIS (claramente audível):
  | Harmônico | BASE | RANGE | Total@100% | Variação |
  |-----------|------|-------|------------|----------|
  | H2        | 0.45 | 0.45  | 0.90       | +100%    |
  | H3        | 0.40 | 0.40  | 0.80       | +100%    |
  | H4        | 0.18 | 0.25  | 0.43       | +139%    |
  | H5        | 0.06 | 0.14  | 0.20       | +233%    |

O BASE foi reduzido para dar "headroom" ao RANGE, mantendo os totais máximos
dentro de valores musicais razoáveis.

RESULTADO
---------
Agora o knob CHAR no Hybrid Engine tem efeito claramente audível:
  - CHAR = 0%   -> harmônicos sutis, som mais limpo/natural
  - CHAR = 50%  -> balanço equilibrado
  - CHAR = 100% -> harmônicos pronunciados, som mais agressivo/presente

================================================================================

POR QUE FUNCIONA
----------------
  - Audio thread NUNCA mais toca em advParams — só lê audioParams
  - UI thread escreve em advParams e seta paramsDirty = true
  - No próximo processBlock (a cada ~1-10ms), a cópia é feita sob SpinLock
  - SpinLock dura ~100ns — totalmente transparente para o áudio
  - ScopedTryLockType garante que se a UI estiver escrevendo, a audio thread
    simplesmente usa audioParams anterior (sem bloquear, sem glitch)

================================================================================
