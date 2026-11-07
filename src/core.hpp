// FLEY AI - переносимое ядро (без Windows API): JSON, .env, WAV, multipart, VAD, тексты ошибок.
// Тестируется отдельно: tests/test_core.cpp
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace fley {

// ---------------------------------------------------------------- JSON
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::string> keys;
    std::vector<Json> vals;

    const Json& operator[](const std::string& k) const {
        static const Json nul;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return vals[i];
        return nul;
    }
    const Json& at(size_t i) const {
        static const Json nul;
        return i < a.size() ? a[i] : nul;
    }
    std::string str() const { return t == Str ? s : std::string(); }
    bool is_obj() const { return t == Obj; }
};

inline void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : p_(text.data()), end_(text.data() + text.size()) {}
    bool parse(Json& out) {
        ws();
        if (!value(out, 0)) return false;
        ws();
        return true;
    }

private:
    const char* p_;
    const char* end_;

    void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if ((size_t)(end_ - p_) < n || std::strncmp(p_, w, n) != 0) return false;
        p_ += n;
        return true;
    }
    bool hex4(uint32_t& v) {
        if (end_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (p_ >= end_ || *p_ != '"') return false;
        ++p_;
        while (p_ < end_) {
            char c = *p_++;
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p_ >= end_) return false;
            char e = *p_++;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                        const char* save = p_;
                        p_ += 2;
                        uint32_t lo;
                        if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p_ = save;
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 64 || p_ >= end_) return false;
        char c = *p_;
        if (c == '{') {
            ++p_;
            out.t = Json::Obj;
            ws();
            if (p_ < end_ && *p_ == '}') { ++p_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p_ >= end_ || *p_ != ':') return false;
                ++p_;
                ws();
                Json v;
                if (!value(v, depth + 1)) return false;
                out.keys.push_back(k);
                out.vals.push_back(v);
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            out.t = Json::Arr;
            ws();
            if (p_ < end_ && *p_ == ']') { ++p_; return true; }
            for (;;) {
                ws();
                Json v;
                if (!value(v, depth + 1)) return false;
                out.a.push_back(v);
                ws();
                if (p_ < end_ && *p_ == ',') { ++p_; continue; }
                if (p_ < end_ && *p_ == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') { out.t = Json::Str; return string(out.s); }
        if (c == 't') { out.t = Json::Bool; out.b = true; return lit("true"); }
        if (c == 'f') { out.t = Json::Bool; out.b = false; return lit("false"); }
        if (c == 'n') { out.t = Json::Null; return lit("null"); }
        std::string num;
        while (p_ < end_ && std::strchr("+-0123456789.eE", *p_)) num += *p_++;
        if (num.empty()) return false;
        out.t = Json::Num;
        out.n = std::strtod(num.c_str(), nullptr);
        return true;
    }
};

inline bool json_parse(const std::string& text, Json& out) {
    out = Json();
    return JsonParser(text).parse(out);
}

inline std::string json_escape(const std::string& in) {
    std::string o;
    o.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else o += (char)c;
        }
    }
    return o;
}

// ---------------------------------------------------------------- текст
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string to_lower_ascii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// убирает markdown-символы, мешающие озвучке
inline std::string clean_text(const std::string& in) {
    std::string o;
    for (char c : in) if (c != '*' && c != '_' && c != '`' && c != '#' && c != '>') o += c;
    std::string r;
    int nl = 0;
    for (char c : o) {
        if (c == '\n') { if (++nl > 2) continue; }
        else if (c != '\r') nl = 0;
        r += c;
    }
    return trim(r);
}

// есть ли в тексте кириллица (для выбора русского голоса)
inline bool has_cyrillic(const std::string& s) {
    for (unsigned char c : s) if (c == 0xD0 || c == 0xD1) return true;
    return false;
}

