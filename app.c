/* app.c — ações, menus, bandeja, janela de mensagens e ponto de entrada. */

/* ------------------------------------------------------------------------ */
/* Propagação de mudanças                                                   */
/* ------------------------------------------------------------------------ */

static void ScheduleTrim(void) { if (g_msg) SetTimer(g_msg, TMR_TRIM, 3000, NULL); }

static void Changed(void)
{
    Save();
    SyncOverlays();
    StartAnim();
    Reschedule();
    MainRefresh();
}

static void StatesChanged(void)
{
    SaveStates();
    SaveConfig();
    RenderAll();
    MainRefresh();
}

static void GroupsChanged(void)
{
    SaveGroups();
    SaveConfig();
    SyncOverlays();
    RenderAll();
    MainRefresh();
}

static void ConfigChanged(void)
{
    SaveConfig();
    g_fontDirty = TRUE;
    SyncOverlays();
    RenderAll();
    MainRefresh();
}

/* ------------------------------------------------------------------------ */
/* Ações sobre tarefas                                                      */
/* ------------------------------------------------------------------------ */

/* Marca como concluída (com a animação "feita"). autoDone = foi o fim do prazo.
   Não salva nem redesenha: quem chama decide (Complete ou o agendador). */
static BOOL CompleteCore(DWORD id, BOOL autoDone)
{
    Item *it = GetItem(id);
    if (!it || it->status != S_PENDING) return FALSE;
    if (it->recur && it->date) {
        /* recorrente: registra a conclusão como cópia e avança a original */
        Item copy = *it, *c;
        copy.notes = StrDupW(it->notes);
        it->date = NextAfter(it, Stamp(Today(), 2359));
        it->notified = 0;
        it->flags &= (BYTE)~F_KEEP;
        copy.flags = (BYTE)((copy.flags & F_SHOW_MASK) | (autoDone ? F_AUTO : 0));
        copy.id = g_nextId++;
        copy.parent = id;
        copy.recur = R_NONE;
        copy.status = S_DONE;
        copy.done = NowStamp();
        c = NewItem();             /* pode realocar: 'it' não é mais usado */
        if (!c) { Free(copy.notes); return FALSE; }
        *c = copy;
        Animate(c, A_DONE);
        g_lastDoneId = copy.id;
    } else {
        it->status = S_DONE;
        it->done = NowStamp();
        it->flags = (BYTE)((it->flags & F_SHOW_MASK) | (autoDone ? F_AUTO : 0));
        Animate(it, A_DONE);
        g_lastDoneId = id;
    }
    return TRUE;
}

static void Complete(DWORD id)
{
    if (CompleteCore(id, FALSE)) Changed();
}

static void Restore(DWORD id)
{
    int i = FindItem(id);
    Item *it;
    if (i < 0) return;
    it = &g_items[i];
    if (it->status == S_PENDING) return;
    if (it->status == S_DONE && it->parent) {
        Item *p = GetItem(it->parent);
        if (p && p->status == S_PENDING && p->recur && it->date && it->date < p->date) {
            /* desfaz a conclusão de uma recorrente: volta a data e apaga o registro */
            p->date = it->date;
            ItemTimingChanged(p);
            Animate(p, A_IN);
            RemoveAt(i);
            if (g_lastDoneId == id) g_lastDoneId = 0;
            Changed();
            return;
        }
    }
    it->status = S_PENDING;
    it->done = 0;
    it->flags &= F_SHOW_MASK;
    KeepIfPast(it);            /* restaurada depois do prazo: não conclui sozinha de novo */
    Animate(it, A_IN);
    if (g_lastDoneId == id) g_lastDoneId = 0;
    Changed();
}

static void DeleteItem(DWORD id)
{
    Item *it = GetItem(id);
    BOOL onDesktop;
    if (!it) return;
    onDesktop = it->status == S_PENDING && IsVisible(it, Today()) && GroupOnDesktop(ItemGroup(it));
    if (g_hasUndoDel) FreeItemData(&g_undoDel);
    g_undoDel = *it;
    g_undoDel.notes = StrDupW(it->notes);
    g_undoDel.anim = A_NONE;
    g_hasUndoDel = TRUE;
    it->status = S_DELETED;
    if (onDesktop) Animate(it, A_OUT);
    else { it->anim = A_NONE; ArchiveOld(FALSE); }
    Changed();
}

