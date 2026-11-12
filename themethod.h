/*
 * The Method — cabeçalho comum.
 *
 * Compilação "unity build": só o themethod.c é compilado; ele inclui os módulos
 * na ordem certa. Tudo é 'static' (uma única unidade de tradução), o que
 * deixa o executável menor e permite ao compilador otimizar o programa inteiro.
 */
#ifndef THEMETHOD_H
#define THEMETHOD_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#undef WINVER
#undef _WIN32_WINNT
#undef _WIN32_IE
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define _WIN32_IE    0x0A00
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <math.h>
#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#endif
/* gdiplus.dll, dwmapi.dll e uxtheme.dll são carregadas sob demanda. */

/* ------------------------------------------------------------------------ */
/* Constantes                                                               */
/* ------------------------------------------------------------------------ */

#define APP          L"The Method"
#define CLS_OVERLAY  L"TheMethodOverlay"
#define CLS_MSG      L"TheMethodMsg"
#define CLS_MAIN     L"TheMethodPainel"
#define CLS_QUICK    L"TheMethodRapida"
#define CLS_CAL      L"TheMethodCalendario"
#define CLS_ALARM    L"TheMethodAlarme"
#define CLS_TIME     L"TheMethodHorario"
#define CLS_COLOR    L"TheMethodCor"

#define WM_TRAY      (WM_APP + 1)
#define WM_APPOPEN   (WM_APP + 2)
#define WM_RECREATE  (WM_APP + 3)
#define WM_EDITKEY   (WM_APP + 4)   /* wParam = id do controle, lParam = tecla */

enum { TMR_ANIM = 1, TMR_SCHED, TMR_TRIM, TMR_RECREATE, TMR_TOAST, TMR_ALARMSTOP };
enum { T_TASK = 0, T_REMIND = 1, T_ALARM = 2 };
enum { S_PENDING = 0, S_DONE = 1, S_DELETED = 2 };
enum { R_NONE = 0, R_DAILY, R_WEEKDAYS, R_WEEKLY, R_MONTHLY };
enum { A_NONE = 0, A_DONE, A_IN, A_OUT };
enum { V_TODAY = 0, V_WEEK, V_ALL, V_DONE, V_HISTORY, V_GROUP, V_SETTINGS };

#define TEXT_MAX       200
#define NOTES_MAX      4000
#define NAME_MAX       40
#define MAX_GROUPS     48
#define MENU_LIST_MAX  20
#define POS_UNSET      (-32000)
#define GF_HIDDEN      1          /* grupo não aparece na área de trabalho */

#define FT_SEC  (10000000ULL)
#define FT_MIN  (600000000ULL)
#define FT_DAY  (864000000000ULL)

static const int    kWidths[4]     = { 300, 380, 460, 560 };
static const WCHAR *kWidthName[4]  = { L"Estreita", L"Média", L"Larga", L"Extra" };
static const int    kOpac[4]       = { 255, 217, 178, 140 };
static const WCHAR *kOpacName[4]   = { L"100%", L"85%", L"70%", L"55%" };
static const WCHAR *kTypeName[3]   = { L"Tarefa", L"Lembrete", L"Alarme" };
static const WCHAR *kRecurName[5]  = { L"Não repete", L"Todo dia", L"Dias úteis", L"Toda semana", L"Todo mês" };

/* cores dos grupos (funcionam no tema claro e no escuro) */
static const COLORREF kPalette[8] = {
    RGB(122, 162, 255), RGB(179, 140, 255), RGB(255, 122, 182), RGB(255, 122, 122),
    RGB(255, 170,  92), RGB(232, 196,  72), RGB( 94, 210, 156), RGB( 79, 209, 217)
};
static const WCHAR *kPaletteName[8] = {
    L"Azul", L"Violeta", L"Rosa", L"Vermelho", L"Laranja", L"Amarelo", L"Verde", L"Turquesa"
};
/* sugestões de cor para o texto na área de trabalho */
static const COLORREF kTextSwatch[8] = {
    RGB(255, 255, 255), RGB(217, 220, 227), RGB(30, 31, 36), RGB(122, 162, 255),
    RGB(179, 140, 255), RGB(94, 210, 156), RGB(255, 170, 92), RGB(255, 122, 182)
};

/* ------------------------------------------------------------------------ */
/* Tipos                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    DWORD     id, parent, group;
    BYTE      status, type, recur, notified, prio;
    DWORD     date;              /* AAAAMMDD  (0 = sem data)     */
    int       time;              /* HHMM      (-1 = sem horário) */
    ULONGLONG created, done;     /* AAAAMMDDHHMM                 */
    WCHAR     text[TEXT_MAX];
    WCHAR    *notes;             /* heap, CRLF; NULL = sem notas */
    BYTE      anim;              /* só em memória */
    DWORD     animStart;
} Item;

