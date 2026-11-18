/*
 * mainwin.c — o painel do Task Killer.
 *
 *  ┌ barra lateral ┐┌ cabeçalho + busca ──────────────┐┌ detalhes ┐
 *  │ Nova tarefa   ││ indicadores (Hoje)              ││ título   │
 *  │ Hoje / 7 dias ││ lista agrupada por seção        ││ campos   │
 *  │ Grupos        ││                                 ││ notas    │
 *  │ Configurações ││                                 ││ ações    │
 *
 * Tudo é desenhado sob demanda (sem animação contínua). Ao fechar, a janela
 * é destruída e a memória e o GDI+ são liberados.
 */

enum {
    Z_NONE = 0, Z_NEW, Z_NAV, Z_GROUP, Z_GROUPADD, Z_GROUPMENU, Z_LOCK, Z_CARD, Z_ROW, Z_CHECK, Z_HROW, Z_HCHECK,
    Z_DCLOSE, Z_DCHECK, Z_DGROUP, Z_DTYPE, Z_DDATE, Z_DRECUR, Z_DPRIO, Z_DDONE, Z_DDELETE, Z_DTIMECLR, Z_DTIMEPICK, Z_DSHOW,
    Z_TOAST, Z_SET, Z_SWATCH, Z_EMPTYADD,
    Z_TBSORT, Z_TBSTATE, Z_TBPRIO, Z_TBDATE, Z_TBCLEAR, Z_ROWSTATE, Z_DSTATE,
    Z_STCOLOR, Z_STNAME, Z_STRENAME, Z_STUP, Z_STDOWN, Z_STDEF, Z_STDEL, Z_STADD,
    Z_BCARD, Z_BCHECK, Z_BADD, Z_BCOLHEAD, Z_TBVIEW, Z_TBBY, Z_STLATE, Z_STATGROUP
};
enum { K_LAYOUT = 1, K_LOCK, K_DATE, K_HIDEEMPTY, K_SHADOW, K_WIDTH, K_OPAC, K_FONT, K_COLOR, K_THEME,
       K_AUTOSTART, K_EXPORT, K_IMPORT, K_ARCHIVE, K_FOLDER, K_HISTORY, K_HORIZON, K_NOTESMODE, K_EMPHASIS, K_AUTODONE, K_STATEDESK };
#define SETARG(k, v) ((k) * 16 + (v))

#define IDC_SEARCH 201
#define IDC_DTITLE 202
#define IDC_DNOTES 203
#define IDC_DTIME  204
#define IDC_INLINE 205

enum { LE_SEC, LE_ITEM, LE_HIST };
typedef struct { BYTE kind; int idx; DWORD gid; BOOL hasDot; COLORREF dot; WCHAR label[64]; int count, y, h; } LEntry;

static HWND    m_hwnd, m_search, m_title, m_notes, m_time, m_inline;
static UiFonts m_f;
static Zones   m_z;
static BackBuf m_bb;
static HBRUSH  m_brSurf, m_brField;
static int     m_view = V_TODAY, m_scroll, m_contentH, m_x0, m_x1, m_detX;
static DWORD   m_viewGroup, m_sel, m_inlineGroup, m_inlineState, m_today;

/* filtros da lista (valem enquanto o painel está aberto) e barra de ferramentas */
enum { FD_ANY = 0, FD_LATE, FD_TODAY, FD_WEEK, FD_MONTH, FD_NONE };
static DWORD   m_fState;          /* 0 = todos os estados        */
static int     m_fPrio, m_fDate;  /* 0 = sem filtro              */
static RECT    m_tb[7];           /* ordenar, estado, importância, data, limpar, lista/cards, colunas */
static BOOL    m_tbShow[7];
static const WCHAR *kViewOpt[2] = { L"Lista", L"Cards" };
static const WCHAR *kByMenu[2]  = { L"Grupo (categoria)", L"Estado" };
static const WCHAR *kSortMenu[4] = { L"Data", L"Ordem alfabética (A–Z)", L"Importância", L"Estado" };
static const WCHAR *kSortShort[4] = { L"Ordem: data", L"Ordem: A–Z", L"Ordem: importância", L"Ordem: estado" };
static const WCHAR *kPrioMenu[3] = { L"Todas", L"Só importantes", L"Só as não importantes" };
static const WCHAR *kDateMenu[6] = { L"Qualquer data", L"Atrasadas", L"Hoje", L"Próximos 7 dias", L"Este mês", L"Sem data" };
static RECT    m_rList, m_rSearch;
static int     m_hotA, m_hotG, m_downA, m_downG;
static BOOL    m_tracking, m_loading;
static WCHAR   m_query[120];
static LEntry *m_list;
static int     m_nList, m_capList;
static Item   *m_hist;
static int     m_nHist;
static WCHAR   m_toast[96];
static int     m_toastKind;
static DWORD   m_toastId;

#define MS(x) MulDiv((x), (int)m_f.dpi, 96)
#define MHOT(a, g) (m_hotA == (a) && m_hotG == (int)(g))
#define SIDE_W 236
#define DET_W  384

static void MSelect(DWORD id);
static void StatsFree(void);
static BOOL PageView(void) { return m_view == V_SETTINGS || m_view == V_STATS; }   /* telas sem lista */
static void BuildBoard(void);
static BOOL BoardOn(void);
static void ClampHScroll(void);
static int  m_hscroll;

/* ------------------------------------------------------------------------ */
/* Dados da lista                                                           */
/* ------------------------------------------------------------------------ */

typedef struct { int today, overdue, doneToday, week, pending, done; } MStats;

static void ComputeStats(MStats *s)
{
    DWORD t = Today(), wk = AddDays(t, 6);
    int i;
    ZeroMemory(s, sizeof *s);
    for (i = 0; i < g_n; i++) {
        const Item *it = &g_items[i];
        if (it->status == S_PENDING) {
            s->pending++;
            if (!it->date || it->date <= t) s->today++;
            if (it->date && it->date < t) s->overdue++;
            if (it->date && it->date <= wk) s->week++;
        } else if (it->status == S_DONE) {
            s->done++;
            if ((DWORD)(it->done / 10000) == t) s->doneToday++;
        }
    }
}

static int UpcomingCount(void)
{
    DWORD t = Today(), nx = AddDays(t, 7);
    int i, c = 0;
    for (i = 0; i < g_n; i++)
        if (g_items[i].status == S_PENDING && g_items[i].date > t && g_items[i].date <= nx) c++;
    return c;
}

static const Item *LItem(int idx, BOOL hist) { return hist ? &m_hist[idx] : &g_items[idx]; }

static ULONGLONG DoneKey(const Item *it) { return it->done ? it->done : it->created; }

static int FoldCmp(const WCHAR *a, const WCHAR *b)
{
    while (*a && Fold(*a) == Fold(*b)) { a++; b++; }
    return (int)Fold(*a) - (int)Fold(*b);
}

static BOOL FiltersOn(void) { return m_fState || m_fPrio || m_fDate; }

static int ListKey(const Item *it)
{
    DWORD t = m_today;
    switch (g_cfg.sort) {
    case SORT_ALPHA: return 0;
    case SORT_PRIO:  return it->prio ? 0 : 1;
    case SORT_STATE: return StateOrder(ItemState(it));
    }
    switch (m_view) {
    case V_TODAY:   return it->date && it->date < t ? 0 : it->date == t ? 1 : 2;
    case V_GROUP:   return it->date && it->date < t ? 0 : it->date == t ? 1 : it->date ? 2 : 3;
    case V_WEEK:    return it->date < t ? 0 : (int)it->date;
    case V_ALL:     return FindGroup(ItemGroup(it));
    case V_DONE:
    case V_HISTORY: return (int)(DoneKey(it) / 10000);
    }
    return 0;
}

static int LCmp(int a, int b)
{
    BOOL hist = m_view == V_HISTORY;
    const Item *A = LItem(a, hist), *B = LItem(b, hist);
    int ka, kb;
    if (g_cfg.sort != SORT_DATE) {
        int c;
        ka = ListKey(A); kb = ListKey(B);
        if (ka != kb) return ka < kb ? -1 : 1;
        if (g_cfg.sort == SORT_ALPHA && (c = FoldCmp(A->text, B->text)) != 0) return c < 0 ? -1 : 1;
        if (m_view == V_DONE || hist) {
            ULONGLONG da = DoneKey(A), db = DoneKey(B);
            if (da != db) return da > db ? -1 : 1;
            return a > b ? -1 : 1;
        }
        return CmpItems(A, B, m_today);
    }
    if (m_view == V_DONE || hist) {
        ULONGLONG da = DoneKey(A), db = DoneKey(B);
        if (da != db) return da > db ? -1 : 1;
        return a > b ? -1 : 1;
    }
    ka = ListKey(A); kb = ListKey(B);
    if (ka != kb) return ka < kb ? -1 : 1;
    return CmpItems(A, B, m_today);
}

static BOOL LMatch(const Item *it)
{
    DWORD t = m_today;
    if (m_fState && ItemState(it) != m_fState) return FALSE;
    if (m_fPrio == 1 && !it->prio) return FALSE;
    if (m_fPrio == 2 && it->prio) return FALSE;
    switch (m_fDate) {
    case FD_LATE:  if (!it->date || it->date >= t) return FALSE; break;
    case FD_TODAY: if (it->date != t) return FALSE; break;
    case FD_WEEK:  if (!it->date || it->date < t || it->date > AddDays(t, 6)) return FALSE; break;
    case FD_MONTH: if (!it->date || it->date / 100 != t / 100) return FALSE; break;
    case FD_NONE:  if (it->date) return FALSE; break;
    }
    if (!m_query[0]) return TRUE;
    return FoldContains(it->text, m_query) || FoldContains(it->notes, m_query);
}

static BOOL LInView(const Item *it)
{
    switch (m_view) {
    case V_TODAY: return it->status == S_PENDING && (!it->date || it->date <= m_today);
    case V_WEEK:  return it->status == S_PENDING && it->date && it->date <= AddDays(m_today, 6);
    case V_ALL:   return it->status == S_PENDING;
    case V_GROUP: return it->status == S_PENDING && ItemGroup(it) == m_viewGroup;
    case V_DONE:  return it->status == S_DONE;
    }
    return FALSE;
}

static void SecLabel(const Item *it, int key, WCHAR *out, LEntry *e)
{
    DWORD dummy, *gid = &dummy;
    out[0] = 0;
    switch (g_cfg.sort) {
    case SORT_ALPHA: lstrcpyW(out, L"Ordem alfabética"); return;
    case SORT_PRIO:  lstrcpyW(out, key == 0 ? L"Importantes" : L"Demais"); return;
    case SORT_STATE: {
        DWORD sid = ItemState(it);
        lstrcpynW(out, StateName(sid), 64);
        e->hasDot = TRUE;
        e->dot = StateColor(sid);
        return;
    }
    }
    switch (m_view) {
    case V_TODAY: lstrcpyW(out, key == 0 ? L"Atrasadas" : key == 1 ? L"Hoje" : L"Sem data"); break;
    case V_GROUP: lstrcpyW(out, key == 0 ? L"Atrasadas" : key == 1 ? L"Hoje" : key == 2 ? L"Próximas" : L"Sem data"); break;
    case V_WEEK:
        if (key == 0) lstrcpyW(out, L"Atrasadas");
        else if ((DWORD)key == m_today) lstrcpyW(out, L"Hoje");
        else if ((DWORD)key == AddDays(m_today, 1)) lstrcpyW(out, L"Amanhã");
        else { FmtDate((DWORD)key, L"dddd, dd/MM", out, 64); }
        break;
    case V_ALL: *gid = ItemGroup(it); lstrcpynW(out, GroupName(*gid), 64); e->hasDot = TRUE; e->dot = GroupColor(*gid); break;
    default:
        if ((DWORD)key == m_today) lstrcpyW(out, L"Hoje");
        else if ((DWORD)key == AddDays(m_today, -1)) lstrcpyW(out, L"Ontem");
        else FmtDate((DWORD)key, L"dddd, dd/MM/yyyy", out, 64);
        break;
    }
}

static LEntry *AddEntry(void)
{
    if (m_nList == m_capList) {
        int nc = m_capList ? m_capList * 2 : 64;
        LEntry *p = (LEntry *)ReAlloc(m_list, (SIZE_T)nc * sizeof(LEntry));
        if (!p) return NULL;
        m_list = p; m_capList = nc;
    }
    ZeroMemory(&m_list[m_nList], sizeof(LEntry));
    return &m_list[m_nList++];
}

static void BuildList(void)
{
    BOOL hist = m_view == V_HISTORY;
    int total = hist ? m_nHist : g_n, n = 0, i, j, y = 0, lastKey = 0, sec = -1;
    int *idx;
    m_today = Today();
    m_nList = 0;
    m_contentH = 0;
    if (PageView() || !m_hwnd) return;
    idx = (int *)Alloc(((SIZE_T)total + 1) * sizeof(int));
    if (!idx) return;
    for (i = 0; i < total; i++) {
        const Item *it = LItem(i, hist);
        if ((hist || LInView(it)) && LMatch(it)) {
            j = n++;
            while (j > 0 && LCmp(i, idx[j - 1]) < 0) { idx[j] = idx[j - 1]; j--; }
            idx[j] = i;
        }
    }
    if (hist && n > 1500) n = 1500;
    for (i = 0; i < n; i++) {
        const Item *it = LItem(idx[i], hist);
        int key = ListKey(it);
        LEntry *e;
        if (sec < 0 || key != lastKey) {
            if (sec >= 0) y += MS(10);
            e = AddEntry();
            if (!e) break;
            e->kind = LE_SEC;
            SecLabel(it, key, e->label, e);
            e->y = y; e->h = MS(36);
            y += e->h;
            sec = m_nList - 1;
            lastKey = key;
        }
        e = AddEntry();
        if (!e) break;
        e->kind = hist ? LE_HIST : LE_ITEM;
        e->idx = idx[i];
        e->y = y; e->h = MS(48);
        y += e->h;
        m_list[sec].count++;
    }
    m_contentH = y + MS(80);
    Free(idx);
    BuildBoard();
}

static void ClampScroll(void)
{
    int vis = m_rList.bottom - m_rList.top, mx = m_contentH - vis;
    if (mx < 0) mx = 0;
    if (m_scroll > mx) m_scroll = mx;
    if (m_scroll < 0) m_scroll = 0;
}