static void UndoDelete(void)
{
    Item *it;
    if (!g_hasUndoDel) return;
    g_hasUndoDel = FALSE;
    it = GetItem(g_undoDel.id);
    if (it) {                                 /* ainda animando: só volta */
        it->status = g_undoDel.status;
        Animate(it, A_IN);
        FreeItemData(&g_undoDel);
    } else {
        it = NewItem();
        if (!it) { FreeItemData(&g_undoDel); return; }
        *it = g_undoDel;                       /* a nota passa a ser do item */
        g_undoDel.notes = NULL;
        Animate(it, A_IN);
    }
    KeepIfPast(it);
    Changed();
}

static void Postpone(DWORD id)
{
    Item *it = GetItem(id);
    if (!it) return;
    it->date = AddDays(Today(), 1);
    ItemTimingChanged(it);
    Animate(it, A_OUT);
    Changed();
}

static void MoveToGroup(DWORD id, DWORD gid)
{
    Item *it = GetItem(id);
    if (!it || !GetGroup(gid) || it->group == gid) return;
    it->group = gid;
    if (it->status == S_PENDING) Animate(it, A_IN);
    Changed();
}

static void SetItemState(DWORD id, DWORD sid)
{
    Item *it = GetItem(id);
    if (!it || !GetState(sid) || it->state == sid) return;
    it->state = sid;
    Changed();
}

static void SetItemShow(DWORD id, int mode)
{
    Item *it = GetItem(id);
    if (!it || mode < SH_DEFAULT || mode > SH_NEVER || ShowMode(it) == mode) return;
    SetShowMode(it, mode);
    if (it->status == S_PENDING && IsVisible(it, Today())) Animate(it, A_IN);
    Changed();
}

static void TogglePrio(DWORD id)
{
    Item *it = GetItem(id);
    if (!it) return;
    it->prio = it->prio ? 0 : 1;
    Changed();
}

static void DuplicateItem(DWORD id)
{
    Item *src = GetItem(id), copy, *it;
    if (!src) return;
    copy = *src;
    copy.notes = StrDupW(src->notes);
    copy.id = g_nextId++;
    copy.parent = 0;
    copy.status = S_PENDING;
    copy.done = 0;
    copy.flags &= F_SHOW_MASK;
    copy.created = NowStamp();
    it = NewItem();
    if (!it) { Free(copy.notes); return; }
    *it = copy;
    ItemTimingChanged(it);
    Animate(it, A_IN);
    Changed();
}

/* ------------------------------------------------------------------------ */
/* Diálogos simples                                                         */
/* ------------------------------------------------------------------------ */

static BOOL ConfirmBox(HWND owner, const WCHAR *title, const WCHAR *text, const WCHAR *ok)
{
    typedef HRESULT (WINAPI *TaskDialogIndirect_t)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
    HMODULE cc = GetModuleHandleW(L"comctl32.dll");
    TaskDialogIndirect_t tdi = cc ? (TaskDialogIndirect_t)(void *)GetProcAddress(cc, "TaskDialogIndirect") : NULL;
    if (tdi) {
        TASKDIALOGCONFIG c;
        TASKDIALOG_BUTTON b[1];
        int pressed = 0;
        b[0].nButtonID = 100; b[0].pszButtonText = ok;
        ZeroMemory(&c, sizeof c);
        c.cbSize = sizeof c;
        c.hwndParent = owner;
        c.hInstance = g_inst;
        c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        c.pszWindowTitle = APP;
        c.pszMainInstruction = title;
        c.pszContent = text;
        c.cButtons = 1;
        c.pButtons = b;
        c.nDefaultButton = IDCANCEL;
        if (SUCCEEDED(tdi(&c, &pressed, NULL, NULL))) return pressed == 100;
    }
    return MessageBoxW(owner, text, title, MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}

static void ChooseTextFont(HWND owner)
{
    LOGFONTW lf;
    CHOOSEFONTW cf;
    HDC sdc = GetDC(NULL);
    ZeroMemory(&lf, sizeof lf);
    lf.lfHeight = -MulDiv(g_cfg.size10, GetDeviceCaps(sdc, LOGPIXELSY), 720);
    ReleaseDC(NULL, sdc);
    lf.lfWeight = g_cfg.weight;
    lf.lfItalic = (BYTE)g_cfg.italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, g_cfg.face, LF_FACESIZE);
    ZeroMemory(&cf, sizeof cf);
    cf.lStructSize = sizeof cf;
    cf.hwndOwner = owner;
    cf.lpLogFont = &lf;
    cf.rgbColors = g_cfg.color;
    cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    if (!ChooseFontW(&cf)) return;
    lstrcpynW(g_cfg.face, lf.lfFaceName, LF_FACESIZE);
    if (cf.iPointSize >= 60) g_cfg.size10 = cf.iPointSize;
    g_cfg.weight = lf.lfWeight ? lf.lfWeight : FW_NORMAL;
    g_cfg.italic = lf.lfItalic ? 1 : 0;
    g_cfg.color = cf.rgbColors;
    ConfigChanged();
}

