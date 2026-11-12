/*
 * ui.c — kit visual das janelas (painel, adição rápida, calendário, alarme).
 *
 * Formas com anti-aliasing via GDI+ (gdiplus.dll do próprio Windows),
 * carregada só enquanto alguma dessas janelas está aberta e descarregada
 * ao fechar. Texto com ClearType via GDI. Ícones da fonte Segoe Fluent
 * Icons (Windows 11) ou Segoe MDL2 Assets (Windows 10).
 */

/* ------------------------------------------------------------------------ */
/* Tema                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
    BOOL dark;
    COLORREF bg, side, surf, surf2, field, border, seg, text, text2, text3,
             accent, accentHi, onAccent, danger, success, warn, toast, toastText;
} Theme;

static Theme T;

static void UiLoadTheme(void)
{
    BOOL dark = g_cfg.theme == 2 || (g_cfg.theme == 0 && !RegFlag(THEME_KEY, L"AppsUseLightTheme", TRUE));
    T.dark = dark;
    if (dark) {
        T.bg = RGB(18, 19, 22);      T.side = RGB(22, 23, 27);   T.surf = RGB(27, 28, 33);
        T.surf2 = RGB(36, 38, 44);   T.field = RGB(33, 35, 40);  T.border = RGB(44, 46, 53);
        T.seg = RGB(52, 55, 63);
        T.text = RGB(236, 237, 240); T.text2 = RGB(160, 164, 173); T.text3 = RGB(108, 112, 122);
        T.accent = RGB(122, 162, 255); T.accentHi = RGB(150, 182, 255); T.onAccent = RGB(12, 16, 28);
        T.danger = RGB(242, 107, 107); T.success = RGB(76, 195, 138); T.warn = RGB(245, 184, 76);
        T.toast = RGB(46, 48, 56);   T.toastText = RGB(236, 237, 240);
    } else {
        T.bg = RGB(246, 247, 249);   T.side = RGB(255, 255, 255); T.surf = RGB(255, 255, 255);
        T.surf2 = RGB(241, 243, 246); T.field = RGB(246, 247, 249); T.border = RGB(228, 230, 235);
        T.seg = RGB(255, 255, 255);
        T.text = RGB(22, 24, 29);    T.text2 = RGB(91, 96, 107);  T.text3 = RGB(145, 150, 161);
        T.accent = RGB(61, 99, 245); T.accentHi = RGB(88, 122, 250); T.onAccent = RGB(255, 255, 255);
        T.danger = RGB(229, 72, 77); T.success = RGB(47, 158, 110); T.warn = RGB(214, 150, 30);
        T.toast = RGB(30, 32, 38);   T.toastText = RGB(244, 245, 247);
    }
}

static COLORREF Mix(COLORREF a, COLORREF b, int t) /* t: 0 = a, 255 = b */
{
    return RGB((GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255,
               (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255,
               (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255);
}

/* ------------------------------------------------------------------------ */
/* GDI+ (API "flat", carregada dinamicamente)                               */
/* ------------------------------------------------------------------------ */

typedef struct { unsigned int GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
typedef struct { float X, Y; } GpPointF;

typedef int (WINAPI *GpStartup_t)(ULONG_PTR *, const GpStartupInput *, void *);
typedef void (WINAPI *GpShutdown_t)(ULONG_PTR);
typedef int (WINAPI *GpCreateFromHDC_t)(HDC, void **);
typedef int (WINAPI *GpObj_t)(void *);
typedef int (WINAPI *GpSetMode_t)(void *, int);
typedef int (WINAPI *GpCreateSolidFill_t)(unsigned int, void **);
typedef int (WINAPI *GpCreatePen1_t)(unsigned int, float, int, void **);
typedef int (WINAPI *GpCreatePath_t)(int, void **);
typedef int (WINAPI *GpAddPathArc_t)(void *, float, float, float, float, float, float);
typedef int (WINAPI *GpAddPathRect_t)(void *, float, float, float, float);
typedef int (WINAPI *GpPathOp_t)(void *, void *, void *);
typedef int (WINAPI *GpEllipse_t)(void *, void *, float, float, float, float);
typedef int (WINAPI *GpLine_t)(void *, void *, float, float, float, float);
typedef int (WINAPI *GpLines_t)(void *, void *, const GpPointF *, int);

static struct {
    HMODULE dll;
    ULONG_PTR token;
    int refs;
    GpStartup_t gp_Startup;
    GpShutdown_t gp_Shutdown;
    GpCreateFromHDC_t gp_CreateFromHDC;
    GpObj_t gp_DeleteGraphics, gp_DeleteBrush, gp_DeletePen, gp_DeletePath, gp_ClosePathFigure;
    GpSetMode_t gp_SetSmoothingMode, gp_SetPixelOffsetMode, gp_SetPenStartCap, gp_SetPenEndCap, gp_SetPenLineJoin;
    GpCreateSolidFill_t gp_CreateSolidFill;
    GpCreatePen1_t gp_CreatePen1;
    GpCreatePath_t gp_CreatePath;
    GpAddPathArc_t gp_AddPathArc;
    GpAddPathRect_t gp_AddPathRectangle;
    GpPathOp_t gp_FillPath, gp_DrawPath;
    GpEllipse_t gp_FillEllipse, gp_DrawEllipse, gp_FillRectangle;
    GpLine_t gp_DrawLine;
    GpLines_t gp_DrawLines;
} gp;

static void *GpProc(const char *n) { return (void *)GetProcAddress(gp.dll, n); }

static void UiInit(void)
{
    GpStartupInput in;
    if (gp.refs++) return;
    gp.dll = LoadLibraryExW(L"gdiplus.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gp.dll) return;
    gp.gp_Startup            = (GpStartup_t)GpProc("GdiplusStartup");
    gp.gp_Shutdown           = (GpShutdown_t)GpProc("GdiplusShutdown");
    gp.gp_CreateFromHDC      = (GpCreateFromHDC_t)GpProc("GdipCreateFromHDC");
    gp.gp_DeleteGraphics     = (GpObj_t)GpProc("GdipDeleteGraphics");
    gp.gp_DeleteBrush        = (GpObj_t)GpProc("GdipDeleteBrush");
    gp.gp_DeletePen          = (GpObj_t)GpProc("GdipDeletePen");
    gp.gp_DeletePath         = (GpObj_t)GpProc("GdipDeletePath");
    gp.gp_ClosePathFigure    = (GpObj_t)GpProc("GdipClosePathFigure");
    gp.gp_SetSmoothingMode   = (GpSetMode_t)GpProc("GdipSetSmoothingMode");
    gp.gp_SetPixelOffsetMode = (GpSetMode_t)GpProc("GdipSetPixelOffsetMode");
    gp.gp_SetPenStartCap     = (GpSetMode_t)GpProc("GdipSetPenStartCap");
    gp.gp_SetPenEndCap       = (GpSetMode_t)GpProc("GdipSetPenEndCap");
    gp.gp_SetPenLineJoin     = (GpSetMode_t)GpProc("GdipSetPenLineJoin");
    gp.gp_CreateSolidFill    = (GpCreateSolidFill_t)GpProc("GdipCreateSolidFill");
    gp.gp_CreatePen1         = (GpCreatePen1_t)GpProc("GdipCreatePen1");
    gp.gp_CreatePath         = (GpCreatePath_t)GpProc("GdipCreatePath");
    gp.gp_AddPathArc         = (GpAddPathArc_t)GpProc("GdipAddPathArc");
    gp.gp_AddPathRectangle   = (GpAddPathRect_t)GpProc("GdipAddPathRectangle");
    gp.gp_FillPath           = (GpPathOp_t)GpProc("GdipFillPath");
    gp.gp_DrawPath           = (GpPathOp_t)GpProc("GdipDrawPath");
    gp.gp_FillEllipse        = (GpEllipse_t)GpProc("GdipFillEllipse");
    gp.gp_DrawEllipse        = (GpEllipse_t)GpProc("GdipDrawEllipse");
    gp.gp_FillRectangle      = (GpEllipse_t)GpProc("GdipFillRectangle");
    gp.gp_DrawLine           = (GpLine_t)GpProc("GdipDrawLine");
    gp.gp_DrawLines          = (GpLines_t)GpProc("GdipDrawLines");
    in.GdiplusVersion = 1; in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE; in.SuppressExternalCodecs = TRUE;
    if (!gp.gp_Startup || !gp.gp_Shutdown || !gp.gp_CreateFromHDC || !gp.gp_DeleteGraphics || !gp.gp_DeleteBrush ||
        !gp.gp_DeletePen || !gp.gp_DeletePath || !gp.gp_ClosePathFigure || !gp.gp_SetSmoothingMode ||
        !gp.gp_SetPixelOffsetMode || !gp.gp_SetPenStartCap || !gp.gp_SetPenEndCap || !gp.gp_SetPenLineJoin ||
        !gp.gp_CreateSolidFill || !gp.gp_CreatePen1 || !gp.gp_CreatePath || !gp.gp_AddPathArc || !gp.gp_AddPathRectangle ||
        !gp.gp_FillPath || !gp.gp_DrawPath || !gp.gp_FillEllipse || !gp.gp_DrawEllipse || !gp.gp_FillRectangle ||
        !gp.gp_DrawLine || !gp.gp_DrawLines || gp.gp_Startup(&gp.token, &in, NULL) != 0) {
        int refs = gp.refs;
        FreeLibrary(gp.dll);
        ZeroMemory(&gp, sizeof gp);
        gp.refs = refs;
    }
}

static void UiDone(void)
{
    if (gp.refs <= 0) return;
    if (--gp.refs > 0) return;
    if (gp.dll) { gp.gp_Shutdown(gp.token); FreeLibrary(gp.dll); }
    ZeroMemory(&gp, sizeof gp);
}

static unsigned int ARGBc(COLORREF c, int a)
{
    return ((unsigned int)(a & 255) << 24) | ((unsigned int)GetRValue(c) << 16) |
           ((unsigned int)GetGValue(c) << 8) | GetBValue(c);
}

static void *GpBegin(HDC dc)
{
    void *g = NULL;
    if (!gp.dll || gp.gp_CreateFromHDC(dc, &g) != 0) return NULL;
    gp.gp_SetSmoothingMode(g, 4);   /* AntiAlias */
    gp.gp_SetPixelOffsetMode(g, 4); /* Half: alinha com a grade de pixels do GDI */
    return g;
}

static void *GpRoundPath(float x, float y, float w, float h, float r)
{
    void *p = NULL;
    float d;
    if (gp.gp_CreatePath(0, &p) != 0) return NULL;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0.5f) { gp.gp_AddPathRectangle(p, x, y, w, h); return p; }
    d = r * 2;
    gp.gp_AddPathArc(p, x, y, d, d, 180, 90);
    gp.gp_AddPathArc(p, x + w - d, y, d, d, 270, 90);
    gp.gp_AddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    gp.gp_AddPathArc(p, x, y + h - d, d, d, 90, 90);
    gp.gp_ClosePathFigure(p);
    return p;
}

static void GdiRound(HDC dc, int x, int y, int w, int h, int r, COLORREF fill, BOOL hasFill, COLORREF line, BOOL hasLine)
{
    HBRUSH br = hasFill ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pen = hasLine ? CreatePen(PS_SOLID, 1, line) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w + (hasLine ? 0 : 1), y + h + (hasLine ? 0 : 1), r * 2, r * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    if (hasFill) DeleteObject(br);
    if (hasLine) DeleteObject(pen);
}

/* Retângulo arredondado preenchido (alfa 0–255). */
static void URound(HDC dc, int x, int y, int w, int h, int r, COLORREF c, int a)
{
    void *g = GpBegin(dc), *br = NULL, *p;
    if (w <= 0 || h <= 0) { if (g) gp.gp_DeleteGraphics(g); return; }
    if (!g) { GdiRound(dc, x, y, w, h, r, a >= 255 ? c : Mix(T.surf, c, a), TRUE, 0, FALSE); return; }
    gp.gp_CreateSolidFill(ARGBc(c, a), &br);
    p = GpRoundPath((float)x, (float)y, (float)w, (float)h, (float)r);
    if (p && br) gp.gp_FillPath(g, br, p);
    if (p) gp.gp_DeletePath(p);
    if (br) gp.gp_DeleteBrush(br);
    gp.gp_DeleteGraphics(g);
}

/* Contorno arredondado. */
static void URoundLine(HDC dc, int x, int y, int w, int h, int r, COLORREF c, int a, float pw)
{
    void *g = GpBegin(dc), *pen = NULL, *p;
    if (!g) { GdiRound(dc, x, y, w, h, r, 0, FALSE, c, TRUE); return; }
    gp.gp_CreatePen1(ARGBc(c, a), pw, 2, &pen);
    p = GpRoundPath((float)x + pw / 2, (float)y + pw / 2, (float)w - pw, (float)h - pw, (float)r);
    if (p && pen) gp.gp_DrawPath(g, pen, p);
    if (p) gp.gp_DeletePath(p);
    if (pen) gp.gp_DeletePen(pen);
    gp.gp_DeleteGraphics(g);
}

static void UCircle(HDC dc, float cx, float cy, float r, COLORREF c, int a)
{
    void *g = GpBegin(dc), *br = NULL;
    if (!g) { GdiRound(dc, (int)(cx - r), (int)(cy - r), (int)(r * 2), (int)(r * 2), (int)r, c, TRUE, 0, FALSE); return; }
    gp.gp_CreateSolidFill(ARGBc(c, a), &br);
    if (br) { gp.gp_FillEllipse(g, br, cx - r, cy - r, r * 2, r * 2); gp.gp_DeleteBrush(br); }
    gp.gp_DeleteGraphics(g);
}

static void UCircleLine(HDC dc, float cx, float cy, float r, COLORREF c, int a, float pw)
{
    void *g = GpBegin(dc), *pen = NULL;
    if (!g) { GdiRound(dc, (int)(cx - r), (int)(cy - r), (int)(r * 2), (int)(r * 2), (int)r, 0, FALSE, c, TRUE); return; }
    gp.gp_CreatePen1(ARGBc(c, a), pw, 2, &pen);
    if (pen) { gp.gp_DrawEllipse(g, pen, cx - r, cy - r, r * 2, r * 2); gp.gp_DeletePen(pen); }
    gp.gp_DeleteGraphics(g);
}

static void ULine(HDC dc, float x1, float y1, float x2, float y2, COLORREF c, int a, float pw)
{
    void *g = GpBegin(dc), *pen = NULL;
    if (!g) {
        HPEN p = CreatePen(PS_SOLID, (int)(pw + 0.5f), c);
        HGDIOBJ o = SelectObject(dc, p);
        MoveToEx(dc, (int)x1, (int)y1, NULL);
        LineTo(dc, (int)x2, (int)y2);
        SelectObject(dc, o);
        DeleteObject(p);
        return;
    }
    gp.gp_CreatePen1(ARGBc(c, a), pw, 2, &pen);
    if (pen) {
        gp.gp_SetPenStartCap(pen, 2); gp.gp_SetPenEndCap(pen, 2);
        gp.gp_DrawLine(g, pen, x1, y1, x2, y2);
        gp.gp_DeletePen(pen);
    }
    gp.gp_DeleteGraphics(g);
}

/* Marca de "feito" desenhada (não depende de fonte). */
static void UCheck(HDC dc, float cx, float cy, float s, COLORREF c, int a, float pw)
{
    void *g = GpBegin(dc), *pen = NULL;
    GpPointF pt[3];
    pt[0].X = cx - s * 0.42f; pt[0].Y = cy + s * 0.02f;
    pt[1].X = cx - s * 0.12f; pt[1].Y = cy + s * 0.32f;
    pt[2].X = cx + s * 0.45f; pt[2].Y = cy - s * 0.30f;
    if (!g) {
        ULine(dc, pt[0].X, pt[0].Y, pt[1].X, pt[1].Y, c, a, pw);
        ULine(dc, pt[1].X, pt[1].Y, pt[2].X, pt[2].Y, c, a, pw);
        return;
    }
    gp.gp_CreatePen1(ARGBc(c, a), pw, 2, &pen);
    if (pen) {
        gp.gp_SetPenStartCap(pen, 2); gp.gp_SetPenEndCap(pen, 2); gp.gp_SetPenLineJoin(pen, 2);
        gp.gp_DrawLines(g, pen, pt, 3);
        gp.gp_DeletePen(pen);
    }
    gp.gp_DeleteGraphics(g);
}

static void UFill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    SetRect(&r, x, y, x + w, y + h);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* ------------------------------------------------------------------------ */
/* Texto e ícones                                                           */
/* ------------------------------------------------------------------------ */

static WCHAR g_uiFace[LF_FACESIZE], g_uiFaceDisp[LF_FACESIZE], g_iconFace[LF_FACESIZE];

static BOOL FontExists(const WCHAR *face)
{
    HDC dc = GetDC(NULL);
    HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ o = SelectObject(dc, f);
    WCHAR got[LF_FACESIZE];
    BOOL ok;
    got[0] = 0;
    GetTextFaceW(dc, LF_FACESIZE, got);
    ok = !lstrcmpiW(got, face);
    SelectObject(dc, o);
    DeleteObject(f);
    ReleaseDC(NULL, dc);
    return ok;
}

static void UiPickFaces(void)
{
    if (g_uiFace[0]) return;
    if (FontExists(L"Segoe UI Variable Text")) {
        lstrcpyW(g_uiFace, L"Segoe UI Variable Text");
        lstrcpyW(g_uiFaceDisp, FontExists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : g_uiFace);
    } else {
        lstrcpyW(g_uiFace, L"Segoe UI");
        lstrcpyW(g_uiFaceDisp, L"Segoe UI");
    }
    lstrcpyW(g_iconFace, FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets");
}

typedef struct {
    HFONT base, bold, sm, smB, cap, h1, h2, big, edTitle, quick, icon, iconSm, iconLg;
    int lhBase, lhTitle, lhQuick;
    UINT dpi;
} UiFonts;

static HFONT UiFont(const WCHAR *face, int pt10, int weight, UINT dpi)
{
    return CreateFontW(-MulDiv(pt10, (int)dpi, 720), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static int FontLineH(HFONT f)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ o = SelectObject(dc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, o);
    ReleaseDC(NULL, dc);
    return tm.tmHeight;
}

static void UiFontsFree(UiFonts *f)
{
    HFONT *list[13];
    int i;
    list[0] = &f->base; list[1] = &f->bold; list[2] = &f->sm; list[3] = &f->smB; list[4] = &f->cap;
    list[5] = &f->h1; list[6] = &f->h2; list[7] = &f->big; list[8] = &f->edTitle; list[9] = &f->quick;
    list[10] = &f->icon; list[11] = &f->iconSm; list[12] = &f->iconLg;
    for (i = 0; i < 13; i++) if (*list[i]) { DeleteObject(*list[i]); *list[i] = NULL; }
}

static void UiFontsMake(UiFonts *f, UINT dpi)
{
    UiPickFaces();
    UiFontsFree(f);
    f->dpi     = dpi;
    f->base    = UiFont(g_uiFace, 100, FW_NORMAL, dpi);
    f->bold    = UiFont(g_uiFace, 100, FW_SEMIBOLD, dpi);
    f->sm   = UiFont(g_uiFace, 90, FW_NORMAL, dpi);
    f->smB  = UiFont(g_uiFace, 90, FW_SEMIBOLD, dpi);
    f->cap     = UiFont(g_uiFace, 78, FW_SEMIBOLD, dpi);
    f->h1      = UiFont(g_uiFaceDisp, 200, FW_SEMIBOLD, dpi);
    f->h2      = UiFont(g_uiFaceDisp, 125, FW_SEMIBOLD, dpi);
    f->big     = UiFont(g_uiFaceDisp, 230, FW_SEMIBOLD, dpi);
    f->edTitle = UiFont(g_uiFaceDisp, 140, FW_SEMIBOLD, dpi);
    f->quick   = UiFont(g_uiFace, 135, FW_NORMAL, dpi);
    f->icon    = UiFont(g_iconFace, 110, FW_NORMAL, dpi);
    f->iconSm  = UiFont(g_iconFace, 90, FW_NORMAL, dpi);
    f->iconLg  = UiFont(g_iconFace, 200, FW_NORMAL, dpi);
    f->lhBase  = FontLineH(f->base);
    f->lhTitle = FontLineH(f->edTitle);
    f->lhQuick = FontLineH(f->quick);
}

static void UText(HDC dc, HFONT f, const WCHAR *s, int x, int y, int w, int h, COLORREF c, UINT fl)
{
    RECT r;
    HGDIOBJ o = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    SetRect(&r, x, y, x + w, y + h);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
    SelectObject(dc, o);
}
#define UT_L (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS)
#define UT_C (DT_SINGLELINE | DT_VCENTER | DT_CENTER)
#define UT_R (DT_SINGLELINE | DT_VCENTER | DT_RIGHT)

static int UTextW(HDC dc, HFONT f, const WCHAR *s)
{
    SIZE sz;
    HGDIOBJ o = SelectObject(dc, f);
    sz.cx = 0;
    GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
    SelectObject(dc, o);
    return sz.cx;
}

/* Ícone centralizado em (cx, cy). */
static void UIcon(HDC dc, HFONT f, WCHAR glyph, int cx, int cy, COLORREF c)
{
    WCHAR s[2];
    s[0] = glyph; s[1] = 0;
    UText(dc, f, s, cx - 64, cy - 64, 128, 128, c, UT_C);
}

/* glifos (Segoe Fluent Icons / MDL2) */
#define IC_ADD       0xE710
#define IC_SETTINGS  0xE713
#define IC_SEARCH    0xE721
#define IC_CHECK     0xE73E
#define IC_DELETE    0xE74D
#define IC_CALENDAR  0xE787
#define IC_CLOCK     0xE823
#define IC_REPEAT    0xE8EE
#define IC_BELL      0xEA8F
#define IC_SOUND     0xE767
#define IC_STAR      0xE734
#define IC_STARFILL  0xE735
#define IC_HISTORY   0xE81C
#define IC_CHEVDOWN  0xE70D
#define IC_CHEVLEFT  0xE76B
#define IC_CHEVRIGHT 0xE76C
#define IC_CLOSE     0xE711
#define IC_LOCK      0xE72E
#define IC_UNLOCK    0xE785
#define IC_DAY       0xE8BF
#define IC_WEEK      0xE8C0
#define IC_LIST      0xEA37
#define IC_UNDO      0xE7A7
#define IC_MORE      0xE712
#define IC_FOLDER    0xE8B7
#define IC_TASK      0xE73A

/* ------------------------------------------------------------------------ */
/* Zonas clicáveis (registradas durante a pintura)                          */
/* ------------------------------------------------------------------------ */

typedef struct { RECT r; int act, arg; } Zone;
typedef struct { Zone *z; int n, cap; } Zones;

static void ZClear(Zones *zs) { zs->n = 0; }
static void ZFree(Zones *zs) { Free(zs->z); zs->z = NULL; zs->n = zs->cap = 0; }

static void ZAdd(Zones *zs, int x, int y, int w, int h, int act, int arg)
{
    if (zs->n == zs->cap) {
        int nc = zs->cap ? zs->cap * 2 : 64;
        Zone *p = (Zone *)ReAlloc(zs->z, (SIZE_T)nc * sizeof(Zone));
        if (!p) return;
        zs->z = p; zs->cap = nc;
    }
    SetRect(&zs->z[zs->n].r, x, y, x + w, y + h);
    zs->z[zs->n].act = act;
    zs->z[zs->n].arg = arg;
    zs->n++;
}

/* Adiciona só a parte visível (dentro de 'clip'). */
static void ZAddClip(Zones *zs, int x, int y, int w, int h, int act, int arg, const RECT *clip)
{
    RECT a, r;
    SetRect(&a, x, y, x + w, y + h);
    if (!IntersectRect(&r, &a, clip)) return;
    ZAdd(zs, r.left, r.top, r.right - r.left, r.bottom - r.top, act, arg);
}

static Zone *ZHit(Zones *zs, int x, int y)
{
    int i;
    for (i = zs->n - 1; i >= 0; i--) {
        RECT *r = &zs->z[i].r;
        if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) return &zs->z[i];
    }
    return NULL;
}

static Zone *ZFind(Zones *zs, int act, int arg)
{
    int i;
    for (i = 0; i < zs->n; i++) if (zs->z[i].act == act && zs->z[i].arg == arg) return &zs->z[i];
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Janela: barra de título escura, cantos arredondados (DWM)                */
/* ------------------------------------------------------------------------ */

typedef HRESULT (WINAPI *DwmSetAttr_t)(HWND, DWORD, const void *, DWORD);

static HRESULT UiDwm(HWND h, DWORD attr, const void *v, DWORD sz)
{
    static DwmSetAttr_t f;
    static BOOL tried;
    if (!tried) {
        HMODULE m = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        tried = TRUE;
        if (m) f = (DwmSetAttr_t)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
    }
    return f ? f(h, attr, v, sz) : E_FAIL;
}

static void UiStyleWindow(HWND h, BOOL popup)
{
    BOOL d = T.dark;
    int corner = popup ? 2 : 0; /* DWMWCP_ROUND */
    COLORREF cap = T.bg, txt = T.text, bor = T.border;
    UiDwm(h, 20, &d, sizeof d); /* DWMWA_USE_IMMERSIVE_DARK_MODE */
    if (popup) {
        g_win11 = SUCCEEDED(UiDwm(h, 33, &corner, sizeof corner)); /* DWMWA_WINDOW_CORNER_PREFERENCE */
        UiDwm(h, 34, &bor, sizeof bor);                             /* DWMWA_BORDER_COLOR */
    } else {
        UiDwm(h, 35, &cap, sizeof cap);                             /* DWMWA_CAPTION_COLOR */
        UiDwm(h, 36, &txt, sizeof txt);                             /* DWMWA_TEXT_COLOR */
    }
}

/* Barra de rolagem escura nos campos de texto. */
static void UiEditTheme(HWND e)
{
    typedef HRESULT (WINAPI *SetWindowTheme_t)(HWND, LPCWSTR, LPCWSTR);
    static SetWindowTheme_t f;
    static BOOL tried;
    if (!tried) {
        HMODULE m = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        tried = TRUE;
        if (m) f = (SetWindowTheme_t)(void *)GetProcAddress(m, "SetWindowTheme");
    }
    if (f) f(e, T.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

/* Campo de texto sem borda; Enter/Esc são repassados ao pai (WM_EDITKEY). */
static LRESULT CALLBACK EditSub(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC old = (WNDPROC)GetWindowLongPtrW(h, GWLP_USERDATA);
    BOOL multi = (GetWindowLongPtrW(h, GWL_STYLE) & ES_MULTILINE) != 0;
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (m == WM_KEYDOWN) {
        if (w == VK_ESCAPE || (w == VK_RETURN && (!multi || ctrl))) {
            PostMessageW(GetParent(h), WM_EDITKEY, (WPARAM)GetDlgCtrlID(h), (LPARAM)(ctrl && w == VK_RETURN ? 0x10D : w));
            return 0;
        }
        if (!multi && (w == VK_UP || w == VK_DOWN || w == VK_TAB)) {
            PostMessageW(GetParent(h), WM_EDITKEY, (WPARAM)GetDlgCtrlID(h), (LPARAM)w);
            return 0;
        }
        if (ctrl && w == 'A') { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    }
    if (m == WM_CHAR) {
        if (w == 27 || w == 1) return 0;                       /* Esc, Ctrl+A */
        if ((w == L'\r' || w == 10) && (!multi || ctrl)) return 0;
        if (w == L'\t' && !multi) return 0;
    }
    if (m == WM_GETDLGCODE) return CallWindowProcW(old, h, m, w, l) | DLGC_WANTALLKEYS;
    return CallWindowProcW(old, h, m, w, l);
}

static HWND UiEdit(HWND parent, int id, DWORD style, HFONT f, const WCHAR *cue)
{
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    LONG_PTR old;
    if (!e) return NULL;
    SendMessageW(e, WM_SETFONT, (WPARAM)f, FALSE);
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    if (cue) SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    old = GetWindowLongPtrW(e, GWLP_WNDPROC);
    SetWindowLongPtrW(e, GWLP_USERDATA, old);
    SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditSub);
    UiEditTheme(e);
    return e;
}

static void EditText(HWND e, WCHAR *out, int cap)
{
    out[0] = 0;
    if (e) GetWindowTextW(e, out, cap);
}

/* ------------------------------------------------------------------------ */
/* Componentes                                                              */
/* ------------------------------------------------------------------------ */

/* Botão: 0 = primário, 1 = secundário, 2 = fantasma, 3 = perigo (fantasma vermelho) */
static void UButton(HDC dc, UiFonts *f, int x, int y, int w, int h, const WCHAR *text, WCHAR icon, int kind, BOOL hot)
{
    COLORREF fg = T.text, bg = T.surf2;
    int r = h / 4 + 2, tw, ix, gap = icon ? MulDiv(8, (int)f->dpi, 96) : 0, iw = icon ? MulDiv(16, (int)f->dpi, 96) : 0;
    switch (kind) {
    case 0: bg = hot ? T.accentHi : T.accent; fg = T.onAccent; URound(dc, x, y, w, h, r, bg, 255); break;
    case 1: URound(dc, x, y, w, h, r, hot ? Mix(T.surf2, T.text, 18) : T.surf2, 255);
            URoundLine(dc, x, y, w, h, r, T.border, 255, 1.0f); break;
    case 2: if (hot) URound(dc, x, y, w, h, r, T.surf2, 255); fg = T.text2; break;
    case 3: if (hot) URound(dc, x, y, w, h, r, T.danger, 30); fg = T.danger; break;
    }
    tw = text ? UTextW(dc, f->bold, text) : 0;
    ix = x + (w - (tw + iw + (text && icon ? gap : 0))) / 2;
    if (icon) { UIcon(dc, f->icon, icon, ix + iw / 2, y + h / 2, fg); ix += iw + gap; }
    if (text) UText(dc, f->bold, text, ix, y, tw + 2, h, fg, DT_SINGLELINE | DT_VCENTER);
}

static void UToggle(HDC dc, UiFonts *f, int x, int y, BOOL on, BOOL hot)
{
    int w = MulDiv(40, (int)f->dpi, 96), h = MulDiv(22, (int)f->dpi, 96);
    float kr = (float)h / 2 - MulDiv(4, (int)f->dpi, 96) / 2.0f - 1.0f;
    if (on) URound(dc, x, y, w, h, h / 2, hot ? T.accentHi : T.accent, 255);
    else { URound(dc, x, y, w, h, h / 2, hot ? Mix(T.surf2, T.text, 22) : T.surf2, 255); URoundLine(dc, x, y, w, h, h / 2, T.border, 255, 1.0f); }
    UCircle(dc, on ? (float)(x + w - h / 2) : (float)(x + h / 2), (float)y + h / 2.0f, kr,
            on ? T.onAccent : T.text2, 255);
}

/* Controle segmentado; retorna a largura. Registra zonas (act, base + i). */
static int USegment(HDC dc, UiFonts *f, Zones *zs, int xRight, int y, int h, const WCHAR **opts, int n, int sel,
                    int act, int base, int hotAct, int hotArg, BOOL alignLeft)
{
    int pad = MulDiv(14, (int)f->dpi, 96), in = MulDiv(3, (int)f->dpi, 96), i, total = 0, x;
    int w[8];
    for (i = 0; i < n && i < 8; i++) { w[i] = UTextW(dc, f->sm, opts[i]) + pad * 2; total += w[i]; }
    total += in * 2;
    x = alignLeft ? xRight : xRight - total;
    URound(dc, x, y, total, h, MulDiv(8, (int)f->dpi, 96), T.surf2, 255);
    x += in;
    for (i = 0; i < n && i < 8; i++) {
        BOOL s = i == sel, hot = hotAct == act && hotArg == base + i;
        if (s) {
            URound(dc, x, y + in, w[i], h - in * 2, MulDiv(6, (int)f->dpi, 96), T.seg, 255);
            if (!T.dark) URoundLine(dc, x, y + in, w[i], h - in * 2, MulDiv(6, (int)f->dpi, 96), T.border, 255, 1.0f);
        }
        UText(dc, s ? f->smB : f->sm, opts[i], x, y, w[i], h, s ? T.text : (hot ? T.text : T.text2), UT_C);
        ZAdd(zs, x, y, w[i], h, act, base + i);
        x += w[i];
    }
    return total;
}

/* "Chip" arredondado com ícone e texto; retorna a largura. */
static int UChip(HDC dc, UiFonts *f, int x, int y, int h, WCHAR icon, COLORREF iconColor, BOOL dot,
                 const WCHAR *text, BOOL set, BOOL hot, BOOL chevron)
{
    int pad = MulDiv(11, (int)f->dpi, 96), iw = MulDiv(16, (int)f->dpi, 96), gap = MulDiv(6, (int)f->dpi, 96);
    int tw = text && text[0] ? UTextW(dc, f->sm, text) : 0;
    int w = pad * 2 + (icon || dot ? iw : 0) + (tw ? (icon || dot ? gap : 0) + tw : 0) + (chevron ? gap + MulDiv(10, (int)f->dpi, 96) : 0);
    int cx = x + pad;
    URound(dc, x, y, w, h, h / 2, hot ? Mix(T.surf2, T.text, 20) : T.surf2, 255);
    if (!set) URoundLine(dc, x, y, w, h, h / 2, T.border, 255, 1.0f);
    if (dot) UCircle(dc, (float)(cx + iw / 2), (float)y + h / 2.0f, MulDiv(4, (int)f->dpi, 96) + 0.5f, iconColor, 255);
    else if (icon) UIcon(dc, f->iconSm, icon, cx + iw / 2, y + h / 2, set ? iconColor : T.text3);
    if (icon || dot) cx += iw + (tw ? gap : 0);
    if (tw) UText(dc, f->sm, text, cx, y, tw + 2, h, set ? T.text : T.text3, DT_SINGLELINE | DT_VCENTER);
    if (chevron) UIcon(dc, f->iconSm, IC_CHEVDOWN, x + w - pad - MulDiv(4, (int)f->dpi, 96), y + h / 2, T.text3);
    return w;
}

/* Bolinha de concluir. */
static void UCheckCircle(HDC dc, float cx, float cy, float r, BOOL done, BOOL hot, COLORREF ring, float scale)
{
    if (done) {
        UCircle(dc, cx, cy, r, T.accent, 255);
        UCheck(dc, cx, cy, r * 1.05f, T.onAccent, 255, 1.7f * scale);
    } else {
        UCircleLine(dc, cx, cy, r - 0.8f * scale, hot ? T.accent : ring, 255, 1.6f * scale);
        if (hot) UCheck(dc, cx, cy, r * 1.05f, T.accent, 150, 1.6f * scale);
    }
}

/* Menu pop-up de escolha simples; retorna o índice escolhido ou -1. */
static int UPickMenu(HWND owner, POINT pt, const WCHAR **opts, int n, int sel)
{
    HMENU m = CreatePopupMenu();
    UINT c;
    int i;
    for (i = 0; i < n; i++) AppendMenuW(m, MF_STRING | (i == sel ? MF_CHECKED : 0), (UINT_PTR)(i + 1), opts[i]);
    c = RunMenu(m, pt, owner);
    return c ? (int)c - 1 : -1;
}

/* Back buffer: pinta tudo fora da tela e copia de uma vez (sem piscar). */
typedef struct { HDC dc; HBITMAP bmp; HGDIOBJ old; int w, h; } BackBuf;

static HDC BBBegin(BackBuf *b, HDC wdc, int w, int h)
{
    if (!b->dc || w != b->w || h != b->h) {
        if (b->dc) { SelectObject(b->dc, b->old); DeleteObject(b->bmp); DeleteDC(b->dc); }
        b->dc = CreateCompatibleDC(wdc);
        b->bmp = CreateCompatibleBitmap(wdc, w > 0 ? w : 1, h > 0 ? h : 1);
        b->old = SelectObject(b->dc, b->bmp);
        b->w = w; b->h = h;
    }
    return b->dc;
}

static void BBFree(BackBuf *b)
{
    if (b->dc) { SelectObject(b->dc, b->old); DeleteObject(b->bmp); DeleteDC(b->dc); }
    ZeroMemory(b, sizeof *b);
}
