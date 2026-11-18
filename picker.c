/* picker.c — seletor de hora (qualquer horário) e seletor de cores (anel + triângulo + RGB). */

#define IC_CHEVUP 0xE70E

/* ======================================================================== */
/* Seletor de hora                                                          */
/* ======================================================================== */

enum { TZ_HUP = 1, TZ_HDN, TZ_MUP, TZ_MDN, TZ_HDISP, TZ_MDISP, TZ_HOUR, TZ_MIN, TZ_CLEAR, TZ_NOW, TZ_OK };

static HWND    t_hwnd, t_owner;
static TimeCb  t_cb;
static void   *t_ctx;
static int     t_h, t_m, t_hotA, t_hotG, t_downA, t_downG, t_nkeys;
static WCHAR   t_keys[5];
static UiFonts t_f;
static Zones   t_z;
static BackBuf t_bb;
static BOOL    t_tracking, t_picked;
#define TSZ(x) MulDiv((x), (int)t_f.dpi, 96)

static void TimePaint(HDC dc, int W, int H)
{
    WCHAR s[8];
    int cx = W / 2, dw = TSZ(84), dh = TSZ(56), dy = TSZ(36), x0 = TSZ(16), cw = (W - TSZ(32)) / 6, ch = TSZ(32);
    int hx = cx - TSZ(14) - dw, mx = cx + TSZ(14), i, y0, bw;
    ZClear(&t_z);
    UFill(dc, 0, 0, W, H, T.surf);

    /* mostrador grande com setas */
    if (t_hotA == TZ_HDISP) URound(dc, hx, dy, dw, dh, TSZ(12), T.surf2, 255);
    if (t_hotA == TZ_MDISP) URound(dc, mx, dy, dw, dh, TSZ(12), T.surf2, 255);
    wsprintfW(s, L"%02d", t_h);
    UText(dc, t_f.big, s, hx, dy, dw, dh, T.text, UT_C);
    wsprintfW(s, L"%02d", t_m);
    UText(dc, t_f.big, s, mx, dy, dw, dh, T.text, UT_C);
    UText(dc, t_f.big, L":", cx - TSZ(14), dy, TSZ(28), dh - TSZ(4), T.text3, UT_C);
    ZAdd(&t_z, hx, dy, dw, dh, TZ_HDISP, 0);
    ZAdd(&t_z, mx, dy, dw, dh, TZ_MDISP, 0);
    for (i = 0; i < 2; i++) {
        int x = i ? mx : hx;
        int up = i ? TZ_MUP : TZ_HUP, dn = i ? TZ_MDN : TZ_HDN;
        if (t_hotA == up) URound(dc, x + TSZ(18), TSZ(8), dw - TSZ(36), TSZ(26), TSZ(8), T.surf2, 255);
        if (t_hotA == dn) URound(dc, x + TSZ(18), dy + dh + TSZ(2), dw - TSZ(36), TSZ(26), TSZ(8), T.surf2, 255);
        UIcon(dc, t_f.iconSm, IC_CHEVUP, x + dw / 2, TSZ(21), T.text2);
        UIcon(dc, t_f.iconSm, IC_CHEVDOWN, x + dw / 2, dy + dh + TSZ(15), T.text2);
        ZAdd(&t_z, x + TSZ(18), TSZ(8), dw - TSZ(36), TSZ(26), up, 0);
        ZAdd(&t_z, x + TSZ(18), dy + dh + TSZ(2), dw - TSZ(36), TSZ(26), dn, 0);
    }

    /* horas */
    y0 = TSZ(140);
    UText(dc, t_f.cap, L"HORA", x0 + TSZ(4), y0 - TSZ(22), TSZ(100), TSZ(18), T.text3, UT_L);
    for (i = 0; i < 24; i++) {
        int x = x0 + (i % 6) * cw, y = y0 + (i / 6) * ch;
        BOOL sel = i == t_h, hot = t_hotA == TZ_HOUR && t_hotG == i;
        wsprintfW(s, L"%02d", i);
        if (sel) URound(dc, x + TSZ(3), y + TSZ(2), cw - TSZ(6), ch - TSZ(4), TSZ(8), T.primary, 255);
        else if (hot) URound(dc, x + TSZ(3), y + TSZ(2), cw - TSZ(6), ch - TSZ(4), TSZ(8), T.surf2, 255);
        UText(dc, sel ? t_f.smB : t_f.sm, s, x, y, cw, ch, sel ? T.onPrimary : T.text, UT_C);
        ZAdd(&t_z, x, y, cw, ch, TZ_HOUR, i);
    }

    /* minutos (de 5 em 5; ajuste fino pelas setas, roda do mouse ou teclado) */
    y0 = TSZ(140) + 4 * ch + TSZ(30);
    UText(dc, t_f.cap, L"MINUTOS", x0 + TSZ(4), y0 - TSZ(22), TSZ(160), TSZ(18), T.text3, UT_L);
    for (i = 0; i < 12; i++) {
        int x = x0 + (i % 6) * cw, y = y0 + (i / 6) * ch, v = i * 5;
        BOOL sel = v == t_m, hot = t_hotA == TZ_MIN && t_hotG == v;
        wsprintfW(s, L"%02d", v);
        if (sel) URound(dc, x + TSZ(3), y + TSZ(2), cw - TSZ(6), ch - TSZ(4), TSZ(8), T.primary, 255);
        else if (hot) URound(dc, x + TSZ(3), y + TSZ(2), cw - TSZ(6), ch - TSZ(4), TSZ(8), T.surf2, 255);
        UText(dc, sel ? t_f.smB : t_f.sm, s, x, y, cw, ch, sel ? T.onPrimary : T.text, UT_C);
        ZAdd(&t_z, x, y, cw, ch, TZ_MIN, v);
    }

    UText(dc, t_f.sm, L"Dica: digite os números (ex.: 1437) ou use a roda do mouse.",
          x0, H - TSZ(84), W - TSZ(32), TSZ(20), T.text3, UT_L);
    UFill(dc, TSZ(14), H - TSZ(58), W - TSZ(28), 1, T.border);
    bw = (W - TSZ(28) - TSZ(16)) / 3;
    UButton(dc, &t_f, TSZ(14), H - TSZ(48), bw, TSZ(36), L"Sem horário", 0, 2, t_hotA == TZ_CLEAR);
    ZAdd(&t_z, TSZ(14), H - TSZ(48), bw, TSZ(36), TZ_CLEAR, 0);
    UButton(dc, &t_f, TSZ(14) + bw + TSZ(8), H - TSZ(48), bw, TSZ(36), L"Agora", 0, 2, t_hotA == TZ_NOW);
    ZAdd(&t_z, TSZ(14) + bw + TSZ(8), H - TSZ(48), bw, TSZ(36), TZ_NOW, 0);
    UButton(dc, &t_f, TSZ(14) + (bw + TSZ(8)) * 2, H - TSZ(48), bw, TSZ(36), L"OK", 0, 0, t_hotA == TZ_OK);
    ZAdd(&t_z, TSZ(14) + (bw + TSZ(8)) * 2, H - TSZ(48), bw, TSZ(36), TZ_OK, 0);
    PopupBorder(dc, W, H);
}

