# MaxxBassClone v2.3 PRO

> **Psychoacoustic Bass Enhancer Plugin (VST2)**  
> Algoritmo profissional de aprimoramento de graves baseado em psicoacústica — Desenvolvido com JUCE 8

---

## 📋 Visão Geral

MaxxBassClone é um plugin VST2 que utiliza princípios psicoacústicos para criar a percepção de graves profundos mesmo em sistemas de áudio com limitações na resposta de baixas frequências. Através da geração controlada de harmônicos, o plugin permite que sub-graves sejam percebidos em fones de ouvido e caixas de som pequenas.

---

## 🚀 Changelog

### Versão 2.3 — Melhorias de Engenharia

#### 1. Modelo Perceptual ISO 226 / A-Weighting

Implementação da curva de ponderação A (IEC 61672) baseada no padrão ISO 226 de loudness igual.

| Componente | v2.2 PRO | v2.3 PRO |
|------------|----------|----------|
| Base do modelo de pesos | Heurística com `smoothStep01` em faixas fixas | **ISO 226 / A-weighting IEC 61672** |
| Distribuição harmônica (w2/w3/w4) | Redistribuição empírica frequência-dependente | **Correção perceptual real baseada na audibilidade de cada harmônica** |
| Método de cálculo | Estático (independente da frequência de corte) | **`aWeightingDB()` em `updateDSP` → `isoPowH*` pré-computado** |
| Performance | — | **Sem chamadas `pow()` no hot path** (pré-computado em `updateDSP`) |

**Funcionamento:** A função `aWeightingDB(f)` implementa a resposta de transferência da ponderação A, aproximando a curva ISO 226 de loudness igual. Para cada frequência de corte definida, o algoritmo calcula a diferença de audibilidade entre as harmônicas e o fundamental:

- **Fundamental em 60 Hz:** 2ª harmônica (120 Hz) ≈ **+18 dB mais audível** → maior peso em w2
- **Fundamental em 60 Hz:** 3ª harmônica (180 Hz) ≈ **+25 dB mais audível** → maior peso em w3
- **Fundamental em 200 Hz:** Diferenças menores → correção sutil

O expoente de compressão `0.30` previne valores extremos em fundamentais abaixo de 40 Hz, mantendo a correção perceptualmente precisa.

---

#### 2. Look-Ahead Soft-Knee Limiter

Sistema de limitação feed-forward com detecção antecipada de picos.

| Componente | v2.2 PRO | v2.3 PRO |
|------------|----------|----------|
| Fórmula | `x / (1 + \|x\| × 0.1)` — saturação sem teto definido | **Feed-forward look-ahead 5 ms** |
| Teto de saída | Indefinido (–0.8 dB @ 0 dBFS) | **–0.17 dBFS garantido** |
| Knee | Ausente | **4 dB soft knee (curva quadrática em dB)** |
| Attack | Implícito (instantâneo) | **0.5 ms** |
| Release | N/A | **150 ms** |
| Overshoot | Presente (detecção reativa) | **Zero** (detecção 5 ms antecipada) |
| Latência reportada | 0 | **`setLatencySamples(laSamples)`** ✓ |

**Princípio de Operação:** O caminho do sinal é atrasado 5 ms através de um ring buffer circular. O detector de picos opera sobre o sinal futuro (não atrasado), calculando o ganho necessário com knee suave. Este ganho é suavizado por um envelope (attack/release) e aplicado ao sinal atrasado — quando o pico chega, o ganho já está preparado. Resultado: zero overshoot, sem efeito de pumping.

---

#### 3. Band Splitter Linkwitz-Riley 4ª Ordem

Crossover de alta precisão com soma de fase coerente.

| Componente | v2.2 PRO | v2.3 PRO |
|------------|----------|----------|
| Topologia dos filtros | Butterworth 2ª ordem independentes | **Linkwitz-Riley 4ª ordem** |
| Soma `sub + kick` | ≠ sinal original (pico de +3 dB em 50 Hz) | **= sinal original (resposta plana)** |
| Relação de fase entre bandas | Diferente (group delay desalinhado) | **Coerente (mesma relação de fase)** |
| Impacto no `DynHarmGain` | Envelope distorcido pelo pico de +3 dB | **Envelope preciso, timing sincronizado** |
| Impacto no `KickDetector` | Duck com timing impreciso | **Duck preciso, transientes mais limpos** |