static void MEnsureVisible(DWORD id)
{
    int i, vis = m_rList.bottom - m_rList.top;
    for (i = 0; i < m_nList; i++) {
        LEntry *e = &m_list[i];
        if (e->kind != LE_ITEM || g_items[e->idx].id != id) continue;
        if (e->y - MS(40) < m_scroll) m_scroll = e->y - MS(40);
        if (e->y + e->h > m_scroll + vis) m_scroll = e->y + e->h - vis + MS(8);
        ClampScroll();
        return;
    }
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                   */
/* ------------------------------------------------------------------------ */

static BOOL ShowCards(void) { return m_view == V_TODAY && !m_query[0]; }

static void TbText(int i, WCHAR *out)
{
    switch (i) {
    case 0: lstrcpyW(out, kSortShort[g_cfg.sort]); break;
    case 1: lstrcpynW(out, m_fState && GetState(m_fState) ? StateName(m_fState) : L"Estado", 48); break;
    case 2: lstrcpyW(out, m_fPrio == 1 ? L"Importantes" : m_fPrio == 2 ? L"Não importantes" : L"Importância"); break;
    case 3: lstrcpyW(out, m_fDate ? kDateMenu[m_fDate] : L"Data"); break;
    case 6:  lstrcpyW(out, g_cfg.boardBy ? L"Colunas: estado" : L"Colunas: grupo"); break;
    default: lstrcpyW(out, L"Limpar filtros"); break;
    }
}

/* Posiciona os chips de ordenar/filtrar a partir de y, quebrando a linha se faltar espaço.
   Retorna a altura ocupada. */
static int TbLayout(int y)
{
    static const int order[7] = { 5, 6, 0, 1, 2, 3, 4 };
    HDC dc;
    int k, i, x = m_x0, h = MS(30), gap = MS(8), rows = 1;
    WCHAR t[64];
    if (PageView()) { for (i = 0; i < 7; i++) m_tbShow[i] = FALSE; return 0; }
    dc = GetDC(m_hwnd);
    for (k = 0; k < 7; k++) {
        int w;
        i = order[k];
        m_tbShow[i] = i < 4 || (i == 4 && FiltersOn()) || (i == 5 && m_view != V_HISTORY) || (i == 6 && BoardOn());
        if (!m_tbShow[i]) continue;
        if (i == 5) {   /* controle segmentado Lista | Cards (mesma conta do USegment) */
            HGDIOBJ o = SelectObject(dc, m_f.sm);
            SIZE sz;
            int j;
            w = MS(3) * 2;
            for (j = 0; j < 2; j++) { GetTextExtentPoint32W(dc, kViewOpt[j], lstrlenW(kViewOpt[j]), &sz); w += sz.cx + MS(14) * 2; }
            SelectObject(dc, o);
        } else {
            TbText(i, t);
            w = UChipW(dc, &m_f, TRUE, t, i < 4 || i == 6);
        }
        if (x > m_x0 && x + w > m_x1) { x = m_x0; y += h + gap; rows++; }
        SetRect(&m_tb[i], x, y, x + w, y + h);
        x += w + gap;
    }
    ReleaseDC(m_hwnd, dc);
    return rows * h + (rows - 1) * gap;
}

static void PaintToolbar(HDC dc)
{
    static const int act[7] = { Z_TBSORT, Z_TBSTATE, Z_TBPRIO, Z_TBDATE, Z_TBCLEAR, Z_TBVIEW, Z_TBBY };
    static const WCHAR icons[7] = { IC_SORT, 0, IC_STAR, IC_CALENDAR, IC_CLOSE, 0, 0xE8A9 };
    WCHAR t[64];
    int i;
    for (i = 0; i < 7; i++) {
        RECT *r = &m_tb[i];
        BOOL on, dot = FALSE;
        COLORREF ic = T.accent;
        if (!m_tbShow[i]) continue;
        if (i == 5) {
            USegment(dc, &m_f, &m_z, r->left, r->top, r->bottom - r->top, kViewOpt, 2, g_cfg.panelMode, Z_TBVIEW, 0,
                     m_hotA, m_hotG, TRUE);
            continue;
        }
        TbText(i, t);
        on = i == 0 ? g_cfg.sort != SORT_DATE : i == 1 ? m_fState != 0 : i == 2 ? m_fPrio != 0 : i == 3 ? m_fDate != 0 : i == 6;
        if (i == 1) { dot = TRUE; ic = m_fState ? StateColor(m_fState) : T.text3; }
        if (i == 2 && m_fPrio == 1) ic = T.warn;
        UChip(dc, &m_f, r->left, r->top, r->bottom - r->top, dot ? 0 : icons[i], ic, dot, t, on || i == 0,
              MHOT(act[i], 0), i < 4 || i == 6);
        ZAdd(&m_z, r->left, r->top, r->right - r->left, r->bottom - r->top, act[i], 0);
    }
}

static void PlaceEdit(HWND e, BOOL show, int x, int y, int w, int h)
{
    if (!e) return;
    if (show) { MoveWindow(e, x, y, w, h, TRUE); if (!IsWindowVisible(e)) ShowWindow(e, SW_SHOWNA); }
    else if (IsWindowVisible(e)) ShowWindow(e, SW_HIDE);
}

static void MLayout(void)
{
    RECT c;
    int cw, ch, det, sw, sh, top;
    BOOL detail;
    if (!m_hwnd) return;
    GetClientRect(m_hwnd, &c);
    cw = c.right; ch = c.bottom;
    detail = m_sel && GetItem(m_sel) && !PageView() && m_view != V_HISTORY;
    det = detail ? MS(DET_W) : 0;
    m_detX = cw - det;
    m_x0 = MS(SIDE_W) + MS(32);
    m_x1 = cw - det - MS(32);
    if (m_x1 < m_x0 + MS(200)) m_x1 = m_x0 + MS(200);

    top = ShowCards() ? MS(104) + MS(100) + MS(20) : MS(104);
    if (!PageView()) top += TbLayout(top) + MS(4) + (BoardOn() ? MS(8) : 0);
    SetRect(&m_rList, m_x0, top, m_x1, ch);
    ClampHScroll();

    sw = MS(260); sh = MS(36);
    SetRect(&m_rSearch, m_x1 - sw, MS(28), m_x1, MS(28) + sh);

    m_loading = TRUE;
    PlaceEdit(m_search, !PageView(), m_rSearch.left + MS(38), m_rSearch.top + (sh - m_f.lhBase) / 2,
              sw - MS(48), m_f.lhBase);
    if (detail) {
        int dx = m_detX + MS(24), dr = cw - MS(24);
        int fy = MS(140) + 3 * MS(46);
        int ny = MS(140) + 7 * MS(46) + MS(40), nb = ch - MS(84);
        if (nb < ny + MS(80)) nb = ny + MS(80);
        PlaceEdit(m_title, TRUE, dx + MS(36), MS(80) - m_f.lhTitle / 2, dr - dx - MS(36), m_f.lhTitle);
        PlaceEdit(m_time, TRUE, dx + MS(104) + MS(12), fy + (MS(46) - m_f.lhBase) / 2, MS(62), m_f.lhBase);
        PlaceEdit(m_notes, TRUE, dx + MS(14), ny + MS(12), dr - dx - MS(28), nb - ny - MS(24));
    } else {
        PlaceEdit(m_title, FALSE, 0, 0, 0, 0);
        PlaceEdit(m_time, FALSE, 0, 0, 0, 0);
        PlaceEdit(m_notes, FALSE, 0, 0, 0, 0);
    }
    m_loading = FALSE;
    ClampScroll();
}

/* ------------------------------------------------------------------------ */
/* Pintura: barra lateral                                                   */
/* ------------------------------------------------------------------------ */

static void NavItem(HDC dc, int y, WCHAR icon, const WCHAR *label, int count, BOOL sel, int act, int arg)
{
    int x = MS(12), w = MS(SIDE_W) - MS(24), h = MS(36);
    BOOL hot = MHOT(act, arg);
    WCHAR c[16];
    if (sel) URound(dc, x, y, w, h, MS(8), T.surf2, 255);
    else if (hot) URound(dc, x, y, w, h, MS(8), T.surf2, 150);
    UIcon(dc, m_f.icon, icon, x + MS(20), y + h / 2, sel ? T.accent : T.text2);
    UText(dc, sel ? m_f.bold : m_f.base, label, x + MS(40), y, w - MS(80), h, sel ? T.text : T.text2, UT_L);
    if (count > 0) {
        wsprintfW(c, L"%d", count);
        UText(dc, m_f.sm, c, x, y, w - MS(12), h, T.text3, UT_R);
    }
    ZAdd(&m_z, x, y, w, h, act, arg);
}

static void PaintSidebar(HDC dc, int ch)
{
    int side = MS(SIDE_W), y, i, bottom;
    MStats s;
    WCHAR c[16];
    ComputeStats(&s);
    UFill(dc, 0, 0, side, ch, T.side);
    UFill(dc, side - 1, 0, 1, ch, T.border);

    /* marca */
    ULogo(dc, MS(20), MS(19), MS(32), T.accent);
    UText(dc, m_f.brand, L"Task Killer", MS(62), MS(14), MS(166), MS(42), T.text, UT_L);

    /* nova tarefa */
    UButton(dc, &m_f, MS(16), MS(68), side - MS(32), MS(40), L"Nova tarefa", IC_ADD, 0, MHOT(Z_NEW, 0));
    ZAdd(&m_z, MS(16), MS(68), side - MS(32), MS(40), Z_NEW, 0);

    y = MS(124);
    NavItem(dc, y, IC_DAY, L"Hoje", s.today, m_view == V_TODAY, Z_NAV, V_TODAY);          y += MS(38);
    NavItem(dc, y, IC_WEEK, L"Próximos 7 dias", s.week, m_view == V_WEEK, Z_NAV, V_WEEK); y += MS(38);
    NavItem(dc, y, IC_LIST, L"Todas", s.pending, m_view == V_ALL, Z_NAV, V_ALL);         y += MS(38);
    NavItem(dc, y, IC_CHECK, L"Concluídas", s.done, m_view == V_DONE, Z_NAV, V_DONE);    y += MS(38);
    NavItem(dc, y, IC_HISTORY, L"Histórico", 0, m_view == V_HISTORY, Z_NAV, V_HISTORY); y += MS(38);
    NavItem(dc, y, IC_CHART, L"Progresso", 0, m_view == V_STATS, Z_NAV, V_STATS);      y += MS(38);

    /* grupos */
    y += MS(18);
    UText(dc, m_f.cap, L"GRUPOS", MS(26), y, MS(120), MS(24), T.text3, UT_L);
    if (MHOT(Z_GROUPADD, 0)) URound(dc, side - MS(48), y - MS(2), MS(28), MS(28), MS(7), T.surf2, 255);
    UIcon(dc, m_f.iconSm, IC_ADD, side - MS(34), y + MS(12), T.text2);
    ZAdd(&m_z, side - MS(48), y - MS(2), MS(28), MS(28), Z_GROUPADD, 0);
    y += MS(30);

    bottom = ch - MS(104);
    for (i = 0; i < g_nGroups && y + MS(34) <= bottom; i++) {
        Group *g = &g_groups[i];
        int x = MS(12), w = side - MS(24), h = MS(34);
        BOOL sel = m_view == V_GROUP && m_viewGroup == g->id, hot = MHOT(Z_GROUP, g->id);
        int cnt = 0, k;
        for (k = 0; k < g_n; k++) if (g_items[k].status == S_PENDING && ItemGroup(&g_items[k]) == g->id) cnt++;
        if (m_inlineGroup == g->id) URound(dc, x, y, w, h, MS(8), T.field, 255);
        else if (sel) URound(dc, x, y, w, h, MS(8), T.surf2, 255);
        else if (hot) URound(dc, x, y, w, h, MS(8), T.surf2, 150);
        if (g->flags & GF_HIDDEN) UCircleLine(dc, (float)(x + MS(20)), (float)y + h / 2.0f, (float)MS(4), g->color, 255, 1.5f);
        else UCircle(dc, (float)(x + MS(20)), (float)y + h / 2.0f, (float)MS(4) + 0.5f, g->color, 255);
        if (m_inlineGroup != g->id) {
            UText(dc, sel ? m_f.bold : m_f.base, g->name, x + MS(40), y, w - MS(80), h, sel ? T.text : T.text2, UT_L);
            if (cnt) { wsprintfW(c, L"%d", cnt); UText(dc, m_f.sm, c, x, y, w - MS(12), h, T.text3, UT_R); }
        }
        ZAdd(&m_z, x, y, w, h, Z_GROUP, (int)g->id);
        y += MS(36);
    }

    /* rodapé */
    y = ch - MS(96);
    UFill(dc, MS(16), y, side - MS(32), 1, T.border);
    y += MS(10);
    NavItem(dc, y, g_cfg.locked ? IC_LOCK : IC_UNLOCK, g_cfg.locked ? L"Posições travadas" : L"Movendo janelas",
            0, FALSE, Z_LOCK, 0);
    y += MS(38);
    NavItem(dc, y, IC_SETTINGS, L"Configurações", 0, m_view == V_SETTINGS, Z_NAV, V_SETTINGS);
}

/* ------------------------------------------------------------------------ */
/* Pintura: cabeçalho e indicadores                                         */
/* ------------------------------------------------------------------------ */

static void PaintHeader(HDC dc)
{
    WCHAR title[64], sub[120];
    int x = m_x0, tw;
    MStats s;
    ComputeStats(&s);
    sub[0] = 0;
    switch (m_view) {
    case V_TODAY:
        lstrcpyW(title, L"Hoje");
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, NULL, L"dddd, d 'de' MMMM", sub, 120);
        Capitalize(sub);
        break;
    case V_WEEK:  lstrcpyW(title, L"Próximos 7 dias"); wsprintfW(sub, L"%d tarefa%s com data", s.week, s.week == 1 ? L"" : L"s"); break;
    case V_ALL:   lstrcpyW(title, L"Todas as tarefas"); wsprintfW(sub, L"%d pendente%s em %d grupo%s", s.pending, s.pending == 1 ? L"" : L"s", g_nGroups, g_nGroups == 1 ? L"" : L"s"); break;
    case V_DONE:  lstrcpyW(title, L"Concluídas"); lstrcpyW(sub, L"Últimos 7 dias. Clique na bolinha para restaurar."); break;
    case V_HISTORY: lstrcpyW(title, L"Histórico"); lstrcpyW(sub, L"Tudo o que já foi concluído ou excluído fica guardado aqui."); break;
    case V_STATS: lstrcpyW(title, L"Progresso"); lstrcpyW(sub, L"Seu ritmo, os prazos e o andamento de cada grupo."); break;
    case V_SETTINGS: lstrcpyW(title, L"Configurações"); lstrcpyW(sub, L"Aparência na área de trabalho, painel e dados."); break;
    case V_GROUP: {
        Group *g = GetGroup(m_viewGroup);
        int cnt = 0, k;
        lstrcpynW(title, g ? g->name : L"Grupo", 64);
        for (k = 0; k < g_n; k++) if (g_items[k].status == S_PENDING && ItemGroup(&g_items[k]) == m_viewGroup) cnt++;
        wsprintfW(sub, L"%d pendente%s  ·  %s", cnt, cnt == 1 ? L"" : L"s",
                  g && (g->flags & GF_HIDDEN) ? L"oculto da área de trabalho" : L"visível na área de trabalho");
        if (g) { UCircle(dc, (float)(x + MS(7)), (float)MS(46), (float)MS(6), g->color, 255); x += MS(24); }
        break;
    }
    default: title[0] = 0;
    }
    tw = UTextW(dc, m_f.h1, title);
    UText(dc, m_f.h1, title, x, MS(26), m_rSearch.left - x - MS(60), MS(40), T.text, UT_L);
    UText(dc, m_f.base, sub, m_x0, MS(66), m_rSearch.left - m_x0 - MS(20), MS(22), T.text2, UT_L);
    if (m_view == V_GROUP) {
        int bx = x + tw + MS(10);
        if (bx > m_rSearch.left - MS(50)) bx = m_rSearch.left - MS(50);
        if (MHOT(Z_GROUPMENU, 0)) URound(dc, bx, MS(30), MS(32), MS(32), MS(8), T.surf2, 255);
        UIcon(dc, m_f.icon, IC_MORE, bx + MS(16), MS(46), T.text2);
        ZAdd(&m_z, bx, MS(30), MS(32), MS(32), Z_GROUPMENU, 0);
    }
    if (!PageView()) {
        RECT *r = &m_rSearch;
        URound(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, MS(10), T.surf, 255);
        URoundLine(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, MS(10),
                   GetFocus() == m_search ? T.accent : T.border, 255, 1.0f);
        UIcon(dc, m_f.iconSm, IC_SEARCH, r->left + MS(20), r->top + (r->bottom - r->top) / 2, T.text3);
    }
}

static void Card(HDC dc, int x, int y, int w, int h, const WCHAR *label, int value, COLORREF vc,
                 const WCHAR *foot, int pct, int view)
{
    WCHAR v[16];
    BOOL hot = MHOT(Z_CARD, view);
    URound(dc, x, y, w, h, MS(14), T.surf, 255);
    URoundLine(dc, x, y, w, h, MS(14), hot ? Mix(T.border, T.accent, 120) : T.border, 255, 1.0f);
    UText(dc, m_f.sm, label, x + MS(18), y + MS(14), w - MS(36), MS(20), T.text2, UT_L);
    wsprintfW(v, L"%d", value);
    UText(dc, m_f.big, v, x + MS(18), y + MS(34), w - MS(36), MS(40), vc, UT_L);
    if (pct >= 0) {
        int bw = w - MS(36), fw = bw * pct / 100;
        URound(dc, x + MS(18), y + h - MS(22), bw, MS(6), MS(3), T.surf2, 255);
        if (fw > 0) URound(dc, x + MS(18), y + h - MS(22), fw < MS(6) ? MS(6) : fw, MS(6), MS(3), T.accent, 255);
    } else {
        UText(dc, m_f.sm, foot, x + MS(18), y + h - MS(30), w - MS(36), MS(20), T.text3, UT_L);
    }
    ZAdd(&m_z, x, y, w, h, Z_CARD, view);
}

static void PaintCards(HDC dc)
{
    MStats s;
    int y = MS(104), h = MS(100), gap = MS(14), w = (m_x1 - m_x0 - gap * 3) / 4, x = m_x0, pct, total;
    ComputeStats(&s);
    total = s.today + s.doneToday;
    pct = total ? s.doneToday * 100 / total : 0;
    Card(dc, x, y, w, h, L"Para hoje", s.today, T.text, NULL, pct, V_TODAY);                       x += w + gap;
    {   /* prazos que vencem nas próximas 24 h (e atrasadas) */
        ULONGLONG now = NowFt();
        int i, nNear = 0, nUrgent = 0;
        WCHAR foot[48];
        for (i = 0; i < g_n; i++) {
            int t = Urgency(&g_items[i], now, NULL, NULL);
            if (t >= U_NEAR) nNear++;
            if (t >= U_URGENT) nUrgent++;
        }
        if (s.overdue) wsprintfW(foot, L"%d atrasada%s", s.overdue, s.overdue == 1 ? L"" : L"s");
        else if (nUrgent) wsprintfW(foot, L"%d nas próximas 2 h", nUrgent);
        else lstrcpyW(foot, L"nas próximas 24 h");
        Card(dc, x, y, w, h, L"Prazos próximos", nNear, nUrgent ? T.danger : nNear ? T.warn : T.text, foot, -1, V_WEEK);
        x += w + gap;
    }
    Card(dc, x, y, w, h, L"Concluídas hoje", s.doneToday, s.doneToday ? T.success : T.text, L"bom ritmo", -1, V_DONE);
    x += w + gap;
    Card(dc, x, y, w, h, L"Próximos 7 dias", UpcomingCount(), T.text, L"a partir de amanhã", -1, V_WEEK);
}

/* ------------------------------------------------------------------------ */
/* Pintura: lista                                                           */
/* ------------------------------------------------------------------------ */

static void PaintSection(HDC dc, LEntry *e, int y)
{
    WCHAR lbl[64], c[16];
    int x = m_x0, tw;
    HGDIOBJ o;
    lstrcpynW(lbl, e->label, 64);
    CharUpperW(lbl);
    if (e->hasDot) { UDot(dc, (float)(x + MS(4)), (float)(y + e->h - MS(14)), (float)MS(4), e->dot); x += MS(16); }
    o = SelectObject(dc, m_f.cap);
    SetTextCharacterExtra(dc, MS(1));
    SelectObject(dc, o);
    UText(dc, m_f.cap, lbl, x, y + e->h - MS(26), m_x1 - x, MS(24), T.text3, UT_L);
    tw = UTextW(dc, m_f.cap, lbl);
    o = SelectObject(dc, m_f.cap);
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, o);
    wsprintfW(c, L"%d", e->count);
    UText(dc, m_f.cap, c, x + tw + MS(10), y + e->h - MS(26), MS(40), MS(24), T.text3, UT_L);
}

