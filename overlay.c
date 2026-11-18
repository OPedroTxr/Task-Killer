/*
 * overlay.c — as janelas transparentes na área de trabalho.
 *
 * Modo "uma janela": todos os grupos juntos, com o nome de cada grupo.
 * Modo "janela por grupo": cada grupo tem sua própria janela arrastável.
 *
 * Desenho: o texto é desenhado em tons de cinza numa DIB e cada canal vira
 * uma máscara de alfa com sua própria cor:
 *   verde    → texto principal (cor escolhida pelo usuário)
 *   vermelho → detalhes na cor do grupo (títulos, marcador de importante)
 *   azul     → destaque de prazo (âmbar → vermelho conforme se aproxima)
 * Depois de UpdateLayeredWindow a imagem é descartada.
 */

enum { OR_TASK, OR_HEAD, OR_EMPTY, OR_DATE, OR_HINT };

typedef struct {
    BYTE    kind, tier;
    DWORD   id, gid, sid;
    int     idx, top, h, count, notesH, xe;
    BOOL    hit;
    RowAnim ra;
} ORow;

typedef struct {
    HWND  hwnd;
    DWORD gid;                 /* 0 = janela única com todos os grupos */
    ORow *rows;
    int   nRows, cap;
    int   W, pad, lineH, gap;
    DWORD hover, down;
    BOOL  tracking, dying;
} Overlay;

static Overlay g_ov[MAX_GROUPS + 1];
static int     g_nOv;
static HFONT   g_fMain, g_fMainB, g_fHead, g_fSmall, g_fSym, g_fSymS;
static UINT    g_fontDpi;
static BOOL    g_fontDirty = TRUE;
static int    *g_vis;
static int     g_capVis;

static Overlay *FindOv(HWND h)
{
    int i;
    for (i = 0; i < g_nOv; i++) if (g_ov[i].hwnd == h) return &g_ov[i];
    return NULL;
}

static BOOL GroupOnDesktop(DWORD gid) { Group *g = GetGroup(gid); return g && !(g->flags & GF_HIDDEN); }

/* Última data mostrada na área de trabalho, conforme "Mostrar tarefas". */
static DWORD HorizonLimit(DWORD today)
{
    if (g_cfg.horizon == H_ALL) return 99991231;
    if (g_cfg.horizon == H_WEEK) return AddDays(today, 6);
    return today;
}

/* A data da tarefa entra na área de trabalho? (a escolha da tarefa vale mais que a geral) */
static BOOL DesktopDateOk(const Item *it, DWORD today, DWORD lim)
{
    if (!it->date) return TRUE;
    switch (ShowMode(it)) {
    case SH_ALWAYS: return TRUE;
    case SH_WEEK:   return it->date <= AddDays(today, 6);
    case SH_DAY:    return it->date <= today;
    }
    return it->date <= lim;
}

static BOOL IsVisibleLim(const Item *it, DWORD today, DWORD limit)
{
    if (ShowMode(it) == SH_NEVER) return FALSE;
    if (it->anim == A_OUT) return TRUE;                         /* saindo da tela */
    if (it->anim) return DesktopDateOk(it, today, limit);       /* entrando ou sendo riscada */
    if (it->status != S_PENDING) return FALSE;
    return DesktopDateOk(it, today, limit);
}

static BOOL IsVisible(const Item *it, DWORD today) { return IsVisibleLim(it, today, HorizonLimit(today)); }

/* Ordem: importantes, atrasadas, por data, por horário, por criação. */
static int CmpItems(const Item *a, const Item *b, DWORD today)
{
    DWORD da = a->date ? a->date : today, db = b->date ? b->date : today;
    int   ta = a->time < 0 ? 9999 : a->time, tb = b->time < 0 ? 9999 : b->time;
    DWORD oa = a->parent ? a->parent : a->id, ob = b->parent ? b->parent : b->id;
    if (a->prio != b->prio) return a->prio > b->prio ? -1 : 1;
    if (da != db) return da < db ? -1 : 1;
    if (ta != tb) return ta < tb ? -1 : 1;
    if (oa != ob) return oa < ob ? -1 : 1;
    return a->id < b->id ? -1 : (a->id > b->id);
}

/* Itens visíveis de um grupo, já ordenados, em g_vis. */
static int CollectGroup(DWORD gid, DWORD today)
{
    int i, j, n = 0;
    DWORD lim = HorizonLimit(today);
    if (g_capVis < g_n + 1) {
        int nc = g_n + 32;
        int *p = (int *)ReAlloc(g_vis, (SIZE_T)nc * sizeof(int));
        if (!p) return 0;
        g_vis = p; g_capVis = nc;
    }
    for (i = 0; i < g_n; i++) {
        if (ItemGroup(&g_items[i]) != gid || !IsVisibleLim(&g_items[i], today, lim)) continue;
        j = n++;
        while (j > 0 && CmpItems(&g_items[i], &g_items[g_vis[j - 1]], today) < 0) { g_vis[j] = g_vis[j - 1]; j--; }
        g_vis[j] = i;
    }
    return n;
}

static int PendingInGroup(DWORD gid, DWORD today)
{
    int i, c = 0;
    DWORD lim = HorizonLimit(today);
    for (i = 0; i < g_n; i++) {
        const Item *it = &g_items[i];
        if (it->status == S_PENDING && ItemGroup(it) == gid && ShowMode(it) != SH_NEVER && DesktopDateOk(it, today, lim)) c++;
    }
    return c;
}

static BOOL GroupHasVisible(DWORD gid, DWORD today)
{
    int i;
    DWORD lim = HorizonLimit(today);
    for (i = 0; i < g_n; i++) if (ItemGroup(&g_items[i]) == gid && IsVisibleLim(&g_items[i], today, lim)) return TRUE;
    return FALSE;
}

static BOOL HasNotes(const Item *it) { return it->notes && it->notes[0]; }

/* ------------------------------------------------------------------------ */
/* Fontes e animação                                                        */
/* ------------------------------------------------------------------------ */