// обрезка UTF-8 по границе символа
inline std::string utf8_truncate(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t n = max_bytes;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

// ---------------------------------------------------------------- .env и настройки
inline std::map<std::string, std::string> parse_env(const std::string& text) {
    std::map<std::string, std::string> m;
    std::string t = text;
    if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t.erase(0, 3);
    size_t pos = 0;
    while (pos <= t.size()) {
        size_t e = t.find('\n', pos);
        if (e == std::string::npos) e = t.size();
        std::string line = trim(t.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
            v = v.substr(1, v.size() - 2);
        m[k] = v;
    }
    return m;
}

struct Settings {
    std::string key, host, base_path, model, stt, tts, voice;
    std::string win_voice, call_me, effort;  // имя голоса Windows, обращение («сэр»), глубина размышлений
    double win_rate = 1.05, win_pitch = 0.85;  // скорость и высота голоса Windows (pitch < 1 = ниже, глубже)
    double silence = 0.9, threshold = 0.0;
    std::string wake;          // слово-активатор (например «флей»); пусто = отвечать на всё
    bool autostart = false;    // запуск вместе с Windows
    bool confirm = false;      // спрашивать подтверждение для опасных действий (по умолчанию нет)
    bool voice_fx = true;      // эффект «голос ИИ» поверх голоса Windows
    bool openrouter = false;
    bool groq = false;
    bool windows_tts = false;  // озвучка встроенным голосом Windows
    bool pc = true;            // управление компьютером голосом (FLEY_PC_CONTROL=0 отключает)
};

inline void split_url(std::string url, std::string& host, std::string& path) {
    size_t p = url.find("://");
    if (p != std::string::npos) url = url.substr(p + 3);
    size_t s = url.find('/');
    if (s == std::string::npos) { host = url; path.clear(); }
    else { host = url.substr(0, s); path = url.substr(s); }
    while (!path.empty() && path.back() == '/') path.pop_back();
}

inline Settings make_settings(const std::map<std::string, std::string>& env) {
    auto get = [&](const char* k, const std::string& def) {
        auto it = env.find(k);
        return (it != env.end() && !it->second.empty()) ? it->second : def;
    };
    Settings s;
    s.key = get("OPENAI_API_KEY", get("OPENROUTER_API_KEY", get("GROQ_API_KEY", "")));
    s.openrouter = s.key.compare(0, 6, "sk-or-") == 0;
    s.groq = s.key.compare(0, 4, "gsk_") == 0;
    std::string base, model, stt, tts;
    if (s.openrouter) {
        base = "https://openrouter.ai/api/v1";
        model = "openai/gpt-4o-mini";
        stt = "openai/whisper-large-v3";
        tts = "openai/gpt-4o-mini-tts-2025-12-15";
    } else if (s.groq) {
        base = "https://api.groq.com/openai/v1";
        model = "openai/gpt-oss-120b";
        stt = "whisper-large-v3";
        tts = "windows";
    } else {
        base = "https://api.openai.com/v1";
        model = "gpt-4o-mini";
        stt = "gpt-4o-mini-transcribe";
        tts = "gpt-4o-mini-tts";
    }
    std::string url = get("FLEY_BASE_URL", base);
    split_url(url, s.host, s.base_path);
    s.openrouter = s.openrouter || s.host.find("openrouter.ai") != std::string::npos;
    s.model = get("FLEY_MODEL", model);
    s.stt = get("FLEY_STT_MODEL", stt);
    s.tts = get("FLEY_TTS_MODEL", tts);
    s.windows_tts = to_lower_ascii(s.tts) == "windows";
    s.voice = get("FLEY_VOICE", "onyx");
    {
        std::string pc = to_lower_ascii(get("FLEY_PC_CONTROL", "1"));
        s.pc = !(pc == "0" || pc == "false" || pc == "no" || pc == "off");
    }
    s.silence = std::strtod(get("FLEY_SILENCE_SECONDS", "0.9").c_str(), nullptr);
    if (s.silence < 0.4 || s.silence > 5.0) s.silence = 0.9;
    {
        std::string c = to_lower_ascii(get("FLEY_CONFIRM", "0"));
        s.confirm = (c == "1" || c == "true" || c == "yes" || c == "on");
        std::string f = to_lower_ascii(get("FLEY_VOICE_FX", "1"));
        s.voice_fx = !(f == "0" || f == "false" || f == "no" || f == "off");
    }
    s.wake = get("FLEY_WAKE", "");
    {
        std::string a = to_lower_ascii(get("FLEY_AUTOSTART", "0"));
        s.autostart = (a == "1" || a == "true" || a == "yes" || a == "on");
    }
    s.win_voice = get("FLEY_WINDOWS_VOICE", "");
    s.call_me = get("FLEY_CALL_ME", "");
    s.effort = to_lower_ascii(get("FLEY_REASONING", "low"));
    if (s.effort != "low" && s.effort != "medium" && s.effort != "high") s.effort = "low";
    s.win_rate = std::strtod(get("FLEY_VOICE_RATE", "1.05").c_str(), nullptr);
    if (s.win_rate < 0.5 || s.win_rate > 2.5) s.win_rate = 1.0;
    s.win_pitch = std::strtod(get("FLEY_VOICE_PITCH", "0.85").c_str(), nullptr);
    if (s.win_pitch < 0.5 || s.win_pitch > 1.5) s.win_pitch = 0.85;
    s.threshold = std::strtod(get("FLEY_VOICE_THRESHOLD", "0").c_str(), nullptr);
    return s;
}

// ---------------------------------------------------------------- аудио
inline void put16(std::string& s, uint32_t v) { for (int i = 0; i < 2; ++i) s.push_back((char)((v >> (8 * i)) & 0xFF)); }
inline void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 0xFF)); }

