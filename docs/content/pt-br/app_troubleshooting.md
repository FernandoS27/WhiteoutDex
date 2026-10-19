# Problemas e soluções

## O menu WhiteoutDex não aparece

- Verifique se a sua versão do 3ds Max está entre 2016 e 2027.
- Instale o WhiteoutDex de novo com o 3ds Max fechado.
- O 3ds Max 2026 e mais novos precisam do **.NET 8**. Instale-o pela Microsoft e inicie o 3ds Max de novo.

## O modelo não tem texturas (fica branco ou rosa)

- Abra [[t:menu_settings]] e verifique as pastas do jogo. A linha de estado precisa dizer que os arquivos foram encontrados.
- Modelos clássicos procuram nos arquivos MPQ, modelos Reforged no CASC. Defina a pasta do jogo de onde o modelo vem.
- As texturas de modelos personalizados precisam estar ao lado do arquivo do modelo ou em uma subpasta com o caminho que o modelo usa (por exemplo `Textures\`).

## O modelo parece virado do avesso no jogo

Os triângulos estão virados para o lado errado. O exportador inverte as malhas que estão viradas contra as suas próprias normais. Se uma parte ainda parecer errada, selecione-a e use **Flip** nas normais dela no 3ds Max.

## O modelo fica invisível ou pisca na borda da tela

As **extensões** (o tamanho de cada animação) são pequenas demais. O exportador as mede em cada quadro. Se você moveu o modelo depois da importação, exporte de novo. O exportador substitui as extensões que não contêm o modelo.

## Partes do modelo não se movem com os ossos

- A malha precisa de um modificador **Skin** com os ossos.
- Na janela de importação, a opção *Com skin* precisa estar ligada.
- Para modelos clássicos, ligue *Quantizar pesos de skin* na janela de exportação.

## A exportação mostra avisos

O relatório de exportação lista o que precisa da sua atenção. Cada linha cita o objeto. A lista completa está em `mdlx_export_debug.log` (veja *Arquivos e configurações*).

## O 3ds Max mostra "Startup Failure Detection"

O 3ds Max não fechou direito da última vez. Clique em **Continue Without Restore**. Não escolha *Restore Factory Settings*, a não ser que você queira redefinir todo o 3ds Max.

## Onde consigo ajuda?

- **Discord:** o servidor do WhiteoutDex (link em [[t:menu_about]]).
- **Hive Workshop:** o tópico do WhiteoutDex.
- **GitHub:** relate erros na página *Issues*. Anexe os arquivos de log de *Arquivos e configurações*.
