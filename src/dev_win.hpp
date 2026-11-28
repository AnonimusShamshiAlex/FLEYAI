// dev_win.hpp - FLEY DEV: окно для программистов, сисадминов и гиков.
// Слева: база знаний (команды, утилиты, приёмы по git, docker, Linux, Windows, сетям, защите, Python, C++ и т.д.) с поиском.
// Справа: встроенный терминал (CMD или PowerShell) и кнопка «ИИ»: объяснить команду, разобрать вывод или решить задачу.
// Свои записи: файл kb_user.txt рядом с программой, строки вида КАТЕГОРИЯ|команда|описание.
// Правило потоков: окно создаётся и живёт в потоке главного окна; остальные потоки шлют WM_APP_DEV.
#pragma once
#include "kb_data.hpp"

// WM_APP_DEV объявлен в main.cpp: 1 - открыть (lParam: std::string* запрос или 0), 3 - скрыть
static const int WM_DEV_OUT = WM_APP + 21;   // вывод команды (lParam: std::wstring*)

struct KbE { std::wstring cat, cmd, desc, low; };
static std::vector<KbE> g_kb;
static std::vector<std::wstring> g_kbCats;
static std::once_flag g_kbOnce;
static std::vector<int> g_kbView;

enum { DV_CAT = 401, DV_SEARCH, DV_LIST, DV_DETAIL, DV_OUT, DV_IN, DV_RUN, DV_SHELL, DV_AI, DV_CLR };
static HWND g_devWnd = nullptr, g_dvCat = nullptr, g_dvSearch = nullptr, g_dvList = nullptr, g_dvDetail = nullptr,
            g_dvOut = nullptr, g_dvIn = nullptr, g_dvRun = nullptr, g_dvShell = nullptr, g_dvAi = nullptr, g_dvClr = nullptr;
static HFONT g_dvFont = nullptr;
static HBRUSH g_dvBr = nullptr;
static bool g_dvPs = false, g_dvReady = false;
static int g_dvCatIdx = 0;                      // 0 = все категории
static std::wstring g_dvCwd, g_dvLastCmd, g_dvLastOut;
static volatile LONG g_dvBusy = 0;
static std::vector<std::wstring> g_dvHist;
static int g_dvHistPos = 0;
static WNDPROC g_dvOldIn = nullptr;

