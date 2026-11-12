/* popups.c — adição rápida, mini calendário e janela de alarme. */

static void PopupHover(HWND h, Zones *zs, int *hotA, int *hotG, BOOL *tracking, LPARAM l)
{
    Zone *z = ZHit(zs, GET_X_LPARAM(l), GET_Y_LPARAM(l));
    int a = z ? z->act : 0, g = z ? z->arg : 0;
    if (!*tracking) {
        TRACKMOUSEEVENT t;
        t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = h; t.dwHoverTime = 0;
        TrackMouseEvent(&t);
        *tracking = TRUE;
    }
    if (a != *hotA || g != *hotG) { *hotA = a; *hotG = g; InvalidateRect(h, NULL, FALSE); }
}

static BOOL PopupCursor(HWND h, Zones *zs)
{
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(h, &pt);
    if (ZHit(zs, pt.x, pt.y)) { SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32649))); return TRUE; }
    return FALSE;
}

static void PopupBorder(HDC dc, int W, int H)
{
    if (!g_win11) {
        RECT r;
        HBRUSH b = CreateSolidBrush(T.border);
        SetRect(&r, 0, 0, W, H);
        FrameRect(dc, &r, b);
        DeleteObject(b);
    }
}

static void WorkAreaAt(POINT pt, RECT *wa)
{
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) *wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0);
}

static POINT ZoneScreen(HWND h, Zones *zs, int act, int arg, BOOL bottom)
{
    POINT pt = { 0, 0 };
    Zone *z = ZFind(zs, act, arg);
    if (z) { pt.x = z->r.left; pt.y = bottom ? z->r.bottom + 4 : z->r.top; }
    ClientToScreen(h, &pt);
    return pt;
}

/* ======================================================================== */
/* Mini calendário                                                          */
/* ======================================================================== */

enum { CZ_PREV = 1, CZ_NEXT, CZ_DAY, CZ_TODAY, CZ_TOMORROW, CZ_CLEAR };

static HWND    c_hwnd, c_owner;
static CalCb   c_cb;
static void   *c_ctx;
static int     c_year, c_month, c_hotA, c_hotG, c_downA, c_downG;
static DWORD   c_sel;
static UiFonts c_f;
static Zones   c_z;
static BackBuf c_bb;
static BOOL    c_tracking, c_picked;
#define CSZ(x) MulDiv((x), (int)c_f.dpi, 96)

static void QCalClosed(void);

