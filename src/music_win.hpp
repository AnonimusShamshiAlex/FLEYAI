// music_win.hpp - музыкальный плеер FLEY AI: поиск и воспроизведение музыки внутри программы.
// Поиск: Audius (полные треки) и Deezer (отрывки по 30 с), ключи не нужны.
// Звук: MCI (winmm), так что новых библиотек не требуется. Файл скачивается во временную папку и удаляется после прослушивания.
// Подключается из main.cpp после geo_win.hpp (нужны Panel, Txt, Btn, GeoArea) и до DrawHud.
// Правило потоков: MCI вызывается ТОЛЬКО из потока окна. Остальные потоки шлют сообщение WM_APP_MUSIC.
#pragma once

static const int WM_APP_MUSIC = WM_APP + 3;
enum { MM_START = 1, MM_TOGGLE, MM_NEXT, MM_PREV, MM_STOP, MM_CLOSE };
enum { MH_NONE = 0, MH_PREV, MH_PLAY, MH_NEXT, MH_VDN, MH_VUP, MH_YT, MH_STOP, MH_HIDE, MH_BAR, MH_ROW = 100 };

struct MusicState {
    std::mutex mx;
    bool open = false;                 // панель на экране
    int busy = 0;                      // 0 - ничего, 1 - идёт поиск, 2 - загружается трек
    std::string status, spoken;        // строка состояния и фраза для озвучки
    std::vector<MusicTrack> results;   // что показано в списке
    std::vector<MusicTrack> queue;     // очередь воспроизведения
    int qi = -1;                       // номер играющего трека в очереди
    int gen = 0;                       // поколение: устаревшие загрузки отбрасываются
    bool loaded = false, paused = false;
    long posMs = 0, lenMs = 0;
    int vol = 800;                     // 0..1000
    int scroll = 0;
};
static MusicState g_mus;
static std::wstring g_musCur;          // файл, открытый в MCI (трогает только поток окна)
static int g_musMx = -1, g_musMy = -1; // положение мыши для подсветки

static std::wstring MusicTempPath(int gen) {
    wchar_t t[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"fley_music_" + std::to_wstring(gen) + L".mp3";
}

// ---- сеть (https и http, редиректы WinHTTP выполняет сам)
static bool MusicHttpGet(const std::string& url8, std::string& out, size_t maxBytes, DWORD recvMs) {
    out.clear();
    std::wstring url = Widen(url8);
    wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET ses = WinHttpOpen(L"FLEY-AI/1.2 (music player)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 6000, 6000, 10000, (int)recvMs);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    std::wstring rp = std::wstring(path) + extra;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", rp.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = false;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        bool done = false;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) break;
            if (avail == 0) { done = true; break; }
            if (out.size() + avail > maxBytes) break;  // слишком большой файл
            size_t at = out.size();
            out.resize(at + avail);
            DWORD n = 0;
            if (!WinHttpReadData(req, &out[at], avail, &n)) { out.resize(at); break; }
            out.resize(at + n);
        }
        ok = done && code >= 200 && code < 300;
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok && !out.empty();
}

// Поиск сразу в двух сервисах: сначала полные треки Audius, затем отрывки Deezer. Пустой запрос - популярное.
static bool MusicSearch(const std::string& q, std::vector<MusicTrack>& out, std::string& err) {
    out.clear();
    std::string ua, ub, enc = url_encode(q);
    if (q.empty()) {
        ua = "https://api.audius.co/v1/tracks/trending?limit=10&app_name=FLEY_AI";
        ub = "https://api.deezer.com/chart/0/tracks?limit=15";
    } else {
        ua = "https://api.audius.co/v1/tracks/search?query=" + enc + "&limit=10&app_name=FLEY_AI";
        ub = "https://api.deezer.com/search?q=" + enc + "&limit=15";
    }
    bool anyOk = false;
    std::string body;
    if (MusicHttpGet(ua, body, 2 * 1024 * 1024, 8000) && music_parse_audius(body, out, 10)) anyOk = true;
    if (MusicHttpGet(ub, body, 2 * 1024 * 1024, 8000) && music_parse_deezer(body, out, 25)) anyOk = true;
    if (!anyOk) { err = "Нет связи с музыкальными сервисами. Проверьте интернет."; return false; }
    return true;
}