static void PaintRow(HDC dc, LEntry *e, int y)
{
    BOOL hist = e->kind == LE_HIST;
    const Item *it = LItem(e->idx, hist);
    int x0 = m_x0, x1 = m_x1, h = e->h, rx = x1 - MS(8), tx, avail, tw;
    int act = hist ? Z_HROW : Z_ROW, chk = hist ? Z_HCHECK : Z_CHECK, arg = hist ? e->idx : (int)it->id;
    BOOL sel = !hist && it->id == m_sel, hot = MHOT(act, arg) || MHOT(chk, arg), done = it->status != S_PENDING;
    WCHAR when[48];
    COLORREF wc = T.text2, tc;
    float sc = (float)m_f.dpi / 96.0f;

    if (sel) URound(dc, x0 - MS(10), y + MS(2), x1 - x0 + MS(20), h - MS(4), MS(10), T.accent, T.dark ? 36 : 26);
    else if (hot) URound(dc, x0 - MS(10), y + MS(2), x1 - x0 + MS(20), h - MS(4), MS(10), T.surf2, 255);
    ZAddClip(&m_z, x0 - MS(10), y, x1 - x0 + MS(20), h, act, arg, &m_rList);

    if (it->status == S_DELETED) UIcon(dc, m_f.iconSm, IC_DELETE, x0 + MS(12), y + h / 2, MHOT(chk, arg) ? T.accent : T.text3);
    else UCheckCircle(dc, (float)(x0 + MS(12)), (float)y + h / 2.0f, (float)MS(9) + 0.5f, done, MHOT(chk, arg), T.text3, sc);
    ZAddClip(&m_z, x0 - MS(6), y, MS(36), h, chk, arg, &m_rList);

    /* coluna direita: quando */
    when[0] = 0;
    if (done) {
        ULONGLONG s = DoneKey(it);
        FmtTime((int)(s % 10000), when);
        if (it->status == S_DELETED) { WCHAR t2[48]; wsprintfW(t2, L"excluída  %s", when); lstrcpyW(when, t2); }
        else if (it->flags & F_AUTO) { WCHAR t2[48]; wsprintfW(t2, L"prazo encerrado  %s", when); lstrcpyW(when, t2); }
        wc = T.text3;
    } else {
        WhenLabel(it->date, it->time, m_today, when, 48);
        if (it->date && it->date < m_today) wc = T.danger;
        else if (it->date == m_today && it->time >= 0) wc = T.accent;
    }
    if (when[0]) {
        int ww = UTextW(dc, m_f.sm, when);
        UText(dc, m_f.sm, when, rx - ww, y, ww + 2, h, wc, DT_SINGLELINE | DT_VCENTER);
        rx -= ww + MS(16);
    }
    if (!done && g_cfg.emphasis) {   /* destaque: "em 2h" num selo colorido */
        WCHAR ul[32];
        int tier = Urgency(it, NowFt(), ul, NULL);
        if (tier != U_NONE && ul[0]) {
            COLORREF uc = tier >= U_URGENT ? T.danger : T.warn;
            int uw = UTextW(dc, m_f.smB, ul) + MS(18), uy = y + (h - MS(22)) / 2;
            URound(dc, rx - uw, uy, uw, MS(22), MS(11), uc, tier == U_SOON ? (T.dark ? 30 : 24) : (T.dark ? 52 : 40));
            UText(dc, m_f.smB, ul, rx - uw, uy, uw, MS(22), uc, UT_C);
            rx -= uw + MS(10);
        }
    }
    if (it->recur) { UIcon(dc, m_f.iconSm, IC_REPEAT, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    if (!done && ShowMode(it) == SH_NEVER) { UIcon(dc, m_f.iconSm, IC_HIDE, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    else if (!done && (ShowMode(it) == SH_DAY || ShowMode(it) == SH_WEEK) && it->date && !IsVisible(it, m_today))
        { UIcon(dc, m_f.iconSm, IC_HIDE, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    if (it->type == T_ALARM) { UIcon(dc, m_f.iconSm, IC_SOUND, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    else if (it->type == T_REMIND) { UIcon(dc, m_f.iconSm, IC_BELL, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }

    {   /* selo do estado (clique para trocar) */
        DWORD sid = ItemState(it);
        const WCHAR *sn = StateName(sid);
        COLORREF scol = StateColor(sid);
        int cw = UTextW(dc, m_f.smB, sn) + MS(34), cy = y + (h - MS(24)) / 2;
        BOOL shot = !hist && MHOT(Z_ROWSTATE, arg);
        if (rx - cw > x0 + MS(200)) {
            URound(dc, rx - cw, cy, cw, MS(24), MS(12), scol, done ? 18 : (T.dark ? 40 : 34));
            if (shot) URoundLine(dc, rx - cw, cy, cw, MS(24), MS(12), scol, 255, 1.2f);
            UDot(dc, (float)(rx - cw + MS(13)), (float)cy + MS(12), (float)MS(4), scol);
            UText(dc, m_f.smB, sn, rx - cw + MS(22), cy, cw - MS(28), MS(24), done ? T.text3 : T.text, UT_L);
            if (!hist) ZAddClip(&m_z, rx - cw, cy, cw, MS(24), Z_ROWSTATE, arg, &m_rList);
            rx -= cw + MS(10);
        }
    }

    if (m_view != V_GROUP && m_view != V_ALL) {
        const WCHAR *gn = GroupName(ItemGroup(it));
        int cw = UTextW(dc, m_f.sm, gn) + MS(34), cy = y + (h - MS(24)) / 2;
        if (rx - cw > x0 + MS(260)) {
            URound(dc, rx - cw, cy, cw, MS(24), MS(12), sel ? T.surf : T.surf2, 255);
            UCircle(dc, (float)(rx - cw + MS(13)), (float)cy + MS(12), (float)MS(3) + 0.5f, GroupColor(ItemGroup(it)), 255);
            UText(dc, m_f.sm, gn, rx - cw + MS(22), cy, cw - MS(28), MS(24), T.text2, UT_L);
            rx -= cw + MS(12);
        }
    }

    tx = x0 + MS(36);
    if (it->prio) { UIcon(dc, m_f.iconSm, IC_STARFILL, tx + MS(7), y + h / 2, T.warn); tx += MS(22); }
    avail = rx - tx - MS(8);
    if (avail < MS(40)) avail = MS(40);
    tc = done ? T.text3 : T.text;
    tw = UTextW(dc, m_f.base, it->text);
    if (tw > avail) tw = avail;
    UText(dc, m_f.base, it->text, tx, y, avail, h, tc, UT_L);
    if (it->status == S_DONE) ULine(dc, (float)tx, (float)y + h / 2.0f + 1, (float)(tx + tw), (float)y + h / 2.0f + 1, T.text3, 200, 1.2f * sc);
    if (it->notes && tw + MS(60) < avail) {
        WCHAR prev[90];
        int k = 0;
        const WCHAR *s = it->notes;
        while (*s && *s != L'\r' && *s != L'\n' && k < 88) prev[k++] = *s++;
        prev[k] = 0;
        if (k) UText(dc, m_f.sm, prev, tx + tw + MS(14), y, avail - tw - MS(14), h, T.text3, UT_L);
    }
}

static void PaintEmpty(HDC dc)
{
    int cx = (m_rList.left + m_rList.right) / 2, cy = m_rList.top + MS(120);
    const WCHAR *t1 = L"Tudo em dia", *t2 = L"Nenhuma tarefa por aqui. Aperte Ctrl+N para adicionar.";
    BOOL add = TRUE;
    if (m_query[0] || FiltersOn()) { t1 = L"Nada encontrado"; t2 = m_query[0] ? L"Tente buscar por outra palavra." : L"Nenhuma tarefa com esses filtros. Clique em Limpar filtros."; add = FALSE; }
    else if (m_view == V_DONE) { t1 = L"Nenhuma concluída"; t2 = L"As tarefas concluídas nos últimos 7 dias aparecem aqui."; add = FALSE; }
    else if (m_view == V_HISTORY) { t1 = L"Histórico vazio"; t2 = L"Concluídas com mais de 7 dias e excluídas ficam guardadas aqui."; add = FALSE; }
    UCircle(dc, (float)cx, (float)cy, (float)MS(34), T.accent, T.dark ? 30 : 22);
    UCheck(dc, (float)cx, (float)cy, (float)MS(28), T.accent, 255, (float)MS(3));
    UText(dc, m_f.h2, t1, m_rList.left, cy + MS(50), m_rList.right - m_rList.left, MS(30), T.text, UT_C);
    UText(dc, m_f.base, t2, m_rList.left, cy + MS(80), m_rList.right - m_rList.left, MS(24), T.text2, UT_C);
    if (add) {
        int bw = MS(150);
        UButton(dc, &m_f, cx - bw / 2, cy + MS(120), bw, MS(38), L"Nova tarefa", IC_ADD, 1, MHOT(Z_EMPTYADD, 0));
        ZAdd(&m_z, cx - bw / 2, cy + MS(120), bw, MS(38), Z_EMPTYADD, 0);
    }
}

static void PaintScrollbar(HDC dc)
{
    int vis = m_rList.bottom - m_rList.top, th, ty;
    if (m_contentH <= vis || vis <= 0) return;
    th = vis * vis / m_contentH;
    if (th < MS(40)) th = MS(40);
    ty = m_rList.top + (vis - th) * m_scroll / (m_contentH - vis);
    URound(dc, m_x1 + MS(18), ty + MS(4), MS(4), th - MS(8), MS(2), T.text3, 110);
}

static void PaintList(HDC dc)
{
    int save = SaveDC(dc), i;
    IntersectClipRect(dc, m_rList.left - MS(14), m_rList.top, m_rList.right + MS(14), m_rList.bottom);
    if (!m_nList) PaintEmpty(dc);
    for (i = 0; i < m_nList; i++) {
        LEntry *e = &m_list[i];
        int y = m_rList.top + e->y - m_scroll;
        if (y + e->h < m_rList.top || y > m_rList.bottom) continue;
        if (e->kind == LE_SEC) PaintSection(dc, e, y);
        else if (e->kind == LE_HIST ? e->idx < m_nHist : e->idx < g_n) PaintRow(dc, e, y);
    }
    RestoreDC(dc, save);
    PaintScrollbar(dc);
}

/* ------------------------------------------------------------------------ */
/* Cards: grade dinâmica de blocos (um por grupo ou por estado)             */
/*                                                                          */
/* Cada bloco tem a altura do próprio conteúdo e é encaixado na coluna da   */
/* grade que estiver mais curta (layout "masonry"). Blocos vazios somem.    */
/* ------------------------------------------------------------------------ */

typedef struct { DWORD key; int count, x, y, h; } BCol;     /* x, y: posição no conteúdo */
typedef struct { int idx, col, y, h, titleH; } BCard;       /* y: dentro do bloco */

static BCol    m_bcol[MAX_GROUPS + MAX_STATES];
static int     m_nbcol, m_boxW;
static BCard  *m_bcard;
static int     m_nbcard, m_capbcard;
static DWORD   m_dragId;
static BOOL    m_dragging;
static POINT   m_dragStart, m_dragPt, m_dragOff;
static int     m_dragCol = -1, m_dropCol = -1;

#define BOX_MIN  MS(268)
#define BOX_GAP  MS(16)
#define BHEAD_H  MS(46)
#define BCARD_P  MS(12)
#define BCARD_IN MS(10)    /* margem dos cards dentro do bloco */

static BOOL BoardOn(void) { return g_cfg.panelMode == 1 && m_view != V_SETTINGS && m_view != V_HISTORY && m_view != V_STATS; }

static DWORD BoardKey(const Item *it) { return g_cfg.boardBy ? ItemState(it) : ItemGroup(it); }

static void ClampHScroll(void) { m_hscroll = 0; }   /* a grade não rola para os lados */

/* Monta os blocos e mede os cards, na mesma ordem da lista (respeita "Ordem" e filtros). */
static void BuildBoard(void)
{
    DWORD keys[MAX_GROUPS + MAX_STATES];
    int counts[MAX_GROUPS + MAX_STATES];
    int colH[16];
    int nk = 0, i, k, ncols, avail, inner;
    HDC dc;
    HGDIOBJ o;

    m_nbcol = 0;
    m_nbcard = 0;
    if (!BoardOn() || !m_hwnd) return;

    if (g_cfg.boardBy) for (i = 0; i < g_nStates; i++) keys[nk++] = g_states[i].id;
    else if (m_view == V_GROUP) keys[nk++] = m_viewGroup;
    else for (i = 0; i < g_nGroups; i++) keys[nk++] = g_groups[i].id;
    for (k = 0; k < nk; k++) counts[k] = 0;
    for (i = 0; i < m_nList; i++) {
        DWORD key;
        if (m_list[i].kind != LE_ITEM || m_list[i].idx >= g_n) continue;
        key = BoardKey(&g_items[m_list[i].idx]);
        for (k = 0; k < nk; k++) if (keys[k] == key) { counts[k]++; break; }
    }
    for (k = 0; k < nk; k++)                       /* grupos/estados vazios não aparecem */
        if (counts[k]) { m_bcol[m_nbcol].key = keys[k]; m_bcol[m_nbcol].count = 0; m_bcol[m_nbcol].h = BHEAD_H; m_nbcol++; }

    /* quantas colunas cabem; os blocos esticam para ocupar a largura toda */
    avail = m_x1 - m_x0;
    ncols = (avail + BOX_GAP) / (BOX_MIN + BOX_GAP);
    if (ncols < 1) ncols = 1;
    if (ncols > 16) ncols = 16;
    if (ncols > m_nbcol && m_nbcol > 0) ncols = m_nbcol;
    m_boxW = (avail - BOX_GAP * (ncols - 1)) / ncols;
    if (m_boxW > MS(420) && m_nbcol <= 2) m_boxW = MS(420);   /* poucos blocos: não fica largo demais */
    inner = m_boxW - BCARD_IN * 2 - BCARD_P * 2 - MS(28);

    dc = GetDC(m_hwnd);
    o = SelectObject(dc, m_f.bold);
    for (i = 0; i < m_nList; i++) {
        LEntry *e = &m_list[i];
        Item *it;
        BCard *c;
        RECT r;
        int col, th;
        if (e->kind != LE_ITEM || e->idx >= g_n) continue;
        it = &g_items[e->idx];
        for (col = 0; col < m_nbcol && m_bcol[col].key != BoardKey(it); col++) {}
        if (col >= m_nbcol) continue;
        if (m_nbcard == m_capbcard) {
            int nc = m_capbcard ? m_capbcard * 2 : 64;
            BCard *p = (BCard *)ReAlloc(m_bcard, (SIZE_T)nc * sizeof(BCard));
            if (!p) break;
            m_bcard = p; m_capbcard = nc;
        }
        SetRect(&r, 0, 0, inner > 40 ? inner : 40, 0);
        DrawTextW(dc, it->text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        th = r.bottom;
        if (th > m_f.lhBase * 3) th = m_f.lhBase * 3;
        if (th < m_f.lhBase) th = m_f.lhBase;
        c = &m_bcard[m_nbcard++];
        c->idx = e->idx;
        c->col = col;
        c->titleH = th;
        c->h = BCARD_P + th + MS(10) + MS(24) + (it->notes && it->notes[0] ? MS(22) : 0) + BCARD_P;
        c->y = m_bcol[col].h;
        m_bcol[col].h += c->h + MS(8);
        m_bcol[col].count++;
    }
    SelectObject(dc, o);
    ReleaseDC(m_hwnd, dc);

    /* encaixe: cada bloco vai para a coluna mais curta */
    for (k = 0; k < ncols; k++) colH[k] = 0;
    for (i = 0; i < m_nbcol; i++) {
        int best = 0;
        m_bcol[i].h += MS(4);
        for (k = 1; k < ncols; k++) if (colH[k] < colH[best]) best = k;
        m_bcol[i].x = best * (m_boxW + BOX_GAP);
        m_bcol[i].y = colH[best];
        colH[best] += m_bcol[i].h + BOX_GAP;
    }
    m_contentH = MS(60);
    for (k = 0; k < ncols; k++) if (colH[k] + MS(60) > m_contentH) m_contentH = colH[k] + MS(60);
}

/* Bloco sob um ponto da janela (para soltar o card); -1 = nenhum. */
static int BoardBoxAt(int x, int y)
{
    int i;
    for (i = 0; i < m_nbcol; i++) {
        int bx = m_x0 + m_bcol[i].x, by = m_rList.top + m_bcol[i].y - m_scroll;
        if (x >= bx - BOX_GAP / 2 && x < bx + m_boxW + BOX_GAP / 2 && y >= by - BOX_GAP / 2 && y < by + m_bcol[i].h + BOX_GAP / 2)
            return i;
    }
    return -1;
}

static void ColInfo(int c, const WCHAR **name, COLORREF *col)
{
    DWORD k = m_bcol[c].key;
    if (g_cfg.boardBy) { *name = StateName(k); *col = StateColor(k); }
    else { *name = GroupName(k); *col = GroupColor(k); }
}

static void DrawCard(HDC dc, const Item *it, int x, int y, int w, int h, int titleH, BOOL sel, BOOL hot, BOOL ghost)
{
    WCHAR when[48], ul[32];
    int px = x + BCARD_P, mx, my, tier = U_NONE, done = it->status != S_PENDING;
    float sc = (float)m_f.dpi / 96.0f;
    COLORREF wc = T.text2;

    ul[0] = 0;
    if (ghost) URound(dc, x + MS(3), y + MS(5), w, h, MS(12), RGB(0, 0, 0), T.dark ? 90 : 40);   /* sombra */
    URound(dc, x, y, w, h, MS(12), T.surf, 255);
    URoundLine(dc, x, y, w, h, MS(12), sel ? T.accent : hot ? Mix(T.border, T.accent, 110) : T.border, 255, sel ? 1.6f : 1.0f);
    /* faixa na cor da outra categoria (estado quando as colunas são grupos, e vice-versa) */
    URound(dc, x + MS(1), y + MS(10), MS(3), h - MS(20), MS(2), g_cfg.boardBy ? GroupColor(ItemGroup(it)) : StateColor(ItemState(it)), 255);

    UCheckCircle(dc, (float)(px + MS(9)), (float)(y + BCARD_P + m_f.lhBase / 2), (float)MS(9) + 0.5f, done,
                 FALSE, T.text3, sc);
    UText(dc, m_f.bold, it->text, px + MS(28), y + BCARD_P, w - BCARD_P * 2 - MS(28), titleH, done ? T.text3 : T.text,
          DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);

    /* linha de detalhes */
    mx = px; my = y + BCARD_P + titleH + MS(10);
    when[0] = 0;
    if (!done) {
        WhenLabel(it->date, it->time, m_today, when, 48);
        if (g_cfg.emphasis) tier = Urgency(it, NowFt(), ul, NULL);
        if (it->date && it->date < m_today) wc = T.danger;
    }
    if (tier != U_NONE && ul[0]) {
        COLORREF uc = tier >= U_URGENT ? T.danger : T.warn;
        int uw = UTextW(dc, m_f.smB, ul) + MS(16);
        URound(dc, mx, my, uw, MS(22), MS(11), uc, tier == U_SOON ? 26 : 44);
        UText(dc, m_f.smB, ul, mx, my, uw, MS(22), uc, UT_C);
        mx += uw + MS(8);
    } else if (when[0]) {
        int ww = UTextW(dc, m_f.sm, when);
        UIcon(dc, m_f.iconSm, IC_CALENDAR, mx + MS(7), my + MS(11), wc);
        UText(dc, m_f.sm, when, mx + MS(18), my, ww + 2, MS(22), wc, DT_SINGLELINE | DT_VCENTER);
        mx += ww + MS(28);
    }
    {   /* a outra categoria, como selo */
        DWORD k = g_cfg.boardBy ? ItemGroup(it) : ItemState(it);
        const WCHAR *nm = g_cfg.boardBy ? GroupName(k) : StateName(k);
        COLORREF cc = g_cfg.boardBy ? GroupColor(k) : StateColor(k);
        int cw = UTextW(dc, m_f.sm, nm) + MS(28), right = x + w - BCARD_P - (it->prio ? MS(22) : 0) - (it->recur ? MS(20) : 0);
        if (mx + cw <= right) {
            URound(dc, mx, my, cw, MS(22), MS(11), cc, T.dark ? 36 : 30);
            UDot(dc, (float)(mx + MS(11)), (float)(my + MS(11)), (float)MS(3) + 0.5f, cc);
            UText(dc, m_f.sm, nm, mx + MS(19), my, cw - MS(22), MS(22), T.text2, UT_L);
        }
    }
    {
        int rx = x + w - BCARD_P;
        if (it->prio)  { UIcon(dc, m_f.iconSm, IC_STARFILL, rx - MS(8), my + MS(11), T.warn); rx -= MS(22); }
        if (it->recur) UIcon(dc, m_f.iconSm, IC_REPEAT, rx - MS(8), my + MS(11), T.text3);
    }
    if (it->notes && it->notes[0]) {
        WCHAR prev[90];
        int k = 0;
        const WCHAR *s = it->notes;
        while (*s && *s != L'\r' && *s != L'\n' && k < 88) prev[k++] = *s++;
        prev[k] = 0;
        UText(dc, m_f.sm, prev, px, my + MS(28), w - BCARD_P * 2, MS(18), T.text3, UT_L);
    }
}

static void PaintBoard(HDC dc)
{
    int save = SaveDC(dc), c, i, top = m_rList.top, bottom = m_rList.bottom;
    BCard *drag = NULL;
    IntersectClipRect(dc, m_rList.left - MS(4), top, m_rList.right + MS(4), bottom);

    if (!m_nbcol) PaintEmpty(dc);
    for (c = 0; c < m_nbcol; c++) {
        BCol *bc = &m_bcol[c];
        int x = m_x0 + bc->x, y = top + bc->y - m_scroll, nw;
        const WCHAR *nm;
        COLORREF col;
        WCHAR cnt[16];
        BOOL target = m_dragging && c == m_dropCol && c != m_dragCol;
        if (y > bottom || y + bc->h < top) continue;
        ColInfo(c, &nm, &col);
        URound(dc, x, y, m_boxW, bc->h, MS(14), T.dark ? T.side : T.surf2, 255);
        URound(dc, x + MS(14), y, m_boxW - MS(28), MS(3), MS(2), col, 255);            /* filete na cor do bloco */
        if (target) {
            URound(dc, x, y, m_boxW, bc->h, MS(14), T.accent, T.dark ? 22 : 16);
            URoundLine(dc, x, y, m_boxW, bc->h, MS(14), T.accent, 255, 1.6f);
        }
        UDot(dc, (float)(x + MS(18)), (float)(y + BHEAD_H / 2), (float)MS(5), col);
        nw = UTextW(dc, m_f.bold, nm);
        if (nw > m_boxW - MS(100)) nw = m_boxW - MS(100);
        UText(dc, m_f.bold, nm, x + MS(30), y, nw + 2, BHEAD_H, T.text, UT_L);
        wsprintfW(cnt, L"%d", bc->count);
        UText(dc, m_f.sm, cnt, x + MS(38) + nw, y, MS(40), BHEAD_H, T.text3, UT_L);
        if (MHOT(Z_BADD, c)) URound(dc, x + m_boxW - MS(40), y + MS(8), MS(30), MS(30), MS(8), T.surf, 255);
        UIcon(dc, m_f.iconSm, IC_ADD, x + m_boxW - MS(25), y + MS(23), T.text2);
        ZAddClip(&m_z, x, y, m_boxW - MS(44), BHEAD_H, Z_BCOLHEAD, c, &m_rList);
        ZAddClip(&m_z, x + m_boxW - MS(40), y + MS(8), MS(30), MS(30), Z_BADD, c, &m_rList);
    }

    for (i = 0; i < m_nbcard; i++) {
        BCard *b = &m_bcard[i];
        Item *it = &g_items[b->idx];
        BCol *bc = &m_bcol[b->col];
        int x = m_x0 + bc->x + BCARD_IN, y = top + bc->y + b->y - m_scroll, w = m_boxW - BCARD_IN * 2;
        if (y > bottom || y + b->h < top) continue;
        if (m_dragging && it->id == m_dragId) {   /* lugar vazio onde o card estava */
            URoundLine(dc, x, y, w, b->h, MS(12), T.text3, 120, 1.2f);
            drag = b;
            continue;
        }
        DrawCard(dc, it, x, y, w, b->h, b->titleH, it->id == m_sel, MHOT(Z_BCARD, it->id) || MHOT(Z_BCHECK, it->id), FALSE);
        ZAddClip(&m_z, x, y, w, b->h, Z_BCARD, (int)it->id, &m_rList);
        ZAddClip(&m_z, x, y, MS(36), BCARD_P + m_f.lhBase + MS(6), Z_BCHECK, (int)it->id, &m_rList);  /* por cima do card */
    }
    RestoreDC(dc, save);

    /* card sendo arrastado, acompanhando o mouse */
    if (m_dragging) {
        if (!drag) for (i = 0; i < m_nbcard; i++) if (g_items[m_bcard[i].idx].id == m_dragId) drag = &m_bcard[i];
        if (drag)
            DrawCard(dc, &g_items[drag->idx], m_dragPt.x - m_dragOff.x, m_dragPt.y - m_dragOff.y, m_boxW - BCARD_IN * 2,
                     drag->h, drag->titleH, FALSE, TRUE, TRUE);
    }
    PaintScrollbar(dc);
}

static BCard *CardById(DWORD id)
{
    int i;
    for (i = 0; i < m_nbcard; i++) if (g_items[m_bcard[i].idx].id == id) return &m_bcard[i];
    return NULL;
}

static void BoardDrop(void)
{
    DWORD key, id = m_dragId;
    WCHAR msg[96];
    const WCHAR *nm;
    COLORREF col;
    if (m_dropCol < 0 || m_dropCol >= m_nbcol || m_dropCol == m_dragCol || !GetItem(id)) return;
    key = m_bcol[m_dropCol].key;
    ColInfo(m_dropCol, &nm, &col);
    if (g_cfg.boardBy) SetItemState(id, key); else MoveToGroup(id, key);
    wsprintfW(msg, L"Movida para %s", nm);
    MainToast(msg, 0, 0);
}

static void BoardEndDrag(void)
{
    m_dragId = 0;
    m_dragging = FALSE;
    m_dragCol = m_dropCol = -1;
}


/* ------------------------------------------------------------------------ */
/* Progresso: ritmo, prazos e andamento de cada grupo                       */
/*                                                                          */
/* O histórico é lido uma vez ao abrir esta tela e resumido em contadores;  */
/* as tarefas atuais entram ao vivo a cada pintura.                         */
/* ------------------------------------------------------------------------ */

#define ST_DAYS 400

static struct {
    BOOL  loaded;
    DWORD today;
    int   day[ST_DAYS];                    /* concluídas por "dias atrás" */
    int   onTime, dated;                   /* últimos 30 dias */
    DWORD gid[MAX_GROUPS * 4];
    int   closed[MAX_GROUPS * 4], ng;      /* concluídas (sem repetições) por grupo */
} s_hist;

static void StatsFree(void) { s_hist.loaded = FALSE; }

static void StatsAddDone(const Item *it, DWORD today, int *day, int *onTime, int *dated)
{
    int ago = DayDiff((DWORD)(it->done / 10000), today);
    if (ago >= 0 && ago < ST_DAYS) day[ago]++;
    if (ago >= 0 && ago < 30 && it->date) {
        (*dated)++;
        if (it->done <= DeadlineStamp(it)) (*onTime)++;
    }
}

/* Conta para o andamento do grupo? (repetições ficam de fora: nunca "terminam") */
static BOOL StatsMilestoneItem(const Item *it) { return !it->parent && !it->recur; }

static void StatsLoad(void)
{
    Item *h;
    int n = 0, i, k;
    DWORD today = Today();
    if (s_hist.loaded && s_hist.today == today) return;
    ZeroMemory(&s_hist, sizeof s_hist);
    s_hist.today = today;
    h = LoadHistory(&n);
    for (i = 0; i < n; i++) {
        Item *it = &h[i];
        if (it->status != S_DONE || !it->done) continue;
        StatsAddDone(it, today, s_hist.day, &s_hist.onTime, &s_hist.dated);
        if (!StatsMilestoneItem(it)) continue;
        for (k = 0; k < s_hist.ng && s_hist.gid[k] != it->group; k++) {}
        if (k == s_hist.ng && s_hist.ng < MAX_GROUPS * 4) { s_hist.gid[k] = it->group; s_hist.closed[k] = 0; s_hist.ng++; }
        if (k < s_hist.ng) s_hist.closed[k]++;
    }
    FreeItems(h, n);
    s_hist.loaded = TRUE;
}

static int StatsHistClosed(DWORD gid)
{
    int k;
    for (k = 0; k < s_hist.ng; k++) if (s_hist.gid[k] == gid) return s_hist.closed[k];
    return 0;
}

/* Painel com título (usado pelos blocos desta tela). */
static void SBox(HDC dc, int x, int y, int w, int h, const WCHAR *title, const WCHAR *sub)
{
    URound(dc, x, y, w, h, MS(14), T.surf, 255);
    URoundLine(dc, x, y, w, h, MS(14), T.border, 255, 1.0f);
    UText(dc, m_f.bold, title, x + MS(20), y + MS(14), w - MS(40), MS(24), T.text, UT_L);
    if (sub) UText(dc, m_f.sm, sub, x + MS(20), y + MS(14), w - MS(40), MS(24), T.text3, UT_R);
}

static void STile(HDC dc, int x, int y, int w, int h, const WCHAR *label, const WCHAR *value, COLORREF vc,
                  const WCHAR *foot, COLORREF fc, int act)
{
    BOOL hot = act && MHOT(act, 0);
    URound(dc, x, y, w, h, MS(14), T.surf, 255);
    URoundLine(dc, x, y, w, h, MS(14), hot ? Mix(T.border, T.accent, 120) : T.border, 255, 1.0f);
    UText(dc, m_f.sm, label, x + MS(18), y + MS(14), w - MS(36), MS(20), T.text2, UT_L);
    UText(dc, m_f.big, value, x + MS(18), y + MS(34), w - MS(36), MS(40), vc, UT_L);
    UText(dc, m_f.sm, foot, x + MS(18), y + h - MS(30), w - MS(36), MS(20), fc, UT_L);
    if (act) ZAdd(&m_z, x, y, w, h, act, 0);
}

static void PaintStats(HDC dc)
{
    int day[ST_DAYS], onTime = s_hist.onTime, dated = s_hist.dated;
    int save = SaveDC(dc), y0 = m_rList.top + MS(4) - m_scroll, y = y0, i, k, z0 = m_z.n;
    int W = m_x1 - m_x0, week = 0, prev = 0, streak = 0, late = 0, maxDay = 1, pendTotal = 0;
    int pend[MAX_STATES];
    DWORD today = Today();
    ULONGLONG now = NowFt();
    WCHAR v[48], f[96];

    StatsLoad();
    CopyMemory(day, s_hist.day, sizeof day);
    ZeroMemory(pend, sizeof pend);
    for (i = 0; i < g_n; i++) {
        const Item *it = &g_items[i];
        if (it->status == S_DONE && it->done) StatsAddDone(it, today, day, &onTime, &dated);
        else if (it->status == S_PENDING) {
            int so = StateOrder(ItemState(it));
            if (so >= 0 && so < MAX_STATES) pend[so]++;
            pendTotal++;
            if (Urgency(it, now, NULL, NULL) == U_LATE) late++;
        }
    }
    for (k = 0; k < 7; k++) week += day[k];
    for (k = 7; k < 14; k++) prev += day[k];
    for (k = day[0] ? 0 : 1; k < ST_DAYS && day[k]; k++) streak++;
    for (k = 0; k < 14; k++) if (day[k] > maxDay) maxDay = day[k];

    IntersectClipRect(dc, m_rList.left - MS(4), m_rList.top, m_rList.right + MS(4), m_rList.bottom);

    /* ---- indicadores ---- */
    {
        int gap = MS(14), tw = (W - gap * 3) / 4, th = MS(100), x = m_x0;
        COLORREF up = week >= prev ? T.success : T.warn;
        wsprintfW(v, L"%d", week);
        if (week == prev) lstrcpyW(f, L"igual à semana anterior");
        else wsprintfW(f, L"%s%d vs semana anterior", week > prev ? L"+" : L"−", week > prev ? week - prev : prev - week);
        STile(dc, x, y, tw, th, L"Concluídas em 7 dias", v, T.text, f, week == prev ? T.text3 : up, 0);
        x += tw + gap;
        if (dated) { wsprintfW(v, L"%d%%", onTime * 100 / dated); wsprintfW(f, L"%d de %d com prazo · 30 dias", onTime, dated); }
        else { lstrcpyW(v, L"—"); lstrcpyW(f, L"nenhuma com prazo em 30 dias"); }
        STile(dc, x, y, tw, th, L"Entregues no prazo", v, !dated ? T.text3 : onTime * 100 >= dated * 80 ? T.success : T.warn,
              f, T.text3, 0);
        x += tw + gap;
        wsprintfW(v, streak == 1 ? L"%d dia" : L"%d dias", streak);
        STile(dc, x, y, tw, th, L"Sequência", v, streak ? T.text : T.text3,
              day[0] ? L"com ao menos 1 concluída por dia" : L"conclua algo hoje para manter", T.text3, 0);
        x += tw + gap;
        wsprintfW(v, L"%d", late);
        STile(dc, x, y, tw, th, L"Atrasadas agora", v, late ? T.danger : T.text,
              late ? L"clique para ver" : L"nada atrasado", late ? T.danger : T.text3, late ? Z_STLATE : 0);
        y += th + MS(18);
    }

    /* ---- atividade: concluídas por dia, últimos 14 dias ---- */
    {
        int h = MS(214), cx = m_x0 + MS(20), cw = W - MS(40), base = y + h - MS(42), top = y + MS(58);
        int bw = cw / 14, barW = bw * 58 / 100;
        wsprintfW(f, L"%d concluída%s em 14 dias", week + prev, week + prev == 1 ? L"" : L"s");
        SBox(dc, m_x0, y, W, h, L"Atividade", f);
        UFill(dc, cx, base, cw, 1, T.border);
        for (k = 13; k >= 0; k--) {
            int col = 13 - k, bx = cx + col * bw + (bw - barW) / 2, bh = day[k] * (base - top) / maxDay;
            WCHAR lb[16];
            if (day[k] && bh < MS(4)) bh = MS(4);
            if (day[k]) URound(dc, bx, base - bh, barW, bh, MS(4), T.accent, k == 0 ? 255 : 150);
            else URound(dc, bx, base - MS(3), barW, MS(3), MS(1), T.text3, 60);
            if (day[k]) { wsprintfW(lb, L"%d", day[k]); UText(dc, m_f.cap, lb, bx - MS(10), base - bh - MS(20), barW + MS(20), MS(18), T.text2, UT_C); }
            if (k == 0) lstrcpyW(lb, L"hoje");
            else FmtDate(AddDays(today, -k), L"ddd", lb, 16);
            UText(dc, k == 0 ? m_f.smB : m_f.sm, lb, cx + col * bw, base + MS(8), bw, MS(20), k == 0 ? T.text : T.text3, UT_C);
        }
        y += h + MS(18);
    }

    /* ---- pendentes por estado ---- */
    {
        int h = MS(124), bx = m_x0 + MS(20), bwid = W - MS(40), by = y + MS(52), x = bx, lx = bx, ly = by + MS(28);
        wsprintfW(f, L"%d pendente%s", pendTotal, pendTotal == 1 ? L"" : L"s");
        SBox(dc, m_x0, y, W, h, L"Pendentes por estado", f);
        URound(dc, bx, by, bwid, MS(12), MS(6), T.surf2, 255);
        for (i = 0; i < g_nStates && pendTotal; i++) {
            int sw = pend[i] * bwid / pendTotal;
            if (!pend[i]) continue;
            if (i == g_nStates - 1 || x + sw > bx + bwid) sw = bx + bwid - x;
            if (sw > 0) URound(dc, x, by, sw, MS(12), MS(6), g_states[i].color, 255);
            x += sw;
        }
        for (i = 0; i < g_nStates; i++) {
            WCHAR t[64];
            int tw;
            wsprintfW(t, L"%s  %d", g_states[i].name, pend[i]);
            tw = UTextW(dc, m_f.sm, t) + MS(30);
            if (lx + tw > bx + bwid) break;
            UDot(dc, (float)(lx + MS(5)), (float)(ly + MS(10)), (float)MS(4), g_states[i].color);
            UText(dc, m_f.sm, t, lx + MS(14), ly, tw, MS(20), pend[i] ? T.text2 : T.text3, UT_L);
            lx += tw;
        }
        y += h + MS(18);
    }

    /* ---- andamento por grupo (estilo milestones) ---- */
    {
        int rows = 0, rh = MS(74), hh, yy;
        for (i = 0; i < g_nGroups; i++) {
            int open = 0, closed = StatsHistClosed(g_groups[i].id);
            for (k = 0; k < g_n; k++) {
                const Item *it = &g_items[k];
                if (ItemGroup(it) != g_groups[i].id || !StatsMilestoneItem(it)) continue;
                if (it->status == S_PENDING) open++; else if (it->status == S_DONE) closed++;
            }
            if (open + closed) rows++;
        }
        hh = MS(56) + (rows ? rows * rh : MS(40)) + MS(8);
        SBox(dc, m_x0, y, W, hh, L"Andamento por grupo", L"tarefas únicas · repetições contam só na atividade");
        yy = y + MS(52);
        if (!rows) UText(dc, m_f.sm, L"Nenhuma tarefa ainda.", m_x0 + MS(20), yy, W - MS(40), MS(30), T.text3, UT_L);
        for (i = 0; i < g_nGroups; i++) {
            Group *g = &g_groups[i];
            int open = 0, closed = StatsHistClosed(g->id), lateG = 0, total, pct, bx = m_x0 + MS(20), bwid = W - MS(40);
            DWORD next = 0;
            BOOL hot = MHOT(Z_STATGROUP, (int)g->id);
            for (k = 0; k < g_n; k++) {
                const Item *it = &g_items[k];
                if (ItemGroup(it) != g->id || !StatsMilestoneItem(it)) continue;
                if (it->status == S_DONE) { closed++; continue; }
                if (it->status != S_PENDING) continue;
                open++;
                if (Urgency(it, now, NULL, NULL) == U_LATE) lateG++;
                else if (it->date && (!next || it->date < next)) next = it->date;
            }
            total = open + closed;
            if (!total) continue;
            pct = closed * 100 / total;
            if (hot) URound(dc, m_x0 + MS(8), yy - MS(4), W - MS(16), rh - MS(4), MS(10), T.surf2, 255);
            if (rows > 1 && yy > y + MS(52)) UFill(dc, bx, yy - MS(6), bwid, 1, T.border);
            UDot(dc, (float)(bx + MS(5)), (float)(yy + MS(12)), (float)MS(5), g->color);
            UText(dc, m_f.bold, g->name, bx + MS(18), yy, bwid / 2, MS(24), T.text, UT_L);
            wsprintfW(v, L"%d%%", pct);
            UText(dc, m_f.bold, v, bx, yy, bwid, MS(24), pct == 100 ? T.success : T.text, UT_R);
            URound(dc, bx, yy + MS(30), bwid, MS(8), MS(4), T.surf2, 255);
            if (closed) URound(dc, bx, yy + MS(30), bwid * closed / total > MS(8) ? bwid * closed / total : MS(8), MS(8), MS(4),
                               pct == 100 ? T.success : g->color, 255);
            wsprintfW(f, L"%d de %d concluídas  ·  %d aberta%s", closed, total, open, open == 1 ? L"" : L"s");
            if (lateG) { WCHAR t[32]; wsprintfW(t, L"  ·  %d atrasada%s", lateG, lateG == 1 ? L"" : L"s"); lstrcatW(f, t); }
            if (next) { WCHAR t[40], d[24]; DayLabel(next, today, d, 24); wsprintfW(t, L"  ·  próximo prazo: %s", d); lstrcatW(f, t); }
            UText(dc, m_f.sm, f, bx, yy + MS(44), bwid, MS(20), lateG ? T.danger : T.text3, UT_L);
            ZAdd(&m_z, m_x0 + MS(8), yy - MS(4), W - MS(16), rh - MS(4), Z_STATGROUP, (int)g->id);
            yy += rh;
        }
        y += hh + MS(24);
    }

    m_contentH = y - y0 + MS(8);
    RestoreDC(dc, save);
    for (i = z0; i < m_z.n; i++) {          /* o que está fora da área visível não recebe clique */
        RECT r;
        if (!IntersectRect(&r, &m_z.z[i].r, &m_rList)) m_z.z[i].act = Z_NONE;
        else m_z.z[i].r = r;
    }
    PaintScrollbar(dc);
}

/* ------------------------------------------------------------------------ */
/* Pintura: configurações                                                   */
/* ------------------------------------------------------------------------ */

#define SROW MS(66)

static int SSection(HDC dc, int y, const WCHAR *title, int rows)
{
    int w = m_x1 - m_x0;
    UText(dc, m_f.cap, title, m_x0 + MS(4), y, w, MS(22), T.text3, UT_L);
    y += MS(28);
    URound(dc, m_x0, y, w, rows * SROW, MS(14), T.surf, 255);
    URoundLine(dc, m_x0, y, w, rows * SROW, MS(14), T.border, 255, 1.0f);
    return y;
}

static int SRow(HDC dc, int y, int i, const WCHAR *label, const WCHAR *desc)
{
    int top = y + i * SROW;
    if (i) UFill(dc, m_x0 + MS(20), top, m_x1 - m_x0 - MS(40), 1, T.border);
    if (desc) {
        UText(dc, m_f.bold, label, m_x0 + MS(20), top + MS(12), MS(320), MS(22), T.text, UT_L);
        UText(dc, m_f.sm, desc, m_x0 + MS(20), top + MS(34), m_x1 - m_x0 - MS(380), MS(20), T.text3, UT_L);
    } else {
        UText(dc, m_f.bold, label, m_x0 + MS(20), top, MS(320), SROW, T.text, UT_L);
    }
    return top;
}

static void SToggle(HDC dc, int top, BOOL on, int key)
{
    int x = m_x1 - MS(20) - MS(40), y = top + (SROW - MS(22)) / 2;
    UToggle(dc, &m_f, x, y, on, MHOT(Z_SET, SETARG(key, !on)));
    ZAdd(&m_z, x - MS(6), y - MS(6), MS(52), MS(34), Z_SET, SETARG(key, !on));
}

static void SSeg(HDC dc, int top, const WCHAR **opts, int n, int sel, int key)
{
    USegment(dc, &m_f, &m_z, m_x1 - MS(20), top + (SROW - MS(34)) / 2, MS(34), opts, n, sel, Z_SET, SETARG(key, 0),
             m_hotA, m_hotG, FALSE);
}

static void SBtn(HDC dc, int top, const WCHAR *text, int key)
{
    int w = UTextW(dc, m_f.bold, text) + MS(36), x = m_x1 - MS(20) - w, y = top + (SROW - MS(34)) / 2;
    UButton(dc, &m_f, x, y, w, MS(34), text, 0, 1, MHOT(Z_SET, SETARG(key, 0)));
    ZAdd(&m_z, x, y, w, MS(34), Z_SET, SETARG(key, 0));
}

static void PaintSettings(HDC dc)
{
    int save = SaveDC(dc), y = m_rList.top + MS(4) - m_scroll, base, top, i, z0 = m_z.n;
    static const WCHAR *lay[2] = { L"Uma janela", L"Uma por grupo" };
    static const WCHAR *thm[3] = { L"Sistema", L"Claro", L"Escuro" };
    static const WCHAR *hor[3] = { L"Só hoje", L"Próximos 7 dias", L"Todas" };
    static const WCHAR *nts[3] = { L"Fixas", L"Ao passar o mouse", L"Só no app" };
    WCHAR t[96];
    IntersectClipRect(dc, m_rList.left - MS(4), m_rList.top, m_rList.right + MS(4), m_rList.bottom);

    base = SSection(dc, y, L"TAREFAS E PRAZOS", 4);
    top = SRow(dc, base, 0, L"Mostrar na área de trabalho", L"Só as de hoje, as dos próximos 7 dias ou todas, qualquer que seja a data");
    SSeg(dc, top, hor, 3, g_cfg.horizon, K_HORIZON);
    top = SRow(dc, base, 1, L"Exibir notas", L"Fixas embaixo da tarefa, num cartão ao passar o mouse ou só aqui no app");
    SSeg(dc, top, nts, 3, g_cfg.notesMode, K_NOTESMODE);
    top = SRow(dc, base, 2, L"Destacar prazos próximos", L"Cor e contagem regressiva a partir de 3 dias antes; mais forte nas últimas 24 h e 2 h");
    SToggle(dc, top, g_cfg.emphasis, K_EMPHASIS);
    top = SRow(dc, base, 3, L"Concluir ao vencer o prazo", L"Quando a data (e a hora) passa, a tarefa é riscada e vai para Concluídas");
    SToggle(dc, top, g_cfg.autoDone, K_AUTODONE);
    y = base + 4 * SROW + MS(30);

    /* ---- estados ---- */
    base = SSection(dc, y, L"ESTADOS", g_nStates + 1);
    for (i = 0; i < g_nStates; i++) {
        State *st = &g_states[i];
        int sid = (int)st->id, cyy, bx, k, cnt = 0, nx = m_x0 + MS(54), nw;
        WCHAR d[64];
        BOOL isDef = st->id == g_cfg.defState;
        top = base + i * SROW;
        cyy = top + SROW / 2;
        if (i) UFill(dc, m_x0 + MS(20), top, m_x1 - m_x0 - MS(40), 1, T.border);
        for (k = 0; k < g_n; k++) if (g_items[k].status == S_PENDING && ItemState(&g_items[k]) == st->id) cnt++;

        /* cor (clique para escolher) */
        if (MHOT(Z_STCOLOR, sid)) UCircle(dc, (float)(m_x0 + MS(32)), (float)cyy, (float)MS(14), T.surf2, 255);
        UDot(dc, (float)(m_x0 + MS(32)), (float)cyy, (float)MS(10), st->color);
        ZAdd(&m_z, m_x0 + MS(18), cyy - MS(14), MS(28), MS(28), Z_STCOLOR, sid);

        /* nome (duplo clique para renomear) */
        nw = UTextW(dc, m_f.bold, st->name);
        if (nw > MS(240)) nw = MS(240);
        if (m_inlineState == st->id) URound(dc, nx - MS(6), top + MS(8), MS(250), MS(28), MS(6), T.field, 255);
        else UText(dc, m_f.bold, st->name, nx, top + MS(10), nw + 2, MS(24), T.text, UT_L);
        ZAdd(&m_z, nx - MS(6), top + MS(8), MS(250), MS(28), Z_STNAME, sid);
        if (isDef && m_inlineState != st->id) {
            int bw = UTextW(dc, m_f.cap, L"PADRÃO") + MS(14);
            URound(dc, nx + nw + MS(10), top + MS(13), bw, MS(18), MS(9), T.accent, T.dark ? 46 : 32);
            UText(dc, m_f.cap, L"PADRÃO", nx + nw + MS(10), top + MS(13), bw, MS(18), T.accent, UT_C);
        }
        wsprintfW(d, L"%d tarefa%s pendente%s", cnt, cnt == 1 ? L"" : L"s", cnt == 1 ? L"" : L"s");
        UText(dc, m_f.sm, d, nx, top + MS(34), MS(260), MS(20), T.text3, UT_L);

        /* botões à direita: renomear · padrão · subir · descer · excluir */
        {
            static const int acts[5] = { Z_STRENAME, Z_STDEF, Z_STUP, Z_STDOWN, Z_STDEL };
            WCHAR ic[5];
            BOOL en[5];
            ic[0] = IC_EDIT; ic[1] = isDef ? IC_STARFILL : IC_STAR; ic[2] = IC_CHEVUP; ic[3] = IC_CHEVDOWN; ic[4] = IC_DELETE;
            en[0] = TRUE; en[1] = !isDef; en[2] = i > 0; en[3] = i < g_nStates - 1; en[4] = !isDef && g_nStates > 1;
            bx = m_x1 - MS(20) - 5 * MS(34);
            for (k = 0; k < 5; k++) {
                int x = bx + k * MS(34), yy = cyy - MS(15);
                BOOL hot = en[k] && MHOT(acts[k], sid);
                COLORREF c = !en[k] ? (k == 1 && isDef ? T.warn : Mix(T.surf, T.text3, 110)) : k == 4 ? T.danger : T.text2;
                if (hot) URound(dc, x, yy, MS(30), MS(30), MS(8), k == 4 ? T.danger : T.surf2, k == 4 ? 30 : 255);
                UIcon(dc, m_f.iconSm, ic[k], x + MS(15), cyy, c);
                if (en[k]) ZAdd(&m_z, x, yy, MS(30), MS(30), acts[k], sid);
            }
        }
    }
    top = base + g_nStates * SROW;
    if (g_nStates) UFill(dc, m_x0 + MS(20), top, m_x1 - m_x0 - MS(40), 1, T.border);
    if (g_nStates < MAX_STATES) {
        int bw = UTextW(dc, m_f.bold, L"Novo estado") + MS(60);
        UButton(dc, &m_f, m_x0 + MS(20), top + (SROW - MS(34)) / 2, bw, MS(34), L"Novo estado", IC_ADD, 1, MHOT(Z_STADD, 0));
        ZAdd(&m_z, m_x0 + MS(20), top + (SROW - MS(34)) / 2, bw, MS(34), Z_STADD, 0);
        UText(dc, m_f.sm, L"Clique na bolinha para mudar a cor · duplo clique no nome para renomear",
              m_x0 + MS(36) + bw, top, m_x1 - m_x0 - bw - MS(56), SROW, T.text3, UT_L);
    } else {
        UText(dc, m_f.sm, L"Limite de 16 estados.", m_x0 + MS(20), top, MS(300), SROW, T.text3, UT_L);
    }
    y = base + (g_nStates + 1) * SROW + MS(30);

    base = SSection(dc, y, L"ÁREA DE TRABALHO", 10);
    top = SRow(dc, base, 0, L"Organização", L"Todos os grupos juntos ou cada grupo em sua própria janela");
    SSeg(dc, top, lay, 2, g_cfg.layout, K_LAYOUT);
    top = SRow(dc, base, 1, L"Travar posição", L"Destrave para arrastar as janelas pela área de trabalho");
    SToggle(dc, top, g_cfg.locked, K_LOCK);
    top = SRow(dc, base, 2, L"Mostrar data", L"Dia da semana e data acima da lista");
    SToggle(dc, top, g_cfg.showDate, K_DATE);
    top = SRow(dc, base, 3, L"Ocultar grupos vazios", L"No modo uma por grupo, esconde janelas sem tarefas");
    SToggle(dc, top, g_cfg.hideEmpty, K_HIDEEMPTY);
    top = SRow(dc, base, 4, L"Sombra no texto", L"Melhora a leitura sobre papéis de parede claros");
    SToggle(dc, top, g_cfg.shadow, K_SHADOW);
    top = SRow(dc, base, 5, L"Largura", NULL);
    for (i = 0; i < 4 && kWidths[i] != g_cfg.width; i++) {}
    SSeg(dc, top, kWidthName, 4, i, K_WIDTH);
    top = SRow(dc, base, 6, L"Opacidade", NULL);
    for (i = 0; i < 4 && kOpac[i] != g_cfg.opacity; i++) {}
    SSeg(dc, top, kOpacName, 4, i, K_OPAC);
    top = SRow(dc, base, 7, L"Fonte", NULL);
    if (g_cfg.size10 % 10) wsprintfW(t, L"%s  ·  %d,%d pt", g_cfg.face, g_cfg.size10 / 10, g_cfg.size10 % 10);
    else wsprintfW(t, L"%s  ·  %d pt", g_cfg.face, g_cfg.size10 / 10);
    SBtn(dc, top, t, K_FONT);
    top = SRow(dc, base, 8, L"Cor do texto", NULL);
    {
        int r = MS(11), cx = m_x1 - MS(20) - r, cy = top + SROW / 2;
        BOOL custom = TRUE;
        if (MHOT(Z_SET, SETARG(K_COLOR, 0))) UCircle(dc, (float)cx, (float)cy, (float)r + 2, T.surf2, 255);
        UCircleLine(dc, (float)cx, (float)cy, (float)r, T.border, 255, 1.2f);
        UIcon(dc, m_f.iconSm, IC_ADD, cx, cy, T.text2);
        ZAdd(&m_z, cx - r - 3, cy - r - 3, r * 2 + 6, r * 2 + 6, Z_SET, SETARG(K_COLOR, 0));
        for (i = 7; i >= 0; i--) {
            cx -= r * 2 + MS(10);
            if (kTextSwatch[i] == g_cfg.color) { custom = FALSE; UCircleLine(dc, (float)cx, (float)cy, (float)r + 3.5f, T.accent, 255, 2.0f); }
            UCircle(dc, (float)cx, (float)cy, (float)r, kTextSwatch[i], 255);
            UCircleLine(dc, (float)cx, (float)cy, (float)r, T.border, 255, 1.0f);
            ZAdd(&m_z, cx - r - 3, cy - r - 3, r * 2 + 6, r * 2 + 6, Z_SWATCH, i);
        }
        if (custom) {
            cx -= r * 2 + MS(18);
            UCircle(dc, (float)cx, (float)cy, (float)r, g_cfg.color, 255);
            UCircleLine(dc, (float)cx, (float)cy, (float)r + 3.5f, T.accent, 255, 2.0f);
        }
    }
    {
        static const WCHAR *sd[3] = { L"Não mostrar", L"Cor", L"Cor e nome" };
        top = SRow(dc, base, 9, L"Estado na área de trabalho", L"O marcador da tarefa ganha a cor do estado; opcionalmente, o nome ao lado");
        SSeg(dc, top, sd, 3, g_cfg.stateDesk, K_STATEDESK);
    }
    y = base + 10 * SROW + MS(30);

    base = SSection(dc, y, L"PAINEL", 1);
    top = SRow(dc, base, 0, L"Tema", L"Segue o Windows ou escolha claro/escuro");
    SSeg(dc, top, thm, 3, g_cfg.theme, K_THEME);
    y = base + SROW + MS(30);

    base = SSection(dc, y, L"GERAL", 2);
    top = SRow(dc, base, 0, L"Iniciar com o Windows", L"O Task Killer abre sozinho quando você liga o computador");
    SToggle(dc, top, AutoGet(), K_AUTOSTART);
    top = SRow(dc, base, 1, L"Atalho de nova tarefa", L"Funciona em qualquer lugar do Windows");
    {
        int w = UTextW(dc, m_f.smB, L"Ctrl + Alt + T") + MS(28), x = m_x1 - MS(20) - w, yy = top + (SROW - MS(30)) / 2;
        URound(dc, x, yy, w, MS(30), MS(8), T.surf2, 255);
        URoundLine(dc, x, yy, w, MS(30), MS(8), T.border, 255, 1.0f);
        UText(dc, m_f.smB, L"Ctrl + Alt + T", x, yy, w, MS(30), T.text2, UT_C);
    }
    y = base + 2 * SROW + MS(30);

    base = SSection(dc, y, L"DADOS E CALENDÁRIO", 5);
    top = SRow(dc, base, 0, L"Exportar para calendário", L"Arquivo .ics para Google Agenda, Outlook ou Calendário do Windows");
    SBtn(dc, top, L"Exportar…", K_EXPORT);
    top = SRow(dc, base, 1, L"Importar de calendário", L"Traz eventos de hoje em diante de um arquivo .ics");
    SBtn(dc, top, L"Importar…", K_IMPORT);
    top = SRow(dc, base, 2, L"Arquivar concluídas", L"Move as concluídas para o histórico agora");
    SBtn(dc, top, L"Arquivar", K_ARCHIVE);
    top = SRow(dc, base, 3, L"Histórico completo", L"Abre o arquivo de histórico no Bloco de Notas");
    SBtn(dc, top, L"Abrir", K_HISTORY);
    top = SRow(dc, base, 4, L"Pasta de dados", g_dir);
    SBtn(dc, top, L"Abrir pasta", K_FOLDER);
    y = base + 5 * SROW + MS(24);

    m_contentH = y - (m_rList.top + MS(4) - m_scroll) + MS(24);
    RestoreDC(dc, save);

    /* zonas fora da área visível não podem receber cliques */
    for (i = z0; i < m_z.n; i++) {
        RECT r;
        if (!IntersectRect(&r, &m_z.z[i].r, &m_rList)) m_z.z[i].act = Z_NONE;
        else m_z.z[i].r = r;
    }
    PaintScrollbar(dc);
}

/* ------------------------------------------------------------------------ */
/* Pintura: detalhes                                                        */
/* ------------------------------------------------------------------------ */

static void FieldLabel(HDC dc, int y, const WCHAR *t)
{
    UText(dc, m_f.base, t, m_detX + MS(24), y, MS(96), MS(46), T.text2, UT_L);
}

static void PaintDetail(HDC dc, int cw, int ch)
{
    Item *it = GetItem(m_sel);
    int dx = m_detX, px = dx + MS(24), pr = cw - MS(24), cx = px + MS(104), y0 = MS(140), rh = MS(46), y, w;
    WCHAR t[96], c1[32];
    BOOL done;
    float sc = (float)m_f.dpi / 96.0f;
    if (!it) return;
    done = it->status != S_PENDING;

    UFill(dc, dx, 0, cw - dx, ch, T.surf);
    UFill(dc, dx, 0, 1, ch, T.border);

    UText(dc, m_f.cap, L"DETALHES", px, MS(22), MS(200), MS(24), T.text3, UT_L);
    if (MHOT(Z_DCLOSE, 0)) URound(dc, pr - MS(32), MS(18), MS(32), MS(32), MS(8), T.surf2, 255);
    UIcon(dc, m_f.iconSm, IC_CLOSE, pr - MS(16), MS(34), T.text2);
    ZAdd(&m_z, pr - MS(32), MS(18), MS(32), MS(32), Z_DCLOSE, 0);

    UCheckCircle(dc, (float)(px + MS(12)), (float)MS(80), (float)MS(11), done, MHOT(Z_DCHECK, 0), T.text3, sc);
    ZAdd(&m_z, px, MS(64), MS(28), MS(32), Z_DCHECK, 0);
    URoundLine(dc, px + MS(30), MS(62), pr - px - MS(30), MS(36), MS(8),
               GetFocus() == m_title ? T.accent : T.surf, 255, 1.0f);

    {
        WCHAR cr[32];
        FmtDate((DWORD)(it->created / 10000), L"dd/MM/yyyy", cr, 32);
        if (done && it->done) {
            FmtDate((DWORD)(it->done / 10000), L"dd/MM", c1, 32);
            wsprintfW(t, L"Criada em %s  ·  concluída em %s", cr, c1);
        } else wsprintfW(t, L"Criada em %s", cr);
        {   /* estado: selo clicável logo abaixo do título */
            DWORD sid = ItemState(it);
            int sw = UChip(dc, &m_f, px + MS(36), MS(100), MS(26), 0, StateColor(sid), TRUE, StateName(sid), TRUE,
                           MHOT(Z_DSTATE, 0), TRUE);
            ZAdd(&m_z, px + MS(36), MS(100), sw, MS(26), Z_DSTATE, 0);
            UText(dc, m_f.sm, t, px + MS(36) + sw + MS(10), MS(100), pr - px - MS(46) - sw, MS(26), T.text3, UT_L);
        }
    }

    /* Grupo */
    y = y0;
    FieldLabel(dc, y, L"Grupo");
    w = UChip(dc, &m_f, cx, y + MS(8), MS(30), 0, GroupColor(ItemGroup(it)), TRUE, GroupName(ItemGroup(it)), TRUE,
              MHOT(Z_DGROUP, 0), TRUE);
    ZAdd(&m_z, cx, y + MS(8), w, MS(30), Z_DGROUP, 0);
    y += rh;

    /* Tipo */
    FieldLabel(dc, y, L"Tipo");
    USegment(dc, &m_f, &m_z, cx, y + MS(7), MS(32), kTypeName, 3, it->type, Z_DTYPE, 0, m_hotA, m_hotG, TRUE);
    y += rh;

    /* Data */
    FieldLabel(dc, y, L"Data");
    if (it->date) FmtDate(it->date, L"ddd, dd 'de' MMM 'de' yyyy", t, 96); else lstrcpyW(t, L"Sem data");
    w = UChip(dc, &m_f, cx, y + MS(8), MS(30), IC_CALENDAR, T.accent, FALSE, t, it->date != 0, MHOT(Z_DDATE, 0), TRUE);
    ZAdd(&m_z, cx, y + MS(8), w, MS(30), Z_DDATE, 0);
    y += rh;

    /* Hora (campo de texto) */
    FieldLabel(dc, y, L"Hora");
    URound(dc, cx, y + MS(6), MS(110), MS(34), MS(8), T.field, 255);
    URoundLine(dc, cx, y + MS(6), MS(110), MS(34), MS(8), GetFocus() == m_time ? T.accent : T.border, 255, 1.0f);
    if (MHOT(Z_DTIMEPICK, 0)) URound(dc, cx + MS(80), y + MS(9), MS(27), MS(28), MS(7), T.surf2, 255);
    UIcon(dc, m_f.iconSm, IC_CLOCK, cx + MS(93), y + MS(23), MHOT(Z_DTIMEPICK, 0) ? T.accent : T.text2);
    ZAdd(&m_z, cx + MS(78), y + MS(6), MS(32), MS(34), Z_DTIMEPICK, 0);
    if (it->time >= 0) {
        if (MHOT(Z_DTIMECLR, 0)) URound(dc, cx + MS(118), y + MS(9), MS(28), MS(28), MS(7), T.surf2, 255);
        UIcon(dc, m_f.iconSm, IC_CLOSE, cx + MS(132), y + MS(23), T.text3);
        ZAdd(&m_z, cx + MS(118), y + MS(9), MS(28), MS(28), Z_DTIMECLR, 0);
    }
    y += rh;

    /* Repetir */
    FieldLabel(dc, y, L"Repetir");
    RecurLabel(it->recur, it->wdays, t, 96);
    w = UChip(dc, &m_f, cx, y + MS(8), MS(30), IC_REPEAT, T.accent, FALSE, t, it->recur != 0,
              MHOT(Z_DRECUR, 0), TRUE);
    ZAdd(&m_z, cx, y + MS(8), w, MS(30), Z_DRECUR, 0);
    y += rh;

    /* Importante */
    FieldLabel(dc, y, L"Importante");
    UToggle(dc, &m_f, cx, y + MS(12), it->prio != 0, MHOT(Z_DPRIO, 0));
    ZAdd(&m_z, cx - MS(6), y + MS(6), MS(52), MS(34), Z_DPRIO, 0);
    y += rh;

    /* Exibir na área de trabalho */
    FieldLabel(dc, y, L"Exibir");
    {
        int sm = ShowMode(it);
        WCHAR st[64];
        if (sm == SH_DEFAULT) {
            static const WCHAR *hz[3] = { L"só hoje", L"7 dias", L"todas" };
            wsprintfW(st, L"Padrão (%s)", hz[g_cfg.horizon]);
        } else lstrcpynW(st, kShowName[sm], 64);
        w = UChip(dc, &m_f, cx, y + MS(8), MS(30), sm == SH_NEVER ? IC_HIDE : IC_DESKTOP, T.accent, FALSE, st,
                  sm != SH_DEFAULT, MHOT(Z_DSHOW, 0), TRUE);
        ZAdd(&m_z, cx, y + MS(8), w, MS(30), Z_DSHOW, 0);
    }
    y += rh;

    /* Notas */
    UText(dc, m_f.base, L"Notas e descrição", px, y + MS(8), MS(220), MS(24), T.text2, UT_L);
    y += MS(40);
    {
        int nb = ch - MS(84);
        if (nb < y + MS(80)) nb = y + MS(80);
        URound(dc, px, y, pr - px, nb - y, MS(10), T.field, 255);
        URoundLine(dc, px, y, pr - px, nb - y, MS(10), GetFocus() == m_notes ? T.accent : T.border, 255, 1.0f);
    }

    /* ações */
    {
        int by = ch - MS(60), bh = MS(40), bw = MS(160);
        UButton(dc, &m_f, px, by, bw, bh, done ? L"Restaurar" : L"Concluir", done ? IC_UNDO : IC_CHECK, done ? 1 : 0,
                MHOT(Z_DDONE, 0));
        ZAdd(&m_z, px, by, bw, bh, Z_DDONE, 0);
        UButton(dc, &m_f, pr - MS(118), by, MS(118), bh, L"Excluir", IC_DELETE, 3, MHOT(Z_DDELETE, 0));
        ZAdd(&m_z, pr - MS(118), by, MS(118), bh, Z_DDELETE, 0);
    }
}

static void PaintToast(HDC dc)
{
    int w, h = MS(46), x, y, tw, uw = 0;
    if (!m_toast[0]) return;
    tw = UTextW(dc, m_f.base, m_toast);
    if (m_toastKind) uw = UTextW(dc, m_f.bold, L"Desfazer") + MS(28);
    w = tw + uw + MS(44);
    x = (m_x0 + m_x1) / 2 - w / 2;
    y = m_rList.bottom - h - MS(24);
    URound(dc, x, y, w, h, MS(12), T.toast, 255);
    UText(dc, m_f.base, m_toast, x + MS(22), y, tw + 4, h, T.toastText, DT_SINGLELINE | DT_VCENTER);
    if (m_toastKind) {
        int ux = x + MS(22) + tw + MS(14);
        if (MHOT(Z_TOAST, 0)) URound(dc, ux, y + MS(7), uw, h - MS(14), MS(8), RGB(255, 255, 255), 24);
        UText(dc, m_f.bold, L"Desfazer", ux, y, uw, h, T.dark ? T.accent : RGB(150, 182, 255), UT_C);
        ZAdd(&m_z, ux, y + MS(4), uw, h - MS(8), Z_TOAST, 0);
    }
}

static void MPaint(HDC dc, int cw, int ch)
{
    ZClear(&m_z);
    UFill(dc, 0, 0, cw, ch, T.bg);
    PaintSidebar(dc, ch);
    PaintHeader(dc);
    if (m_view == V_SETTINGS) PaintSettings(dc);
    else if (m_view == V_STATS) PaintStats(dc);
    else {
        if (ShowCards()) PaintCards(dc);
        PaintToolbar(dc);
        if (BoardOn()) PaintBoard(dc); else PaintList(dc);
    }
    if (m_sel && GetItem(m_sel) && !PageView() && m_view != V_HISTORY) PaintDetail(dc, cw, ch);
    PaintToast(dc);
}

/* ------------------------------------------------------------------------ */
/* Edição de campos                                                         */
/* ------------------------------------------------------------------------ */

static void MLoadDetail(void)
{
    Item *it = GetItem(m_sel);
    WCHAR t[8];
    m_loading = TRUE;
    if (it) {
        SetWindowTextW(m_title, it->text);
        SetWindowTextW(m_notes, it->notes ? it->notes : L"");
        FmtTime(it->time, t);
        SetWindowTextW(m_time, t);
    }
    m_loading = FALSE;
}

static void MSyncTime(void)
{
    Item *it = GetItem(m_sel);
    WCHAR t[8];
    if (!it || GetFocus() == m_time) return;
    m_loading = TRUE;
    FmtTime(it->time, t);
    SetWindowTextW(m_time, t);
    m_loading = FALSE;
}

/* "14:30", "1430", "14h", "14h30", "9" → HHMM; -1 = vazio; -2 = inválido */
static int MParseTime(const WCHAR *s)
{
    WCHAR f[24];
    int k = 0, v;
    for (; *s && k < 23; s++) if (*s != L' ') f[k++] = Fold(*s);
    f[k] = 0;
    if (!k) return -1;
    if ((v = TimeWord(f)) >= 0) return v;
    if (k <= 2 && f[0] >= L'0' && f[0] <= L'9') {
        v = f[0] - L'0';
        if (k == 2) { if (f[1] < L'0' || f[1] > L'9') return -2; v = v * 10 + f[1] - L'0'; }
        return v <= 23 ? v * 100 : -2;
    }
    if (k == 4) {
        int i;
        for (i = 0; i < 4; i++) if (f[i] < L'0' || f[i] > L'9') return -2;
        v = (f[0] - L'0') * 1000 + (f[1] - L'0') * 100 + (f[2] - L'0') * 10 + (f[3] - L'0');
        return v / 100 <= 23 && v % 100 <= 59 ? v : -2;
    }
    return -2;
}

static void MCommitTitle(void)
{
    Item *it;
    WCHAR t[TEXT_MAX];
    if (m_loading || !(it = GetItem(m_sel))) return;
    EditText(m_title, t, TEXT_MAX);
    CleanText(t);
    if (!t[0]) { MLoadDetail(); return; }
    if (lstrcmpW(t, it->text)) { lstrcpyW(it->text, t); Changed(); }
}

static void MCommitNotes(void)
{
    Item *it;
    int n;
    WCHAR *t;
    if (m_loading || !(it = GetItem(m_sel))) return;
    n = GetWindowTextLengthW(m_notes);
    t = (WCHAR *)Alloc(((SIZE_T)n + 2) * sizeof(WCHAR));
    if (!t) return;
    GetWindowTextW(m_notes, t, n + 1);
    if (!NotesEqual(t, it->notes)) { ItemSetNotes(it, t[0] ? t : NULL); Changed(); }
    Free(t);
}

static void MCommitTime(void)
{
    Item *it;
    WCHAR t[24];
    int v;
    if (m_loading || !(it = GetItem(m_sel))) return;
    EditText(m_time, t, 24);
    v = MParseTime(t);
    if (v == -2) { MessageBeep(MB_ICONWARNING); MSyncTime(); return; }
    if (v != it->time) {
        it->time = v;
        if (v >= 0 && !it->date) it->date = Today();
        if (v < 0 && it->type != T_TASK) it->type = T_TASK;
        ItemTimingChanged(it);
        Changed();
    }
    m_loading = TRUE;
    FmtTime(it->time, t);
    SetWindowTextW(m_time, t);
    m_loading = FALSE;
}

static void MCommitAll(void)
{
    MCommitTitle();
    MCommitNotes();
    MCommitTime();
}

static void MSelect(DWORD id)
{
    if (m_sel && id != m_sel) MCommitAll();
    m_sel = id;
    if (GetFocus() == m_title || GetFocus() == m_notes || GetFocus() == m_time) {
        m_loading = TRUE;
        SetFocus(m_hwnd);
        m_loading = FALSE;
    }
    MLoadDetail();
    MLayout();
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

static void MSetView(int v, DWORD gid)
{
    if (v == m_view && (v != V_GROUP || gid == m_viewGroup)) return;
    if (m_view == V_STATS && v != V_STATS) StatsFree();
    if (m_view == V_HISTORY && v != V_HISTORY) { FreeItems(m_hist, m_nHist); m_hist = NULL; m_nHist = 0; }
    if (v == V_HISTORY && !m_hist) m_hist = LoadHistory(&m_nHist);
    m_view = v;
    m_viewGroup = gid;
    m_scroll = 0;
    m_hscroll = 0;
    if (v == V_SETTINGS || v == V_HISTORY || v == V_STATS) MSelect(0);
    else { MLayout(); InvalidateRect(m_hwnd, NULL, FALSE); }
    BuildList();
    MLayout();
}

static void MMoveSel(int dir)
{
    int i, cur = -1, k;
    for (i = 0; i < m_nList; i++) if (m_list[i].kind == LE_ITEM && g_items[m_list[i].idx].id == m_sel) cur = i;
    for (k = cur + dir; k >= 0 && k < m_nList; k += dir)
        if (m_list[k].kind == LE_ITEM) { MSelect(g_items[m_list[k].idx].id); MEnsureVisible(m_sel); return; }
    if (cur < 0)
        for (k = 0; k < m_nList; k++) if (m_list[k].kind == LE_ITEM) { MSelect(g_items[m_list[k].idx].id); return; }
}

static void MainToast(const WCHAR *text, int kind, DWORD id)
{
    if (!m_hwnd) return;
    lstrcpynW(m_toast, text, 96);
    m_toastKind = kind;
    m_toastId = id;
    SetTimer(m_hwnd, TMR_TOAST, 5000, NULL);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

static void MToggleDone(DWORD id)
{
    Item *it = GetItem(id);
    if (!it) return;
    if (it->status == S_PENDING) {
        Complete(id);
        MainToast(L"Tarefa concluída", 1, g_lastDoneId);
    } else {
        Restore(id);
        MainToast(L"Tarefa restaurada", 0, 0);
    }
}

static void MRestoreHistory(int idx)
{
    Item *src, *it;
    if (idx < 0 || idx >= m_nHist) return;
    src = &m_hist[idx];
    it = NewItem();
    if (!it) return;
    *it = *src;
    it->notes = StrDupW(src->notes);
    it->id = g_nextId++;
    it->parent = 0;
    it->status = S_PENDING;
    it->done = 0;
    it->notified = 1;
    it->flags &= F_SHOW_MASK;
    KeepIfPast(it);
    it->group = FindGroup(src->group) >= 0 ? src->group : g_cfg.defGroup;
    Animate(it, A_IN);
    Changed();
    MainToast(L"Tarefa restaurada para a lista", 0, 0);
}

/* inline: renomear grupo na barra lateral */
static void MBeginRename(DWORD gid)
{
    Group *g = GetGroup(gid);
    Zone *z;
    if (!g) return;
    m_inlineGroup = gid;
    InvalidateRect(m_hwnd, NULL, FALSE);
    UpdateWindow(m_hwnd);
    z = ZFind(&m_z, Z_GROUP, (int)gid);
    if (!z) { m_inlineGroup = 0; return; }
    MoveWindow(m_inline, z->r.left + MS(40), z->r.top + (z->r.bottom - z->r.top - m_f.lhBase) / 2,
               z->r.right - z->r.left - MS(52), m_f.lhBase, TRUE);
    SetWindowTextW(m_inline, g->name);
    ShowWindow(m_inline, SW_SHOW);
    SetFocus(m_inline);
    SendMessageW(m_inline, EM_SETSEL, 0, -1);
}

static void MBeginRenameState(DWORD sid)
{
    State *st = GetState(sid);
    Zone *z;
    int tries;
    if (!st || !m_hwnd) return;
    if (m_view != V_SETTINGS) MSetView(V_SETTINGS, 0);
    m_inlineGroup = 0;
    m_inlineState = sid;
    for (tries = 0; tries < 2; tries++) {
        InvalidateRect(m_hwnd, NULL, FALSE);
        UpdateWindow(m_hwnd);
        z = ZFind(&m_z, Z_STNAME, (int)sid);
        if (z && z->r.bottom - z->r.top >= MS(24)) {
            MoveWindow(m_inline, z->r.left + MS(6), z->r.top + (z->r.bottom - z->r.top - m_f.lhBase) / 2,
                       z->r.right - z->r.left - MS(12), m_f.lhBase, TRUE);
            SetWindowTextW(m_inline, st->name);
            ShowWindow(m_inline, SW_SHOW);
            SetFocus(m_inline);
            SendMessageW(m_inline, EM_SETSEL, 0, -1);
            return;
        }
        /* a linha está fora da área visível: rola até ela e tenta de novo */
        m_scroll = m_contentH;
        ClampScroll();
    }
    m_inlineState = 0;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

static void MEndRename(BOOL commit)
{
    DWORD gid = m_inlineGroup;
    WCHAR t[NAME_MAX];
    Group *g;
    if (m_inlineState) {
        DWORD sid = m_inlineState;
        State *st;
        m_inlineState = 0;
        EditText(m_inline, t, NAME_MAX);
        CleanText(t);
        ShowWindow(m_inline, SW_HIDE);
        st = GetState(sid);
        if (commit && st && t[0] && lstrcmpW(t, st->name)) { lstrcpynW(st->name, t, NAME_MAX); StatesChanged(); }
        else InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    }
    if (!gid) return;
    m_inlineGroup = 0;
    EditText(m_inline, t, NAME_MAX);
    CleanText(t);
    ShowWindow(m_inline, SW_HIDE);
    g = GetGroup(gid);
    if (commit && g && t[0] && lstrcmpW(t, g->name)) { lstrcpynW(g->name, t, NAME_MAX); GroupsChanged(); }
    else InvalidateRect(m_hwnd, NULL, FALSE);
}

static void MTimeCb(void *ctx, int t)
{
    Item *it = GetItem(m_sel);
    (void)ctx;
    if (!it) return;
    it->time = t;
    if (t >= 0 && !it->date) it->date = Today();
    if (t < 0 && it->type != T_TASK) it->type = T_TASK;
    ItemTimingChanged(it);
    Changed();
    MSyncTime();
}

static void MDaysCb(void *ctx, int mask)
{
    Item *it = GetItem(m_sel);
    (void)ctx;
    if (!it || mask <= 0) return;
    it->recur = R_DAYS;
    it->wdays = (BYTE)mask;
    it->date = AlignToDays(it->date && it->date >= Today() ? it->date : Today(), mask);
    ItemTimingChanged(it);
    Changed();
}

static void MDateCb(void *ctx, DWORD d)
{
    Item *it = GetItem(m_sel);
    (void)ctx;
    if (!it) return;
    it->date = d;
    if (!d) { it->time = -1; it->recur = R_NONE; if (it->type != T_TASK) it->type = T_TASK; }
    ItemTimingChanged(it);
    Changed();
    MSyncTime();
}

static void MSetting(int key, int v)
{
    switch (key) {
    case K_LAYOUT:    g_cfg.layout = v; ConfigChanged(); break;
    case K_LOCK:      g_cfg.locked = v; ConfigChanged(); break;
    case K_DATE:      g_cfg.showDate = v; ConfigChanged(); break;
    case K_HIDEEMPTY: g_cfg.hideEmpty = v; ConfigChanged(); break;
    case K_STATEDESK: if (v <= 2) { g_cfg.stateDesk = v; ConfigChanged(); } break;
    case K_HORIZON:   if (v <= H_ALL) { g_cfg.horizon = v; ConfigChanged(); } break;
    case K_NOTESMODE: if (v <= N_APP) { g_cfg.notesMode = v; TipHide(); ConfigChanged(); } break;
    case K_EMPHASIS:  g_cfg.emphasis = v; ConfigChanged(); Reschedule(); break;
    case K_AUTODONE:
        g_cfg.autoDone = v;
        if (v) { g_cfg.autoSinceDate = (int)Today(); g_cfg.autoSinceTime = NowTime(); }  /* vale a partir de agora */
        ConfigChanged();
        Reschedule();
        break;
    case K_SHADOW:    g_cfg.shadow = v; ConfigChanged(); break;
    case K_WIDTH:     if (v < 4) { g_cfg.width = kWidths[v]; ConfigChanged(); } break;
    case K_OPAC:      if (v < 4) { g_cfg.opacity = kOpac[v]; ConfigChanged(); } break;
    case K_FONT:      ChooseTextFont(m_hwnd); break;
    case K_COLOR:     ChooseTextColor(m_hwnd); break;
    case K_THEME:     g_cfg.theme = v; SaveConfig(); UiLoadTheme(); MainApplyTheme(); break;
    case K_AUTOSTART: AutoSet(v); InvalidateRect(m_hwnd, NULL, FALSE); break;
    case K_EXPORT:    ExportIcs(m_hwnd); break;
    case K_IMPORT:    ImportIcs(m_hwnd); break;
    case K_ARCHIVE:   ArchiveOld(TRUE); Changed(); MainToast(L"Concluídas movidas para o histórico", 0, 0); break;
    case K_HISTORY:
        if (GetFileAttributesW(g_histPath) == INVALID_FILE_ATTRIBUTES) WriteUtf8(g_histPath, L"", 0, TRUE);
        ShellExecuteW(NULL, L"open", L"notepad.exe", g_histPath, NULL, SW_SHOWNORMAL);
        break;
    case K_FOLDER:    ShellExecuteW(NULL, L"open", g_dir, NULL, NULL, SW_SHOWNORMAL); break;
    }
}

static void MAct(int a, int g)
{
    Item *it = GetItem(m_sel);
    POINT pt;
    int v;
    switch (a) {
    case Z_NEW: case Z_EMPTYADD: QuickOpen(m_view == V_GROUP ? m_viewGroup : 0); break;
    case Z_NAV:      MSetView(g, 0); break;
    case Z_CARD:     MSetView(g, 0); break;
    case Z_GROUP:    MSetView(V_GROUP, (DWORD)g); break;
    case Z_GROUPADD: {
        Group *ng = NewGroup(L"Novo grupo", NextGroupColor());
        if (ng) { DWORD id = ng->id; GroupsChanged(); MSetView(V_GROUP, id); MBeginRename(id); }
        break;
    }
    case Z_GROUPMENU: pt = ZoneScreen(m_hwnd, &m_z, Z_GROUPMENU, 0, TRUE); GroupMenu(m_viewGroup, pt, m_hwnd, TRUE); break;
    case Z_LOCK:     g_cfg.locked = !g_cfg.locked; ConfigChanged(); break;
    case Z_ROW:      MSelect((DWORD)g); break;
    case Z_CHECK:    MToggleDone((DWORD)g); break;
    case Z_HCHECK:   MRestoreHistory(g); break;
    case Z_DCLOSE:   MSelect(0); break;
    case Z_DCHECK: case Z_DDONE: if (it) MToggleDone(it->id); break;
    case Z_DGROUP:
        if (it) {
            HMENU m = CreatePopupMenu();
            UINT c;
            int i;
            for (i = 0; i < g_nGroups; i++)
                AppendMenuW(m, MF_STRING | (g_groups[i].id == ItemGroup(it) ? MF_CHECKED : 0), (UINT_PTR)(i + 1), g_groups[i].name);
            c = RunMenu(m, ZoneScreen(m_hwnd, &m_z, Z_DGROUP, 0, TRUE), m_hwnd);
            if (c >= 1 && (int)c <= g_nGroups) MoveToGroup(m_sel, g_groups[c - 1].id);
        }
        break;
    case Z_DTYPE:
        if (it && g >= 0 && g <= T_ALARM && it->type != g) {
            it->type = (BYTE)g;
            if (g != T_TASK && it->time < 0) {
                int hr = NowTime() / 100 + 1;
                if (!it->date) it->date = Today();
                if (hr > 23) { hr = 9; if (it->date == Today()) it->date = AddDays(Today(), 1); }
                it->time = hr * 100;
            }
            ItemTimingChanged(it);
            Changed();
            MSyncTime();
        }
        break;
    case Z_DDATE:
        if (it) { pt = ZoneScreen(m_hwnd, &m_z, Z_DDATE, 0, TRUE); CalOpen(m_hwnd, pt.x, pt.y, it->date, MDateCb, NULL); }
        break;
    case Z_DTIMECLR:
        if (it) { it->time = -1; if (it->type != T_TASK) it->type = T_TASK; ItemTimingChanged(it); Changed(); MSyncTime(); }
        break;
    case Z_DTIMEPICK:
        if (it) {
            MCommitTime();
            it = GetItem(m_sel);
            pt = ZoneScreen(m_hwnd, &m_z, Z_DTIMEPICK, 0, TRUE);
            if (it) TimeOpen(m_hwnd, pt.x - MS(78), pt.y, it->time, MTimeCb, NULL);
        }
        break;
    case Z_DRECUR:
        if (it) {
            pt = ZoneScreen(m_hwnd, &m_z, Z_DRECUR, 0, TRUE);
            v = UPickMenu(m_hwnd, pt, kRecurName, 6, it->recur);
            it = GetItem(m_sel);
            if (it && v == R_DAYS) DaysOpen(m_hwnd, pt.x, pt.y, it->recur == R_DAYS ? it->wdays : 0x2A, MDaysCb, NULL);
            else if (it && v >= 0) {
                it->recur = (BYTE)v; it->wdays = 0;
                if (v && !it->date) it->date = Today();
                ItemTimingChanged(it); Changed();
            }
        }
        break;
    case Z_DPRIO:    if (it) TogglePrio(it->id); break;
    case Z_DSHOW:
        if (it && (v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, Z_DSHOW, 0, TRUE), kShowMenu, 5, ShowMode(it))) >= 0)
            SetItemShow(m_sel, v);
        break;
    case Z_DDELETE:
        if (it) { DWORD id = it->id; MSelect(0); DeleteItem(id); MainToast(L"Tarefa excluída", 2, id); }
        break;
    case Z_TOAST:
        if (m_toastKind == 1) Restore(m_toastId);
        else if (m_toastKind == 2) UndoDelete();
        m_toast[0] = 0;
        KillTimer(m_hwnd, TMR_TOAST);
        InvalidateRect(m_hwnd, NULL, FALSE);
        break;
    case Z_SET:      MSetting(g / 16, g % 16); break;

    /* lista ou cards */
    case Z_TBVIEW:
        if (g != g_cfg.panelMode && (g == 0 || g == 1)) {
            g_cfg.panelMode = g; SaveConfig();
            m_scroll = 0; m_hscroll = 0;
            MLayout(); BuildList(); MLayout(); InvalidateRect(m_hwnd, NULL, FALSE);
        }
        break;
    case Z_TBBY:
        if ((v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, a, 0, TRUE), kByMenu, 2, g_cfg.boardBy)) >= 0) {
            g_cfg.boardBy = v; SaveConfig(); m_scroll = 0; m_hscroll = 0;
            MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        }
        break;
    case Z_BCARD:  MSelect((DWORD)g); break;
    case Z_STLATE: MSetView(V_ALL, 0); m_fDate = FD_LATE; MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE); break;
    case Z_STATGROUP: if (GetGroup((DWORD)g)) MSetView(V_GROUP, (DWORD)g); break;
    case Z_BCHECK: MToggleDone((DWORD)g); break;
    case Z_BADD:
        if (g >= 0 && g < m_nbcol) {
            DWORD k = m_bcol[g].key;
            if (g_cfg.boardBy) QuickOpenIn(m_view == V_GROUP ? m_viewGroup : 0, k);
            else QuickOpen(k);
        }
        break;

    /* barra de ordenar/filtrar */
    case Z_TBSORT:
        if ((v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, a, 0, TRUE), kSortMenu, 4, g_cfg.sort)) >= 0) {
            g_cfg.sort = v; SaveConfig(); m_scroll = 0; MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        }
        break;
    case Z_TBSTATE: {
        HMENU mm = CreatePopupMenu();
        UINT c;
        int i;
        AppendMenuW(mm, MF_STRING | (!m_fState ? MF_CHECKED : 0), 1, L"Todos os estados");
        AppendMenuW(mm, MF_SEPARATOR, 0, NULL);
        for (i = 0; i < g_nStates; i++)
            AppendMenuW(mm, MF_STRING | (m_fState == g_states[i].id ? MF_CHECKED : 0), (UINT_PTR)(10 + i), g_states[i].name);
        c = RunMenu(mm, ZoneScreen(m_hwnd, &m_z, a, 0, TRUE), m_hwnd);
        if (c == 1) m_fState = 0;
        else if (c >= 10 && (int)c < 10 + g_nStates) m_fState = g_states[c - 10].id;
        else break;
        m_scroll = 0; MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        break;
    }
    case Z_TBPRIO:
        if ((v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, a, 0, TRUE), kPrioMenu, 3, m_fPrio)) >= 0) {
            m_fPrio = v; m_scroll = 0; MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        }
        break;
    case Z_TBDATE:
        if ((v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, a, 0, TRUE), kDateMenu, 6, m_fDate)) >= 0) {
            m_fDate = v; m_scroll = 0; MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        }
        break;
    case Z_TBCLEAR:
        m_fState = 0; m_fPrio = 0; m_fDate = 0; m_scroll = 0;
        MLayout(); BuildList(); InvalidateRect(m_hwnd, NULL, FALSE);
        break;

    /* estado de uma tarefa */
    case Z_ROWSTATE:
    case Z_DSTATE: {
        DWORD id = a == Z_DSTATE ? m_sel : (DWORD)g;
        Item *ti = GetItem(id);
        HMENU mm;
        UINT c;
        int i;
        if (!ti) break;
        mm = CreatePopupMenu();
        for (i = 0; i < g_nStates; i++)
            AppendMenuW(mm, MF_STRING | (g_states[i].id == ItemState(ti) ? MF_CHECKED : 0), (UINT_PTR)(i + 1), g_states[i].name);
        AppendMenuW(mm, MF_SEPARATOR, 0, NULL);
        AppendMenuW(mm, MF_STRING, 100, L"Gerenciar estados…");
        c = RunMenu(mm, ZoneScreen(m_hwnd, &m_z, a, a == Z_DSTATE ? 0 : g, TRUE), m_hwnd);
        if (c >= 1 && (int)c <= g_nStates) SetItemState(id, g_states[c - 1].id);
        else if (c == 100) MSetView(V_SETTINGS, 0);
        break;
    }

    /* configurações: estados */
    case Z_STCOLOR: {
        State *st = GetState((DWORD)g);
        if (st) ColorOpen(m_hwnd, st->color, L"Cor do estado", StateColorCb, (void *)(ULONG_PTR)st->id);
        break;
    }
    case Z_STRENAME: MBeginRenameState((DWORD)g); break;
    case Z_STDEF:    if (GetState((DWORD)g)) { g_cfg.defState = (DWORD)g; StatesChanged(); } break;
    case Z_STUP:     MoveState((DWORD)g, -1); StatesChanged(); break;
    case Z_STDOWN:   MoveState((DWORD)g, 1); StatesChanged(); break;
    case Z_STDEL: {
        State *st = GetState((DWORD)g);
        WCHAR t[160];
        if (!st) break;
        wsprintfW(t, L"As tarefas em \"%s\" passam para \"%s\". Nada é apagado.", st->name, StateName(g_cfg.defState));
        if (ConfirmBox(m_hwnd, L"Excluir estado?", t, L"Excluir estado") && DeleteStateData((DWORD)g)) {
            if (m_fState == (DWORD)g) m_fState = 0;
            Save();
            StatesChanged();
        }
        break;
    }
    case Z_STADD: {
        State *st = NewState(L"Novo estado", kPalette[(g_nStates + 4) % 8]);
        if (st) { DWORD sid = st->id; StatesChanged(); MBeginRenameState(sid); }
        break;
    }
    case Z_SWATCH:   if (g >= 0 && g < 8) { g_cfg.color = kTextSwatch[g]; ConfigChanged(); } break;
    }
}

/* ------------------------------------------------------------------------ */
/* API pública                                                              */
/* ------------------------------------------------------------------------ */

static void MainRefresh(void)
{
    if (!m_hwnd) return;
    if (m_view == V_STATS) StatsFree();     /* algo mudou: relê o histórico na próxima pintura */
    if (m_sel && !GetItem(m_sel)) { m_sel = 0; MLayout(); }
    if (m_view == V_GROUP && !GetGroup(m_viewGroup)) { m_view = V_ALL; MLayout(); }
    BuildList();
    ClampScroll();
    InvalidateRect(m_hwnd, NULL, FALSE);
}

static void MBrushes(void)
{
    if (m_brSurf) DeleteObject(m_brSurf);
    if (m_brField) DeleteObject(m_brField);
    m_brSurf = CreateSolidBrush(T.surf);
    m_brField = CreateSolidBrush(T.field);
}

static void MainApplyTheme(void)
{
    if (q_hwnd) InvalidateRect(q_hwnd, NULL, FALSE);
    if (a_hwnd) InvalidateRect(a_hwnd, NULL, FALSE);
    if (t_hwnd) InvalidateRect(t_hwnd, NULL, FALSE);
    if (k_hwnd) {
        if (k_brField) DeleteObject(k_brField);
        k_brField = CreateSolidBrush(T.field);
        RedrawWindow(k_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    if (!m_hwnd) return;
    MBrushes();
    UiStyleWindow(m_hwnd, FALSE);
    UiEditTheme(m_search); UiEditTheme(m_title); UiEditTheme(m_notes); UiEditTheme(m_time); UiEditTheme(m_inline);
    RedrawWindow(m_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

static void MainShowView(int view)
{
    MainOpen(0);
    if (m_hwnd) MSetView(view, 0);
}

static void MainOpen(DWORD sel)
{
    if (!m_hwnd) {
        UINT dpi = GetDpiForSystem();
        RECT wa;
        int w = MulDiv(1180, (int)dpi, 96), h = MulDiv(760, (int)dpi, 96);
        UiLoadTheme();
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (w > wa.right - wa.left) w = wa.right - wa.left;
        if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
        CreateWindowExW(0, CLS_MAIN, APP, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                        wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                        NULL, NULL, g_inst, NULL);
        if (!m_hwnd) return;
        if (g_cfg.mw > 0 && g_cfg.mh > 0) {
            WINDOWPLACEMENT wp;
            ZeroMemory(&wp, sizeof wp);
            wp.length = sizeof wp;
            wp.showCmd = g_cfg.mmax ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            SetRect(&wp.rcNormalPosition, g_cfg.mx, g_cfg.my, g_cfg.mx + g_cfg.mw, g_cfg.my + g_cfg.mh);
            SetWindowPlacement(m_hwnd, &wp);
        } else {
            ShowWindow(m_hwnd, SW_SHOWNORMAL);
        }
    } else if (IsIconic(m_hwnd)) {
        ShowWindow(m_hwnd, SW_RESTORE);
    }
    SetForegroundWindow(m_hwnd);
    if (sel) {
        Item *it = GetItem(sel);
        if (it) {
            int v = it->status != S_PENDING ? V_DONE : (!it->date || it->date <= Today()) ? V_TODAY : V_ALL;
            if (m_view != v) MSetView(v, 0);
            if (m_query[0]) SetWindowTextW(m_search, L"");
            MSelect(sel);
            BuildList();
            MEnsureVisible(sel);
        }
    }
    MainRefresh();
}

/* Abre o painel na tarefa, com o cursor no fim das notas. */
static void MainOpenNotes(DWORD id)
{
    int n;
    MainOpen(id);
    if (!m_hwnd || !m_notes || m_sel != id) return;
    SetFocus(m_notes);
    n = GetWindowTextLengthW(m_notes);
    SendMessageW(m_notes, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

/* atalhos de teclado (chamado pelo laço de mensagens) */
static BOOL MainKey(MSG *msg)
{
    BOOL ctrl;
    if (!m_hwnd || msg->message != WM_KEYDOWN) return FALSE;
    if (msg->hwnd != m_hwnd && !IsChild(m_hwnd, msg->hwnd)) return FALSE;
    ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && msg->wParam == 'N') { QuickOpen(m_view == V_GROUP ? m_viewGroup : 0); return TRUE; }
    if (ctrl && msg->wParam == 'F') { SetFocus(m_search); SendMessageW(m_search, EM_SETSEL, 0, -1); return TRUE; }
    if (msg->hwnd != m_hwnd) return FALSE;
    switch (msg->wParam) {
    case VK_UP:     MMoveSel(-1); return TRUE;
    case VK_DOWN:   MMoveSel(1); return TRUE;
    case VK_SPACE:  if (m_sel) MToggleDone(m_sel); return TRUE;
    case VK_RETURN: if (m_sel) { SetFocus(m_title); SendMessageW(m_title, EM_SETSEL, 0, -1); } return TRUE;
    case VK_DELETE:
        if (m_sel) { DWORD id = m_sel; MSelect(0); DeleteItem(id); MainToast(L"Tarefa excluída", 2, id); }
        return TRUE;
    case VK_ESCAPE: if (m_sel) MSelect(0); return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Janela                                                                   */
/* ------------------------------------------------------------------------ */

static void MHover(LPARAM l)
{
    Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
    int a = z ? z->act : 0, g = z ? z->arg : 0;
    if (!m_tracking) {
        TRACKMOUSEEVENT t;
        t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = m_hwnd; t.dwHoverTime = 0;
        TrackMouseEvent(&t);
        m_tracking = TRUE;
    }
    if (a != m_hotA || g != m_hotG) { m_hotA = a; m_hotG = g; InvalidateRect(m_hwnd, NULL, FALSE); }
}

static void MRefont(void)
{
    SendMessageW(m_search, WM_SETFONT, (WPARAM)m_f.base, FALSE);
    SendMessageW(m_title, WM_SETFONT, (WPARAM)m_f.edTitle, FALSE);
    SendMessageW(m_notes, WM_SETFONT, (WPARAM)m_f.base, FALSE);
    SendMessageW(m_time, WM_SETFONT, (WPARAM)m_f.base, FALSE);
    SendMessageW(m_inline, WM_SETFONT, (WPARAM)m_f.base, FALSE);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        m_hwnd = h;
        UiInit();
        BrandLoad();
        UiFontsMake(&m_f, GetDpiForWindow(h));
        MBrushes();
        m_search = UiEdit(h, IDC_SEARCH, WS_VISIBLE | ES_AUTOHSCROLL, m_f.base, L"Buscar tarefas  (Ctrl+F)");
        m_title  = UiEdit(h, IDC_DTITLE, ES_AUTOHSCROLL, m_f.edTitle, L"Título da tarefa");
        m_notes  = UiEdit(h, IDC_DNOTES, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, m_f.base,
                          L"Descrição, detalhes, links ou uma lista…");
        m_time   = UiEdit(h, IDC_DTIME, ES_AUTOHSCROLL, m_f.base, L"--:--");
        m_inline = UiEdit(h, IDC_INLINE, ES_AUTOHSCROLL, m_f.base, NULL);
        SendMessageW(m_title, EM_LIMITTEXT, TEXT_MAX - 1, 0);
        SendMessageW(m_notes, EM_LIMITTEXT, NOTES_MAX, 0);
        SendMessageW(m_time, EM_LIMITTEXT, 8, 0);
        SendMessageW(m_inline, EM_LIMITTEXT, NAME_MAX - 1, 0);
        SendMessageW(m_search, EM_LIMITTEXT, 100, 0);
        UiStyleWindow(h, FALSE);
        m_query[0] = 0;
        MLayout();
        BuildList();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mi = (MINMAXINFO *)l;
        UINT dpi = GetDpiForWindow(h);
        if (!dpi) dpi = 96;
        mi->ptMinTrackSize.x = MulDiv(980, (int)dpi, 96);
        mi->ptMinTrackSize.y = MulDiv(640, (int)dpi, 96);
        return 0;
    }
    case WM_SIZE:
        MLayout();
        BuildList();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&m_bb, wdc, c.right, c.bottom);
        MPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HWND e = (HWND)l;
        BOOL field = e == m_notes || e == m_time || e == m_inline;
        SetTextColor((HDC)w, T.text);
        SetBkColor((HDC)w, field ? T.field : T.surf);
        return (LRESULT)(field ? m_brField : m_brSurf);
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_SEARCH && code == EN_CHANGE && !m_loading) {
            EditText(m_search, m_query, 120);
            CleanText(m_query);
            m_scroll = 0;
            MLayout();
            BuildList();
            InvalidateRect(h, NULL, FALSE);
        } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            if (code == EN_KILLFOCUS) {
                if (id == IDC_DTITLE) MCommitTitle();
                else if (id == IDC_DNOTES) MCommitNotes();
                else if (id == IDC_DTIME) MCommitTime();
                else if (id == IDC_INLINE) MEndRename(TRUE);
            }
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_EDITKEY: {
        int id = (int)w;
        int k = (int)l;
        if (id == IDC_SEARCH) {
            if (k == VK_ESCAPE) { SetWindowTextW(m_search, L""); SetFocus(h); }
            else if (k == VK_DOWN || k == VK_RETURN) { SetFocus(h); MMoveSel(1); }
            else if (k == VK_UP) { SetFocus(h); MMoveSel(-1); }
        } else if (id == IDC_INLINE) {
            if (k == VK_RETURN) MEndRename(TRUE);
            else if (k == VK_ESCAPE) MEndRename(FALSE);
            SetFocus(h);
        } else if (id == IDC_DTITLE || id == IDC_DTIME || id == IDC_DNOTES) {
            if (k == VK_ESCAPE && id != IDC_DNOTES) MLoadDetail();
            if (k == VK_ESCAPE || k == VK_RETURN || k == 0x10D) SetFocus(h);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (m_dragId && (w & MK_LBUTTON)) {
            POINT pt;
            pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
            if (!m_dragging && (IAbs(pt.x - m_dragStart.x) > MS(6) || IAbs(pt.y - m_dragStart.y) > MS(6))) m_dragging = TRUE;
            if (m_dragging) {
                m_dragPt = pt;
                /* rola sozinho perto das bordas de cima e de baixo */
                if (pt.y > m_rList.bottom - MS(40)) m_scroll += MS(14);
                else if (pt.y < m_rList.top + MS(30)) m_scroll -= MS(14);
                ClampScroll();
                m_dropCol = BoardBoxAt(pt.x, pt.y);
                SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32646)));
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
        }
        MHover(l);
        return 0;
    case WM_CAPTURECHANGED:
        if (m_dragId && (HWND)l != h) { BoardEndDrag(); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_MOUSELEAVE: m_tracking = FALSE; if (m_hotA) { m_hotA = m_hotG = 0; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt;
            Zone *z;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            z = ZHit(&m_z, pt.x, pt.y);
            SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(z && z->act ? 32649 : 32512)));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        m_downA = z ? z->act : 0; m_downG = z ? z->arg : 0;
        if (GetFocus() != h) SetFocus(h);
        if (z && z->act == Z_BCARD) {          /* pode virar um arrastar */
            BCard *b = CardById((DWORD)z->arg);
            if (b) {
                m_dragId = (DWORD)z->arg;
                m_dragging = FALSE;
                m_dragStart.x = GET_X_LPARAM(l); m_dragStart.y = GET_Y_LPARAM(l);
                m_dragOff.x = m_dragStart.x - (m_x0 + m_bcol[b->col].x + BCARD_IN);
                m_dragOff.y = m_dragStart.y - (m_rList.top + m_bcol[b->col].y + b->y - m_scroll);
                m_dragCol = b->col;
                SetCapture(h);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z;
        if (m_dragId) {
            BOOL was = m_dragging;
            if (was) BoardDrop();
            BoardEndDrag();
            ReleaseCapture();
            if (was) { m_downA = 0; InvalidateRect(h, NULL, FALSE); return 0; }
        }
        z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act && z->act == m_downA && z->arg == m_downG) MAct(z->act, z->arg);
        m_downA = 0;
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == Z_GROUP) MBeginRename((DWORD)z->arg);
        else if (z && z->act == Z_STNAME) MBeginRenameState((DWORD)z->arg);
        else if (z && (z->act == Z_ROW || z->act == Z_BCARD)) { MSelect((DWORD)z->arg); SetFocus(m_title); SendMessageW(m_title, EM_SETSEL, 0, -1); }
        return 0;
    }
    case WM_RBUTTONUP: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        POINT pt;
        GetCursorPos(&pt);
        if (!z) return 0;
        if (z->act == Z_ROW || z->act == Z_CHECK || z->act == Z_BCARD || z->act == Z_BCHECK) { MSelect((DWORD)z->arg); TaskMenu((DWORD)z->arg, pt, h, TRUE); }
        else if ((z->act == Z_BCOLHEAD || z->act == Z_BADD) && z->arg >= 0 && z->arg < m_nbcol) {
            if (g_cfg.boardBy) StateMenu(m_bcol[z->arg].key, pt, h); else GroupMenu(m_bcol[z->arg].key, pt, h, TRUE);
        }
        else if (z->act == Z_GROUP) GroupMenu((DWORD)z->arg, pt, h, TRUE);
        else if (z->act >= Z_STCOLOR && z->act <= Z_STDEL) StateMenu((DWORD)z->arg, pt, h);
        else if (z->act == Z_HROW || z->act == Z_HCHECK) {
            HMENU mm = CreatePopupMenu();
            int idx = z->arg;
            AppendMenuW(mm, MF_STRING, 1, L"Restaurar para a lista");
            if (RunMenu(mm, pt, h) == 1) MRestoreHistory(idx);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        ScreenToClient(h, &pt);
        if (PtInRect(&m_rList, pt)) {
            if (m_inlineState) MEndRename(TRUE);
            m_scroll -= GET_WHEEL_DELTA_WPARAM(w) * MS(56) / WHEEL_DELTA;
            ClampScroll();
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TMR_TOAST) { KillTimer(h, w); m_toast[0] = 0; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)l;
        UiFontsMake(&m_f, HIWORD(w));
        MRefont();
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        MLayout();
        BuildList();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wp;
        MCommitAll();
        wp.length = sizeof wp;
        if (GetWindowPlacement(h, &wp)) {
            g_cfg.mx = wp.rcNormalPosition.left;
            g_cfg.my = wp.rcNormalPosition.top;
            g_cfg.mw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            g_cfg.mh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            g_cfg.mmax = wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0;
            SaveConfig();
        }
        m_hwnd = NULL;
        m_search = m_title = m_notes = m_time = m_inline = NULL;
        m_sel = 0; m_inlineGroup = 0; m_inlineState = 0;
        BoardEndDrag();
        Free(m_bcard); m_bcard = NULL; m_nbcard = m_capbcard = 0; m_nbcol = 0;
        StatsFree(); m_toast[0] = 0; m_hotA = m_hotG = 0; m_tracking = FALSE;
        if (m_view == V_HISTORY) m_view = V_TODAY;
        FreeItems(m_hist, m_nHist); m_hist = NULL; m_nHist = 0;
        Free(m_list); m_list = NULL; m_nList = m_capList = 0;
        UiFontsFree(&m_f); ZFree(&m_z); BBFree(&m_bb);
        BrandUnload();
        if (m_brSurf) DeleteObject(m_brSurf);
        if (m_brField) DeleteObject(m_brField);
        m_brSurf = m_brField = NULL;
        UiDone();
        ScheduleTrim();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