/* Seletor de cores: a área de trabalho muda ao vivo enquanto você escolhe. */
static void TextColorCb(void *ctx, COLORREF c, int phase)
{
    (void)ctx;
    g_cfg.color = c;
    if (phase == 1) ConfigChanged();
    else { RenderAll(); MainRefresh(); }
}

static void ChooseTextColor(HWND owner)
{
    ColorOpen(owner, g_cfg.color, L"Cor do texto", TextColorCb, NULL);
}

static void StateColorCb(void *ctx, COLORREF c, int phase)
{
    State *s = GetState((DWORD)(ULONG_PTR)ctx);
    if (!s) return;
    s->color = c;
    if (phase == 1) StatesChanged();
    else { RenderAll(); MainRefresh(); }
}

static void GroupColorCb(void *ctx, COLORREF c, int phase)
{
    Group *g = GetGroup((DWORD)(ULONG_PTR)ctx);
    if (!g) return;
    g->color = c;
    if (phase == 1) GroupsChanged();
    else { RenderAll(); MainRefresh(); }
}

/* ------------------------------------------------------------------------ */
/* Menus                                                                    */
/* ------------------------------------------------------------------------ */

static void MenuLabel(WCHAR *out, int cap, const WCHAR *pre, const WCHAR *text)
{
    int k = 0, n = 0;
    const WCHAR *s;
    for (s = pre; *s && k < cap - 1; s++) out[k++] = *s;
    for (s = text; *s && k < cap - 3; s++, n++) {
        if (n >= 44) { out[k++] = L'…'; break; }
        if (*s == L'&') out[k++] = L'&';
        out[k++] = *s;
    }
    out[k] = 0;
}

static UINT RunMenu(HMENU m, POINT pt, HWND owner)
{
    UINT cmd;
    if (!owner) owner = g_msg;
    if (owner == g_msg) SetForegroundWindow(g_msg);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, owner, NULL);
    if (owner == g_msg) PostMessageW(g_msg, WM_NULL, 0, 0);
    DestroyMenu(m);
    return cmd;
}

enum { TKM_DONE = 1, TKM_OPEN, TKM_TOMORROW, TKM_PRIO, TKM_DUP, TKM_DELETE, TKM_NEW, TKM_PANEL, TKM_NOTES, TKM_STATE0 = 30, TKM_SHOW0 = 50, TKM_GROUP0 = 100 };