inline std::string make_wav(const std::string& pcm, int rate, int channels = 1) {
    std::string w;
    w.reserve(44 + pcm.size());
    w += "RIFF"; put32(w, (uint32_t)(36 + pcm.size()));
    w += "WAVEfmt "; put32(w, 16); put16(w, 1); put16(w, (uint32_t)channels);
    put32(w, (uint32_t)rate); put32(w, (uint32_t)(rate * channels * 2)); put16(w, (uint32_t)(channels * 2)); put16(w, 16);
    w += "data"; put32(w, (uint32_t)pcm.size());
    w += pcm;
    return w;
}

inline int parse_rate(const std::string& content_type, int def = 24000) {
    size_t p = content_type.find("rate=");
    if (p == std::string::npos) return def;
    int r = std::atoi(content_type.c_str() + p + 5);
    return (r >= 8000 && r <= 96000) ? r : def;
}

inline double rms16(const short* d, size_t n) {
    if (!n) return 0;
    double acc = 0;
    for (size_t i = 0; i < n; ++i) acc += (double)d[i] * d[i];
    return std::sqrt(acc / (double)n);
}

// ---------------------------------------------------------------- multipart
struct Multipart {
    std::string boundary = "----FLEYBoundary7MA4YWxkTrZu0gW";
    std::string body;
    void field(const std::string& name, const std::string& value) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
    }
    void file(const std::string& name, const std::string& filename, const std::string& ctype, const std::string& data) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"; filename=\"" + filename +
                "\"\r\nContent-Type: " + ctype + "\r\n\r\n" + data + "\r\n";
    }
    std::string finish() const { return body + "--" + boundary + "--\r\n"; }
    std::string content_type() const { return "multipart/form-data; boundary=" + boundary; }
};

// ---------------------------------------------------------------- VAD (авто-стоп по тишине)
// Блоки по 50 мс. Первые 10 блоков (0.5 c) - калибровка шума.
class Vad {
public:
    enum Result { Continue, Done, NoSpeech };
    Vad(double silence_s, double fixed_threshold) : silence_(silence_s), fixed_(fixed_threshold) {}

