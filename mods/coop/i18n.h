// Portable 5-language localization (English, Russian, Hungarian, Spanish, Simplified Chinese)
// and privacy IP address masking helpers (BUG-1572, BUG-1575).
#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace i18n {

enum class Lang : uint8_t {
    En = 0,
    Ru = 1,
    Hu = 2,
    Es = 3,
    Zh = 4,
    Count = 5,
};

inline Lang g_lang = Lang::En;
inline bool g_lang_auto = true;

inline Lang lang() { return g_lang; }
inline bool is_auto() { return g_lang_auto; }
inline bool is_ru() { return g_lang == Lang::Ru; }

inline void set_lang(Lang l, bool is_auto_mode = false)
{
    g_lang = (uint8_t)l < (uint8_t)Lang::Count ? l : Lang::En;
    g_lang_auto = is_auto_mode;
}

inline const char* lang_code(Lang l)
{
    switch (l) {
    case Lang::En: return "en";
    case Lang::Ru: return "ru";
    case Lang::Hu: return "hu";
    case Lang::Es: return "es";
    case Lang::Zh: return "zh";
    default: return "en";
    }
}

inline const char* lang_display(Lang l)
{
    switch (l) {
    case Lang::En: return "English";
    case Lang::Ru: return "Русский";
    case Lang::Hu: return "Magyar";
    case Lang::Es: return "Español";
    case Lang::Zh: return "中文";
    default: return "English";
    }
}

// Windows PRIMARYLANGID mapping without including <windows.h>
inline Lang lang_from_win_primary(uint16_t primary_id)
{
    switch (primary_id) {
    case 0x19: return Lang::Ru; // LANG_RUSSIAN
    case 0x0E: return Lang::Hu; // LANG_HUNGARIAN
    case 0x0A: return Lang::Es; // LANG_SPANISH
    case 0x04: return Lang::Zh; // LANG_CHINESE
    default: return Lang::En;
    }
}

inline bool parse_lang(const std::string& raw, Lang& out, bool& out_auto)
{
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw)
        if (!std::isspace(c))
            s.push_back((char)std::tolower(c));
    out_auto = false;
    if (s.empty() || s == "auto") {
        out_auto = true;
        out = Lang::En;
        return true;
    }
    if (s == "en" || s == "english") {
        out = Lang::En;
        return true;
    }
    if (s == "ru" || s == "russian") {
        out = Lang::Ru;
        return true;
    }
    if (s == "hu" || s == "hungarian" || s == "magyar") {
        out = Lang::Hu;
        return true;
    }
    if (s == "es" || s == "spanish" || s == "espanol") {
        out = Lang::Es;
        return true;
    }
    if (s == "zh" || s == "zh-cn" || s == "zh_cn" || s == "zh-tw" || s == "chinese" || s == "cn") {
        out = Lang::Zh;
        return true;
    }
    return false;
}

// BUG-1572: Mask IP/hostname while keeping port visible (e.g. "192.168.1.5:47810" -> "***.***.***.***:47810").
// Handles comma-separated host address lists ("192.168.1.5:47810,  10.0.0.2:47810" -> "***.***.***.***:47810").
inline std::string mask_ip_address(const std::string& raw)
{
    if (raw.empty())
        return {};
    // Take first entry if comma-separated so multiple local interfaces collapse to one clean mask
    size_t comma = raw.find(',');
    std::string first = comma == std::string::npos ? raw : raw.substr(0, comma);
    while (!first.empty() && std::isspace((unsigned char)first.front()))
        first.erase(first.begin());
    while (!first.empty() && std::isspace((unsigned char)first.back()))
        first.pop_back();
    if (first.empty())
        return {};
    // Preserve ":port" suffix if present (handles IPv4:port, hostname:port, [IPv6]:port)
    std::string port;
    size_t colon = first.rfind(':');
    size_t rbr = first.rfind(']');
    if (colon != std::string::npos && (rbr == std::string::npos || colon > rbr)) {
        std::string cand = first.substr(colon + 1);
        bool all_digits = !cand.empty();
        for (unsigned char c : cand)
            if (!std::isdigit(c))
                all_digits = false;
        if (all_digits)
            port = ":" + cand;
    }
    return std::string("***.***.***.***") + port;
}

struct Entry {
    const char* en;
    const char* ru;
    const char* hu;
    const char* es;
    const char* zh;
};

