/* data.c — tarefas, grupos, arquivos, histórico e configuração. */

/* ------------------------------------------------------------------------ */
/* Tarefas                                                                  */
/* ------------------------------------------------------------------------ */

static int FindItem(DWORD id)
{
    int i;
    if (!id) return -1;
    for (i = 0; i < g_n; i++) if (g_items[i].id == id) return i;
    return -1;
}

static Item *GetItem(DWORD id) { int i = FindItem(id); return i < 0 ? NULL : &g_items[i]; }

static Item *NewItem(void)
{
    Item *it;
    if (g_n == g_cap) {
        int nc = g_cap ? g_cap * 2 : 32;
        Item *p = (Item *)ReAlloc(g_items, (SIZE_T)nc * sizeof(Item));
        if (!p) return NULL;
        g_items = p; g_cap = nc;
    }
    it = &g_items[g_n++];
    ZeroMemory(it, sizeof *it);
    it->time = -1;
    it->group = g_cfg.defGroup;
    return it;
}

static void FreeItemData(Item *it) { Free(it->notes); it->notes = NULL; }

static void RemoveAt(int i)
{
    if (i < 0 || i >= g_n) return;
    FreeItemData(&g_items[i]);
    MoveMemory(&g_items[i], &g_items[i + 1], (SIZE_T)(g_n - i - 1) * sizeof(Item));
    g_n--;
}

static void ItemSetNotes(Item *it, const WCHAR *s)
{
    Free(it->notes);
    it->notes = StrDupW(s);
}

static BOOL NotesEqual(const WCHAR *a, const WCHAR *b)
{
    if (!a || !*a) return !b || !*b;
    if (!b) return FALSE;
    return !lstrcmpW(a, b);
}

/* Recalcula se um lembrete já "passou" após mudar data/hora/tipo. */
static void ItemTimingChanged(Item *it)
{
    it->notified = (it->type != T_TASK && it->date && it->time >= 0 &&
                    Stamp(it->date, it->time) < NowStamp()) ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* Grupos                                                                   */
/* ------------------------------------------------------------------------ */

static int FindGroup(DWORD id)
{
    int i;
    for (i = 0; i < g_nGroups; i++) if (g_groups[i].id == id) return i;
    return -1;
}

static Group *GetGroup(DWORD id) { int i = FindGroup(id); return i < 0 ? NULL : &g_groups[i]; }

static DWORD ItemGroup(const Item *it) { return FindGroup(it->group) >= 0 ? it->group : g_cfg.defGroup; }

static COLORREF GroupColor(DWORD id) { Group *g = GetGroup(id); return g ? g->color : kPalette[0]; }

static const WCHAR *GroupName(DWORD id) { Group *g = GetGroup(id); return g ? g->name : L"Geral"; }

static void SortGroups(void)
{
    int i, j;
    for (i = 1; i < g_nGroups; i++) {
        Group t = g_groups[i];
        for (j = i; j > 0 && g_groups[j - 1].order > t.order; j--) g_groups[j] = g_groups[j - 1];
        g_groups[j] = t;
    }
    for (i = 0; i < g_nGroups; i++) g_groups[i].order = i;
}

static COLORREF NextGroupColor(void) { return kPalette[g_nGroups % 8]; }

static Group *NewGroup(const WCHAR *name, COLORREF color)
{
    Group *g;
    if (g_nGroups >= MAX_GROUPS) return NULL;
    g = &g_groups[g_nGroups];
    ZeroMemory(g, sizeof *g);
    g->id = g_nextGroupId++;
    g->order = g_nGroups;
    g->color = color;
    g->x = g->y = POS_UNSET;
    lstrcpynW(g->name, name, NAME_MAX);
    CleanText(g->name);
    if (!g->name[0]) lstrcpyW(g->name, L"Grupo");
    g_nGroups++;
    return g;
}

static Group *FindGroupByName(const WCHAR *name)
{
    int i;
    for (i = 0; i < g_nGroups; i++) if (FoldEqual(g_groups[i].name, name)) return &g_groups[i];
    return NULL;
}

/* Exclui o grupo e move as tarefas dele para o grupo padrão. */
static BOOL DeleteGroupData(DWORD id)
{
    int i = FindGroup(id), k;
    if (i < 0 || id == g_cfg.defGroup || g_nGroups <= 1) return FALSE;
    for (k = 0; k < g_n; k++) if (g_items[k].group == id) g_items[k].group = g_cfg.defGroup;
    MoveMemory(&g_groups[i], &g_groups[i + 1], (SIZE_T)(g_nGroups - i - 1) * sizeof(Group));
    g_nGroups--;
    SortGroups();
    return TRUE;
}

static void MoveGroup(DWORD id, int dir)
{
    int i = FindGroup(id), j = i + dir;
    Group t;
    if (i < 0 || j < 0 || j >= g_nGroups) return;
    t = g_groups[i]; g_groups[i] = g_groups[j]; g_groups[j] = t;
    SortGroups();
}

/* ------------------------------------------------------------------------ */
/* Arquivos (UTF-8, gravação atômica)                                       */
/* ------------------------------------------------------------------------ */

static BOOL WriteUtf8(const WCHAR *path, const WCHAR *s, int len, BOOL append)
{
    int n;
    char *u;
    BOOL ok = FALSE;
    HANDLE h;
    DWORD w = 0;

    if (len < 0) len = lstrlenW(s);
    n = len ? WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL) : 0;
    u = (char *)Alloc((SIZE_T)n + 1);
    if (!u) return FALSE;
    if (n) WideCharToMultiByte(CP_UTF8, 0, s, len, u, n, NULL, NULL);

    if (append) {
        h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ok = WriteFile(h, u, (DWORD)n, &w, NULL) && w == (DWORD)n;
            CloseHandle(h);
        }
    } else {
        WCHAR tmp[MAX_PATH + 8];
        wsprintfW(tmp, L"%s.tmp", path);
        h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ok = WriteFile(h, u, (DWORD)n, &w, NULL) && w == (DWORD)n;
            FlushFileBuffers(h);
            CloseHandle(h);
            if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
    Free(u);
    return ok;
}