**Importância:** Tanto `DynHarmGain` quanto `KickDetector` operam sobre envelopes extraídos das bandas de sub e kick. Com Butterworth 2ª ordem, a soma `sub(x) + kick(x) ≠ x` cria um pico artificial de +3 dB em 50 Hz, fazendo o envelope total representar mais energia do que realmente existe — resultando em compressão prematura dos harmônicos. Com LR4, a soma é perfeitamente plana, garantindo envelopes calibrados e duck preciso.

---

### Versão 2.2 — Refinamento do Filtro Harmônico

| Componente | v2.1 PRO | v2.2 PRO |
|------------|----------|----------|
| Ordem do `harmHPF` | 2ª ordem (12 dB/oct) | **4ª ordem — cascata LR4 (24 dB/oct)** |
| Frequência de corte | `0.70 × cutFreq` | **`0.90 × cutFreq`** (maior rejeição do fundamental) |
| Rejeição do fundamental | **-0.9 dB** (insuficiente) | **-4.4 dB** (4ª ordem @ 0.90×) |
| Atenuação da 2ª harmônica (2f) | -1.5 dB | **-0.4 dB** (imperceptível) |
| Ressonância nos graves | Presente (vazamento do fundamental) | **Eliminada** — fundamental isolado antes do blend |

**Análise da Causa Raiz:** O polinômio de Chebyshev T3 gera matematicamente um componente no fundamental (f) além do 3º harmônico desejado (3f). Com filtro de 12 dB/oct a 0.70×cutFreq, o fundamental fica apenas 1.43× acima da frequência de corte, resultando em apenas -0.9 dB de atenuação. Este vazamento soma-se ao sinal original, criando ressonância perceptível. A cascata de dois estágios LR4 a 0.90×cutFreq resolve cirurgicamente sem alterar o caráter tonal.

---

### Versão 2.1 — Ajustes Finos de Calibração

| Componente | v2.0 PRO | v2.1 PRO |
|------------|----------|----------|
| Faixa do parâmetro `cutT` | Saturação em ~120 Hz | **Estendido até ~150 Hz** (maior controle tonal) |
| Ganho harmônico `norm` | `1.20` (sobrecarrega limiter) | **`1.12`** (maior headroom, timbre preservado) |
| Compensação w3/w4 | `0.10` / `0.08` | **`0.08` / `0.06`** (redução de harmônicos espúrios) |
| Threshold `DynHarmGain` (HIGH) | `0.25` | **`0.22`** (compressão iniciada mais cedo) |
| Boost `DynHarmGain` | `1.5x` | **`1.4x`** (menos agressivo em sinais fracos) |
| Hold `DynHarmGain` | `0.7x` | **`0.75x`** (menos duck em sinais fortes) |
| Duck `KickDetector` | `0.60` (−4.4 dB) | **`0.65`** (−3.7 dB, mais musical) |
| Release do Duck | 50 ms | **60 ms** (recuperação mais natural) |
| Multiplicador `envAmt` | `× 2.0` | **`× 1.7`** (teto de drive dinâmico mais conservador) |
| Compressão harmônica | Ausente | **Soft knee dedicada** (`÷(1+\|h\|×0.25)`) antes do blend |

---

### Versão 2.0 — Arquitetura Profissional

| Componente | v1 (Básico) | v2 PRO (Profissional) |
|------------|-------------|----------------------|
| Crossover | Butterworth 2ª ordem | **Linkwitz-Riley 4ª ordem** (soma plana) |
| Geração de harmônicos | Waveshaper genérico | **Polinômios de Chebyshev** (harmônicos puros) |
| Resposta dinâmica | Estática | **Envelope Follower** (reativo ao sinal) |
| Proteção contra aliasing | Nenhuma | **Oversampling 4x** com filtragem anti-alias |
| Distribuição harmônica | Indefinida | **2f > 3f > 4f** (caráter natural, similar a válvulas) |
| Suavização de parâmetros | Nenhuma (cliques) | **SmoothedValue** (transições sem artefatos) |
| Limitação de saída | Hard clip | **Soft limiter** + filtro DC-block |
| Monitoramento | Nenhum | **3 VU meters** (Entrada / Harmônicos / Saída) |

---

## 🔧 Fluxo de Processamento

