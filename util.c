/* util.c — memória, texto e datas. */

static int IAbs(int v) { return v < 0 ? -v : v; }

static void *Alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
static void *ReAlloc(void *p, SIZE_T n) { return p ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, n) : Alloc(n); }
static void  Free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static WCHAR *StrDupW(const WCHAR *s)
{
    int n;
    WCHAR *d;
    if (!s || !*s) return NULL;
    n = lstrlenW(s);
    d = (WCHAR *)Alloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    if (d) CopyMemory(d, s, ((SIZE_T)n + 1) * sizeof(WCHAR));
    return d;
}

static void BufPut(Buf *b, const WCHAR *s, int n)
{
    if (n < 0) n = lstrlenW(s);
    if (b->len + n + 1 > b->cap) {
        int nc = b->cap ? b->cap : 1024;
        WCHAR *q;
        while (b->len + n + 1 > nc) nc *= 2;
        q = (WCHAR *)ReAlloc(b->p, (SIZE_T)nc * sizeof(WCHAR));
        if (!q) return;
        b->p = q; b->cap = nc;
    }
    CopyMemory(b->p + b->len, s, (SIZE_T)n * sizeof(WCHAR));
    b->len += n;
    b->p[b->len] = 0;
}
static void BufCh(Buf *b, WCHAR c) { BufPut(b, &c, 1); }
static void BufNum(Buf *b, LONGLONG v)
{
    WCHAR t[24];
    int i = 23;
    BOOL neg = v < 0;
    ULONGLONG u = neg ? (ULONGLONG)(-v) : (ULONGLONG)v;
    t[i] = 0;
    do { t[--i] = (WCHAR)(L'0' + (int)(u % 10)); u /= 10; } while (u);
    if (neg) t[--i] = L'-';
    BufPut(b, t + i, 23 - i);
}

/* Lê um inteiro (com sinal) e avança até depois do próximo TAB. */
static LONGLONG ParseNum(const WCHAR **pp)
{
    const WCHAR *p = *pp;
    BOOL neg = FALSE;
    LONGLONG v = 0;
    while (*p == L' ') p++;
    if (*p == L'-') { neg = TRUE; p++; }
    while (*p >= L'0' && *p <= L'9') { v = v * 10 + (*p - L'0'); p++; }
    while (*p && *p != L'\t' && *p != L'\n' && *p != L'\r') p++;
    if (*p == L'\t') p++;
    *pp = p;
    return neg ? -v : v;
}

static BOOL Contains(const WCHAR *s, const WCHAR *sub)
{
    int n = lstrlenW(sub);
    for (; *s; s++) {
        int i = 0;
        while (i < n && s[i] && s[i] == sub[i]) i++;
        if (i == n) return TRUE;
    }
    return FALSE;
}