static WCHAR *ReadUtf8(const WCHAR *path)
{
    HANDLE h;
    LARGE_INTEGER sz;
    DWORD n, r = 0;
    char *u, *s;
    int wn;
    WCHAR *w;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return NULL; }
    n = (DWORD)sz.QuadPart;
    u = (char *)Alloc((SIZE_T)n + 1);
    if (!u || !ReadFile(h, u, n, &r, NULL)) { CloseHandle(h); Free(u); return NULL; }
    CloseHandle(h);

    s = u;
    if (r >= 3 && (BYTE)u[0] == 0xEF && (BYTE)u[1] == 0xBB && (BYTE)u[2] == 0xBF) { s += 3; r -= 3; }
    wn = r ? MultiByteToWideChar(CP_UTF8, 0, s, (int)r, NULL, 0) : 0;
    w = (WCHAR *)Alloc(((SIZE_T)wn + 1) * sizeof(WCHAR));
    if (w && wn) MultiByteToWideChar(CP_UTF8, 0, s, (int)r, w, wn);
    Free(u);
    return w;
}

/* Próxima linha de um buffer (modifica o buffer). Retorna NULL no fim. */
static WCHAR *NextLine(WCHAR **pp)
{
    WCHAR *line = *pp, *p = *pp;
    int L;
    if (!*p) return NULL;
    while (*p && *p != L'\n') p++;
    if (*p == L'\n') { *p = 0; p++; }
    *pp = p;
    L = lstrlenW(line);
    if (L && line[L - 1] == L'\r') line[L - 1] = 0;
    return line;
}

static void BufEsc(Buf *b, const WCHAR *s)
{
    for (; s && *s; s++) {
        if (*s == L'\\')      BufPut(b, L"\\\\", 2);
        else if (*s == L'\t') BufPut(b, L"\\t", 2);
        else if (*s == L'\n') BufPut(b, L"\\n", 2);
        else if (*s != L'\r') BufCh(b, *s);
    }
}

/* Uma linha (formato v2, 14 campos separados por TAB):
   id parent status type recur notified date time created done group prio texto notas */
static void FmtItem(Buf *b, const Item *it)
{
    BufNum(b, it->id);       BufCh(b, L'\t');
    BufNum(b, it->parent);   BufCh(b, L'\t');
    BufNum(b, it->status);   BufCh(b, L'\t');
    BufNum(b, it->type);     BufCh(b, L'\t');
    BufNum(b, it->recur);    BufCh(b, L'\t');
    BufNum(b, it->notified); BufCh(b, L'\t');
    BufNum(b, it->date);     BufCh(b, L'\t');
    BufNum(b, it->time);     BufCh(b, L'\t');
    BufNum(b, (LONGLONG)it->created); BufCh(b, L'\t');
    BufNum(b, (LONGLONG)it->done);    BufCh(b, L'\t');
    BufNum(b, it->group);    BufCh(b, L'\t');
    BufNum(b, it->prio);     BufCh(b, L'\t');
    BufEsc(b, it->text);     BufCh(b, L'\t');
    BufEsc(b, it->notes);
    BufPut(b, L"\r\n", 2);
}