// ---- воспроизведение (MCI), только поток окна
static long MusicStatusNum(const wchar_t* what) {
    wchar_t buf[64] = {};
    std::wstring cmd = std::wstring(L"status fleymus ") + what;
    if (mciSendStringW(cmd.c_str(), buf, 63, nullptr) != 0) return 0;
    return _wtol(buf);
}
static void MusicApplyVolume() {
    int v;
    { std::lock_guard<std::mutex> lk(g_mus.mx); v = g_mus.vol; }
    if (g_state == SPEAKING) v /= 4;  // пока говорит FLEY, музыка тише
    mciSendStringW((L"setaudio fleymus volume to " + std::to_wstring(v)).c_str(), nullptr, 0, nullptr);
}
static void MusicCloseDevice() {
    mciSendStringW(L"close fleymus", nullptr, 0, nullptr);
    if (!g_musCur.empty()) { DeleteFileW(g_musCur.c_str()); g_musCur.clear(); }
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.loaded = false;
    g_mus.paused = false;
    g_mus.posMs = g_mus.lenMs = 0;
}
static void MusicStartFile(int gen) {
    std::wstring f = MusicTempPath(gen);
    { std::lock_guard<std::mutex> lk(g_mus.mx); if (g_mus.gen != gen) { DeleteFileW(f.c_str()); return; } }
    MusicCloseDevice();
    bool ok = mciSendStringW((L"open \"" + f + L"\" type mpegvideo alias fleymus").c_str(), nullptr, 0, nullptr) == 0;
    if (ok) {
        g_musCur = f;
        mciSendStringW(L"set fleymus time format milliseconds", nullptr, 0, nullptr);
        MusicApplyVolume();
        ok = mciSendStringW(L"play fleymus", nullptr, 0, nullptr) == 0;
    }
    if (!ok) {
        MusicCloseDevice();
        DeleteFileW(f.c_str());
        std::lock_guard<std::mutex> lk(g_mus.mx);
        g_mus.busy = 0;
        g_mus.status = "Не удалось воспроизвести файл. Попробуйте другой трек.";
        return;
    }
    long len = MusicStatusNum(L"length");
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.loaded = true;
    g_mus.paused = false;
    g_mus.busy = 0;
    g_mus.lenMs = len;
    g_mus.posMs = 0;
    g_mus.status.clear();
}

// ---- загрузка трека (любой поток)
struct MusicDl { int gen; std::string url; };
static DWORD WINAPI MusicDownloadThread(LPVOID p) {
    MusicDl* a = (MusicDl*)p;
    std::string data;
    bool ok = MusicHttpGet(a->url, data, 80u * 1024 * 1024, 30000);
    if (ok && (data.size() < 8192 || data[0] == '<' || data[0] == '{')) ok = false;  // пришла страница или JSON с ошибкой, а не музыка
    bool stale;
    { std::lock_guard<std::mutex> lk(g_mus.mx); stale = g_mus.gen != a->gen; }
    if (stale) { delete a; return 0; }
    if (ok) ok = WriteFileText(MusicTempPath(a->gen), data);
    if (!ok) {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        if (g_mus.gen == a->gen) { g_mus.busy = 0; g_mus.status = "Не удалось загрузить трек. Попробуйте другой."; }
    } else {
        PostMessageW(g_hwnd, WM_APP_MUSIC, MM_START, (LPARAM)a->gen);
    }
    LogLine(std::string("music: ") + (ok ? "загружен " : "ошибка загрузки ") + a->url);
    delete a;
    return 0;
}
static void MusicPlayQueue(int idx) {
    MusicTrack t;
    int gen;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        if (idx < 0 || idx >= (int)g_mus.queue.size()) return;
        g_mus.qi = idx;
        t = g_mus.queue[(size_t)idx];
        gen = ++g_mus.gen;
        g_mus.busy = 2;
        g_mus.status = "Загружаю: " + t.title;
    }
    MusicDl* a = new MusicDl{gen, t.url};
    HANDLE th = CreateThread(nullptr, 0, MusicDownloadThread, a, 0, nullptr);
    if (th) CloseHandle(th);
    else {
        delete a;
        std::lock_guard<std::mutex> lk(g_mus.mx);
        g_mus.busy = 0;
        g_mus.status = "Не удалось начать загрузку.";
    }
}