/* Remove espaços das pontas e troca TAB/quebras de linha por espaço. */
static void CleanText(WCHAR *s)
{
    int i, n, a = 0;
    for (i = 0; s[i]; i++) if (s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n') s[i] = L' ';
    n = lstrlenW(s);
    while (a < n && s[a] == L' ') a++;
    while (n > a && s[n - 1] == L' ') n--;
    if (a) MoveMemory(s, s + a, (SIZE_T)(n - a) * sizeof(WCHAR));
    s[n - a] = 0;
}

/* Comparação sem acento e sem diferenciar maiúsculas (para busca e grupos). */
static WCHAR Fold(WCHAR c)
{
    static const char hi[] = "aaaaaaaceeeeiiiidnoooooxouuuuyts" "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (c >= L'A' && c <= L'Z') return (WCHAR)(c + 32);
    if (c >= 0xC0 && c <= 0xFF) return (WCHAR)(BYTE)hi[c - 0xC0];
    return c;
}

static BOOL FoldEqual(const WCHAR *a, const WCHAR *b)
{
    while (*a && *b && Fold(*a) == Fold(*b)) { a++; b++; }
    return !*a && !*b;
}

static BOOL FoldContains(const WCHAR *h, const WCHAR *n)
{
    int ln = lstrlenW(n), i;
    if (!ln) return TRUE;
    if (!h) return FALSE;
    for (; *h; h++) {
        for (i = 0; i < ln && h[i] && Fold(h[i]) == Fold(n[i]); i++) {}
        if (i == ln) return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Datas: AAAAMMDD / HHMM / AAAAMMDDHHMM (sempre hora local)                */
/* ------------------------------------------------------------------------ */

static DWORD     StDate(const SYSTEMTIME *s) { return (DWORD)s->wYear * 10000u + (DWORD)s->wMonth * 100u + s->wDay; }
static int       StTime(const SYSTEMTIME *s) { return s->wHour * 100 + s->wMinute; }
static ULONGLONG Stamp(DWORD d, int t)       { return (ULONGLONG)d * 10000u + (ULONGLONG)(t < 0 ? 0 : t); }
static DWORD     Today(void)                 { SYSTEMTIME s; GetLocalTime(&s); return StDate(&s); }
static ULONGLONG NowStamp(void)              { SYSTEMTIME s; GetLocalTime(&s); return Stamp(StDate(&s), StTime(&s)); }
static int       NowTime(void)               { SYSTEMTIME s; GetLocalTime(&s); return StTime(&s); }

static void DateToSt(DWORD d, int t, SYSTEMTIME *s)
{
    ZeroMemory(s, sizeof *s);
    s->wYear  = (WORD)(d / 10000);
    s->wMonth = (WORD)(d / 100 % 100);
    s->wDay   = (WORD)(d % 100);
    if (t >= 0) { s->wHour = (WORD)(t / 100); s->wMinute = (WORD)(t % 100); }
}

static BOOL ToFt(DWORD d, int t, ULONGLONG *out)
{
    SYSTEMTIME s;
    FILETIME f;
    DateToSt(d, t, &s);
    if (!SystemTimeToFileTime(&s, &f)) return FALSE;
    *out = ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
    return TRUE;
}

static void FromFt(ULONGLONG v, SYSTEMTIME *s)
{
    FILETIME f;
    f.dwLowDateTime  = (DWORD)v;
    f.dwHighDateTime = (DWORD)(v >> 32);
    FileTimeToSystemTime(&f, s);
}

static ULONGLONG NowFt(void)
{
    SYSTEMTIME s;
    FILETIME f;
    GetLocalTime(&s);
    SystemTimeToFileTime(&s, &f);
    return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

static BOOL ValidDate(DWORD d) { ULONGLONG v; return d && ToFt(d, -1, &v); }

static DWORD AddDays(DWORD d, int n)
{
    ULONGLONG v;
    SYSTEMTIME s;
    if (!ToFt(d, -1, &v)) return d;
    v += (ULONGLONG)((LONGLONG)n * (LONGLONG)FT_DAY);
    FromFt(v, &s);
    return StDate(&s);
}

static int WeekDay(DWORD d)
{
    ULONGLONG v;
    SYSTEMTIME s;
    if (!ToFt(d, -1, &v)) return 1;
    FromFt(v, &s);
    return s.wDayOfWeek; /* 0 = domingo */
}

static DWORD AddMonth(DWORD d)
{
    int y = (int)(d / 10000), m = (int)(d / 100 % 100) + 1, day = (int)(d % 100);
    ULONGLONG v;
    if (m > 12) { m = 1; y++; }
    for (; day > 28; day--)
        if (ToFt((DWORD)(y * 10000 + m * 100 + day), -1, &v)) break;
    return (DWORD)(y * 10000 + m * 100 + day);
}

static DWORD StepDate(DWORD d, int recur)
{
    int wd;
    switch (recur) {
    case R_DAILY:    return AddDays(d, 1);
    case R_WEEKLY:   return AddDays(d, 7);
    case R_MONTHLY:  return AddMonth(d);
    case R_WEEKDAYS:
        do { d = AddDays(d, 1); wd = WeekDay(d); } while (wd == 0 || wd == 6);
        return d;
    }
    return d;
}

static DWORD StepDateM(DWORD d, int recur, int mask)
{
    int wd, guard = 0;
    switch (recur) {
    case R_DAYS:
        if (!(mask & WD_ALL)) return AddDays(d, 7);
        do { d = AddDays(d, 1); wd = WeekDay(d); } while (!(mask & (1 << wd)) && ++guard < 8);
        return d;
    }
    return StepDate(d, recur);
}

/* Próxima ocorrência de uma tarefa recorrente estritamente depois de 'limit'. */
static DWORD NextAfter(const Item *it, ULONGLONG limit)
{
    DWORD d = it->date;
    int guard = 0;
    if (!it->recur || !d) return d;
    do { d = StepDateM(d, it->recur, it->wdays); }
    while (Stamp(d, it->time) <= limit && ++guard < 20000);
    return d;
}

static void FmtDate(DWORD d, const WCHAR *fmt, WCHAR *out, int cap)
{
    SYSTEMTIME s;
    ULONGLONG v;
    out[0] = 0;
    if (!ToFt(d, -1, &v)) return;
    FromFt(v, &s); /* preenche o dia da semana */
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &s, fmt, out, cap)) out[0] = 0;
}

static void FmtTime(int t, WCHAR *out)
{
    if (t < 0) { out[0] = 0; return; }
    wsprintfW(out, L"%02d:%02d", t / 100, t % 100);
}

/* "Hoje", "Amanhã", "Ontem" ou "qui 13/11". */
static void DayLabel(DWORD d, DWORD today, WCHAR *out, int cap)
{
    if (d == today)                 lstrcpynW(out, L"Hoje", cap);
    else if (d == AddDays(today, 1))  lstrcpynW(out, L"Amanhã", cap);
    else if (d == AddDays(today, -1)) lstrcpynW(out, L"Ontem", cap);
    else FmtDate(d, L"ddd dd/MM", out, cap);
}

static void Capitalize(WCHAR *s) { if (s[0]) CharUpperBuffW(s, 1); }

/* Data + hora curtas para listas: "Hoje 14:30", "qui 13/11". */
static void WhenLabel(DWORD d, int t, DWORD today, WCHAR *out, int cap)
{
    WCHAR tt[8];
    out[0] = 0;
    if (d) DayLabel(d, today, out, cap);
    if (t >= 0) {
        FmtTime(t, tt);
        if (out[0] && lstrlenW(out) + 7 < cap) lstrcatW(out, L"  ");
        if (lstrlenW(out) + 6 < cap) lstrcatW(out, tt);
    }
}

/* ------------------------------------------------------------------------ */
/* Prazos                                                                   */
/* ------------------------------------------------------------------------ */

/* Fim do prazo: data + hora, ou o fim do dia quando não há hora. */
static BOOL Deadline(const Item *it, ULONGLONG *ft)
{
    if (!it->date) return FALSE;
    if (it->time >= 0) return ToFt(it->date, it->time, ft);
    return ToFt(AddDays(it->date, 1), 0, ft);
}

static ULONGLONG DeadlineStamp(const Item *it) { return Stamp(it->date, it->time >= 0 ? it->time : 2359); }

static int DayDiff(DWORD from, DWORD to)
{
    ULONGLONG a, b;
    if (!ToFt(from, -1, &a) || !ToFt(to, -1, &b)) return 0;
    return (int)((LONGLONG)(b - a) / (LONGLONG)FT_DAY);
}

/*
 * Nível de destaque de uma tarefa pendente:
 *   U_SOON   faltam até 3 dias      U_NEAR  até 24 h
 *   U_URGENT até 2 h                U_LATE  prazo vencido
 * 'label' recebe "em 2 dias", "amanhã", "em 5h", "em 40 min", "atrasada"…
 * 'next'  recebe o próximo instante em que o nível ou o rótulo muda (0 = nunca).
 */
static int Urgency(const Item *it, ULONGLONG now, WCHAR *label, ULONGLONG *next)
{
    static const ULONGLONG off[4] = { 3 * FT_DAY, FT_DAY, 120 * FT_MIN, 0 };
    ULONGLONG D, best = 0, ch = 0;
    LONGLONG r;
    int tier, i;
    WCHAR tmp[32];
    if (!label) label = tmp;
    label[0] = 0;
    if (next) *next = 0;
    if (it->status != S_PENDING || !Deadline(it, &D)) return U_NONE;
    r = (LONGLONG)(D - now);
    if (r <= 0) tier = U_LATE;
    else if (r <= (LONGLONG)(120 * FT_MIN)) tier = U_URGENT;
    else if (r <= (LONGLONG)FT_DAY) tier = U_NEAR;
    else if (r <= (LONGLONG)(3 * FT_DAY)) tier = U_SOON;
    else tier = U_NONE;
    for (i = 0; i < 4; i++) {
        ULONGLONG b = D - off[i];
        if (b > now && (!best || b < best)) best = b;
    }

    if (tier == U_LATE) lstrcpyW(label, L"atrasada");
    else if (it->time < 0 || r >= (LONGLONG)FT_DAY) {
        int dd = DayDiff(Today(), it->date);
        if (dd <= 0) lstrcpyW(label, L"hoje");
        else if (dd == 1) lstrcpyW(label, L"amanhã");
        else wsprintfW(label, L"em %d dias", dd);
        /* muda na virada do dia, que já acorda o app */
    } else if (r < (LONGLONG)(60 * FT_MIN)) {
        LONGLONG m = (r + (LONGLONG)FT_MIN - 1) / (LONGLONG)FT_MIN;
        wsprintfW(label, L"em %d min", (int)m);
        ch = D - (ULONGLONG)(m - 1) * FT_MIN;
    } else if (r <= (LONGLONG)(120 * FT_MIN)) {     /* "em 1h30" */
        LONGLONG m = (r + (LONGLONG)FT_MIN - 1) / (LONGLONG)FT_MIN;
        if (m >= 120) lstrcpyW(label, L"em 2h");
        else if (m == 60) lstrcpyW(label, L"em 1h");
        else wsprintfW(label, L"em 1h%02d", (int)(m - 60));
        ch = D - (ULONGLONG)(m - 1) * FT_MIN;
    } else {                                        /* "em 5h", arredondado para cima */
        LONGLONG h = (r + (LONGLONG)(60 * FT_MIN) - 1) / (LONGLONG)(60 * FT_MIN);
        wsprintfW(label, L"em %dh", (int)h);
        ch = D - (ULONGLONG)(h - 1) * 60 * FT_MIN;
    }
    if (ch > now && (!best || ch < best)) best = ch;
    if (next) *next = best;
    return tier;
}

/* Primeiro dia >= d que está na máscara (para alinhar a data ao escolher os dias). */
static DWORD AlignToDays(DWORD d, int mask)
{
    int guard = 0;
    if (!(mask & WD_ALL) || !d) return d;
    while (!(mask & (1 << WeekDay(d))) && ++guard < 8) d = AddDays(d, 1);
    return d;
}

/* "Seg, Qua, Sex"; atalhos para dias úteis, fim de semana e todos. */
static void DaysLabel(int mask, WCHAR *out, int cap)
{
    static const int order[7] = { 1, 2, 3, 4, 5, 6, 0 };   /* começa na segunda */
    int i, n = 0;
    out[0] = 0;
    mask &= WD_ALL;
    if (mask == WD_ALL) { lstrcpynW(out, L"Todo dia", cap); return; }
    if (mask == 0x3E)   { lstrcpynW(out, L"Dias úteis", cap); return; }
    if (mask == 0x41)   { lstrcpynW(out, L"Fim de semana", cap); return; }
    for (i = 0; i < 7; i++)
        if (mask & (1 << order[i]) && lstrlenW(out) + 6 < cap) {
            if (n++) lstrcatW(out, L", ");
            lstrcatW(out, kWdShort[order[i]]);
        }
    if (!n) lstrcpynW(out, L"Nenhum dia", cap);
}

/* Nome da repetição de uma tarefa (com os dias, quando for R_DAYS). */
static void RecurLabel(int recur, int mask, WCHAR *out, int cap)
{
    if (recur == R_DAYS) DaysLabel(mask, out, cap);
    else lstrcpynW(out, kRecurName[recur >= 0 && recur <= R_MONTHLY ? recur : 0], cap);
}