static void CalPaint(HDC dc, int W, int H)
{
    WCHAR t[64];
    static const WCHAR *wd[7] = { L"D", L"S", L"T", L"Q", L"Q", L"S", L"S" };
    DWORD first = (DWORD)(c_year * 10000 + c_month * 100 + 1), d, today = Today();
    int x0 = CSZ(14), cw = (W - CSZ(28)) / 7, y0 = CSZ(84), ch = CSZ(36), r, c, bw;
    ZClear(&c_z);
    UFill(dc, 0, 0, W, H, T.surf);

    FmtDate(first, L"MMMM 'de' yyyy", t, 64);
    Capitalize(t);
    UText(dc, c_f.h2, t, CSZ(18), CSZ(14), W - CSZ(110), CSZ(34), T.text, UT_L);
    if (c_hotA == CZ_PREV) URound(dc, W - CSZ(86), CSZ(16), CSZ(32), CSZ(30), CSZ(8), T.surf2, 255);
    if (c_hotA == CZ_NEXT) URound(dc, W - CSZ(50), CSZ(16), CSZ(32), CSZ(30), CSZ(8), T.surf2, 255);
    UIcon(dc, c_f.iconSm, IC_CHEVLEFT, W - CSZ(70), CSZ(31), T.text2);
    UIcon(dc, c_f.iconSm, IC_CHEVRIGHT, W - CSZ(34), CSZ(31), T.text2);
    ZAdd(&c_z, W - CSZ(86), CSZ(16), CSZ(32), CSZ(30), CZ_PREV, 0);
    ZAdd(&c_z, W - CSZ(50), CSZ(16), CSZ(32), CSZ(30), CZ_NEXT, 0);

    for (c = 0; c < 7; c++) UText(dc, c_f.cap, wd[c], x0 + c * cw, CSZ(58), cw, CSZ(20), T.text3, UT_C);

    d = AddDays(first, -WeekDay(first));
    for (r = 0; r < 6; r++)
        for (c = 0; c < 7; c++) {
            int cx = x0 + c * cw + cw / 2, cy = y0 + r * ch + ch / 2;
            BOOL inMonth = (int)(d / 100 % 100) == c_month, sel = d == c_sel, hot = c_hotA == CZ_DAY && c_hotG == (int)d;
            float rad = (float)CSZ(15);
            wsprintfW(t, L"%d", (int)(d % 100));
            if (sel) UCircle(dc, (float)cx, (float)cy, rad, T.accent, 255);
            else if (hot) UCircle(dc, (float)cx, (float)cy, rad, T.surf2, 255);
            if (d == today && !sel) UCircleLine(dc, (float)cx, (float)cy, rad - 0.5f, T.accent, 255, 1.4f);
            UText(dc, sel || d == today ? c_f.smB : c_f.sm, t, cx - cw / 2, cy - ch / 2, cw, ch,
                  sel ? T.onAccent : (inMonth ? T.text : T.text3), UT_C);
            ZAdd(&c_z, cx - cw / 2, cy - ch / 2, cw, ch, CZ_DAY, (int)d);
            d = AddDays(d, 1);
        }

    UFill(dc, CSZ(14), H - CSZ(52), W - CSZ(28), 1, T.border);
    bw = (W - CSZ(28)) / 3;
    UButton(dc, &c_f, CSZ(14), H - CSZ(44), bw, CSZ(32), L"Hoje", 0, 2, c_hotA == CZ_TODAY);
    UButton(dc, &c_f, CSZ(14) + bw, H - CSZ(44), bw, CSZ(32), L"Amanhã", 0, 2, c_hotA == CZ_TOMORROW);
    UButton(dc, &c_f, CSZ(14) + bw * 2, H - CSZ(44), bw, CSZ(32), L"Sem data", 0, 2, c_hotA == CZ_CLEAR);
    ZAdd(&c_z, CSZ(14), H - CSZ(44), bw, CSZ(32), CZ_TODAY, 0);
    ZAdd(&c_z, CSZ(14) + bw, H - CSZ(44), bw, CSZ(32), CZ_TOMORROW, 0);
    ZAdd(&c_z, CSZ(14) + bw * 2, H - CSZ(44), bw, CSZ(32), CZ_CLEAR, 0);
    PopupBorder(dc, W, H);
}

static void CalPick(DWORD d)
{
    CalCb cb = c_cb;
    void *ctx = c_ctx;
    c_picked = TRUE;
    c_cb = NULL;
    if (cb) cb(ctx, d);
    if (c_hwnd) DestroyWindow(c_hwnd);
}

static void CalAct(int a, int g)
{
    switch (a) {
    case CZ_PREV: if (--c_month < 1) { c_month = 12; c_year--; } InvalidateRect(c_hwnd, NULL, FALSE); break;
    case CZ_NEXT: if (++c_month > 12) { c_month = 1; c_year++; } InvalidateRect(c_hwnd, NULL, FALSE); break;
    case CZ_DAY:      CalPick((DWORD)g); break;
    case CZ_TODAY:    CalPick(Today()); break;
    case CZ_TOMORROW: CalPick(AddDays(Today(), 1)); break;
    case CZ_CLEAR:    CalPick(0); break;
    }
}

