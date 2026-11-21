// geo_win.hpp - гео-модуль FLEY AI 1.2: анимированный глобус + настоящая карта OpenStreetMap.
// Подключается из main.cpp после функций рисования HUD (Txt, Panel, Brackets) и до DrawHud.
// Поиск места: Nominatim (OpenStreetMap) по адресу, названию или координатам. Ключ не нужен.
#pragma once

// Суша 3x3 градуса (60 строк с севера на юг, по 120 клеток, 4 клетки на шестнадцатеричную цифру)
static const char* kLandHex[60] = {
    "000000000000000000000000000000",
    "000000000000000000000000000000",
    "00000000001FE00000000000000000",
    "000000FFE7FFFC0070000000000000",
    "0000001FC3FFFC000000000C000000",
    "00000000007FF800000381FFF00000",
    "01E007FFF87FF80008000FFFFFFF00",
    "07FFFFFFFE3F80007F87FFFFFFFFFF",
    "03FFFFFE2E1F0E01EFFFFFFFFFFFFE",
    "07FFFFF8201E0003DFFFFFFFFFFFF8",
    "0181FFF83E000023CFFFFFFFFFE040",
    "0000FFFE3F0000201FFFFFFFFFC0C0",
    "00007FFFFF80007BFFFFFFFFFFF080",
    "00003FFFFF00000FFFFFFFFFFFF000",
    "00001FFFFC00001FFFFFFFFFFFC000",
    "00001FFFF800000C3877FFFFFF9000",
    "00001FFFF00000705FF7FFFFFE0000",
    "00001FFFE000007017FBFFFFF22000",
    "00000FFFC000003F00FFFFFFF08000",
    "000007FF8000007FEDFFFFFFF10000",
    "000001F0800000FFFEF7FFFFF80000",
    "000000F0000001FFFFF8FFFFF00000",
    "00000070800001FFFF7F1FFFC00000",
    "00000072000003FFFF7E0F1E000000",
    "0000001E000003FFFFB80E0F080000",
    "00000001000003FFFFE00607080000",
    "000000009E0001FFFFF80601000000",
    "000000007F8000FFFFF00004000000",
    "000000003FE00041FFF00004300000",
    "000000003FE00001FFE00004E00000",
    "000000007FF80001FFC00006680000",
    "000000007FFE0000FF800002087000",
    "000000007FFF0000FF800001C03800",
    "000000003FFF0000FF800000000000",
    "000000001FFE0000FF88000001D000",
    "000000001FFE0000FF98000007F800",
    "000000000FFE0000FF1000000FF800",
    "000000000FFC00007F1000003FFC00",
    "000000000FF000007E0000003FFE00",
    "000000000FF000007E0000003FFE00",
    "000000000FE000003C0000003FFE00",
    "000000000F80000000000000107C00",
    "000000000F80000000000000001802",
    "000000001E00000000000000000004",
    "000000001C00000000000000000008",
    "000000001E00000000000000000000",
    "000000001C00000000000000000000",
    "000000000800000000000000000000",
    "000000000000000000000000000000",
    "000000000000000000000000000000",
    "000000000000000000000000000000",
    "000000000100000000000000000000",
    "00000000038000000003FFFFFF8000",
    "000000001FF0003FFFFFFFFFFFFF80",
    "FFFE03FFFFFFFFFFFFFFFFFFFFFFFF",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
};
static bool LandAt(int row, int col) {
    char c = kLandHex[row][col >> 2];
    int v = (c >= 'A') ? (c - 'A' + 10) : (c - '0');
    return ((v >> (3 - (col & 3))) & 1) != 0;
}

struct GeoState {
    std::mutex mx;
    int phase = 0;      // 0 выключено, 1 идёт поиск, 2 найдено, 3 не найдено
    int mapState = 0;   // 0 грузится, 1 готова, 2 недоступна
    ULONGLONG tStart = 0, tFound = 0, tMap = 0, tFail = 0;
    double lat = 0, lon = 0, startLam = 0, startPhi = 0;
    int zoom = 16, gen = 0;
    float mkx = 0.5f, mky = 0.5f;  // положение точки на карте (доли 0..1)
    std::string query, name, errText, spoken;
    Gdiplus::Bitmap* map = nullptr;  // 3x3 тайла по 256 px
};
static GeoState g_geo;

