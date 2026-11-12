/* ics.c — exportar/importar iCalendar (.ics): Google Agenda, Outlook, Calendário do Windows. */

static void IcsText(Buf *b, const WCHAR *s)
{
    for (; s && *s; s++) {
        if (*s == L'\r') continue;
        if (*s == L'\n') { BufPut(b, L"\\n", 2); continue; }
        if (*s == L'\\' || *s == L';' || *s == L',') BufCh(b, L'\\');
        BufCh(b, *s);
    }
}

static void IcsDate(Buf *b, DWORD d)
{
    WCHAR t[16];
    wsprintfW(t, L"%08lu", (unsigned long)d);
    BufPut(b, t, -1);
}

static void ExportIcs(HWND owner)
{
    WCHAR path[MAX_PATH] = L"The Method.ics", t[80];
    OPENFILENAMEW of;
    SYSTEMTIME u;
    Buf b = { 0 };
    int i, count = 0;

    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = owner;
    of.lpstrFilter = L"Calendário (*.ics)\0*.ics\0";
    of.lpstrFile = path;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"ics";
    of.lpstrTitle = L"Exportar tarefas para o calendário";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&of)) return;

    GetSystemTime(&u);
    BufPut(&b, L"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//The Method//PT-BR\r\nCALSCALE:GREGORIAN\r\n", -1);
    for (i = 0; i < g_n; i++) {
        Item *it = &g_items[i];
        if (it->status != S_PENDING || !it->date) continue;
        count++;
        BufPut(&b, L"BEGIN:VEVENT\r\nUID:themethod-", -1);
        BufNum(&b, it->id);
        BufPut(&b, L"@local\r\n", -1);
        wsprintfW(t, L"DTSTAMP:%04d%02d%02dT%02d%02d%02dZ\r\n", u.wYear, u.wMonth, u.wDay, u.wHour, u.wMinute, u.wSecond);
        BufPut(&b, t, -1);
        BufPut(&b, L"SUMMARY:", -1);
        IcsText(&b, it->text);
        BufPut(&b, L"\r\nCATEGORIES:", -1);
        IcsText(&b, GroupName(ItemGroup(it)));
        BufPut(&b, L"\r\n", 2);
        if (it->notes) { BufPut(&b, L"DESCRIPTION:", -1); IcsText(&b, it->notes); BufPut(&b, L"\r\n", 2); }
        if (it->prio) BufPut(&b, L"PRIORITY:1\r\n", -1);
        if (it->time >= 0) {
            BufPut(&b, L"DTSTART:", -1);
            IcsDate(&b, it->date);
            wsprintfW(t, L"T%02d%02d00\r\nDURATION:PT30M\r\n", it->time / 100, it->time % 100);
            BufPut(&b, t, -1);
        } else {
            BufPut(&b, L"DTSTART;VALUE=DATE:", -1);
            IcsDate(&b, it->date);
            BufPut(&b, L"\r\nDTEND;VALUE=DATE:", -1);
            IcsDate(&b, AddDays(it->date, 1));
            BufPut(&b, L"\r\n", 2);
        }
        switch (it->recur) {
        case R_DAILY:    BufPut(&b, L"RRULE:FREQ=DAILY\r\n", -1); break;
        case R_WEEKDAYS: BufPut(&b, L"RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR\r\n", -1); break;
        case R_WEEKLY:   BufPut(&b, L"RRULE:FREQ=WEEKLY\r\n", -1); break;
        case R_MONTHLY:  BufPut(&b, L"RRULE:FREQ=MONTHLY\r\n", -1); break;
        }
        if (it->type != T_TASK && it->time >= 0) {
            BufPut(&b, L"BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:", -1);
            IcsText(&b, it->text);
            BufPut(&b, L"\r\nTRIGGER:-PT0M\r\nEND:VALARM\r\n", -1);
        }
        BufPut(&b, L"END:VEVENT\r\n", -1);
    }
    BufPut(&b, L"END:VCALENDAR\r\n", -1);

    if (b.p && WriteUtf8(path, b.p, b.len, FALSE)) {
        wsprintfW(t, L"%d tarefa%s com data exportada%s.", count, count == 1 ? L"" : L"s", count == 1 ? L"" : L"s");
        Notify(L"Calendário", t);
        MainToast(t, 0, 0);
    } else {
        MessageBoxW(owner, L"Não foi possível salvar o arquivo.", APP, MB_ICONWARNING);
    }
    Free(b.p);
}