static LRESULT CALLBACK CalProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        c_hwnd = h;
        UiInit();
        UiFontsMake(&c_f, GetDpiForWindow(h));
        UiStyleWindow(h, TRUE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&c_bb, wdc, c.right, c.bottom);
        CalPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: PopupHover(h, &c_z, &c_hotA, &c_hotG, &c_tracking, l); return 0;
    case WM_MOUSELEAVE: c_tracking = FALSE; c_hotA = c_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && PopupCursor(h, &c_z)) return TRUE; break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&c_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        c_downA = z ? z->act : 0; c_downG = z ? z->arg : 0;
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&c_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == c_downA && z->arg == c_downG) CalAct(z->act, z->arg);
        return 0;
    }
    case WM_MOUSEWHEEL:
        CalAct(GET_WHEEL_DELTA_WPARAM(w) > 0 ? CZ_PREV : CZ_NEXT, 0);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
        HWND owner = c_owner;
        BOOL picked = c_picked;
        UiFontsFree(&c_f); ZFree(&c_z); BBFree(&c_bb);
        UiDone();
        c_hwnd = NULL; c_owner = NULL; c_cb = NULL;
        if (picked && owner && IsWindow(owner)) SetForegroundWindow(owner);
        QCalClosed();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void CalOpen(HWND owner, int x, int y, DWORD date, CalCb cb, void *ctx)
{
    DWORD base;
    UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    int w = MulDiv(304, (int)dpi, 96), hgt = MulDiv(352, (int)dpi, 96);
    RECT wa;
    POINT pt;
    if (c_hwnd) DestroyWindow(c_hwnd);
    c_owner = owner; c_cb = cb; c_ctx = ctx; c_sel = date; c_picked = FALSE;
    base = date ? date : Today();
    c_year = (int)(base / 10000); c_month = (int)(base / 100 % 100);
    c_hotA = c_hotG = 0;
    pt.x = x; pt.y = y;
    WorkAreaAt(pt, &wa);
    if (x + w > wa.right) x = wa.right - w;
    if (y + hgt > wa.bottom) y = y - hgt - MulDiv(40, (int)dpi, 96);
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_CAL, L"Calendário", WS_POPUP, x, y, w, hgt,
                    owner, NULL, g_inst, NULL);
    if (!c_hwnd) return;
    ShowWindow(c_hwnd, SW_SHOW);
    SetForegroundWindow(c_hwnd);
}

/* ======================================================================== */
/* Adição rápida (Ctrl+Alt+T)                                               */
/* ======================================================================== */

enum { QZ_GROUP = 1, QZ_DATE, QZ_TIME, QZ_TYPE, QZ_RECUR, QZ_PRIO, QZ_ADD, QZ_MORE };
#define IDC_QEDIT 301

static HWND    q_hwnd, q_edit;
static UiFonts q_f;
static Zones   q_z;
static BackBuf q_bb;
static Parsed  q_p;
static DWORD   q_ctxGroup;
static int     q_hotA, q_hotG, q_downA, q_downG;
static BOOL    q_tracking, q_keep;
static HBRUSH  q_brEdit;
static struct { BOOL group, date, time, type, recur, prio; DWORD gid, d; int t, ty, rc, pr; } q_ov;
#define QSZ(x) MulDiv((x), (int)q_f.dpi, 96)

typedef struct { DWORD gid; WCHAR newName[NAME_MAX]; DWORD date; int time, type, recur, prio; } QEffective;

static void QEff(QEffective *e)
{
    DWORD today = Today();
    ZeroMemory(e, sizeof *e);
    e->date  = q_ov.date ? q_ov.d : q_p.date;
    e->time  = q_ov.time ? q_ov.t : q_p.time;
    e->type  = q_ov.type ? q_ov.ty : (q_p.type >= 0 ? q_p.type : T_TASK);
    e->recur = q_ov.recur ? q_ov.rc : (q_p.recur >= 0 ? q_p.recur : R_NONE);
    e->prio  = q_ov.prio ? q_ov.pr : (q_p.prio > 0 ? 1 : 0);
    if ((e->time >= 0 || e->recur) && !e->date) e->date = today;
    if (e->type != T_TASK && e->time < 0) {   /* lembrete sem hora: próxima hora cheia */
        int hr = NowTime() / 100 + 1;
        if (!e->date) e->date = today;
        if (hr > 23) { hr = 9; if (e->date == today) e->date = AddDays(today, 1); }
        e->time = hr * 100;
    }
    if (q_ov.group) e->gid = q_ov.gid;
    else if (q_p.group[0]) {
        Group *g = FindGroupByName(q_p.group);
        if (g) e->gid = g->id; else lstrcpynW(e->newName, q_p.group, NAME_MAX);
    } else e->gid = q_ctxGroup && GetGroup(q_ctxGroup) ? q_ctxGroup : g_cfg.defGroup;
}

static void QParse(void)
{
    WCHAR buf[TEXT_MAX * 2];
    EditText(q_edit, buf, TEXT_MAX * 2);
    ParseQuick(buf, Today(), &q_p);
    if (q_hwnd) InvalidateRect(q_hwnd, NULL, FALSE);
}

