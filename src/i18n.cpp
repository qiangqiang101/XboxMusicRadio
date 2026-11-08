// The mod's in-game text, one row per language the game ships, as FH2ANNA does it: the language is the one the game's
// text is in (its string tables' manager, [game+0x3662990], has the code at +0x30: "EN", "DE", "CHT"...; Extra
// Options' language setting included). The Mods page and the X button's label are written before the game has one,
// so they use the one seen last (kept in XboxMusicRadio.ini), or on the first run Azimuth's language for the game.
// Czech and Hungarian stay English: the game's own text there is English, and its fonts may not have their letters.
// Menu names are the game's own in that language (ScreenTitles.str IDS_AudioOptions). WaitingFor goes to the radio
// display, which only draws plain ASCII (accents are dropped): languages in other scripts keep the English one there.
// The game's fonts (media\ui\Fonts.zip, .vfont: glyph count at 0x80, 28-byte records from 0xC0, code point first)
// only have the characters the game itself uses: Anthem_A-D (248 glyphs) for every language, plus a subset font for
// CHT, JP, KO and RU (fontsettings.xml). Every character here was checked against them; anything else draws as a
// box (no full-width brackets or colons, no 켬/켭). On/Off are the game's own words (Assists.str IDS_ABS_On/Off).
#include "i18n.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <vector>