/* Lê uma linha do formato v1 (11 campos) ou v2 (14 campos). */
static BOOL ParseItemLine(WCHAR *line, Item *out)
{
    const WCHAR *q;
    int tabs = 0, k = 0;
    BOOL v2;

    for (q = line; *q; q++) if (*q == L'\t') tabs++;
    if (!line[0] || line[0] == L'#' || tabs < 10) return FALSE;
    v2 = tabs >= 13;

    ZeroMemory(out, sizeof *out);
    q = line;
    out->id       = (DWORD)ParseNum(&q);
    out->parent   = (DWORD)ParseNum(&q);
    out->status   = (BYTE)ParseNum(&q);
    out->type     = (BYTE)ParseNum(&q);
    out->recur    = (BYTE)ParseNum(&q);
    out->notified = (BYTE)ParseNum(&q);
    out->date     = (DWORD)ParseNum(&q);
    out->time     = (int)ParseNum(&q);
    out->created  = (ULONGLONG)ParseNum(&q);
    out->done     = (ULONGLONG)ParseNum(&q);
    if (v2) {
        out->group = (DWORD)ParseNum(&q);
        out->prio  = (BYTE)ParseNum(&q);
    }
    while (*q && *q != L'\t') {
        WCHAR c = *q++;
        if (c == L'\\' && *q) { WCHAR e = *q++; c = (e == L't' || e == L'n') ? L' ' : e; }
        if (k < TEXT_MAX - 1) out->text[k++] = c;
    }
    out->text[k] = 0;
    CleanText(out->text);

    if (v2 && *q == L'\t') {
        Buf nb = { 0 };
        q++;
        while (*q && nb.len < NOTES_MAX) {
            WCHAR c = *q++;
            if (c == L'\\' && *q) {
                WCHAR e = *q++;
                if (e == L'n') { BufPut(&nb, L"\r\n", 2); continue; }
                c = e == L't' ? L' ' : e;
            }
            BufCh(&nb, c);
        }
        if (nb.len) out->notes = nb.p; else Free(nb.p);
    }

    if (!out->id || !out->text[0]) { FreeItemData(out); return FALSE; }
    if (out->status > S_DELETED) out->status = S_PENDING;
    if (out->type > T_ALARM) out->type = T_TASK;
    if (out->recur > R_MONTHLY) out->recur = R_NONE;
    if (out->prio > 1) out->prio = 1;
    if (out->time < -1 || out->time > 2359 || out->time % 100 > 59) out->time = -1;
    if (out->date && !ValidDate(out->date)) out->date = 0;
    return TRUE;
}

static void Save(void)
{
    Buf b = { 0 };
    int i;
    BufPut(&b, L"# The Method v2\tid\tparent\tstatus\ttype\trecur\tnotified\tdate\ttime\tcreated\tdone\tgroup\tprio\ttext\tnotes\r\n", -1);
    for (i = 0; i < g_n; i++) FmtItem(&b, &g_items[i]);
    if (b.p) WriteUtf8(g_dataPath, b.p, b.len, FALSE);
    Free(b.p);
    UpdateTip();
}

static void Load(void)
{
    WCHAR *w = ReadUtf8(g_dataPath), *p, *line;
    if (!w) return;
    p = w;
    while ((line = NextLine(&p)) != NULL) {
        Item tmp, *it;
        if (!ParseItemLine(line, &tmp)) continue;
        if (FindItem(tmp.id) >= 0) { FreeItemData(&tmp); continue; }
        it = NewItem();
        if (!it) { FreeItemData(&tmp); break; }
        *it = tmp;
        if (it->id >= g_nextId) g_nextId = it->id + 1;
    }
    Free(w);
}

/* Lê o histórico completo (só quando o painel mostra a aba Histórico). */
static Item *LoadHistory(int *count)
{
    WCHAR *w = ReadUtf8(g_histPath), *p, *line;
    Item *arr = NULL;
    int n = 0, cap = 0;
    *count = 0;
    if (!w) return NULL;
    p = w;
    while ((line = NextLine(&p)) != NULL) {
        Item tmp;
        if (!ParseItemLine(line, &tmp)) continue;
        if (n == cap) {
            int nc = cap ? cap * 2 : 128;
            Item *q = (Item *)ReAlloc(arr, (SIZE_T)nc * sizeof(Item));
            if (!q) { FreeItemData(&tmp); break; }
            arr = q; cap = nc;
        }
        arr[n++] = tmp;
    }
    Free(w);
    *count = n;
    return arr;
}

