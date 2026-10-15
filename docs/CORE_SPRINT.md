# Reproduzir a sprint

Requer C++17, CMake, JUCE 7.0.12 com os patches fornecidos, Zstandard incluído e um toolchain Windows x64. `build.ps1` compila instrumento, FX, Standalone e testes. O CMake usa por padrão `third_party/JUCE`; não substitua por JUCE sem os patches de fila/IDs em `juce_AudioProcessor.h` e `juce_audio_plugin_client_VST3.cpp`.

Execute os testes a partir da pasta que contém `outputs/Aurora`, passando as pastas dos dois VST3 compilados. O host nativo do teste configura maxBlock=1024 antes de enviar ProcessData com 1024 amostras. As comparações de convolução incluem IRs de 1,17,127,128,129,257 e 509 amostras e uma referência FIR independente. Há teste de invariância da automação entre blocos 64/1024 e verificação da fila VST3 real.

Exemplo de ferramenta offline (Python 3.14, só biblioteca padrão):

```powershell
python tools/preset_tool.py inspect "FactoryPresets/Aurora Factory 001.aurora" preset.json
python tools/preset_tool.py pack preset.json regravado.aurora
```

Geração reproduzível do banco original, usando o executável compilado:

```powershell
AuroraTests.exe --factory="C:/caminho/Aurora/FactoryPresets"
```

O gerador também exporta `docs/aurora-parameter-catalog.json` (792 IDs Aurora, ranges, defaults e unidades; 35 mapeamentos canônicos explícitos). O catálogo de 2.637 IDs estrangeiros é um inventário estático, não o registro de parâmetros do host. Parâmetros sem mapeamento explícito são rejeitados pelo adaptador.

Resultados: `validation.txt`, `docs/factory-validation.txt`, `docs/offline-validation.txt`. A investigação não inclui comparação auditiva/diferencial com o original. O MPE funcional foi exercitado por eventos sintéticos; hardware, conformidade oficial e anúncio nativo de todas as expressões permanecem pendentes. Consulte `GAP_ANALYSIS.md` antes de interpretar os resultados como equivalência.

Na revisão 0.2.1, os patches JUCE também incluem o anúncio INoteExpressionController e o acesso do host de testes ao controller via ExtensionsVisitor. A série Bode recuperada é validada por bits e quadratura em 48 kHz; o teste de frequência verifica +100 Hz com rejeição de banda lateral. A enumeração runtime está em runtime-host-parameters.json; não se transforma amostras de conversão em fórmulas presumidas.
