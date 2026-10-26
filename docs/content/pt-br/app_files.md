# Arquivos e configurações

Esta página mostra onde o WhiteoutDex guarda os seus arquivos. Normalmente você não precisa abri-los. Eles ajudam quando algo dá errado ou quando você muda para um novo computador.

## A pasta de instalação

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Pasta ou arquivo | O que contém |
|---|---|
| `native plugins\Max<year>\` | Os plug-ins para cada versão do 3ds Max (importador, exportador, partículas, rastros, pré-visualizador, leitor de BLP). |
| `scripts\` | As ferramentas do WhiteoutDex (MAXScript). |
| `lang\` | As traduções do pré-visualizador. |
| `whiteoutdex_icons\` | Os ícones da barra lateral e das ferramentas. |
| `maps\WhiteoutDex\TeamGlow\` | As texturas de brilho de equipe. |
| `version.txt` | A versão instalada. |

Não altere os arquivos daqui. Uma atualização os substitui.

## As suas configurações

As suas configurações ficam na pasta de configurações do 3ds Max **plugcfg**. Para encontrá-la, abra o MAXScript Listener ([[key:F11]]) e digite `getDir #plugcfg`.

| Arquivo | O que guarda |
|---|---|
| `WhiteoutDex_Settings.ini` | Idioma, pasta do Reforged, ordem dos MPQ, barra lateral, verificação de atualizações. |
| `WhiteoutDexMPQ.ini` | Pasta do jogo clássico. |
| `WhiteoutDexImporter.ini` | As suas últimas opções de importação. |
| `MDLXExporter.ini` | As suas últimas opções de exportação. |
| `WhiteoutDex_MaterialFix.ini` | As configurações da *Correção de materiais* para SD e HD. |

!!! tip
    Para voltar o WhiteoutDex ao estado do primeiro início, feche o 3ds Max e apague estes arquivos. O WhiteoutDex cria arquivos novos com os valores padrão.

## O pré-visualizador

| Arquivo | O que guarda |
|---|---|
| `WhiteoutFlakes.ini` (ao lado de `3dsmax.exe`) | As configurações do pré-visualizador WhiteoutFlakes. |

## Arquivos de log

Quando algo dá errado, estes arquivos dizem o que aconteceu. Eles ficam na sua pasta temporária. Para abri-la, pressione [[key:Win+R]], digite `%TEMP%` e pressione [[key:Enter]].

| Arquivo | Escrito por |
|---|---|
| `mdlx_import_debug.log` | O importador |
| `mdlx_export_debug.log` | O exportador, com a lista completa de avisos |
| `WhiteoutDex_render.log` | O pré-visualizador WhiteoutFlakes |

Envie estes arquivos quando você relatar um problema.