static HFONT MkFont(const WCHAR *face, int size10, int weight, BOOL italic, UINT dpi)
{
    return CreateFontW(-MulDiv(size10, (int)dpi, 720), 0, 0, 0, weight, (DWORD)italic, 0, 0, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static void EnsureFonts(UINT dpi)
{
    if (!g_fontDirty && dpi == g_fontDpi) return;
    if (g_fMain)  DeleteObject(g_fMain);
    if (g_fMainB) DeleteObject(g_fMainB);
    if (g_fHead)  DeleteObject(g_fHead);
    if (g_fSmall) DeleteObject(g_fSmall);
    if (g_fSym)   DeleteObject(g_fSym);
    if (g_fSymS)  DeleteObject(g_fSymS);
    g_fMain  = MkFont(g_cfg.face, g_cfg.size10, g_cfg.weight, g_cfg.italic, dpi);
    g_fMainB = MkFont(g_cfg.face, g_cfg.size10, g_cfg.weight >= FW_BOLD ? FW_HEAVY : FW_BOLD, g_cfg.italic, dpi);
    g_fHead  = MkFont(g_cfg.face, g_cfg.size10 * 70 / 100, FW_BOLD, FALSE, dpi);
    g_fSmall = MkFont(g_cfg.face, g_cfg.size10 * 74 / 100, FW_NORMAL, FALSE, dpi);
    g_fSym   = MkFont(L"Segoe UI Symbol", g_cfg.size10 * 88 / 100, FW_NORMAL, FALSE, dpi);
    g_fSymS  = MkFont(L"Segoe UI Symbol", g_cfg.size10 * 70 / 100, FW_NORMAL, FALSE, dpi);
    g_fontDpi = dpi;
    g_fontDirty = FALSE;
}

static float C01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float Seg(DWORD el, int a, int b) { return C01(((float)(int)el - (float)a) / (float)(b - a)); }
static float EOut(float t) { float u = 1 - t; return 1 - u * u * u; }
static float EInOut(float t) { float u = -2 * t + 2; return t < 0.5f ? 4 * t * t * t : 1 - u * u * u / 2; }

#define DUR_DONE 1650
#define DUR_IN   420
#define DUR_OUT  520

static RowAnim GetAnim(const Item *it, DWORD now)
{
    RowAnim r;
    DWORD el = now - it->animStart;
    r.alpha = 1; r.strike = 0; r.check = 0; r.label = 0; r.size = 1;
    switch (it->anim) {
    case A_DONE:   /* risca → ✓ + "feita" → some → fecha o espaço */
        r.strike = EOut(Seg(el, 0, 420));
        r.check  = Seg(el, 200, 450);
        r.label  = EOut(Seg(el, 350, 700));
        r.alpha  = 1 - EInOut(Seg(el, 950, 1400));
        r.size   = 1 - EInOut(Seg(el, 1400, DUR_DONE));
        break;
    case A_IN:
        r.size  = EOut(Seg(el, 0, 220));
        r.alpha = EOut(Seg(el, 100, DUR_IN));
        break;
    case A_OUT:
        r.alpha = 1 - EOut(Seg(el, 0, 300));
        r.size  = 1 - EInOut(Seg(el, 300, DUR_OUT));
        break;
    }
    return r;
}

static DWORD AnimDur(BYTE a) { return a == A_DONE ? DUR_DONE : a == A_IN ? DUR_IN : a == A_OUT ? DUR_OUT : 0; }

static void Animate(Item *it, BYTE kind) { it->anim = kind; it->animStart = GetTickCount(); }

static int Lv(float a, int level)
{
    int v = (int)(a * (float)level + 0.5f);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}
#define PRI(a, l) RGB(0, Lv(a, l), 0)   /* canal do texto principal */
#define ACC(a, l) RGB(Lv(a, l), 0, 0)   /* canal da cor do grupo    */
#define URG(a, l) RGB(0, 0, Lv(a, l))   /* canal do destaque        */

/* Cores do destaque de prazo na área de trabalho. */
static COLORREF TierColor(int tier)
{
    switch (tier) {
    case U_SOON:   return RGB(255, 204, 112);
    case U_NEAR:   return RGB(255, 164, 64);
    case U_URGENT:
    case U_LATE:   return RGB(255, 98, 98);
    }
    return RGB(255, 255, 255);
}

/* Texto antes do título: horário e, se não for hoje, a data. Quando há
   destaque, a data vira o rótulo relativo ("em 2 dias") depois do título. */
static void RowPrefix(const Item *it, DWORD today, int tier, WCHAR *out)
{
    out[0] = 0;
    if (it->date && it->date != today && (tier == U_NONE || tier == U_LATE)) {
        if (it->date < today) FmtDate(it->date, L"dd/MM", out, 16);
        else if (it->date == AddDays(today, 1)) lstrcpyW(out, L"amanhã");
        else FmtDate(it->date, L"ddd dd/MM", out, 16);
    }
    if (it->time >= 0) {
        WCHAR t[8];
        FmtTime(it->time, t);
        if (out[0]) lstrcatW(out, L" ");
        lstrcatW(out, t);
    }
}

static void PanelPx(int x, int y, int W, int H, int r, int *pa, int *pc)
{
    int dx = -1, dy = -1;
    if (x < r) dx = r - 1 - x; else if (x >= W - r) dx = x - (W - r);
    if (y < r) dy = r - 1 - y; else if (y >= H - r) dy = y - (H - r);
    if (dx >= 0 && dy >= 0) {
        int d2 = dx * dx + dy * dy;
        if (d2 >= r * r) { *pa = 0; *pc = 0; return; }
        if (d2 >= (r - 1) * (r - 1)) { *pa = 120; *pc = 225; return; }
    } else if (x == 0 || y == 0 || x == W - 1 || y == H - 1) {
        *pa = 120; *pc = 225; return;
    }
    *pa = 100; *pc = 16;
}

static ORow *AddRow(Overlay *ov, BYTE kind)
{
    ORow *r;
    if (ov->nRows == ov->cap) {
        int nc = ov->cap ? ov->cap * 2 : 32;
        ORow *p = (ORow *)ReAlloc(ov->rows, (SIZE_T)nc * sizeof(ORow));
        if (!p) return NULL;
        ov->rows = p; ov->cap = nc;
    }
    r = &ov->rows[ov->nRows++];
    ZeroMemory(r, sizeof *r);
    r->kind = kind;
    r->ra.alpha = 1; r->ra.size = 1;
    return r;
}

/* ------------------------------------------------------------------------ */
/* Renderização                                                             */
/* ------------------------------------------------------------------------ */

static void RenderOverlay(Overlay *ov)
{
    UINT dpi;
    int W, H, pad, lineH, gap, rowH, bulletW, ind, y, k, gi, headH, nTasks = 0, sp, notesW;
    HDC sdc, dc;
    TEXTMETRICW tm, th, ts;
    HGDIOBJ oldFont, oldBmp;
    DWORD today, now;
    ULONGLONG nowFt;
    BITMAPINFO bi;
    HBITMAP bmp;
    void *bits = NULL;
    RECT rc;
    BOOL headings, showDate;

    if (!ov->hwnd) return;
    dpi = GetDpiForWindow(ov->hwnd);
    if (!dpi) dpi = 96;
    EnsureFonts(dpi);

    W   = MulDiv(g_cfg.width, (int)dpi, 96);
    pad = MulDiv(12, (int)dpi, 96);
    sp  = MulDiv(8, (int)dpi, 96);

    sdc = GetDC(NULL);
    dc  = CreateCompatibleDC(sdc);
    oldFont = SelectObject(dc, g_fMain);  GetTextMetricsW(dc, &tm);
    SelectObject(dc, g_fHead);            GetTextMetricsW(dc, &th);
    SelectObject(dc, g_fSmall);           GetTextMetricsW(dc, &ts);

    lineH   = tm.tmHeight;
    gap     = lineH * 40 / 100;
    rowH    = lineH + gap;
    headH   = th.tmHeight + gap * 55 / 100;
    bulletW = lineH * 9 / 10;
    ind     = bulletW + MulDiv(6, (int)dpi, 96);
    notesW  = W - pad - (pad + ind);
    today   = Today();
    now     = GetTickCount();
    nowFt   = NowFt();

    /* ---- layout ---- */
    ov->nRows = 0;
    showDate = g_cfg.showDate && (ov->gid == 0 || ov == &g_ov[0]);
    if (showDate) { ORow *r = AddRow(ov, OR_DATE); if (r) r->h = th.tmHeight + gap; }

    headings = ov->gid != 0;
    if (ov->gid == 0) {
        int withRows = 0;
        for (gi = 0; gi < g_nGroups; gi++)
            if (!(g_groups[gi].flags & GF_HIDDEN) && GroupHasVisible(g_groups[gi].id, today)) withRows++;
        headings = withRows > 1;
    }
    for (gi = 0; gi < g_nGroups; gi++) {
        Group *g = &g_groups[gi];
        int n;
        if (ov->gid ? g->id != ov->gid : (g->flags & GF_HIDDEN) != 0) continue;
        n = CollectGroup(g->id, today);
        if (!n && ov->gid == 0) continue;
        if (headings) {
            ORow *r = AddRow(ov, OR_HEAD);
            if (r) {
                r->gid = g->id;
                r->count = PendingInGroup(g->id, today);
                r->h = headH + (ov->nRows > 1 ? gap : 0);
            }
        }
        for (k = 0; k < n; k++) {
            Item *it = &g_items[g_vis[k]];
            ORow *r = AddRow(ov, OR_TASK);
            if (!r) break;
            r->ra   = GetAnim(it, now);
            r->id   = it->id;
            r->gid  = g->id;
            r->sid  = ItemState(it);
            r->idx  = g_vis[k];
            r->tier = g_cfg.emphasis ? (BYTE)Urgency(it, nowFt, NULL, NULL) : U_NONE;
            if (g_cfg.notesMode == N_FIXED && HasNotes(it) && notesW > 20) {
                RECT nr;
                int cap = ts.tmHeight * 3;
                SetRect(&nr, 0, 0, notesW, 0);
                SelectObject(dc, g_fSmall);
                DrawTextW(dc, it->notes, -1, &nr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
                r->notesH = (nr.bottom > cap ? cap : nr.bottom) + gap / 4;
            }
            r->h   = (int)((float)(rowH + r->notesH) * r->ra.size + 0.5f);
            r->hit = it->status == S_PENDING && (it->anim == A_NONE || it->anim == A_IN);
            nTasks++;
        }
        if (!n && ov->gid) { ORow *r = AddRow(ov, OR_EMPTY); if (r) { r->h = rowH; r->gid = g->id; } }
    }
    if (ov->gid == 0 && !nTasks) { ORow *r = AddRow(ov, OR_EMPTY); if (r) r->h = rowH; }
    if (!g_cfg.locked) { ORow *r = AddRow(ov, OR_HINT); if (r) r->h = ts.tmHeight + gap; }

    y = pad;
    for (k = 0; k < ov->nRows; k++) { ov->rows[k].top = y; y += ov->rows[k].h; }
    H = y - gap + pad;
    if (H < lineH + 2 * pad) H = lineH + 2 * pad;
    ov->W = W; ov->pad = pad; ov->lineH = lineH; ov->gap = gap;

    /* ---- desenho (máscaras) ---- */
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { SelectObject(dc, oldFont); DeleteDC(dc); ReleaseDC(NULL, sdc); return; }
    oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    for (k = 0; k < ov->nRows; k++) {
        ORow *r = &ov->rows[k];
        int top = r->top;
        if (r->kind == OR_DATE) {
            WCHAR ds[80];
            if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, NULL, L"dddd, d 'de' MMMM", ds, 80)) ds[0] = 0;
            SelectObject(dc, g_fHead);
            SetTextColor(dc, PRI(1, 165));
            SetRect(&rc, pad, top, W - pad, top + th.tmHeight);
            DrawTextW(dc, ds, -1, &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else if (r->kind == OR_HEAD) {
            WCHAR name[NAME_MAX], cnt[16];
            SIZE sz;
            int hy = top + r->h - headH;
            lstrcpynW(name, GroupName(r->gid), NAME_MAX);
            CharUpperW(name);
            SelectObject(dc, g_fHead);
            SetTextCharacterExtra(dc, MulDiv(1, (int)dpi, 96) + th.tmHeight / 14);
            SetTextColor(dc, ACC(1, 255));
            SetRect(&rc, pad, hy, W - pad, hy + th.tmHeight);
            DrawTextW(dc, name, -1, &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            GetTextExtentPoint32W(dc, name, lstrlenW(name), &sz);
            SetTextCharacterExtra(dc, 0);
            if (r->count) {
                wsprintfW(cnt, L"%d", r->count);
                SetTextColor(dc, PRI(1, 120));
                SetRect(&rc, pad + sz.cx + th.tmAveCharWidth, hy, W - pad, hy + th.tmHeight);
                DrawTextW(dc, cnt, -1, &rc, DT_SINGLELINE | DT_NOPREFIX);
            }
        } else if (r->kind == OR_EMPTY) {
            SelectObject(dc, g_fMain);
            SetTextColor(dc, PRI(1, 105));
            SetRect(&rc, pad, top, W - pad, top + lineH);
            DrawTextW(dc, L"tudo em dia", -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        } else if (r->kind == OR_HINT) {
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, PRI(1, 160));
            SetRect(&rc, pad, top, W - pad, top + ts.tmHeight);
            DrawTextW(dc, L"arraste para mover  ·  duplo clique para travar", -1, &rc,
                      DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else {
            Item *it = &g_items[r->idx];
            float a = r->ra.alpha, ck = r->ra.check;
            WCHAR pre[40], lbl[32];
            SIZE sz;
            int x, x0, xe, avail, lblW = 0, icoW = 0, stW = 0, tier = r->tier;
            const WCHAR *stName = StateName(r->sid);
            BOOL hov, strong = tier == U_URGENT || tier == U_LATE, extras = it->anim != A_DONE;
            BOOL icon = extras && g_cfg.notesMode == N_HOVER && HasNotes(it);
            HFONT tf = strong ? g_fMainB : g_fMain;
            if (a < 0.004f || r->h <= 0) continue;
            hov = it->id == ov->hover && r->hit;

            /* marcador: ○ → ✓. Cor: prazo próximo > estado > importante (cor do grupo) */
            SelectObject(dc, g_fSym);
            if (ck > 0.01f) SetTextColor(dc, PRI(a, 140 + (int)(115.0f * ck)));
            else if (tier >= U_NEAR) SetTextColor(dc, URG(a, 255));
            else if (g_cfg.stateDesk) SetTextColor(dc, ACC(a, hov ? 255 : 225));
            else if (it->prio) SetTextColor(dc, ACC(a, 255));
            else SetTextColor(dc, PRI(a, hov ? 255 : 125));
            SetRect(&rc, pad, top, pad + bulletW, top + lineH);
            DrawTextW(dc, ck > 0.01f ? L"✓" : (it->prio || strong ? L"●" : L"○"), 1, &rc,
                      DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            x = x0 = pad + ind;
            SelectObject(dc, g_fMain);
            RowPrefix(it, today, tier, pre);
            if (pre[0]) {
                GetTextExtentPoint32W(dc, pre, lstrlenW(pre), &sz);
                SetTextColor(dc, tier >= U_NEAR ? URG(a, 255) : PRI(a, 135));
                SetRect(&rc, x, top, W - pad, top + lineH);
                DrawTextW(dc, pre, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                x += sz.cx + tm.tmAveCharWidth;
            }

            /* o que vai depois do título: ícone de nota e rótulo do prazo */
            lbl[0] = 0;
            if (extras && tier != U_NONE) Urgency(it, nowFt, lbl, NULL);
            if (lbl[0]) { SelectObject(dc, g_fSmall); GetTextExtentPoint32W(dc, lbl, lstrlenW(lbl), &sz); lblW = sz.cx + sp; }
            if (icon) { SelectObject(dc, g_fSymS); GetTextExtentPoint32W(dc, L"✎", 1, &sz); icoW = sz.cx + sp; }
            if (extras && g_cfg.stateDesk == 2) {
                SelectObject(dc, g_fSmall);
                GetTextExtentPoint32W(dc, stName, lstrlenW(stName), &sz);
                stW = sz.cx + sp;
            }
            avail = W - pad - lblW - icoW - stW - x;
            if (avail < lineH * 2) { stW = 0; avail = W - pad - lblW - icoW - x; }
            if (avail < lineH * 2) { lblW = icoW = 0; lbl[0] = 0; icon = FALSE; avail = W - pad - x; }

            SelectObject(dc, tf);
            GetTextExtentPoint32W(dc, it->text, lstrlenW(it->text), &sz);
            xe = x + (sz.cx < avail ? sz.cx : (avail > 0 ? avail : 0));
            if (avail > 0) {
                SetTextColor(dc, PRI(a, 255));
                SetRect(&rc, x, top, x + avail, top + lineH);
                DrawTextW(dc, it->text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            r->xe = xe;
            if (icon) {
                SelectObject(dc, g_fSymS);
                SetTextColor(dc, PRI(a, hov ? 230 : 140));
                SetRect(&rc, xe + sp, top, xe + icoW, top + lineH);
                DrawTextW(dc, L"✎", 1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                r->xe = xe + icoW;
            }
            if (stW) {   /* nome do estado, na cor do estado */
                SelectObject(dc, g_fSmall);
                SetTextColor(dc, ACC(a, 235));
                SetRect(&rc, r->xe + sp, top, r->xe + stW, top + lineH);
                DrawTextW(dc, stName, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                r->xe += stW;
            }
            if (lbl[0]) {
                SelectObject(dc, g_fSmall);
                SetTextColor(dc, URG(a, tier == U_SOON ? 200 : 255));
                SetRect(&rc, r->xe + sp, top, W - pad, top + lineH);
                DrawTextW(dc, lbl, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                r->xe += lblW;
            }

            /* notas fixas, logo abaixo do título */
            if (r->notesH > 0 && r->h > lineH) {
                int ny = top + lineH - gap / 6, nb = top + lineH + r->notesH - gap / 4;
                if (nb > top + r->h) nb = top + r->h;
                SelectObject(dc, g_fSmall);
                SetTextColor(dc, PRI(a, 155));
                SetRect(&rc, x0, ny, x0 + notesW, nb);
                DrawTextW(dc, it->notes, -1, &rc, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);
            }

            if (r->ra.strike > 0.001f) {
                int em = tm.tmHeight - tm.tmInternalLeading;
                int thick = em / 13 < 1 ? 1 : em / 13;
                int ly = top + tm.tmAscent - em * 28 / 100 - thick / 2;
                int x1 = x0 + (int)((float)(xe - x0) * r->ra.strike + 0.5f);
                HBRUSH br = CreateSolidBrush(PRI(a, 240));
                SetRect(&rc, x0, ly, x1, ly + thick);
                FillRect(dc, &rc, br);
                DeleteObject(br);
            }
            if (r->ra.label > 0.01f) {
                int lx;
                SelectObject(dc, g_fSmall);
                GetTextExtentPoint32W(dc, L"feita", 5, &sz);
                lx = xe + MulDiv(10, (int)dpi, 96);
                if (lx + sz.cx <= W - pad) {
                    SetTextColor(dc, PRI(a * r->ra.label, 150));
                    SetRect(&rc, lx, top, W - pad, top + lineH);
                    DrawTextW(dc, L"feita", 5, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                }
            }
        }
    }
    GdiFlush();

    /* ---- composição: três máscaras → cores + sombra + alfa pré-multiplicado ---- */
    {
        int N = W * H, i, xx, yy;
        BYTE *px = (BYTE *)bits;
        BYTE *mp = (BYTE *)Alloc((SIZE_T)N), *ma = (BYTE *)Alloc((SIZE_T)N), *mu = (BYTE *)Alloc((SIZE_T)N);
        BYTE *hitY = (BYTE *)Alloc((SIZE_T)H);
        COLORREF *lc = (COLORREF *)Alloc((SIZE_T)H * sizeof(COLORREF));
        COLORREF *uc = (COLORREF *)Alloc((SIZE_T)H * sizeof(COLORREF));
        if (mp && ma && mu && hitY && lc && uc) {
            int cr = GetRValue(g_cfg.color), cg = GetGValue(g_cfg.color), cb = GetBValue(g_cfg.color);
            int lum = (cr * 299 + cg * 587 + cb * 114) / 1000;
            int sc  = lum < 110 ? 255 : 0;
            int off = g_cfg.shadow ? (int)(dpi / 96) : 0;
            int rad = MulDiv(10, (int)dpi, 96), hx0 = pad / 2, hx1 = W - pad / 2;
            if (g_cfg.shadow && off < 1) off = 1;

            for (i = 0; i < N; i++) { mu[i] = px[i * 4]; mp[i] = px[i * 4 + 1]; ma[i] = px[i * 4 + 2]; }
            for (yy = 0; yy < H; yy++) { lc[yy] = g_cfg.color; uc[yy] = TierColor(U_NEAR); }
            for (k = 0; k < ov->nRows; k++) {
                ORow *r = &ov->rows[k];
                int a0 = r->top, a1 = r->top + r->h;
                if (r->kind != OR_HEAD && r->kind != OR_TASK) continue;
                for (yy = a0 < 0 ? 0 : a0; yy < a1 && yy < H; yy++) {
                    lc[yy] = r->kind == OR_TASK && g_cfg.stateDesk ? StateColor(r->sid) : GroupColor(r->gid);
                    if (r->kind == OR_TASK) uc[yy] = TierColor(r->tier);
                }
                if (g_cfg.locked && r->hit) {
                    int hb = ov->lineH + r->notesH + ov->gap / 2;
                    a1 = a0 + (r->h < hb ? r->h : hb);
                    for (yy = a0 < 0 ? 0 : a0; yy < a1 && yy < H; yy++) hitY[yy] = 1;
                }
            }

            for (yy = 0; yy < H; yy++) {
                int ar = GetRValue(lc[yy]), ag = GetGValue(lc[yy]), ab = GetBValue(lc[yy]);
                int ur = GetRValue(uc[yy]), ug = GetGValue(uc[yy]), ub = GetBValue(uc[yy]);
                for (xx = 0; xx < W; xx++) {
                    int tp, ta, tu, w, s = 0, A = 0, R = 0, G = 0, B = 0;
                    i = yy * W + xx;
                    tp = mp[i]; ta = ma[i]; tu = mu[i];
                    w = tp + ta + tu;
                    if (w) {
                        A = 255 - (255 - tp) * (255 - ta) / 255 * (255 - tu) / 255;
                        R = (cr * tp + ar * ta + ur * tu) * A / (w * 255);
                        G = (cg * tp + ag * ta + ug * tu) * A / (w * 255);
                        B = (cb * tp + ab * ta + ub * tu) * A / (w * 255);
                    }
                    if (off) {
                        int m1 = 0, m2 = 0, j;
                        if (xx >= off && yy >= off) {
                            j = i - off * W - off;
                            m1 = mp[j]; if (ma[j] > m1) m1 = ma[j]; if (mu[j] > m1) m1 = mu[j];
                        }
                        if (xx >= 2 * off && yy >= 2 * off) {
                            j = i - 2 * off * W - 2 * off;
                            m2 = mp[j]; if (ma[j] > m2) m2 = ma[j]; if (mu[j] > m2) m2 = mu[j];
                            m2 = m2 * 3 / 5;
                        }
                        s = (m1 > m2 ? m1 : m2) * 170 / 255;
                    }
                    if (s) {
                        int add = s * (255 - A) / 255;
                        R += sc * add / 255; G += sc * add / 255; B += sc * add / 255;
                        A += add;
                    }
                    if (!g_cfg.locked) {
                        int pa, pc;
                        PanelPx(xx, yy, W, H, rad, &pa, &pc);
                        if (pa) {
                            int f = pa * (255 - A) / 255;
                            R += pc * f / 255; G += pc * f / 255; B += pc * f / 255;
                            A += f;
                        }
                    } else if (A == 0 && hitY[yy] && xx >= hx0 && xx < hx1) {
                        A = 1;
                    }
                    if (A > 255) A = 255;
                    if (R > A) R = A;
                    if (G > A) G = A;
                    if (B > A) B = A;
                    px[i * 4]     = (BYTE)B;
                    px[i * 4 + 1] = (BYTE)G;
                    px[i * 4 + 2] = (BYTE)R;
                    px[i * 4 + 3] = (BYTE)A;
                }
            }
        }
        Free(mp); Free(ma); Free(mu); Free(hitY); Free(lc); Free(uc);
    }

    {
        SIZE size;
        POINT src = { 0, 0 };
        BLENDFUNCTION bf;
        size.cx = W; size.cy = H;
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = (BYTE)g_cfg.opacity;
        bf.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(ov->hwnd, sdc, NULL, &size, dc, &src, 0, &bf, ULW_ALPHA);
    }

    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, sdc);
}

static void RenderAll(void)
{
    int i;
    for (i = 0; i < g_nOv; i++) RenderOverlay(&g_ov[i]);
    if (!g_animOn) ScheduleTrim();
}

static ORow *RowObjAt(Overlay *ov, int x, int y)
{
    int k;
    if (x < ov->pad / 2 || x >= ov->W - ov->pad / 2) return NULL;
    for (k = 0; k < ov->nRows; k++) {
        ORow *r = &ov->rows[k];
        int hb = ov->lineH + r->notesH + ov->gap / 2;
        if (r->kind == OR_TASK && r->hit && y >= r->top && y < r->top + (r->h < hb ? r->h : hb)) return r;
    }
    return NULL;
}

static DWORD RowAt(Overlay *ov, int x, int y) { ORow *r = RowObjAt(ov, x, y); return r ? r->id : 0; }

/* ------------------------------------------------------------------------ */
/* Cartão de notas (aparece ao passar o mouse)                              */
/* ------------------------------------------------------------------------ */

static HWND    g_tip, g_tipOv;
static DWORD   g_tipId, g_tipPend;
static UiFonts g_tipF;
static int     g_tipTitleH, g_tipNotesH;
#define TPSZ(x) MulDiv((x), (int)g_tipF.dpi, 96)
#define TIP_W 320

static void TipHide(void)
{
    if (g_msg) KillTimer(g_msg, TMR_TIP);
    g_tipPend = 0;
    if (g_tip) DestroyWindow(g_tip);
}

/* Chamado quando o mouse passa para outra tarefa (ou sai). */
static void TipRequest(Overlay *ov, DWORD id)
{
    Item *it;
    if (g_tip && id == g_tipId) return;
    TipHide();
    if (!id || g_cfg.notesMode != N_HOVER || !(it = GetItem(id)) || !HasNotes(it)) return;
    g_tipPend = id;
    g_tipOv = ov->hwnd;
    SetTimer(g_msg, TMR_TIP, 380, NULL);
}

static void TipMeasure(const Item *it, int W)
{
    RECT r;
    HDC dc = GetDC(NULL);
    HGDIOBJ o = SelectObject(dc, g_tipF.bold);
    int inner = W - TPSZ(32);
    SetRect(&r, 0, 0, inner, 0);
    DrawTextW(dc, it->text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    g_tipTitleH = r.bottom > g_tipF.lhBase * 2 + 2 ? g_tipF.lhBase * 2 + 2 : r.bottom;
    SelectObject(dc, g_tipF.base);
    SetRect(&r, 0, 0, inner, 0);
    DrawTextW(dc, it->notes, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    g_tipNotesH = r.bottom > g_tipF.lhBase * 14 ? g_tipF.lhBase * 14 : r.bottom;
    SelectObject(dc, o);
    ReleaseDC(NULL, dc);
}

static void TipPaint(HDC dc, int W, int H)
{
    Item *it = GetItem(g_tipId);
    WCHAR meta[160], lbl[32], when[48];
    int x = TPSZ(16), y = TPSZ(14), tier;
    COLORREF tc;
    UFill(dc, 0, 0, W, H, T.surf);
    if (!it) return;

    /* grupo · quando · rótulo do prazo */
    UCircle(dc, (float)(x + TPSZ(4)), (float)(y + TPSZ(9)), (float)TPSZ(4), GroupColor(ItemGroup(it)), 255);
    lstrcpynW(meta, GroupName(ItemGroup(it)), 48);
    lstrcatW(meta, L"  ·  ");
    lstrcatW(meta, StateName(ItemState(it)));
    WhenLabel(it->date, it->time, Today(), when, 48);
    if (when[0]) { lstrcatW(meta, L"  ·  "); lstrcatW(meta, when); }
    UText(dc, g_tipF.sm, meta, x + TPSZ(14), y, W - x - TPSZ(110), TPSZ(18), T.text2, UT_L);
    tier = Urgency(it, NowFt(), lbl, NULL);
    if (lbl[0] && tier != U_NONE) {
        tc = tier >= U_URGENT ? T.danger : T.warn;
        UText(dc, g_tipF.smB, lbl, W - TPSZ(110), y, TPSZ(94), TPSZ(18), tc, UT_R);
    }
    y += TPSZ(26);

    UText(dc, g_tipF.bold, it->text, x, y, W - TPSZ(32), g_tipTitleH, T.text,
          DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
    y += g_tipTitleH + TPSZ(10);
    UFill(dc, x, y, W - TPSZ(32), 1, T.border);
    y += TPSZ(10);
    UText(dc, g_tipF.base, it->notes, x, y, W - TPSZ(32), g_tipNotesH, T.text2,
          DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
    if (!g_win11) {
        RECT r;
        HBRUSH b = CreateSolidBrush(T.border);
        SetRect(&r, 0, 0, W, H);
        FrameRect(dc, &r, b);
        DeleteObject(b);
    }
}

static LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NCHITTEST: return HTTRANSPARENT;   /* o mouse "atravessa" o cartão */
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC dc = BeginPaint(h, &ps);
        GetClientRect(h, &c);
        TipPaint(dc, c.right, c.bottom);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        UiFontsFree(&g_tipF);
        UiDone();
        g_tip = NULL;
        g_tipId = 0;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void TipShow(Overlay *ov, DWORD id)
{
    Item *it = GetItem(id);
    ORow *row = NULL;
    RECT wr, wa;
    POINT pt;
    MONITORINFO mi;
    UINT dpi;
    int k, W, H, x, y;
    if (!it || !HasNotes(it) || !ov->hwnd) return;
    for (k = 0; k < ov->nRows; k++) if (ov->rows[k].kind == OR_TASK && ov->rows[k].id == id) row = &ov->rows[k];
    if (!row) return;

    UiLoadTheme();
    UiInit();
    dpi = GetDpiForWindow(ov->hwnd);
    UiFontsMake(&g_tipF, dpi ? dpi : 96);
    W = TPSZ(TIP_W);
    TipMeasure(it, W);
    H = TPSZ(14) + TPSZ(26) + g_tipTitleH + TPSZ(21) + g_tipNotesH + TPSZ(16);

    GetWindowRect(ov->hwnd, &wr);
    pt.x = wr.left + row->xe; pt.y = wr.top + row->top;
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    /* à direita do texto; se não couber, logo abaixo da linha */
    x = pt.x + TPSZ(14);
    y = pt.y - TPSZ(6);
    if (x + W > wa.right) { x = wr.left + ov->pad + ov->lineH; y = pt.y + ov->lineH + TPSZ(4); }
    if (x + W > wa.right) x = wa.right - W - TPSZ(8);
    if (x < wa.left) x = wa.left + TPSZ(8);
    if (y + H > wa.bottom) y = wa.bottom - H - TPSZ(8);
    if (y < wa.top) y = wa.top;

    g_tipId = id;
    g_tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, CLS_TIP, L"Notas", WS_POPUP,
                            x, y, W, H, NULL, NULL, g_inst, NULL);
    if (!g_tip) { UiFontsFree(&g_tipF); UiDone(); g_tipId = 0; return; }
    UiStyleWindow(g_tip, TRUE);
    ShowWindow(g_tip, SW_SHOWNOACTIVATE);
}

static void TipFire(void)
{
    Overlay *ov;
    KillTimer(g_msg, TMR_TIP);
    ov = FindOv(g_tipOv);
    if (ov && g_tipPend && ov->hover == g_tipPend) TipShow(ov, g_tipPend);
    g_tipPend = 0;
}

/* ------------------------------------------------------------------------ */
/* Animação (timer na janela de mensagens)                                  */
/* ------------------------------------------------------------------------ */

static void StartAnim(void)
{
    int i;
    BOOL any = FALSE;
    for (i = 0; i < g_n; i++) if (g_items[i].anim) { any = TRUE; break; }
    if (any && !g_animOn && g_msg) { SetTimer(g_msg, TMR_ANIM, 15, NULL); g_animOn = TRUE; }
    RenderAll();
}

static void AnimTick(void)
{
    DWORD now = GetTickCount();
    BOOL any = FALSE, archive = FALSE, ended = FALSE;
    int i;
    for (i = 0; i < g_n; i++) {
        Item *it = &g_items[i];
        if (!it->anim) continue;
        if (now - it->animStart >= AnimDur(it->anim)) {
            it->anim = A_NONE;
            ended = TRUE;
            if (it->status == S_DELETED) archive = TRUE;
        } else {
            any = TRUE;
        }
    }
    if (archive) ArchiveOld(FALSE);
    if (!any) {
        KillTimer(g_msg, TMR_ANIM);
        g_animOn = FALSE;
    }
    if (ended) SyncOverlays();
    RenderAll();
    if (archive) MainRefresh();
}

/* ------------------------------------------------------------------------ */
/* Criação / sincronização das janelas                                      */
/* ------------------------------------------------------------------------ */

static void ClampWnd(HWND h, int *ox, int *oy)
{
    RECT r, wa;
    HMONITOR mon;
    MONITORINFO mi;
    int x, y, w;
    GetWindowRect(h, &r);
    mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return;
    wa = mi.rcWork;
    x = r.left; y = r.top; w = r.right - r.left;
    if (x + w > wa.right) x = wa.right - w;
    if (x < wa.left) x = wa.left;
    if (y > wa.bottom - 60) y = wa.bottom - 60;
    if (y < wa.top) y = wa.top;
    if (x != r.left || y != r.top) SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    *ox = x; *oy = y;
}

static void SaveOvPos(Overlay *ov)
{
    RECT r;
    GetWindowRect(ov->hwnd, &r);
    if (ov->gid == 0) { g_cfg.x = r.left; g_cfg.y = r.top; SaveConfig(); }
    else {
        Group *g = GetGroup(ov->gid);
        if (g) { g->x = r.left; g->y = r.top; SaveGroups(); }
    }
}

static void CreateOv(DWORD gid, int stackIndex)
{
    Overlay *ov;
    int x = g_cfg.x, y = g_cfg.y, cx, cy;
    Group *g = gid ? GetGroup(gid) : NULL;
    BOOL placed = FALSE;
    if (g_nOv >= MAX_GROUPS + 1) return;
    if (g && g->x != POS_UNSET) { x = g->x; y = g->y; }
    else if (g) { y = g_cfg.y + stackIndex * MulDiv(190, (int)GetDpiForSystem(), 96); placed = TRUE; }

    ov = &g_ov[g_nOv++];
    ZeroMemory(ov, sizeof *ov);
    ov->gid = gid;
    ov->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLS_OVERLAY, APP, WS_POPUP,
                               x, y, 10, 10, g_progman, NULL, g_inst, NULL);
    if (!ov->hwnd) { g_nOv--; return; }
    RenderOverlay(ov);
    ShowWindow(ov->hwnd, SW_SHOWNOACTIVATE);
    if (g_progman) SetWindowPos(ov->hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    cx = x; cy = y;
    ClampWnd(ov->hwnd, &cx, &cy);
    if (placed || cx != x || cy != y) SaveOvPos(ov);
}

static void DestroyOvAt(int i)
{
    Overlay *ov = &g_ov[i];
    if (ov->hwnd == g_tipOv) TipHide();
    ov->dying = TRUE;
    if (ov->hwnd) DestroyWindow(ov->hwnd);
    Free(ov->rows);
    MoveMemory(&g_ov[i], &g_ov[i + 1], (SIZE_T)(g_nOv - i - 1) * sizeof(Overlay));
    g_nOv--;
}

/* Cria/destrói janelas para refletir o modo e os grupos atuais. */
static void SyncOverlays(void)
{
    DWORD want[MAX_GROUPS + 1], today = Today();
    int nw = 0, i, j;
    if (g_quitting || !g_msg) return;
    TipHide();
    g_progman = FindWindowW(L"Progman", NULL);

    if (g_cfg.layout == 0) want[nw++] = 0;
    else
        for (i = 0; i < g_nGroups; i++) {
            Group *g = &g_groups[i];
            if (g->flags & GF_HIDDEN) continue;
            if (g_cfg.hideEmpty && g_cfg.locked && !GroupHasVisible(g->id, today)) continue;
            want[nw++] = g->id;
        }

    for (i = g_nOv - 1; i >= 0; i--) {
        BOOL keep = FALSE;
        for (j = 0; j < nw; j++) if (want[j] == g_ov[i].gid) keep = TRUE;
        if (!keep || !g_ov[i].hwnd || !IsWindow(g_ov[i].hwnd)) DestroyOvAt(i);
    }
    for (j = 0; j < nw; j++) {
        BOOL have = FALSE;
        for (i = 0; i < g_nOv; i++) if (g_ov[i].gid == want[j]) have = TRUE;
        if (!have) CreateOv(want[j], j);
    }
    /* mantém a ordem dos grupos (a primeira janela mostra a data) */
    for (i = 1; i < g_nOv; i++) {
        Overlay t = g_ov[i];
        int ti = FindGroup(t.gid);
        for (j = i; j > 0 && FindGroup(g_ov[j - 1].gid) > ti; j--) g_ov[j] = g_ov[j - 1];
        g_ov[j] = t;
    }
    for (i = 0; i < g_nOv; i++) {
        SetWindowLongPtrW(g_ov[i].hwnd, GWLP_HWNDPARENT, (LONG_PTR)g_progman);
        if (g_progman) SetWindowPos(g_ov[i].hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void ClampAll(void)
{
    int i, x, y;
    for (i = 0; i < g_nOv; i++) {
        RECT r;
        GetWindowRect(g_ov[i].hwnd, &r);
        ClampWnd(g_ov[i].hwnd, &x, &y);
        if (x != r.left || y != r.top) SaveOvPos(&g_ov[i]);
    }
}

static void DestroyAllOverlays(void)
{
    while (g_nOv > 0) DestroyOvAt(g_nOv - 1);
}

/* ------------------------------------------------------------------------ */
/* Janela                                                                   */
/* ------------------------------------------------------------------------ */

static void MainMenuAt(POINT pt);

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    Overlay *ov = FindOv(h);
    if (!ov) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r == HTCLIENT && !g_cfg.locked) return HTCAPTION;
        return r;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_WINDOWPOSCHANGING:
        if (g_progman) {
            WINDOWPOS *p = (WINDOWPOS *)l;
            if (!(p->flags & SWP_NOZORDER)) p->hwndInsertAfter = HWND_BOTTOM;
        }
        break;

    case WM_SETCURSOR: {
        int ht = LOWORD(l);
        if (ht == HTCAPTION) { SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32646))); return TRUE; }
        if (ht == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(RowAt(ov, pt.x, pt.y) ? 32649 : 32512)));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        DWORD id;
        if (!ov->tracking) {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = h; t.dwHoverTime = 0;
            TrackMouseEvent(&t);
            ov->tracking = TRUE;
        }
        id = RowAt(ov, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (id != ov->hover) { ov->hover = id; TipRequest(ov, id); if (!g_animOn) RenderOverlay(ov); }
        return 0;
    }
    case WM_MOUSELEAVE:
        ov->tracking = FALSE;
        if (g_tipOv == h) TipHide();
        if (ov->hover) { ov->hover = 0; if (!g_animOn) RenderOverlay(ov); }
        return 0;

    case WM_LBUTTONDOWN:
        ov->down = RowAt(ov, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_LBUTTONUP: {
        DWORD id = RowAt(ov, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (id && id == ov->down) { ov->hover = 0; TipHide(); Complete(id); }
        if ((ov = FindOv(h)) != NULL) ov->down = 0;
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt;
        DWORD id = RowAt(ov, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        GetCursorPos(&pt);
        TipHide();
        if (id) TaskMenu(id, pt, g_msg, FALSE); else MainMenuAt(pt);
        return 0;
    }
    case WM_NCRBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        MainMenuAt(pt);
        return 0;
    }
    case WM_NCLBUTTONDBLCLK:
        g_cfg.locked = TRUE;
        ConfigChanged();
        return 0;

    case WM_EXITSIZEMOVE:
        SaveOvPos(ov);
        return 0;

    case WM_DPICHANGED: {
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        g_fontDirty = TRUE;
        RenderOverlay(ov);
        return 0;
    }
    case WM_DESTROY:
        if (!ov->dying) {
            ov->hwnd = NULL;
            if (!g_quitting && g_msg) PostMessageW(g_msg, WM_RECREATE, 0, 0);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