static void QCommit(BOOL details)
{
    QEffective e;
    Item *it;
    DWORD id;
    QParse();
    if (!q_p.title[0]) { MessageBeep(MB_ICONWARNING); SetFocus(q_edit); return; }
    QEff(&e);
    if (!e.gid && e.newName[0]) {
        Group *g = NewGroup(e.newName, NextGroupColor());
        if (g) { e.gid = g->id; SaveGroups(); } else e.gid = g_cfg.defGroup;
    }
    it = NewItem();
    if (!it) return;
    it->id = g_nextId++;
    it->group = e.gid;
    it->date = e.date;
    it->time = e.time;
    it->type = (BYTE)e.type;
    it->recur = (BYTE)e.recur;
    it->prio = (BYTE)e.prio;
    it->created = NowStamp();
    lstrcpynW(it->text, q_p.title, TEXT_MAX);
    ItemTimingChanged(it);
    Animate(it, A_IN);
    id = it->id;
    DestroyWindow(q_hwnd);
    Changed();
    if (details) MainOpen(id);
}

static void QDateCb(void *ctx, DWORD d)
{
    (void)ctx;
    q_ov.date = TRUE;
    q_ov.d = d;
    if (!d) { q_ov.time = TRUE; q_ov.t = -1; q_ov.recur = TRUE; q_ov.rc = R_NONE; }
    if (q_hwnd) InvalidateRect(q_hwnd, NULL, FALSE);
}

static void QTimeCb(void *ctx, int t)
{
    (void)ctx;
    q_ov.time = TRUE;
    q_ov.t = t;
    if (q_hwnd) InvalidateRect(q_hwnd, NULL, FALSE);
}

static void QCalClosed(void)
{
    if (!q_hwnd || !q_keep) return;
    q_keep = FALSE;
    if (GetForegroundWindow() != q_hwnd) PostMessageW(q_hwnd, WM_CLOSE, 0, 0);
    else SetFocus(q_edit);
}

static void QAct(int a)
{
    QEffective e;
    POINT pt = ZoneScreen(q_hwnd, &q_z, a, 0, TRUE);
    int v;
    QEff(&e);
    switch (a) {
    case QZ_GROUP: {
        HMENU m = CreatePopupMenu();
        UINT c;
        int i;
        for (i = 0; i < g_nGroups; i++)
            AppendMenuW(m, MF_STRING | (g_groups[i].id == e.gid ? MF_CHECKED : 0), (UINT_PTR)(i + 1), g_groups[i].name);
        c = RunMenu(m, pt, q_hwnd);
        if (c >= 1 && (int)c <= g_nGroups) { q_ov.group = TRUE; q_ov.gid = g_groups[c - 1].id; }
        break;
    }
    case QZ_DATE:
        q_keep = TRUE;
        CalOpen(q_hwnd, pt.x, pt.y, e.date, QDateCb, NULL);
        return;
    case QZ_TIME:
        q_keep = TRUE;
        TimeOpen(q_hwnd, pt.x, pt.y, e.time, QTimeCb, NULL);
        return;
    case QZ_TYPE:
        if ((v = UPickMenu(q_hwnd, pt, kTypeName, 3, e.type)) >= 0) { q_ov.type = TRUE; q_ov.ty = v; }
        break;
    case QZ_RECUR:
        if ((v = UPickMenu(q_hwnd, pt, kRecurName, 5, e.recur)) >= 0) { q_ov.recur = TRUE; q_ov.rc = v; }
        break;
    case QZ_PRIO: q_ov.prio = TRUE; q_ov.pr = !e.prio; break;
    case QZ_ADD:  QCommit(FALSE); return;
    case QZ_MORE: QCommit(TRUE); return;
    }
    if (q_hwnd) { InvalidateRect(q_hwnd, NULL, FALSE); SetFocus(q_edit); }
}

