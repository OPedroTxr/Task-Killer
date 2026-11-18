# Task Killer

Sua lista de tarefas escrita direto na área de trabalho, com **grupos**, um **painel completo**, seletor de horário livre e seletor de cores. (Antes se chamava *The Method* e, antes disso, *Risca*.)

## Instalar ou atualizar

1. Dê dois cliques em **`build.bat`**. Ele fecha o app se estiver aberto (inclusive os antigos `TheMethod.exe` e `Risca.exe`), encontra o Visual Studio (MSVC) ou o MinGW e gera o **`TaskKiller.exe`**.
2. Coloque o `TaskKiller.exe` numa pasta fixa (ex.: `%LOCALAPPDATA%\Programs\Task Killer`) e abra.

**Vindo do The Method ou do Risca?** Na primeira vez, o Task Killer move seus dados de `%APPDATA%\The Method` (ou `%APPDATA%\Risca`) para `%APPDATA%\Task Killer` e troca a entrada "Iniciar com o Windows" para o novo nome. Depois é só apagar o `TheMethod.exe` (ou `Risca.exe`) antigo.

## Na área de trabalho

- **Clique** numa tarefa para concluir (risca, mostra "feita" e some).
- **Clique direito** numa tarefa: concluir, abrir detalhes, adiar, mover de grupo, importante, duplicar, excluir.
- **Organização** (bandeja → Organização, ou Configurações):
  - *Uma janela*: todos os grupos juntos, cada um com seu título colorido.
  - *Uma por grupo*: cada grupo vira uma janela transparente própria. Desmarque *Travar posições*, arraste cada uma para onde quiser e dê duplo clique para travar.
- Tarefas marcadas como importantes aparecem com o marcador na cor do grupo.

## Adição rápida — `Ctrl + Alt + T`

Escreva do jeito que você fala. O Task Killer entende e mostra o resultado em "chips" que você pode ajustar com um clique:

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

Clique no ícone ✔ da bandeja (ou abra o `TaskKiller.exe` de novo).

- **Hoje** com indicadores: para hoje (com barra de progresso), atrasadas, concluídas hoje e próximos 7 dias.
- **Próximos 7 dias**, **Todas** (separadas por grupo), **Concluídas** e **Histórico** (tudo o que já foi concluído ou excluído, com opção de restaurar).
- **Grupos** na barra lateral: `+` cria, duplo clique renomeia, clique direito para cor, ordem, grupo padrão, mostrar/ocultar da área de trabalho e excluir (as tarefas vão para o grupo padrão).
- **Detalhes** à direita: título, grupo, tipo, data (mini calendário), hora, repetição, importante e **notas**. Tudo salva sozinho.
- **Busca** que ignora acentos (`Ctrl+F`).
- Ao concluir ou excluir, aparece **Desfazer** por alguns segundos.
- **Configurações**: organização, travar posição, data, sombra, largura, opacidade, fonte, cor do texto, tema do painel (Sistema/Claro/Escuro), iniciar com o Windows, calendário e dados.

Teclado no painel: `↑ ↓` navega · `Espaço` conclui · `Enter` edita o título · `Delete` exclui · `Esc` fecha os detalhes · `Ctrl+N` nova tarefa.

## Lista ou cards

No painel, o seletor **Lista | Cards** (na barra acima das tarefas) troca o jeito de ver:

- **Lista:** como sempre, com seções.
- **Cards:** uma **grade dinâmica de blocos**, um por **grupo** (categoria) ou por **estado** (escolha em *Colunas: …*).
  - Cada bloco tem a altura das próprias tarefas e é encaixado onde houver espaço; grupos/estados sem tarefas não aparecem.
  - A quantidade de colunas acompanha a largura da janela.
  - Cada card mostra título, prazo (com o selo de destaque), a outra categoria como selo, importante, repetição e a primeira linha das notas.
  - **Arraste um card para outro bloco** para trocar o grupo ou o estado.
  - Clique abre os detalhes, a bolinha conclui, duplo clique edita o título, clique direito abre o menu.
  - O **+** no topo do bloco cria uma tarefa já naquele grupo/estado; clique direito no título do bloco abre as opções.
- Ordenação, filtros e busca valem nos dois modos. O Histórico é sempre em lista. A escolha fica salva.