// Core UI, settings, Rivals, Space, and gameplay toast dictionary.
// Keyed by exact English source string.
inline const Entry kStrings[] = {
    // ---- Main menu & dialog titles ----
    {"Co-op", "Кооператив", "Kooperatív", "Cooperativo", "联机合作"},
    {"CO-OP", "КООПЕРАТИВ", "KOOPERATÍV", "COOPERATIVO", "联机合作"},
    {"CO-OP: NAME", "КООПЕРАТИВ: ИМЯ", "KOOPERATÍV: NÉV", "COOPERATIVO: NOMBRE", "联机: 昵称"},
    {"CO-OP: HOST", "КООПЕРАТИВ: СЕРВЕР", "KOOPERATÍV: SZERVER", "COOPERATIVO: ANFITRIÓN", "联机: 创建房间"},
    {"CO-OP: JOIN", "КООПЕРАТИВ: ВХОД", "KOOPERATÍV: CSATLAKOZÁS", "COOPERATIVO: UNIRSE", "联机: 加入房间"},
    {"CO-OP LOBBY", "КООПЕРАТИВ: ЛОББИ", "KOOPERATÍV: LOBBI", "SALA COOPERATIVA", "联机大厅"},
    {"CO-OP: SETTINGS", "КООПЕРАТИВ: НАСТРОЙКИ", "KOOPERATÍV: BEÁLLÍTÁSOK", "COOPERATIVO: AJUSTES", "联机: 设置"},
    {"CO-OP: SPACE", "КООПЕРАТИВ: КОСМОС", "KOOPERATÍV: ŰR", "COOPERATIVO: ESPACIO", "联机: 太空"},
    {"CO-OP: RIVALS", "КООПЕРАТИВ: СОПЕРНИКИ", "KOOPERATÍV: RIVÁLISOK", "COOPERATIVO: RIVALES", "联机: 对手模式"},

    // ---- Common buttons & navigation ----
    {"Report a problem", "Сообщить об ошибке", "Hiba jelentése", "Reportar problema", "报告问题"},
    {"< Back", "< Назад", "< Vissza", "< Volver", "< 返回"},
    {"< Lobby", "< Лобби", "< Lobbi", "< Sala", "< 返回大厅"},
    {"Continue", "Продолжить", "Tovább", "Continuar", "继续"},
    {"Change", "Изменить", "Módosítás", "Cambiar", "更改"},
    {"Show", "Показать", "Mutat", "Mostrar", "显示"},
    {"Hide", "Скрыть", "Elrejt", "Ocultar", "隐藏"},
    {"Send", "Отправить", "Küldés", "Enviar", "发送"},
    {"Don't send", "Не отправлять", "Ne küldje", "No enviar", "不发送"},
    {"Close session", "Закрыть сессию", "Munkamenet bezárása", "Cerrar sesión", "关闭房间"},
    {"Leave", "Выйти", "Kilépés", "Salir", "离开"},
    {"Settings", "Настройки", "Beállítások", "Ajustes", "设置"},
    {"Settings (%d)", "Настройки (%d)", "Beállítások (%d)", "Ajustes (%d)", "设置 (%d)"},
    {"Settings (%d changed)", "Настройки (изменено: %d)", "Beállítások (%d mód.)", "Ajustes (%d camb.)", "设置 (%d 项已改)"},
    {"Space >", "Космос >", "Űr >", "Espacio >", "太空 >"},
    {"Rivals >", "Соперники >", "Riválisok >", "Rivales >", "对手 >"},
    {"I'm ready", "Я готов", "Kész vagyok", "Estoy listo", "准备就绪"},
    {"Not ready", "Не готов", "Nem kész", "No listo", "取消准备"},
    {"Evolve now", "Эволюция", "Fejlődés most", "Evolucionar", "立即进化"},
    {"Start now", "Начать сейчас", "Indítás most", "Iniciar ahora", "立即开始"},
    {"Send my world", "Отправить мир", "Világ küldése", "Enviar mundo", "发送世界"},
    {"Continue ", "Продолжить ", "Folytatás: ", "Continuar ", "继续 "},
    {"More >", "Ещё >", "Több >", "Más >", "更多 >"},
    {"Next >", "Далее >", "Következő >", "Siguiente >", "下一个 >"},
    {"Cycle", "Сменить", "Váltás", "Cambiar", "切换"},
    {"Accept", "Принять", "Elfogad", "Aceptar", "接受"},
    {"Land", "Сесть", "Leszállás", "Aterrizar", "降落"},
    {"Jump there", "Прыгнуть", "Ugrás oda", "Saltar allí", "跃迁至此"},
    {"Fly there", "Лететь туда", "Repülés oda", "Volar allí", "飞往此处"},
    {"Ask the players for their reports", "Попросить у игроков их отчёты", "Jelentések kérése a játékosoktól", "Pedir informes a los jugadores", "向所有玩家请求诊断报告"},
    {"Give to ", "Отдать ", "Átadás: ", "Dar a ", "移交给 "},
    {"Give 10 to ", "Дать 10: ", "10 adása: ", "Dar 10 a ", "赠送10给 "},
    {"Jump to ", "К звезде: ", "Ugrás: ", "Saltar a ", "跃迁至 "},

    // ---- Connect / Host / Join steps ----
    {"What should other players call you?", "Как вас будут видеть другие игроки?", "Hogyan lássanak a többi játékosok?", "¿Cómo te verán los demás jugadores?", "其他玩家将如何称呼你？"},
    {"Your name:", "Ваше имя:", "Neved:", "Tu nombre:", "你的名字:"},
    {"Type a name first", "Сначала введите имя", "Előbb írj be egy nevet", "Escribe un nombre primero", "请先输入昵称"},
    {"Host a game", "Создать игру", "Játék létrehozása", "Crear partida", "创建房间"},
    {"Join a game", "Войти в игру", "Csatlakozás játékhoz", "Unirse a partida", "加入房间"},
    {"Playing as: ", "Имя игрока: ", "Játékos neve: ", "Jugando como: ", "当前昵称: "},
    {"Host a co-op session", "Создать совместную игру", "Kooperatív szerver indítása", "Crear sesión cooperativa", "创建联机房间"},
    {"UDP port:", "UDP-порт:", "UDP port:", "Puerto UDP:", "UDP 端口:"},
    {"Password (optional):", "Пароль (необязательно):", "Jelszó (opcionális):", "Contraseña (opcional):", "密码 (可选):"},
    {"Password:", "Пароль:", "Jelszó:", "Contraseña:", "密码:"},
    {"Start hosting", "Запустить сервер", "Szerver indítása", "Iniciar servidor", "启动房间"},
    {"Connect", "Подключиться", "Csatlakozás", "Conectar", "连接"},
    {"Or type the host's address", "Или введите адрес хоста", "Vagy írd be a szerver címét", "O escribe la dirección del anfitrión", "或输入房主地址"},
    {"Games on your network (click to join):", "Игры в вашей сети (нажмите, чтобы войти):", "Hálózati játékok (kattints a belépéshez):", "Partidas en tu red (clic para unirte):", "局域网房间 (点击加入):"},
    {"Games on your network: %d-%d of %d (click to join)", "Игры в сети: %d-%d из %d (нажмите, чтобы войти)", "Hálózati játékok: %d-%d / %d (kattints)", "Partidas en red: %d-%d de %d (clic)", "局域网房间: %d-%d / %d (点击加入)"},
    {"Recent servers (click to fill address):", "Недавние серверы (нажмите, чтобы подставить):", "Legutóbbi szerverek (kattints):", "Servidores recientes (clic para rellenar):", "最近服务器 (点击填入):"},
    {"Recent servers: %d-%d of %d (click to fill)", "Недавние серверы: %d-%d из %d (нажмите)", "Legutóbbi szerverek: %d-%d / %d", "Servidores recientes: %d-%d de %d", "最近服务器: %d-%d / %d"},
    {"[Locked] ", "[Пароль] ", "[Jelszó] ", "[Clave] ", "[需密码] "},
    {"Friends connect to: ", "Друзья подключаются к: ", "Barátok csatlakozása: ", "Amigos se conectan a: ", "好友连接地址: "},
    {"Host address: ", "Адрес хоста: ", "Szerver címe: ", "Dirección del anfitrión: ", "房主地址: "},
    {"Hidden (streamer mode)", "Скрыто (режим стримера)", "Rejtve (streamer mód)", "Oculto (modo streamer)", "已隐藏 (主播模式)"},
    {"Type the host's address, e.g. 192.168.1.5 or sporecoop.local", "Введите адрес хоста, например 192.168.1.5", "Írd be a szerver címét, pl. 192.168.1.5", "Escribe la dirección del anfitrión, ej. 192.168.1.5", "请输入房主地址，例如 192.168.1.5"},
    {"Left the session", "Вы вышли из игры", "Kiléptél a munkamenetből", "Saliste de la sesión", "已离开房间"},
    {"Sending the world to everyone...", "Мир отправляется всем...", "Világ küldése mindenkinek...", "Enviando el mundo a todos...", "正在向所有玩家发送世界..."},
    {"Loading ", "Загружается ", "Betöltés: ", "Cargando ", "正在加载 "},
    {"Start or load a game first: friends who join get it by themselves", "Сначала начните игру: вошедшие друзья получат её сами", "Előbb indíts vagy tölts be egy játékot", "Inicia o carga una partida primero", "请先开始或读取一个存档，好友加入时会自动同步"},
    {"Nobody has joined yet: friends who join get your world by themselves", "Пока никто не вошёл: друзья получат ваш мир сами", "Még senki sem csatlakozott", "Nadie se ha unido todavía", "尚无其他玩家加入，好友进入时会自动同步世界"},
    {"Not available in this version yet", "В этой версии пока недоступно", "Ebben a verzióban még nem elérhető", "Aún no disponible en esta versión", "当前版本暂不可用"},
    {"Sent: logs, a game summary, screenshots, crash files (paths masked)", "Отправится: логи, сводка, снимки, сбои (пути скрыты)", "Küldve: naplók, összefoglaló, képernyőképek", "Se envía: registros, resumen, capturas, fallos", "将发送: 日志、游戏摘要、截图和崩溃信息 (路径已脱敏)"},
    {"Sent: zip with logs, summary, screenshots, crash files (paths masked)", "Отправится .zip: логи, сводка, снимки, сбои (пути скрыты)", "Küldve (.zip): naplók, összefoglaló, képek, hibák", "Se envía (.zip): registros, resumen, capturas, fallos", "将打包发送 .zip: 日志、摘要、截图与崩溃信息 (路径已脱敏)"},
    {"Host UDP ", "Сервер UDP ", "Szerver UDP ", "Host UDP ", "房主 UDP "},
    {" pl.", " игр.", " ját.", " jug.", " 人"},

    // ---- Lobby table & columns ----
    {"Player", "Игрок", "Játékos", "Jugador", "玩家"},
    {"Stage", "Этап", "Szakasz", "Fase", "阶段"},
    {"Ping", "Пинг", "Ping", "Ping", "延迟"},
    {"Status", "Статус", "Állapot", "Estado", "状态"},
    {"Ready", "Готов", "Kész", "Listo", "已就绪"},
    {"Host", "Хост", "Szerver", "Anfitrión", "房主"},
    {"My role: ", "Моя роль: ", "Szerepem: ", "Mi rol: ", "我的职责: "},
    {"My creature: ", "Моё существо: ", "Lényem: ", "Mi criatura: ", "我的生物: "},
    {"My cell: ", "Моя клетка: ", "Sejtem: ", "Mi célula: ", "我的细胞: "},
    {"My ship: ", "Мой корабль: ", "Hajóm: ", "Mi nave: ", "我的飞船: "},
    {"Nothing here yet", "Пока ничего", "Még nincs itt semmi", "Nada aquí todavía", "暂无内容"},

    // ---- Settings page ----
    {"Session settings", "Настройки сессии", "Munkamenet beállításai", "Ajustes de la sesión", "房间设置"},
    {"The host decides these settings: you see them here.", "Эти настройки выбирает хост, здесь они видны вам.", "A szerver állítja be ezeket; itt megtekintheted.", "El anfitrión elige estos ajustes; aquí puedes verlos.", "这些设置由房主决定，你可以在此查看。"},
    {"Live settings change now; the others are locked in play.", "Настройки «на лету» меняются сразу, остальные только в меню галактики.", "Az «azonnali» beállítások játék közben is módosíthatók.", "Los ajustes «en vivo» cambian al instante; el resto solo en el menú.", "「即时」设置可在游戏中修改，其余仅可在星系菜单更改。"},
    {"Click to change. Live settings can change in play too.", "Нажмите, чтобы изменить. Настройки «на лету» меняются и в игре.", "Kattints a váltáshoz. Az «azonnali» játék közben is állítható.", "Clic para cambiar. Los ajustes «en vivo» también cambian en partida.", "点击切换。「即时」设置在游玩时也可随时修改。"},
    {"* differs from the default. Live: the host can change it in play.", "* отличается от стандарта. На лету: хост меняет и в игре.", "* eltér az alapértelmezettől. Azonnali: játék közben is változtatható.", "* distinto del valor inicial. En vivo: cambiable en partida.", "* 与默认值不同。即时: 房主可在游戏中随时修改。"},
    {"Only the host changes the settings", "Настройки меняет только хост", "Csak a szerver módosíthatja a beállításokat", "Solo el anfitrión cambia los ajustes", "只有房主可以修改设置"},
    {"Locked: a session setting changes only before the start", "Закрыто: меняется только в меню галактики до старта", "Zárolva: csak a játék indítása előtt módosítható", "Bloqueado: solo cambia antes de iniciar", "已锁定: 仅可在进入世界前修改"},
    {"Not changed: ", "Не изменено: ", "Nem változott: ", "Sin cambios: ", "未更改: "},
    {"locked", "закрыто", "zárolva", "bloqueado", "已锁定"},
    {"live", "на лету", "azonnali", "en vivo", "即时"},
    {"session", "до старта", "indítás előtt", "pre-partida", "开局前"},
    {"local", "локально", "helyi", "local", "本地"},
    {"On", "Вкл", "Be", "Sí", "开"},
    {"Off", "Выкл", "Ki", "No", "关"},
    {"Own", "Свои", "Saját", "Propio", "独立"},
    {"Host's", "Хоста", "Szerveré", "Anfitrión", "房主"},
    {"Auto", "Авто", "Auto", "Auto", "自动"},
    {"Default", "Обычный", "Alap", "Normal", "默认"},

    // ---- Display / local settings ----
    {"Display (this PC)", "Экран и интерфейс (этот ПК)", "Megjelenítés (ez a PC)", "Pantalla e interfaz (este PC)", "显示与界面 (本机)"},
    {"Frame rate cap", "Ограничение FPS", "FPS korlát", "Límite de FPS", "帧率上限"},
    {"Frame rate cap: ", "Ограничение FPS: ", "FPS korlát: ", "Límite de FPS: ", "帧率上限: "},
    {"Borderless window", "Окно без рамки", "Keret nélküli ablak", "Ventana sin bordes", "无边框窗口"},
    {"Borderless window: ", "Окно без рамки: ", "Keret nélküli ablak: ", "Ventana sin bordes: ", "无边框窗口: "},
    {"Third-person FOV", "Обзор от 3-го лица", "Külső nézet FOV", "Campo de visión (3ª pers.)", "第三人称视野 (FOV)"},
    {"Third-person FOV: ", "Обзор от 3-го лица: ", "Külső nézet FOV: ", "Campo de visión: ", "第三人称视野: "},
    {"Hide IP (streamer mode)", "Скрывать IP (режим стримера)", "IP elrejtése (streamer mód)", "Ocultar IP (modo streamer)", "隐藏 IP (主播模式)"},
    {"Hide IP (streamer mode): ", "Скрывать IP (режим стримера): ", "IP elrejtése (streamer mód): ", "Ocultar IP (modo streamer): ", "隐藏 IP (主播模式): "},
    {"Language", "Язык интерфейса", "Nyelv", "Idioma", "界面语言"},
    {"Language: ", "Язык интерфейса: ", "Nyelv: ", "Idioma: ", "界面语言: "},

    // ---- Setting groups ----
    {"All stages", "Все этапы", "Minden szakasz", "Todas las fases", "所有阶段"},
    {"Cell", "Клетка", "Sejt", "Célula", "细胞阶段"},
    {"Creature", "Существо", "Lény", "Criatura", "生物阶段"},
    {"Tribe", "Племя", "Törzs", "Tribu", "部落阶段"},
    {"Civilization", "Цивилизация", "Civilizáció", "Civilización", "文明阶段"},
    {"Space", "Космос", "Űr", "Espacio", "太空阶段"},
    {"Adventure", "Приключения", "Kalandok", "Aventuras", "银河冒险"},

    // ---- Setting value names ----
    {"own wallet", "свой кошелёк", "saját pénztárca", "propia bolsa", "独立钱包"},
    {"shared", "общий", "közös", "compartido", "共享"},
    {"own progress", "свой прогресс", "saját haladás", "progreso propio", "独立进度"},
    {"auto", "авто", "auto", "auto", "自动"},
    {"5 min", "5 мин", "5 perc", "5 min", "5 分钟"},
    {"10 min", "10 мин", "10 perc", "10 min", "10 分钟"},
    {"15 min", "15 мин", "15 perc", "15 min", "15 分钟"},
    {"30 min", "30 мин", "30 perc", "30 min", "30 分钟"},
    {"60 min", "60 мин", "60 perc", "60 min", "60 分钟"},

    // ---- Rivals UI & stage modes ----
    {"Rivals & Race", "Соперники и гонка", "Riválisok és verseny", "Rivales y carrera", "对手模式与竞赛"},
    {"Race, Relations & Chronicle", "Гонка, отношения и хроника", "Verseny, kapcsolatok és krónika", "Carrera, relaciones y crónica", "竞赛、外交与编年史"},
    {"Rivals Stage Modes", "Режимы этапов: Соперники", "Szakasz módok: Riválisok", "Modos de fase: Rivales", "阶段模式: 对手"},
    {"Stage modes preset", "Пресет режимов", "Mód előbeállítás", "Preajuste de modos", "模式预设"},
    {"All together", "Все вместе", "Mind együtt", "Todos juntos", "全部合作"},
    {"All rivals", "Все соперники", "Mind rivális", "Todos rivales", "全部对手"},
    {"Custom", "Свой", "Egyéni", "Personalizado", "自定义"},
    {"Together", "Вместе", "Együtt", "Juntos", "合作"},
    {"Rivals", "Соперники", "Riválisok", "Rivales", "对手"},
    {"Catch-up timer", "Таймер догона", "Felzárkózási idő", "Tiempo de alcance", "追赶倒计时"},
    {"+60 s", "+60 с", "+60 mp", "+60 s", "+60 秒"},
    {"Draft picks", "Выборов драфта", "Draft választások", "Turnos de draft", "选秀次数"},
    {"Base distance", "Дистанция баз", "Bázistávolság", "Distancia de bases", "基地距离"},
    {"Close", "Близко", "Közel", "Cerca", "近"},
    {"Middle", "Средне", "Közepes", "Media", "中"},
    {"Far", "Далеко", "Messze", "Lejos", "远"},
    {"RACE", "ГОНКА", "VERSENY", "CARRERA", "竞赛"},
    {"won! Catch-up", "победил! Догон", "nyert! Felzárkózás", "ganó! Alcance", "获胜！追赶"},
    {"Race progress", "Прогресс гонки", "Verseny állása", "Progreso de carrera", "竞赛进度"},
    {"Rivals: ", "Соперники: ", "Riválisok: ", "Rivales: ", "对手模式: "},
    {"Player relations", "Отношения игроков", "Játékos kapcsolatok", "Relaciones entre jugadores", "玩家外交关系"},
    {"Neutral", "Нейтралитет", "Semleges", "Neutral", "中立"},
    {"War", "Война", "Háború", "Guerra", "战争"},
    {"Ally", "Союз", "Szövetség", "Alianza", "同盟"},
    {"Declare war", "Война", "Háború", "Guerra", "宣战"},
    {"Offer peace", "Мир", "Béke", "Paz", "求和"},
    {"Propose peace", "Мир", "Béke", "Proponer paz", "求和"},
    {"Accept peace", "Принять мир", "Béke elfog.", "Aceptar paz", "接受和平"},
    {"Offer ally", "Союз", "Szövetség", "Alianza", "结盟"},
    {"Propose ally", "Союз", "Szövetség", "Proponer alianza", "结盟"},
    {"Accept ally", "Принять союз", "Szöv. elfog.", "Aceptar alianza", "接受同盟"},
    {"Break ally", "Разорвать", "Bontás", "Romper", "解除同盟"},
    {"Decline", "Отклонить", "Elutasít", "Rechazar", "拒绝"},
    {"ally offered", "предложен союз", "szövetség ajánlva", "alianza ofrecida", "收到结盟提议"},
    {"peace offered", "предложен мир", "béke ajánlva", "paz ofrecida", "收到停战提议"},
    {"Ally offer from", "Предложение союза:", "Szövetségi ajánlat:", "Oferta de alianza de", "结盟提议来自"},
    {"Peace offer from", "Предложение мира:", "Békeajánlat:", "Oferta de paz de", "停战提议来自"},
    {"Hostility:", "Вражда:", "Ellenséges:", "Hostilidad:", "敌对操作:"},
    {"Influence: not used in Cell stage", "Влияние: не используется на этапе Клетки", "Befolyás: nem használt a Sejt szakaszban", "Influencia: no se usa en fase Célula", "影响力: 细胞阶段不使用"},
    {"Turn", "Ссора", "Ellene", "Enemistar", "挑拨"},
    {"Smooth", "Мир", "Békít", "Apaciguar", "缓和"},
    {"Influence", "Влияние", "Befolyás", "Influencia", "影响力"},
    {"Buy +25", "Купить +25", "Vétel +25", "Comprar +25", "购买 +25"},
    {"Turn against", "Натравить", "Ellene fordít", "Poner en contra", "挑拨敌对"},
    {"Protect", "Защитить", "Védelem", "Proteger", "保护"},
    {"Smooth over", "Уладить", "Békítés", "Apaciguar", "缓和关系"},
    {"Chronicle", "Хроника", "Krónika", "Crónica", "编年史"},
    {"Draft", "Драфт", "Draft", "Draft", "选秀"},
    {"None", "Нет", "Nincs", "Ninguna", "无"},
    {"Food", "Еда", "Étel", "Comida", "食物"},
    {"Gift 25 food", "Дать 25 еды", "25 étel ajándék", "Dar 25 comida", "赠送25食物"},
    {"Starting Path (Mid-Game)", "Начальный путь (с середины игры)", "Kezdő út (játék közben)", "Ruta inicial (media partida)", "起始路线 (中途加入)"},
    {"Space Philosophy: ", "Философия в Космосе: ", "Űr filozófia: ", "Filosofía espacial: ", "太空哲学: "},

    // ---- Space UI & autosave ----
    {"Your galaxy together", "Галактика вместе", "Közös galaxis", "Tu galaxia juntos", "共享星系"},
    {" (autosave on)", " (автосохр.)", " (auto mentés)", " (autoguard.)", " (自动保存)"},
    {" switched to rivals mode: every player is an empire of its own with its own budget", ": соперники - своя империя и бюджет", ": riválisok - saját birodalom és pénz", ": rivales - cada uno su imperio y dinero", ": 对手模式 - 各自独立帝国与资金"},
    {" switched to allies mode: one empire again, the budgets are merged", ": союзники - общая империя и бюджет", ": szövetség - közös birodalom és pénz", ": aliados - un imperio y presupuesto común", ": 合作模式 - 共享帝国与资金"},
    {"Rivals mode goes on from the last session: every player is an empire of its own again", "Соперники: у каждого своя империя и бюджет", "Riválisok: mindenkinek saját birodalma van", "Rivales: cada jugador tiene su imperio", "对手模式继续: 每位玩家拥有独立帝国"},
    {"Rivals mode: your own empire starts with a budget of ", "Соперники: стартовый бюджет ", "Riválisok: kezdő keret ", "Rivales: presupuesto inicial ", "对手模式: 初始资金 "},
    {"Your badges and rank are back.", "Ваши значки и звание восстановлены.", "A jelvényeid és rangod visszatértek.", "Tus insignias y rango han vuelto.", "你的徽章与军衔已恢复。"},
    {"Your colonies are back in your empire.", "Ваши колонии снова в вашей империи.", "A kolóniáid visszatértek a birodalmadba.", "Tus colonias volvieron a tu imperio.", "你的殖民地已回到你的帝国。"},
    {"somewhere in space", "где-то в космосе", "valahol az űrben", "en algún lugar del espacio", "在太空某处"},
    {"flying in the galaxy", "летит по галактике", "a galaxisban repül", "volando por la galaxia", "正在星系中飞行"},
    {"flying near ", "летит у ", "közelében repül: ", "volando cerca de ", "正飞近 "},
    {"a star", "звезда", "egy csillag", "una estrella", "一颗恒星"},
    {"no spice", "без специй", "nincs fűszer", "sin especia", "无香料"},

    // ---- Tooltips ----
    {"Save your display name and continue", "Сохранить имя и продолжить", "Név mentése és folytatás", "Guardar tu nombre y continuar", "保存昵称并继续"},
    {"Change the name other players see", "Изменить имя, которое видят игроки", "Más játékosok által látott név módosítása", "Cambiar el nombre que ven los demás", "修改其他玩家看到的名字"},
    {"Host a co-op session in your world", "Создать совместную игру в своём мире", "Kooperatív munkamenet indítása", "Crear una sesión cooperativa", "在你的世界中创建联机房间"},
    {"Join a friend's co-op session", "Подключиться к совместной игре друга", "Csatlakozás barát játékához", "Unirse a la sesión de un amigo", "加入好友的联机房间"},
    {"Return to Host or Join", "Вернуться к выбору: создать или войти", "Vissza a választáshoz", "Volver a Crear o Unirse", "返回创建或加入界面"},
    {"Start hosting on this UDP port", "Открыть сервер на этом UDP-порту", "Szerver indítása ezen az UDP porton", "Iniciar servidor en este puerto UDP", "在此 UDP 端口启动房间"},
    {"Show or hide the session password", "Показать или скрыть пароль сессии", "Jelszó megjelenítése vagy elrejtése", "Mostrar u ocultar la contraseña", "显示或隐藏房间密码"},
    {"Connect to the host address", "Подключиться по указанному адресу", "Csatlakozás a szerver címéhez", "Conectar a la dirección del anfitrión", "连接到房主地址"},
    {"Show or hide the server password", "Показать или скрыть пароль сервера", "Szerver jelszó mutatása/elrejtése", "Mostrar u ocultar contraseña del servidor", "显示或隐藏服务器密码"},
    {"Show more games found on your network", "Показать другие игры в локальной сети", "További hálózati játékok mutatása", "Mostrar más partidas en tu red", "显示更多局域网房间"},
    {"Show more recently joined servers", "Показать другие недавние серверы", "További legutóbbi szerverek", "Mostrar más servidores recientes", "显示更多最近服务器"},
    {"Click to join this game on your network", "Нажмите, чтобы войти в эту сетевую игру", "Kattints a csatlakozáshoz", "Clic para unirte a esta partida", "点击加入此局域网房间"},
    {"Click to fill the host address field", "Нажмите, чтобы подставить адрес хоста", "Kattints a cím kitöltéséhez", "Clic para rellenar la dirección", "点击自动填入此房主地址"},
    {"Download and install the new SporeCoop version", "Скачать и установить новую версию SporeCoop", "Új SporeCoop verzió letöltése", "Descargar e instalar la nueva versión", "下载并安装新版 SporeCoop"},
    {"Save a zip with logs and diagnostics for bug reports", "Сохранить архив с логами для отчёта об ошибке", "Naplók mentése hibajelentéshez", "Guardar archivo zip con registros de error", "保存包含日志与诊断信息的压缩包"},
    {"View or change session rules and display options", "Правила сессии, язык, приватность и экран", "Szabályok, nyelv és megjelenítés", "Reglas, idioma, privacidad y pantalla", "查看或更改规则、语言与显示选项"},
    {"Mark yourself ready for stage transitions and votes", "Отметить готовность к переходам и голосованиям", "Kész jelzés szavazásokhoz és váltásokhoz", "Marcar listo para votaciones y cambios de fase", "标记已准备好进行投票或阶段切换"},
    {"Send your current world save to all joined players", "Сохранить и заново разослать мир всем игрокам", "Aktuális világ mentése és küldése mindenkinek", "Guardar y reenviar tu mundo a todos", "保存并将当前世界同步给所有玩家"},
    {"Close this session or disconnect from the host", "Закрыть сессию или отключиться от хоста", "Munkamenet bezárása vagy bontás", "Cerrar sesión o desconectar", "关闭房间或断开连接"},
    {"Ask all joined players to send their problem reports", "Попросить всех игроков прислать отчёты", "Jelentés kérése minden játékostól", "Pedir informes a todos los jugadores", "请求所有玩家提交诊断报告"},
    {"Send your chat message to everyone in the session", "Отправить сообщение в чат всем игрокам", "Üzenet küldése mindenkinek", "Enviar mensaje de chat a todos", "向房间内所有人发送消息"},
    {"Open the Space stage co-op status and actions", "Открыть окно совместного этапа Космос", "Űr szakasz állapot és műveletek", "Abrir estado y acciones del Espacio", "打开太空阶段联机状态与操作"},
    {"Open Rivals mode settings, race progress, relations, and influence", "Настройки Соперников, прогресс гонки, дипломатия и влияние", "Riválisok beállításai, verseny, diplomácia", "Ajustes de Rivales, carrera, relaciones e influencia", "打开对手模式设置、竞赛进度、外交与影响力"},
    {"Switch all stages to Together and restore Together default Settings", "Переключить все этапы на Вместе и вернуть стандартные настройки", "Minden szakasz Együtt módra állítása", "Cambiar todas las fases a Juntos", "将所有阶段切换为合作模式并恢复默认设置"},
    {"Switch all stages to Rivals and apply Rivals default Settings", "Переключить все этапы на Соперников и применить настройки соперников", "Minden szakasz Riválisok módra állítása", "Cambiar todas las fases a Rivales", "将所有阶段切换为对手模式并应用对手设置"},
    {"Toggle Together or Rivals mode for the next stage", "Переключить режим Вместе или Соперники для следующего этапа", "Következő szakasz módjának váltása", "Cambiar modo Juntos/Rivales para la siguiente fase", "切换下一阶段的合作/对手模式"},
    {"Pick your next player colour", "Выбрать следующий цвет игрока", "Következő játékosszín választása", "Elegir el siguiente color de jugador", "切换玩家颜色"},
    {"Choose a different creation or role", "Выбрать другое творение или роль", "Másik alkotás vagy szerep választása", "Elegir otra creación o rol", "选择其他造物或职责"},
    {"Choose a different creature from My Creations", "Выбрать другое существо из Моих творений", "Másik lény választása", "Elegir otra criatura de Mis creaciones", "从我的造物中选择其他生物"},
    {"Transfer the selected units or spice to this partner", "Передать выбранные юниты или специю напарнику", "Kijelölt egységek vagy fűszer átadása", "Transferir unidades o especia al compañero", "将选中的单位或香料移交给队友"},
    {"Pick another partner to receive the gift", "Выбрать другого напарника для передачи", "Másik partner választása", "Elegir otro compañero destinatario", "选择另一位接收队友"},
    {"Fly to your partner's star system", "Перелететь в звёздную систему напарника", "Repülés a partner csillagrendszerébe", "Volar al sistema estelar del compañero", "飞往队友所在的星系"},
    {"Include your logs and summary in the host's report", "Отправить ваши логи в общий отчёт хоста", "Naplók küldése a szerver jelentésébe", "Incluir tus registros en el informe", "将你的日志包含到房主的报告中"},
    {"Pack and send your logs (.zip) to the host's report", "Собрать и отправить архив (.zip) с логами в отчёт хоста", "Naplók (.zip) becsomagolása és küldése a szervernek", "Empaquetar y enviar tus registros (.zip) al anfitrión", "打包并发送包含日志的 .zip 文件给房主"},
    {"Decline sending your report this time", "Не отправлять ваш отчёт в этот раз", "Jelentés küldésének elutasítása", "No enviar tu informe esta vez", "本次不发送你的报告"},
    {"Return to the co-op lobby", "Вернуться в лобби кооператива", "Vissza a lobbiba", "Volver a la sala cooperativa", "返回联机大厅"},
    {"Close the co-op window (press F9 to reopen)", "Закрыть окно кооператива (F9 - открыть снова)", "Ablak bezárása (F9: újranyitás)", "Cerrar ventana (F9 para reabrir)", "关闭联机窗口 (按 F9 重新打开)"},
    {"On: hides IP addresses as ***.***.***.*** in the lobby, join list, and during vote popups (ideal for streams and recordings). Off: shows real IP addresses.",
     "Вкл: скрывает IP-адреса в виде ***.***.***.*** в лобби, списке серверов и при автооткрытии голосований (удобно для стримов и записей). Выкл: показываются реальные IP.",
     "Be: elrejti az IP-címeket ( ***.***.***.*** ) a lobbiban, szerverlistában és szavazásoknál (streameléshez ideális). Ki: valódi IP-k látszanak.",
     "Sí: oculta las IP como ***.***.***.*** en la sala, lista de servidores y votaciones (ideal para directos). No: muestra las IP reales.",
     "开: 在大厅、服务器列表和投票弹窗中将 IP 地址隐藏为 ***.***.***.*** (适合直播与录屏)。关: 显示真实 IP 地址。"},
    {"Switch the SporeCoop interface language between Auto (system language), English, Russian, Hungarian, Spanish, and Chinese. Saved in coop.ini.",
     "Переключает язык интерфейса SporeCoop: Авто (язык системы), English, Русский, Magyar, Español или Китайский. Выбор сохраняется в coop.ini и применяется сразу.",
     "A SporeCoop felület nyelvének váltása: Auto (rendszernyelv), angol, orosz, magyar, spanyol vagy kínai. A coop.ini fájlba mentődik.",
     "Cambia el idioma de SporeCoop entre Automático (idioma del sistema), inglés, ruso, húngaro, español y chino. Se guarda en coop.ini.",
     "切换 SporeCoop 界面语言: 自动 (系统语言)、英语、俄语、匈牙利语、西班牙语或中文，立即生效并保存至 coop.ini。"},
    {"Cap the game's frame rate (60-240 FPS) to reduce stutter, GPU heat, and physics quirks. Auto uses Spore's default cap. Saved on this PC only.",
     "Ограничивает частоту кадров игры (60-240 FPS), снижая нагрев видеокарты, фризы и проблемы физики. Авто: стандартное ограничение Spore. Сохраняется локально.",
     "Korlátozza a képkockasebességet (60-240 FPS) a szaggatás és a GPU-terhelés csökkentéséhez. Auto: a Spore alap korlátja. Csak ezen a PC-n mentődik.",
     "Limita los FPS del juego (60-240 FPS) para reducir tirones, carga de GPU y fallos de física. Auto usa el límite por defecto de Spore.",
     "限制游戏帧率 (60-240 FPS)，降低显卡负载、卡顿与物理异常。自动: 使用 Spore 默认上限。仅保存在本机。"},
    {"On: stretches Spore into a borderless fullscreen window for instant Alt-Tab without minimizing or flicker. Off: normal game display mode. Local to this PC.",
     "Вкл: запускает Spore в окне без рамки на весь экран для мгновенного Alt-Tab без сворачивания и мерцаний. Выкл: стандартный режим окна игры. Только для этого ПК.",
     "Be: keret nélküli teljes képernyős ablakban futtatja a Spore-t az azonnali Alt-Tab érdekében. Ki: normál megjelenítési mód. Csak ezen a PC-n.",
     "Sí: ejecuta Spore en ventana sin bordes a pantalla completa para hacer Alt-Tab al instante sin parpadeos. No: modo normal del juego.",
     "开: 以无边框全屏窗口运行 Spore，支持秒切 Alt-Tab 且不最小化或闪烁。关: 使用游戏原生显示模式。仅限本机生效。"},
    {"Widens the 3rd-person camera field of view (+3 to +18 deg) in Creature and Galactic Adventures stages. Default keeps Spore's original camera angle. Local to this PC.",
     "Увеличивает угол обзора камеры от 3-го лица (+3..+18°) на этапах Существо и Приключения. Обычный: стандартный угол камеры Spore. Применяется только на этом ПК.",
     "Szélesíti a 3. személyű kamera látószögét (+3..+18 fok) Lény és Kaland szakaszokban. Alap: a Spore eredeti kameraszöge. Csak ezen a PC-n.",
     "Amplía el campo de visión en 3ª persona (+3 a +18 grados) en Criatura y Aventuras Galácticas. Normal mantiene la cámara original de Spore.",
     "增大生物与银河冒险阶段的第三人称视野角度 (+3° 至 +18°)。默认: 保持 Spore 原始镜头角度。仅对本机生效。"},

    // ---- Common gameplay notices ----
    {" wants to fire a superweapon", " хочет применить супероружие", " szuperfegyvert akar bevetni", " quiere usar una superarma", " 想要发射超级武器"},
    {": press I'm ready (F9)", ": нажмите «Я готов» (F9)", ": nyomd meg a «Kész vagyok» gombot (F9)", ": pulsa «Estoy listo» (F9)", ": 请按 F9 点击「准备就绪」"},
    {"the superweapon fires", "супероружие сработает", "a szuperfegyver elsül", "se dispara la superarma", "超级武器即将发射"},
    {" is knocked out: swim right next to them within 10 s", " в нокауте: подплывите вплотную за 10 с для оживления", " kiütve: ússz mellé 10 mp-en belül", " está inconsciente: nada junto a él en 10 s", " 已昏迷: 请在 10 秒内游到其身旁复活"},
    {" revived ", " оживил(а) ", " újraélesztette: ", " revivió a ", " 复活了 "},
    {" was not revived in time", " не успели оживить", " nem lett időben újraélesztve", " no fue revivido a tiempo", " 未能及时被复活"},
    {" respawned small", " возродился маленьким", " kicsiként éledt újra", " reapareció pequeño", " 以小体型重生"},
    {" next to ", " рядом с ", " mellett: ", " junto a ", " 在旁: "},
    {"Rival main hut is protected from destruction (attacks raid food instead).", "Главная хижина соперника защищена от разрушения (удары воруют еду).", "A rivális fő kunyhó védett (a támadás ételt zsákmányol).", "La choza principal rival está protegida (atacar roba comida).", "对手主屋受保护无法被摧毁 (攻击将改为掠夺食物)。"},
    {" raided ", " украл(а) ", " elrabolt ", " saqueó ", " 掠夺了 "},
    {" food from ", " еды у ", " ételt tőle: ", " de comida de ", " 食物，来自 "},
    {" attacked a rival: PvP shield ended early!", " атаковал(а) соперника: PvP-щит снят досрочно!", " riválist támadott: a PvP pajzs lejárt!", " atacó a un rival: ¡escudo PvP terminado antes!", " 主动攻击了对手: PvP 护盾提前解除！"},
    {"'s tribe was wiped out! Respawning in ", ": племя уничтожено! Возрождение через ", " törzse megsemmisült! Újraéledés: ", ": tribu aniquilada! Reaparece en ", " 的部落被全灭！将在 "},
    {"'s tribe respawned! PvP shield active for ", ": племя возродилось! PvP-щит действует ", " törzse újraéledt! PvP pajzs: ", ": ¡tribu reaparecida! Escudo PvP: ", " 的部落已重生！PvP 护盾剩余 "},
    {"Your tribe", "Ваше племя", "A törzsed", "Tu tribu", "你的部落"},
    {"'s tribe", ": племя", " törzse", ": tribu", " 的部落"},
    {" has a PvP shield for ", " получает PvP-щит на ", " PvP pajzsot kapott: ", " tiene escudo PvP por ", " 获得了 PvP 护盾，持续 "},
    {"  Your PvP shield: ", "  Ваш PvP-щит: ", "  Saját PvP pajzs: ", "  Tu escudo PvP: ", "  你的 PvP 护盾: "},
    {" PvP shield: ", " (PvP-щит): ", " PvP pajzs: ", " escudo PvP: ", " PvP 护盾: "},
    {" (respawning...)", " (возрождение...)", " (újraéledés...)", " (reapareciendo...)", " (重生中...)"},
    {"  Ally gift to ", "  Подарок союзнику ", "  Ajándék szövetségesnek: ", "  Regalo a aliado ", "  赠予盟友 "},

    // ---- Rivals errors & notices (BUG-1579, all 5 languages) ----
    {"invalid player", "неверный игрок", "érvénytelen játékos", "jugador no válido", "无效的玩家"},
    {"can only propose alliance when neutral", "союз можно предложить только при нейтралитете", "szövetség csak semleges viszonynál ajánlható", "solo puedes proponer alianza siendo neutral", "仅在中立状态下可提议结盟"},
    {"not neutral", "вы не в нейтралитете", "nem semleges a viszony", "no eres neutral", "当前不是中立状态"},
    {"no alliance proposal", "нет предложения союза", "nincs szövetségi ajánlat", "no hay oferta de alianza", "没有待处理的结盟提议"},
    {"already at war", "уже идёт война", "már háborúban álltok", "ya estáis en guerra", "已经处于战争状态"},
    {"allied: must break alliance first (60 s notice)", "вы союзники: сначала разорвите союз (60 с)", "szövetségesek: előbb bontsd a szövetséget (60 mp)", "aliados: primero rompe la alianza (aviso de 60 s)", "当前为盟友: 需先解除同盟 (60 秒缓冲)"},
    {"not allied", "вы не в союзе", "nem vagytok szövetségesek", "no sois aliados", "当前并非同盟关系"},
    {"Alliance just formed, wait a moment", "союз только что заключён, подождите немного", "a szövetség most jött létre, várj egy pillanatot", "la alianza acaba de formarse, espera un momento", "同盟刚刚建立，请稍候再操作"},
    {"alliance break already pending", "разрыв союза уже начат", "a szövetség bontása már folyamatban van", "la ruptura de alianza ya está en curso", "解除同盟倒计时已在进行中"},
    {"not at war", "вы не в состоянии войны", "nem álltok háborúban", "no estáis en guerra", "当前并未处于战争状态"},
    {"no peace proposal", "нет предложения мира", "nincs békeajánlat", "no hay oferta de paz", "没有待处理的停战提议"},
    {"unknown relation action", "неизвестное действие дипломатии", "ismeretlen diplomáciai művelet", "acción diplomática desconocida", "未知的外交操作"},
    {"rival bond is protected", "связь соперника под защитой", "a rivális kapcsolata védett", "el vínculo del rival está protegido", "对手的该外交关系正受保护"},
    {"not enough influence", "недостаточно влияния", "nincs elég befolyás", "no tienes suficiente influencia", "影响力不足"},
    {"npc rejected protect", "фракция отклонила защиту", "az NPC elutasította a védelmet", "la facción rechazó la protección", "该 NPC 阵营拒绝了保护"},
    {"no active draft", "сейчас нет активного драфта", "nincs aktív draft", "no hay draft activo", "当前没有进行中的选秀"},
    {"cannot pick", "нельзя выбрать предмет", "nem választhatsz", "no puedes elegir", "无法选择"},
    {"no picks left", "выборы драфта закончились", "elfogytak a választások", "no quedan turnos de elección", "已无剩余选择次数"},
    {"unknown draft item", "неизвестный предмет драфта", "ismeretlen draft elem", "elemento de draft desconocido", "未知的选秀物品"},
    {"already picked", "уже выбрано", "már kiválasztva", "ya elegido", "已经选择过该物品"},
    {"cannot skip", "нельзя пропустить драфт", "nem ugorható át", "no puedes omitir", "无法跳过"},
    {"rivals.core is off", "режим соперников отключён", "a Riválisok rendszer ki van kapcsolva", "el modo Rivales está desactivado", "对手模式核心已关闭"},
    {"not host", "действие доступно только хосту", "csak a szerver végezheti el", "solo disponible para el anfitrión", "仅限房主操作"},
    {"Influence can only be bought during an active stage", "влияние можно купить только на активном этапе", "befolyás csak aktív szakaszban vásárolható", "la influencia solo se compra durante una fase activa", "仅可在进行中的游戏阶段购买影响力"},
    {"insufficient resources to buy influence", "недостаточно ресурсов для покупки влияния", "nincs elég erőforrás a befolyáshoz", "recursos insuficientes para comprar influencia", "资源不足，无法购买影响力"},
    {"buy failed", "не удалось купить влияние", "sikertelen vásárlás", "error al comprar influencia", "购买失败"},
    {"npc rejected turn against", "фракция отказалась ссориться", "az NPC elutasította az uszítást", "la facción rechazó enemistarse", "该 NPC 阵营拒绝了挑拨"},
    {"must be allied with faction to protect", "нужен союз с фракцией для защиты", "szövetség szükséges a védelemhez", "debes ser aliado de la facción para protegerla", "需与该阵营结盟才可施加保护"},
    {"npc rejected smooth over", "фракция отказалась мириться", "az NPC elutasította a békítést", "la facción rechazó apaciguarse", "该 NPC 阵营拒绝了缓和关系"},
    {"unknown influence action", "неизвестное действие влияния", "ismeretlen befolyás művelet", "acción de influencia desconocida", "未知的影响力操作"},
    {"invalid target", "неверная цель", "érvénytelen célpont", "objetivo no válido", "无效的目标"},
    {"host does not support MSG_RIVALS", "хост использует старую версию без Соперников", "a szerver nem támogatja a Riválisok módot", "el anfitrión no soporta el modo Rivales", "房主版本过旧，不支持对手模式"},
    {"not connected", "нет подключения к сессии", "nincs kapcsolat a munkamenettel", "no conectado a la sesión", "尚未连接到房间"},
    {"DNA", "ДНК", "DNS", "ADN", "DNA"},
    {"food", "еды", "étel", "comida", "食物"},
    {"available in active stage", "доступно на активном этапе", "aktív szakaszban elérhető", "disponible en fase activa", "仅在游玩阶段可用"},
    {"Faction", "Фракция", "Frakció", "Facción", "阵营"},
    {"bought", "купил(а)", "vett:", "compró", "购买了"},
    {"influence", "влияния", "befolyást", "influencia", "影响力"},
    {"turned", "настроил(а)", "ellene fordította:", "enemistó a", "挑拨了"},
    {"against", "против", "ellene:", "contra", "敌视"},
    {"protected bond with", "защитил(а) союз с", "megvédte a kapcsolatot:", "protegió el vínculo con", "保护了外交关系:"},
    {"smoothed over relations with", "улучшил(а) отношения с", "javította a viszonyt:", "mejoró relaciones con", "缓和了关系:"},
    {"copied", "взял(а)", "lemásolta:", "copió", "复制了"},
    {"winner", "победителю", "győztesnek", "ganador", "胜者"},

    // ---- Session problem report (sreport.cpp, BUG-1579) ----
    {"The host", "Хост", "A szerver", "El anfitrión", "房主"},
    {" (host) asks for your report:", " (хост) просит ваш отчёт (.zip):", " (szerver) kéri a jelentésed (.zip):", " (anfitrión) pide tu informe (.zip):", " (房主) 请求你的诊断报告 (.zip):"},
    {" asks for your problem report: click Send in F9 to pack & send logs (.zip), or Don't send", " просит отчёт об ошибке: нажмите «Отправить» в F9, чтобы собрать и отправить архив логов (.zip)", " hibajelentést kér: kattints a Küldés gombra (F9) a .zip napló elküldéséhez", " pide tu informe: pulsa Enviar en F9 para crear y enviar el .zip con registros", " 请求你的问题报告: 请在 F9 点击「发送」打包并发送 .zip 日志，或点击「不发送」"},
    {"Making your report zip...", "Собираю архив отчёта (.zip)...", "Jelentés (.zip) készítése...", "Creando archivo de informe (.zip)...", "正在打包诊断报告 (.zip)..."},
    {"Sending your report zip to the host...", "Отправляю архив отчёта (.zip) хосту...", "Jelentés (.zip) küldése a szervernek...", "Enviando informe (.zip) al anfitrión...", "正在向房主发送诊断报告 (.zip)..."},
    {"Sending your report to the host: ", "Отправка архива отчёта хосту: ", "Jelentés küldése a szervernek: ", "Enviando tu informe al anfitrión: ", "正在发送诊断报告给房主: "},
    {"Report zip sent to the host (", "Архив отчёта (.zip) отправлен хосту (", "Jelentés (.zip) elküldve a szervernek (", "Informe (.zip) enviado al anfitrión (", "诊断报告 (.zip) 已发送给房主 ("},
    {"You did not send your report", "Вы не отправили отчёт", "Nem küldtél jelentést", "No enviaste tu informe", "你未发送诊断报告"},
    {"The host's request for your report ran out", "Время запроса отчёта истекло", "A jelentéskérés ideje lejárt", "La solicitud de informe expiró", "房主的诊断报告请求已超时"},
    {"Your report is too big to send (over 25 MB)", "Отчёт слишком большой для отправки (более 25 МБ)", "A jelentés túl nagy a küldéshez (>25 MB)", "Tu informe es demasiado grande (>25 MB)", "报告文件过大无法发送 (超过 25 MB)"},
    {"The session ended before your report was sent", "Сессия завершилась до отправки отчёта", "A munkamenet véget ért a küldés előtt", "La sesión terminó antes de enviar el informe", "房间在报告发送完成前已关闭"},
    {"Your report could not be made (SporeKit too old?)", "Не удалось собрать отчёт (устаревший SporeKit?)", "Nem sikerült elkészíteni a jelentést", "No se pudo crear tu informe", "无法生成诊断报告"},
    {"Session report saved: ", "Общий отчёт сохранён: ", "Közös jelentés mentve: ", "Informe de sesión guardado: ", "房间诊断报告已保存: "},
    {"No report came (see the chat)", "Отчёты не пришли (см. чат)", "Nem érkezett jelentés (lásd a chatet)", "No llegó ningún informe (ver chat)", "未收到任何报告 (详见聊天记录)"},
    {"The session report could not be written (see the chat)", "Не удалось записать общий отчёт (см. чат)", "A közös jelentés nem írható (lásd a chatet)", "No se pudo guardar el informe (ver chat)", "无法写入房间诊断报告 (详见聊天记录)"},
    {"Reports: ", "Отчёты: ", "Jelentések: ", "Informes: ", "报告进度: "},
    {" of ", " из ", " / ", " de ", " / "},
    {" answered", " ответили", " válaszolt", " respondieron", " 已回复"},

    // ---- Tribe Rivals chieftain KO respawn (tribe_rivals.cpp, BUG-1580) ----
    {"  Chieftain reviving: ", "  Вождь возрождается: ", "  Főnök éledése: ", "  Jefe reviviendo: ", "  酋长复活倒计时: "},
    {"Your Chieftain was knocked out! Reviving in ", "Ваш вождь без сознания! Возрождение через ", "A főnököd kiütve! Éledés: ", "Tu jefe quedó inconsciente! Revive en ", "你的酋长已倒地! 复活倒计时: "},
    {"Your Chieftain has recovered!", "Ваш вождь пришёл в себя!", "A főnököd felépült!", "Tu jefe se ha recuperado!", "你的酋长已恢复行动!"},
};