// ---- сеть
struct GeoHttp {
    HINTERNET ses = nullptr, con = nullptr;
    explicit GeoHttp(const wchar_t* host) {
        ses = WinHttpOpen(L"FLEY-AI/1.2 (desktop voice assistant)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (ses) {
            WinHttpSetTimeouts(ses, 6000, 6000, 8000, 12000);
            con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        }
    }
    ~GeoHttp() {
        if (con) WinHttpCloseHandle(con);
        if (ses) WinHttpCloseHandle(ses);
    }
    bool Get(const std::wstring& path, std::string& out) {
        out.clear();
        if (!con) return false;
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req) return false;
        bool ok = false;
        const wchar_t* hdr = L"Accept-Language: ru,en;q=0.8\r\n";
        if (WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail && out.size() < 4000000) {
                std::string chunk(avail, '\0');
                DWORD n = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &n) || n == 0) break;
                out.append(chunk.data(), n);
            }
            ok = code >= 200 && code < 300;
        }
        WinHttpCloseHandle(req);
        return ok;
    }
};

// Адрес/название/координаты -> точка. Реальные данные OpenStreetMap, ничего не выдумываем.
static bool GeoLookup(const std::string& q, GeoHit& hit, std::string& err) {
    double lat = 0, lon = 0;
    GeoHttp http(L"nominatim.openstreetmap.org");
    std::string body;
    if (geo_parse_coords(q, lat, lon)) {
        hit = GeoHit();
        hit.ok = true; hit.lat = lat; hit.lon = lon;
        char buf[200];
        std::snprintf(buf, sizeof buf, "/reverse?format=jsonv2&zoom=18&accept-language=ru&lat=%.7f&lon=%.7f", lat, lon);
        if (http.Get(Widen(buf), body)) {
            GeoHit r;
            if (geo_parse_nominatim(body, r)) hit.name = r.name;
        }
        if (hit.name.empty()) hit.name = "Точка на карте (адреса в этом месте нет): " + geo_fmt_coord(lat, true) + ", " + geo_fmt_coord(lon, false);
        return true;
    }
    std::string path = "/search?format=jsonv2&limit=1&accept-language=ru&q=" + url_encode(q);
    if (!http.Get(Widen(path), body)) { err = "Нет связи с сервисом карт OpenStreetMap."; return false; }
    if (!geo_parse_nominatim(body, hit)) { err = "Такого места нет в базе OpenStreetMap."; return false; }
    return true;
}

// ---- загрузка тайлов карты (3x3) и сборка картинки
struct GeoMapArg { int gen; double lat, lon; int z; };
static DWORD WINAPI GeoMapThread(LPVOID p) {
    GeoMapArg a = *(GeoMapArg*)p;
    delete (GeoMapArg*)p;
    using namespace Gdiplus;
    const int T = 256, N = 3;
    double tx = 0, ty = 0;
    geo_tile(a.lat, a.lon, a.z, tx, ty);
    int n = 1 << a.z, cx = (int)std::floor(tx), cy = (int)std::floor(ty), okc = 0;
    bool aborted = false;
    Bitmap* raw = new Bitmap(T * N, T * N, PixelFormat32bppARGB);
    {
        Graphics gg(raw);
        gg.Clear(Color(255, 235, 235, 230));
        GeoHttp http(L"tile.openstreetmap.org");
        for (int dy = -1; dy <= 1 && !aborted; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                {
                    std::lock_guard<std::mutex> lk(g_geo.mx);
                    if (g_geo.gen != a.gen) { aborted = true; break; }
                }
                int y = cy + dy;
                if (y < 0 || y >= n) continue;
                int x = ((cx + dx) % n + n) % n;
                std::wstring path = L"/" + std::to_wstring(a.z) + L"/" + std::to_wstring(x) + L"/" + std::to_wstring(y) + L".png";
                std::string png;
                if (!http.Get(path, png) || png.size() < 100) continue;
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, png.size());
                if (!hg) continue;
                void* mem = GlobalLock(hg);
                if (mem) std::memcpy(mem, png.data(), png.size());
                GlobalUnlock(hg);
                IStream* st = nullptr;
                if (SUCCEEDED(CreateStreamOnHGlobal(hg, TRUE, &st)) && st) {
                    Bitmap* tb = Bitmap::FromStream(st);
                    if (tb && tb->GetLastStatus() == Gdiplus::Ok) {
                        gg.DrawImage(tb, (dx + 1) * T, (dy + 1) * T, T, T);
                        ++okc;
                    }
                    delete tb;
                    st->Release();
                } else {
                    GlobalFree(hg);
                }
            }
        }
    }
    if (aborted) { delete raw; return 0; }
    if (okc == 0) {
        delete raw;
        std::lock_guard<std::mutex> lk(g_geo.mx);
        if (g_geo.gen == a.gen) g_geo.mapState = 2;
        return 0;
    }
    // стиль HUD: инверсия светлой карты OSM в тёмно-синюю, подписи светятся
    Bitmap* tinted = new Bitmap(T * N, T * N, PixelFormat32bppARGB);
    {
        Graphics g2(tinted);
        ColorMatrix cm = {{{-0.45f, 0, 0, 0, 0}, {0, -0.75f, 0, 0, 0}, {0, 0, -0.95f, 0, 0}, {0, 0, 0, 1, 0}, {0.50f, 0.85f, 1.10f, 0, 1}}};
        ImageAttributes ia;
        ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        g2.DrawImage(raw, Rect(0, 0, T * N, T * N), 0, 0, T * N, T * N, UnitPixel, &ia);
    }
    delete raw;
    std::lock_guard<std::mutex> lk(g_geo.mx);
    if (g_geo.gen != a.gen) { delete tinted; return 0; }
    delete g_geo.map;
    g_geo.map = tinted;
    g_geo.mkx = (float)((tx - (cx - 1)) / N);
    g_geo.mky = (float)((ty - (cy - 1)) / N);
    g_geo.mapState = 1;
    g_geo.tMap = GetTickCount64();
    return 0;
}