// ---- управление (поток окна)
static void MusicToggle() {
    bool loaded, paused;
    int qi;
    { std::lock_guard<std::mutex> lk(g_mus.mx); loaded = g_mus.loaded; paused = g_mus.paused; qi = g_mus.qi; }
    if (!loaded) { if (qi >= 0) MusicPlayQueue(qi); return; }  // ничего не играет: заново включить текущий трек
    mciSendStringW(paused ? L"resume fleymus" : L"pause fleymus", nullptr, 0, nullptr);
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.paused = !paused;
}
static void MusicSeekMs(long ms) {
    bool paused;
    long len;
    { std::lock_guard<std::mutex> lk(g_mus.mx); if (!g_mus.loaded) return; paused = g_mus.paused; len = g_mus.lenMs; }
    ms = std::max(0L, std::min(ms, std::max(0L, len - 300)));
    mciSendStringW((L"seek fleymus to " + std::to_wstring(ms)).c_str(), nullptr, 0, nullptr);
    mciSendStringW(L"play fleymus", nullptr, 0, nullptr);
    if (paused) mciSendStringW(L"pause fleymus", nullptr, 0, nullptr);
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.posMs = ms;
}
static void MusicStep(int dir) {
    int idx = -1;
    long pos;
    bool loaded;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        if (g_mus.queue.empty()) return;
        pos = g_mus.posMs;
        loaded = g_mus.loaded;
        idx = g_mus.qi + dir;
        if (dir < 0 && loaded && pos > 3000) idx = g_mus.qi;  // «назад» в середине трека - в начало трека
        if (idx < 0) idx = 0;
        if (idx >= (int)g_mus.queue.size()) { g_mus.status = "Это последний трек в списке."; return; }
    }
    MusicPlayQueue(idx);
}
static void MusicStopPlayback() {
    { std::lock_guard<std::mutex> lk(g_mus.mx); ++g_mus.gen; g_mus.busy = 0; }  // отменяем загрузки в пути
    MusicCloseDevice();
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.status = "Остановлено.";
}
static void MusicVolStep(int dir) {
    bool loaded;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        g_mus.vol = std::max(0, std::min(1000, g_mus.vol + dir * 100));
        loaded = g_mus.loaded;
    }
    if (loaded) MusicApplyVolume();
}
static void MusicShutdown() {
    { std::lock_guard<std::mutex> lk(g_mus.mx); ++g_mus.gen; }
    MusicCloseDevice();
}

// Вызывается из таймера окна: позиция, конец трека, приглушение на время речи FLEY
static void MusicTick() {
    static DWORD last = 0;
    static LONG lastDuck = -1;
    DWORD now = GetTickCount();
    if (now - last < 250) return;
    last = now;
    bool loaded, paused, loading;
    { std::lock_guard<std::mutex> lk(g_mus.mx); loaded = g_mus.loaded; paused = g_mus.paused; loading = g_mus.busy == 2; }
    if (!loaded) { lastDuck = -1; return; }
    LONG duck = g_state == SPEAKING ? 1 : 0;
    if (duck != lastDuck) { lastDuck = duck; MusicApplyVolume(); }
    if (paused) return;
    long pos = MusicStatusNum(L"position");
    wchar_t mode[32] = {};
    mciSendStringW(L"status fleymus mode", mode, 31, nullptr);
    { std::lock_guard<std::mutex> lk(g_mus.mx); g_mus.posMs = pos; }
    if (!loading && _wcsicmp(mode, L"stopped") == 0) {  // трек доиграл: следующий в очереди
        int next = -1;
        { std::lock_guard<std::mutex> lk(g_mus.mx); if (g_mus.qi + 1 < (int)g_mus.queue.size()) next = g_mus.qi + 1; }
        if (next >= 0) MusicPlayQueue(next);
        else MusicCloseDevice();
    }
}

// ---- панель
static bool MusicOpen() { std::lock_guard<std::mutex> lk(g_mus.mx); return g_mus.open; }
static bool MusicPlaying() { std::lock_guard<std::mutex> lk(g_mus.mx); return g_mus.loaded && !g_mus.paused; }
static bool MusicOwnsMedia() { std::lock_guard<std::mutex> lk(g_mus.mx); return g_mus.loaded; }
static void MusicHide() { std::lock_guard<std::mutex> lk(g_mus.mx); g_mus.open = false; }
static void MusicShow() {
    GeoClose();  // глобус и плеер не показываются вместе
    std::lock_guard<std::mutex> lk(g_mus.mx);
    g_mus.open = true;
}
static void MusicToggleOpen() { if (MusicOpen()) MusicHide(); else MusicShow(); }
static std::string MusicSpokenAnswer() { std::lock_guard<std::mutex> lk(g_mus.mx); return g_mus.spoken; }