struct SettingTr {
    const char* key;
    const char* label[4]; // Ru, Hu, Es, Zh
    const char* help[4];  // Ru, Hu, Es, Zh
};

inline const SettingTr kSettingsTr[] = {
    {"cell.regen",
     {"Регенерация рядом с напарником", "Regeneráció partner közelében", "Regeneración cerca de aliado", "靠近队友时恢复生命"},
     {"Вкл: ваша клетка постепенно восстанавливает здоровье, пока рядом плывёт напарник. Выкл: регенерация работает как в одиночной игре.",
      "Be: a sejted fokozatosan gyógyul, amíg egy partner a közeledben úszik. Ki: az alap egyjátékos gyógyulás marad.",
      "Sí: tu célula recupera vida gradualmente mientras un aliado nada cerca. No: regeneración normal como en solitario.",
      "开: 队友在附近游动时你的细胞会持续回血。关: 与单人游戏相同的默认恢复速度。"}},
    {"all.partner_arrows",
     {"Стрелки к напарникам", "Partner nyilak", "Flechas hacia aliados", "队友方向箭头"},
     {"Вкл: у края экрана появляется цветная стрелка, показывающая направление к напарникам вне кадра (в режиме Соперники всегда выключено). Выкл: стрелок нет.",
      "Be: színes nyíl jelzi a képernyő szélén a látómezőn kívüli partnerek irányát (Rivális módban mindig kikapcsolva). Ki: nincs nyíl.",
      "Sí: una flecha de color en el borde señala a los aliados fuera de pantalla (siempre desactivada en Rivales). No: sin flechas.",
      "开: 屏幕边缘显示彩色箭头指向视野外的队友 (对手模式下自动关闭)。关: 不显示箭头。"}},
    {"all.partner_map",
     {"Напарники на миникарте", "Partnerek a térképen", "Aliados en el minimapa", "小地图显示队友"},
     {"Вкл: напарники отмечаются на миникарте цветными маркерами на этапах Существо и Племя, чтобы их было легко найти. Выкл: метки напарников скрыты.",
      "Be: a partnerek színes jelölővel látszanak a kistérképen (Lény és Törzs szakaszban). Ki: a jelölők rejtve maradnak.",
      "Sí: los aliados aparecen con marcadores de color en el minimapa (Criatura y Tribu). No: no se muestran en el minimapa.",
      "开: 在生物和部落阶段的小地图上用彩色标记显示队友位置。关: 不在小地图显示。"}},
    {"all.dna_pace",
     {"Темп прогресса как в одиночке", "Egyjátékos haladási tempó", "Ritmo de progreso individual", "单人节奏进度"},
     {"Вкл: награды ДНК и прогресса делятся на число игроков (1/N), чтобы этап не проходился в 2-4 раза быстрее одиночки. Выкл: каждый получает 100% от каждой награды.",
      "Be: a DNS és haladás jutalmak eloszlanak a játékosok száma szerint (1/N), így a tempó egyjátékos marad. Ki: mindenki 100%-ot kap.",
      "Sí: las ganancias de ADN y progreso se dividen entre N jugadores (ritmo individual). No: cada jugador recibe el 100%.",
      "开: DNA 与进化进度奖励按人数 (1/N) 平摊，保持单人游戏节奏。关: 每人获得 100% 完整奖励。"}},
    {"all.shared_dna",
     {"ДНК и прогресс", "DNS és haladás", "ADN y progreso", "DNA 与进化进度"},
     {"Свой кошелёк: прогресс общий, но в редакторе каждый тратит только свои ДНК. Общий пул: одна казна ДНК на всех. Свой прогресс: у каждого своя шкала эволюции в Клетке и Существе (в Соперниках всегда свой кошелёк).",
      "Saját pénztárca: közös haladás, de a szerkesztőben mindenki saját DNS-t költ. Közös: közös DNS kassza. Saját haladás: önálló fejlődési sáv (Riválisban mindig saját).",
      "Propia bolsa: progreso compartido, pero cada uno gasta su ADN en el editor. Compartido: fondo común. Progreso propio: barra independiente (en Rivales siempre propia).",
      "独立钱包: 进度共享，但编辑器仅消耗个人 DNA。共享池: 全队共用同一份 DNA。独立进度: 细胞/生物阶段各自独立进化条 (对手模式强制独立)。"}},
    {"creature.npcs_target_partners",
     {"Дикие NPC видят напарников", "NPC állatok célba veszik a partnereket", "NPCs atacan también a aliados", "野生生物同样攻击队友"},
     {"Вкл: дикие существа и хищники замечают, преследуют и атакуют гостей так же, как хоста. Выкл: дикие NPC реагируют только на существо хоста.",
      "Be: a vad lények és ragadozók a vendégeket is észreveszik, üldözik és támadják, mint a szervert. Ki: csak a szerverre reagálnak.",
      "Sí: las criaturas salvajes detectan, persiguen y atacan a los invitados igual que al anfitrión. No: solo reaccionan al anfitrión.",
      "开: 野生生物和掠食者会像对待房主一样发现、追击并攻击队友。关: 仅对房主做出反应。"}},
    {"creature.pack_rescue",
     {"Спасение в стае", "Falkamentés", "Rescate de manada", "群体救援"},
     {"Вкл: в этапе Существо смертельный удар рядом с напарником не убивает, а оставляет 40% здоровья (срабатывает раз в 60 с; выкл в Соперниках). Выкл: удар убивает сразу.",
      "Be: Lény szakaszban partner mellett a halálos csapás nem öl meg, 40% HP-n tart (60 mp töltés; Riválisban ki). Ki: azonnali halál.",
      "Sí: en Criatura, un golpe letal cerca de un aliado te deja con 40% de vida (cada 60 s; desactivado en Rivales). No: mueres al instante.",
      "开: 生物阶段在队友附近受到致命伤时不死亡，保留 40% 血量 (冷却 60 秒，对手模式关闭)。关: 直接死亡。"}},
    {"creature.revive",
     {"Оживление павшего напарника", "Kiütött partner újraélesztése", "Revivir compañero caído", "复活倒地队友"},
     {"Вкл: при потере всего HP существо не умирает, а падает в нокаут на 60 с; напарник может поднять его, постояв рядом 1.5 с (иначе смерть и штраф 10% ДНК; выкл в Соперниках). Выкл: обычная смерть.",
      "Be: 0 HP-nál a lény 60 mp-re kiütődik; egy partner 1.5 mp alatt felélesztheti (különben 10% DNS büntetés; Riválisban ki). Ki: sima halál.",
      "Sí: al caer a 0 HP quedas inconsciente 60 s; un aliado te revive estando cerca 1.5 s (si no, mueres con 10% coste ADN; desactivado en Rivales). No: muerte normal.",
      "开: 血量归零时进入 60 秒倒地状态，队友靠近 1.5 秒即可复活 (超时死亡扣 10% DNA，对手模式关闭)。关: 直接死亡。"}},
    {"creature.pack_heal",
     {"Лечение рядом с напарником", "Gyógyítás partner mellett", "Curación junto a aliados", "队友附近回血"},
     {"Вкл: в этапе Существо здоровье восстанавливается быстрее, пока вы находитесь рядом с напарником (выкл в Соперниках). Выкл: стандартная регенерация как в одиночке.",
      "Be: Lény szakaszban az életerő gyorsabban töltődik vissza partner közelében (Riválisban ki). Ki: alap egyjátékos regeneráció.",
      "Sí: en Criatura, recuperas salud más rápido mientras permaneces cerca de un aliado (desactivado en Rivales). No: regeneración estándar.",
      "开: 生物阶段待在队友附近时生命值恢复加快 (对手模式关闭)。关: 与单人模式相同的默认回血速度。"}},
    {"creature.joint_hunt",
     {"Бонус совместной охоты", "Közös vadászat bónusz", "Bono de caza conjunta", "联手狩猎加成"},
     {"Вкл: если ударить цель, которую недавно атаковал напарник, удар наносит +25% бонусного урона (выкл в Соперниках). Выкл: бонус за совместную атаку отключён.",
      "Be: ha olyan célpontot ütsz, amelyet nemrég a partnered is megsebzett, +25% bónusz sebzést okozol (Riválisban ki). Ki: nincs bónusz.",
      "Sí: golpear a un objetivo atacado recientemente por un aliado inflige +25% de daño extra (desactivado en Rivales). No: sin bonificación.",
      "开: 攻击队友刚刚命中的同一目标时造成 +25% 额外伤害 (对手模式关闭)。关: 无额外伤害加成。"}},
    {"all.name_tags",
     {"Имена над игроками", "Nevek a játékosok felett", "Nombres sobre jugadores", "玩家头顶名称"},
     {"Вкл: над существами, вождями, техникой и кораблями игроков отображаются их ники. Выкл: подписи скрыты (удобно при большом числе юнитов или для чистой картинки).",
      "Be: a játékosok neve megjelenik a lények, főnökök, járművek és űrhajók felett. Ki: nevek elrejtve (tisztább képhez).",
      "Sí: muestra el nombre del jugador sobre sus criaturas, jefes, vehículos y naves. No: oculta las etiquetas sobre las unidades.",
      "开: 在玩家生物、部落首领、载具和飞船上方显示昵称。关: 隐藏头顶名字标签 (画面更清爽)。"}},
    {"all.friendly_fire",
     {"Огонь по своим (Friendly Fire)", "Baráti tűz", "Fuego amigo", "友军伤害"},
     {"Вкл: игроки могут атаковать и ранить друг друга в режиме Вместе (Клетка, Существо, Племя, Цивилизация). В режиме Соперники PvP регулируется войной (F9 -> Соперники). Выкл: урона по своим нет.",
      "Be: a játékosok sebezhetik egymást Együtt módban (Sejt, Lény, Törzs, Civ). Riválisban a Háború dönt (F9 -> Riválisok). Ki: nincs baráti sebzés.",
      "Sí: permite dañarse entre jugadores en modo Juntos (Célula, Criatura, Tribu, Civ). En Rivales el PvP depende de la Guerra (F9 -> Rivales). No: sin fuego amigo.",
      "开: 合作模式下玩家可在细胞/生物/部落/文明阶段互相攻击。对手模式下 PvP 由宣战决定 (F9 -> 对手)。关: 禁止友伤。"}},
    {"all.max_players",
     {"Максимум игроков", "Max. játékosszám", "Máx. jugadores", "最大玩家数"},
     {"Ограничение числа игроков в комнате, включая хоста (от 2 до 4). Если комната заполнена, новым подключениям придёт отказ «сессия заполнена».",
      "Maximum ennyi játékos lehet a szobában a szerverrel együtt (2-4). Ha betelt, az új belépők elutasítást kapnak.",
      "Límite total de jugadores en la sesión contando al anfitrión (de 2 a 4). Quien intente entrar por encima del límite verá sesión llena.",
      "限制房间总人数 (2 至 4 人，包含房主)。满员后新加入的玩家会收到房间已满提示。"}},
    {"all.party_difficulty",
     {"Сложность для 3-4 игроков", "Nehézség 3-4 játékosra", "Dificultad para 3-4 jugadores", "3-4 人动态难度"},
     {"Усиливает здоровье и урон враждебных NPC для большой группы: 150% = в 1.5 раза прочнее и сильнее. Авто: +25% за каждого игрока свыше двоих (3 игрока = 125%, 4 = 150%). 100%: как в соло.",
      "Az NPC-k életerejét és sebzését növeli több játékosnál: 150% = 1.5x erősebb. Auto: +25% minden 2 feletti játékos után (3 fő = 125%, 4 = 150%). 100%: alap.",
      "Escala la salud y el daño de los NPCs: 150% = 1.5x más fuertes. Auto: +25% por jugador extra sobre 2 (3 = 125%, 4 = 150%). 100%: dificultad normal.",
      "提升野生与敌对 NPC 的血量和伤害: 150% 即 1.5 倍强度。自动: 超过 2 人后每人 +25% (3人 125%，4人 150%)。100%: 原版难度。"}},
    {"all.late_join",
     {"Вход после старта игры", "Csatlakozás játék közben", "Unirse con partida empezada", "允许中途加入"},
     {"Вкл: новые игроки могут подключаться прямо во время игры на планете. Выкл: вход открыт только в меню галактики до загрузки мира (вылетевшие игроки всё равно могут вернуться).",
      "Be: új játékosok játék közben is beléphetnek a bolygóra. Ki: csak a galaxis menüben (a lecsatlakozott társak visszatérhetnek).",
      "Sí: permite que nuevos jugadores se unan con la partida en marcha. No: solo en el menú de galaxia (quien se desconecte sí puede volver).",
      "开: 允许新玩家在星球游玩中途加入房间。关: 仅允许在星系菜单加入 (掉线玩家仍可重连)。"}},
    {"cell.knockout",
     {"Нокаут вместо смерти (Клетка)", "Kiütés halál helyett", "Inconsciencia en vez de muerte", "昏迷代替死亡"},
     {"Вкл: на этапе Клетка смертельный удар рядом с напарником отправляет клетку в нокаут на 10 с, чтобы друг успел подплыть и оживить её. Выкл: обычная мгновенная смерть.",
      "Be: Sejt szakaszban partner mellett a halálos csapás 10 mp kiütést okoz, amit a társ feléleszthet. Ki: azonnali halál mint egyedül.",
      "Sí: en Célula, un golpe letal cerca de un aliado te deja inconsciente 10 s para que te reviva al acercarse. No: muerte instantánea normal.",
      "开: 细胞阶段在队友附近受到致命伤时会昏迷 10 秒，队友游近即可复活。关: 原版直接死亡。"}},
    {"cell.respawn_small",
     {"Возрождение рядом с напарником", "Újraéledés partner mellett", "Reaparecer junto al aliado", "在队友旁重生"},
     {"Вкл: если вырубленную клетку не успели оживить за 10 с, она возрождается маленькой прямо рядом с напарником. Выкл: обычное возрождение из яйца как в одиночной игре.",
      "Be: ha a kiütött sejtet 10 mp alatt nem élesztik fel, kicsiként a partner mellett éled újra. Ki: alap tojásból újjászületés.",
      "Sí: si nadie te revive en 10 s, reapareces como célula pequeña junto a tu aliado. No: reaparición normal desde huevo como en solitario.",
      "开: 昏迷 10 秒未被救起时以小体型直接重生在队友身旁。关: 原版孵化重生。"}},
    {"cell.lineage_cells",
     {"Старые клетки как дикие виды", "Régi sejtek vad fajként", "Células viejas como salvajes", "旧细胞作为野生种"},
     {"Вкл: примерно каждая шестая дикая клетка подходящей диеты принимает облик прошлых клеток игроков (для гостя требуется галочка в лобби F9). Выкл: только стандартные виды Spore.",
      "Be: nagyjából minden hatodik megfelelő étrendű vad sejt felveszi a játékosok korábbi sejtjeinek kinézetét. Ki: csak alap Spore fajok.",
      "Sí: aproximadamente una de cada seis células salvajes usa diseños anteriores de los jugadores (con permiso del invitado en F9). No: solo especies del juego.",
      "开: 约六分之一同食性的野生细胞会套用玩家以前的细胞设计 (队友需在 F9 勾选)。关: 仅原版物种。"}},
    {"core.switch_vote",
     {"Голосование перед Существом", "Szavazás Lény szakasz előtt", "Votación antes de Criatura", "进入生物阶段前投票"},
     {"Вкл: при переходе хоста с этапа Клетка на этап Существо сначала запускается голосование готовности всех игроков. Выкл: этап Существо начинается сразу.",
      "Be: a Sejt -> Lény szakaszváltás megvárja az összes játékos beleegyezését. Ki: a Lény szakasz azonnal elindul.",
      "Sí: pasar de Célula a Criatura espera a que todos voten estar listos. No: la fase Criatura comienza de inmediato.",
      "开: 房主从细胞进入生物阶段前需等待全员投票准备就绪。关: 立即切换阶段。"}},
    {"creature.mode",
     {"Вид существа", "Lény mód", "Criatura", "生物外观模式"},
     {"Свои: каждый игрок выбирает и эволюционирует собственное существо со своими частями и одеждой. Хоста: все играют существом хоста, редактировать его может только хост.",
      "Saját: minden játékos saját lényével játszik és fejlődik. Szerveré: mindenki a szerver lényével játszik, csak a szerver szerkesztheti.",
      "Propio: cada jugador juega y evoluciona su propia especie. Anfitrión: todos usan la especie del anfitrión y solo él puede editarla.",
      "独立: 每位玩家使用并进化自己的专属物种。房主: 全员共用房主的物种，且仅房主可编辑。"}},
    {"core.editor_now",
     {"Редактор хоста без голосования", "Szerver szerkesztő azonnal", "Editor del anfitrión al instante", "房主编辑器立即开启"},
     {"Вкл: редактор эволюции хоста открывается мгновенно (в Клетке гости получают уведомление, в Существе - кнопку «Эволюционировать сейчас»). Выкл: перед открытием идёт голосование.",
      "Be: a szerver szerkesztője azonnal megnyílik, a vendégek értesítést vagy azonnali fejlődés gombot kapnak. Ki: előbb szavazás indul.",
      "Sí: el editor del anfitrión se abre al instante y los invitados reciben aviso u oferta para evolucionar. No: requiere votación previa.",
      "开: 房主直接开启进化编辑器无需投票 (队友收到提示或同步进化按钮)。关: 先发起全员投票。"}},
    {"tribe.pincer",
     {"Бонус фланговой атаки (+50%)", "Fogó támadás bónusz (+50%)", "Bono de tenaza (+50%)", "夹击加成 (+50%)"},
     {"Вкл: на этапе Племя если юниты двух разных игроков атакуют одно вражеское племя в пределах 10 секунд, они наносят +50% урона. Выкл: обычный урон.",
      "Be: Törzs szakaszban ha két játékos harcosai 10 mp-en belül ugyanazt az ellenséges törzset támadják, +50% sebzést kapnak. Ki: alap sebzés.",
      "Sí: en Tribu, si unidades de dos jugadores atacan la misma tribu enemiga en un lapso de 10 s, infligen +50% de daño. No: daño normal.",
      "开: 部落阶段两名玩家的族人在 10 秒内夹击同一敌方部落时造成 +50% 伤害。关: 普通伤害。"}},
    {"tribe.chief_aura",
     {"Аура вождя", "Törzsfőnök aura", "Aura de jefe", "首领光环"},
     {"Вкл: соплеменники рядом со своим вождём бьют на +20% сильнее, а если вождь получает смертельный удар рядом с юнитом напарника, он выживает с 30% HP. Выкл: без ауры.",
      "Be: a főnök közelében a törzstagok +20%-ot sebeznek; partner tagja mellett a főnök 30% HP-val túlél egy halálos csapást. Ki: nincs aura.",
      "Sí: los aldeanos cerca de su jefe hacen +20% daño; si el jefe va a caer junto a un aliado sobrevive con 30% HP. No: sin aura.",
      "开: 族人在本方首领附近攻击 +20%；首领在队友族人旁受致命伤时保留 30% 血量。关: 无光环。"}},
    {"tribe.duet",
     {"Совместный концерт (Дуэт)", "Duett bónusz", "Bono de dueto", "合奏加成"},
     {"Вкл: когда музыканты двух игроков одновременно выступают перед одной деревней NPC, шкала мини-игры дружбы заполняется заметно быстрее. Выкл: как в одиночной игре.",
      "Be: ha két játékos zenészei egyszerre adnak műsort ugyanannak a falunak, a barátkozás minijáték gyorsabban telik. Ki: alap sebesség.",
      "Sí: si músicos de dos jugadores tocan a la vez ante la misma aldea NPC, la barra de amistad se llena más rápido. No: velocidad normal.",
      "开: 两名玩家的乐手同时在同一 NPC 村庄演奏时，结盟音游进度条增长更快。关: 单人速度。"}},
    {"tribe.epic",
     {"Событие: Монстр в Племени", "Epikus lény esemény", "Evento de criatura épica", "史诗巨兽事件"},
     {"Вкл: на этапе Племя время от времени к деревне приходит гигантский монстр; за совместную победу над ним выдаётся награда 80 еды. Выкл: событие не запускается.",
      "Be: Törzs szakaszban időnként egy epikus szörny támadja meg a falut; közös legyőzése 80 ételt ad. Ki: nincs co-op epikus esemény.",
      "Sí: en Tribu aparece periódicamente una criatura épica en la aldea; vencerla juntos otorga 80 de comida. No: sin evento épico.",
      "开: 部落阶段每隔一段时间会有史诗巨兽袭击村庄，联手击杀奖励 80 食物。关: 不触发此事件。"}},
    {"civ.roles",
     {"Роли губернаторов", "Szerepek (katonai/gazd./vall.)", "Roles de gobernador", "总督职责"},
     {"Вкл: в Цивилизации каждый игрок выбирает специализацию в F9 (Военный, Экономика или Религия) и получает соответствующий бонус для своих юнитов и городов. Выкл: без ролей.",
      "Be: Civilizációban minden játékos választ egy szerepet (Katonai, Gazdasági, Vallási) és bónuszt kap. Ki: nincsenek szerepek és bónuszok.",
      "Sí: en Civilización cada jugador elige un rol en F9 (Militar, Económico o Religioso) con bonificaciones propias. No: sin roles.",
      "开: 文明阶段每位玩家可在 F9 选择军事、经济或宗教总督职责并获得专属加成。关: 无职责加成。"}},
    {"civ.joint_siege",
     {"Бонус совместной осады (+50%)", "Közös ostrom bónusz (+50%)", "Bono de asedio conjunto (+50%)", "联合围城加成 (+50%)"},
     {"Вкл: город, который одновременно атакует техника двух игроков, захватывается или обращается на 50% быстрее. Выкл: стандартная скорость захвата.",
      "Be: ha két játékos járművei egyszerre ostromolnak egy várost, 50%-kal gyorsabban foglalják el. Ki: alap foglalási sebesség.",
      "Sí: una ciudad atacada a la vez por vehículos de dos jugadores se captura un 50% más rápido. No: velocidad estándar.",
      "开: 两名玩家的载具同时围攻同一座城市时占领/转化速度加快 50%。关: 原版占领速度。"}},
    {"civ.superweapon_vote",
     {"Голосование за супероружие", "Szuperfegyver szavazás", "Votación de superarma", "超级武器投票"},
     {"Вкл: в режиме Вместе запуск супероружия (бомба, ядерка и др.) требует подтверждения всех игроков. Выкл: супероружие срабатывает сразу (в Соперниках голосование не требуется).",
      "Be: Együtt módban a szuperfegyver kilövése közös szavazást kér. Ki: azonnal elsül (Rivális módban nincs szavazás).",
      "Sí: en modo Juntos las superarmas requieren votación de todos antes de disparar. No: se lanzan al instante (en Rivales no hay votación).",
      "开: 合作模式下发射超级武器前需全员投票同意。关: 立即发射 (对手模式下无需投票)。"}},
    {"civ.own_money",
     {"Раздельный бюджет (Цив.)", "Saját pénz (Civilizáció)", "Dinero propio (Civ.)", "独立资金 (文明)"},
     {"Вкл: у каждого игрока свой кошелёк спорлингов, каждый оплачивает свои покупки и получает доход со своих городов (обязательно в Соперниках). Выкл: одна общая казна.",
      "Be: minden játékosnak saját spórakasszája van, saját városai jövedelmével fizet (Riválisban kötelező). Ki: egy közös kincstár.",
      "Sí: cada jugador tiene su propio dinero, paga sus compras y cobra de sus ciudades (activo en Rivales). No: tesorería compartida.",
      "开: 每位玩家拥有独立孢币账户、独立支付开销并收取各自城市收入 (对手模式必需)。关: 共享国库。"}},
    {"space.disable",
     {"Отключение вместо гибели корабля", "Bénulás megsemmisülés helyett", "Nave desactivada, no destruida", "飞船瘫痪代替摧毁"},
     {"Вкл: если ваш корабль сбили рядом с напарником, он не взрывается, а отключается на 20 с, чтобы напарник мог подлететь и починить его. Выкл: обычная гибель корабля.",
      "Be: ha partner mellett lövik ki a hajódat, nem semmisül meg, 20 mp-re lebénul és megjavítható. Ki: alap megsemmisülés.",
      "Sí: si derriban tu nave cerca de un aliado, queda desactivada 20 s para que la repare en lugar de explotar. No: destrucción normal.",
      "开: 队友在附近时飞船被击毁会改为瘫痪 20 秒并可由队友修复，不会立刻爆炸。关: 原版直接摧毁。"}},
    {"space.trade",
     {"Обмен инструментами и специями", "Kereskedelem és ajándékok", "Comercio y regalos", "工具与香料交易"},
     {"Вкл: на этапе Космос игроки могут дарить друг другу инструменты корабля и передавать специи через окно F9 -> Космос. Выкл: передача между игроками отключена.",
      "Be: Űr szakaszban a játékosok eszközöket ajándékozhatnak és fűszert cserélhetnek (F9 -> Űr). Ki: nincs játékosok közti csere.",
      "Sí: en Espacio permite regalar herramientas de nave e intercambiar especia desde F9 -> Espacio. No: comercio entre jugadores desactivado.",
      "开: 太空阶段玩家可通过 F9 -> 太空互相赠送飞船工具与交易香料。关: 禁用玩家间交易。"}},
    {"space.contracts",
     {"Совместные контракты", "Közös megbízások", "Contratos cooperativos", "合作合约"},
     {"Вкл: в режиме Вместе периодически появляются общие кооп-задания в Космосе (армады пиратов, совместная разведка звёзд; в Соперниках выкл). Выкл: контрактов нет.",
      "Be: Együtt módban időnként közös űrmegbízások indulnak (kalózarmada, csillagfelderítés; Riválisban ki). Ki: nincsenek megbízások.",
      "Sí: en modo Juntos aparecen misiones cooperativas espaciales (armadas piratas, exploración; desactivado en Rivales). No: sin contratos.",
      "开: 合作模式下定时生成多人太空合约 (海盗舰队、星系探索，对手模式关闭)。关: 无合作合约。"}},
    {"space.diplomacy",
     {"Общая дипломатия", "Közös diplomácia", "Diplomacia compartida", "共享外交"},
     {"Вкл: войны, союзы и репутация с ИИ-империями синхронизируются на всех игроков. Выкл: у каждого игрока свои независимые отношения с ИИ-империями.",
      "Be: az AI birodalmakkal kötött háborúk, szövetségek és viszonyok közösek. Ki: minden játékosnak külön diplomáciája van.",
      "Sí: guerras, alianzas y reputación con imperios NPC se comparten entre todos. No: cada jugador mantiene su propia diplomacia.",
      "开: 与 AI 帝国的宣战、结盟与好感度在所有玩家间共享。关: 每位玩家保持独立外交关系。"}},
    {"space.allies",
     {"Колонии напарников как союзники", "Partnerek szövetségesként", "Aliados como imperio amigo", "队友殖民地显示为盟友"},
     {"Вкл: колонии друзей отображаются в вашей галактике как союзная империя, с которой можно торговать и летать рядом. Выкл: колонии партнёров не зеркалируются как империя.",
      "Be: a partnerek kolóniái szövetséges birodalomként jelennek meg a galaxisodban. Ki: a kolóniák nincsenek birodalomként tükrözve.",
      "Sí: las colonias de tus compañeros aparecen en tu galaxia como un imperio aliado visible. No: no se muestran como imperio.",
      "开: 队友的殖民地会在你的星系中以盟友帝国形式同步显示。关: 不镜像显示为帝国。"}},
    {"space.finale",
     {"Совместный финал в центре галактики", "Közös finálé a galaxis magjában", "Final juntos en el núcleo", "共同进入银河核心"},
     {"Вкл: при подлёте к центру галактики игроки синхронно входят в финал вместе. Выкл: каждый игрок входит в центр галактики самостоятельно как в одиночке.",
      "Be: a galaxis magjába való belépés szinkronizálva történik együtt. Ki: minden játékos külön lép be, mint egyjátékos módban.",
      "Sí: la entrada al núcleo galáctico se sincroniza para vivir el final juntos. No: cada jugador entra por separado como en solitario.",
      "开: 抵达银河核心时全员同步进入触发共同结局。关: 各自独立进入如单人游戏。"}},
    {"space.rivals",
     {"Разрешить соперничество в Космосе", "Rivális mód az űrben", "Permitir Rivales en Espacio", "允许太空对手模式"},
     {"Вкл: хост может переключить этап Космос в режим Соперников (отдельные родные планеты, бюджеты и PvP-бои). Выкл: игроки в Космосе остаются союзниками.",
      "Be: a szerver átkapcsolhatja az Űr szakaszt Rivális módra (külön anyabolygók, kassza, PvP). Ki: a játékosok szövetségesek maradnak.",
      "Sí: el anfitrión puede activar Rivales en Espacio (planetas natales propios, presupuestos y PvP). No: siempre aliados en Espacio.",
      "开: 允许房主在太空阶段开启对手模式 (独立母星、预算与 PvP)。关: 太空阶段始终为盟友。"}},
    {"all.autosave",
     {"Автосохранение хоста", "Szerver automata mentés", "Autoguardado del anfitrión", "房主自动保存"},
     {"Задаёт интервал автосохранения мира у хоста (5-60 мин) плюс автосохранение после важных событий (новый этап, колония, город, монстр). Выкл: сохраняетесь только вручную.",
      "Milyen gyakran mentse automatikusan a világot a szerver (5-60 perc) és mérföldkövek után (új szakasz, kolónia, város). Ki: csak kézi mentés.",
      "Intervalo de autoguardado del anfitrión (5-60 min) más hitos clave (nuevo estadio, colonia, ciudad, épico). No: solo guardado manual.",
      "设置房主自动存档间隔 (5-60 分钟) 及关键里程碑自动存档 (新阶段、殖民地、城市、巨兽)。关: 仅手动保存。"}},
    {"adventure.shared",
     {"Совместные приключения (GA)", "Közös kalandok (GA)", "Aventuras compartidas (GA)", "共享银河冒险 (GA)"},
     {"Вкл: в Космических приключениях синхронизируются цели, смена актов, финал и враги (симуляция на хосте). Выкл: каждый проходит локальную копию, видны только капитаны друзей.",
      "Be: Galaktikus Kalandokban közösek a célok, felvonások, eredmény és a szerver által irányított ellenségek. Ki: csak a kapitányok látszanak.",
      "Sí: en Aventuras Galácticas se comparten objetivos, actos, desenlace y enemigos simulados por el anfitrión. No: solo se ven los capitanes.",
      "开: 银河冒险中同步任务目标、幕次切换、结局与房主模拟的敌人。关: 各自本地运行，仅同步队长外观。"}},
};