static void GeoClose() {
    std::lock_guard<std::mutex> lk(g_geo.mx);
    ++g_geo.gen;
    g_geo.phase = 0;
    delete g_geo.map;
    g_geo.map = nullptr;
}

static std::string GeoSpokenAnswer() {
    std::lock_guard<std::mutex> lk(g_geo.mx);
    return g_geo.spoken;
}

static ActResult ActGeoClose() {
    GeoClose();
    return Ok("Карта закрыта.");
}

// geo | запрос: запускает анимацию поиска, находит точку и грузит карту. Возвращает точный адрес и координаты.
static ActResult ActGeo(const Action& a) {
    std::string q = trim(a.args.empty() ? std::string() : a.args[0]);
    if (q.empty()) return Fail("Не указано, что искать на карте.");
    q = utf8_truncate(q, 200);
    MusicHide();  // плеер не показывается поверх карты; музыка при этом продолжает играть
    int gen;
    ULONGLONG t0;
    {
        std::lock_guard<std::mutex> lk(g_geo.mx);
        gen = ++g_geo.gen;
        delete g_geo.map;
        g_geo.map = nullptr;
        g_geo.phase = 1;
        g_geo.mapState = 0;
        g_geo.tStart = t0 = GetTickCount64();
        g_geo.tMap = 0;
        g_geo.query = q;
        g_geo.name.clear();
        g_geo.errText.clear();
        g_geo.spoken.clear();
    }
    GeoHit hit;
    std::string err;
    bool ok = GeoLookup(q, hit, err);
    ULONGLONG now = GetTickCount64();
    if (now - t0 < 1500) { Sleep((DWORD)(1500 - (now - t0))); now = GetTickCount64(); }  // чтобы анимация поиска успела показаться
    if (!ok) {
        std::lock_guard<std::mutex> lk(g_geo.mx);
        if (g_geo.gen == gen) { g_geo.phase = 3; g_geo.tFail = now; g_geo.errText = err; }
        return Fail("Не нашёл на карте «" + q + "». " + err, true);
    }
    int z = 16;
    double dummyLat = 0, dummyLon = 0;
    bool byCoords = geo_parse_coords(q, dummyLat, dummyLon);
    if (!byCoords && hit.hasBox) z = geo_zoom_for_span(hit.north - hit.south, hit.east - hit.west);
    char sp[64], lb[64], ob[64];
    std::snprintf(lb, sizeof lb, "%.5f", hit.lat);
    std::snprintf(ob, sizeof ob, "%.5f", hit.lon);
    std::snprintf(sp, sizeof sp, "%.4f, %.4f", hit.lat, hit.lon);
    for (char* c = sp; *c; ++c) if (*c == '.') *c = ',';
    {
        std::lock_guard<std::mutex> lk(g_geo.mx);
        if (g_geo.gen != gen) return Fail("Поиск на карте отменён.");
        double tt = (double)(now - g_geo.tStart) / 1000.0;
        g_geo.startLam = std::fmod(tt * 35.0, 360.0);
        g_geo.startPhi = 18.0 + 8.0 * std::sin(0.7 * tt);
        g_geo.lat = hit.lat; g_geo.lon = hit.lon; g_geo.zoom = z;
        g_geo.name = hit.name;
        g_geo.tFound = now;
        g_geo.phase = 2;
        g_geo.spoken = "Нашёл: " + geo_short_name(hit.name) + ". Координаты: " + sp + ".";
    }
    GeoMapArg* ma = new GeoMapArg{gen, hit.lat, hit.lon, z};
    HANDLE th = CreateThread(nullptr, 0, GeoMapThread, ma, 0, nullptr);
    if (th) CloseHandle(th); else delete ma;
    return Ok("Найдено на карте OpenStreetMap: " + hit.name + ". Широта " + lb + ", долгота " + ob + ". Карта показана на экране.", true);
}