## Progresso

Item **Progresso** na barra lateral, com o que importa:

- **Concluídas em 7 dias**, comparando com a semana anterior.
- **Entregues no prazo**: quantas das concluídas nos últimos 30 dias saíram antes do prazo.
- **Sequência**: dias seguidos com pelo menos uma tarefa concluída.
- **Atrasadas agora** (clique para ver a lista).
- **Atividade**: gráfico de barras com as concluídas por dia nas últimas 2 semanas.
- **Pendentes por estado**: barra com a distribuição nas cores dos estados.
- **Andamento por grupo**, no estilo das milestones do GitHub: porcentagem, "X de Y concluídas", abertas, atrasadas e o próximo prazo. Clique para abrir o grupo. Tarefas que se repetem entram só na atividade (elas não "terminam").

O histórico é lido só enquanto essa tela está aberta.

## Repetir em dias da semana

Em **Repetir → Dias da semana…** (nos detalhes ou na adição rápida) abre uma janela com **Seg Ter Qua Qui Sex Sáb Dom**: marque um ou mais dias. Há atalhos para *Dias úteis*, *Fim de semana* e *Todos*, e as teclas 1–7 marcam seg–dom.

Na adição rápida, escreva do jeito natural:

| Você escreve | Vira |
|---|---|
| `Academia toda seg, qua e sex 7h` | repete seg, qua e sex às 07:00 |
| `Inglês todas as terças e quintas 19h` | repete ter e qui |
| `Futebol às segundas e quartas` | repete seg e qua |
| `Natação nas terças e quintas` | repete ter e qui |

Ao concluir, a tarefa vai para o próximo dia marcado (ex.: concluída na quarta → sexta). A exportação `.ics` leva os dias (`BYDAY`), e a importação entende eventos com vários dias.

## Estados de andamento

Cada tarefa tem um estado, com cor própria. Vêm quatro prontos:

| Estado | Cor |
|---|---|
| Aguardando | branco |
| Em andamento | amarelo |
| Em revisão | azul |
| Pausado | vermelho |

- **Trocar o estado:** clique no selo colorido da tarefa (na lista ou embaixo do título, nos detalhes), use o clique direito → *Estado* na área de trabalho, ou escreva `@estado` na adição rápida (`Proposta do cliente @andamento`; basta um pedaço do nome). Também há um chip de estado na adição rápida.
- **Criar e personalizar:** Configurações → **Estados** (até 16):
  - clique na bolinha para escolher a cor (abre o seletor de cores);
  - duplo clique no nome ou no ✎ para renomear;
  - ★ define o estado padrão das novas tarefas; setas mudam a ordem; a lixeira exclui (as tarefas vão para o estado padrão).
- **Na área de trabalho** (Configurações → Área de trabalho → *Estado na área de trabalho*): o marcador da tarefa fica na cor do estado (*Cor*), com o nome ao lado (*Cor e nome*), ou nada (*Não mostrar*). O destaque de prazo próximo continua tendo prioridade na cor do marcador.
- Tarefas das versões anteriores começam como **Aguardando**. Os estados ficam em `estados.tsv`.

## Ordenar e filtrar (painel)

Acima da lista há uma barra com:

- **Ordem:** data (padrão), ordem alfabética A–Z, importância (importantes primeiro) ou estado (agrupa por estado, na ordem definida nas configurações). A escolha fica salva.
- **Estado:** mostra só um estado.
- **Importância:** só importantes ou só as não importantes.
- **Data:** atrasadas, hoje, próximos 7 dias, este mês ou sem data.
- **Limpar filtros** aparece quando há algum filtro ativo.

Os filtros se combinam entre si e com a busca, e valem em todas as listas (Hoje, 7 dias, Todas, grupos, Concluídas e Histórico).

## Prazos

Toda tarefa com data tem um prazo: a data e a hora, ou o fim do dia quando não tem hora.

