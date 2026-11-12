/*
 * parse.c — entende frases como:
 *   "Reunião com cliente amanhã às 14h #trabalho !lembrete"
 *   "Academia toda segunda 7h !alarme"
 *   "Pagar boleto 15/11 !!"
 *   "Tomar remédio todo dia 8h30"
 * O que for reconhecido sai do título e vira data, hora, grupo, tipo etc.
 */

#define QP_MAXTOK 64

typedef struct {
    const WCHAR *s;   /* início no texto original */
    int len;
    WCHAR f[24];      /* versão normalizada (sem acento/maiúsculas/pontuação final) */
    BOOL used;
} QTok;

static BOOL Is(const QTok *t, const WCHAR *w) { return !lstrcmpW(t->f, w); }

static int Digits2(const WCHAR *s, int *v)
{
    int n = 0;
    *v = 0;
    while (n < 2 && s[n] >= L'0' && s[n] <= L'9') { *v = *v * 10 + (s[n] - L'0'); n++; }
    return n;
}

/* "14h", "14h30", "14:30", "9h", "14hs", "meio-dia" → HHMM (ou -1) */
static int TimeWord(const WCHAR *f)
{
    int h, m = 0, n, k;
    if (!lstrcmpW(f, L"meio-dia") || !lstrcmpW(f, L"meiodia")) return 1200;
    if (!lstrcmpW(f, L"meia-noite")) return 0;
    n = Digits2(f, &h);
    if (!n) return -1;
    f += n;
    if (*f == L':') {
        f++;
        if (Digits2(f, &m) != 2) return -1;
        f += 2;
        if (*f == L'h') f++;
    } else if (*f == L'h') {
        f++;
        k = Digits2(f, &m);
        if (k == 1) return -1;
        f += k;
        if (!k && *f == L'r') f++;
        if (*f == L's') f++;
        if (k && *f == L'm') { f++; if (*f == L'i') { f++; if (*f == L'n') f++; } }
    } else {
        return -1;
    }
    if (*f || h > 23 || m > 59) return -1;
    return h * 100 + m;
}

/* "15/11", "15/11/26", "15/11/2026" → AAAAMMDD (ou 0) */
static DWORD DateWord(const WCHAR *f, DWORD today)
{
    int d, m, y = (int)(today / 10000), n;
    DWORD r;
    n = Digits2(f, &d);
    if (!n || f[n] != L'/') return 0;
    f += n + 1;
    n = Digits2(f, &m);
    if (!n) return 0;
    f += n;
    if (*f == L'/') {
        int a = 0, k = 0;
        f++;
        while (k < 4 && f[k] >= L'0' && f[k] <= L'9') { a = a * 10 + (f[k] - L'0'); k++; }
        if (k == 2) y = 2000 + a; else if (k == 4) y = a; else return 0;
        f += k;
        if (*f) return 0;
        r = (DWORD)(y * 10000 + m * 100 + d);
        return ValidDate(r) ? r : 0;
    }
    if (*f) return 0;
    r = (DWORD)(y * 10000 + m * 100 + d);
    if (!ValidDate(r)) return 0;
    if (r < today) { r = (DWORD)((y + 1) * 10000 + m * 100 + d); if (!ValidDate(r)) return 0; }
    return r;
}

static int WeekdayWord(const WCHAR *f)
{
    static const WCHAR *names[7][2] = {
        { L"domingo", L"domingo" }, { L"segunda", L"segunda-feira" }, { L"terca", L"terca-feira" },
        { L"quarta", L"quarta-feira" }, { L"quinta", L"quinta-feira" }, { L"sexta", L"sexta-feira" },
        { L"sabado", L"sabado" }
    };
    int i;
    for (i = 0; i < 7; i++) if (!lstrcmpW(f, names[i][0]) || !lstrcmpW(f, names[i][1])) return i;
    return -1;
}

static DWORD NextWeekday(DWORD today, int wd)
{
    DWORD d = today;
    int guard = 0;
    while (WeekDay(d) != wd && ++guard < 8) d = AddDays(d, 1);
    return d;
}

static int Tokenize(const WCHAR *in, QTok *t)
{
    int n = 0;
    const WCHAR *p = in;
    while (*p && n < QP_MAXTOK) {
        int k, j = 0;
        while (*p == L' ') p++;
        if (!*p) break;
        t[n].s = p;
        while (*p && *p != L' ') p++;
        t[n].len = (int)(p - t[n].s);
        for (k = 0; k < t[n].len && j < 23; k++) t[n].f[j++] = Fold(t[n].s[k]);
        while (j > 1 && (t[n].f[j - 1] == L',' || t[n].f[j - 1] == L'.' || t[n].f[j - 1] == L';')) j--;
        t[n].f[j] = 0;
        t[n].used = FALSE;
        n++;
    }
    return n;
}