// ---- рисование
static float Lerpf(float a, float b, float k) { return a + (b - a) * k; }
static float Sm01(double x) { x = std::max(0.0, std::min(1.0, x)); return (float)(x * x * (3.0 - 2.0 * x)); }

static Gdiplus::RectF GeoArea() {
    float s = L.s;
    float top = (L.mini ? 60.0f : 106.0f) * s;
    float bottom = L.cmd.Y - 12 * s;
    float m = (L.mini ? 10.0f : 34.0f) * s;
    return Gdiplus::RectF(m, top, L.W - 2 * m, std::max(60.0f * s, bottom - top));
}
static void GeoOpenIdle() {  // кнопка «ГЕО»: глобус вращается и ждёт название места
    std::lock_guard<std::mutex> lk(g_geo.mx);
    ++g_geo.gen;
    delete g_geo.map;
    g_geo.map = nullptr;
    g_geo.phase = 4;
    g_geo.mapState = 0;
    g_geo.tStart = GetTickCount64();
    g_geo.query.clear();
}
static bool GeoIsIdle() {
    std::lock_guard<std::mutex> lk(g_geo.mx);
    return g_geo.phase == 4;
}
static bool GeoActive() {
    std::lock_guard<std::mutex> lk(g_geo.mx);
    return g_geo.phase != 0;
}
static bool GeoOverlayHit(int, int y) {
    if (!GeoActive()) return false;
    Gdiplus::RectF A = GeoArea();
    return (float)y >= A.Y - 8 * L.s && (float)y < L.cmd.Y - 2 * L.s;
}

static std::vector<std::wstring> GeoWrap(const std::wstring& w, size_t maxc, size_t maxLines) {
    std::vector<std::wstring> out;
    std::wstring cur;
    size_t i = 0;
    while (i < w.size()) {
        size_t j = w.find(L", ", i);
        std::wstring part = (j == std::wstring::npos) ? w.substr(i) : w.substr(i, j - i + 1);
        i = (j == std::wstring::npos) ? w.size() : j + 2;
        if (!cur.empty() && cur.size() + 1 + part.size() > maxc) {
            out.push_back(cur);
            cur.clear();
            if (out.size() >= maxLines) return out;
        }
        cur += (cur.empty() ? std::wstring() : std::wstring(L" ")) + part;
    }
    if (!cur.empty() && out.size() < maxLines) out.push_back(cur);
    return out;
}

static double g_gLonS[120], g_gLonC[120], g_gLatS[60], g_gLatC[60];
static void GeoInitTables() {
    static bool done = false;
    if (done) return;
    done = true;
    const double D2R = 3.14159265358979323846 / 180.0;
    for (int i = 0; i < 120; ++i) { double lo = (-180.0 + 3.0 * (i + 0.5)) * D2R; g_gLonS[i] = std::sin(lo); g_gLonC[i] = std::cos(lo); }
    for (int j = 0; j < 60; ++j) { double la = (90.0 - 3.0 * (j + 0.5)) * D2R; g_gLatS[j] = std::sin(la); g_gLatC[j] = std::cos(la); }
}