```
Entrada
  │
  ├─→ [Crossover Linkwitz-Riley 4ª Ordem]
  │       │
  │       ├─→ Banda Grave (< X Hz) ──→ [Envelope Follower]
  │       │                                  │
  │       │                                  ↓ (amplitude dinâmica)
  │       │                            [Oversampler 4x]
  │       │                                  │
  │       │                                  ↓
  │       │                          [Chebyshev T2+T3+T4]
  │       │                          (geração de 2f, 3f, 4f)
  │       │                                  │
  │       │                                  ↓
  │       │                           [Anti-Alias Filter]
  │       │                           (downsample 4x→1x)
  │       │                                  │
  │       │                                  ↓
  │       │                    [HPF LR4 @ 0.90×cutFreq]
  │       │                    (remoção do fundamental vazado)
  │       │                                  │
  │       │                                  ↓
  │       │                    [LPF LR4 @ 5×cutFreq]
  │       │                    (proteção da região vocal)
  │       │                    Corte: harmônicos > 800 Hz
  │       │                                  │
  │       │                                  ↓ harmônicos processados
  │       │                                  │
  │       └─→ Banda Aguda (> X Hz) ──────────┤
  │                                          │
  └─→ Banda Grave × (1 − SubReplace) ────────┤
                                              │
                                      [Mixer: grave + harmônicos]
                                              │
                                        [DC Block Filter]
                                        [Soft-Knee Limiter]
                                        [Output Gain]
                                              │
                                              ↓
                                            Saída
```

---

## 🎛️ Parâmetros

| Controle | Faixa | Descrição |
|----------|-------|-----------|
| **FREQUÊNCIA** | 20–300 Hz | Frequência de corte do crossover que separa os graves do restante do espectro |
| **BASS FAKE** | 0–100% | Quantidade de harmônicos psicoacústicos adicionados ao sinal |
| **DRIVE** | 1–16x | Intensidade da geração de harmônicos via waveshaping |
| **CARÁCTER** | 0–100% | Balanceamento espectral: 0% = ênfase em 2ª harmônica (som gordo/valvulado); 100% = ênfase em 3ª/4ª harmônicas (som agressivo) |
| **DINÂMICA** | 0–100% | Profundidade da modulação dinâmica dos harmônicos pelo envelope follower |
| **OUTPUT** | -18 a +12 dB | Ganho de saída master |
| **SUB REPLACE** | 0–100% | Modo de operação: 0% = blend tradicional; 100% = remoção completa do sub original, mantendo apenas harmônicos (modo MaxxBass clássico) |
| **BYPASS** | ON/OFF | Bypass instantâneo do processamento |

---

## 💻 Compilação (Windows)

### Pré-requisitos

- **JUCE 8** — Instalado em `C:\JUCE`
- **Visual Studio 2022 ou 2026** — Com workload "Desktop development with C++"
- **VST2 SDK** — Extraído em `C:\SDKs\VST2`
  - Download: [VST SDK 2.4](https://github.com/R-Tur/VST_SDK_2.4/archive/refs/heads/master.zip)

### Instruções de Build

1. Abra o arquivo `MaxxBassClone.jucer` no **Projucer**
2. Configure os caminhos globais:
   - **Path to JUCE:** `C:\JUCE`
   - **VST SDK:** `C:\SDKs\VST2`
3. Adicione o exporter **Visual Studio 2022** (ou versão instalada)
4. Salve o projeto (**File → Save Project**)
5. No terminal, navegue até `Builds\VisualStudio2022` e execute:

   ```bash
   msbuild MaxxBassClone.sln /p:Configuration=Release /p:Platform=x64
   ```

6. O binário compilado estará em: `x64\Release\VST\MaxxBassClone.dll`
7. Copie o DLL para o diretório de plugins:
   ```
   C:\Program Files\EqualizerAPO\VSTPlugins\
   ```

### Configuração no Equalizer APO

No Configuration Editor do Equalizer APO:

```
Preamp: -3 dB
VST: C:\Program Files\EqualizerAPO\VSTPlugins\MaxxBassClone.dll
```

---

## 🎧 Presets Recomendados para Headphones

Configuração otimizada para reprodução em fones de ouvido:

| Parâmetro | Valor Sugerido |
|-----------|----------------|
| **FREQUÊNCIA** | 60–80 Hz |
| **BASS FAKE** | 55–75% |
| **DRIVE** | 3–6x |
| **CARÁCTER** | 20–40% (ênfase em corpo, menos agressividade) |
| **DINÂMICA** | 40–60% |
| **OUTPUT** | -1.5 dB |

---

## 📄 Licença

Consulte o arquivo `LICENSE` no diretório raiz do projeto.

---

## 🤝 Contribuições

Contribuições são bem-vindas! Por favor, abra issues para reportar bugs ou sugerir melhorias.

---

**Desenvolvido com ❤️ para a comunidade de áudio profissional**