inline const std::unordered_map<std::string_view, const Entry*>& string_map()
{
    static const auto m = [] {
        std::unordered_map<std::string_view, const Entry*> out;
        out.reserve(sizeof(kStrings) / sizeof(kStrings[0]) * 2);
        for (const auto& e : kStrings)
            out[e.en] = &e;
        return out;
    }();
    return m;
}

inline const std::unordered_map<std::string_view, const SettingTr*>& setting_map()
{
    static const auto m = [] {
        std::unordered_map<std::string_view, const SettingTr*> out;
        out.reserve(sizeof(kSettingsTr) / sizeof(kSettingsTr[0]) * 2);
        for (const auto& s : kSettingsTr)
            out[s.key] = &s;
        return out;
    }();
    return m;
}

inline const char* tr_lang(Lang l, const char* en, const char* ru = nullptr)
{
    if (!en)
        return "";
    if (l == Lang::En)
        return en;
    const auto& m = string_map();
    auto it = m.find(en);
    if (it != m.end()) {
        const Entry* e = it->second;
        switch (l) {
        case Lang::Ru: return (e->ru && *e->ru) ? e->ru : (ru && *ru ? ru : en);
        case Lang::Hu: return (e->hu && *e->hu) ? e->hu : en;
        case Lang::Es: return (e->es && *e->es) ? e->es : en;
        case Lang::Zh: return (e->zh && *e->zh) ? e->zh : en;
        default: return en;
        }
    }
    if (l == Lang::Ru && ru && *ru)
        return ru;
    return en;
}

