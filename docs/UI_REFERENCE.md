# Interface 0.2.2 baseada no UIDesc fornecido

A tela principal usa a geometria do pacote `serum2-ui-decompilada.zip` fornecido pelo usuário. O UIDesc normalizado tem SHA-256 `3beca5b6da0a8b622ce0012cc800579417a57951e6fbb65dbb63639c7cf574f7`. O manifesto reduzido está em `ui-reference-layout.json`; as coordenadas compiláveis estão em `Source/ReferenceLayout.h`.

Base lógica: 1190 × 740. Cabeçalho: 69; faixa de osciladores/filtro: 318; modulação: 265; teclado: faixa final de 90. Sub: largura 76; cada oscilador: 270; Noise: 100; filtro compartilhado: 204. As coordenadas dos três osciladores são consultadas diretamente no manifesto compilado. Filtros 1/2 compartilham o mesmo espaço e alternam por abas, preservando o estado de ambos.

O redimensionamento da janela mantém a proporção 1190/740. Na tela principal os componentes usam um espaço lógico único e uma escala comum; não há rolagem externa. A navegação MIX/FX/MATRIX/GLOBAL e o browser conservam os editores existentes adaptados à nova moldura; não são reconstruções integrais das páginas estrangeiras.

## Vínculos

Os nomes/tags do UIDesc são apenas evidência geométrica. Nenhum ID estrangeiro é passado ao APVTS. Engine, Position, Level, Tune, Pan, Grain, Unison, Detune, Warp, Phase e Random usam `osc{0..2}_*`; filtro usa `filter{0..1}_*` e `route{0..6}_*`. ENV/LFO, macros, master e voicing continuam usando os parâmetros próprios existentes. Sub/Noise exibem os níveis reais e o balanço de envio aos filtros. Seus pequenos desenhos são ilustrações estáticas, não medidores de áudio. A animação do filtro continua usando a telemetria do DSP.

Há adaptações intencionais: controles sem implementação correspondente no Aurora não são simulados; importação de áudio/SFZ e seleção de rota ficam acessíveis na faixa sob o oscilador. A marca é Aurora. O pacote de referência não contém os 344 bitmaps referenciados pela skin; os gráficos e controles são desenhados pelo Aurora, portanto não há reprodução pixel a pixel da skin original.

## Verificação

A suíte UI confere as três posições/larguras de osciladores, alternância e exclusividade das duas abas de filtro, contenção dos knobs na tela principal e ausência de rolagem em larguras 660/960/1190/1600. Mantém navegação das seis páginas, drag-and-drop de modulação, reordenação FX, MIDI learn e undo/redo. Capturas normal/reduzida são geradas para revisão visual. O DSP e o formato de estado não foram alterados nesta revisão.
