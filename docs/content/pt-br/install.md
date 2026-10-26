# Instalação

Um único instalador coloca o WhiteoutDex em todas as versões do 3ds Max do seu computador (2016 a 2027).

## Instalar

1. Feche o 3ds Max.
2. Baixe o `WhiteoutDex_Setup_v<version>.exe` mais recente. Você o encontra na página **Releases** do GitHub e no Hive Workshop.
3. Inicie o instalador. O Windows pede direitos de administrador. Clique em **Sim**.
4. Escolha o seu idioma e siga os passos. O instalador encontra sozinho todas as suas versões do 3ds Max.
5. Inicie o 3ds Max. Agora você vê o menu **WhiteoutDex** e a barra lateral do WhiteoutDex.

!!! note
    O 3ds Max 2026 e mais novos precisam do **.NET 8**. O instalador avisa você se ele estiver faltando.

## Onde ficam os arquivos

O WhiteoutDex é instalado em uma pasta:

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

O 3ds Max carrega tudo de lá. Você não precisa copiar nenhum arquivo.

As suas configurações pessoais (idioma, pastas do jogo, últimas opções de exportação) ficam na pasta de configurações do 3ds Max `plugcfg`. Elas continuam lá quando você atualiza o WhiteoutDex.

## Atualizar

O WhiteoutDex pode procurar novas versões quando o 3ds Max inicia (3ds Max 2018 e mais novos). Quando sai uma nova versão, ele pergunta a você:

- Clique em **Sim** para baixar e instalar. O 3ds Max fecha para a atualização.
- Clique em **Não** para continuar trabalhando. Ele pergunta de novo no dia seguinte.

Você pode desligar isso ou verificar agora em [[t:menu_settings]].

## Desinstalar

Abra **Configurações do Windows > Aplicativos**, encontre o **WhiteoutDex** e clique em **Desinstalar**.
