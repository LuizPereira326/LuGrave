CORRECAO: ESTADO FANTASMA / APVTS COMO FONTE DE VERDADE
======================================================

PROBLEMA
--------
A janela Advanced Parameters podia abrir com valores vindos de estado global
compartilhado (shared memory) e nao necessariamente com o estado real salvo no
host via APVTS. Isso permitia:

1. Reabrir o plugin e ver valores antigos reaparecendo na UI
2. Misturar APVTS + advParams + shared memory como fontes de verdade
3. Parametros avancados operarem com restauracao inconsistente
4. Medo legitimo de acumulacao invisivel / comportamento fora do que a UI mostra

CORRECAO APLICADA
-----------------
1. APVTS virou a fonte de verdade para os parametros avancados expostos na UI
2. Todos os parametros da janela Advanced Parameters agora sao registrados no APVTS
3. syncAdvancedParamsFromApvts() agora cobre todos os parametros expostos na UI
4. O estado global compartilhado foi neutralizado (no-op) para nao contaminar instancias
5. Ao abrir a janela avancada, o editor recarrega os valores do APVTS real
6. A janela avancada e recriada ao abrir, garantindo sliders consistentes
7. setStateInformation() agora sincroniza imediatamente advParams/audioParams a partir do APVTS

ARQUIVOS ALTERADOS
------------------
- AdvancedParams.h
- PluginProcessor.h
- PluginProcessor.cpp
- PluginEditor.cpp

EFEITO PRATICO
--------------
- Nao deve mais haver carregamento fantasma de valores antigos por shared state
- A UI avancada deve refletir o estado real salvo no host
- Reabrir a mesma instancia nao deve "somar" efeito escondido
- O que estiver no APVTS e o que o DSP vai usar