static std::wstring DvTrim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n"), b = s.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}
static std::wstring DvLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}
static void DevParseLine(const std::string& s) {
    size_t a = s.find('|'), b = s.rfind('|');
    if (a == std::string::npos || b == a) return;
    KbE e;
    e.cat = Widen(s.substr(0, a));
    e.cmd = Widen(s.substr(a + 1, b - a - 1));
    e.desc = Widen(s.substr(b + 1));
    if (e.cat.empty() || e.cmd.empty()) return;
    e.low = DvLower(e.cat + L" " + e.cmd + L" " + e.desc);
    g_kb.push_back(e);
}
static void DevLoadKb() {
    std::call_once(g_kbOnce, [] {
        for (int i = 0; i < kKbCount; ++i) DevParseLine(kKbLines[i]);
        std::string u = ReadFile(g_dir + L"\\kb_user.txt");   // свои записи пользователя
        size_t p = 0;
        while (p < u.size()) {
            size_t e = u.find('\n', p);
            if (e == std::string::npos) e = u.size();
            std::string ln = u.substr(p, e - p);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (!ln.empty() && ln[0] != '#') DevParseLine(ln);
            p = e + 1;
        }
        for (const KbE& e : g_kb) {
            bool have = false;
            for (const std::wstring& c : g_kbCats) if (c == e.cat) { have = true; break; }
            if (!have) g_kbCats.push_back(e.cat);
        }
    });
}
// поиск: все слова запроса должны встречаться в категории, команде или описании
static std::vector<int> DevMatch(const std::wstring& q0, const std::wstring& cat) {
    std::vector<std::wstring> tok;
    std::wstring q = DvLower(q0), cur;
    for (wchar_t c : q) { if (c == L' ') { if (!cur.empty()) tok.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) tok.push_back(cur);
    std::vector<int> r;
    for (int i = 0; i < (int)g_kb.size(); ++i) {
        if (!cat.empty() && g_kb[i].cat != cat) continue;
        bool ok = true;
        for (const std::wstring& t : tok) if (g_kb[i].low.find(t) == std::wstring::npos) { ok = false; break; }
        if (ok) r.push_back(i);
    }
    return r;
}
static void DevFilter() {
    if (!g_dvReady) return;
    wchar_t buf[256] = {};
    GetWindowTextW(g_dvSearch, buf, 255);
    std::wstring cat = g_dvCatIdx > 0 && g_dvCatIdx <= (int)g_kbCats.size() ? g_kbCats[g_dvCatIdx - 1] : L"";
    g_kbView = DevMatch(buf, cat);
    SendMessageW(g_dvList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_dvList, LB_RESETCONTENT, 0, 0);
    for (int i : g_kbView) {
        const KbE& e = g_kb[i];
        SendMessageW(g_dvList, LB_ADDSTRING, 0, (LPARAM)(L"[" + e.cat + L"] " + e.cmd + L"   -   " + e.desc).c_str());
    }
    SendMessageW(g_dvList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_dvList, nullptr, TRUE);
    SetWindowTextW(g_dvDetail, (L"Найдено: " + std::to_wstring(g_kbView.size()) + L" из " + std::to_wstring(g_kb.size()) +
                                L".  Двойной щелчок по записи - вставить команду в терминал.").c_str());
    SetWindowTextW(g_dvCat, (L"КАТЕГОРИЯ: " + (cat.empty() ? std::wstring(L"ВСЕ") : cat)).c_str());
}
static void DevShowSel() {
    int s = (int)SendMessageW(g_dvList, LB_GETCURSEL, 0, 0);
    if (s < 0 || s >= (int)g_kbView.size()) return;
    const KbE& e = g_kb[g_kbView[s]];
    SetWindowTextW(g_dvDetail, (e.cat + L":  " + e.cmd + L"\r\n" + e.desc + L"   (двойной щелчок - в терминал)").c_str());
}
static void DevAppend(const std::wstring& t) {
    std::wstring o;
    o.reserve(t.size() + 16);
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'\r') continue;
        if (t[i] == L'\n') o += L"\r\n"; else o += t[i];
    }
    int len = GetWindowTextLengthW(g_dvOut);
    if (len > 150000) { SetWindowTextW(g_dvOut, L""); len = 0; }
    SendMessageW(g_dvOut, EM_SETSEL, len, len);
    SendMessageW(g_dvOut, EM_REPLACESEL, FALSE, (LPARAM)o.c_str());
    SendMessageW(g_dvOut, EM_SCROLLCARET, 0, 0);
}
static std::string DvB64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '=';
    }
    return o;
}
struct DevJob { std::wstring cmd, cwd; bool ps; };
static DWORD WINAPI DevRunThread(LPVOID p) {
    DevJob* j = (DevJob*)p;
    std::wstring cl;
    if (j->ps) {   // PowerShell: команда передаётся в кодировке base64, поэтому кавычки и кириллица не ломаются
        std::wstring cwd;
        for (wchar_t c : j->cwd) { cwd += c; if (c == L'\'') cwd += c; }
        std::wstring script = L"[Console]::OutputEncoding=[Text.Encoding]::GetEncoding([Console]::OutputEncoding.CodePage); Set-Location -LiteralPath '" + cwd + L"'; " + j->cmd;
        std::string raw((const char*)script.data(), script.size() * sizeof(wchar_t));
        cl = L"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + Widen(DvB64(raw));
    } else {
        cl = L"cmd.exe /d /s /c \"cd /d \"" + j->cwd + L"\" && " + j->cmd + L"\"";
    }
    DWORD code = 0;
    std::string out = RunCapture(cl, 60000, &code);
    std::wstring w = Widen(out);
    if (code == (DWORD)-2) w += L"\n[таймаут 60 с: команда остановлена]";
    else w += L"\n[код возврата " + std::to_wstring((long)code) + L"]";
    HWND target = g_devWnd;
    if (target) PostMessageW(target, WM_DEV_OUT, 0, (LPARAM) new std::wstring(w)); else InterlockedExchange(&g_dvBusy, 0);
    delete j;
    return 0;
}
static bool DevCd(std::wstring arg) {
    arg = DvTrim(arg);
    if (arg.size() >= 2 && (arg.substr(0, 2) == L"/d" || arg.substr(0, 2) == L"/D")) arg = DvTrim(arg.substr(2));
    if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"') arg = arg.substr(1, arg.size() - 2);
    wchar_t old[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, old);
    SetCurrentDirectoryW(g_dvCwd.c_str());
    bool ok = arg.empty() ? true : SetCurrentDirectoryW(arg.c_str()) != 0;
    if (ok) { wchar_t nw[MAX_PATH] = {}; GetCurrentDirectoryW(MAX_PATH, nw); g_dvCwd = nw; }
    SetCurrentDirectoryW(old);
    return ok;
}
static std::wstring DvPrompt() { return g_dvCwd + (g_dvPs ? L" PS> " : L"> "); }
static void DevExec() {
    wchar_t buf[2048] = {};
    GetWindowTextW(g_dvIn, buf, 2047);
    std::wstring c = DvTrim(buf);
    if (c.empty()) return;
    if (g_dvBusy) { DevAppend(L"[занято: дождитесь окончания предыдущей команды]\n"); return; }
    SetWindowTextW(g_dvIn, L"");
    g_dvHist.push_back(c);
    g_dvHistPos = (int)g_dvHist.size();
    DevAppend(DvPrompt() + c + L"\n");
    std::wstring lc = DvLower(c);
    if (lc == L"cls" || lc == L"clear") { SetWindowTextW(g_dvOut, L""); return; }
    if (lc == L"exit") { ShowWindow(g_devWnd, SW_HIDE); return; }
    if (lc == L"cd") { DevAppend(g_dvCwd + L"\n"); return; }
    if (lc.size() > 3 && (lc.compare(0, 3, L"cd ") == 0) && lc.find(L"&&") == std::wstring::npos && lc.find(L';') == std::wstring::npos) {
        if (!DevCd(c.substr(3))) DevAppend(L"Системе не удаётся найти указанный путь.\n");
        return;
    }
    if (lc.size() == 2 && lc[1] == L':') { if (!DevCd(c)) DevAppend(L"Диск не найден.\n"); return; }
    g_dvLastCmd = c;
    g_dvLastOut.clear();
    InterlockedExchange(&g_dvBusy, 1);
    DevJob* j = new DevJob{c, g_dvCwd, g_dvPs};
    HANDLE th = CreateThread(nullptr, 0, DevRunThread, j, 0, nullptr);
    if (th) CloseHandle(th); else { delete j; InterlockedExchange(&g_dvBusy, 0); DevAppend(L"[не удалось запустить поток]\n"); }
}
static void DevAskAi() {
    wchar_t buf[2048] = {};
    GetWindowTextW(g_dvIn, buf, 2047);
    std::wstring typed = DvTrim(buf), p;
    int s = (int)SendMessageW(g_dvList, LB_GETCURSEL, 0, 0);
    if (!typed.empty()) {
        p = L"Я программист или сисадмин. Помоги с задачей или командой: " + typed + L". Если нужен код или скрипт, напиши его целиком; если это про защиту, давай только законные, оборонительные и учебные варианты.";
    } else if (!g_dvLastCmd.empty()) {
        std::wstring tail = g_dvLastOut;
        if (tail.size() > 1500) tail = tail.substr(tail.size() - 1500);
        p = L"Я в терминале разработчика. Команда: " + g_dvLastCmd + L"\nВывод:\n" + tail + L"\nОбъясни коротко, что это значит и что делать дальше.";
    } else if (s >= 0 && s < (int)g_kbView.size()) {
        const KbE& e = g_kb[g_kbView[s]];
        p = L"Объясни команду и когда её применять: " + e.cmd + L" (" + e.desc + L")";
    } else {
        DevAppend(L"[введите вопрос или команду в строку терминала и нажмите ИИ]\n");
        return;
    }
    SubmitText(Narrow(p));
    DevAppend(L"[отправлено ИИ, ответ в главном окне FLEY AI]\n");
}
static LRESULT CALLBACK DevInProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR && (w == L'\r' || w == L'\n')) return 0;
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) { PostMessageW(g_devWnd, WM_COMMAND, DV_RUN, 0); return 0; }
        if (w == VK_UP || w == VK_DOWN) {
            if (!g_dvHist.empty()) {
                g_dvHistPos += (w == VK_UP) ? -1 : 1;
                if (g_dvHistPos < 0) g_dvHistPos = 0;
                if (g_dvHistPos >= (int)g_dvHist.size()) { g_dvHistPos = (int)g_dvHist.size(); SetWindowTextW(h, L""); }
                else SetWindowTextW(h, g_dvHist[g_dvHistPos].c_str());
                SendMessageW(h, EM_SETSEL, 0x7fff, 0x7fff);
            }
            return 0;
        }
    }
    return CallWindowProcW(g_dvOldIn, h, m, w, l);
}
static void DevLayout(int W, int H) {
    int m = 10, lw = (int)(W * 0.44), x0 = lw + m, rw = W - x0 - m;
    int bh = 30, bw = 104, gap = 6;
    MoveWindow(g_dvCat, m, m, lw - m, bh, TRUE);
    MoveWindow(g_dvSearch, m, m + bh + 6, lw - m, bh, TRUE);
    int ly = m + 2 * bh + 12;
    MoveWindow(g_dvList, m, ly, lw - m, std::max(40, H - ly - m), TRUE);
    MoveWindow(g_dvDetail, x0, m, rw, 52, TRUE);
    int by = H - m - bh;
    MoveWindow(g_dvOut, x0, m + 58, rw, std::max(40, by - (m + 58) - 8), TRUE);
    int bx = x0 + rw - 4 * bw - 3 * gap;
    if (bx < x0 + 120) bx = x0 + 120;
    MoveWindow(g_dvIn, x0, by, std::max(60, bx - x0 - gap), bh, TRUE);
    MoveWindow(g_dvRun, bx, by, bw, bh, TRUE);
    MoveWindow(g_dvShell, bx + (bw + gap), by, bw, bh, TRUE);
    MoveWindow(g_dvAi, bx + 2 * (bw + gap), by, bw, bh, TRUE);
    MoveWindow(g_dvClr, bx + 3 * (bw + gap), by, bw, bh, TRUE);
}
static HWND DvMk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, DWORD ex, int id) {
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, p, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (g_dvFont) SendMessageW(c, WM_SETFONT, (WPARAM)g_dvFont, TRUE);
    return c;
}
static LRESULT CALLBACK DevProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            g_devWnd = h;
            g_dvBr = CreateSolidBrush(RGB(3, 18, 8));
            g_dvFont = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            g_dvCat = DvMk(h, L"BUTTON", L"КАТЕГОРИЯ: ВСЕ", BS_OWNERDRAW, 0, DV_CAT);
            g_dvSearch = DvMk(h, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, DV_SEARCH);
            SendMessageW(g_dvSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Поиск: nmap, git rebase, порты, docker...");
            g_dvList = DvMk(h, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE, DV_LIST);
            SendMessageW(g_dvList, LB_SETHORIZONTALEXTENT, 3000, 0);
            g_dvDetail = DvMk(h, L"STATIC", L"", SS_LEFT, 0, DV_DETAIL);
            g_dvOut = DvMk(h, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE, DV_OUT);
            SendMessageW(g_dvOut, EM_LIMITTEXT, 0, 0);
            g_dvIn = DvMk(h, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, DV_IN);
            SendMessageW(g_dvIn, EM_SETCUEBANNER, TRUE, (LPARAM)L"Команда или вопрос к ИИ, Enter - выполнить");
            g_dvOldIn = (WNDPROC)SetWindowLongPtrW(g_dvIn, GWLP_WNDPROC, (LONG_PTR)DevInProc);
            g_dvRun = DvMk(h, L"BUTTON", L"ВЫПОЛНИТЬ", BS_OWNERDRAW, 0, DV_RUN);
            g_dvShell = DvMk(h, L"BUTTON", L"ОБОЛОЧКА: CMD", BS_OWNERDRAW, 0, DV_SHELL);
            g_dvAi = DvMk(h, L"BUTTON", L"СПРОСИТЬ ИИ", BS_OWNERDRAW, 0, DV_AI);
            g_dvClr = DvMk(h, L"BUTTON", L"ОЧИСТИТЬ", BS_OWNERDRAW, 0, DV_CLR);
            wchar_t up[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
            g_dvCwd = (n > 0 && n < MAX_PATH) ? up : L"C:\\";
            g_dvReady = true;
            DevFilter();
            DevAppend(L"FLEY DEV // терминал разработчика. База знаний слева, поиск по любому слову.\n"
                      L"cls - очистить, cd - сменить папку, exit - скрыть окно. Стрелки вверх/вниз - история.\n"
                      L"Работайте только со своими системами или с разрешения владельца.\n");
            return 0;
        }
        case WM_SIZE:
            if (g_dvReady && w != SIZE_MINIMIZED) DevLayout(LOWORD(l), HIWORD(l));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mi = (MINMAXINFO*)l;
            mi->ptMinTrackSize.x = 760; mi->ptMinTrackSize.y = 440;
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(w), code = HIWORD(w);
            if (id == DV_SEARCH && code == EN_CHANGE) DevFilter();
            else if (id == DV_LIST && code == LBN_SELCHANGE) DevShowSel();
            else if (id == DV_LIST && code == LBN_DBLCLK) {
                int s = (int)SendMessageW(g_dvList, LB_GETCURSEL, 0, 0);
                if (s >= 0 && s < (int)g_kbView.size()) {
                    SetWindowTextW(g_dvIn, g_kb[g_kbView[s]].cmd.c_str());
                    SetFocus(g_dvIn);
                    SendMessageW(g_dvIn, EM_SETSEL, 0x7fff, 0x7fff);
                }
            }
            else if (id == DV_CAT) { g_dvCatIdx = (g_dvCatIdx + 1) % ((int)g_kbCats.size() + 1); DevFilter(); }
            else if (id == DV_RUN) DevExec();
            else if (id == DV_SHELL) { g_dvPs = !g_dvPs; SetWindowTextW(g_dvShell, g_dvPs ? L"ОБОЛОЧКА: PS" : L"ОБОЛОЧКА: CMD"); }
            else if (id == DV_AI) DevAskAi();
            else if (id == DV_CLR) SetWindowTextW(g_dvOut, L"");
            return 0;
        }
        case WM_DEV_OUT: {
            std::wstring* t = (std::wstring*)l;
            if (t) {
                g_dvLastOut = *t;
                DevAppend(*t + L"\n");
                delete t;
            }
            InterlockedExchange(&g_dvBusy, 0);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)l;
            if (d->CtlType != ODT_BUTTON) break;
            bool down = (d->itemState & ODS_SELECTED) != 0;
            HBRUSH bg = CreateSolidBrush(down ? RGB(10, 70, 30) : RGB(4, 34, 16));
            FillRect(d->hDC, &d->rcItem, bg);
            DeleteObject(bg);
            HBRUSH fr = CreateSolidBrush(RGB(40, 255, 120));
            FrameRect(d->hDC, &d->rcItem, fr);
            DeleteObject(fr);
            wchar_t t[128] = {};
            GetWindowTextW(d->hwndItem, t, 127);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, RGB(170, 255, 200));
            HGDIOBJ of = SelectObject(d->hDC, g_dvFont);
            DrawTextW(d->hDC, t, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(d->hDC, of);
            return TRUE;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w;
            SetTextColor(dc, RGB(120, 255, 160));
            SetBkColor(dc, RGB(3, 18, 8));
            return (LRESULT)g_dvBr;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)w, &rc, g_dvBr);
            return 1;
        }
        case WM_SETFOCUS:
            if (g_dvIn) SetFocus(g_dvIn);
            return 0;
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);   // окно не уничтожается: терминал и история остаются
            return 0;
        case WM_DESTROY:
            g_dvReady = false;
            g_devWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void DevOpen(const std::string& query) {
    DevLoadKb();
    if (!g_devWnd) {
        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = DevProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"FleyAiDev";
            wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
            RegisterClassW(&wc);
            reg = true;
        }
        CreateWindowExW(0, L"FleyAiDev", L"FLEY DEV - терминал и база знаний", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                        CW_USEDEFAULT, CW_USEDEFAULT, S(1180), S(720), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_devWnd) return;
        RECT rc;
        GetClientRect(g_devWnd, &rc);
        DevLayout(rc.right, rc.bottom);
    }
    if (!query.empty()) SetWindowTextW(g_dvSearch, Widen(query).c_str());
    ShowWindow(g_devWnd, IsIconic(g_devWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_devWnd);
    SetFocus(g_dvIn);
}
static void DevHandle(WPARAM w, LPARAM l) {
    std::string* q = (std::string*)l;
    if (w == 1) DevOpen(q ? *q : std::string());
    else if (w == 3 && g_devWnd) ShowWindow(g_devWnd, SW_HIDE);
    delete q;
}
// действия для ИИ: dev | запрос - открыть окно и найти в базе; dev_close - скрыть окно
static ActResult ActDev(const Action& a) {
    DevLoadKb();
    std::string q = a.args.empty() ? "" : a.args[0];
    PostMessageW(g_hwnd, WM_APP_DEV, 1, (LPARAM) new std::string(q));
    if (q.empty()) return Ok("Окно разработчика открыто: терминал и база знаний из " + std::to_string(g_kb.size()) + " записей.");
    std::vector<int> r = DevMatch(Widen(q), L"");
    if (r.empty()) return Ok("В базе FLEY DEV по запросу «" + q + "» ничего не нашлось; окно открыто, можно искать вручную.", true);
    std::string msg = "Найдено в базе FLEY DEV по запросу «" + q + "» (всего " + std::to_string(r.size()) + "): ";
    for (size_t i = 0; i < r.size() && i < 8; ++i) msg += Narrow(g_kb[r[i]].cmd) + " - " + Narrow(g_kb[r[i]].desc) + "; ";
    return Ok(msg, true);
}
static ActResult ActDevClose() {
    PostMessageW(g_hwnd, WM_APP_DEV, 3, 0);
    return Ok("Окно разработчика скрыто.");
}