typedef struct {
    BOOL  inEv, inAlarm, hasAlarm;
    WCHAR sum[TEXT_MAX], cat[NAME_MAX];
    Buf   desc;
    DWORD date;
    int   time, recur, prio, added;
    DWORD today, defGroup;
} IcsState;

static void IcsUnescape(const WCHAR *v, WCHAR *out, int cap, BOOL keepNl)
{
    int k = 0;
    while (*v && k < cap - 2) {
        WCHAR c = *v++;
        if (c == L'\\' && *v) {
            WCHAR e = *v++;
            if (e == L'n' || e == L'N') {
                if (keepNl) { out[k++] = L'\r'; out[k++] = L'\n'; continue; }
                c = L' ';
            } else c = e;
        }
        out[k++] = c;
    }
    out[k] = 0;
    if (!keepNl) CleanText(out);
}

static int Digits(const WCHAR *s, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        if (s[i] < L'0' || s[i] > L'9') return -1;
        v = v * 10 + (s[i] - L'0');
    }
    return v;
}

static void IcsParseDate(const WCHAR *v, DWORD *date, int *time)
{
    int y = Digits(v, 4), mo = Digits(v + 4, 2), d = Digits(v + 6, 2);
    if (y < 0 || mo < 0 || d < 0) return;
    *date = (DWORD)(y * 10000 + mo * 100 + d);
    *time = -1;
    if (v[8] == L'T') {
        int hh = Digits(v + 9, 2), mm = Digits(v + 11, 2);
        if (hh >= 0 && mm >= 0) {
            *time = hh * 100 + mm;
            if (v[13] && v[14] && v[15] == L'Z') {
                SYSTEMTIME ut, lt;
                DateToSt(*date, *time, &ut);
                if (SystemTimeToTzSpecificLocalTime(NULL, &ut, &lt)) { *date = StDate(&lt); *time = StTime(&lt); }
            }
        }
    }
    if (!ValidDate(*date)) *date = 0;
}

static BOOL IsDuplicate(const WCHAR *text, DWORD date, int time)
{
    int i;
    for (i = 0; i < g_n; i++) {
        const Item *it = &g_items[i];
        if (it->status == S_PENDING && it->date == date && it->time == time && !lstrcmpW(it->text, text)) return TRUE;
    }
    return FALSE;
}