- **Mostrar na área de trabalho** (Configurações → Tarefas e prazos): *Só hoje*, *Próximos 7 dias* ou *Todas*, qualquer que seja a data.
- **Exibir, tarefa por tarefa:** cada tarefa pode ignorar a regra geral. Escolha nos detalhes (linha *Exibir*), no clique direito (*Exibir na área de trabalho*) ou na adição rápida:
  - *Padrão*: segue a regra geral;
  - *Sempre* (`!sempre`): aparece em qualquer data;
  - *7 dias antes* (`!semana`): só na semana do prazo;
  - *Só no dia* (`!nodia`): só no dia do prazo;
  - *Só no app* (`!oculta`): nunca vai para a área de trabalho.

  No painel, um ícone de olho riscado marca as tarefas que estão escondidas da área de trabalho por essa escolha.
- **Destaque conforme o prazo chega**, com contagem regressiva ao lado da tarefa:
  - até 3 dias antes: rótulo âmbar suave (`em 2 dias`);
  - últimas 24 h: âmbar mais forte, marcador e horário coloridos (`em 5h`);
  - últimas 2 h: vermelho e texto em negrito (`em 1h30`, `em 25 min`).
- **Concluir ao vencer o prazo** (ligado por padrão): quando o prazo passa, a tarefa é riscada com a animação de "feita" e vai para **Concluídas**, marcada como *prazo encerrado*. Para trazer de volta, restaure normalmente; uma tarefa restaurada depois do prazo não é concluída de novo. Tarefas que já estavam vencidas antes de você instalar esta versão não são mexidas.
- No painel, o selo colorido aparece na lista e o indicador **Prazos próximos** conta o que vence em 24 h.

O app só acorda quando algo muda de fato (um nível de destaque, um rótulo, um prazo ou um lembrete). O rótulo em minutos é atualizado uma vez por minuto, e só na última hora.

## Notas e descrição

- **No painel:** campo *Notas e descrição* nos detalhes da tarefa.
- **Na adição rápida:** tudo depois de `//` vira nota: `Reunião amanhã 14h // levar os slides`. Links como `https://` não são confundidos.
- **Na área de trabalho:** clique direito na tarefa → *Adicionar notas…* ou *Editar notas…*.
- **Exibir notas** (Configurações → Tarefas e prazos):
  - *Fixas*: até 3 linhas aparecem embaixo da tarefa, na própria área de trabalho;
  - *Ao passar o mouse* (padrão): a tarefa ganha um ✎ e, ao parar o mouse sobre ela, abre um cartão com grupo, prazo e as notas completas, que some quando o mouse sai;
  - *Só no app*: as notas aparecem apenas no painel.

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

`%APPDATA%\Task Killer`, em texto puro UTF-8:

- `tarefas.tsv`: tarefas ativas e concluídas dos últimos 7 dias.
- `grupos.tsv`: nome, cor, ordem e posição da janela de cada grupo.
- `estados.tsv`: nome, cor e ordem de cada estado.
- `historico.tsv`: tudo que foi concluído ou excluído, para sempre (só cresce no final).
- `config.ini`: aparência e posições.

Toda gravação é atômica (escreve num `.tmp` e troca), então uma queda de energia não corrompe nada.

## Ícone e marca

O ícone do programa (área de trabalho, barra de tarefas, bandeja) e o logo do painel vêm dos SVGs da marca. O nome no painel usa a fonte **Dainty Fusion**, embutida no `.exe` e carregada só enquanto o painel está aberto, sem instalar nada no Windows.

## Por que continua leve

- **C + Win32**, sem framework nem runtime. Um único arquivo é compilado (os módulos são incluídos nele), o que deixa o `.exe` pequeno.
- **0% de CPU parado.** Nada fica verificando em segundo plano: um único timer acorda o app na hora exata do próximo lembrete ou na virada do dia. As animações duram menos de 2 segundos.
- As janelas da área de trabalho continuam em GDI puro, e a imagem é descartada depois de desenhada.
- O **painel, os pop-ups e os seletores só existem enquanto estão abertos.** O GDI+ (para cantos e círculos suaves) é carregado ao abrir e descarregado ao fechar, junto com fontes e buffers. Com o painel fechado, o Task Killer volta ao consumo mínimo e devolve a memória ociosa ao Windows.

## Desinstalar

Configurações → desligue *Iniciar com o Windows* → bandeja → *Sair*. Apague o `TaskKiller.exe` e, se quiser, `%APPDATA%\Task Killer`.