static void QPaint(HDC dc, int W, int H)
{
    QEffective e;
    WCHAR t[64];
    int x = QSZ(20), y = QSZ(92), h = QSZ(30), gap = QSZ(8), w, bw;
    WCHAR ic;
    COLORREF gc;
    QEff(&e);
    ZClear(&q_z);
    UFill(dc, 0, 0, W, H, T.surf);

    UCircle(dc, (float)QSZ(38), (float)QSZ(42), (float)QSZ(16), T.accent, T.dark ? 44 : 30);
    UIcon(dc, q_f.icon, IC_ADD, QSZ(38), QSZ(42), T.accent);
    UFill(dc, 0, QSZ(80), W, 1, T.border);

    /* chips */
    if (e.gid) { gc = GroupColor(e.gid); lstrcpynW(t, GroupName(e.gid), 64); }
    else { gc = NextGroupColor(); wsprintfW(t, L"+ %s", e.newName); }
    w = UChip(dc, &q_f, x, y, h, 0, gc, TRUE, t, TRUE, q_hotA == QZ_GROUP, TRUE);
    ZAdd(&q_z, x, y, w, h, QZ_GROUP, 0); x += w + gap;

    if (e.date) DayLabel(e.date, Today(), t, 64); else lstrcpyW(t, L"Data");
    w = UChip(dc, &q_f, x, y, h, IC_CALENDAR, T.accent, FALSE, t, e.date != 0, q_hotA == QZ_DATE, FALSE);
    ZAdd(&q_z, x, y, w, h, QZ_DATE, 0); x += w + gap;

    if (e.time >= 0) FmtTime(e.time, t); else lstrcpyW(t, L"Hora");
    w = UChip(dc, &q_f, x, y, h, IC_CLOCK, T.accent, FALSE, t, e.time >= 0, q_hotA == QZ_TIME, FALSE);
    ZAdd(&q_z, x, y, w, h, QZ_TIME, 0); x += w + gap;

    ic = e.type == T_ALARM ? IC_SOUND : e.type == T_REMIND ? IC_BELL : IC_TASK;
    w = UChip(dc, &q_f, x, y, h, ic, T.accent, FALSE, kTypeName[e.type], e.type != T_TASK, q_hotA == QZ_TYPE, FALSE);
    ZAdd(&q_z, x, y, w, h, QZ_TYPE, 0); x += w + gap;

    w = UChip(dc, &q_f, x, y, h, IC_REPEAT, T.accent, FALSE, e.recur ? kRecurName[e.recur] : L"Repetir",
              e.recur != 0, q_hotA == QZ_RECUR, FALSE);
    ZAdd(&q_z, x, y, w, h, QZ_RECUR, 0); x += w + gap;

    w = UChip(dc, &q_f, x, y, h, e.prio ? IC_STARFILL : IC_STAR, T.warn, FALSE, NULL, e.prio != 0, q_hotA == QZ_PRIO, FALSE);
    ZAdd(&q_z, x, y, w, h, QZ_PRIO, 0);

    /* rodapé */
    UText(dc, q_f.sm, L"Ex.: Reunião amanhã 14h #trabalho !lembrete  ·  !! importante",
          QSZ(20), H - QSZ(46), W - QSZ(260), QSZ(32), T.text3, UT_L);
    bw = QSZ(112);
    UButton(dc, &q_f, W - QSZ(20) - bw, H - QSZ(46), bw, QSZ(32), L"Adicionar", 0, 0, q_hotA == QZ_ADD);
    ZAdd(&q_z, W - QSZ(20) - bw, H - QSZ(46), bw, QSZ(32), QZ_ADD, 0);
    UButton(dc, &q_f, W - QSZ(28) - bw * 2, H - QSZ(46), bw, QSZ(32), L"Detalhes", 0, 2, q_hotA == QZ_MORE);
    ZAdd(&q_z, W - QSZ(28) - bw * 2, H - QSZ(46), bw, QSZ(32), QZ_MORE, 0);
    PopupBorder(dc, W, H);
}

static void QLayout(void)
{
    RECT c;
    if (!q_hwnd || !q_edit) return;
    GetClientRect(q_hwnd, &c);
    MoveWindow(q_edit, QSZ(66), QSZ(42) - q_f.lhQuick / 2, c.right - QSZ(66) - QSZ(22), q_f.lhQuick, TRUE);
}