struct MusLay {
    Gdiplus::RectF A, list, bar, bPrev, bPlay, bNext, bVd, bVu, bYt, bStop, bHide;
    float rowH = 0;
    int rows = 1;
    bool narrow = false;
};
static MusLay MusicLay() {
    using namespace Gdiplus;
    MusLay m;
    float s = L.s;
    m.A = GeoArea();
    m.narrow = m.A.Width < 800 * s;
    float y0 = m.A.Y + m.A.Height - 124 * s;
    m.list = RectF(m.A.X + 14 * s, m.A.Y + 76 * s, m.A.Width - 28 * s, std::max(40.0f * s, y0 - (m.A.Y + 76 * s) - 8 * s));
    m.rowH = 38 * s;
    m.rows = std::max(1, (int)(m.list.Height / m.rowH));
    m.bar = RectF(m.A.X + 84 * s, y0 + 60 * s, m.A.Width - 168 * s, 10 * s);
    float by = y0 + 80 * s, bh = 36 * s, bw = 62 * s, gap = 8 * s, x = m.A.X + 20 * s;
    m.bPrev = RectF(x, by, bw, bh);
    m.bPlay = RectF(x + bw + gap, by, bw, bh);
    m.bNext = RectF(x + 2 * (bw + gap), by, bw, bh);
    float rx = m.A.X + m.A.Width - 20 * s;
    float wHide = (m.narrow ? 96.0f : 130.0f) * s, wStop = (m.narrow ? 70.0f : 100.0f) * s;
    m.bHide = RectF(rx - wHide, by, wHide, bh);
    m.bStop = RectF(m.bHide.X - gap - wStop, by, wStop, bh);
    if (!m.narrow) {
        float vx = x + 3 * (bw + gap) + 24 * s;
        m.bVd = RectF(vx, by, 44 * s, bh);
        m.bVu = RectF(vx + 44 * s + 150 * s, by, 44 * s, bh);
        m.bYt = RectF(m.bStop.X - gap - 190 * s, by, 190 * s, bh);
    }
    return m;
}
static bool MusIn(const Gdiplus::RectF& r, int x, int y) {
    return r.Width > 0 && (float)x >= r.X && (float)x < r.X + r.Width && (float)y >= r.Y && (float)y < r.Y + r.Height;
}
static int MusicHitTest(const MusLay& m, int x, int y, size_t nRes, int scroll) {
    if (MusIn(m.bPrev, x, y)) return MH_PREV;
    if (MusIn(m.bPlay, x, y)) return MH_PLAY;
    if (MusIn(m.bNext, x, y)) return MH_NEXT;
    if (MusIn(m.bVd, x, y)) return MH_VDN;
    if (MusIn(m.bVu, x, y)) return MH_VUP;
    if (MusIn(m.bYt, x, y)) return MH_YT;
    if (MusIn(m.bStop, x, y)) return MH_STOP;
    if (MusIn(m.bHide, x, y)) return MH_HIDE;
    float s = L.s;
    if (MusIn(Gdiplus::RectF(m.bar.X, m.bar.Y - 8 * s, m.bar.Width, m.bar.Height + 16 * s), x, y)) return MH_BAR;
    if (MusIn(m.list, x, y)) {
        int row = (int)(((float)y - m.list.Y) / m.rowH);
        int idx = scroll + row;
        if (row >= 0 && row < m.rows && idx >= 0 && (size_t)idx < nRes) return MH_ROW + idx;
    }
    return MH_NONE;
}
static int MusicHoverId() {
    size_t n;
    int sc;
    { std::lock_guard<std::mutex> lk(g_mus.mx); if (!g_mus.open) return MH_NONE; n = g_mus.results.size(); sc = g_mus.scroll; }
    return MusicHitTest(MusicLay(), g_musMx, g_musMy, n, sc);
}
static bool MusicOverlayHit(int, int y) {  // клики в полосе плеера не должны доходить до HUD под ним
    if (!MusicOpen()) return false;
    Gdiplus::RectF A = GeoArea();
    return (float)y >= A.Y - 8 * L.s && (float)y < L.cmd.Y - 2 * L.s;
}
static void MusicScroll(int rowsDelta) {
    int rows = MusicLay().rows;
    std::lock_guard<std::mutex> lk(g_mus.mx);
    int maxSc = std::max(0, (int)g_mus.results.size() - rows);
    g_mus.scroll = std::max(0, std::min(maxSc, g_mus.scroll + rowsDelta));
}