static void ParseQuick(const WCHAR *in, DWORD today, Parsed *r)
{
    QTok t[QP_MAXTOK];
    int n, i, k, len = 0;

    ZeroMemory(r, sizeof *r);
    r->time = r->type = r->recur = r->prio = -1;
    n = Tokenize(in, t);

    for (i = 0; i < n; i++) {
        QTok *a = &t[i], *b = i + 1 < n ? &t[i + 1] : NULL, *c = i + 2 < n ? &t[i + 2] : NULL;
        int v, wd;
        DWORD d;
        if (a->used) continue;

        if (a->s[0] == L'#' && a->len > 1) {
            int m = a->len - 1 < NAME_MAX - 1 ? a->len - 1 : NAME_MAX - 1;
            CopyMemory(r->group, a->s + 1, (SIZE_T)m * sizeof(WCHAR));
            r->group[m] = 0;
            for (m = lstrlenW(r->group); m > 0 && (r->group[m - 1] == L',' || r->group[m - 1] == L'.'); m--) r->group[m - 1] = 0;
            for (m = 0; r->group[m]; m++) if (r->group[m] == L'_') r->group[m] = L' ';
            a->used = TRUE;
            continue;
        }
        if (Is(a, L"!!"))                           { r->prio = 1; a->used = TRUE; continue; }
        if (Is(a, L"!a") || Is(a, L"!alarme"))      { r->type = T_ALARM; a->used = TRUE; continue; }
        if (Is(a, L"!l") || Is(a, L"!lembrete"))    { r->type = T_REMIND; a->used = TRUE; continue; }
        if (Is(a, L"!t") || Is(a, L"!tarefa"))      { r->type = T_TASK; a->used = TRUE; continue; }

        /* repetição */
        if ((Is(a, L"todo") || Is(a, L"todos")) && b && Is(b, L"dia") && c && Is(c, L"util")) {
            r->recur = R_WEEKDAYS; a->used = b->used = c->used = TRUE; continue;
        }
        if (Is(a, L"dias") && b && Is(b, L"uteis")) { r->recur = R_WEEKDAYS; a->used = b->used = TRUE; continue; }
        if (Is(a, L"todo") && b && Is(b, L"dia"))   { r->recur = R_DAILY; a->used = b->used = TRUE; continue; }
        if (Is(a, L"todos") && b && Is(b, L"os") && c && Is(c, L"dias")) { r->recur = R_DAILY; a->used = b->used = c->used = TRUE; continue; }
        if (Is(a, L"diariamente"))                  { r->recur = R_DAILY; a->used = TRUE; continue; }
        if (Is(a, L"toda") && b && Is(b, L"semana")) { r->recur = R_WEEKLY; a->used = b->used = TRUE; continue; }
        if (Is(a, L"semanalmente"))                 { r->recur = R_WEEKLY; a->used = TRUE; continue; }
        if (Is(a, L"todo") && b && Is(b, L"mes"))   { r->recur = R_MONTHLY; a->used = b->used = TRUE; continue; }
        if (Is(a, L"mensalmente"))                  { r->recur = R_MONTHLY; a->used = TRUE; continue; }
        if ((Is(a, L"toda") || Is(a, L"todo")) && b && (wd = WeekdayWord(b->f)) >= 0) {
            r->recur = R_WEEKLY; r->date = NextWeekday(today, wd); a->used = b->used = TRUE; continue;
        }

        /* datas */
        if (Is(a, L"hoje"))   { r->date = today; a->used = TRUE; continue; }
        if (Is(a, L"amanha")) { r->date = AddDays(today, 1); a->used = TRUE; continue; }
        if (Is(a, L"depois") && b && Is(b, L"de") && c && Is(c, L"amanha")) {
            r->date = AddDays(today, 2); a->used = b->used = c->used = TRUE; continue;
        }
        if ((wd = WeekdayWord(a->f)) >= 0) {
            r->date = NextWeekday(today, wd);
            a->used = TRUE;
            for (k = i - 1; k >= 0 && k >= i - 2 && !t[k].used; k--) {
                if (Is(&t[k], L"na") || Is(&t[k], L"no") || Is(&t[k], L"nesta") || Is(&t[k], L"neste") ||
                    Is(&t[k], L"proxima") || Is(&t[k], L"proximo") || Is(&t[k], L"essa") || Is(&t[k], L"esta"))
                    t[k].used = TRUE;
                else break;
            }
            continue;
        }
        if ((d = DateWord(a->f, today)) != 0) {
            r->date = d;
            a->used = TRUE;
            if (i > 0 && !t[i - 1].used && (Is(&t[i - 1], L"dia") || Is(&t[i - 1], L"em") || Is(&t[i - 1], L"ate")))
                t[i - 1].used = TRUE;
            continue;
        }

        /* hora */
        if ((v = TimeWord(a->f)) >= 0) {
            r->time = v;
            a->used = TRUE;
            if (i > 0 && !t[i - 1].used && (Is(&t[i - 1], L"as") || Is(&t[i - 1], L"a") || Is(&t[i - 1], L"ao")))
                t[i - 1].used = TRUE;
            continue;
        }
        if (Is(a, L"as") && b && (v = TimeWord(b->f)) >= 0) {
            r->time = v; a->used = b->used = TRUE; continue;
        }
    }

    /* o que sobrou é o título */
    for (i = 0; i < n; i++) {
        if (t[i].used) continue;
        if (len && len < TEXT_MAX - 1) r->title[len++] = L' ';
        for (k = 0; k < t[i].len && len < TEXT_MAX - 1; k++) r->title[len++] = t[i].s[k];
    }
    r->title[len] = 0;
    CleanText(r->title);
}