static void TimePick(int t)
{
    TimeCb cb = t_cb;
    void *ctx = t_ctx;
    t_picked = TRUE;
    t_cb = NULL;
    if (cb) cb(ctx, t);
    if (t_hwnd) DestroyWindow(t_hwnd);
}

static void TimeStep(BOOL hour, int d)
{
    if (hour) t_h = (t_h + d + 24) % 24;
    else t_m = (t_m + d + 60) % 60;
    t_nkeys = 0;
    InvalidateRect(t_hwnd, NULL, FALSE);
}

static void TimeAct(int a, int g)
{
    switch (a) {
    case TZ_HUP: TimeStep(TRUE, 1); break;
    case TZ_HDN: TimeStep(TRUE, -1); break;
    case TZ_MUP: TimeStep(FALSE, 1); break;
    case TZ_MDN: TimeStep(FALSE, -1); break;
    case TZ_HOUR: t_h = g; t_nkeys = 0; InvalidateRect(t_hwnd, NULL, FALSE); break;
    case TZ_MIN: t_m = g; TimePick(t_h * 100 + t_m); break;
    case TZ_CLEAR: TimePick(-1); break;
    case TZ_NOW: { int n = NowTime(); TimePick(n); break; }
    case TZ_OK: TimePick(t_h * 100 + t_m); break;
    }
}

/* digitar "9", "930", "1437"... */
static void TimeKey(WCHAR c)
{
    int v = 0, i;
    if (t_nkeys >= 4) t_nkeys = 0;
    t_keys[t_nkeys++] = c;
    for (i = 0; i < t_nkeys; i++) v = v * 10 + (t_keys[i] - L'0');
    if (t_nkeys <= 2) { if (v <= 23) t_h = v; }
    else {
        int h = v / 100, m = v % 100;
        if (h <= 23 && m <= 59) { t_h = h; t_m = m; }
    }
    InvalidateRect(t_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK TimeProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        t_hwnd = h;
        UiInit();
        UiFontsMake(&t_f, GetDpiForWindow(h));
        UiStyleWindow(h, TRUE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&t_bb, wdc, c.right, c.bottom);
        TimePaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: PopupHover(h, &t_z, &t_hotA, &t_hotG, &t_tracking, l); return 0;
    case WM_MOUSELEAVE: t_tracking = FALSE; t_hotA = t_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && PopupCursor(h, &t_z)) return TRUE; break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&t_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        t_downA = z ? z->act : 0; t_downG = z ? z->arg : 0;
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&t_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == t_downA && z->arg == t_downG) TimeAct(z->act, z->arg);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt;
        Zone *z;
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        ScreenToClient(h, &pt);
        z = ZHit(&t_z, pt.x, pt.y);
        TimeStep(z && (z->act == TZ_HDISP || z->act == TZ_HOUR || z->act == TZ_HUP || z->act == TZ_HDN), d);
        return 0;
    }
    case WM_CHAR:
        if (w >= L'0' && w <= L'9') TimeKey((WCHAR)w);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        else if (w == VK_RETURN) TimePick(t_h * 100 + t_m);
        else if (w == VK_UP) TimeStep(FALSE, 1);
        else if (w == VK_DOWN) TimeStep(FALSE, -1);
        else if (w == VK_PRIOR) TimeStep(TRUE, 1);
        else if (w == VK_NEXT) TimeStep(TRUE, -1);
        else if (w == VK_BACK) { t_nkeys = 0; }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
        HWND owner = t_owner;
        BOOL picked = t_picked;
        UiFontsFree(&t_f); ZFree(&t_z); BBFree(&t_bb);
        UiDone();
        t_hwnd = NULL; t_owner = NULL; t_cb = NULL;
        if (picked && owner && IsWindow(owner)) SetForegroundWindow(owner);
        QCalClosed();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void TimeOpen(HWND owner, int x, int y, int time, TimeCb cb, void *ctx)
{
    UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    int w = MulDiv(316, (int)dpi, 96), hgt = MulDiv(452, (int)dpi, 96);
    RECT wa;
    POINT pt;
    if (t_hwnd) DestroyWindow(t_hwnd);
    if (time < 0) { int n = NowTime(); time = ((n / 100 + 1) % 24) * 100; }
    t_owner = owner; t_cb = cb; t_ctx = ctx; t_picked = FALSE;
    t_h = time / 100; t_m = time % 100;
    t_hotA = t_hotG = 0; t_nkeys = 0;
    pt.x = x; pt.y = y;
    WorkAreaAt(pt, &wa);
    if (x + w > wa.right) x = wa.right - w;
    if (y + hgt > wa.bottom) y = y - hgt - MulDiv(40, (int)dpi, 96);
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_TIME, L"Horário", WS_POPUP, x, y, w, hgt,
                    owner, NULL, g_inst, NULL);
    if (!t_hwnd) return;
    ShowWindow(t_hwnd, SW_SHOW);
    SetForegroundWindow(t_hwnd);
}