// Глобус: ортографическая проекция, сетка, точки суши, подсветка по краю
static void GeoDrawGlobe(Gdiplus::Graphics& g, float cx, float cy, float R, double lam0deg, double phi0deg, RGBc col) {
    using namespace Gdiplus;
    float s = L.s;
    GeoInitTables();
    const double D2R = 3.14159265358979323846 / 180.0;
    double lam0 = lam0deg * D2R, phi0 = phi0deg * D2R;
    double sp0 = std::sin(phi0), cp0 = std::cos(phi0), sl0 = std::sin(lam0), cl0 = std::cos(lam0);
    for (int i = 4; i >= 1; --i) {
        Pen gp(Al(col, 12 * i), 3.0f * s);
        float rr = R + i * 3.0f * s;
        g.DrawEllipse(&gp, cx - rr, cy - rr, 2 * rr, 2 * rr);
    }
    {
        GraphicsPath path;
        path.AddEllipse(cx - R, cy - R, 2 * R, 2 * R);
        PathGradientBrush pb(&path);
        pb.SetCenterPoint(PointF(cx - R * 0.3f, cy - R * 0.3f));
        pb.SetCenterColor(Al({16, 78, 124}, 245));
        Color sur = Al({2, 12, 28}, 250);
        INT cnt = 1;
        pb.SetSurroundColors(&sur, &cnt);
        g.FillPath(&pb, &path);
    }
    auto proj = [&](double latd, double lond, float& x, float& y) -> bool {
        double la = latd * D2R, lo = lond * D2R, cl = std::cos(lo - lam0);
        double cosc = sp0 * std::sin(la) + cp0 * std::cos(la) * cl;
        x = cx + (float)(R * std::cos(la) * std::sin(lo - lam0));
        y = cy - (float)(R * (cp0 * std::sin(la) - sp0 * std::cos(la) * cl));
        return cosc > 0;
    };
    Pen grid(Al(col, 42), 1.0f);
    for (int lon = -180; lon < 180; lon += 30) {
        bool pv = false; float px = 0, py = 0;
        for (int lat = -90; lat <= 90; lat += 5) {
            float x, y;
            bool v = proj(lat, lon, x, y);
            if (v && pv) g.DrawLine(&grid, px, py, x, y);
            pv = v; px = x; py = y;
        }
    }
    for (int lat = -60; lat <= 60; lat += 30) {
        bool pv = false; float px = 0, py = 0;
        for (int lon = -180; lon <= 180; lon += 5) {
            float x, y;
            bool v = proj(lat, lon, x, y);
            if (v && pv) g.DrawLine(&grid, px, py, x, y);
            pv = v; px = x; py = y;
        }
    }
    SolidBrush* br[6];
    for (int i = 0; i < 6; ++i) br[i] = new SolidBrush(Al(Mix(col, C_WHITE, 0.15f + 0.1f * i), 70 + 31 * i));
    for (int j = 0; j < 60; ++j) {
        double sla = g_gLatS[j], cla = g_gLatC[j], a = sp0 * sla, b = cp0 * cla;
        for (int i = 0; i < 120; ++i) {
            if (!LandAt(j, i)) continue;
            double cl = g_gLonC[i] * cl0 + g_gLonS[i] * sl0;
            double cosc = a + b * cl;
            if (cosc <= 0.04) continue;
            double sd = g_gLonS[i] * cl0 - g_gLonC[i] * sl0;
            float x = cx + (float)(R * cla * sd);
            float y = cy - (float)(R * (cp0 * sla - sp0 * cla * cl));
            int bi = std::min(5, (int)(cosc * 6.0));
            float sz = std::max(1.5f * s, R * 0.0175f * (float)(0.55 + 0.45 * cosc));
            g.FillRectangle(br[bi], x - sz / 2, y - sz / 2, sz, sz);
        }
    }
    for (int i = 0; i < 6; ++i) delete br[i];
    Pen rim(Al(col, 210), 2.0f * s);
    g.DrawEllipse(&rim, cx - R, cy - R, 2 * R, 2 * R);
    Pen rim2(Al(col, 70), 1.0f);
    g.DrawEllipse(&rim2, cx - R * 0.965f, cy - R * 0.965f, 2 * R * 0.965f, 2 * R * 0.965f);
}