static DWORD WINAPI MusicYtThread(LPVOID p) {
    std::string* q = (std::string*)p;
    ActYouTube(*q, true);
    delete q;
    return 0;
}
static void MusicOpenYoutube() {  // полная версия песни: ищем то же самое на YouTube
    std::string q;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        if (g_mus.qi >= 0 && g_mus.qi < (int)g_mus.queue.size()) q = g_mus.queue[(size_t)g_mus.qi].artist + " " + g_mus.queue[(size_t)g_mus.qi].title;
    }
    q = trim(q);
    if (q.empty()) return;
    if (MusicPlaying()) MusicToggle();  // чтобы два источника звука не играли вместе
    HANDLE th = CreateThread(nullptr, 0, MusicYtThread, new std::string(q), 0, nullptr);
    if (th) CloseHandle(th);
}
static void MusicClick(int x, int y) {
    MusLay m = MusicLay();
    size_t n;
    int sc;
    { std::lock_guard<std::mutex> lk(g_mus.mx); n = g_mus.results.size(); sc = g_mus.scroll; }
    int id = MusicHitTest(m, x, y, n, sc);
    switch (id) {
        case MH_PREV: MusicStep(-1); break;
        case MH_PLAY: MusicToggle(); break;
        case MH_NEXT: MusicStep(1); break;
        case MH_VDN: MusicVolStep(-1); break;
        case MH_VUP: MusicVolStep(1); break;
        case MH_YT: MusicOpenYoutube(); break;
        case MH_STOP: MusicStopPlayback(); break;
        case MH_HIDE: MusicHide(); break;
        case MH_BAR: {
            long len;
            { std::lock_guard<std::mutex> lk(g_mus.mx); len = g_mus.lenMs; }
            float frac = std::max(0.0f, std::min(1.0f, ((float)x - m.bar.X) / std::max(1.0f, m.bar.Width)));
            MusicSeekMs((long)(frac * (float)len));
            break;
        }
        default:
            if (id >= MH_ROW) {
                int idx = id - MH_ROW;
                {
                    std::lock_guard<std::mutex> lk(g_mus.mx);
                    if (idx < 0 || idx >= (int)g_mus.results.size()) return;
                    g_mus.queue = g_mus.results;  // дальше трек за треком по этому списку
                }
                MusicPlayQueue(idx);
            }
    }
}

// Сообщения от других потоков
static void MusicOnMessage(WPARAM w, LPARAM l) {
    switch ((int)w) {
        case MM_START: MusicStartFile((int)l); break;
        case MM_TOGGLE: MusicToggle(); break;
        case MM_NEXT: MusicStep(1); break;
        case MM_PREV: MusicStep(-1); break;
        case MM_STOP: MusicStopPlayback(); break;
        case MM_CLOSE: MusicStopPlayback(); MusicHide(); break;
    }
}

// ---- действия для голоса / ИИ
static ActResult ActMusicClose() {
    PostMessageW(g_hwnd, WM_APP_MUSIC, MM_CLOSE, 0);
    return Ok("Плеер закрыт.");
}
static ActResult ActMusicMedia(const std::string& what) {
    int code = what == "play_pause" ? MM_TOGGLE : what == "next" ? MM_NEXT : what == "prev" ? MM_PREV : what == "stop" ? MM_STOP : 0;
    if (!code) return Fail("Неизвестная команда плеера.");
    PostMessageW(g_hwnd, WM_APP_MUSIC, (WPARAM)code, 0);
    return Ok("Готово.");
}
// music | запрос | play (включить первый найденный) или list (показать список) или idle (открыть пустой плеер)
static ActResult ActMusic(const Action& a) {
    std::string q = utf8_truncate(trim(a.args.empty() ? std::string() : a.args[0]), 120);
    std::string mode = a.args.size() > 1 ? a.args[1] : std::string("play");
    MusicShow();
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        g_mus.spoken.clear();
        if (q.empty() && mode == "idle") {
            g_mus.status = "Введите название песни или исполнителя в строку внизу и нажмите Enter.";
            return Ok("Плеер открыт.");
        }
        g_mus.busy = 1;
        g_mus.status = q.empty() ? "Ищу популярные треки" : "Ищу: " + q;
    }
    std::vector<MusicTrack> tr;
    std::string err;
    bool ok = MusicSearch(q, tr, err);
    if (!ok || tr.empty()) {
        std::string why = ok ? "В плеере ничего не нашлось по запросу «" + q + "»." : err;
        { std::lock_guard<std::mutex> lk(g_mus.mx); g_mus.busy = 0; g_mus.status = why; }
        if (mode == "play") {  // запасной путь: то же самое на YouTube
            ActResult yt = ActYouTube(q.empty() ? std::string("популярная музыка") : q, true);
            if (yt.ok) {
                std::lock_guard<std::mutex> lk(g_mus.mx);
                g_mus.spoken = "В плеере не нашёл, включаю на YouTube.";
                return Ok(why + " Включено на YouTube. " + yt.msg);
            }
        }
        return Fail(why, true);
    }
    size_t nFull = 0;
    for (const MusicTrack& t : tr) if (t.full) ++nFull;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        g_mus.results = tr;
        g_mus.scroll = 0;
        g_mus.busy = 0;
        g_mus.status = "Найдено: " + std::to_string(tr.size()) + " (полных треков: " + std::to_string(nFull) + ", отрывков 30 с: " + std::to_string(tr.size() - nFull) + ")";
        if (mode == "play") g_mus.queue = tr;
    }
    std::string spoken;
    if (mode == "play") {
        MusicPlayQueue(0);
        spoken = "Включаю: " + tr[0].title + (tr[0].artist.empty() ? "" : ", " + tr[0].artist) + "." + (tr[0].full ? "" : " Это отрывок на тридцать секунд.");
    } else {
        spoken = "Нашёл треков: " + std::to_string(tr.size()) + ". Выберите в списке.";
    }
    { std::lock_guard<std::mutex> lk(g_mus.mx); g_mus.spoken = spoken; }
    std::string list;
    for (size_t i = 0; i < tr.size() && i < 6; ++i) list += std::to_string(i + 1) + ". " + tr[i].title + (tr[i].artist.empty() ? "" : " - " + tr[i].artist) + (tr[i].full ? " (полный)" : " (отрывок 30 с)") + "; ";
    return Ok("Найдено в плеере: " + std::to_string(tr.size()) + " треков. " + (mode == "play" ? "Включён первый. " : "Список показан на экране. ") + list, true);
}