static void FreeItems(Item *arr, int n)
{
    int i;
    for (i = 0; i < n; i++) FreeItemData(&arr[i]);
    Free(arr);
}

/* Concluídas há mais de 7 dias e excluídas vão para o histórico (append-only). */
static void ArchiveOld(BOOL allDone)
{
    Buf b = { 0 };
    DWORD limit = AddDays(Today(), -7);
    int i, j;
    BOOL any = FALSE;

    for (i = 0, j = 0; i < g_n; i++) {
        Item *it = &g_items[i];
        BOOL go = !it->anim && (it->status == S_DELETED ||
                  (it->status == S_DONE && (allDone || (DWORD)(it->done / 10000) < limit)));
        if (go) { FmtItem(&b, it); FreeItemData(it); any = TRUE; continue; }
        if (i != j) g_items[j] = g_items[i];
        j++;
    }
    g_n = j;
    if (any) {
        if (GetFileAttributesW(g_histPath) == INVALID_FILE_ATTRIBUTES)
            WriteUtf8(g_histPath, L"# The Method - historico (status 1 = concluida, 2 = excluida)\tid\tparent\tstatus\ttype\trecur\tnotified\tdate\ttime\tcreated\tdone\tgroup\tprio\ttext\tnotes\r\n", -1, TRUE);
        WriteUtf8(g_histPath, b.p, b.len, TRUE);
        Save();
    }
    Free(b.p);
}

/* ------------------------------------------------------------------------ */
/* grupos.tsv: id ordem cor x y flags nome                                  */
/* ------------------------------------------------------------------------ */

static void SaveGroups(void)
{
    Buf b = { 0 };
    int i;
    BufPut(&b, L"# The Method grupos v1\tid\tordem\tcor\tx\ty\tflags\tnome\r\n", -1);
    for (i = 0; i < g_nGroups; i++) {
        Group *g = &g_groups[i];
        BufNum(&b, g->id);     BufCh(&b, L'\t');
        BufNum(&b, g->order);  BufCh(&b, L'\t');
        BufNum(&b, (LONGLONG)g->color); BufCh(&b, L'\t');
        BufNum(&b, g->x);      BufCh(&b, L'\t');
        BufNum(&b, g->y);      BufCh(&b, L'\t');
        BufNum(&b, g->flags);  BufCh(&b, L'\t');
        BufEsc(&b, g->name);
        BufPut(&b, L"\r\n", 2);
    }
    if (b.p) WriteUtf8(g_groupsPath, b.p, b.len, FALSE);
    Free(b.p);
}

static void LoadGroups(void)
{
    WCHAR *w = ReadUtf8(g_groupsPath), *p, *line;
    if (w) {
        p = w;
        while ((line = NextLine(&p)) != NULL && g_nGroups < MAX_GROUPS) {
            const WCHAR *q = line;
            Group g;
            int k = 0;
            if (!line[0] || line[0] == L'#') continue;
            ZeroMemory(&g, sizeof g);
            g.id    = (DWORD)ParseNum(&q);
            g.order = (int)ParseNum(&q);
            g.color = (COLORREF)ParseNum(&q);
            g.x     = (int)ParseNum(&q);
            g.y     = (int)ParseNum(&q);
            g.flags = (DWORD)ParseNum(&q);
            while (*q && k < NAME_MAX - 1) {
                WCHAR c = *q++;
                if (c == L'\\' && *q) { WCHAR e = *q++; c = (e == L't' || e == L'n') ? L' ' : e; }
                g.name[k++] = c;
            }
            g.name[k] = 0;
            CleanText(g.name);
            if (!g.id || !g.name[0] || FindGroup(g.id) >= 0) continue;
            g_groups[g_nGroups++] = g;
            if (g.id >= g_nextGroupId) g_nextGroupId = g.id + 1;
        }
        Free(w);
    }
    if (!g_nGroups) { NewGroup(L"Geral", kPalette[0]); SaveGroups(); }
    SortGroups();
    if (FindGroup(g_cfg.defGroup) < 0) g_cfg.defGroup = g_groups[0].id;
}

/* ------------------------------------------------------------------------ */
/* config.ini (UTF-16)                                                      */
/* ------------------------------------------------------------------------ */