namespace {

struct Language {
    const char* codes;  // the game's language codes it's for (space-separated)
    const char* text[static_cast<size_t>(Text::Count)];
};

// clang-format off
const Language kLanguages[] = {
    {"EN GB", {
        "Turns the radio's Xbox Music station on and plays your music on it: your own files (each folder in your Music "
        "folder is a playlist), Spotify, YouTube Music, NetEase Cloud Music or QQ Music, chosen in Audio Options.",
        "Turns the radio's Xbox Music station on. It plays your music files (each folder in your Windows Music folder is "
        "a playlist), Spotify or a music app on your PC: choose in Audio Options.",
        "Song names from tags",
        "On: titles and artists come from the music files' tags. Off: from the file names, as in \"Artist - Title.mp3\".",
        "Shuffle songs",
        "On: the playlist plays in a random order. Off: in folder order.",
        "Xbox Music Radio: next song",
        "Refresh Music",
        "All music",
        "Waiting for {0}",
        "Next Track",
        "Previous Track",
        "Playlist",
        "Horizon 2 Mix",
    }},
    {"DE", {
        "Schaltet den Radiosender Xbox Music ein und spielt darauf deine Musik: deine eigenen Dateien (jeder Ordner in "
        "deinem Musikordner ist eine Playlist), Spotify, YouTube Music, NetEase Cloud Music oder QQ Music, auszuwählen "
        "unter Audio.",
        "Schaltet den Radiosender Xbox Music ein. Er spielt deine Musikdateien (jeder Ordner in deinem Windows-Musikordner "
        "ist eine Playlist), Spotify oder eine Musik-App auf deinem PC: Auswahl unter Audio.",
        "Songnamen aus Tags",
        "Ein: Titel und Interpreten kommen aus den Tags der Musikdateien. Aus: aus den Dateinamen, wie in "
        "„Interpret - Titel.mp3“.",
        "Zufallswiedergabe",
        "Ein: Die Playlist läuft in zufälliger Reihenfolge. Aus: in der Reihenfolge des Ordners.",
        "Xbox Music Radio: nächster Song",
        "Musik aktualisieren",
        "Alle Titel",
        "Warte auf {0}",
        "Nächster Titel",
        "Vorheriger Titel",
        "Spielliste",
        "Horizon 2 Mix",
    }},
    {"FR", {
        "Active la station Xbox Music de la radio et y diffuse votre musique : vos propres fichiers (chaque dossier de "
        "votre dossier Musique est une playlist), Spotify, YouTube Music, NetEase Cloud Music ou QQ Music, au choix dans "
        "Audio.",
        "Active la station Xbox Music de la radio. Elle diffuse vos fichiers musicaux (chaque dossier de votre dossier "
        "Musique de Windows est une playlist), Spotify ou une application musicale de votre PC : faites votre choix dans "
        "Audio.",
        "Noms des titres depuis les tags",
        "Activé : les titres et les artistes proviennent des tags des fichiers. Désactivé : des noms de fichiers, comme "
        "dans « Artiste - Titre.mp3 ».",
        "Lecture aléatoire",
        "Activé : la playlist est lue dans un ordre aléatoire. Désactivé : dans l'ordre du dossier.",
        "Xbox Music Radio : titre suivant",
        "Actualiser la musique",
        "Toute la musique",
        "En attente de {0}",
        "Piste suivante",
        "Piste précédente",
        "Playlist",
        "Mix Horizon 2",
    }},
    {"ES MX", {
        "Activa la emisora Xbox Music de la radio y reproduce en ella tu música: tus propios archivos (cada carpeta de tu "
        "carpeta Música es una lista de reproducción), Spotify, YouTube Music, NetEase Cloud Music o QQ Music, a elegir "
        "en Sonido.",
        "Activa la emisora Xbox Music de la radio. Reproduce tus archivos de música (cada carpeta de tu carpeta Música de "
        "Windows es una lista de reproducción), Spotify o una aplicación de música de tu PC: elige en Sonido.",
        "Nombres de canciones desde etiquetas",
        "Activado: los títulos y artistas salen de las etiquetas de los archivos. Desactivado: de los nombres de archivo, "
        "como en «Artista - Título.mp3».",
        "Orden aleatorio",
        "Activado: la lista suena en orden aleatorio. Desactivado: en el orden de la carpeta.",
        "Xbox Music Radio: siguiente canción",
        "Actualizar música",
        "Toda la música",
        "Esperando a {0}",
        "Pista siguiente",
        "Pista anterior",
        "Lista de reproducción",
        "Mezcla de Horizon 2",
    }},
    {"IT", {
        "Attiva la stazione Xbox Music della radio e vi riproduce la tua musica: i tuoi file (ogni cartella nella "
        "cartella Musica è una playlist), Spotify, YouTube Music, NetEase Cloud Music o QQ Music, da scegliere in Audio.",
        "Attiva la stazione Xbox Music della radio. Riproduce i tuoi file musicali (ogni cartella nella cartella Musica di "
        "Windows è una playlist), Spotify o un'app musicale sul tuo PC: scegli in Audio.",
        "Nomi dei brani dai tag",
        "Attivato: titoli e artisti vengono dai tag dei file musicali. Disattivato: dai nomi dei file, come in "
        "\"Artista - Titolo.mp3\".",
        "Riproduzione casuale",
        "Attivato: la playlist viene riprodotta in ordine casuale. Disattivato: nell'ordine della cartella.",
        "Xbox Music Radio: brano successivo",
        "Aggiorna musica",
        "Tutta la musica",
        "In attesa di {0}",
        "Brano successivo",
        "Brano precedente",
        "Playlist",
        "Mix Horizon 2",
    }},
    {"JP", {
        "ラジオの Xbox Music 局をオンにして、あなたの音楽を流します。自分の音楽ファイル（ミュージック フォルダー内の各フォルダーが"
        "プレイリストになります）、Spotify、YouTube Music、NetEase Cloud Music、QQ Music から「オーディオ」で選べます。",
        "ラジオの Xbox Music 局をオンにします。音楽ファイル（Windows のミュージック フォルダー内の各フォルダーがプレイリストに"
        "なります）、Spotify、または PC の音楽アプリを再生します。「オーディオ」で選んでください。",
        "タグから曲名を取得",
        "On: 曲名とアーティスト名を音楽ファイルのタグから取得します。Off: 「アーティスト - 曲名.mp3」のようなファイル名から"
        "取得します。",
        "シャッフル再生",
        "On: プレイリストをランダムな順番で再生します。Off: フォルダーの順番で再生します。",
        "Xbox Music Radio: 次の曲",
        "音楽を更新",
        "すべての音楽",
        nullptr,
        "次の曲",
        "前の曲",
        "プレイリスト",
        "Horizon 2 ミックス",
    }},
    {"KO", {
        "라디오의 Xbox Music 채널을 켜고 내 음악을 재생합니다. 내 음악 파일(음악 폴더의 각 폴더가 재생 목록이 됩니다), Spotify, "
        "YouTube Music, NetEase Cloud Music 또는 QQ Music 중에서 오디오 메뉴에서 선택하세요.",
        "라디오의 Xbox Music 채널을 사용합니다. 음악 파일(Windows 음악 폴더의 각 폴더가 재생 목록이 됩니다), Spotify 또는 PC의 음악 "
        "앱을 재생합니다. 오디오 메뉴에서 선택하세요.",
        "태그에서 곡 이름 가져오기",
        "켜짐: 제목과 아티스트를 음악 파일의 태그에서 가져옵니다. 꺼짐: \"아티스트 - 제목.mp3\"처럼 파일 이름에서 가져옵니다.",
        "무작위 재생",
        "켜짐: 재생 목록을 무작위 순서로 재생합니다. 꺼짐: 폴더 순서대로 재생합니다.",
        "Xbox Music Radio: 다음 곡",
        "음악 새로 고침",
        "모든 음악",
        nullptr,
        "다음 곡",
        "이전 곡",
        "재생 목록",
        "Anthem",
    }},
    {"CHT", {
        "開啟電台的 Xbox Music 頻道並播放你的音樂：你自己的檔案 (音樂資料夾中的每個資料夾都是一個播放清單) 、Spotify、"
        "YouTube Music、NetEase Cloud Music 或 QQ Music，在「音訊」中選擇。",
        "開啟電台的 Xbox Music 頻道。它會播放你的音樂檔案 (Windows 音樂資料夾中的每個資料夾都是一個播放清單) 、Spotify 或電腦上"
        "的音樂軟體，在「音訊」中選擇。",
        "從標籤讀取歌曲名稱",
        "開啟：歌名和演出者取自音樂檔案的標籤。關閉：取自檔案名稱，例如「演出者 - 歌名.mp3」。",
        "隨機播放",
        "開啟：播放清單以隨機順序播放。關閉：依資料夾順序播放。",
        "Xbox Music Radio：下一首",
        "重新整理音樂",
        "所有音樂",
        nullptr,
        "下一首",
        "上一首",
        "播放清單",
        "Horizon 2 Mix",
    }},
    {"CZ-unused", {   // kept for when the game has Czech text: it shows English there
        "Zapne v rádiu stanici Xbox Music a hraje na ní vaši hudbu: vaše vlastní soubory (každá složka ve složce Hudba je "
        "playlist), Spotify, YouTube Music, NetEase Cloud Music nebo QQ Music, podle volby v nabídce Audio.",
        "Zapne v rádiu stanici Xbox Music. Hraje vaše hudební soubory (každá složka ve složce Hudba ve Windows je "
        "playlist), Spotify nebo hudební aplikaci ve vašem PC: vyberte v nabídce Audio.",
        "Názvy skladeb ze štítků",
        "Zapnuto: názvy a interpreti se berou ze štítků hudebních souborů. Vypnuto: z názvů souborů, např. "
        "„Interpret - Název.mp3“.",
        "Náhodné pořadí",
        "Zapnuto: playlist hraje v náhodném pořadí. Vypnuto: v pořadí ve složce.",
        "Xbox Music Radio: další skladba",
        "Obnovit hudbu",
        "Všechna hudba",
        "Čekám na {0}",
        "Další skladba",
        "Předchozí skladba",
        "Playlist",
        "Horizon 2 Mix",
    }},
    {"HU-unused", {   // likewise Hungarian
        "Bekapcsolja a rádió Xbox Music adóját, és a saját zenédet játssza rajta: a saját fájljaidat (a Zene mappád "
        "minden mappája egy lejátszási lista), a Spotifyt, a YouTube Musicot, a NetEase Cloud Musicot vagy a QQ Musicot, "
        "az Audio menüben választva.",
        "Bekapcsolja a rádió Xbox Music adóját. A zenefájljaidat játssza (a Windows Zene mappájának minden mappája egy "
        "lejátszási lista), a Spotifyt vagy egy zenei alkalmazást a gépedről: válassz az Audio menüben.",
        "Dalcímek a címkékből",
        "Be: a címek és az előadók a zenefájlok címkéiből származnak. Ki: a fájlnevekből, például „Előadó - Cím.mp3”.",
        "Véletlen sorrend",
        "Be: a lejátszási lista véletlen sorrendben szól. Ki: a mappa sorrendjében.",
        "Xbox Music Radio: következő dal",
        "Zene frissítése",
        "Minden zene",
        "Várakozás: {0}",
        "Következő szám",
        "Előző szám",
        "Lejátszási lista",
        "Horizon 2 Mix",
    }},
    {"PL", {
        "Włącza stację Xbox Music w radiu i odtwarza na niej Twoją muzykę: Twoje własne pliki (każdy folder w folderze "
        "Muzyka to playlista), Spotify, YouTube Music, NetEase Cloud Music lub QQ Music, do wyboru w menu Dźwięk.",
        "Włącza stację Xbox Music w radiu. Odtwarza Twoje pliki muzyczne (każdy folder w folderze Muzyka systemu Windows "
        "to playlista), Spotify lub aplikację muzyczną na Twoim PC: wybierz w menu Dźwięk.",
        "Nazwy utworów z tagów",
        "Wł.: tytuły i wykonawcy pochodzą z tagów plików muzycznych. Wył.: z nazw plików, np. „Wykonawca - Tytuł.mp3”.",
        "Losowa kolejność",
        "Wł.: playlista jest odtwarzana w losowej kolejności. Wył.: w kolejności folderu.",
        "Xbox Music Radio: następny utwór",
        "Odśwież muzykę",
        "Cała muzyka",
        "Czekam na {0}",
        "Następny utwór",
        "Poprzedni utwór",
        "Lista odtwarzania",
        "Horizon 2 Mix",
    }},
    {"RU", {
        "Включает радиостанцию Xbox Music и играет на ней вашу музыку: ваши собственные файлы (каждая папка в папке "
        "«Музыка» — это плейлист), Spotify, YouTube Music, NetEase Cloud Music или QQ Music, на выбор в меню "
        "«Настройка звука».",
        "Включает радиостанцию Xbox Music. Она играет ваши музыкальные файлы (каждая папка в папке «Музыка» Windows — это "
        "плейлист), Spotify или музыкальное приложение на вашем ПК: выберите в меню «Настройка звука».",
        "Названия песен из тегов",
        "Вкл.: названия и исполнители берутся из тегов музыкальных файлов. Выкл.: из имён файлов, например "
        "«Исполнитель - Название.mp3».",
        "Случайный порядок",
        "Вкл.: плейлист играет в случайном порядке. Выкл.: в порядке папки.",
        "Xbox Music Radio: следующая песня",
        "Обновить музыку",
        "Вся музыка",
        nullptr,
        "Следующий трек",
        "Предыдущий трек",
        "Плейлист",
        "Микс Horizon 2",
    }},
    {"NL", {
        "Zet de radiozender Xbox Music aan en speelt er je eigen muziek op: je eigen bestanden (elke map in je map Muziek "
        "is een afspeellijst), Spotify, YouTube Music, NetEase Cloud Music of QQ Music, te kiezen bij Geluid.",
        "Zet de radiozender Xbox Music aan. Die speelt je muziekbestanden (elke map in je Windows-map Muziek is een "
        "afspeellijst), Spotify of een muziek-app op je pc: kies bij Geluid.",
        "Nummernamen uit tags",
        "Aan: titels en artiesten komen uit de tags van de muziekbestanden. Uit: uit de bestandsnamen, zoals in "
        "'Artiest - Titel.mp3'.",
        "Willekeurige volgorde",
        "Aan: de afspeellijst speelt in willekeurige volgorde. Uit: in de volgorde van de map.",
        "Xbox Music Radio: volgend nummer",
        "Muziek vernieuwen",
        "Alle muziek",
        "Wachten op {0}",
        "Volgend nummer",
        "Vorig nummer",
        "Afspeellijst",
        "Anthem",
    }},
    {"BR", {
        "Ativa a estação Xbox Music do rádio e toca sua música nela: seus próprios arquivos (cada pasta na sua pasta "
        "Músicas é uma playlist), Spotify, YouTube Music, NetEase Cloud Music ou QQ Music, à escolha em Áudio.",
        "Ativa a estação Xbox Music do rádio. Ela toca seus arquivos de música (cada pasta na pasta Músicas do Windows é "
        "uma playlist), o Spotify ou um app de música do seu PC: escolha em Áudio.",
        "Nomes das músicas pelas tags",
        "Ativado: títulos e artistas vêm das tags dos arquivos de música. Desativado: dos nomes dos arquivos, como em "
        "\"Artista - Título.mp3\".",
        "Ordem aleatória",
        "Ativado: a playlist toca em ordem aleatória. Desativado: na ordem da pasta.",
        "Xbox Music Radio: próxima música",
        "Atualizar músicas",
        "Todas as músicas",
        "Aguardando {0}",
        "Próxima faixa",
        "Faixa anterior",
        "Lista de reprodução",
        "Horizon 2 Mix",
    }},
};
// clang-format on

std::atomic<const Language*> g_lang{&kLanguages[0]};
std::wstring g_ini;
uintptr_t g_base = 0;

const Language* find_code(const std::string& code) {
    for (const Language& lang : kLanguages) {
        const char* p = lang.codes;
        while (*p) {
            const char* end = std::strchr(p, ' ');
            size_t n        = end ? static_cast<size_t>(end - p) : std::strlen(p);
            if (n == code.size() && _strnicmp(p, code.c_str(), n) == 0) return &lang;
            p += n;
            while (*p == ' ') ++p;
        }
    }
    return &kLanguages[0];   // English, GB, CZ, HU, DEV, LOC
}

std::wstring widen(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

std::wstring ini_get(const std::wstring& ini, const std::wstring& section, const wchar_t* key) {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section.c_str(), key, L"", buf, 1024, ini.c_str());
    return buf;
}

// Azimuth's language for the running game: the [title-...] section whose folder holds the game's Mount folder,
// else the one the launcher last started.
std::wstring azimuth_language(const std::wstring& azimuth_dir, const std::wstring& game_dir) {
    std::wstring ini = azimuth_dir + L"\\data\\azimuth.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return L"";
    std::wstring mount = lower(game_dir);
    std::vector<wchar_t> names(32 * 1024);
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), ini.c_str());
    for (const wchar_t* p = names.data(); p < names.data() + n && *p; p += wcslen(p) + 1) {
        std::wstring section = p;
        if (section.rfind(L"title-", 0) != 0) continue;
        std::wstring folder = lower(ini_get(ini, section, L"folder"));
        if (!folder.empty() && !mount.empty() && (mount == folder || mount.rfind(folder + L"\\", 0) == 0))
            return ini_get(ini, section, L"language");
    }
    std::wstring game = ini_get(ini, L"launcher", L"game");
    return game.empty() ? L"" : ini_get(ini, L"title-" + game, L"language");
}

