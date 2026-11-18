/*
 * sched.c — lembretes, alarmes e prazos.
 *
 * Um único timer acorda o app no próximo instante em que algo muda:
 * um lembrete/alarme, o fim de um prazo (conclusão automática), a troca de
 * nível de destaque ("em 3 dias" → "em 20h" → "em 40 min") ou a virada do
 * dia. Não existe verificação periódica.
 */

static BOOL g_inSched;

static BOOL IsDue(const Item *it, ULONGLONG now)
{
    return it->status == S_PENDING && it->type != T_TASK && it->date && it->time >= 0 &&
           !it->notified && Stamp(it->date, it->time) <= now;
}

static ULONGLONG AutoSince(void) { return Stamp((DWORD)g_cfg.autoSinceDate, g_cfg.autoSinceTime); }

/* A tarefa entra na conclusão automática quando o prazo dela passar? */
static BOOL AutoEligible(const Item *it)
{
    return g_cfg.autoDone && it->status == S_PENDING && it->date && !(it->flags & F_KEEP) &&
           DeadlineStamp(it) >= AutoSince();
}

static int AutoCompleteDue(void)
{
    ULONGLONG now = NowFt(), d;
    int i, n = 0, count = g_n;
    for (i = 0; i < count && i < g_n; i++) {
        Item *it = &g_items[i];
        if (AutoEligible(it) && Deadline(it, &d) && d <= now && CompleteCore(it->id, TRUE)) n++;
    }
    return n;
}

static void Snooze(DWORD id, const WCHAR *text)
{
    SYSTEMTIME s;
    ULONGLONG f = NowFt() + 5 * FT_MIN;
    Item *it = GetItem(id);
    DWORD d;
    int t;
    FromFt(f, &s);
    d = StDate(&s);
    t = StTime(&s);
    if (it && !it->recur && it->status == S_PENDING) {
        it->date = d;
        it->time = t;
        it->notified = 0;
        it->flags &= (BYTE)~F_KEEP;
    } else {
        DWORD gid = it ? it->group : g_cfg.defGroup;
        it = NewItem();
        if (!it) return;
        it->id = g_nextId++;
        it->type = T_ALARM;
        it->group = gid;
        it->date = d;
        it->time = t;
        it->created = NowStamp();
        lstrcpynW(it->text, text, TEXT_MAX);
    }
    Changed();
}

static void Reschedule(void)
{
    Buf rem = { 0 };
    int nrem = 0, i, fired = 0, autoN;
    ULONGLONG nowFt, best, ms;
    SYSTEMTIME st;

    if (!g_msg || g_inSched) return;
    KillTimer(g_msg, TMR_SCHED);
    g_inSched = TRUE;

    for (;;) {
        ULONGLONG now = NowStamp(), dueFt = 0;
        Item *it = NULL;
        BOOL late;
        for (i = 0; i < g_n; i++) if (IsDue(&g_items[i], now)) { it = &g_items[i]; break; }
        if (!it) break;
        fired++;
        ToFt(it->date, it->time, &dueFt);
        late = NowFt() > dueFt + 10 * FT_MIN;

        if (it->type == T_ALARM && !late) AlarmShow(it->id, it->text, it->time);
        else {
            if (nrem) BufCh(&rem, L'\n');
            BufPut(&rem, it->text, -1);
            if (late) BufPut(&rem, L"  (atrasado)", -1);
            nrem++;
        }
        /* AlarmShow não altera a lista, então 'it' continua válido aqui */
        if (it->recur) {
            it->date = NextAfter(it, now);
            it->notified = Stamp(it->date, it->time) <= now ? 1 : 0;
        } else {
            it->notified = 1;
        }
    }
    autoN = AutoCompleteDue();
    g_inSched = FALSE;
    if (fired || autoN) { Save(); SyncOverlays(); StartAnim(); MainRefresh(); }

    if (nrem && rem.p) Notify(nrem > 1 ? L"Lembretes" : L"Lembrete", rem.p);
    Free(rem.p);

    /* próximo instante em que algo muda */
    GetLocalTime(&st);
    nowFt = NowFt();
    ToFt(AddDays(StDate(&st), 1), 0, &best);
    best += 2 * FT_SEC;
    for (i = 0; i < g_n; i++) {
        Item *it = &g_items[i];
        ULONGLONG f;
        if (it->status != S_PENDING || !it->date) continue;
        if (it->type != T_TASK && it->time >= 0 && !it->notified && ToFt(it->date, it->time, &f) && f < best)
            best = f;
        if (AutoEligible(it) && Deadline(it, &f) && f > nowFt && f < best)
            best = f;
        if (g_cfg.emphasis) {
            WCHAR lbl[32];
            Urgency(it, nowFt, lbl, &f);
            if (f > nowFt && f < best) best = f;
        }
    }
    ms = best > nowFt ? (best - nowFt) / 10000 + 250 : 1000;
    if (ms < 500) ms = 500;
    if (ms > 0x7FFFFFF0) ms = 0x7FFFFFF0;
    SetTimer(g_msg, TMR_SCHED, (UINT)ms, NULL);
}