/* ======================================================================== */
/* Seletor de cores                                                         */
/* ======================================================================== */

enum { KZ_FRONT = 1, KZ_ORIG, KZ_SLR, KZ_SLG, KZ_SLB, KZ_SAVED, KZ_ADD, KZ_OK, KZ_CANCEL, KZ_RING, KZ_TRI };
#define IDC_KR   401
#define IDC_KG   402
#define IDC_KB   403
#define IDC_KHEX 404
#define K_PI 3.14159265f

static HWND     k_hwnd, k_owner, k_ed[4];
static ColorCb  k_cb;
static void    *k_ctx;
static COLORREF k_orig, k_rgb;
static float    k_h, k_s, k_v;
static UiFonts  k_f;
static Zones    k_z;
static BackBuf  k_bb;
static HBRUSH   k_brField;
static WCHAR    k_title[48];
static int      k_hotA, k_hotG, k_downA, k_downG, k_drag, k_cx, k_cy, k_R, k_r, k_tx0, k_tx1, k_rowY[3];
static BOOL     k_tracking, k_loading, k_done;
#define KSZ(x) MulDiv((x), (int)k_f.dpi, 96)

static COLORREF HsvToRgb(float h, float s, float v)
{
    float c = v * s, hp = h / 60.0f, x, r = 0, g = 0, b = 0, m;
    int i;
    while (hp >= 6.0f) hp -= 6.0f;
    while (hp < 0.0f) hp += 6.0f;
    x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    i = (int)hp;
    switch (i) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    m = v - c;
    return RGB((int)((r + m) * 255.0f + 0.5f), (int)((g + m) * 255.0f + 0.5f), (int)((b + m) * 255.0f + 0.5f));
}

/* Converte RGB → HSV; mantém o matiz atual quando a cor é cinza. */
static void RgbToHsv(COLORREF c, float *h, float *s, float *v)
{
    float r = GetRValue(c) / 255.0f, g = GetGValue(c) / 255.0f, b = GetBValue(c) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    *v = mx;
    *s = mx > 0.0f ? d / mx : 0.0f;
    if (d > 0.0001f) {
        float hh;
        if (mx == r) hh = fmodf((g - b) / d, 6.0f);
        else if (mx == g) hh = (b - r) / d + 2.0f;
        else hh = (r - g) / d + 4.0f;
        hh *= 60.0f;
        if (hh < 0.0f) hh += 360.0f;
        *h = hh;
    }
}

static float Cl01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static void TriVerts(float *ax, float *ay, float *bx, float *by, float *cx, float *cy)
{
    float a = k_h * K_PI / 180.0f, rt = (float)(k_r - KSZ(5));
    *ax = k_cx + rt * cosf(a);                     *ay = k_cy - rt * sinf(a);
    *bx = k_cx + rt * cosf(a + 2.0943951f);        *by = k_cy - rt * sinf(a + 2.0943951f);
    *cx = k_cx + rt * cosf(a + 4.1887902f);        *cy = k_cy - rt * sinf(a + 4.1887902f);
}