static void TaskMenu(DWORD id, POINT pt, HWND owner, BOOL fromMain)
{
    Item *it = GetItem(id);
    HMENU m, gm, sm, stm;
    UINT c;
    int i;
    BOOL pending;
    if (!it) return;
    pending = it->status == S_PENDING;
    m = CreatePopupMenu();
    gm = CreatePopupMenu();
    sm = CreatePopupMenu();
    stm = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, TKM_DONE, pending ? L"Concluir" : L"Restaurar");
    AppendMenuW(m, MF_STRING, TKM_OPEN, fromMain ? L"Editar título" : L"Abrir detalhes…");
    AppendMenuW(m, MF_STRING, TKM_NOTES, it->notes && it->notes[0] ? L"Editar notas…" : L"Adicionar notas…");
    if (pending) AppendMenuW(m, MF_STRING, TKM_TOMORROW, L"Adiar para amanhã");
    for (i = 0; i < g_nGroups; i++)
        AppendMenuW(gm, MF_STRING | (g_groups[i].id == ItemGroup(it) ? MF_CHECKED : 0), (UINT_PTR)(TKM_GROUP0 + i), g_groups[i].name);
    for (i = 0; i < g_nStates; i++)
        AppendMenuW(stm, MF_STRING | (g_states[i].id == ItemState(it) ? MF_CHECKED : 0), (UINT_PTR)(TKM_STATE0 + i), g_states[i].name);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)stm, L"Estado");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)gm, L"Mover para o grupo");
    for (i = 0; i <= SH_NEVER; i++)
        AppendMenuW(sm, MF_STRING | (ShowMode(it) == i ? MF_CHECKED : 0), (UINT_PTR)(TKM_SHOW0 + i), kShowMenu[i]);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sm, L"Exibir na área de trabalho");
    AppendMenuW(m, MF_STRING | (it->prio ? MF_CHECKED : 0), TKM_PRIO, L"Importante");
    AppendMenuW(m, MF_STRING, TKM_DUP, L"Duplicar");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TKM_DELETE, L"Excluir");
    if (!fromMain) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, TKM_NEW, L"Nova tarefa…\tCtrl+Alt+T");
        AppendMenuW(m, MF_STRING, TKM_PANEL, L"Abrir o Task Killer");
    }
    c = RunMenu(m, pt, owner);
    switch (c) {
    case TKM_DONE:
        if (pending) { Complete(id); if (fromMain) MainToast(L"Tarefa concluída", 1, g_lastDoneId); }
        else Restore(id);
        break;
    case TKM_OPEN:
        if (fromMain) { SetFocus(m_title); SendMessageW(m_title, EM_SETSEL, 0, -1); }
        else MainOpen(id);
        break;
    case TKM_TOMORROW: Postpone(id); break;
    case TKM_NOTES:    MainOpenNotes(id); break;
    case TKM_PRIO:     TogglePrio(id); break;
    case TKM_DUP:      DuplicateItem(id); break;
    case TKM_DELETE:
        if (fromMain && m_sel == id) MSelect(0);
        DeleteItem(id);
        if (fromMain) MainToast(L"Tarefa excluída", 2, id);
        break;
    case TKM_NEW:      QuickOpen(ItemGroup(it)); break;
    case TKM_PANEL:    MainOpen(0); break;
    default:
        if (c >= TKM_STATE0 && (int)c < TKM_STATE0 + g_nStates) { SetItemState(id, g_states[c - TKM_STATE0].id); break; }
        if (c >= TKM_SHOW0 && c <= TKM_SHOW0 + SH_NEVER) { SetItemShow(id, (int)(c - TKM_SHOW0)); break; }
        if (c >= TKM_GROUP0 && (int)c < TKM_GROUP0 + g_nGroups) MoveToGroup(id, g_groups[c - TKM_GROUP0].id);
    }
}

enum { GRM_RENAME = 1, GRM_HIDE, GRM_DEFAULT, GRM_UP, GRM_DOWN, GRM_DELETE, GRM_ADD, GRM_CUSTOM, GRM_COLOR0 = 100 };