    Result feed(double rms) {
        size_t idx = n_++;
        if (idx < kCalib) {
            noise_.push_back(rms);
            if (idx == kCalib - 1) {
                std::vector<double> v = noise_;
                std::sort(v.begin(), v.end());
                double p90 = v[(size_t)(0.9 * (double)(v.size() - 1))];
                thr_ = fixed_ > 0 ? fixed_ : std::min(std::max(300.0, p90 * 3.0), 2000.0);
            }
            return Continue;
        }
        if (rms > thr_) { speech_ = true; last_voice_ = (long)idx; }
        double t = (double)idx * kBlock;
        if (speech_ && quiet_s() >= silence_) return Done;
        if (!speech_ && t - kCalib * kBlock > kNoSpeechTimeout) return NoSpeech;
        if (t > kMaxRecord) return speech_ ? Done : NoSpeech;
        return Continue;
    }
    bool speech() const { return speech_; }
    double quiet_s() const { return speech_ ? (double)((long)n_ - 1 - last_voice_) * kBlock : 0.0; }
    int seconds_left() const { return (int)std::ceil(silence_ - quiet_s()); }
    // сколько блоков оставить в записи: до последнего голоса + 0.5 c хвоста
    size_t keep_blocks() const { return std::min(n_, (size_t)(last_voice_ + 1 + 10)); }

    static constexpr double kBlock = 0.05;
    static constexpr double kNoSpeechTimeout = 12.0;
    static constexpr double kMaxRecord = 90.0;
    static constexpr size_t kCalib = 10;

private:
    double silence_, fixed_, thr_ = 300.0;
    size_t n_ = 0;
    long last_voice_ = -1;
    bool speech_ = false;
    std::vector<double> noise_;
};


// ---------------------------------------------------------------- непрерывный детектор речи
// Микрофон слушает постоянно (как телефонный звонок). Блоки по 50 мс.
// Порог адаптивный: считается от уровня фонового шума, который отслеживается всё время.
// Высказывание начинается после 2 громких блоков подряд (с предзаписью 0.4 с) и заканчивается
// через silence_s секунд тишины. Короткие щелчки (меньше ~0.3 с речи) отбрасываются.
class Endpointer {
public:
    Endpointer(double silence_s, double fixed_threshold, double init_floor = 150.0)
        : silence_(silence_s), fixed_(fixed_threshold), floor_(init_floor) {}

    // true, если собрано целое высказывание (его можно забрать через take())
    bool feed(const short* d, size_t n) {
        if (!n) return false;
        block_ = n;
        double rms = rms16(d, n);
        double thr = threshold();
        bool voiced = rms > thr;
        if (!voiced && !in_speech_) floor_ = rms < floor_ ? floor_ * 0.6 + rms * 0.4 : floor_ * 0.98 + rms * 0.02;

        if (!in_speech_) {
            pre_.emplace_back(d, d + n);
            while (pre_.size() > kPre) pre_.pop_front();
            run_ = voiced ? run_ + 1 : 0;
            if (run_ >= 2) {
                for (auto& b : pre_) utt_.insert(utt_.end(), b.begin(), b.end());
                pre_.clear();
                in_speech_ = true;
                quiet_ = 0;
                blocks_ = 0;
                voiced_blocks_ = run_;
            }
            return false;
        }
        utt_.insert(utt_.end(), d, d + n);
        ++blocks_;
        if (voiced) { quiet_ = 0; ++voiced_blocks_; } else ++quiet_;
        bool end = (double)quiet_ * kBlock >= silence_ || (double)blocks_ * kBlock > kMaxUtt;
        if (!end) return false;
        in_speech_ = false;
        run_ = 0;
        if (voiced_blocks_ < kMinVoiced) { utt_.clear(); return false; }  // щелчок / шум
        // обрезаем лишнюю тишину в конце, оставляя 0.3 с
        size_t keep_quiet = 6;
        if (quiet_ > keep_quiet) {
            size_t cut = (quiet_ - keep_quiet) * block_;
            if (cut < utt_.size()) utt_.resize(utt_.size() - cut);
        }
        return true;
    }
    std::vector<short> take() { std::vector<short> r; r.swap(utt_); return r; }
    bool in_speech() const { return in_speech_; }
    double floor() const { return floor_; }
    double threshold() const {
        return fixed_ > 0 ? fixed_ : std::min(std::max(280.0, floor_ * 3.5), 2500.0);
    }