inline const char* tr(const char* en, const char* ru = nullptr)
{
    return tr_lang(g_lang, en, ru);
}

inline const char* setting_label(const char* key, const char* fallback_en, Lang l = g_lang)
{
    if (l == Lang::En || !key)
        return fallback_en ? fallback_en : "";
    auto it = setting_map().find(key);
    if (it != setting_map().end()) {
        const char* s = it->second->label[(uint8_t)l - 1];
        if (s && *s)
            return s;
    }
    return fallback_en ? fallback_en : "";
}

inline const char* setting_help(const char* key, const char* fallback_en, Lang l = g_lang)
{
    if (l == Lang::En || !key)
        return fallback_en ? fallback_en : "";
    auto it = setting_map().find(key);
    if (it != setting_map().end()) {
        const char* s = it->second->help[(uint8_t)l - 1];
        if (s && *s)
            return s;
    }
    return fallback_en ? fallback_en : "";
}

inline const char* setting_group(const char* group_en, Lang l = g_lang)
{
    return tr_lang(l, group_en);
}

inline std::string setting_value(const std::string& val_en, Lang l = g_lang)
{
    if (l == Lang::En)
        return val_en == "on" ? "On" : val_en == "off" ? "Off" : val_en == "own" ? "Own" : val_en == "host" ? "Host's" : val_en;
    if (val_en == "on") return tr_lang(l, "On");
    if (val_en == "off") return tr_lang(l, "Off");
    if (val_en == "own") return tr_lang(l, "Own");
    if (val_en == "host") return tr_lang(l, "Host's");
    return tr_lang(l, val_en.c_str());
}

} // namespace i18n
