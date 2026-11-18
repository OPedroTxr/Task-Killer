/*
 * Task Killer — sua lista de tarefas do dia, direto na área de trabalho.
 *
 * C puro + API Win32, sem frameworks nem runtime extra.
 * Este é o único arquivo compilado ("unity build"): ele inclui os módulos
 * na ordem de dependência.
 *
 *   util.c     memória, texto, datas
 *   data.c     tarefas, grupos, arquivos TSV, histórico, configuração
 *   parse.c    linguagem natural da adição rápida
 *   ui.c       tema, GDI+ sob demanda, componentes visuais
 *   overlay.c  janelas transparentes na área de trabalho
 *   sched.c    lembretes e alarmes
 *   popups.c   adição rápida, mini calendário, alarme
 *   picker.c   seletor de hora e seletor de cores
 *   mainwin.c  painel principal
 *   ics.c      calendário (.ics)
 *   app.c      ações, menus, bandeja, entrada
 */
#include "taskkiller.h"
#include "util.c"
#include "data.c"
#include "parse.c"
#include "ui.c"
#include "overlay.c"
#include "sched.c"
#include "popups.c"
#include "picker.c"
#include "mainwin.c"
#include "ics.c"
#include "app.c"