    static constexpr double kBlock = 0.05;
    static constexpr double kMaxUtt = 60.0;
    static constexpr size_t kPre = 8;
    static constexpr size_t kMinVoiced = 6;

private:
    double silence_, fixed_, floor_;
    size_t block_ = 800, run_ = 0, quiet_ = 0, blocks_ = 0, voiced_blocks_ = 0;
    bool in_speech_ = false;
    std::deque<std::vector<short>> pre_;
    std::vector<short> utt_;
};

// Нижний регистр для ASCII и кириллицы (UTF-8)
inline std::string lower_utf8(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xD0 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0x90 && d <= 0x9F) { o += (char)0xD0; o += (char)(d + 0x20); ++i; continue; }
            if (d >= 0xA0 && d <= 0xAF) { o += (char)0xD1; o += (char)(d - 0x20); ++i; continue; }
            if (d == 0x81) { o += (char)0xD1; o += (char)0x91; ++i; continue; }
            o += (char)c; continue;
        }
        o += (c < 0x80) ? (char)std::tolower(c) : (char)c;
    }
    return o;
}
inline bool has_wake_word(const std::string& text, const std::string& wake) {
    if (wake.empty()) return true;
    return lower_utf8(text).find(lower_utf8(wake)) != std::string::npos;
}

// Whisper на шуме/тишине любит «галлюцинировать» одни и те же фразы - такие ответы игнорируем
inline bool is_noise_transcript(const std::string& text) {
    std::string t = to_lower_ascii(trim(text));
    if (t.size() < 2) return true;
    static const char* junk[] = {"субтитры", "продолжение следует", "спасибо за просмотр", "thanks for watching",
                                 "thank you for watching", "subtitles by", "редактор субтитров", "корректор",
                                 "подписывайтесь на канал", "dimatorzok"};
    for (const char* j : junk)
        if (t.find(j) != std::string::npos && t.size() < 80) return true;
    bool letters = false;
    for (unsigned char c : t) if (isalnum(c) || c >= 0x80) { letters = true; break; }
    return !letters;
}

// ---------------------------------------------------------------- «голос ИИ»: обработка WAV
// Лёгкий металлический призвук (гребенчатый фильтр), едва заметная кольцевая модуляция и короткое
// эхо, как у компьютерного ассистента. Принимает WAV PCM16 (моно/стерео), возвращает новый WAV.
inline uint32_t rd_le(const std::string& s, size_t o, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes && o + (size_t)i < s.size(); ++i) v |= (uint32_t)(unsigned char)s[o + (size_t)i] << (8 * i);
    return v;
}
inline bool wav_apply_fx(std::string& wav) {
    if (wav.size() < 44 || wav.compare(0, 4, "RIFF") != 0 || wav.compare(8, 4, "WAVE") != 0) return false;
    size_t p = 12, data_off = 0, data_len = 0;
    int fmt = 0, ch = 0, bits = 0, rate = 0;
    while (p + 8 <= wav.size()) {
        std::string id = wav.substr(p, 4);
        size_t len = rd_le(wav, p + 4, 4);
        if (id == "fmt " && p + 8 + 16 <= wav.size()) {
            fmt = (int)rd_le(wav, p + 8, 2);
            ch = (int)rd_le(wav, p + 10, 2);
            rate = (int)rd_le(wav, p + 12, 4);
            bits = (int)rd_le(wav, p + 22, 2);
        } else if (id == "data") {
            data_off = p + 8;
            data_len = std::min(len, wav.size() - data_off);
            break;
        }
        p += 8 + len + (len & 1);
    }
    if (fmt != 1 || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 96000 || !data_off) return false;
    size_t frames = data_len / (size_t)(2 * ch);
    if (frames < 100) return false;
    size_t tail = (size_t)(rate * 0.35);
    size_t total = frames + tail;
    size_t d1 = (size_t)(rate * 0.0065), d2 = (size_t)(rate * 0.0123), d3 = (size_t)(rate * 0.062);
    const double kPi = 3.14159265358979323846;
    std::vector<std::vector<float>> out((size_t)ch, std::vector<float>(total, 0.0f));
    for (int c = 0; c < ch; ++c) {
        std::vector<float> x(total, 0.0f), y(total, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            short v = (short)rd_le(wav, data_off + (i * (size_t)ch + (size_t)c) * 2, 2);
            x[i] = (float)v / 32768.0f;
        }
        for (size_t i = 0; i < total; ++i) {
            float v = x[i];
            if (i >= d1) v += 0.30f * x[i - d1];
            if (i >= d2) v += 0.16f * x[i - d2];
            double ring = std::sin(2.0 * kPi * 85.0 * (double)i / (double)rate);
            y[i] = (float)(v * (1.0 - 0.14) + v * ring * 0.14);
        }
        std::vector<float> e(total, 0.0f);
        for (size_t i = 0; i < total; ++i) {
            e[i] = y[i] + (i >= d3 ? 0.32f * e[i - d3] : 0.0f);
            out[(size_t)c][i] = 0.80f * y[i] + 0.30f * e[i];
        }
    }
    float peak = 0.0f;
    for (auto& v : out) for (float f : v) peak = std::max(peak, std::fabs(f));
    float gain = peak > 0.0001f ? std::min(3.0f, 0.89f / peak) : 1.0f;
    std::string pcm;
    pcm.reserve(total * (size_t)ch * 2);
    for (size_t i = 0; i < total; ++i)
        for (int c = 0; c < ch; ++c) {
            float f = out[(size_t)c][i] * gain;
            f = std::max(-1.0f, std::min(1.0f, f));
            put16(pcm, (uint32_t)(uint16_t)(short)std::lround(f * 32767.0f));
        }
    wav = make_wav(pcm, rate, ch);
    return true;
}