static void GroupMenu(DWORD gid, POINT pt, HWND owner, BOOL fromMain)
{
    Group *g = GetGroup(gid);
    HMENU m, cm;
    UINT c;
    int i, idx;
    if (!g) return;
    idx = FindGroup(gid);
    m = CreatePopupMenu();
    cm = CreatePopupMenu();
    if (fromMain) AppendMenuW(m, MF_STRING, GRM_RENAME, L"Renomear");
    for (i = 0; i < 8; i++)
        AppendMenuW(cm, MF_STRING | (g->color == kPalette[i] ? MF_CHECKED : 0), (UINT_PTR)(GRM_COLOR0 + i), kPaletteName[i]);
    AppendMenuW(cm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(cm, MF_STRING, GRM_CUSTOM, L"Personalizada…");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)cm, L"Cor");
    AppendMenuW(m, MF_STRING | (g->flags & GF_HIDDEN ? 0 : MF_CHECKED), GRM_HIDE, L"Mostrar na área de trabalho");
    AppendMenuW(m, MF_STRING | (g_cfg.defGroup == gid ? MF_CHECKED | MF_GRAYED : 0), GRM_DEFAULT, L"Grupo padrão para novas tarefas");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (idx > 0 ? 0 : MF_GRAYED), GRM_UP, L"Mover para cima");
    AppendMenuW(m, MF_STRING | (idx < g_nGroups - 1 ? 0 : MF_GRAYED), GRM_DOWN, L"Mover para baixo");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, GRM_ADD, L"Nova tarefa neste grupo…");
    AppendMenuW(m, MF_STRING | (g_cfg.defGroup == gid || g_nGroups <= 1 ? MF_GRAYED : 0), GRM_DELETE, L"Excluir grupo…");
    c = RunMenu(m, pt, owner);
    if (!(g = GetGroup(gid))) return;
    switch (c) {
    case GRM_RENAME:  MBeginRename(gid); break;
    case GRM_CUSTOM:
        ColorOpen(owner == g_msg ? NULL : owner, g->color, L"Cor do grupo", GroupColorCb, (void *)(ULONG_PTR)gid);
        break;
    case GRM_HIDE:    g->flags ^= GF_HIDDEN; GroupsChanged(); break;
    case GRM_DEFAULT: g_cfg.defGroup = gid; GroupsChanged(); break;
    case GRM_UP:      MoveGroup(gid, -1); GroupsChanged(); break;
    case GRM_DOWN:    MoveGroup(gid, 1); GroupsChanged(); break;
    case GRM_ADD:     QuickOpen(gid); break;
    case GRM_DELETE: {
        WCHAR t[160];
        wsprintfW(t, L"As tarefas de \"%s\" vão para \"%s\". Nada é apagado.", g->name, GroupName(g_cfg.defGroup));
        if (ConfirmBox(owner == g_msg ? NULL : owner, L"Excluir grupo?", t, L"Excluir grupo") && DeleteGroupData(gid)) {
            Save();
            GroupsChanged();
        }
        break;
    }
    default:
        if (c >= GRM_COLOR0 && c < GRM_COLOR0 + 8) { g->color = kPalette[c - GRM_COLOR0]; GroupsChanged(); }
    }
}

enum { STM_RENAME = 1, STM_COLOR, STM_DEFAULT, STM_UP, STM_DOWN, STM_DELETE };

static void StateMenu(DWORD sid, POINT pt, HWND owner)
{
    State *st = GetState(sid);
    HMENU m;
    UINT c;
    int idx;
    if (!st) return;
    idx = FindState(sid);
    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, STM_RENAME, L"Renomear");
    AppendMenuW(m, MF_STRING, STM_COLOR, L"Cor…");
    AppendMenuW(m, MF_STRING | (g_cfg.defState == sid ? MF_CHECKED | MF_GRAYED : 0), STM_DEFAULT, L"Estado padrão para novas tarefas");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (idx > 0 ? 0 : MF_GRAYED), STM_UP, L"Mover para cima");
    AppendMenuW(m, MF_STRING | (idx < g_nStates - 1 ? 0 : MF_GRAYED), STM_DOWN, L"Mover para baixo");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_cfg.defState == sid || g_nStates <= 1 ? MF_GRAYED : 0), STM_DELETE, L"Excluir estado…");
    c = RunMenu(m, pt, owner);
    if (!(st = GetState(sid))) return;
    switch (c) {
    case STM_RENAME:  MBeginRenameState(sid); break;
    case STM_COLOR:   ColorOpen(owner, st->color, L"Cor do estado", StateColorCb, (void *)(ULONG_PTR)sid); break;
    case STM_DEFAULT: g_cfg.defState = sid; StatesChanged(); break;
    case STM_UP:      MoveState(sid, -1); StatesChanged(); break;
    case STM_DOWN:    MoveState(sid, 1); StatesChanged(); break;
    case STM_DELETE: {
        WCHAR t[160];
        wsprintfW(t, L"As tarefas em \"%s\" passam para \"%s\". Nada é apagado.", st->name, StateName(g_cfg.defState));
        if (ConfirmBox(owner, L"Excluir estado?", t, L"Excluir estado") && DeleteStateData(sid)) { Save(); StatesChanged(); }
        break;
    }
    }
}

enum { TRM_OPEN = 1, TRM_ADD, TRM_UNDO, TRM_LOCK, TRM_LAY0, TRM_LAY1, TRM_SETTINGS, TRM_EXIT, TRM_RESTORE0 = 1000 };

