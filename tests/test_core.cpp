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
    CHECK(s.stt == "openai/whisper-large-v3" && s.voice == "echo" && s.silence == 0.9);
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
        CHECK(make_settings(parse_env("FLEY_SILENCE_SECONDS=0.5")).silence == 0.5 && make_settings(parse_env("FLEY_SILENCE_SECONDS=0.1")).silence == 0.9);
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
        CHECK(local_intent("включи песню Believer", a) && a.type == "youtube_play" && a.args[0] == "песню believer");
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
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