// Делит ответ на 2 части (первое предложение + остальное), чтобы начать говорить раньше
inline std::vector<std::string> split_speech(const std::string& text) {
    std::vector<std::string> r;
    std::string t = trim(text);
    if (t.size() < 160) { r.push_back(t); return r; }
    size_t cut = std::string::npos;
    for (size_t i = 24; i < t.size() && i < 260; ++i) {
        char c = t[i];
        if ((c == '.' || c == '!' || c == '?') && i + 1 < t.size() && t[i + 1] == ' ') { cut = i + 1; break; }
    }
    if (cut == std::string::npos) { r.push_back(t); return r; }
    r.push_back(trim(t.substr(0, cut)));
    std::string rest = trim(t.substr(cut));
    if (!rest.empty()) r.push_back(rest);
    return r;
}

// ---------------------------------------------------------------- ошибки API по-русски
inline std::string error_message(const std::string& body) {
    Json j;
    if (!json_parse(body, j)) return trim(body.substr(0, 200));
    const Json& e = j["error"];
    if (e.t == Json::Str) return e.s;
    if (e.is_obj()) {
        std::string m = e["message"].str();
        if (!m.empty()) return m;
    }
    std::string m = j["message"].str();
    return m;
}

inline std::string explain_error(int status, const std::string& body) {
    std::string low = to_lower_ascii(body);
    std::string msg = error_message(body);
    bool money = low.find("quota") != std::string::npos || low.find("billing") != std::string::npos ||
                 low.find("insufficient") != std::string::npos || low.find("credits") != std::string::npos;
    switch (status) {
        case 401:
            return "Неверный API-ключ. Откройте файл .env (кнопка «Настройки») и вставьте правильный ключ.";
        case 402:
            return "Недостаточно кредитов. Пополните баланс: openrouter.ai/credits";
        case 403:
            return "Нет доступа к модели или региону (попробуйте VPN). Проверьте названия моделей в .env. " + msg;
        case 404:
            return "Модель не найдена. Проверьте названия моделей в .env. " + msg;
        case 429:
            if (money)
                return "Ошибка 429: на аккаунте закончились деньги или лимит. Пополните баланс "
                       "(OpenRouter: openrouter.ai/credits, OpenAI: platform.openai.com - Billing).";
            return "Ошибка 429: слишком много запросов. Подождите минуту и повторите.";
        case 400:
            return "Запрос отклонён: " + msg;
        default:
            break;
    }
    if (status >= 500) return "Сервер временно недоступен (" + std::to_string(status) + "). Попробуйте ещё раз. " + msg;
    return "Ошибка HTTP " + std::to_string(status) + ": " + msg;
}