static void MainMenuAt(POINT pt)
{
    HMENU m = CreatePopupMenu(), dn = CreatePopupMenu(), lay = CreatePopupMenu();
    DWORD ids[MENU_LIST_MAX];
    int order[MENU_LIST_MAX], nDn = 0, i, j;
    WCHAR lbl[96];
    UINT c;
    Item *last;

    AppendMenuW(m, MF_STRING, TRM_OPEN, L"Abrir o Task Killer");
    SetMenuDefaultItem(m, TRM_OPEN, FALSE);
    AppendMenuW(m, MF_STRING, TRM_ADD, L"Nova tarefa…\tCtrl+Alt+T");
    last = GetItem(g_lastDoneId);
    if (last && last->status == S_DONE) {
        MenuLabel(lbl, 96, L"Desfazer: ", last->text);
        AppendMenuW(m, MF_STRING, TRM_UNDO, lbl);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);

    for (i = 0; i < g_n; i++) {
        Item *it = &g_items[i];
        if (it->status != S_DONE) continue;
        for (j = nDn; j > 0 && (it->done > g_items[order[j - 1]].done ||
             (it->done == g_items[order[j - 1]].done && it->id > g_items[order[j - 1]].id)); j--)
            if (j < MENU_LIST_MAX) order[j] = order[j - 1];
        if (j < MENU_LIST_MAX) order[j] = i;
        if (nDn < MENU_LIST_MAX) nDn++;
    }
    for (j = 0; j < nDn; j++) {
        MenuLabel(lbl, 96, L"↺  ", g_items[order[j]].text);
        ids[j] = g_items[order[j]].id;
        AppendMenuW(dn, MF_STRING, (UINT_PTR)(TRM_RESTORE0 + j), lbl);
    }
    if (!nDn) AppendMenuW(dn, MF_STRING | MF_GRAYED, 0, L"(nenhuma ainda)");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)dn, L"Concluídas  (clique para restaurar)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(lay, MF_STRING | (g_cfg.layout == 0 ? MF_CHECKED : 0), TRM_LAY0, L"Uma janela com todos os grupos");
    AppendMenuW(lay, MF_STRING | (g_cfg.layout == 1 ? MF_CHECKED : 0), TRM_LAY1, L"Uma janela por grupo");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)lay, L"Organização");
    AppendMenuW(m, MF_STRING | (g_cfg.locked ? MF_CHECKED : 0), TRM_LOCK, L"Travar posições");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TRM_SETTINGS, L"Configurações…");
    AppendMenuW(m, MF_STRING, TRM_EXIT, L"Sair");

    c = RunMenu(m, pt, g_msg);
    if (c >= TRM_RESTORE0 && (int)c < TRM_RESTORE0 + nDn) { Restore(ids[c - TRM_RESTORE0]); return; }
    switch (c) {
    case TRM_OPEN:     MainOpen(0); break;
    case TRM_ADD:      QuickOpen(0); break;
    case TRM_UNDO:     Restore(g_lastDoneId); break;
    case TRM_LOCK:     g_cfg.locked = !g_cfg.locked; ConfigChanged(); break;
    case TRM_LAY0:     g_cfg.layout = 0; ConfigChanged(); break;
    case TRM_LAY1:     g_cfg.layout = 1; ConfigChanged(); break;
    case TRM_SETTINGS: MainShowView(V_SETTINGS); break;
    case TRM_EXIT:     Quit(); break;
    }
}

/* ------------------------------------------------------------------------ */
/* Bandeja                                                                  */
/* ------------------------------------------------------------------------ */

/* Ícone da bandeja: o mesmo do app, no tamanho pequeno do sistema. */
static HICON MakeTrayIcon(void)
{
    int n = GetSystemMetrics(SM_CXSMICON);
    HICON ic = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, n, n, LR_DEFAULTCOLOR);
    return ic ? ic : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
}

static void TrayData(NOTIFYICONDATAW *n)
{
    ZeroMemory(n, sizeof *n);
    n->cbSize = sizeof *n;
    n->hWnd = g_msg;
    n->uID = 1;
}

