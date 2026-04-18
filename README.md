# MaxxBassClone v2.3 PRO — Psychoacoustic Bass Enhancer (VST2)
> Algoritmo profissional nível MaxxBass real — JUCE 8

---

## O que mudou da v2.2 para v2.3 (três correções de engenharia)

### 1. Modelo perceptual ISO 226 / A-weighting (v2.3)

| Componente | v2.2 PRO | v2.3 PRO |
|---|---|---|
| Base do modelo de pesos | Heurística com `smoothStep01` em faixas fixas | **ISO 226 / A-weighting IEC 61672** |
| `w2/w3/w4` | Redistribuição empírica, frequência-dependente | **+ correção perceptual real: quanto mais audível é cada harmônica** |
| Cálculo | Estático (não varia com frequência de corte) | **`aWeightingDB()` em `updateDSP` → `isoPowH*` pré-computado** |
| Performance | — | **Sem `pow()` no hot path** (pré-computado em `updateDSP`) |

**Como funciona:** `aWeightingDB(f)` implementa a função de transferência da A-weighting (IEC 61672) — aproximação da curva ISO 226 de igual loudness. Para cada cutFreq definido pelo usuário, calculamos quanto mais audível é a 2ª, 3ª e 4ª harmônica relativa ao fundamental:

- Fundamental em 60 Hz: a 2ª harmônica (120 Hz) é **≈+18 dB mais audível** → bias em w2
- Fundamental em 60 Hz: a 3ª harmônica (180 Hz) é **≈+25 dB mais audível** → bias maior em w3
- Fundamental em 200 Hz: diferenças menores → correção suave

Expoente de compressão `0.30` evita valores extremos em fundamentais <40 Hz, mantendo a direção perceptualmente correta.

---

### 2. Look-ahead Soft-Knee Limiter (v2.3)

| Componente | v2.2 PRO | v2.3 PRO |
|---|---|---|
| Fórmula | `x / (1 + \|x\| × 0.1)` — saturação sem teto | **Feed-forward look-ahead 5 ms** |
| Teto | Indefinido (–0.8 dB @ 0 dBFS, nunca limita de fato) | **–0.17 dBFS garantido** |
| Knee | Ausente | **4 dB soft knee (quadrático em dB)** |
| Attack | Implícito (instantâneo na saturação) | **0.5 ms** |
| Release | N/A | **150 ms** |
| Overshoot | Presente (detecta DEPOIS do pico) | **Zero** (detecta 5 ms ANTES) |
| Latência reportada ao host | 0 | **`setLatencySamples(laSamples)`** ✓ |

**Princípio:** O signal path é atrasado 5 ms (ring buffer circular). O detector de pico opera sobre o sinal FUTURO (não atrasado), computa o ganho com knee suave, suaviza com envelope (att/rel), e aplica ao sinal ATRASADO → o ganho já está pronto quando o pico chega. Zero overshoot, sem pumping.

---

### 3. BandSplitter Linkwitz-Riley 4ª ordem (v2.3)

| Componente | v2.2 PRO | v2.3 PRO |
|---|---|---|
| Filtros sub/kick | Butterworth 2ª ordem independentes | **Linkwitz-Riley 4ª ordem** |
| Soma `sub + kick` | ≠ sinal original (+3 dB peak em 50 Hz) | **= sinal original (soma plana)** |
| Phase entre bandas | Diferente (group delay desalinhado) | **Coerente (mesma relação de phase)** |
| Impacto em `DynHarmGain` | Envelope distorcido pelo +3 dB na soma | **Envelope correto, timing sincronizado** |
| Impacto em `KickDetector` | Duck com timing ligeiramente errado | **Duck preciso, punch mais limpo** |

**Por que importa:** O `DynHarmGain` e o `KickDetector` operam sobre envelopes extraídos do sub e do kick. Com Butterworth 2ª ordem, `sub(x) + kick(x) ≠ x` (pico de +3 dB em 50 Hz). Isso fazia o envelope total representar mais energia do que existe — o `DynHarmGain` segurava os harmônicos cedo demais. Com LR4, a soma é perfeitamente plana → envelopes calibrados, duck preciso.

---



| Componente              | v2.1 PRO                               | v2.2 PRO                                                    |
|------------------------|----------------------------------------|-------------------------------------------------------------|
| `harmHPF` ordem        | 2ª ordem (12 dB/oct)                   | **4ª ordem — cascata LR4 (24 dB/oct)**                      |
| `harmHPF` razão corte  | `0.70 × cutFreq`                       | **`0.90 × cutFreq`** (maior rejeição de f)                  |
| Rejeição do fundamental| **-0.9 dB** (quase nenhuma)            | **-4.4 dB** (4ª ordem @ 0.90×)                             |
| 2ª harmônica (2f)      | -1.5 dB de atenuação (ok)              | **-0.4 dB** (imperceptível, caráter inalterado)             |
| Ressonância nos graves | Presente (T3 vazava f no mix)          | **Eliminada** — fundamental isolado antes do blend          |

**Causa raiz identificada**: O polinômio de Chebyshev T3 matematicamente sempre gera um componente
no fundamental f (além do 3f desejado). Com 12 dB/oct a 0.70×cutFreq, f ficava apenas 1.43×
acima do corte → só -0.9 dB de atenuação. Esse fundamental vazava para o mix e somava ao sinal
original, criando a ressonância perceptível. A cascata de 2 estágios a 0.90×cutFreq resolve
cirurgicamente sem alterar o caráter sonoro.

---

## O que mudou da v2.0 para v2.1 (refinamento fino)