static void IcsLine(IcsState *st, WCHAR *line)
{
    WCHAR *colon, *semi, *val;
    if (!lstrcmpiW(line, L"BEGIN:VEVENT") || !lstrcmpiW(line, L"BEGIN:VTODO")) {
        st->inEv = TRUE; st->inAlarm = FALSE; st->hasAlarm = FALSE;
        st->sum[0] = 0; st->cat[0] = 0; st->desc.len = 0; if (st->desc.p) st->desc.p[0] = 0;
        st->date = 0; st->time = -1; st->recur = R_NONE; st->prio = 0;
        return;
    }
    if (!st->inEv) return;
    if (!lstrcmpiW(line, L"BEGIN:VALARM")) { st->inAlarm = TRUE; st->hasAlarm = TRUE; return; }
    if (!lstrcmpiW(line, L"END:VALARM"))   { st->inAlarm = FALSE; return; }
    if (!lstrcmpiW(line, L"END:VEVENT") || !lstrcmpiW(line, L"END:VTODO")) {
        st->inEv = FALSE;
        if (st->sum[0] && st->date && st->added < 1000) {
            int guard = 0;
            if (st->recur)
                while (st->date < st->today && ++guard < 20000) st->date = StepDate(st->date, st->recur);
            if (st->date >= st->today && !IsDuplicate(st->sum, st->date, st->time)) {
                Item *it = NewItem();
                if (it) {
                    Group *g = st->cat[0] ? FindGroupByName(st->cat) : NULL;
                    it->id = g_nextId++;
                    it->group = g ? g->id : st->defGroup;
                    it->type = (st->hasAlarm && st->time >= 0) ? T_REMIND : T_TASK;
                    it->recur = (BYTE)st->recur;
                    it->prio = (BYTE)st->prio;
                    it->date = st->date;
                    it->time = st->time;
                    it->created = NowStamp();
                    lstrcpynW(it->text, st->sum, TEXT_MAX);
                    if (st->desc.len) ItemSetNotes(it, st->desc.p);
                    ItemTimingChanged(it);
                    st->added++;
                }
            }
        }
        return;
    }
    if (st->inAlarm) return;

    for (colon = line; *colon && *colon != L':'; colon++) {}
    if (!*colon) return;
    *colon = 0;
    val = colon + 1;
    for (semi = line; *semi && *semi != L';'; semi++) {}
    *semi = 0;

    if (!lstrcmpiW(line, L"SUMMARY")) IcsUnescape(val, st->sum, TEXT_MAX, FALSE);
    else if (!lstrcmpiW(line, L"CATEGORIES")) {
        WCHAR *c;
        IcsUnescape(val, st->cat, NAME_MAX, FALSE);
        for (c = st->cat; *c; c++) if (*c == L',') { *c = 0; break; }
    } else if (!lstrcmpiW(line, L"DESCRIPTION")) {
        WCHAR *tmp = (WCHAR *)Alloc((SIZE_T)NOTES_MAX * sizeof(WCHAR));
        if (tmp) { IcsUnescape(val, tmp, NOTES_MAX, TRUE); st->desc.len = 0; BufPut(&st->desc, tmp, -1); Free(tmp); }
    } else if (!lstrcmpiW(line, L"PRIORITY")) {
        int p = Digits(val, 1);
        st->prio = (p >= 1 && p <= 4) ? 1 : 0;
    } else if (!lstrcmpiW(line, L"DTSTART") || (!lstrcmpiW(line, L"DUE") && !st->date)) IcsParseDate(val, &st->date, &st->time);
    else if (!lstrcmpiW(line, L"RRULE")) {
        if (Contains(val, L"FREQ=DAILY")) st->recur = R_DAILY;
        else if (Contains(val, L"FREQ=WEEKLY")) st->recur = Contains(val, L"BYDAY=MO,TU,WE,TH,FR") ? R_WEEKDAYS : R_WEEKLY;
        else if (Contains(val, L"FREQ=MONTHLY")) st->recur = R_MONTHLY;
    }
}

static void ImportIcs(HWND owner)
{
    WCHAR path[MAX_PATH] = L"", msg[96];
    OPENFILENAMEW of;
    IcsState st;
    WCHAR *w, *p, *line;
    int L = 0;

    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = owner;
    of.lpstrFilter = L"Calendário (*.ics)\0*.ics\0Todos os arquivos\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Importar do calendário";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;

    w = ReadUtf8(path);
    line = (WCHAR *)Alloc(8192 * sizeof(WCHAR));
    if (!w || !line) { Free(w); Free(line); return; }
    ZeroMemory(&st, sizeof st);
    st.today = Today();
    st.time = -1;
    st.defGroup = g_cfg.defGroup;

    for (p = w;; p++) {
        WCHAR c = *p;
        if (c == 0 || c == L'\n') {
            if (c == L'\n' && (p[1] == L' ' || p[1] == L'\t')) { p++; continue; } /* linha dobrada */
            line[L] = 0;
            if (L) IcsLine(&st, line);
            L = 0;
            if (c == 0) break;
            continue;
        }
        if (c != L'\r' && L < 8191) line[L++] = c;
    }
    Free(line);
    Free(w);
    Free(st.desc.p);

    if (st.added) Changed();
    wsprintfW(msg, L"%d item%s importado%s.", st.added, st.added == 1 ? L"" : L"s", st.added == 1 ? L"" : L"s");
    Notify(L"Calendário", msg);
    MainToast(msg, 0, 0);
}