static int IniInt(const WCHAR *k, int def)
{
    WCHAR b[32];
    const WCHAR *p = b;
    GetPrivateProfileStringW(L"risca", k, L"", b, 32, g_iniPath);
    if (!b[0]) return def;
    return (int)ParseNum(&p);
}
static void IniPutInt(const WCHAR *k, int v)
{
    WCHAR b[16];
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(L"risca", k, b, g_iniPath);
}
static int HexVal(WCHAR c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

static void LoadConfig(void)
{
    Config *c = &g_cfg;
    WCHAR b[16];
    int i, v = 0;

    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(g_iniPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            static const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD w;
            WriteFile(h, bom, 2, &w, NULL);
            CloseHandle(h);
        }
    }
    c->x         = IniInt(L"x", POS_UNSET);
    c->y         = IniInt(L"y", POS_UNSET);
    c->width     = IniInt(L"largura", 380);
    c->size10    = IniInt(L"tamanho", 150);
    c->weight    = IniInt(L"peso", FW_NORMAL);
    c->italic    = IniInt(L"italico", 0);
    c->shadow    = IniInt(L"sombra", 1);
    c->showDate  = IniInt(L"mostrar_data", 1);
    c->locked    = IniInt(L"travada", 1);
    c->opacity   = IniInt(L"opacidade", 255);
    c->layout    = IniInt(L"organizacao", 0);
    c->hideEmpty = IniInt(L"ocultar_vazios", 1);
    c->theme     = IniInt(L"tema", 0);
    c->mx        = IniInt(L"painel_x", POS_UNSET);
    c->my        = IniInt(L"painel_y", POS_UNSET);
    c->mw        = IniInt(L"painel_w", 0);
    c->mh        = IniInt(L"painel_h", 0);
    c->mmax      = IniInt(L"painel_max", 0);
    c->defGroup  = (DWORD)IniInt(L"grupo_padrao", 1);
    GetPrivateProfileStringW(L"risca", L"fonte", L"Segoe UI", c->face, LF_FACESIZE, g_iniPath);
    if (!c->face[0]) lstrcpyW(c->face, L"Segoe UI");

    c->color = RGB(255, 255, 255);
    GetPrivateProfileStringW(L"risca", L"cor", L"", b, 16, g_iniPath);
    if (lstrlenW(b) == 6) {
        for (i = 0; i < 6 && HexVal(b[i]) >= 0; i++) v = v * 16 + HexVal(b[i]);
        if (i == 6) c->color = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }
    {   /* cores salvas: "RRGGBB,RRGGBB,..." */
        WCHAR sv[80];
        const WCHAR *p = sv;
        GetPrivateProfileStringW(L"risca", L"cores_salvas", L"", sv, 80, g_iniPath);
        c->nSaved = 0;
        while (*p && c->nSaved < 8) {
            int k, hv = 0, ok = 1;
            for (k = 0; k < 6; k++) { int d = HexVal(p[k]); if (d < 0) { ok = 0; break; } hv = hv * 16 + d; }
            if (!ok) break;
            c->saved[c->nSaved++] = RGB((hv >> 16) & 255, (hv >> 8) & 255, hv & 255);
            p += 6;
            if (*p == L',') p++; else break;
        }
    }
    if (c->width < 200 || c->width > 1600) c->width = 380;
    if (c->size10 < 60 || c->size10 > 960) c->size10 = 150;
    if (c->opacity < 40 || c->opacity > 255) c->opacity = 255;
    if (c->layout < 0 || c->layout > 1) c->layout = 0;
    if (c->theme < 0 || c->theme > 2) c->theme = 0;
}

