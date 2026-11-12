# The Method

Sua lista de tarefas escrita direto na área de trabalho, com **grupos**, um **painel completo**, seletor de horário livre e seletor de cores. (Antes se chamava *Risca*.)

## Instalar ou atualizar

1. Dê dois cliques em **`build.bat`**. Ele fecha o app se estiver aberto (inclusive o antigo `Risca.exe`), encontra o Visual Studio (MSVC) ou o MinGW e gera o **`TheMethod.exe`**.
2. Coloque o `TheMethod.exe` numa pasta fixa (ex.: `%LOCALAPPDATA%\Programs\The Method`) e abra.

**Vindo do Risca?** Na primeira vez, o The Method move seus dados de `%APPDATA%\Risca` para `%APPDATA%\The Method` e troca a entrada "Iniciar com o Windows" para o novo nome. Depois é só apagar o `Risca.exe` antigo.

## Na área de trabalho

- **Clique** numa tarefa para concluir (risca, mostra "feita" e some).
- **Clique direito** numa tarefa: concluir, abrir detalhes, adiar, mover de grupo, importante, duplicar, excluir.
- **Organização** (bandeja → Organização, ou Configurações):
  - *Uma janela*: todos os grupos juntos, cada um com seu título colorido.
  - *Uma por grupo*: cada grupo vira uma janela transparente própria. Desmarque *Travar posições*, arraste cada uma para onde quiser e dê duplo clique para travar.
- Tarefas marcadas como importantes aparecem com o marcador na cor do grupo.

## Adição rápida — `Ctrl + Alt + T`

Escreva do jeito que você fala. O The Method entende e mostra o resultado em "chips" que você pode ajustar com um clique:

| Você escreve | Vira |
|---|---|
| `Reunião amanhã às 14h #trabalho !lembrete` | Lembrete amanhã 14:00 no grupo Trabalho |
| `Academia toda segunda 7h !alarme` | Alarme semanal, segundas 07:00 |
| `Pagar boleto 15/11 !!` | Tarefa importante em 15/11 |
| `Tomar remédio todo dia 8h30` | Repete todo dia, 08:30 |
| `Entregar relatório na sexta` | Na próxima sexta |

Também entende `hoje`, `depois de amanhã`, `dias úteis`, `toda semana`, `todo mês`, `meio-dia`. Um `#grupo` que não existe é criado na hora.
Clique em **Hora** para abrir o seletor de horário (qualquer hora e minuto). Clique em **Data** para o mini calendário.
**Enter** adiciona · **Ctrl+Enter** adiciona e abre os detalhes · **Esc** fecha.

## O painel

Clique no ícone ✔ da bandeja (ou abra o `TheMethod.exe` de novo).

- **Hoje** com indicadores: para hoje (com barra de progresso), atrasadas, concluídas hoje e próximos 7 dias.
- **Próximos 7 dias**, **Todas** (separadas por grupo), **Concluídas** e **Histórico** (tudo o que já foi concluído ou excluído, com opção de restaurar).
- **Grupos** na barra lateral: `+` cria, duplo clique renomeia, clique direito para cor, ordem, grupo padrão, mostrar/ocultar da área de trabalho e excluir (as tarefas vão para o grupo padrão).
- **Detalhes** à direita: título, grupo, tipo, data (mini calendário), hora, repetição, importante e **notas**. Tudo salva sozinho.
- **Busca** que ignora acentos (`Ctrl+F`).
- Ao concluir ou excluir, aparece **Desfazer** por alguns segundos.
- **Configurações**: organização, travar posição, data, sombra, largura, opacidade, fonte, cor do texto, tema do painel (Sistema/Claro/Escuro), iniciar com o Windows, calendário e dados.

Teclado no painel: `↑ ↓` navega · `Espaço` conclui · `Enter` edita o título · `Delete` exclui · `Esc` fecha os detalhes · `Ctrl+N` nova tarefa.

## Seletor de horário

Qualquer horário, de 00:00 a 23:59:

- Clique numa hora da grade e depois num minuto (de 5 em 5) e pronto.
- Para minutos exatos, use as setinhas acima e abaixo dos números, a roda do mouse ou digite direto (`1437` vira 14:37). `PgUp`/`PgDn` mudam a hora e `↑`/`↓` o minuto.
- **Agora** usa o horário atual; **Sem horário** limpa.

No painel de detalhes, o campo Hora continua aceitando digitação (`14:30`, `1430`, `9`, `14h`) e tem um botão de relógio que abre o seletor.

## Seletor de cores

Em Configurações → Cor do texto (botão `+`) e em qualquer grupo → Cor → *Personalizada…*:

- **Anel** escolhe o matiz; **triângulo** escolhe saturação e brilho (canto da cor pura, branco e preto).
- Barras **R, G, B** com campo numérico, e campo **HEX**.
- A área de trabalho muda **ao vivo** enquanto você escolhe. **Cancelar** (ou `Esc`) volta à cor anterior.
- Clique no quadrado de trás para voltar à cor original.
- `+` salva a cor atual em **Salvas** (até 8). Clique direito numa cor salva para removê-la.
- Arraste a janela pelo fundo.

## Lembretes e alarmes

*Lembrete* mostra a notificação do Windows. *Alarme* abre uma janela no canto da tela com som, **Parar** e **Adiar 5 min**. Ela não bloqueia o resto do app; vários alarmes no mesmo minuto aparecem juntos.

## Calendário

Configurações → Dados e calendário: **Exportar** gera um `.ics` com data, hora, repetição, alerta, grupo (como categoria) e notas, para Google Agenda, Outlook ou Calendário do Windows. **Importar** traz eventos de hoje em diante e respeita a categoria como grupo.

## Onde ficam os dados

`%APPDATA%\The Method`, em texto puro UTF-8:

- `tarefas.tsv`: tarefas ativas e concluídas dos últimos 7 dias.
- `grupos.tsv`: nome, cor, ordem e posição da janela de cada grupo.
- `historico.tsv`: tudo que foi concluído ou excluído, para sempre (só cresce no final).
- `config.ini`: aparência e posições.

Toda gravação é atômica (escreve num `.tmp` e troca), então uma queda de energia não corrompe nada.

## Por que continua leve

- **C + Win32**, sem framework nem runtime. Um único arquivo é compilado (os módulos são incluídos nele), o que deixa o `.exe` pequeno.
- **0% de CPU parado.** Nada fica verificando em segundo plano: um único timer acorda o app na hora exata do próximo lembrete ou na virada do dia. As animações duram menos de 2 segundos.
- As janelas da área de trabalho continuam em GDI puro, e a imagem é descartada depois de desenhada.
- O **painel, os pop-ups e os seletores só existem enquanto estão abertos.** O GDI+ (para cantos e círculos suaves) é carregado ao abrir e descarregado ao fechar, junto com fontes e buffers. Com o painel fechado, o The Method volta ao consumo mínimo e devolve a memória ociosa ao Windows.

## Desinstalar

Configurações → desligue *Iniciar com o Windows* → bandeja → *Sair*. Apague o `TheMethod.exe` e, se quiser, `%APPDATA%\The Method`.
# The Method