// A locale name ("de-DE", "zh-TW") as the game's language code (its own table is near game+0x2226CC8).
std::string locale_code(const std::wstring& locale) {
    std::wstring l = lower(locale);
    auto is        = [&](const wchar_t* p) {
        size_t n = wcslen(p);
        return l.compare(0, n, p) == 0 && (l.size() == n || l[n] == L'-');
    };
    if (l == L"zh-tw" || l == L"zh-hk" || l == L"zh-mo" || l.rfind(L"zh-hant", 0) == 0) return "CHT";
    if (is(L"es")) return l == L"es" || l == L"es-es" ? "ES" : "MX";
    const struct {
        const wchar_t* prefix;
        const char* code;
    } map[] = {
        {L"de", "DE"}, {L"fr", "FR"}, {L"it", "IT"}, {L"ja", "JP"}, {L"ko", "KO"},
        {L"pl", "PL"}, {L"ru", "RU"}, {L"nl", "NL"}, {L"pt", "BR"},
    };
    for (const auto& m : map)
        if (is(m.prefix)) return m.code;
    return "EN";
}

bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const uint8_t*>(p) + n <= static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

// The game's text language ("EN", "DE", "CHT"...), or "" while it has none yet.
std::string game_language() {
    if (!g_base) return "";
    auto* slot = reinterpret_cast<uint8_t* const*>(g_base + 0x3662990);
    if (!readable(slot, sizeof *slot)) return "";
    const uint8_t* manager = *slot;
    if (!manager || !readable(manager + 0x30, 32)) return "";
    const uint8_t* name = manager + 0x30;   // an MSVC std::string, in place (up to 15 chars)
    size_t count, capacity;
    std::memcpy(&count, name + 16, sizeof count);
    std::memcpy(&capacity, name + 24, sizeof capacity);
    if (capacity != 15 || count < 2 || count > 3 || name[count]) return "";
    return std::string(reinterpret_cast<const char*>(name), count);
}

