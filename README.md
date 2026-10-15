# Aurora

Sintetizador e processador de efeitos VST3 de código aberto para Windows x64.

[Baixar Aurora 0.2.3](https://github.com/GGSantoDa019/Aurora/releases/tag/v0.2.3) · [Reportar problema](https://github.com/GGSantoDa019/Aurora/issues) · [Licença GPLv3](LICENSE.txt)

![Interface do Aurora](interface.png)

## Otimizações da versão 0.2.3

All parameters sem sobreposição de wavetables ou teclado duplicado; gráficos com composição comum e atualização por mudança. Processamento mais eficiente durante automação, caches de parâmetros e configuração, compartilhamento de samples/coeficientes de IR e preparação de assets fora da região crítica de áudio. Consulte [medições e limites](docs/OPTIMIZATION_023.md).

## Integração de wavetable e presets

Níveis periódicos compactos preparados fora do áudio, unison de até 16 subvozes, dois slots de warp suportados e fontes de modulação independentes do volume audível. O importador distingue unidades, defaults e IDs locais, aceita três codificações de payload e preserva metadados e campos desconhecidos ao salvar em `.aurora`.

A importação continua parcial: curvas de warp indisponíveis, ciclos de modulação e diferenças nos motores e efeitos são indicados no relatório. Consulte [detalhes e limites](docs/WAVETABLE_PRESET_023.md).

## Novidades da versão 0.2.3

- Interface com três osciladores, painéis Sub/Noise, filtros por abas e editores de envelope/LFO. Redimensionamento proporcional e tamanho inicial próximo de 960 pixels.
- Leitor wavetable de 16 pontos e 512 fases, com transição de 64 avaliações entre níveis e estado independente por subvoz de unison.
- Automação com offsets por amostra e suavização linear de 10 ms.
- Sete instâncias de roteamento, dois buses de efeitos e rack principal.
- Atualizações de filtro, delay, compressor, deslocamento de frequência e convolução.
- Estado estruturado com metadados JSON e payload CBOR/Zstandard, migração de versões e assets incorporados.
- 626 presets procedurais de fábrica, expressão por voz e melhorias de MPE.

A versão passou por **90 verificações automatizadas**, incluindo render dos VST3, estado, importação de assets, interface e regressão numérica. Os testes não substituem a validação em todas as DAWs. MPE completo, alguns motores e algoritmos continuam em desenvolvimento.

## Downloads e instalação

Baixe `Aurora-0.2.3-Windows-x64.zip` na [release](https://github.com/GGSantoDa019/Aurora/releases/tag/v0.2.3) e extraia o arquivo inteiro.

1. Feche a DAW e os aplicativos que estiverem usando o plugin.
2. Execute `Instalar-Aurora.ps1` em um PowerShell como administrador. O script preserva uma cópia da instalação anterior.
3. No gerenciador de plugins da DAW, faça uma nova busca por instrumentos VST3.

Instalação manual: copie as pastas completas `Aurora.vst3` e `Aurora FX.vst3` para `C:\Program Files\Common Files\VST3`. Não copie apenas o arquivo interno. O pacote também inclui o aplicativo Standalone.

No instrumento, abra **Presets → Choose preset folder** e selecione `FactoryPresets`. Para guardar uma edição, use **Save** e o formato `.aurora`.

## Recursos

Três osciladores com seleção de motor; wavetables geradas ou importadas; carregamento de samples e SFZ; filtros com visualização da modulação; quatro envelopes e dez LFOs por voz; oito macros; matriz de modulação; racks de efeitos; arp/clip e controles de performance. Alguns motores usam algoritmos simplificados. Consulte [GAP_ANALYSIS.md](GAP_ANALYSIS.md) para o estado técnico e limites.

Os estados novos usam AuroraState v2. Há leitura e migração dos formatos Aurora antigos, mas a versão 0.1.3 não abre os arquivos v2. Preserve os presets anteriores se precisar voltar.

## Compilar

São necessários CMake 3.22+, compilador C++17 e uma toolchain Windows compatível. JUCE e Zstandard acompanham o código com suas licenças. Use `build.ps1` ou:

```powershell
cmake -S . -B build
cmake --build build --config Release --target Aurora_VST3 AuroraFX_VST3 Aurora_Standalone AuroraTests --parallel 3
```

Para executar a suíte, passe ao `AuroraTests.exe` os caminhos dos bundles VST3 do instrumento e do efeito. O relatório desta entrega está em [validation.txt](validation.txt).

## Licença

O código Aurora é distribuído sob GPL-3.0-or-later. As dependências e os dados de referência têm proveniência documentada em [THIRD_PARTY.md](THIRD_PARTY.md). O projeto não é afiliado a fabricantes de outros instrumentos.