static LRESULT CALLBACK QuickProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        q_hwnd = h;
        UiInit();
        UiFontsMake(&q_f, GetDpiForWindow(h));
        q_brEdit = CreateSolidBrush(T.surf);
        q_edit = UiEdit(h, IDC_QEDIT, WS_VISIBLE | ES_AUTOHSCROLL, q_f.quick, L"O que você precisa fazer?");
        UiStyleWindow(h, TRUE);
        QLayout();
        return 0;
    case WM_SIZE: QLayout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&q_bb, wdc, c.right, c.bottom);
        QPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, T.text);
        SetBkColor((HDC)w, T.surf);
        return (LRESULT)q_brEdit;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_QEDIT && HIWORD(w) == EN_CHANGE) QParse();
        return 0;
    case WM_EDITKEY:
        if (l == VK_ESCAPE) DestroyWindow(h);
        else if (l == VK_RETURN) QCommit(FALSE);
        else if (l == 0x10D) QCommit(TRUE);
        return 0;
    case WM_MOUSEMOVE: PopupHover(h, &q_z, &q_hotA, &q_hotG, &q_tracking, l); return 0;
    case WM_MOUSELEAVE: q_tracking = FALSE; q_hotA = q_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && PopupCursor(h, &q_z)) return TRUE; break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&q_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        q_downA = z ? z->act : 0; q_downG = z ? z->arg : 0;
        if (!z) SetFocus(q_edit);
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&q_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == q_downA && z->arg == q_downG) QAct(z->act);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) { if (!q_keep) PostMessageW(h, WM_CLOSE, 0, 0); }
        else if (q_edit) SetFocus(q_edit);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        UiFontsFree(&q_f); ZFree(&q_z); BBFree(&q_bb);
        if (q_brEdit) DeleteObject(q_brEdit);
        q_brEdit = NULL;
        UiDone();
        q_hwnd = q_edit = NULL;
        ScheduleTrim();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void QuickOpen(DWORD gid)
{
    POINT pt;
    RECT wa;
    UINT dpi;
    int w, h, x, y;
    if (q_hwnd) { SetForegroundWindow(q_hwnd); return; }
    UiLoadTheme();
    q_ctxGroup = gid;
    ZeroMemory(&q_ov, sizeof q_ov);
    ZeroMemory(&q_p, sizeof q_p);
    q_p.time = q_p.type = q_p.recur = q_p.prio = -1;
    q_keep = FALSE;
    q_hotA = q_hotG = 0;
    GetCursorPos(&pt);
    WorkAreaAt(pt, &wa);
    dpi = GetDpiForSystem();
    w = MulDiv(680, (int)dpi, 96);
    h = MulDiv(176, (int)dpi, 96);
    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top) * 22 / 100;
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_QUICK, L"Nova tarefa", WS_POPUP, x, y, w, h,
                    NULL, NULL, g_inst, NULL);
    if (!q_hwnd) return;
    dpi = GetDpiForWindow(q_hwnd);
    if (dpi && dpi != q_f.dpi) {
        UiFontsMake(&q_f, dpi);
        SendMessageW(q_edit, WM_SETFONT, (WPARAM)q_f.quick, FALSE);
    }
    w = QSZ(680); h = QSZ(176);
    SetWindowPos(q_hwnd, NULL, wa.left + (wa.right - wa.left - w) / 2, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(q_hwnd, SW_SHOW);
    SetForegroundWindow(q_hwnd);
    SetFocus(q_edit);
}

/* ======================================================================== */
/* Alarme (não bloqueia o app)                                              */
/* ======================================================================== */

enum { AZ_STOP = 1, AZ_SNOOZE };

static HWND    a_hwnd;
static UiFonts a_f;
static Zones   a_z;
static BackBuf a_bb;
static DWORD   a_ids[4];
static WCHAR   a_texts[4][TEXT_MAX];
static int     a_n, a_time, a_hotA, a_hotG, a_downA, a_downG;
static BOOL    a_tracking;
#define ASZ(x) MulDiv((x), (int)a_f.dpi, 96)

static void AlarmSound(BOOL on)
{
    if (on) {
        WCHAR wav[MAX_PATH];
        GetWindowsDirectoryW(wav, MAX_PATH - 32);
        lstrcatW(wav, L"\\Media\\Alarm01.wav");
        if (!PlaySoundW(wav, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT))
            PlaySoundW(L"SystemExclamation", NULL, SND_ALIAS | SND_ASYNC);
    } else {
        PlaySoundW(NULL, NULL, 0);
    }
}

static void AlarmClose(BOOL snooze)
{
    DWORD ids[4];
    WCHAR texts[4][TEXT_MAX];
    int n = a_n, i;
    AlarmSound(FALSE);
    for (i = 0; i < n; i++) { ids[i] = a_ids[i]; lstrcpynW(texts[i], a_texts[i], TEXT_MAX); }
    if (a_hwnd) DestroyWindow(a_hwnd);
    if (snooze) for (i = 0; i < n; i++) Snooze(ids[i], texts[i]);
}