| Componente              | v2.0 PRO                    | v2.1 PRO                                          |
|------------------------|-----------------------------|---------------------------------------------------|
| `cutT` range           | saturava em ~120 Hz         | **estendido até ~150 Hz** (mais gradação tonal)   |
| Ganho harmônico `norm` | `1.20` (pressiona limiter)  | **`1.12`** (mais margem, sem mudar timbre)        |
| Compensação w3/w4      | `0.10` / `0.08`             | **`0.08` / `0.06`** (menos harmônico fantasma)    |
| `DynHarmGain` THRESH   | HIGH = `0.25`               | **`0.22`** (segura mais cedo, kick médio já comprime) |
| `DynHarmGain` boost    | `1.5x`                      | **`1.4x`** (menos agressivo em sub fraco)         |
| `DynHarmGain` hold     | `0.7x`                      | **`0.75x`** (menos duck em sinal forte)           |
| `KickDetector` duck    | `0.60` (−4.4 dB)            | **`0.65`** (−3.7 dB, mais musical)                |
| Duck release           | 50ms                        | **60ms** (recuperação mais natural)               |
| `envAmt` multiplier    | `× 2.0`                     | **`× 1.7`** (teto de drive dinâmico mais contido) |
| Compressão harmônica   | ausente                     | **soft knee isolada no harmônico** (`÷(1+|h|×0.25)`) antes do blend |

| Componente         | v1 (básico)              | v2 PRO (profissional)                        |
|-------------------|--------------------------|----------------------------------------------|
| Crossover         | Butterworth 2a ordem     | **Linkwitz-Riley 4a ordem** (soma plana)     |
| Harmônicos        | Waveshaper genérico      | **Polinômios de Chebyshev** (harmônicos puros) |
| Dinâmica          | Estático                 | **Envelope Follower** (resposta ao sinal)    |
| Aliasing          | Sem proteção             | **Oversampling 4x** com anti-aliasing        |
| Rolloff harmônico | Misturado                | **2f>3f>4f** natural (imita valvulado)       |
| Smooth de params  | Nenhum (clicks)          | **SmoothedValue** (sem clicks)               |
| Saída             | Hard clip                | **Soft limiter** + DC block                  |
| VU Meters         | Nenhum                   | **3 VU meters** (IN / HARM / OUT)            |

---

## Algoritmo explicado

```
Entrada
  ├── [LR4 Crossover]
  │     ├── Bass (abaixo de X Hz) ──→ [Envelope Follower]
  │     │                                    │ (amplitude dinâmica)
  │     │                             [Oversampler 4x]
  │     │                                    │
  │     │                          [Chebyshev T2+T3+T4]
  │     │                          gera 2f, 3f, 4f puros
  │     │                                    │
  │     │                             [AA Filter 4x→1x]
  │     │                                    │
  │     │                    [HPF LR4 @ 0.90×cutFreq]  ← remove 1f vazado
  │     │                                    │
  │     │                    [LPF LR4 @ 5×cutFreq]     ← PROTEÇÃO VOCAL
  │     │                    bloqueia harmônicos >800Hz      (limita 2f/3f/4f,
  │     │                    que invadiriam a região de voz)  nada acima)
  │     │                                    │ harmônicos
  │     └── High (acima de X Hz) ─────────┐  │
  │                                        ↓  ↓
  └──── Bass × (1 − SubReplace) ──→ [Mix: bass_atenuado + harm × mix]
                                          │
                                    [DC Block]
                                    [Soft Limiter]
                                    [Output Gain]
                                          │
                                        Saída
```

---

## Parâmetros

| Knob        | Range        | Função                                                      |
|------------|-------------|--------------------------------------------------------------|
| FREQUENCIA | 20–300 Hz   | Onde o crossover separa o bass do resto                      |
| BASS FAKE  | 0–100%      | Quantidade de harmônicos adicionados ao sinal                |
| DRIVE      | 1–16x       | Intensidade da geração de harmônicos                         |
| CARACTER   | 0–100%      | 0% = mais 2o (gordo/valvulado), 100% = mais 3o/4o (agressivo)|
| DINAMICA   | 0–100%      | Quanto o envelope modula os harmônicos dinamicamente         |
| OUTPUT     | -18 a +12dB | Ganho de saída                                               |
| SUB REPLACE| 0–100%      | 0% = blend puro; 100% = remove sub original, deixa só harmônicos (MaxxBass mode) |
| BYPASS     | ON/OFF      | Bypass instantâneo                                           |

---

## Como compilar (Windows)

### Pré-requisitos
- **JUCE 8** extraído em `C:\JUCE`
- **Visual Studio 2022/2026** com "Desktop C++"
- **VST2 SDK** em `C:\SDKs\VST2`
  - https://github.com/R-Tur/VST_SDK_2.4/archive/refs/heads/master.zip

### Passos
1. Abra `MaxxBassClone.jucer` no Projucer
2. Global Paths → Path to JUCE: `C:\JUCE` | VST SDK: `C:\SDKs\VST2`
3. Adicione exporter: Visual Studio 2022
4. File → Save Project
5. No terminal da pasta `Builds\VisualStudio2022`:
   ```
   msbuild MaxxBassClone.sln /p:Configuration=Release /p:Platform=x64
   ```
6. DLL em: `x64\Release\VST\MaxxBassClone.dll`
7. Copie para `C:\Program Files\EqualizerAPO\VSTPlugins\`

### Config EQ APO
```
Preamp: -3 dB
VST: C:\Program Files\EqualizerAPO\VSTPlugins\MaxxBassClone.dll
```

### Configurações recomendadas para headphones
- FREQUENCIA: 60–80 Hz
- BASS FAKE: 55–75%
- DRIVE: 3–6x
- CARACTER: 20–40% (mais gordo que sujo)
- DINAMICA: 40–60%
- OUTPUT: -1.5 dB