// ------------------------------------------------------------------ действия на ПК (разбор тегов)
struct Action {
    std::string type;               // open_app, youtube, run ...
    std::vector<std::string> args;  // аргументы
};

// Вырезает теги [[ACTION: тип | арг | арг]] из ответа модели. Возвращает текст без тегов.
inline std::string extract_actions(const std::string& in, std::vector<Action>& out) {
    std::string text;
    size_t pos = 0;
    while (true) {
        size_t a = in.find("[[", pos);
        if (a == std::string::npos) { text += in.substr(pos); break; }
        size_t b = in.find("]]", a + 2);
        if (b == std::string::npos) { text += in.substr(pos); break; }
        std::string inner = trim(in.substr(a + 2, b - a - 2));
        if (inner.size() >= 7 && to_lower_ascii(inner.substr(0, 7)) == "action:") {
            text += in.substr(pos, a - pos);
            std::string body = inner.substr(7);
            Action act;
            size_t st = 0;
            bool first = true;
            while (true) {
                size_t bar = body.find('|', st);
                std::string part = trim(body.substr(st, bar == std::string::npos ? std::string::npos : bar - st));
                if (first) { act.type = to_lower_ascii(part); first = false; }
                else act.args.push_back(part);
                if (bar == std::string::npos) break;
                st = bar + 1;
            }
            if (!act.type.empty()) out.push_back(act);
        } else {
            text += in.substr(pos, b + 2 - pos);
        }
        pos = b + 2;
    }
    return trim(text);
}

// Процентное кодирование (UTF-8) для URL
inline std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

// Разрешаем только обычные http/https-ссылки без пробелов и кавычек
inline bool is_http_url(const std::string& u) {
    std::string l = to_lower_ascii(u);
    if (l.compare(0, 8, "https://") != 0 && l.compare(0, 7, "http://") != 0) return false;
    for (unsigned char c : u) if (c <= 32 || c == '"' || c == '<' || c == '>' || c == '^' || c == '|') return false;
    return u.size() < 2000;
}

// Действия, требующие подтверждения пользователя в окне
inline bool is_dangerous_action(const std::string& type) {
    return type == "run" || type == "delete" || type == "close_app" || type == "power";
}

// Имя процесса для taskkill: только безопасные символы, .exe в конце
inline std::string safe_process_name(std::string n) {
    n = trim(n);
    std::string o;
    for (unsigned char c : n) if (isalnum(c) || c == '.' || c == '_' || c == '-') o += (char)c;
    if (o.empty()) return "";
    std::string l = to_lower_ascii(o);
    if (l.size() < 4 || l.compare(l.size() - 4, 4, ".exe") != 0) o += ".exe";
    return o;
}

// Пути, которые нельзя удалять ни при каких обстоятельствах (сравнение в нижнем регистре, слэши вида \)
inline bool is_protected_path(std::string p) {
    p = to_lower_ascii(trim(p));
    for (char& c : p) if (c == '/') c = '\\';
    while (p.size() > 3 && p.back() == '\\') p.pop_back();
    if (p.size() <= 3) return true;  // корень диска
    static const char* roots[] = {"c:\\windows", "c:\\program files", "c:\\program files (x86)", "c:\\programdata", "c:\\users"};
    for (const char* r : roots) {
        std::string rs = r;
        if (p == rs) return true;
        if (rs == "c:\\windows" && p.compare(0, rs.size() + 1, rs + "\\") == 0) return true;
        if (rs.compare(0, 10, "c:\\program") == 0 && p.compare(0, rs.size() + 1, rs + "\\") == 0) return true;
    }
    return false;
}

}  // namespace fley