static void AlarmPaint(HDC dc, int W, int H)
{
    WCHAR t[64], body[4 * TEXT_MAX + 8];
    int i, bw = ASZ(110), bh = ASZ(36);
    ZClear(&a_z);
    UFill(dc, 0, 0, W, H, T.surf);
    UCircle(dc, (float)ASZ(42), (float)ASZ(46), (float)ASZ(20), T.accent, T.dark ? 44 : 30);
    UIcon(dc, a_f.icon, IC_BELL, ASZ(42), ASZ(46), T.accent);
    if (a_time >= 0) { WCHAR tt[8]; FmtTime(a_time, tt); wsprintfW(t, L"ALARME  ·  %s", tt); }
    else lstrcpyW(t, L"ALARME");
    UText(dc, a_f.cap, t, ASZ(76), ASZ(24), W - ASZ(96), ASZ(18), T.text3, UT_L);
    body[0] = 0;
    for (i = 0; i < a_n; i++) { if (i) lstrcatW(body, L"\n"); lstrcatW(body, a_texts[i]); }
    UText(dc, a_f.h2, body, ASZ(76), ASZ(44), W - ASZ(96), H - ASZ(44) - ASZ(70), T.text,
          DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
    UButton(dc, &a_f, W - ASZ(20) - bw, H - ASZ(20) - bh, bw, bh, L"Parar", 0, 0, a_hotA == AZ_STOP);
    ZAdd(&a_z, W - ASZ(20) - bw, H - ASZ(20) - bh, bw, bh, AZ_STOP, 0);
    UButton(dc, &a_f, W - ASZ(30) - bw * 2, H - ASZ(20) - bh, bw, bh, L"Adiar 5 min", 0, 1, a_hotA == AZ_SNOOZE);
    ZAdd(&a_z, W - ASZ(30) - bw * 2, H - ASZ(20) - bh, bw, bh, AZ_SNOOZE, 0);
    PopupBorder(dc, W, H);
}

static LRESULT CALLBACK AlarmProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        a_hwnd = h;
        UiInit();
        UiFontsMake(&a_f, GetDpiForWindow(h));
        UiStyleWindow(h, TRUE);
        SetTimer(h, TMR_ALARMSTOP, 180000, NULL);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&a_bb, wdc, c.right, c.bottom);
        AlarmPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (w == TMR_ALARMSTOP) { KillTimer(h, w); AlarmSound(FALSE); }
        return 0;
    case WM_MOUSEMOVE: PopupHover(h, &a_z, &a_hotA, &a_hotG, &a_tracking, l); return 0;
    case WM_MOUSELEAVE: a_tracking = FALSE; a_hotA = a_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && PopupCursor(h, &a_z)) return TRUE; break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&a_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        a_downA = z ? z->act : 0; a_downG = z ? z->arg : 0;
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&a_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == a_downA) AlarmClose(z->act == AZ_SNOOZE);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE || w == VK_RETURN) AlarmClose(FALSE);
        return 0;
    case WM_CLOSE: AlarmClose(FALSE); return 0;
    case WM_DESTROY:
        UiFontsFree(&a_f); ZFree(&a_z); BBFree(&a_bb);
        UiDone();
        a_hwnd = NULL; a_n = 0;
        ScheduleTrim();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void AlarmShow(DWORD id, const WCHAR *text, int time)
{
    POINT pt = { 0, 0 };
    RECT wa;
    UINT dpi;
    int w, h;
    if (a_hwnd) {
        if (a_n < 4) { a_ids[a_n] = id; lstrcpynW(a_texts[a_n], text, TEXT_MAX); a_n++; }
        InvalidateRect(a_hwnd, NULL, FALSE);
        return;
    }
    UiLoadTheme();
    a_n = 1;
    a_ids[0] = id;
    lstrcpynW(a_texts[0], text, TEXT_MAX);
    a_time = time;
    a_hotA = a_hotG = 0;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    pt.x = wa.right - 10; pt.y = wa.bottom - 10;
    WorkAreaAt(pt, &wa);
    dpi = GetDpiForSystem();
    w = MulDiv(400, (int)dpi, 96);
    h = MulDiv(196, (int)dpi, 96);
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_ALARM, L"Alarme", WS_POPUP,
                    wa.right - w - MulDiv(20, (int)dpi, 96), wa.bottom - h - MulDiv(20, (int)dpi, 96), w, h,
                    NULL, NULL, g_inst, NULL);
    if (!a_hwnd) { Notify(L"Alarme", text); return; }
    AlarmSound(TRUE);
    ShowWindow(a_hwnd, SW_SHOWNOACTIVATE);
}