static void SaveConfig(void)
{
    WCHAR b[16];
    IniPutInt(L"x", g_cfg.x);
    IniPutInt(L"y", g_cfg.y);
    IniPutInt(L"largura", g_cfg.width);
    IniPutInt(L"tamanho", g_cfg.size10);
    IniPutInt(L"peso", g_cfg.weight);
    IniPutInt(L"italico", g_cfg.italic);
    IniPutInt(L"sombra", g_cfg.shadow);
    IniPutInt(L"mostrar_data", g_cfg.showDate);
    IniPutInt(L"travada", g_cfg.locked);
    IniPutInt(L"opacidade", g_cfg.opacity);
    IniPutInt(L"organizacao", g_cfg.layout);
    IniPutInt(L"ocultar_vazios", g_cfg.hideEmpty);
    IniPutInt(L"tema", g_cfg.theme);
    IniPutInt(L"painel_x", g_cfg.mx);
    IniPutInt(L"painel_y", g_cfg.my);
    IniPutInt(L"painel_w", g_cfg.mw);
    IniPutInt(L"painel_h", g_cfg.mh);
    IniPutInt(L"painel_max", g_cfg.mmax);
    IniPutInt(L"grupo_padrao", (int)g_cfg.defGroup);
    WritePrivateProfileStringW(L"risca", L"fonte", g_cfg.face, g_iniPath);
    wsprintfW(b, L"%02X%02X%02X", GetRValue(g_cfg.color), GetGValue(g_cfg.color), GetBValue(g_cfg.color));
    WritePrivateProfileStringW(L"risca", L"cor", b, g_iniPath);
    {
        WCHAR sv[80];
        int i;
        sv[0] = 0;
        for (i = 0; i < g_cfg.nSaved && i < 8; i++) {
            COLORREF c = g_cfg.saved[i];
            wsprintfW(sv + lstrlenW(sv), i ? L",%02X%02X%02X" : L"%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        }
        WritePrivateProfileStringW(L"risca", L"cores_salvas", sv, g_iniPath);
    }
}

static void InitPaths(void)
{
    DWORD n = GetEnvironmentVariableW(L"APPDATA", g_dir, MAX_PATH);
    if (!n || n > MAX_PATH - 40) {
        WCHAR *s;
        GetModuleFileNameW(NULL, g_dir, MAX_PATH);
        for (s = g_dir + lstrlenW(g_dir); s > g_dir && *s != L'\\'; s--) {}
        *s = 0;
    }
    {
        /* Migração: a versão anterior se chamava "Risca". Se só existir a pasta
           antiga, ela é renomeada; se não der (arquivo em uso), os arquivos são copiados. */
        WCHAR old[MAX_PATH];
        wsprintfW(old, L"%s\\Risca", g_dir);
        lstrcatW(g_dir, L"\\The Method");
        if (GetFileAttributesW(g_dir) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES &&
            !MoveFileW(old, g_dir)) {
            static const WCHAR *files[4] = { L"tarefas.tsv", L"historico.tsv", L"config.ini", L"grupos.tsv" };
            WCHAR a[MAX_PATH + 20], b[MAX_PATH + 20];
            int i;
            CreateDirectoryW(g_dir, NULL);
            for (i = 0; i < 4; i++) {
                wsprintfW(a, L"%s\\%s", old, files[i]);
                wsprintfW(b, L"%s\\%s", g_dir, files[i]);
                CopyFileW(a, b, TRUE);
            }
        }
    }
    CreateDirectoryW(g_dir, NULL);
    wsprintfW(g_dataPath,   L"%s\\tarefas.tsv", g_dir);
    wsprintfW(g_histPath,   L"%s\\historico.tsv", g_dir);
    wsprintfW(g_iniPath,    L"%s\\config.ini", g_dir);
    wsprintfW(g_groupsPath, L"%s\\grupos.tsv", g_dir);
}

/* ------------------------------------------------------------------------ */
/* Iniciar com o Windows (HKCU\...\Run)                                     */
/* ------------------------------------------------------------------------ */

static const WCHAR kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static BOOL AutoGet(void)
{
    HKEY k;
    BOOL r = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        r = RegQueryValueExW(k, APP, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return r;
}

static void AutoSet(BOOL on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    if (on) {
        WCHAR p[MAX_PATH], q[MAX_PATH + 4];
        GetModuleFileNameW(NULL, p, MAX_PATH);
        wsprintfW(q, L"\"%s\"", p);
        RegSetValueExW(k, APP, 0, REG_SZ, (const BYTE *)q, (DWORD)((lstrlenW(q) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(k, APP);
    }
    RegCloseKey(k);
}

/* Remove a entrada antiga "Risca" do início do Windows e passa para o novo nome. */
static void AutoMigrate(void)
{
    HKEY k;
    BOOL had = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        had = RegQueryValueExW(k, L"Risca", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        if (had) RegDeleteValueW(k, L"Risca");
        RegCloseKey(k);
    }
    if (had) AutoSet(TRUE);
}

static BOOL RegFlag(const WCHAR *key, const WCHAR *val, BOOL def)
{
    DWORD v = (DWORD)def, sz = sizeof v;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, val, NULL, NULL, (BYTE *)&v, &sz) != ERROR_SUCCESS) v = (DWORD)def;
        RegCloseKey(k);
    }
    return v != 0;
}
#define THEME_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