// ---- рисование
static void MusIcon(Gdiplus::Graphics& g, int kind, Gdiplus::RectF r, Gdiplus::Color col) {  // 0 play, 1 pause, 2 prev, 3 next
    using namespace Gdiplus;
    SolidBrush b(col);
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, h = std::min(r.Width, r.Height) * 0.30f;
    if (kind == 0) {
        PointF p[3] = {PointF(cx - h * 0.7f, cy - h), PointF(cx - h * 0.7f, cy + h), PointF(cx + h, cy)};
        g.FillPolygon(&b, p, 3);
    } else if (kind == 1) {
        g.FillRectangle(&b, cx - h * 0.85f, cy - h, h * 0.6f, 2 * h);
        g.FillRectangle(&b, cx + h * 0.25f, cy - h, h * 0.6f, 2 * h);
    } else if (kind == 2) {
        PointF p[3] = {PointF(cx + h, cy - h), PointF(cx + h, cy + h), PointF(cx - h * 0.5f, cy)};
        g.FillPolygon(&b, p, 3);
        g.FillRectangle(&b, cx - h * 0.95f, cy - h, h * 0.35f, 2 * h);
    } else {
        PointF p[3] = {PointF(cx - h, cy - h), PointF(cx - h, cy + h), PointF(cx + h * 0.5f, cy)};
        g.FillPolygon(&b, p, 3);
        g.FillRectangle(&b, cx + h * 0.6f, cy - h, h * 0.35f, 2 * h);
    }
}