void (*g_log)(const char*) = nullptr;

DWORD WINAPI watch(LPVOID) {
    for (int i = 0; i < 2 * 60 * 10; ++i) {   // twice a second until the game has one (its first seconds)
        std::string code = game_language();
        if (!code.empty()) {
            const Language* now = find_code(code);
            bool changed        = now != g_lang.exchange(now);
            WritePrivateProfileStringW(L"language", L"last", widen(code.c_str()).c_str(), g_ini.c_str());
            if (g_log) {
                char line[200];
                std::snprintf(line, sizeof line,
                              changed ? "the game's text is in %s: the Mods page and the X button follow from the next start"
                                      : "the game's text is in %s",
                              code.c_str());
                g_log(line);
            }
            return 0;
        }
        Sleep(500);
    }
    return 0;
}

} // namespace

std::string i18n_init(const std::wstring& ini, const std::wstring& azimuth_dir, const std::wstring& game_dir) {
    g_ini             = ini;
    std::wstring last = ini_get(ini, L"language", L"last");
    std::string code;
    for (wchar_t c : last) code += static_cast<char>(c);   // codes are ASCII
    if (code.empty()) code = locale_code(azimuth_language(azimuth_dir, game_dir));
    g_lang = find_code(code);
    return code + (last.empty() ? " (Azimuth's language for the game, until the game says)" : " (as last time)");
}

void i18n_watch(uintptr_t base, void (*log)(const char*)) {
    g_base = base;
    g_log  = log;
    if (HANDLE t = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr)) CloseHandle(t);
}

const char* tr(Text t) {
    const char* s = g_lang.load()->text[static_cast<size_t>(t)];
    return s ? s : kLanguages[0].text[static_cast<size_t>(t)];
}

std::wstring trw(Text t) { return widen(tr(t)); }

const char* tr_in(const char* code, Text t) {
    const char* s = find_code(code)->text[static_cast<size_t>(t)];
    return s ? s : kLanguages[0].text[static_cast<size_t>(t)];
}