typedef struct {
    DWORD    id;
    int      order;
    COLORREF color;
    int      x, y;               /* posição da janela própria na área de trabalho */
    DWORD    flags;
    WCHAR    name[NAME_MAX];
} Group;

typedef struct {
    int      x, y, width;
    WCHAR    face[LF_FACESIZE];
    int      size10, weight, italic;
    COLORREF color;
    int      shadow, showDate, locked, opacity;
    int      layout;             /* 0 = uma janela; 1 = uma por grupo */
    int      hideEmpty;
    int      theme;              /* 0 = sistema; 1 = claro; 2 = escuro */
    int      mx, my, mw, mh, mmax;
    DWORD    defGroup;
    COLORREF saved[8];           /* cores salvas no seletor */
    int      nSaved;
} Config;

typedef struct { WCHAR *p; int len, cap; } Buf;
typedef struct { float alpha, strike, check, label, size; } RowAnim;

typedef struct {
    WCHAR title[TEXT_MAX];
    WCHAR group[NAME_MAX];       /* "" = não informado */
    DWORD date;                  /* 0  = não informado */
    int   time, type, recur, prio; /* -1 = não informado */
} Parsed;

typedef void (*CalCb)(void *ctx, DWORD date);
typedef void (*TimeCb)(void *ctx, int time);                 /* -1 = sem horário */
typedef void (*ColorCb)(void *ctx, COLORREF c, int phase);   /* 0 prévia, 1 aplicar, 2 cancelar */

/* ------------------------------------------------------------------------ */
/* Estado global                                                            */
/* ------------------------------------------------------------------------ */

static HINSTANCE g_inst;
static HWND      g_msg, g_progman;
static UINT      g_msgTaskbarCreated;
static HICON     g_trayIcon;

static Item  *g_items;
static int    g_n, g_cap;
static DWORD  g_nextId = 1;

static Group  g_groups[MAX_GROUPS];
static int    g_nGroups;
static DWORD  g_nextGroupId = 1;

static Config g_cfg;
static WCHAR  g_dir[MAX_PATH], g_dataPath[MAX_PATH], g_histPath[MAX_PATH],
              g_iniPath[MAX_PATH], g_groupsPath[MAX_PATH];

static BOOL   g_animOn, g_quitting, g_win11;
static DWORD  g_lastDoneId, g_lastDay;
static Item   g_undoDel;
static BOOL   g_hasUndoDel;

/* ------------------------------------------------------------------------ */
/* Funções usadas entre módulos                                             */
/* ------------------------------------------------------------------------ */

static void Changed(void);
static void GroupsChanged(void);
static void ConfigChanged(void);
static void UpdateTip(void);
static void Notify(const WCHAR *title, const WCHAR *msg);
static void ScheduleTrim(void);

static void SyncOverlays(void);
static void RenderAll(void);
static void StartAnim(void);
static void Reschedule(void);

static void MainOpen(DWORD sel);
static void MainShowView(int view);
static void MainRefresh(void);
static void MainApplyTheme(void);
static void MainToast(const WCHAR *text, int kind, DWORD id);

static void QuickOpen(DWORD gid);
static void CalOpen(HWND owner, int x, int y, DWORD date, CalCb cb, void *ctx);
static void AlarmShow(DWORD id, const WCHAR *text, int time);
static void TimeOpen(HWND owner, int x, int y, int time, TimeCb cb, void *ctx);
static void ColorOpen(HWND owner, COLORREF initial, const WCHAR *title, ColorCb cb, void *ctx);
static void UiLoadTheme(void);

static void Complete(DWORD id);
static void Restore(DWORD id);
static void DeleteItem(DWORD id);
static void UndoDelete(void);
static void Postpone(DWORD id);
static void MoveToGroup(DWORD id, DWORD gid);
static void TogglePrio(DWORD id);
static void DuplicateItem(DWORD id);
static void Snooze(DWORD id, const WCHAR *text);

static UINT RunMenu(HMENU m, POINT pt, HWND owner);
static void TaskMenu(DWORD id, POINT pt, HWND owner, BOOL fromMain);
static void GroupMenu(DWORD gid, POINT pt, HWND owner, BOOL fromMain);
static void ExportIcs(HWND owner);
static void ImportIcs(HWND owner);
static void ChooseTextFont(HWND owner);
static void ChooseTextColor(HWND owner);
static BOOL ConfirmBox(HWND owner, const WCHAR *title, const WCHAR *text, const WCHAR *ok);
static void Quit(void);

#endif