static void DrawGeo(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    std::lock_guard<std::mutex> lk(g_geo.mx);
    if (g_geo.phase == 0) return;
    ULONGLONG nowTick = GetTickCount64();
    if (g_geo.phase == 3 && nowTick - g_geo.tFail > 5500) { g_geo.phase = 0; return; }
    float s = L.s;
    double now = nowTick / 1000.0;
    bool found = g_geo.phase == 2, failed = g_geo.phase == 3, idle = g_geo.phase == 4;
    RectF A = GeoArea();
    SolidBrush dim(Al({2, 9, 20}, 240));
    g.FillRectangle(&dim, 0.0f, A.Y - 8 * s, L.W, L.cmd.Y - 2 * s - (A.Y - 8 * s));
    Panel(g, A, L"ГЕОПОИСК  //  ЗЕМЛЯ");
    Txt(g, L"Esc или клик - закрыть", g_ffMono, 13 * s, FontStyleRegular, Al(C_CYAN, 170), RectF(A.X + A.Width - 330 * s, A.Y + 12 * s, 316 * s, 20 * s), 2);

    float infoH = 126 * s;
    RectF C(A.X, A.Y + 44 * s, A.Width, std::max(80.0f * s, A.Height - 44 * s - infoH - 6 * s));
    RGBc col = failed ? C_RED : C_CYAN;

    // ориентация глобуса: пока ищем - вращается; нашли - плавно разворачивается к точке
    double tt = (double)(nowTick - g_geo.tStart) / 1000.0, lam0, phi0, e = 0, u = 0;
    if (!found) {
        lam0 = std::fmod(tt * 35.0, 360.0);
        phi0 = 18.0 + 8.0 * std::sin(0.7 * tt);
    } else {
        u = std::min(1.0, (double)(nowTick - g_geo.tFound) / 3200.0);
        e = 1.0 - std::pow(1.0 - u, 3.0);
        double dl = std::fmod(g_geo.lon - g_geo.startLam, 360.0);
        if (dl > 180) dl -= 360;
        if (dl <= -180) dl += 360;
        lam0 = g_geo.startLam + dl * e;
        phi0 = g_geo.startPhi + (g_geo.lat - g_geo.startPhi) * e;
    }
    bool mapOn = found && g_geo.mapState == 1 && g_geo.map;
    float k = mapOn ? Sm01((double)(nowTick - g_geo.tMap) / 1100.0) : 0.0f;
    float bigR = std::min(C.Width * 0.30f, C.Height * 0.46f), smallR = std::min(C.Width * 0.22f, C.Height * 0.44f);
    float gcx = Lerpf(C.X + C.Width * 0.5f, C.X + C.Width * 0.26f, k), gcy = C.Y + C.Height * 0.5f;
    float R = Lerpf(bigR, smallR, k) * (1.0f + 0.14f * (float)e);

    GeoDrawGlobe(g, gcx, gcy, R, lam0, phi0, col);

    if (!found) {  // радар поиска и мигающие «кандидаты»
        double ang = tt * 140.0;
        for (int i = 0; i < 7; ++i) {
            Pen rp(Al(failed ? C_RED : C_AMBER, 150 - 20 * i), 2.0f * s);
            float rr = R * 1.12f;
            g.DrawArc(&rp, gcx - rr, gcy - rr, 2 * rr, 2 * rr, (float)(ang - i * 7), 6.0f);
        }
        if (!failed) {
            unsigned seed = (unsigned)(tt * 6.0) * 2654435761u + 12345u;
            for (int i = 0; i < 6; ++i) {
                seed = seed * 1664525u + 1013904223u;
                double la = (double)(seed >> 8 & 0xFFFF) / 65535.0 * 120.0 - 60.0;
                seed = seed * 1664525u + 1013904223u;
                double lo = (double)(seed >> 8 & 0xFFFF) / 65535.0 * 360.0 - 180.0;
                double d2r = 3.14159265358979323846 / 180.0, la0 = phi0 * d2r, lm0 = lam0 * d2r, lat = la * d2r, lon = lo * d2r;
                double cc = std::sin(la0) * std::sin(lat) + std::cos(la0) * std::cos(lat) * std::cos(lon - lm0);
                if (cc < 0.1) continue;
                float x = gcx + (float)(R * std::cos(lat) * std::sin(lon - lm0));
                float y = gcy - (float)(R * (std::cos(la0) * std::sin(lat) - std::sin(la0) * std::cos(lat) * std::cos(lon - lm0)));
                Pen pp(Al(C_AMBER, 200), 1.5f * s);
                g.DrawEllipse(&pp, x - 5 * s, y - 5 * s, 10 * s, 10 * s);
            }
        }
    } else {  // захват цели: рамка сжимается к точке, затем пульсирует метка
        float sz = R * (float)(1.7 - 1.45 * e);
        Brackets(g, RectF(gcx - sz, gcy - sz, 2 * sz, 2 * sz), C_AMBER, std::max(8.0f * s, 0.35f * sz), 2.0f * s, 235);
        Pen cl(Al(C_AMBER, (int)(70 * e)), 1.0f);
        g.DrawLine(&cl, gcx - R, gcy, gcx + R, gcy);
        g.DrawLine(&cl, gcx, gcy - R, gcx, gcy + R);
        if (u > 0.85) {
            double ph = std::fmod(now * 0.9, 1.0);
            for (int i = 0; i < 3; ++i) {
                double f = std::fmod(ph + i / 3.0, 1.0);
                Pen pr(Al(C_AMBER, (int)(210 * (1.0 - f))), 2.0f * s);
                float rr = R * 0.03f + R * 0.2f * (float)f;
                g.DrawEllipse(&pr, gcx - rr, gcy - rr, 2 * rr, 2 * rr);
            }
        }
        SolidBrush dot(Al(C_AMBER, 255));
        g.FillEllipse(&dot, gcx - 4 * s, gcy - 4 * s, 8 * s, 8 * s);
    }

    // карта: появляется справа, масштаб «наезжает» на точку
    if (mapOn) {
        float side = std::min(C.Width * 0.46f, C.Height - 4 * s);
        RectF mr(C.X + C.Width * 0.52f, C.Y + (C.Height - side) / 2, side, side);
        double u2 = std::min(1.0, (double)(nowTick - g_geo.tMap) / 1700.0), ez = 1.0 - std::pow(1.0 - u2, 3.0);
        const float full = 768.0f;
        float vs = Lerpf(full, 420.0f, (float)ez);
        float pxm = g_geo.mkx * full, pym = g_geo.mky * full;
        float sx = std::max(0.0f, std::min(full - vs, pxm - vs / 2)), sy = std::max(0.0f, std::min(full - vs, pym - vs / 2));
        ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, k, 0}, {0, 0, 0, 0, 1}}};
        ImageAttributes ia;
        ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(g_geo.map, mr, sx, sy, vs, vs, UnitPixel, &ia);
        float mxp = mr.X + (pxm - sx) / vs * side, myp = mr.Y + (pym - sy) / vs * side;
        Pen thin(Al(C_AMBER, (int)(70 * k)), 1.0f);
        g.DrawLine(&thin, mr.X, myp, mr.X + side, myp);
        g.DrawLine(&thin, mxp, mr.Y, mxp, mr.Y + side);
        if (u2 < 1.0) {
            Pen sc(Al(C_ICE, 200), 2.0f * s);
            float yy = mr.Y + side * (float)u2;
            g.DrawLine(&sc, mr.X, yy, mr.X + side, yy);
        }
        double ph = std::fmod(now * 0.8, 1.0);
        for (int i = 0; i < 3; ++i) {
            double f = std::fmod(ph + i / 3.0, 1.0);
            Pen pr(Al(C_AMBER, (int)(200 * (1.0 - f) * k)), 2.0f * s);
            float rr = 6.0f * s + 26.0f * s * (float)f;
            g.DrawEllipse(&pr, mxp - rr, myp - rr, 2 * rr, 2 * rr);
        }
        SolidBrush pin(Al(C_AMBER, (int)(255 * k)));
        Pen pinRim(Al(C_WHITE, (int)(255 * k)), 2.0f * s);
        g.FillEllipse(&pin, mxp - 6 * s, myp - 6 * s, 12 * s, 12 * s);
        g.DrawEllipse(&pinRim, mxp - 6 * s, myp - 6 * s, 12 * s, 12 * s);
        std::wstring tag = Widen(geo_short_name(g_geo.name));
        if (tag.size() > 38) tag = tag.substr(0, 37) + L"…";
        SolidBrush tagBg(Al({2, 12, 26}, (int)(200 * k)));
        float tw = std::min(side - 12 * s, (float)tag.size() * 8.6f * s + 18 * s);
        RectF tr(mxp - tw / 2, myp - 46 * s, tw, 22 * s);
        tr.X = std::max(mr.X + 4 * s, std::min(mr.X + side - tw - 4 * s, tr.X));
        g.FillRectangle(&tagBg, tr);
        Txt(g, tag, g_ffMono, 13 * s, FontStyleBold, Al(C_WHITE, (int)(255 * k)), RectF(tr.X + 4 * s, tr.Y + 2 * s, tr.Width - 8 * s, 18 * s), 1);
        Brackets(g, mr, C_CYAN, 22 * s, 2.0f * s, (int)(235 * k));
        Txt(g, L"© участники OpenStreetMap", g_ffMono, 11 * s, FontStyleRegular, Al(C_ICE, (int)(200 * k)), RectF(mr.X + 6 * s, mr.Y + side - 18 * s, side - 12 * s, 16 * s), 2);
        Pen beam(Al(C_CYAN, (int)(140 * k)), 1.5f * s);
        beam.SetDashStyle(DashStyleDash);
        g.DrawLine(&beam, gcx + R, gcy, mr.X, mr.Y + side / 2);
    }

    // информационная строка внизу
    RectF I(A.X + 18 * s, A.Y + A.Height - infoH, A.Width - 36 * s, infoH - 8 * s);
    Pen sep(Al(C_CYAN, 80), 1.0f);
    g.DrawLine(&sep, I.X, I.Y - 4 * s, I.X + I.Width, I.Y - 4 * s);
    float px = 15 * s, lh = 22 * s, y = I.Y;
    size_t maxc = (size_t)std::max(20.0f, I.Width / (0.6f * px) - 14.0f);
    std::wstring q = Widen(g_geo.query);
    if (q.size() > maxc) q = q.substr(0, maxc - 1) + L"…";
    if (idle) q = L"-";
    Txt(g, L"ЗАПРОС  : " + q, g_ffMono, px, FontStyleRegular, Al(C_ICE, 255), RectF(I.X, y, I.Width, lh));
    y += lh;
    if (idle) {
        Txt(g, L"ГОТОВ   : скажите «покажи на карте ...» или напечатайте место в строке ниже", g_ffMono, px, FontStyleBold, Al(C_AMBER, 255), RectF(I.X, y, I.Width, lh));
        Txt(g, L"ПРИМЕРЫ  : Ташкент | Эйфелева башня | улица Навои 10, Ташкент | 41.3111, 69.2797", g_ffMono, px, FontStyleRegular, Al(C_ICE, 220), RectF(I.X, y + lh, I.Width, lh));
    } else if (!found && !failed) {
        std::wstring dots(1 + (size_t)(tt * 3) % 3, L'.');
        Txt(g, L"СТАТУС  : ПОИСК В БАЗЕ OPENSTREETMAP" + dots, g_ffMono, px, FontStyleBold, Al(C_AMBER, 255), RectF(I.X, y, I.Width, lh));
    } else if (failed) {
        Txt(g, L"СТАТУС  : НЕ НАЙДЕНО", g_ffMono, px, FontStyleBold, Al(C_RED, 255), RectF(I.X, y, I.Width, lh));
        Txt(g, Widen(g_geo.errText), g_ffMono, px, FontStyleRegular, Al(C_ICE, 220), RectF(I.X, y + lh, I.Width, lh));
    } else {
        std::wstring nm = Widen(g_geo.name);
        size_t nch = (size_t)((nowTick - g_geo.tFound) / 12);
        if (nch < nm.size()) nm.resize(nch);
        std::vector<std::wstring> lines = GeoWrap(nm, maxc, 2);
        for (size_t i = 0; i < lines.size(); ++i)
            Txt(g, (i == 0 ? std::wstring(L"НАЙДЕНО : ") : std::wstring(L"          ")) + lines[i], g_ffMono, px, FontStyleBold, Al(C_WHITE, 255), RectF(I.X, y + lh * (float)i, I.Width, lh));
        y += lh * (float)std::max<size_t>(1, lines.size());
        if (u > 0.5) {
            std::wstring co = L"ШИРОТА  : " + Widen(geo_fmt_coord(g_geo.lat, true)) + L"     ДОЛГОТА : " + Widen(geo_fmt_coord(g_geo.lon, false)) + L"     МАСШТАБ : z" + std::to_wstring(g_geo.zoom);
            Txt(g, co, g_ffMono, px, FontStyleRegular, Al(C_AMBER, 255), RectF(I.X, y, I.Width, lh));
            y += lh;
            std::wstring ms = g_geo.mapState == 0 ? L"КАРТА   : ЗАГРУЗКА ТАЙЛОВ…" : g_geo.mapState == 1 ? L"КАРТА   : ГОТОВА (OpenStreetMap)" : L"КАРТА   : НЕДОСТУПНА (нет связи с сервером тайлов), координаты верны";
            Txt(g, ms, g_ffMono, px, FontStyleRegular, Al(g_geo.mapState == 2 ? C_RED : C_ICE, 230), RectF(I.X, y, I.Width, lh));
        }
    }
}