static void DrawMusic(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    std::vector<MusicTrack> res;
    MusicTrack cur;
    bool hasCur = false, loaded, paused;
    int busy, scroll, vol;
    long pos, len;
    std::string status;
    {
        std::lock_guard<std::mutex> lk(g_mus.mx);
        if (!g_mus.open) return;
        res = g_mus.results;
        if (g_mus.qi >= 0 && g_mus.qi < (int)g_mus.queue.size()) { cur = g_mus.queue[(size_t)g_mus.qi]; hasCur = true; }
        loaded = g_mus.loaded; paused = g_mus.paused; busy = g_mus.busy; scroll = g_mus.scroll; vol = g_mus.vol;
        pos = g_mus.posMs; len = g_mus.lenMs; status = g_mus.status;
    }
    float s = L.s;
    double now = GetTickCount64() / 1000.0;
    MusLay m = MusicLay();
    RectF A = m.A;
    int hid = MusicHitTest(m, g_musMx, g_musMy, res.size(), scroll);

    SolidBrush dim(Al({2, 9, 20}, 240));
    g.FillRectangle(&dim, 0.0f, A.Y - 8 * s, L.W, L.cmd.Y - 2 * s - (A.Y - 8 * s));
    Panel(g, A, L"МУЗЫКА  //  ПЛЕЕР");
    Txt(g, L"Esc - свернуть    колесо мыши - список", g_ffMono, 13 * s, FontStyleRegular, Al(C_CYAN, 170), RectF(A.X + A.Width - 420 * s, A.Y + 12 * s, 406 * s, 20 * s), 2);

    // строка состояния
    std::wstring st;
    if (!status.empty()) st = Widen(status);
    else st = res.empty() ? L"Введите название песни или исполнителя в строку внизу и нажмите Enter" : L"Нажмите на трек, чтобы включить";
    if (busy) st += std::wstring((size_t)(1 + (int)(now * 3) % 3), L'.');
    Txt(g, st, g_ffMono, 15 * s, FontStyleRegular, Al(busy ? C_AMBER : C_ICE, 255), RectF(A.X + 20 * s, A.Y + 46 * s, A.Width - 40 * s, 22 * s));

    // список
    float lx = m.list.X, lw = m.list.Width;
    float wNum = 44 * s, wDur = 64 * s, wTag = 110 * s, wArt = std::max(80.0f * s, lw * 0.26f);
    float wTitle = std::max(60.0f * s, lw - wNum - wDur - wTag - wArt);
    if (res.empty()) {
        Txt(g, L"Например: «Imagine Dragons Believer», «Coldplay», «Бабек».", g_ffMono, 14 * s, FontStyleRegular, Al(C_BLUE, 200), RectF(lx, m.list.Y + 10 * s, lw, 22 * s), 1);
        Txt(g, L"Источники: Audius - полные треки, Deezer - отрывки по 30 секунд.", g_ffMono, 13 * s, FontStyleRegular, Al(C_BLUE, 150), RectF(lx, m.list.Y + 36 * s, lw, 22 * s), 1);
        Txt(g, L"Голосом: «включи песню ...», «найди трек ...», «следующий», «пауза».", g_ffMono, 13 * s, FontStyleRegular, Al(C_BLUE, 150), RectF(lx, m.list.Y + 60 * s, lw, 22 * s), 1);
    }
    for (int r = 0; r < m.rows; ++r) {
        int i = scroll + r;
        if (i >= (int)res.size()) break;
        const MusicTrack& t = res[(size_t)i];
        RectF rr(lx, m.list.Y + r * m.rowH, lw, m.rowH - 2 * s);
        bool isCur = hasCur && t.id == cur.id && t.source == cur.source;
        bool hov = hid == MH_ROW + i;
        if (isCur || hov) {
            SolidBrush f(Al(isCur ? C_AMBER : C_CYAN, isCur ? 45 : 30));
            g.FillRectangle(&f, rr);
        }
        if (isCur) { SolidBrush mk(Al(C_AMBER, 255)); g.FillRectangle(&mk, rr.X, rr.Y, 3 * s, rr.Height); }
        RGBc col = isCur ? C_AMBER : C_ICE;
        int bold = isCur ? FontStyleBold : FontStyleRegular;
        float ty = rr.Y + 8 * s;
        Txt(g, std::to_wstring(i + 1), g_ffMono, 15 * s, FontStyleRegular, Al(C_BLUE, 200), RectF(rr.X + 8 * s, ty, wNum, 22 * s));
        Txt(g, Widen(t.title), g_ffUi, 17 * s, bold, Al(col, 255), RectF(rr.X + wNum, ty - 1 * s, wTitle - 10 * s, 24 * s));
        Txt(g, Widen(t.artist), g_ffUi, 16 * s, FontStyleRegular, Al(C_BLUE, 235), RectF(rr.X + wNum + wTitle, ty - 1 * s, wArt - 10 * s, 24 * s));
        Txt(g, Widen(music_fmt_time(t.secs)), g_ffMono, 15 * s, FontStyleRegular, Al(C_CYAN, 220), RectF(rr.X + wNum + wTitle + wArt, ty, wDur - 6 * s, 22 * s), 2);
        Txt(g, t.full ? L"ПОЛНЫЙ" : L"ОТРЫВОК", g_ffMono, 13 * s, FontStyleBold, Al(t.full ? C_ICE : C_GOLD, 220), RectF(rr.X + lw - wTag, ty + 1 * s, wTag - 10 * s, 22 * s), 2);
    }
    if ((int)res.size() > m.rows) {  // полоса прокрутки
        float th = std::max(20.0f * s, m.list.Height * (float)m.rows / (float)res.size());
        float ty = m.list.Y + (m.list.Height - th) * (float)scroll / (float)std::max(1, (int)res.size() - m.rows);
        SolidBrush sb(Al(C_CYAN, 140));
        g.FillRectangle(&sb, m.list.X + m.list.Width + 4 * s, ty, 4 * s, th);
    }

    // нижний блок: что играет, прогресс, кнопки
    float y0 = A.Y + A.Height - 124 * s;
    Pen sep(Al(C_CYAN, 95), 1.0f);
    g.DrawLine(&sep, A.X + 14 * s, y0, A.X + A.Width - 14 * s, y0);
    std::string nowT = hasCur ? cur.title + (cur.artist.empty() ? "" : "  -  " + cur.artist) : "Ничего не играет";
    Txt(g, Widen(nowT), g_ffUi, 20 * s, FontStyleBold, Al(hasCur ? C_ICE : C_BLUE, 255), RectF(A.X + 20 * s, y0 + 8 * s, A.Width - 220 * s, 28 * s));
    if (hasCur) {
        std::wstring info = cur.full ? L"Audius  -  полный трек" : L"Deezer  -  отрывок 30 секунд. Полная песня: кнопка НА YOUTUBE";
        Txt(g, info, g_ffMono, 13 * s, FontStyleRegular, Al(cur.full ? C_BLUE : C_GOLD, 200), RectF(A.X + 20 * s, y0 + 36 * s, A.Width - 40 * s, 18 * s));
    }
    if (loaded && !paused) {  // эквалайзер
        SolidBrush eb(Al(C_CYAN, 210));
        for (int i = 0; i < 16; ++i) {
            float k = 0.2f + 0.8f * (float)std::fabs(std::sin(now * 5.0 + i * 0.9) * std::sin(now * 2.3 + i * 1.7));
            float bh = k * 24 * s;
            g.FillRectangle(&eb, A.X + A.Width - 20 * s - (16 - i) * 9 * s, y0 + 34 * s - bh, 6 * s, bh);
        }
    }
    long showLen = len > 0 ? len : (long)cur.secs * 1000L;
    float frac = showLen > 0 ? std::max(0.0f, std::min(1.0f, (float)pos / (float)showLen)) : 0.0f;
    Txt(g, Widen(music_fmt_time(pos / 1000)), g_ffMono, 14 * s, FontStyleRegular, Al(C_ICE, 230), RectF(A.X + 20 * s, y0 + 54 * s, 60 * s, 20 * s));
    Txt(g, Widen(music_fmt_time(showLen / 1000)), g_ffMono, 14 * s, FontStyleRegular, Al(C_ICE, 230), RectF(m.bar.X + m.bar.Width + 8 * s, y0 + 54 * s, 70 * s, 20 * s));
    Pen bp(Al(C_CYAN, 120), 1.0f);
    g.DrawRectangle(&bp, m.bar.X, m.bar.Y, m.bar.Width, m.bar.Height);
    SolidBrush pf(Al(C_CYAN, 200));
    g.FillRectangle(&pf, m.bar.X, m.bar.Y, m.bar.Width * frac, m.bar.Height);
    if (hasCur) { SolidBrush kn(Al(C_WHITE, 255)); g.FillRectangle(&kn, m.bar.X + m.bar.Width * frac - 3 * s, m.bar.Y - 4 * s, 6 * s, m.bar.Height + 8 * s); }

    auto B = [&](int id, RectF r, const wchar_t* text, RGBc c) {
        if (r.Width <= 0) return;
        BtnDef b = {id, ToRC(r), text, c};
        Btn(g, b, hid == id, false);
    };
    B(MH_PREV, m.bPrev, L"", C_CYAN);
    B(MH_PLAY, m.bPlay, L"", C_ICE);
    B(MH_NEXT, m.bNext, L"", C_CYAN);
    MusIcon(g, 2, m.bPrev, Al(hid == MH_PREV ? C_WHITE : C_ICE, 255));
    MusIcon(g, (loaded && !paused) ? 1 : 0, m.bPlay, Al(hid == MH_PLAY ? C_WHITE : C_ICE, 255));
    MusIcon(g, 3, m.bNext, Al(hid == MH_NEXT ? C_WHITE : C_ICE, 255));
    if (m.bVd.Width > 0) {
        B(MH_VDN, m.bVd, L"-", C_CYAN);
        B(MH_VUP, m.bVu, L"+", C_CYAN);
        Txt(g, L"ГРОМКОСТЬ " + std::to_wstring(vol / 10) + L"%", g_ffMono, 14 * s, FontStyleBold, Al(C_ICE, 235), RectF(m.bVd.X + m.bVd.Width, m.bVd.Y + 8 * s, m.bVu.X - (m.bVd.X + m.bVd.Width), 22 * s), 1);
    }
    B(MH_YT, m.bYt, L"НА YOUTUBE", C_GOLD);
    B(MH_STOP, m.bStop, L"СТОП", C_RED);
    B(MH_HIDE, m.bHide, L"СВЕРНУТЬ", C_CYAN);
}