/* Desenha o anel de matiz e o triângulo (pixel a pixel, com anti-aliasing). */
static void KDrawWheel(HDC dc)
{
    int R = k_R, r = k_r, size = 2 * R + 4, ox = k_cx - R - 2, oy = k_cy - R - 2, x, y;
    float ax, ay, bx, by, cx, cy, den, hA, hB, hC;
    COLORREF bg = T.surf, hc = HsvToRgb(k_h, 1.0f, 1.0f);
    float bgr = (float)GetRValue(bg), bgg = (float)GetGValue(bg), bgb = (float)GetBValue(bg);
    float hr = (float)GetRValue(hc), hg = (float)GetGValue(hc), hb = (float)GetBValue(hc);
    BITMAPINFO bi;
    void *bits = NULL;
    HDC mdc;
    HBITMAP bmp;
    HGDIOBJ old;
    DWORD *px;

    TriVerts(&ax, &ay, &bx, &by, &cx, &cy);
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    hA = fabsf(den) / sqrtf((bx - cx) * (bx - cx) + (by - cy) * (by - cy));
    hB = fabsf(den) / sqrtf((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));
    hC = fabsf(den) / sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by));

    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return;
    mdc = CreateCompatibleDC(dc);
    old = SelectObject(mdc, bmp);
    px = (DWORD *)bits;

    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++) {
            float fx = (float)(ox + x) + 0.5f, fy = (float)(oy + y) + 0.5f;
            float dx = fx - (float)k_cx, dy = fy - (float)k_cy, d = sqrtf(dx * dx + dy * dy);
            float cov = 0.0f, cr = 0.0f, cg = 0.0f, cb = 0.0f;
            if (d > (float)r - 1.0f && d < (float)R + 1.0f) {
                cov = Cl01((float)R - d + 0.5f) * Cl01(d - (float)r + 0.5f);
                if (cov > 0.0f) {
                    float hue = atan2f(-dy, dx) * 180.0f / K_PI;
                    COLORREF c;
                    if (hue < 0.0f) hue += 360.0f;
                    c = HsvToRgb(hue, 1.0f, 1.0f);
                    cr = (float)GetRValue(c); cg = (float)GetGValue(c); cb = (float)GetBValue(c);
                }
            } else if (d < (float)r) {
                float wa = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / den;
                float wb = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / den;
                float wc = 1.0f - wa - wb, dmin = wa * hA, t;
                t = wb * hB; if (t < dmin) dmin = t;
                t = wc * hC; if (t < dmin) dmin = t;
                cov = Cl01(dmin + 0.5f);
                if (cov > 0.0f) {
                    float sum;
                    wa = Cl01(wa); wb = Cl01(wb); wc = Cl01(wc);
                    sum = wa + wb + wc;
                    if (sum > 0.0f) { wa /= sum; wb /= sum; }
                    cr = wa * hr + wb * 255.0f;
                    cg = wa * hg + wb * 255.0f;
                    cb = wa * hb + wb * 255.0f;
                }
            }
            {
                int R8 = (int)(bgr + (cr - bgr) * cov + 0.5f), G8 = (int)(bgg + (cg - bgg) * cov + 0.5f),
                    B8 = (int)(bgb + (cb - bgb) * cov + 0.5f);
                px[y * size + x] = ((DWORD)R8 << 16) | ((DWORD)G8 << 8) | (DWORD)B8;
            }
        }
    BitBlt(dc, ox, oy, size, size, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

static void KMarker(HDC dc, float x, float y, float rad)
{
    UCircleLine(dc, x, y, rad + 1.0f, RGB(255, 255, 255), 230, 1.5f);
    UCircleLine(dc, x, y, rad, RGB(0, 0, 0), 255, 2.2f);
}

static void KPaint(HDC dc, int W, int H)
{
    int i, x, y, tw = k_tx1 - k_tx0;
    static const WCHAR *lbl[3] = { L"R", L"G", L"B" };
    static const COLORREF lcol[3] = { RGB(240, 80, 80), RGB(70, 200, 110), RGB(90, 140, 255) };
    float ax, ay, bx, by, cx, cy, wa, wb, wc;
    ZClear(&k_z);
    UFill(dc, 0, 0, W, H, T.surf);

    /* cor original (atrás) e atual (na frente) */
    URound(dc, KSZ(32), KSZ(32), KSZ(44), KSZ(44), KSZ(8), k_orig, 255);
    URoundLine(dc, KSZ(32), KSZ(32), KSZ(44), KSZ(44), KSZ(8), T.border, 255, 1.0f);
    ZAdd(&k_z, KSZ(32), KSZ(32), KSZ(44), KSZ(44), KZ_ORIG, 0);
    URound(dc, KSZ(16), KSZ(16), KSZ(44), KSZ(44), KSZ(8), k_rgb, 255);
    URoundLine(dc, KSZ(16), KSZ(16), KSZ(44), KSZ(44), KSZ(8), T.dark ? RGB(255, 255, 255) : T.text3, 140, 1.4f);
    ZAdd(&k_z, KSZ(16), KSZ(16), KSZ(44), KSZ(44), KZ_FRONT, 0);
    if (k_hotA == KZ_ORIG || k_hotA == KZ_FRONT)
        UText(dc, k_f.sm, L"clique atrás para voltar", KSZ(12), KSZ(80), KSZ(90), KSZ(34), T.text3,
              DT_WORDBREAK | DT_CENTER);

    /* RGB */
    for (i = 0; i < 3; i++) {
        int v = i == 0 ? GetRValue(k_rgb) : i == 1 ? GetGValue(k_rgb) : GetBValue(k_rgb), ty = k_rowY[i], kx;
        UText(dc, k_f.smB, lbl[i], KSZ(92), ty - KSZ(12), KSZ(16), KSZ(24), lcol[i], UT_C);
        for (x = 0; x <= tw; x++) {
            int c = x * 255 / (tw > 0 ? tw : 1);
            COLORREF col = i == 0 ? RGB(c, GetGValue(k_rgb), GetBValue(k_rgb))
                         : i == 1 ? RGB(GetRValue(k_rgb), c, GetBValue(k_rgb))
                                  : RGB(GetRValue(k_rgb), GetGValue(k_rgb), c);
            UFill(dc, k_tx0 + x, ty - KSZ(2), 1, KSZ(4), col);
        }
        kx = k_tx0 + v * tw / 255;
        UCircle(dc, (float)kx, (float)ty, (float)KSZ(8), T.surf, 255);
        UCircleLine(dc, (float)kx, (float)ty, (float)KSZ(7), T.text, 255, 1.6f);
        ZAdd(&k_z, k_tx0 - KSZ(10), ty - KSZ(14), tw + KSZ(20), KSZ(28), KZ_SLR + i, 0);
        /* caixa do número */
        URound(dc, W - KSZ(76), ty - KSZ(15), KSZ(60), KSZ(30), KSZ(7), T.field, 255);
        URoundLine(dc, W - KSZ(76), ty - KSZ(15), KSZ(60), KSZ(30), KSZ(7),
                   GetFocus() == k_ed[i] ? T.accent : T.border, 255, 1.0f);
    }
    /* HEX */
    y = k_rowY[2] + KSZ(36);
    UText(dc, k_f.cap, L"HEX", KSZ(84), y - KSZ(12), KSZ(28), KSZ(24), T.text3, UT_C);
    URound(dc, k_tx0, y - KSZ(15), KSZ(112), KSZ(30), KSZ(7), T.field, 255);
    URoundLine(dc, k_tx0, y - KSZ(15), KSZ(112), KSZ(30), KSZ(7), GetFocus() == k_ed[3] ? T.accent : T.border, 255, 1.0f);
    UText(dc, k_f.sm, L"#", k_tx0 + KSZ(8), y - KSZ(15), KSZ(14), KSZ(30), T.text3, UT_L);

    /* anel + triângulo */
    KDrawWheel(dc);
    KMarker(dc, (float)k_cx + ((k_R + k_r) / 2.0f) * cosf(k_h * K_PI / 180.0f),
            (float)k_cy - ((k_R + k_r) / 2.0f) * sinf(k_h * K_PI / 180.0f), (float)(k_R - k_r) / 2.0f - 1.5f);
    TriVerts(&ax, &ay, &bx, &by, &cx, &cy);
    wa = k_s * k_v; wb = k_v - wa; wc = 1.0f - k_v;
    KMarker(dc, wa * ax + wb * bx + wc * cx, wa * ay + wb * by + wc * cy, (float)KSZ(7));

    /* cores salvas */
    y = k_cy + k_R + KSZ(18);
    UText(dc, k_f.cap, L"SALVAS", KSZ(16), y, KSZ(120), KSZ(18), T.text3, UT_L);
    y += KSZ(24);
    x = KSZ(16);
    for (i = 0; i < g_cfg.nSaved && i < 8; i++) {
        URound(dc, x, y, KSZ(28), KSZ(28), KSZ(7), g_cfg.saved[i], 255);
        URoundLine(dc, x, y, KSZ(28), KSZ(28), KSZ(7), k_hotA == KZ_SAVED && k_hotG == i ? T.accent : T.border, 255,
                   k_hotA == KZ_SAVED && k_hotG == i ? 2.0f : 1.0f);
        ZAdd(&k_z, x, y, KSZ(28), KSZ(28), KZ_SAVED, i);
        x += KSZ(34);
    }
    if (k_hotA == KZ_ADD) URound(dc, x, y, KSZ(28), KSZ(28), KSZ(7), T.surf2, 255);
    URoundLine(dc, x, y, KSZ(28), KSZ(28), KSZ(7), T.border, 255, 1.0f);
    UIcon(dc, k_f.iconSm, IC_ADD, x + KSZ(14), y + KSZ(14), T.text2);
    ZAdd(&k_z, x, y, KSZ(28), KSZ(28), KZ_ADD, 0);

    /* rodapé */
    UFill(dc, KSZ(14), H - KSZ(62), W - KSZ(28), 1, T.border);
    UText(dc, k_f.bold, k_title, KSZ(16), H - KSZ(52), W - KSZ(250), KSZ(38), T.text2, UT_L);
    UButton(dc, &k_f, W - KSZ(16) - KSZ(100), H - KSZ(52), KSZ(100), KSZ(38), L"Aplicar", 0, 0, k_hotA == KZ_OK);
    ZAdd(&k_z, W - KSZ(16) - KSZ(100), H - KSZ(52), KSZ(100), KSZ(38), KZ_OK, 0);
    UButton(dc, &k_f, W - KSZ(24) - KSZ(200), H - KSZ(52), KSZ(100), KSZ(38), L"Cancelar", 0, 1, k_hotA == KZ_CANCEL);
    ZAdd(&k_z, W - KSZ(24) - KSZ(200), H - KSZ(52), KSZ(100), KSZ(38), KZ_CANCEL, 0);
    PopupBorder(dc, W, H);
}

static void KLayout(void)
{
    RECT c;
    int W, i, lh;
    if (!k_hwnd) return;
    GetClientRect(k_hwnd, &c);
    W = c.right;
    k_tx0 = KSZ(116);
    k_tx1 = W - KSZ(96);
    for (i = 0; i < 3; i++) k_rowY[i] = KSZ(28) + i * KSZ(36);
    k_R = (W - KSZ(48)) / 2;
    k_r = k_R - KSZ(24);
    k_cx = W / 2;
    k_cy = k_rowY[2] + KSZ(36) + KSZ(30) + k_R;
    lh = k_f.lhBase;
    for (i = 0; i < 3; i++)
        if (k_ed[i]) MoveWindow(k_ed[i], W - KSZ(72), k_rowY[i] - lh / 2, KSZ(52), lh, TRUE);
    if (k_ed[3]) MoveWindow(k_ed[3], k_tx0 + KSZ(22), k_rowY[2] + KSZ(36) - lh / 2, KSZ(84), lh, TRUE);
}

/* Atualiza os campos de texto (menos o que está sendo digitado) e avisa a prévia. */
static void KChanged(int skipId)
{
    WCHAR t[16];
    int i;
    k_loading = TRUE;
    for (i = 0; i < 3; i++) {
        if (IDC_KR + i == skipId) continue;
        wsprintfW(t, L"%d", i == 0 ? GetRValue(k_rgb) : i == 1 ? GetGValue(k_rgb) : GetBValue(k_rgb));
        SetWindowTextW(k_ed[i], t);
    }
    if (skipId != IDC_KHEX) {
        wsprintfW(t, L"%02X%02X%02X", GetRValue(k_rgb), GetGValue(k_rgb), GetBValue(k_rgb));
        SetWindowTextW(k_ed[3], t);
    }
    k_loading = FALSE;
    InvalidateRect(k_hwnd, NULL, FALSE);
    if (k_cb) k_cb(k_ctx, k_rgb, 0);
}

static void KSetRgb(COLORREF c, int skipId)
{
    k_rgb = c;
    RgbToHsv(c, &k_h, &k_s, &k_v);
    KChanged(skipId);
}

static void KSetHsv(void)
{
    k_rgb = HsvToRgb(k_h, k_s, k_v);
    KChanged(0);
}

static void KDragTo(int x, int y)
{
    float dx = (float)(x - k_cx), dy = (float)(y - k_cy);
    if (k_drag == KZ_RING) {
        float hue = atan2f(-dy, dx) * 180.0f / K_PI;
        if (hue < 0.0f) hue += 360.0f;
        k_h = hue;
        KSetHsv();
    } else if (k_drag == KZ_TRI) {
        float ax, ay, bx, by, cx, cy, den, wa, wb, wc, sum, fx = (float)x, fy = (float)y;
        TriVerts(&ax, &ay, &bx, &by, &cx, &cy);
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        wa = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / den;
        wb = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / den;
        wc = 1.0f - wa - wb;
        wa = Cl01(wa); wb = Cl01(wb); wc = Cl01(wc);
        sum = wa + wb + wc;
        if (sum <= 0.0f) return;
        wa /= sum; wb /= sum;
        k_v = Cl01(wa + wb);
        k_s = k_v > 0.0001f ? Cl01(wa / k_v) : 0.0f;
        KSetHsv();
    } else if (k_drag >= KZ_SLR && k_drag <= KZ_SLB) {
        int tw = k_tx1 - k_tx0, v = tw > 0 ? (x - k_tx0) * 255 / tw : 0;
        int r = GetRValue(k_rgb), g = GetGValue(k_rgb), b = GetBValue(k_rgb);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        if (k_drag == KZ_SLR) r = v; else if (k_drag == KZ_SLG) g = v; else b = v;
        KSetRgb(RGB(r, g, b), 0);
    }
}

static void KFinish(BOOL apply)
{
    ColorCb cb = k_cb;
    void *ctx = k_ctx;
    COLORREF c = apply ? k_rgb : k_orig;
    k_done = TRUE;
    k_cb = NULL;
    if (k_hwnd) DestroyWindow(k_hwnd);
    if (cb) cb(ctx, c, apply ? 1 : 2);
}

static void KSaveCurrent(void)
{
    int i;
    for (i = 0; i < g_cfg.nSaved; i++) if (g_cfg.saved[i] == k_rgb) return;
    if (g_cfg.nSaved < 8) g_cfg.saved[g_cfg.nSaved++] = k_rgb;
    else { MoveMemory(&g_cfg.saved[0], &g_cfg.saved[1], 7 * sizeof(COLORREF)); g_cfg.saved[7] = k_rgb; }
    SaveConfig();
    InvalidateRect(k_hwnd, NULL, FALSE);
}

static void KEditChanged(int id)
{
    WCHAR t[16];
    int i, v = 0;
    if (k_loading) return;
    EditText(GetDlgItem(k_hwnd, id), t, 16);
    if (id == IDC_KHEX) {
        WCHAR *p = t[0] == L'#' ? t + 1 : t;
        if (lstrlenW(p) != 6) return;
        for (i = 0; i < 6; i++) { int hv = HexVal(p[i]); if (hv < 0) return; v = v * 16 + hv; }
        KSetRgb(RGB((v >> 16) & 255, (v >> 8) & 255, v & 255), id);
    } else {
        int r = GetRValue(k_rgb), g = GetGValue(k_rgb), b = GetBValue(k_rgb);
        if (!t[0]) return;
        for (i = 0; t[i]; i++) { if (t[i] < L'0' || t[i] > L'9') return; v = v * 10 + (t[i] - L'0'); }
        if (v > 255) v = 255;
        if (id == IDC_KR) r = v; else if (id == IDC_KG) g = v; else b = v;
        KSetRgb(RGB(r, g, b), id);
    }
}

static BOOL KInWheel(int x, int y, int *which)
{
    float dx = (float)(x - k_cx), dy = (float)(y - k_cy), d = sqrtf(dx * dx + dy * dy);
    if (d >= (float)k_r - 2.0f && d <= (float)k_R + 6.0f) { *which = KZ_RING; return TRUE; }
    if (d < (float)k_r - 2.0f) { *which = KZ_TRI; return TRUE; }
    return FALSE;
}

static LRESULT CALLBACK ColorProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: {
        int i;
        k_hwnd = h;
        UiInit();
        UiFontsMake(&k_f, GetDpiForWindow(h));
        k_brField = CreateSolidBrush(T.field);
        for (i = 0; i < 4; i++) {
            k_ed[i] = UiEdit(h, IDC_KR + i, WS_VISIBLE | ES_AUTOHSCROLL | (i < 3 ? ES_CENTER : 0), k_f.base, NULL);
            SendMessageW(k_ed[i], EM_LIMITTEXT, i < 3 ? 3 : 7, 0);
        }
        UiStyleWindow(h, TRUE);
        KLayout();
        return 0;
    }
    case WM_SIZE: KLayout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&k_bb, wdc, c.right, c.bottom);
        KPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, T.text);
        SetBkColor((HDC)w, T.field);
        return (LRESULT)k_brField;
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE) KEditChanged(LOWORD(w));
        else if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) {
            if (HIWORD(w) == EN_KILLFOCUS) KChanged(0);   /* normaliza o que foi digitado */
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_EDITKEY:
        if (l == VK_ESCAPE) KFinish(FALSE);
        else if (l == VK_RETURN || l == 0x10D) KFinish(TRUE);
        return 0;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r == HTCLIENT) {
            POINT pt;
            int which;
            pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
            ScreenToClient(h, &pt);
            if (!ZHit(&k_z, pt.x, pt.y) && !KInWheel(pt.x, pt.y, &which)) return HTCAPTION; /* arrastar a janela */
        }
        return r;
    }
    case WM_MOUSEMOVE:
        if (k_drag) KDragTo(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        else PopupHover(h, &k_z, &k_hotA, &k_hotG, &k_tracking, l);
        return 0;
    case WM_MOUSELEAVE: k_tracking = FALSE; k_hotA = k_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: {
        POINT pt;
        int which;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (LOWORD(l) == HTCLIENT && (PopupCursor(h, &k_z) || KInWheel(pt.x, pt.y, &which))) {
            if (KInWheel(pt.x, pt.y, &which)) SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32515))); /* cruz */
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l), which;
        Zone *z = ZHit(&k_z, x, y);
        SetFocus(h);
        k_downA = z ? z->act : 0; k_downG = z ? z->arg : 0;
        if (z && z->act >= KZ_SLR && z->act <= KZ_SLB) k_drag = z->act;
        else if (!z && KInWheel(x, y, &which)) k_drag = which;
        if (k_drag) { SetCapture(h); KDragTo(x, y); }
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&k_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (k_drag) { k_drag = 0; ReleaseCapture(); return 0; }
        if (z && z->act == k_downA && z->arg == k_downG) {
            switch (z->act) {
            case KZ_ORIG:   KSetRgb(k_orig, 0); break;
            case KZ_SAVED:  if (z->arg < g_cfg.nSaved) KSetRgb(g_cfg.saved[z->arg], 0); break;
            case KZ_ADD:    KSaveCurrent(); break;
            case KZ_OK:     KFinish(TRUE); break;
            case KZ_CANCEL: KFinish(FALSE); break;
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {   /* clique direito numa cor salva: remove */
        Zone *z = ZHit(&k_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == KZ_SAVED && z->arg < g_cfg.nSaved) {
            MoveMemory(&g_cfg.saved[z->arg], &g_cfg.saved[z->arg + 1], (SIZE_T)(g_cfg.nSaved - z->arg - 1) * sizeof(COLORREF));
            g_cfg.nSaved--;
            SaveConfig();
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: k_drag = 0; return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) KFinish(FALSE);
        else if (w == VK_RETURN) KFinish(TRUE);
        return 0;
    case WM_CLOSE: KFinish(FALSE); return 0;
    case WM_DESTROY: {
        ColorCb cb = k_cb;
        void *ctx = k_ctx;
        BOOL done = k_done;
        COLORREF orig = k_orig;
        HWND owner = k_owner;
        UiFontsFree(&k_f); ZFree(&k_z); BBFree(&k_bb);
        if (k_brField) DeleteObject(k_brField);
        k_brField = NULL;
        UiDone();
        k_hwnd = NULL; k_owner = NULL; k_cb = NULL; k_drag = 0;
        ZeroMemory(k_ed, sizeof k_ed);
        if (!done && cb) cb(ctx, orig, 2);    /* fechada por fora: cancela */
        if (owner && IsWindow(owner)) SetForegroundWindow(owner);
        ScheduleTrim();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void ColorOpen(HWND owner, COLORREF initial, const WCHAR *title, ColorCb cb, void *ctx)
{
    UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    int w = MulDiv(352, (int)dpi, 96), hgt = MulDiv(640, (int)dpi, 96), x, y;
    RECT wa;
    POINT pt;
    if (k_hwnd) KFinish(FALSE);
    UiLoadTheme();
    k_owner = owner; k_cb = cb; k_ctx = ctx; k_orig = initial; k_rgb = initial; k_done = FALSE;
    k_h = 0.0f; k_s = 0.0f; k_v = 1.0f;
    RgbToHsv(initial, &k_h, &k_s, &k_v);
    lstrcpynW(k_title, title ? title : L"Cor", 48);
    k_hotA = k_hotG = k_drag = 0;
    if (owner) {
        RECT r;
        GetWindowRect(owner, &r);
        pt.x = (r.left + r.right) / 2; pt.y = (r.top + r.bottom) / 2;
    } else GetCursorPos(&pt);
    WorkAreaAt(pt, &wa);
    if (hgt > wa.bottom - wa.top) hgt = wa.bottom - wa.top;
    x = pt.x - w / 2; y = pt.y - hgt / 2;
    if (x + w > wa.right) x = wa.right - w;
    if (y + hgt > wa.bottom) y = wa.bottom - hgt;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_COLOR, title ? title : L"Cor", WS_POPUP, x, y, w, hgt,
                    owner, NULL, g_inst, NULL);
    if (!k_hwnd) return;
    KChanged(0);
    ShowWindow(k_hwnd, SW_SHOW);
    SetForegroundWindow(k_hwnd);
}

/* ======================================================================== */
/* Seletor de dias da semana (repetição)                                    */
/* ======================================================================== */

enum { DZ_DAY = 1, DZ_WORK, DZ_WEEKEND, DZ_ALL, DZ_OK, DZ_CANCEL };

static HWND    d_hwnd, d_owner;
static DaysCb  d_cb;
static void   *d_ctx;
static int     d_mask, d_hotA, d_hotG, d_downA, d_downG;
static UiFonts d_f;
static Zones   d_z;
static BackBuf d_bb;
static BOOL    d_tracking, d_done;
#define DSZ(x) MulDiv((x), (int)d_f.dpi, 96)

static void DaysPaint(HDC dc, int W, int H)
{
    static const int order[7] = { 1, 2, 3, 4, 5, 6, 0 };   /* seg … dom */
    static const struct { const WCHAR *t; int act, mask; } pre[3] = {
        { L"Dias úteis", DZ_WORK, 0x3E }, { L"Fim de semana", DZ_WEEKEND, 0x41 }, { L"Todos", DZ_ALL, WD_ALL } };
    int i, d = DSZ(38), gap = (W - DSZ(32) - 7 * d) / 6, x, y = DSZ(56), bw, cx;
    WCHAR lbl[64];
    ZClear(&d_z);
    UFill(dc, 0, 0, W, H, T.surf);
    UText(dc, d_f.h2, L"Repetir nos dias", DSZ(16), DSZ(12), W - DSZ(32), DSZ(32), T.text, UT_L);

    for (i = 0; i < 7; i++) {
        int wd = order[i];
        BOOL on = (d_mask & (1 << wd)) != 0, hot = d_hotA == DZ_DAY && d_hotG == wd;
        float r = d / 2.0f;
        x = DSZ(16) + i * (d + gap);
        if (on) UCircle(dc, x + r, y + r, r, hot ? T.primaryHi : T.primary, 255);
        else {
            UCircle(dc, x + r, y + r, r, hot ? Mix(T.surf2, T.text, 22) : T.surf2, 255);
            UCircleLine(dc, x + r, y + r, r - 0.5f, T.border, 255, 1.0f);
        }
        UText(dc, on ? d_f.smB : d_f.sm, kWdShort[wd], x, y, d, d, on ? T.onPrimary : T.text, UT_C);
        ZAdd(&d_z, x, y, d, d, DZ_DAY, wd);
    }

    /* atalhos */
    y += d + DSZ(16);
    cx = DSZ(16);
    for (i = 0; i < 3; i++) {
        BOOL sel = (d_mask & WD_ALL) == pre[i].mask;
        int w = UChip(dc, &d_f, cx, y, DSZ(28), 0, 0, FALSE, pre[i].t, sel, d_hotA == pre[i].act, FALSE);
        ZAdd(&d_z, cx, y, w, DSZ(28), pre[i].act, 0);
        cx += w + DSZ(8);
    }

    /* resumo + botões */
    DaysLabel(d_mask, lbl, 64);
    UFill(dc, DSZ(14), H - DSZ(58), W - DSZ(28), 1, T.border);
    UText(dc, d_f.sm, lbl, DSZ(16), H - DSZ(50), W - DSZ(230), DSZ(36), d_mask ? T.text2 : T.danger, UT_L);
    bw = DSZ(96);
    UButton(dc, &d_f, W - DSZ(16) - bw, H - DSZ(48), bw, DSZ(36), L"OK", 0, 0, d_mask && d_hotA == DZ_OK);
    if (d_mask) ZAdd(&d_z, W - DSZ(16) - bw, H - DSZ(48), bw, DSZ(36), DZ_OK, 0);
    UButton(dc, &d_f, W - DSZ(24) - bw * 2, H - DSZ(48), bw, DSZ(36), L"Cancelar", 0, 1, d_hotA == DZ_CANCEL);
    ZAdd(&d_z, W - DSZ(24) - bw * 2, H - DSZ(48), bw, DSZ(36), DZ_CANCEL, 0);
    PopupBorder(dc, W, H);
}

static void DaysFinish(BOOL ok)
{
    DaysCb cb = d_cb;
    void *ctx = d_ctx;
    int mask = d_mask & WD_ALL;
    d_done = TRUE;
    d_cb = NULL;
    if (d_hwnd) DestroyWindow(d_hwnd);
    if (cb) cb(ctx, ok && mask ? mask : -1);
}

static void DaysAct(int a, int g)
{
    switch (a) {
    case DZ_DAY:     d_mask ^= 1 << g; break;
    case DZ_WORK:    d_mask = 0x3E; break;
    case DZ_WEEKEND: d_mask = 0x41; break;
    case DZ_ALL:     d_mask = WD_ALL; break;
    case DZ_OK:      DaysFinish(TRUE); return;
    case DZ_CANCEL:  DaysFinish(FALSE); return;
    }
    InvalidateRect(d_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK DaysProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        d_hwnd = h;
        UiInit();
        UiFontsMake(&d_f, GetDpiForWindow(h));
        UiStyleWindow(h, TRUE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        HDC wdc = BeginPaint(h, &ps), dc;
        GetClientRect(h, &c);
        dc = BBBegin(&d_bb, wdc, c.right, c.bottom);
        DaysPaint(dc, c.right, c.bottom);
        BitBlt(wdc, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: PopupHover(h, &d_z, &d_hotA, &d_hotG, &d_tracking, l); return 0;
    case WM_MOUSELEAVE: d_tracking = FALSE; d_hotA = d_hotG = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && PopupCursor(h, &d_z)) return TRUE; break;
    case WM_LBUTTONDOWN: {
        Zone *z = ZHit(&d_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        d_downA = z ? z->act : 0; d_downG = z ? z->arg : 0;
        return 0;
    }
    case WM_LBUTTONUP: {
        Zone *z = ZHit(&d_z, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (z && z->act == d_downA && z->arg == d_downG) DaysAct(z->act, z->arg);
        return 0;
    }
    case WM_CHAR:   /* 1 = seg … 7 = dom */
        if (w >= L'1' && w <= L'7') DaysAct(DZ_DAY, (int)(w - L'0') % 7);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DaysFinish(FALSE);
        else if (w == VK_RETURN && (d_mask & WD_ALL)) DaysFinish(TRUE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE: DaysFinish(FALSE); return 0;
    case WM_DESTROY: {
        DaysCb cb = d_cb;
        void *ctx = d_ctx;
        BOOL done = d_done;
        HWND owner = d_owner;
        UiFontsFree(&d_f); ZFree(&d_z); BBFree(&d_bb);
        UiDone();
        d_hwnd = NULL; d_owner = NULL; d_cb = NULL;
        if (!done && cb) cb(ctx, -1);
        if (owner && IsWindow(owner)) SetForegroundWindow(owner);
        QCalClosed();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void DaysOpen(HWND owner, int x, int y, int mask, DaysCb cb, void *ctx)
{
    UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    int w = MulDiv(372, (int)dpi, 96), hgt = MulDiv(226, (int)dpi, 96);
    RECT wa;
    POINT pt;
    if (d_hwnd) DaysFinish(FALSE);
    d_owner = owner; d_cb = cb; d_ctx = ctx; d_done = FALSE;
    d_mask = mask & WD_ALL;
    d_hotA = d_hotG = 0;
    pt.x = x; pt.y = y;
    WorkAreaAt(pt, &wa);
    if (x + w > wa.right) x = wa.right - w;
    if (y + hgt > wa.bottom) y = y - hgt - MulDiv(40, (int)dpi, 96);
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_DAYS, L"Dias da semana", WS_POPUP, x, y, w, hgt,
                    owner, NULL, g_inst, NULL);
    if (!d_hwnd) return;
    ShowWindow(d_hwnd, SW_SHOW);
    SetForegroundWindow(d_hwnd);
}