static void AddTray(void)
{
    NOTIFYICONDATAW n;
    TrayData(&n);
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = g_trayIcon;
    lstrcpynW(n.szTip, APP, 64);
    Shell_NotifyIconW(NIM_ADD, &n);
    n.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &n);
    UpdateTip();
}

static void UpdateTrayIcon(void)
{
    NOTIFYICONDATAW n;
    HICON old = g_trayIcon;
    g_trayIcon = MakeTrayIcon();
    TrayData(&n);
    n.uFlags = NIF_ICON;
    n.hIcon = g_trayIcon;
    Shell_NotifyIconW(NIM_MODIFY, &n);
    if (old) DestroyIcon(old);
}

static void UpdateTip(void)
{
    NOTIFYICONDATAW n;
    int i, c = 0;
    DWORD t = Today();
    if (!g_msg) return;
    for (i = 0; i < g_n; i++)
        if (g_items[i].status == S_PENDING && (!g_items[i].date || g_items[i].date <= t)) c++;
    TrayData(&n);
    n.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (c == 0) lstrcpyW(n.szTip, L"Task Killer — tudo em dia");
    else wsprintfW(n.szTip, L"Task Killer — %d pendente%s hoje", c, c == 1 ? L"" : L"s");
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void Notify(const WCHAR *title, const WCHAR *msg)
{
    NOTIFYICONDATAW n;
    if (!g_msg) return;
    TrayData(&n);
    n.uFlags = NIF_INFO;
    lstrcpynW(n.szInfoTitle, title, 64);
    lstrcpynW(n.szInfo, msg, 256);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

/* Menus escuros quando o Windows está no tema escuro (API não documentada,
   a mesma usada pelo Explorer). Se não existir, nada acontece. */
static void EnableDarkMenus(HWND h)
{
    typedef int  (WINAPI *SetPreferredAppMode_t)(int);
    typedef BOOL (WINAPI *AllowDarkModeForWindow_t)(HWND, BOOL);
    typedef void (WINAPI *FlushMenuThemes_t)(void);
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    FARPROC a, b, c;
    if (!ux) return;
    a = GetProcAddress(ux, MAKEINTRESOURCEA(135));
    b = GetProcAddress(ux, MAKEINTRESOURCEA(133));
    c = GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (a) ((SetPreferredAppMode_t)(void *)a)(1);
    if (b) ((AllowDarkModeForWindow_t)(void *)b)(h, TRUE);
    if (c) ((FlushMenuThemes_t)(void *)c)();
}

static void Quit(void)
{
    NOTIFYICONDATAW n;
    TipHide();
    g_quitting = TRUE;
    TrayData(&n);
    Shell_NotifyIconW(NIM_DELETE, &n);
    if (m_hwnd) DestroyWindow(m_hwnd);
    if (q_hwnd) DestroyWindow(q_hwnd);
    if (c_hwnd) DestroyWindow(c_hwnd);
    if (a_hwnd) { AlarmSound(FALSE); DestroyWindow(a_hwnd); }
    if (t_hwnd) DestroyWindow(t_hwnd);
    if (d_hwnd) DaysFinish(FALSE);
    if (k_hwnd) KFinish(FALSE);
    DestroyAllOverlays();
    DestroyWindow(g_msg);
}

/* ------------------------------------------------------------------------ */
/* Janela de mensagens (invisível)                                          */
/* ------------------------------------------------------------------------ */

static void OnSchedule(void)
{
    DWORD t = Today();
    if (t != g_lastDay) { g_lastDay = t; ArchiveOld(FALSE); UpdateTip(); SyncOverlays(); MainRefresh(); }
    Reschedule();
    RenderAll();
    MainRefresh();
}

static LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == g_msgTaskbarCreated && m) { /* o Explorer reiniciou */
        AddTray();
        SyncOverlays();
        RenderAll();
        return 0;
    }
    switch (m) {
    case WM_TRAY:
        switch (LOWORD(l)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case NIN_BALLOONUSERCLICK:
            MainOpen(0);
            break;
        case WM_CONTEXTMENU: {
            POINT pt;
            GetCursorPos(&pt);
            MainMenuAt(pt);
            break;
        }
        }
        return 0;

    case WM_APPOPEN: MainOpen(0); return 0;
    case WM_HOTKEY:  QuickOpen(0); return 0;

    case WM_TIMER:
        if (w == TMR_SCHED) OnSchedule();
        else if (w == TMR_ANIM) AnimTick();
        else if (w == TMR_TIP) TipFire();
        else if (w == TMR_TRIM) {
            KillTimer(h, TMR_TRIM);
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        } else if (w == TMR_RECREATE) {
            KillTimer(h, TMR_RECREATE);
            if (!g_quitting) { SyncOverlays(); RenderAll(); }
        }
        return 0;

    case WM_RECREATE:
        SetTimer(h, TMR_RECREATE, 1500, NULL);
        return 0;

    case WM_POWERBROADCAST:
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) OnSchedule();
        return TRUE;

    case WM_TIMECHANGE:
        OnSchedule();
        return 0;

    case WM_SETTINGCHANGE:
        if (l && !lstrcmpiW((LPCWSTR)l, L"ImmersiveColorSet")) {
            UpdateTrayIcon();
            if (g_cfg.theme == 0) { UiLoadTheme(); MainApplyTheme(); }
        }
        return 0;

    case WM_DISPLAYCHANGE:
        ClampAll();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------------ */
/* Entrada                                                                  */
/* ------------------------------------------------------------------------ */

static void RegClass(const WCHAR *name, WNDPROC proc, UINT style, HICON icon)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = style;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIcon = icon;
    wc.hIconSm = icon ? (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0) : NULL;
    if (icon && !wc.hIconSm) wc.hIconSm = icon;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show)
{
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    HANDLE mtx;
    HICON appIcon;
    (void)prev; (void)show;

    g_inst = hi;

    /* instância única: abrir o Task Killer de novo abre o painel */
    mtx = CreateMutexW(NULL, FALSE, L"Local\\TaskKiller.Instancia");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND o = FindWindowW(CLS_MSG, NULL);
        if (o) {
            DWORD pid = 0;
            GetWindowThreadProcessId(o, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(o, WM_APPOPEN, 0, 0);
        }
        return 0;
    }

    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    InitPaths();
    LoadConfig();
    LoadGroups();
    LoadStates();
    Load();
    {
        int i;
        for (i = 0; i < g_n; i++) {
            if (FindGroup(g_items[i].group) < 0) g_items[i].group = g_cfg.defGroup;
            if (FindState(g_items[i].state) < 0) g_items[i].state = g_cfg.defState;   /* versões antigas: "Aguardando" */
        }
    }
    g_lastDay = Today();
    ArchiveOld(FALSE);

    appIcon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APP));
    RegClass(CLS_OVERLAY, OverlayProc, 0, NULL);
    RegClass(CLS_MSG, MsgProc, 0, appIcon);
    RegClass(CLS_MAIN, MainProc, CS_DBLCLKS, appIcon);
    RegClass(CLS_QUICK, QuickProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_CAL, CalProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_ALARM, AlarmProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_TIME, TimeProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_COLOR, ColorProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_TIP, TipProc, CS_DROPSHADOW, appIcon);
    RegClass(CLS_DAYS, DaysProc, CS_DROPSHADOW, appIcon);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_msg = CreateWindowExW(WS_EX_TOOLWINDOW, CLS_MSG, APP, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    if (!g_msg) return 1;
    EnableDarkMenus(g_msg);
    UiLoadTheme();

    g_trayIcon = MakeTrayIcon();
    AddTray();

    /* primeira execução: já liga o "Iniciar com o Windows" */
    AutoMigrate();
    if (!IniInt(L"autostart_configurado", 0)) { AutoSet(TRUE); IniPutInt(L"autostart_configurado", 1); }
    else if (AutoGet()) AutoSet(TRUE);

    RegisterHotKey(g_msg, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');

    if (g_cfg.x == POS_UNSET || g_cfg.y == POS_UNSET) {
        RECT wa;
        UINT dpi = GetDpiForSystem();
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        g_cfg.x = wa.right - MulDiv(g_cfg.width + 40, (int)dpi, 96);
        g_cfg.y = wa.top + MulDiv(40, (int)dpi, 96);
        SaveConfig();
    }

    SyncOverlays();
    Reschedule();
    if (cmd && Contains(cmd, L"/painel")) MainOpen(0);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (MainKey(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    if (mtx) CloseHandle(mtx);
    return 0;
}
