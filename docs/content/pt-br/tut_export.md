# Tutorial: Exportar um modelo

Agora você salva o modelo de novo como um arquivo do Warcraft III.

**Você precisa de:** a cena da lição *Importar um modelo*.

## Passo 1: Iniciar a exportação

1. Escolha **WhiteoutDex > [[t:menu_exporter]]**.
2. Escolha uma pasta e um nome de arquivo, por exemplo `MyPaladin.mdx`. Clique em **Save**.

A janela de exportação abre.

![A janela de exportação. Clássico está selecionado.](exporter.png)

## Passo 2: Escolher o jogo

- Para o jogo clássico, escolha **Clássico**. As texturas viram `.blp`.
- Para o Reforged, escolha **Reforged**. As texturas viram `.dds`.

O Paladino é um modelo clássico. Então escolha **Clássico**.

## Passo 3: Verificar a cena

Olhe a caixa acima dos botões:

- **Verde – Nenhum problema:** continue.
- **Amarela – problemas:** clique em [[t:exp_fix_all_btn]]. O WhiteoutDex corrige a cena. Veja *Problemas da cena e Corrigir tudo*.

## Passo 4: Exportar

1. Na aba *Texturas*, verifique se *Converter as texturas na exportação* está ligada.
2. Clique em **Exportar**.

O arquivo `MyPaladin.mdx` e as suas texturas estão agora na sua pasta. Se algo precisar da sua atenção, o relatório de exportação abre (veja *O relatório de exportação*).

## Passo 5: Testar no jogo

1. Copie o modelo e as suas texturas para a pasta do seu mapa. Ou importe-os com o **Import Manager** do World Editor.
2. No World Editor, dê o seu modelo a uma unidade no *Object Editor* (campo *Art – Model File*).
3. Teste o mapa.

!!! tip
    O exportador lembra todas as opções. A próxima exportação do mesmo modelo precisa de um só clique.
