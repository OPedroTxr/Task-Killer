/*
 * mainwin.c — o painel do The Method.
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
    Z_DCLOSE, Z_DCHECK, Z_DGROUP, Z_DTYPE, Z_DDATE, Z_DRECUR, Z_DPRIO, Z_DDONE, Z_DDELETE, Z_DTIMECLR, Z_DTIMEPICK,
    Z_TOAST, Z_SET, Z_SWATCH, Z_EMPTYADD
};
enum { K_LAYOUT = 1, K_LOCK, K_DATE, K_HIDEEMPTY, K_SHADOW, K_WIDTH, K_OPAC, K_FONT, K_COLOR, K_THEME,
       K_AUTOSTART, K_EXPORT, K_IMPORT, K_ARCHIVE, K_FOLDER, K_HISTORY };
#define SETARG(k, v) ((k) * 16 + (v))

#define IDC_SEARCH 201
#define IDC_DTITLE 202
#define IDC_DNOTES 203
#define IDC_DTIME  204
#define IDC_INLINE 205

enum { LE_SEC, LE_ITEM, LE_HIST };
typedef struct { BYTE kind; int idx; DWORD gid; WCHAR label[64]; int count, y, h; } LEntry;

static HWND    m_hwnd, m_search, m_title, m_notes, m_time, m_inline;
static UiFonts m_f;
static Zones   m_z;
static BackBuf m_bb;
static HBRUSH  m_brSurf, m_brField;
static int     m_view = V_TODAY, m_scroll, m_contentH, m_x0, m_x1, m_detX;
static DWORD   m_viewGroup, m_sel, m_inlineGroup, m_today;
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

static int ListKey(const Item *it)
{
    DWORD t = m_today;
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

static void SecLabel(const Item *it, int key, WCHAR *out, DWORD *gid)
{
    *gid = 0;
    out[0] = 0;
    switch (m_view) {
    case V_TODAY: lstrcpyW(out, key == 0 ? L"Atrasadas" : key == 1 ? L"Hoje" : L"Sem data"); break;
    case V_GROUP: lstrcpyW(out, key == 0 ? L"Atrasadas" : key == 1 ? L"Hoje" : key == 2 ? L"Próximas" : L"Sem data"); break;
    case V_WEEK:
        if (key == 0) lstrcpyW(out, L"Atrasadas");
        else if ((DWORD)key == m_today) lstrcpyW(out, L"Hoje");
        else if ((DWORD)key == AddDays(m_today, 1)) lstrcpyW(out, L"Amanhã");
        else { FmtDate((DWORD)key, L"dddd, dd/MM", out, 64); }
        break;
    case V_ALL: *gid = ItemGroup(it); lstrcpynW(out, GroupName(*gid), 64); break;
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
    if (m_view == V_SETTINGS || !m_hwnd) return;
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
            SecLabel(it, key, e->label, &e->gid);
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
    detail = m_sel && GetItem(m_sel) && m_view != V_SETTINGS && m_view != V_HISTORY;
    det = detail ? MS(DET_W) : 0;
    m_detX = cw - det;
    m_x0 = MS(SIDE_W) + MS(32);
    m_x1 = cw - det - MS(32);
    if (m_x1 < m_x0 + MS(200)) m_x1 = m_x0 + MS(200);

    top = ShowCards() ? MS(104) + MS(100) + MS(20) : MS(104);
    SetRect(&m_rList, m_x0, top, m_x1, ch);

    sw = MS(260); sh = MS(36);
    SetRect(&m_rSearch, m_x1 - sw, MS(28), m_x1, MS(28) + sh);

    m_loading = TRUE;
    PlaceEdit(m_search, m_view != V_SETTINGS, m_rSearch.left + MS(38), m_rSearch.top + (sh - m_f.lhBase) / 2,
              sw - MS(48), m_f.lhBase);
    if (detail) {
        int dx = m_detX + MS(24), dr = cw - MS(24);
        int fy = MS(140) + 3 * MS(46);
        int ny = MS(140) + 6 * MS(46) + MS(40), nb = ch - MS(84);
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
    URound(dc, MS(22), MS(22), MS(28), MS(28), MS(8), T.accent, 255);
    UCheck(dc, (float)MS(36), (float)MS(36), (float)MS(14), T.onAccent, 255, MS(2) + 0.2f);
    UText(dc, m_f.h2, L"The Method", MS(60), MS(20), MS(160), MS(32), T.text, UT_L);

    /* nova tarefa */
    UButton(dc, &m_f, MS(16), MS(68), side - MS(32), MS(40), L"Nova tarefa", IC_ADD, 0, MHOT(Z_NEW, 0));
    ZAdd(&m_z, MS(16), MS(68), side - MS(32), MS(40), Z_NEW, 0);

    y = MS(124);
    NavItem(dc, y, IC_DAY, L"Hoje", s.today, m_view == V_TODAY, Z_NAV, V_TODAY);          y += MS(38);
    NavItem(dc, y, IC_WEEK, L"Próximos 7 dias", s.week, m_view == V_WEEK, Z_NAV, V_WEEK); y += MS(38);
    NavItem(dc, y, IC_LIST, L"Todas", s.pending, m_view == V_ALL, Z_NAV, V_ALL);         y += MS(38);
    NavItem(dc, y, IC_CHECK, L"Concluídas", s.done, m_view == V_DONE, Z_NAV, V_DONE);    y += MS(38);
    NavItem(dc, y, IC_HISTORY, L"Histórico", 0, m_view == V_HISTORY, Z_NAV, V_HISTORY); y += MS(38);

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
    if (m_view != V_SETTINGS) {
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
    Card(dc, x, y, w, h, L"Atrasadas", s.overdue, s.overdue ? T.danger : T.text,
         s.overdue ? L"precisam de atenção" : L"nada atrasado", -1, V_WEEK);                        x += w + gap;
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
    if (e->gid) { UCircle(dc, (float)(x + MS(4)), (float)(y + e->h - MS(14)), (float)MS(4), GroupColor(e->gid), 255); x += MS(16); }
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
    if (it->recur) { UIcon(dc, m_f.iconSm, IC_REPEAT, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    if (it->type == T_ALARM) { UIcon(dc, m_f.iconSm, IC_SOUND, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }
    else if (it->type == T_REMIND) { UIcon(dc, m_f.iconSm, IC_BELL, rx - MS(8), y + h / 2, T.text3); rx -= MS(24); }

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
    if (m_query[0]) { t1 = L"Nada encontrado"; t2 = L"Tente buscar por outra palavra."; add = FALSE; }
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
    WCHAR t[96];
    IntersectClipRect(dc, m_rList.left - MS(4), m_rList.top, m_rList.right + MS(4), m_rList.bottom);

    base = SSection(dc, y, L"ÁREA DE TRABALHO", 9);
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
    y = base + 9 * SROW + MS(30);

    base = SSection(dc, y, L"PAINEL", 1);
    top = SRow(dc, base, 0, L"Tema", L"Segue o Windows ou escolha claro/escuro");
    SSeg(dc, top, thm, 3, g_cfg.theme, K_THEME);
    y = base + SROW + MS(30);

    base = SSection(dc, y, L"GERAL", 2);
    top = SRow(dc, base, 0, L"Iniciar com o Windows", L"The Method abre sozinho quando você liga o computador");
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
        UText(dc, m_f.sm, t, px + MS(36), MS(104), pr - px - MS(36), MS(20), T.text3, UT_L);
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
    w = UChip(dc, &m_f, cx, y + MS(8), MS(30), IC_REPEAT, T.accent, FALSE, kRecurName[it->recur], it->recur != 0,
              MHOT(Z_DRECUR, 0), TRUE);
    ZAdd(&m_z, cx, y + MS(8), w, MS(30), Z_DRECUR, 0);
    y += rh;

    /* Importante */
    FieldLabel(dc, y, L"Importante");
    UToggle(dc, &m_f, cx, y + MS(12), it->prio != 0, MHOT(Z_DPRIO, 0));
    ZAdd(&m_z, cx - MS(6), y + MS(6), MS(52), MS(34), Z_DPRIO, 0);
    y += rh;

    /* Notas */
    UText(dc, m_f.base, L"Notas", px, y + MS(8), MS(200), MS(24), T.text2, UT_L);
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
    else {
        if (ShowCards()) PaintCards(dc);
        PaintList(dc);
    }
    if (m_sel && GetItem(m_sel) && m_view != V_SETTINGS && m_view != V_HISTORY) PaintDetail(dc, cw, ch);
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
    if (m_view == V_HISTORY && v != V_HISTORY) { FreeItems(m_hist, m_nHist); m_hist = NULL; m_nHist = 0; }
    if (v == V_HISTORY && !m_hist) m_hist = LoadHistory(&m_nHist);
    m_view = v;
    m_viewGroup = gid;
    m_scroll = 0;
    if (v == V_SETTINGS || v == V_HISTORY) MSelect(0);
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

static void MEndRename(BOOL commit)
{
    DWORD gid = m_inlineGroup;
    WCHAR t[NAME_MAX];
    Group *g;
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
        if (it && (v = UPickMenu(m_hwnd, ZoneScreen(m_hwnd, &m_z, Z_DRECUR, 0, TRUE), kRecurName, 5, it->recur)) >= 0) {
            it = GetItem(m_sel);
            if (it) { it->recur = (BYTE)v; if (v && !it->date) it->date = Today(); ItemTimingChanged(it); Changed(); }
        }
        break;
    case Z_DPRIO:    if (it) TogglePrio(it->id); break;
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
    case Z_SWATCH:   if (g >= 0 && g < 8) { g_cfg.color = kTextSwatch[g]; ConfigChanged(); } break;
    }
}

/* ------------------------------------------------------------------------ */
/* API pública                                                              */
/* ------------------------------------------------------------------------ */

static void MainRefresh(void)
{
    if (!m_hwnd) return;
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
        UiFontsMake(&m_f, GetDpiForWindow(h));
        MBrushes();
        m_search = UiEdit(h, IDC_SEARCH, WS_VISIBLE | ES_AUTOHSCROLL, m_f.base, L"Buscar tarefas  (Ctrl+F)");
        m_title  = UiEdit(h, IDC_DTITLE, ES_AUTOHSCROLL, m_f.edTitle, L"Título da tarefa");
        m_notes  = UiEdit(h, IDC_DNOTES, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, m_f.base,
                          L"Adicione detalhes, links ou uma lista…");
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
    case WM_MOUSEMOVE: MHover(l); return 0;
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
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act && z->act == m_downA && z->arg == m_downG) MAct(z->act, z->arg);
        m_downA = 0;
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == Z_GROUP) MBeginRename((DWORD)z->arg);
        else if (z && z->act == Z_ROW) { MSelect((DWORD)z->arg); SetFocus(m_title); SendMessageW(m_title, EM_SETSEL, 0, -1); }
        return 0;
    }
    case WM_RBUTTONUP: {
        Zone *z = ZHit(&m_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        POINT pt;
        GetCursorPos(&pt);
        if (!z) return 0;
        if (z->act == Z_ROW || z->act == Z_CHECK) { MSelect((DWORD)z->arg); TaskMenu((DWORD)z->arg, pt, h, TRUE); }
        else if (z->act == Z_GROUP) GroupMenu((DWORD)z->arg, pt, h, TRUE);
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
        m_sel = 0; m_inlineGroup = 0; m_toast[0] = 0; m_hotA = m_hotG = 0; m_tracking = FALSE;
        if (m_view == V_HISTORY) m_view = V_TODAY;
        FreeItems(m_hist, m_nHist); m_hist = NULL; m_nHist = 0;
        Free(m_list); m_list = NULL; m_nList = m_capList = 0;
        UiFontsFree(&m_f); ZFree(&m_z); BBFree(&m_bb);
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
