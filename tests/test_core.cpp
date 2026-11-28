#include <cassert>
#include <iostream>
#include "../src/core.hpp"
using namespace fley;

#define CHECK(x) do { if(!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(0)

int main() {
    // JSON: ответ чата, юникод, суррогатные пары, escape
    Json j;
    CHECK(json_parse(R"({"choices":[{"message":{"role":"assistant","content":"Привет \u041c\u0438\u0440 \ud83d\ude00\n\"ok\""}}],"n":1.5e1,"x":null,"t":true})", j));
    CHECK(j["choices"].at(0)["message"]["content"].str() == "Привет Мир \xF0\x9F\x98\x80\n\"ok\"");
    CHECK(j["n"].n == 15.0);
    CHECK(j["t"].b);
    CHECK(j["missing"]["deep"].str().empty());
    CHECK(!json_parse("{\"a\":", j));
    CHECK(!json_parse("{\"a\" 1}", j));
    CHECK(json_escape("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001");
    Json back;  // эскейп -> парс = идентичность
    std::string src = "строка \"с\" кавычками\nи\tтабом";
    CHECK(json_parse("{\"s\":\"" + json_escape(src) + "\"}", back) && back["s"].str() == src);

    // .env
    auto env = parse_env("\xEF\xBB\xBF# c\r\nOPENAI_API_KEY=\"sk-or-v1-abc\"\r\nFLEY_VOICE = echo \r\nBAD\r\n");
    CHECK(env["OPENAI_API_KEY"] == "sk-or-v1-abc");
    CHECK(env["FLEY_VOICE"] == "echo");
    Settings s = make_settings(env);
    CHECK(s.openrouter && s.host == "openrouter.ai" && s.base_path == "/api/v1");
    CHECK(s.stt == "openai/whisper-large-v3" && s.voice == "echo" && s.silence == 1.2);
    Settings o = make_settings(parse_env("OPENAI_API_KEY=sk-proj-1\nFLEY_SILENCE_SECONDS=3"));
    CHECK(!o.openrouter && o.host == "api.openai.com" && o.base_path == "/v1" && o.silence == 3.0);
    Settings e0 = make_settings(parse_env(""));
    CHECK(e0.key.empty());

    // Groq
    Settings g = make_settings(parse_env("GROQ_API_KEY=gsk_abc"));
    CHECK(g.groq && !g.openrouter && g.host == "api.groq.com" && g.base_path == "/openai/v1");
    CHECK(g.model == "openai/gpt-oss-120b" && g.stt == "whisper-large-v3" && g.windows_tts);
    CHECK(!make_settings(parse_env("OPENAI_API_KEY=sk-x")).windows_tts);
    CHECK(make_settings(parse_env("GROQ_API_KEY=gsk_abc\nFLEY_TTS_MODEL=Windows")).windows_tts);
    CHECK(has_cyrillic("Привет") && !has_cyrillic("Hello"));

    // WAV
    std::string wav = make_wav(std::string(100, 'x'), 24000);
    CHECK(wav.size() == 144 && wav.substr(0, 4) == "RIFF" && wav.substr(8, 4) == "WAVE");
    CHECK((unsigned char)wav[24] == (24000 & 0xFF) && (unsigned char)wav[40] == 100);
    CHECK(parse_rate("audio/pcm;rate=24000;channels=1") == 24000 && parse_rate("audio/mpeg") == 24000);

    // multipart
    Multipart mp; mp.field("model", "m"); mp.file("file", "a.wav", "audio/wav", "DATA");
    std::string mb = mp.finish();
    CHECK(mb.find("name=\"model\"\r\n\r\nm\r\n") != std::string::npos);
    CHECK(mb.find("filename=\"a.wav\"") != std::string::npos && mb.rfind("--\r\n") == mb.size() - 4);

    // текст
    CHECK(clean_text("**Привет**, _мир_!\n\n\n\n# Заголовок") == "Привет, мир!\n\n Заголовок");
    CHECK(utf8_truncate("ааа", 3) == "а");  // 2 байта на символ -> граница
    CHECK(utf8_truncate("abc", 10) == "abc");


    // Endpointer: шум -> речь -> тишина => одно высказывание; щелчок отбрасывается
    {
        auto blockOf = [](short amp) { std::vector<short> b(800); for (size_t i = 0; i < b.size(); ++i) b[i] = (short)((i % 2) ? amp : -amp); return b; };
        Endpointer ep(0.9, 0);
        bool got = false; int fed = 0;
        for (int i = 0; i < 20; ++i) { auto b = blockOf(40); got |= ep.feed(b.data(), b.size()); ++fed; }
        CHECK(!got && !ep.in_speech());
        for (int i = 0; i < 20; ++i) { auto b = blockOf(2500); got |= ep.feed(b.data(), b.size()); }
        CHECK(!got && ep.in_speech());
        int quiet = 0;
        while (!got && quiet < 100) { auto b = blockOf(40); got = ep.feed(b.data(), b.size()); ++quiet; }
        CHECK(got && quiet >= 17 && quiet <= 19);  // 0.9 с = 18 блоков
        std::vector<short> u = ep.take();
        double secs = (double)u.size() / 16000.0;
        CHECK(secs > 1.0 && secs < 2.2);
        CHECK(!ep.in_speech());
        // второе высказывание сразу после первого, без калибровки
        got = false;
        for (int i = 0; i < 15; ++i) { auto b = blockOf(2500); got |= ep.feed(b.data(), b.size()); }
        while (!got && quiet < 300) { auto b = blockOf(40); got = ep.feed(b.data(), b.size()); ++quiet; }
        CHECK(got && ep.take().size() > 8000);
        // щелчок 100 мс - не речь
        Endpointer e2(0.9, 0); got = false;
        for (int i = 0; i < 10; ++i) { auto b = blockOf(40); e2.feed(b.data(), b.size()); }
        for (int i = 0; i < 2; ++i) { auto b = blockOf(3000); got |= e2.feed(b.data(), b.size()); }
        for (int i = 0; i < 60; ++i) { auto b = blockOf(40); got |= e2.feed(b.data(), b.size()); }
        CHECK(!got);
        // фиксированный порог
        Endpointer e3(0.9, 1500); auto lo = blockOf(1000);
        for (int i = 0; i < 40; ++i) e3.feed(lo.data(), lo.size());
        CHECK(!e3.in_speech());
    }
    CHECK(is_noise_transcript("") && is_noise_transcript(" . ") && is_noise_transcript("Субтитры сделал DimaTorzok") && !is_noise_transcript("открой ютуб"));
    CHECK(lower_utf8("Флей, ЁЖ ABC") == "флей, ёж abc" && has_wake_word("Эй, Флей, привет", "флей") && !has_wake_word("привет", "флей") && has_wake_word("любой", ""));
    CHECK(make_settings(parse_env("FLEY_WAKE=флей\nFLEY_AUTOSTART=1")).wake == "флей" && make_settings(parse_env("FLEY_AUTOSTART=1")).autostart);
    // голосовой эффект
    {
        std::string pcm;
        for (int i = 0; i < 8000; ++i) put16(pcm, (uint32_t)(uint16_t)(short)(8000 * std::sin(i * 0.1)));
        std::string w = make_wav(pcm, 22050), orig = w;
        CHECK(wav_apply_fx(w) && w.size() > orig.size() && w.substr(0, 4) == "RIFF" && w.substr(8, 4) == "WAVE" && w != orig);
        short mx = 0; for (size_t i = 44; i + 1 < w.size(); i += 2) mx = std::max<short>(mx, (short)std::abs((short)rd_le(w, i, 2)));
        CHECK(mx > 20000 && mx < 32767);
        std::string bad = "nope"; CHECK(!wav_apply_fx(bad));
    }
    // деление ответа на части
    {
        CHECK(split_speech("Коротко.").size() == 1);
        std::string longT = "Первое предложение вполне нормальной длины для речи. " + std::string(200, 'б') + " конец.";
        auto parts = split_speech(longT);
        CHECK(parts.size() == 2 && parts[0].back() == '.' && parts[0].size() < 100);
    }

    // VAD: 0.5с шума, 1с речи, потом тишина -> остановка через ~5с
    {
        Vad v(5.0, 0);
        int blocks = 0; Vad::Result r = Vad::Continue;
        while (r == Vad::Continue && blocks < 5000) {
            double rms = (blocks >= 12 && blocks < 32) ? 2500 : 60;
            r = v.feed(rms); ++blocks;
        }
        CHECK(r == Vad::Done && v.speech());
        double secs = blocks * 0.05;
        CHECK(secs > 6.4 && secs < 6.7);          // речь кончилась на 1.6с + 5с тишины
        CHECK(v.keep_blocks() == 32 + 10);         // 31-й блок последний с голосом
    }
    {   // тишина -> NoSpeech через 12с
        Vad v(5.0, 0); Vad::Result r = Vad::Continue; int b = 0;
        while (r == Vad::Continue && b < 5000) { r = v.feed(50); ++b; }
        CHECK(r == Vad::NoSpeech && !v.speech());
    }
    {   // фиксированный порог
        Vad v(5.0, 1000);
        for (int i = 0; i < 10; ++i) v.feed(50);
        v.feed(800); CHECK(!v.speech());
        v.feed(1200); CHECK(v.speech());
    }

    // ошибки
    CHECK(explain_error(429, R"({"error":{"message":"You exceeded your current quota"}})").find("закончились деньги") != std::string::npos);
    CHECK(explain_error(429, "{}").find("слишком много") != std::string::npos);
    CHECK(explain_error(402, "{}").find("кредитов") != std::string::npos);
    CHECK(explain_error(400, R"({"error":{"message":"bad model"}})").find("bad model") != std::string::npos);
    CHECK(explain_error(502, "not json").find("502") != std::string::npos);


    // действия на ПК
    {
        std::vector<Action> acts;
        std::string vis = extract_actions("Открываю ютуб. [[ACTION: youtube | кошки и собаки]] Готово [[ACTION:open_app|chrome]] [[не действие]]", acts);
        CHECK(acts.size() == 2);
        CHECK(acts[0].type == "youtube" && acts[0].args.size() == 1 && acts[0].args[0] == "кошки и собаки");
        CHECK(acts[1].type == "open_app" && acts[1].args[0] == "chrome");
        CHECK(vis.find("ACTION") == std::string::npos && vis.find("[[не действие]]") != std::string::npos);
        std::vector<Action> a2;
        CHECK(extract_actions("[[ACTION: sysinfo]]", a2).empty() && a2.size() == 1 && a2[0].args.empty());
        CHECK(url_encode("a b/я") == "a%20b%2F%D1%8F");
        CHECK(is_http_url("https://www.youtube.com/watch?v=abc") && !is_http_url("file:///c:/x") && !is_http_url("https://a b") && !is_http_url("javascript:alert(1)"));
        CHECK(is_dangerous_action("run") && is_dangerous_action("power") && !is_dangerous_action("youtube"));
        CHECK(safe_process_name("chrome") == "chrome.exe" && safe_process_name("a&b.exe") == "ab.exe" && safe_process_name("&&").empty());
        CHECK(is_protected_path("C:\\") && is_protected_path("c:\\Windows\\System32") && is_protected_path("C:\\Program Files\\X") && is_protected_path("c:\\users"));
        CHECK(!is_protected_path("D:\\games\\old.txt") && !is_protected_path("C:\\Users\\me\\Desktop\\a.txt"));
        CHECK(!make_settings(parse_env("")).confirm && make_settings(parse_env("FLEY_CONFIRM=1")).confirm);
        CHECK(make_settings(parse_env("")).voice_fx && !make_settings(parse_env("FLEY_VOICE_FX=0")).voice_fx);
        CHECK(make_settings(parse_env("FLEY_SILENCE_SECONDS=0.5")).silence == 0.5 && make_settings(parse_env("FLEY_SILENCE_SECONDS=0.1")).silence == 1.2 && make_settings(parse_env("FLEY_SILENCE_SECONDS=0.9")).silence == 1.2);
        CHECK(make_settings(parse_env("FLEY_PC_CONTROL=0\n")).pc == false && make_settings(parse_env("")).pc == true);
    }
    // новые настройки и режимы голоса
    {
        CHECK(make_settings(parse_env("")).fx_mode == 1 && make_settings(parse_env("FLEY_VOICE_FX=2")).fx_mode == 2 && make_settings(parse_env("FLEY_VOICE_FX=0")).fx_mode == 0);
        CHECK(make_settings(parse_env("")).key.empty() && make_settings(parse_env(""), "gsk_def").key == "gsk_def" && make_settings(parse_env("GROQ_API_KEY=gsk_own"), "gsk_def").key == "gsk_own");
        CHECK(make_settings(parse_env("")).window_mode == "full" && make_settings(parse_env("FLEY_WINDOW_MODE=mini")).window_mode == "mini" && make_settings(parse_env("FLEY_WINDOW_MODE=zzz")).window_mode == "full");
        std::string pcm;
        for (int i = 0; i < 12000; ++i) put16(pcm, (uint32_t)(uint16_t)(short)(9000 * std::sin(i * 0.08) + 3000 * std::sin(i * 0.31)));
        for (int mode = 1; mode <= 2; ++mode) {
            std::string w = make_wav(pcm, 22050), orig = w;
            CHECK(wav_apply_fx(w, mode) && w.size() > orig.size() && w != orig);
            short mx = 0; for (size_t i = 44; i + 1 < w.size(); i += 2) mx = std::max<short>(mx, (short)std::abs((short)rd_le(w, i, 2)));
            CHECK(mx > 20000 && mx < 32767);
        }
        std::string z = make_wav(pcm, 22050); CHECK(!wav_apply_fx(z, 0) && z == make_wav(pcm, 22050));
    }
    // запись в .env
    {
        std::string e = "# c\r\nGROQ_API_KEY=old\r\nFLEY_CALL_ME=\r\n";
        std::string r = set_env_value(e, "GROQ_API_KEY", "gsk_new");
        CHECK(r == "# c\r\nGROQ_API_KEY=gsk_new\r\nFLEY_CALL_ME=\r\n");
        CHECK(set_env_value(r, "FLEY_WAKE", "флей").find("FLEY_WAKE=флей\r\n") != std::string::npos);
        CHECK(set_env_value(r, "OPENAI_API_KEY", "", false) == r);
        CHECK(parse_env(set_env_value("", "A", "1"))["A"] == "1");
    }
    // быстрые локальные команды
    {
        Action a;
        CHECK(local_intent("Включи на ютубе Imagine Dragons.", a) && a.type == "youtube_play" && a.args[0] == "imagine dragons");
        CHECK(local_intent("Поставь музыку через ютуб", a) && a.type == "youtube_play" && a.args[0] == "популярная музыка");
        CHECK(local_intent("Флей, найди на ютубе обзор GTX 1650", a) && a.type == "youtube" && a.args[0] == "обзор gtx 1650");
        CHECK(local_intent("Открой ютуб", a) && a.type == "youtube" && a.args[0].empty());
        CHECK(local_intent("включи песню Believer", a) && a.type == "music" && a.args[0] == "believer" && a.args[1] == "play");  // с 0.9 «песню» идёт во встроенный плеер
        CHECK(local_intent("включи Imagine Dragons", a) && a.type == "youtube_play");
        CHECK(!local_intent("поставь будильник на семь утра", a));
        CHECK(local_intent("Открой Telegram.", a) && a.type == "open_app" && a.args[0] == "telegram");
        CHECK(local_intent("запусти блокнот пожалуйста", a) && a.type == "open_app" && a.args[0] == "блокнот");
        CHECK(local_intent("включи телеграм", a) && a.type == "open_app");
        CHECK(local_intent("открой папку загрузки", a) && a.type == "open_path" && a.args[0] == "загрузки");
        CHECK(local_intent("открой google.com", a) && a.type == "open_url" && a.args[0] == "https://google.com");
        CHECK(!local_intent("открой сайт гитхаб", a) && !local_intent("открой мне файл договор", a));
        CHECK(local_intent("сделай громче", a) && a.type == "volume" && a.args[0] == "up");
        CHECK(local_intent("Потише!", a) && a.type == "volume" && a.args[0] == "down");
        CHECK(local_intent("поставь громкость на 40", a) && a.type == "volume" && a.args[0] == "set" && a.args[1] == "40");
        CHECK(local_intent("Следующий трек.", a) && a.type == "media" && a.args[0] == "next");
        CHECK(local_intent("пауза", a) && a.type == "media" && a.args[0] == "play_pause");
        CHECK(local_intent("заблокируй компьютер", a) && a.type == "lock");
        CHECK(local_intent("запомни, что я люблю кофе без сахара", a) && a.type == "remember" && a.args[0] == "я люблю кофе без сахара");
        CHECK(local_intent("забудь кофе", a) && a.type == "forget" && a.args[0] == "кофе");
        CHECK(local_intent("загугли погода в москве", a) && a.type == "search" && a.args[0] == "погода в москве");
        CHECK(local_intent("найди в интернете рецепт борща", a) && a.type == "search" && a.args[0] == "рецепт борща");
        CHECK(local_intent("выключи микрофон", a) && a.type == "mic_off" && local_intent("пока", a) && a.type == "mic_off");
        CHECK(local_intent("отмени выключение", a) && a.type == "power" && a.args[0] == "cancel");
        CHECK(!local_intent("расскажи анекдот", a) && !local_intent("", a) && !local_intent("как дела", a) && !local_intent("пока я работаю включи что-нибудь", a));
        CHECK(local_ack(a = Action{"youtube_play", {"x"}}) == "Включаю.");
    }
    CHECK(parse_retry_seconds("Please try again in 7m12.5s.") == 433 && parse_retry_seconds("try again in 850ms") == 1 && parse_retry_seconds("try again in 1h2m3s") == 3723 && parse_retry_seconds("nope") == 0);
    CHECK(explain_error(429, "Rate limit ... tokens per day (TPD)").find("Дневной") != std::string::npos);
    // v0.8: усиление микрофона, глубина размышлений, голос
    {
        std::vector<short> q(8000);
        for (size_t i = 0; i < q.size(); ++i) q[i] = (short)(1500 * std::sin(i * 0.2) + 300);  // тихо + смещение
        pcm_enhance(q);
        int mx = 0; double mean = 0;
        for (short v : q) { mx = std::max(mx, (int)std::abs(v)); mean += v; }
        CHECK(mx > 9000 && std::fabs(mean / (double)q.size()) < 2000);
        std::vector<short> loud(8000);
        for (size_t i = 0; i < loud.size(); ++i) loud[i] = (short)(20000 * std::sin(i * 0.2));
        auto copy = loud; pcm_enhance(copy);
        int mx2 = 0; for (short v : copy) mx2 = std::max(mx2, (int)std::abs(v));
        CHECK(mx2 > 15000 && mx2 <= 32767);
        short g[3] = {1000, -1000, 30000}; pcm_gain(g, 3, 4.0);
        CHECK(g[0] == 4000 && g[1] == -4000 && g[2] > 22000 && g[2] <= 32767);
        CHECK(!make_settings(parse_env("")).denoise && make_settings(parse_env("FLEY_DENOISE=1")).denoise && make_settings(parse_env("FLEY_MIC_DEVICE=USB")).mic_device == "USB");
        CHECK(make_settings(parse_env("")).mic_gain == 2.0 && make_settings(parse_env("FLEY_MIC_GAIN=8")).mic_gain == 8.0 && make_settings(parse_env("FLEY_MIC_GAIN=99")).mic_gain == 2.0);
        {   // шумоподавление: речь-синус + шум; после очистки шум в паузах заметно тише
            std::vector<short> v(16000);
            unsigned seed = 1;
            auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((double)(seed >> 8) / 16777216.0 - 0.5) * 2.0; };
            for (size_t i = 0; i < v.size(); ++i) v[i] = (short)(600 * rnd() + ((i > 6000 && i < 12000) ? 3000 * std::sin(i * 0.3) : 0));
            auto orig = v; pcm_denoise(v);
            auto rmsr = [&](const std::vector<short>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (double)(b - a)); };
            CHECK(rmsr(v, 1000, 5000) < rmsr(orig, 1000, 5000) * 0.6);
            CHECK(rmsr(v, 7000, 11000) > rmsr(orig, 7000, 11000) * 0.7);
        }
        {
            Json a, b;
            CHECK(json_parse(R"({"text":"x","segments":[{"avg_logprob":-0.2,"no_speech_prob":0.01,"compression_ratio":1.2}]})", a) && !stt_low_confidence(a));
            CHECK(json_parse(R"({"text":"x","segments":[{"avg_logprob":-2.3,"no_speech_prob":0.1,"compression_ratio":1.0},{"avg_logprob":-1.2,"no_speech_prob":0.9,"compression_ratio":1.0}]})", b) && stt_low_confidence(b));
            CHECK(make_settings(parse_env("")).greeting && !make_settings(parse_env("FLEY_GREETING=0")).greeting);
        }
        CHECK(agc_next(4.0, 500, 1.0, 14.0) > 6.0 && agc_next(4.0, 9000, 1.0, 14.0) < 3.0 && agc_next(1.0, 9000, 1.0, 14.0) == 1.0 && agc_next(13.0, 100, 1.0, 14.0) <= 14.0);
        CHECK(choose_effort("включи свет", "low") == "low" && choose_effort("Объясни, почему небо синее", "low") == "medium");
        CHECK(make_settings(parse_env("")).stt_lang == "ru" && make_settings(parse_env("FLEY_STT_LANG=auto")).stt_lang.empty());
        CHECK(make_settings(parse_env("FLEY_VOICE_PITCH=0.85")).win_pitch > 0.95 && make_settings(parse_env("FLEY_VOICE_PITCH=0.9")).win_pitch == 0.9);
        std::string w = make_wav(std::string(16000 * 2, '\x10'), 22050);
        CHECK(wav_apply_fx(w, 1) && w.size() > 44);
    }
    {   // нейро-озвучка отдельным ключом OpenAI при чате на Groq
        Settings c = make_settings(parse_env("GROQ_API_KEY=gsk_a\nFLEY_TTS_API_KEY=sk-proj-x"));
        CHECK(c.groq && c.tts_cloud && !c.windows_tts && c.tts == "gpt-4o-mini-tts" && c.tts_host == "api.openai.com" && c.tts_path == "/v1" && c.voice == "ash");
        CHECK(make_settings(parse_env("GROQ_API_KEY=gsk_a")).windows_tts && !make_settings(parse_env("GROQ_API_KEY=gsk_a")).tts_cloud);
    }
    {   // гео: координаты, тайлы, голосовые фразы, разбор Nominatim
        double la = 0, lo = 0;
        CHECK(geo_parse_coords("41.3111, 69.2797", la, lo) && std::fabs(la - 41.3111) < 1e-9 && std::fabs(lo - 69.2797) < 1e-9);
        CHECK(geo_parse_coords("41,3111 69,2797", la, lo) && std::fabs(la - 41.3111) < 1e-9 && std::fabs(lo - 69.2797) < 1e-9);
        CHECK(geo_parse_coords("-33.8688 151.2093", la, lo) && la < -33.8 && lo > 151.2);
        CHECK(geo_parse_coords("41.3111 N 69.2797 E", la, lo) && la > 41.3 && lo > 69.2);
        CHECK(geo_parse_coords("33.87 S 151.21 E", la, lo) && la < -33.8 && lo > 151.2);
        CHECK(geo_parse_coords("69.2797 E 41.3111 N", la, lo) && std::fabs(la - 41.3111) < 1e-9);
        CHECK(geo_parse_coords("41\xC2\xB0" "18'40\"N 69\xC2\xB0" "16'47\"E", la, lo) && std::fabs(la - 41.31111) < 1e-4 && std::fabs(lo - 69.27972) < 1e-4);
        CHECK(geo_parse_coords("широта 41.31 долгота 69.24", la, lo) && std::fabs(lo - 69.24) < 1e-9);
        CHECK(!geo_parse_coords("улица Навои 5", la, lo));
        CHECK(!geo_parse_coords("95.0, 10.0", la, lo));
        CHECK(!geo_parse_coords("Ташкент", la, lo));
        double tx, ty;
        geo_tile(0.0, 0.0, 1, tx, ty);
        CHECK(std::fabs(tx - 1.0) < 1e-9 && std::fabs(ty - 1.0) < 1e-9);
        geo_tile(41.3111, 69.2797, 12, tx, ty);
        CHECK((int)tx == 2836 && (int)ty == 1530);
        CHECK(geo_zoom_for_span(0.0, 0.0) == 16 && geo_zoom_for_span(0.3, 0.4) >= 10 && geo_zoom_for_span(0.3, 0.4) <= 11 && geo_zoom_for_span(60, 90) <= 3 + 0);
        CHECK(geo_fmt_coord(41.3111, true) == "41.31110\xC2\xB0 N" && geo_fmt_coord(-69.5, false) == "69.50000\xC2\xB0 W");
        CHECK(geo_short_name("Ташкент, Узбекистан, мир") == "Ташкент, Узбекистан" && geo_short_name("Ташкент") == "Ташкент");
        GeoHit h;
        CHECK(geo_parse_nominatim(R"([{"lat":"41.3123","lon":"69.2787","display_name":"Ташкент, Узбекистан","addresstype":"city","boundingbox":["41.2","41.4","69.1","69.4"]}])", h));
        CHECK(h.ok && h.hasBox && std::fabs(h.lat - 41.3123) < 1e-9 && h.name == "Ташкент, Узбекистан" && h.type == "city" && std::fabs(h.east - 69.4) < 1e-9);
        GeoHit h2;
        CHECK(!geo_parse_nominatim("[]", h2) && !geo_parse_nominatim(R"({"error":"Unable to geocode"})", h2));
        CHECK(geo_parse_nominatim(R"({"lat":"1.5","lon":"2.5","display_name":"X"})", h2) && !h2.hasBox);
        Action a;
        CHECK(local_intent("Покажи на карте Ташкент", a) && a.type == "geo" && a.args[0] == "Ташкент");
        CHECK(local_intent("покажи Самарканд на карте", a) && a.type == "geo" && a.args[0] == "Самарканд");
        CHECK(local_intent("Флей, где находится Эйфелева башня?", a) && a.type == "geo" && a.args[0] == "Эйфелева башня");
        CHECK(local_intent("найди на карте улицу Навои 10, Ташкент", a) && a.type == "geo" && a.args[0] == "улицу Навои 10, Ташкент");
        CHECK(local_intent("покажи координаты 41.31, 69.24", a) && a.type == "geo" && a.args[0] == "41.31, 69.24");
        CHECK(local_intent("закрой карту", a) && a.type == "geo_close");
        CHECK(!(local_intent("где находится файл договор", a) && a.type == "geo"));
        CHECK(!(local_intent("сохрани на карте памяти", a) && a.type == "geo"));
    }

    // музыкальный плеер: разбор Audius / Deezer, формат времени, голосовые фразы
    {
        CHECK(music_fmt_time(0) == "0:00" && music_fmt_time(65) == "1:05" && music_fmt_time(3599) == "59:59" && music_fmt_time(-5) == "0:00");
        std::vector<MusicTrack> tr;
        CHECK(music_parse_audius(R"({"data":[
            {"id":"D7KyD","title":"Night Drive","duration":187,"user":{"name":"DJ Neon"},"is_streamable":true},
            {"id":"BAD ID","title":"Bad id","duration":10,"user":{"name":"x"}},
            {"id":"Zz9","title":"Locked","duration":99,"user":{"name":"y"},"access":{"stream":false}},
            {"id":"Q1","title":"Not streamable","duration":99,"user":{"name":"y"},"is_streamable":false},
            {"id":"Aa1","title":"","duration":5,"user":{"name":"z"}},
            {"id":"K2","title":" Sunrise ","duration":240,"user":{"name":"Лина"}}]})", tr, 10));
        CHECK(tr.size() == 2);
        CHECK(tr[0].id == "D7KyD" && tr[0].title == "Night Drive" && tr[0].artist == "DJ Neon" && tr[0].secs == 187 && tr[0].full && tr[0].source == "audius");
        CHECK(tr[0].url == "https://api.audius.co/v1/tracks/D7KyD/stream?app_name=FLEY_AI");
        CHECK(tr[1].title == "Sunrise" && tr[1].artist == "Лина");
        CHECK(!music_parse_audius("not json", tr, 10) && !music_parse_audius(R"({"error":"x"})", tr, 10));
        std::vector<MusicTrack> none;
        CHECK(music_parse_audius(R"({"data":[]})", none, 10) && none.empty());
        std::vector<MusicTrack> lim;
        CHECK(music_parse_audius(R"({"data":[{"id":"a1","title":"A","duration":1},{"id":"a2","title":"B","duration":1},{"id":"a3","title":"C","duration":1}]})", lim, 2) && lim.size() == 2);

        CHECK(music_parse_deezer(R"({"data":[
            {"id":3135556,"title":"Believer","duration":204,"preview":"https://cdnt-preview.dzcdn.net/api/1/1/a/b/c.mp3","artist":{"name":"Imagine Dragons"}},
            {"id":2,"title":"No preview","duration":100,"preview":"","artist":{"name":"X"}},
            {"id":3,"title":"Bad scheme","duration":100,"preview":"ftp://x/y.mp3","artist":{"name":"X"}}],"total":3})", tr, 25));
        CHECK(tr.size() == 3);
        CHECK(tr[2].id == "3135556" && tr[2].title == "Believer" && tr[2].artist == "Imagine Dragons" && tr[2].secs == 30 && !tr[2].full && tr[2].source == "deezer");
        CHECK(tr[2].url.compare(0, 8, "https://") == 0);
        std::vector<MusicTrack> dz;
        CHECK(!music_parse_deezer(R"({"error":{"type":"Exception","message":"Quota limit exceeded","code":4}})", dz, 25) && dz.empty());
        CHECK(!music_parse_deezer("<html>", dz, 25));

        Action a;
        CHECK(local_intent("Включи песню Believer Imagine Dragons", a) && a.type == "music" && a.args[0] == "believer imagine dragons" && a.args[1] == "play");
        CHECK(local_intent("Флей, поставь трек Numb", a) && a.type == "music" && a.args[0] == "numb" && a.args[1] == "play");
        CHECK(local_intent("включи музыку", a) && a.type == "music" && a.args[0].empty() && a.args[1] == "play");
        CHECK(local_intent("найди песню Бабек", a) && a.type == "music" && a.args[0] == "бабек" && a.args[1] == "list");
        CHECK(local_intent("покажи песни Coldplay", a) && a.type == "music" && a.args[0] == "coldplay" && a.args[1] == "list");
        CHECK(local_intent("включи в плеере Linkin Park", a) && a.type == "music" && a.args[0] == "linkin park" && a.args[1] == "play");
        CHECK(local_intent("открой плеер", a) && a.type == "music" && a.args[0].empty() && a.args[1] == "idle");
        CHECK(local_intent("открой музыкальный плеер", a) && a.type == "music" && a.args[1] == "idle");
        CHECK(local_intent("закрой плеер", a) && a.type == "music_close");
        CHECK(local_intent("включи следующую песню", a) && a.type == "media" && a.args[0] == "next");
        CHECK(local_intent("поставь музыку на паузу", a) && a.type == "media" && a.args[0] == "play_pause");
        // YouTube, клипы и приложения идут по-старому
        CHECK(local_intent("включи песню на ютубе Believer", a) && a.type == "youtube_play");
        CHECK(local_intent("включи клип Believer", a) && a.type == "youtube_play");
        CHECK(local_intent("включи спотифай", a) && a.type == "open_app");
        CHECK(local_intent("следующий трек", a) && a.type == "media" && a.args[0] == "next");
        CHECK(!(local_intent("где находится файл договор", a) && a.type == "music"));
        CHECK(local_ack(Action{"music", {"numb", "play"}}) == "Ищу музыку." && local_ack(Action{"music", {"", "idle"}}) == "Открываю плеер." && local_ack(Action{"music_close", {}}) == "Закрываю плеер.");
    }
    {   // зрение: base64, запрос с картинкой, координаты
        CHECK(base64_encode("") == "" && base64_encode("f") == "Zg==" && base64_encode("fo") == "Zm8=" && base64_encode("foo") == "Zm9v" && base64_encode("foobar") == "Zm9vYmFy");
        int ow, oh;
        fit_size(3840, 2160, 1400, ow, oh);
        CHECK(ow == 1400 && oh == 788);
        fit_size(800, 600, 1400, ow, oh);
        CHECK(ow == 800 && oh == 600);
        std::string vb = vision_body("m", "что \"тут\"?", "image/jpeg", "QUJD", 300, false);
        Json vj;
        CHECK(json_parse(vb, vj));
        CHECK(vj["model"].str() == "m" && vj["max_completion_tokens"].n == 300);
        CHECK(vj["messages"].at(0)["content"].at(0)["text"].str() == "что \"тут\"?");
        CHECK(vj["messages"].at(0)["content"].at(1)["image_url"]["url"].str() == "data:image/jpeg;base64,QUJD");
        CHECK(json_parse(vision_body("m", "q", "image/png", "QQ==", 10, true), vj) && vj["max_tokens"].n == 10);
        Json rj;
        CHECK(json_parse(R"({"choices":[{"message":{"content":"<think>размышляю</think> На экране Chrome."}}]})", rj));
        CHECK(vision_extract_text(rj) == "На экране Chrome.");
        CHECK(json_parse(R"({"choices":[{"message":{"content":[{"type":"text","text":"Привет"}]}}]})", rj) && vision_extract_text(rj) == "Привет");
        double fx, fy;
        CHECK(vision_parse_point(R"({"x": 500, "y": 250})", 1400, 788, fx, fy) && std::fabs(fx - 0.5) < 1e-9 && std::fabs(fy - 0.25) < 1e-9);
        CHECK(vision_parse_point("(1200, 700)", 1400, 788, fx, fy) && std::fabs(fx - 1200.0 / 1400) < 1e-9 && std::fabs(fy - 700.0 / 788) < 1e-9);
        CHECK(vision_parse_point("[100, 200, 300, 400]", 1400, 788, fx, fy) && std::fabs(fx - 0.2) < 1e-9 && std::fabs(fy - 0.3) < 1e-9);
        CHECK(vision_parse_point(R"({"y": 40, "x": 960})", 1400, 788, fx, fy) && std::fabs(fx - 0.96) < 1e-9 && std::fabs(fy - 0.04) < 1e-9);
        CHECK(vision_parse_point("x=0.25, y=0.75", 1400, 788, fx, fy) && std::fabs(fx - 0.25) < 1e-9);
        CHECK(!vision_parse_point("NOT FOUND", 1400, 788, fx, fy) && !vision_parse_point("не вижу такого", 1400, 788, fx, fy) && !vision_parse_point("(5000, 9000)", 1400, 788, fx, fy));
        CHECK(is_vision_action("see") && is_vision_action("click_on") && is_vision_action("scroll") && !is_vision_action("screenshot") && !is_vision_action("key"));
        CHECK(vision_model_rank("whisper-large-v3") == 0 && vision_model_rank("openai/gpt-oss-120b") == 0 && vision_model_rank("llama-3.3-70b-versatile") == 0 && vision_model_rank("meta-llama/llama-guard-4-12b") == 0);
        CHECK(vision_model_rank("meta-llama/llama-4-scout-17b-16e-instruct") > 0 && vision_model_rank("qwen/qwen3.8-27b") > 0 && vision_model_rank("x/some-vision-model") > vision_model_rank("gpt-4o-mini"));
        std::vector<std::string> cands = vision_model_candidates(R"({"data":[{"id":"whisper-large-v3"},{"id":"qwen/qwen3.9-30b"},{"id":"gpt-oss-20b"},{"id":"acme/vision-1"},{"id":"qwen/qwen3.8-27b"}]})", {"qwen/qwen3.8-27b"});
        CHECK(cands.size() == 2 && cands[0] == "acme/vision-1" && cands[1] == "qwen/qwen3.9-30b");
        CHECK(vision_model_candidates("мусор", {}).empty());
        UiElem ue;
        CHECK(ui_parse_line("Button|Сохранить|820|440|90|32\r\n", ue) && ue.type == "Button" && ue.name == "Сохранить" && ue.x == 820 && ue.h == 32);
        CHECK(!ui_parse_line("Button||1|1|1|1", ue) && !ui_parse_line("мусор", ue) && !ui_parse_line("Edit|x|1|1|0|5", ue));
        Settings vs = make_settings(parse_env("GROQ_API_KEY=gsk_x\nFLEY_SCREEN_AWARE=1"));
        CHECK(vs.vision_model == "qwen/qwen3.8-27b" && vs.screen_aware);
        CHECK(make_settings(parse_env("OPENAI_API_KEY=sk-proj-1")).vision_model == "gpt-4o-mini" && !make_settings(parse_env("OPENAI_API_KEY=sk-proj-1")).screen_aware);
        CHECK(make_settings(parse_env("OPENAI_API_KEY=sk-or-1\nFLEY_VISION_MODEL=my/vl")).vision_model == "my/vl");
    }
    {   // любые API-ключи: определение провайдера и голосовой ввод
        CHECK(detect_provider("sk-ant-api03-x") == "anthropic" && detect_provider("AIzaSyX") == "gemini" && detect_provider("xai-abc") == "xai");
        CHECK(detect_provider("sk-0123456789abcdef0123456789abcdef") == "deepseek" && detect_provider("sk-proj-abc") == "openai");
        CHECK(detect_provider("ghp_abc") == "github" && detect_provider("github_pat_abc") == "github");
        CHECK(guess_provider_by_shape(std::string(64, 'a')) == "together" && guess_provider_by_shape("12345678-1234-1234-1234-123456789012") == "sambanova");
        CHECK(guess_provider_by_shape(std::string(32, 'Z')) == "mistral" && guess_provider_by_shape(std::string(40, 'Z')) == "cohere" && guess_provider_by_shape("abc").empty());
        Settings g1 = make_settings(parse_env("GROQ_API_KEY=AIzaSyX"));
        CHECK(g1.provider == "gemini" && g1.stt_ok && g1.stt_chat && g1.stt == "gemini-2.5-flash");
        Settings x1 = make_settings(parse_env("GROQ_API_KEY=xai-abc"));
        CHECK(x1.provider == "xai" && x1.stt_ok && x1.stt_plain && x1.stt_endpoint == "/stt" && x1.stt == "grok-voice-transcribe-2.0");
        Settings f1 = make_settings(parse_env("GROQ_API_KEY=fw_abc"));
        CHECK(f1.provider == "fireworks" && f1.stt_ok && f1.stt_key == "fw_abc" && f1.stt_host == "audio-prod.api.fireworks.ai");
        Settings a1 = make_settings(parse_env("GROQ_API_KEY=sk-ant-x"));            // без запасного ключа голос недоступен, как раньше
        CHECK(a1.provider == "anthropic" && !a1.stt_ok && !a1.stt_fallback);
        Settings a2 = make_settings(parse_env("GROQ_API_KEY=sk-ant-x"), "gsk_def");  // с запасным ключом Groq речь распознаёт Groq
        CHECK(a2.stt_ok && a2.stt_fallback && a2.stt_groq && a2.stt_key == "gsk_def" && a2.stt_host == "api.groq.com" && a2.key == "sk-ant-x");
        CHECK(!make_settings(parse_env("GROQ_API_KEY=sk-ant-x\nFLEY_STT_FALLBACK=0"), "gsk_def").stt_ok);
        CHECK(make_settings(parse_env("GROQ_API_KEY=sk-ant-x\nFLEY_STT_API_KEY=gsk_own"), "gsk_def").stt_key == "gsk_own");
        CHECK(make_settings(parse_env("GROQ_API_KEY=gsk_a"), "gsk_def").stt_groq && !make_settings(parse_env("GROQ_API_KEY=gsk_a"), "gsk_def").stt_fallback);
        CHECK(make_settings(parse_env("GROQ_API_KEY=" + std::string(32, 'Q') + "\nFLEY_BASE_URL=https://x.test/v1")).provider == "custom");
        CHECK(find_provider("cohere") && find_provider("deepinfra") && find_provider("moonshot") && find_provider("github") && find_provider("openai") && find_provider("ollama"));
    }
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
