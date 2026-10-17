#include "uno/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace uno {

namespace {

struct Entry {
    const char* key;
    const char* en;
    const char* zh;
};

const Entry kTable[] = {
    // ---- generic -------------------------------------------------------
    {"common.yes", "Yes", "是"},
    {"common.no", "No", "否"},
    {"common.on", "on", "开"},
    {"common.off", "off", "关"},
    {"common.back", "Back", "返回"},
    {"common.enter_name", "Enter player name (blank for random): ", "输入玩家昵称(留空随机): "},
    {"common.press_enter", "Press Enter", "按回车继续"},
    {"common.press_any", "Press any key", "按任意键继续"},
    {"common.you", "You", "你"},
    {"common.cpu", "CPU", "电脑"},
    {"common.score", "Score", "积分"},
    {"common.cards", "cards", "张"},
    {"common.points", "pts", "分"},
    {"common.draw_pile", "Draw pile", "牌堆"},
    {"common.type_number", "Type a number", "请输入数字"},
    {"common.invalid", "Invalid input", "输入无效"},
    {"common.team", "Team", "队伍"},

    // ---- main menu -----------------------------------------------------
    {"menu.title", "UNO for the terminal", "终端 UNO"},
    {"menu.pve", "Play vs CPU (PVE)", "人机对战 (PVE)"},
    {"menu.host", "Host online game (PVP)", "创建联机房间 (PVP)"},
    {"menu.join", "Join online game (PVP)", "加入联机房间 (PVP)"},
    {"menu.rules", "Rule settings", "规则设置"},
    {"menu.lang", "Language: English / 中文", "语言: 中文 / English"},
    {"menu.quit", "Quit", "退出"},
    {"menu.navigate", "Up/Down or W/S to move, Enter to select", "上下键或 W/S 移动, 回车确认"},

    // ---- PVE setup -----------------------------------------------------
    {"pve.humans", "Human seats (1-2)", "人类席位 (1-2)"},
    {"pve.choose_humans", "How many humans? 1 or 2", "选择人类数量: 1 或 2"},
    {"pve.select_seat", "Select your seat", "选择你的位置"},
    {"pve.teams_note", "Team mode pairs seats 0+2 vs 1+3", "组队模式: 0+2 对阵 1+3"},
    {"pve.diff0", "Easy - random legal moves", "简单 - 随机合法出牌"},
    {"pve.diff1", "Normal - solid heuristics", "普通 - 稳健启发式"},
    {"pve.diff2", "Hard - counts cards, plays to trap", "困难 - 记牌算牌, 精算控场"},

    // ---- rules screen --------------------------------------------------
    {"rules.title", "Rule settings", "规则设置"},
    {"rules.players", "Player count", "玩家人数"},
    {"rules.cards", "Cards per hand", "每人起手牌数"},
    {"rules.target", "Target score", "目标分数"},
    {"rules.stack2", "Draw Two stacking", "加二可叠加"},
    {"rules.chal4", "Challenge Wild+4", "可质疑万能加四"},
    {"rules.stack4", "Wild+4 stacking", "万能加四可叠加"},
    {"rules.mustplay", "Must play if able", "有牌必出"},
    {"rules.recycle", "Recycle discard pile", "弃牌堆回洗"},
    {"rules.unopenalty", "Penalty for missing UNO", "漏报 UNO 罚 2 张"},
    {"rules.swap77", "Opening 7-7 hand swap", "开局 7-7 换手"},
    {"rules.teams", "Team mode (2v2)", "组队模式 (2v2)"},
    {"rules.seed", "Random seed (0 = random)", "随机种子 (0 为随机)"},
    {"rules.saved", "Rules saved", "规则已保存"},
    {"rules.reset", "Restore defaults", "恢复默认"},

    // ---- PVP setup -----------------------------------------------------
    {"pvp.port", "Port", "端口"},
    {"pvp.room", "Room code", "房间号"},
    {"pvp.seat", "Seat", "席位"},
    {"pvp.bots", "Fill empty seats with CPU", "空位由电脑填充"},
    {"pvp.hosting", "Hosting...", "正在监听..."},
    {"pvp.lobby", "Lobby", "房间"},
    {"pvp.ready", "Ready", "准备"},
    {"pvp.not_ready", "Waiting...", "等待中"},
    {"pvp.wait_host", "Waiting for the host to start...", "等待房主开始..."},
    {"pvp.start", "Start", "开始"},
    {"pvp.need_players", "Need more players", "人数不足"},
    {"pvp.already_started", "That match already started", "该局已经开始"},
    {"pvp.host_seat", "Host", "房主"},
    {"pvp.connected", "Connected", "已连接"},
    {"pvp.disconnected", "Connection lost", "连接断开"},
    {"pvp.host_left", "The host closed the room", "房主已离开"},
    {"pvp.connecting", "Connecting to {}...", "正在连接 {}..."},
    {"pvp.turn_you", "Your turn", "轮到你"},
    {"pvp.chat_hint", "Enter message, empty to skip", "输入聊天内容, 留空跳过"},

    // ---- in-game prompts ----------------------------------------------
    {"ui.round_start", "Round {} started", "第 {} 回合开始"},
    {"ui.turn", "Turn {} - {}", "第 {} 回合 - {}"},
    {"ui.your_turn", ">>> Your turn <<<", ">>> 轮到你了 <<<"},
    {"ui.choose_card", "Play card [1-{}], D draw, P pass, U say UNO, C catch",
     "出牌 [1-{}], D 摸牌, P 过牌, U 喊 UNO, C 抓漏报"},
    {"ui.catch_hint", "  (you can catch a missing UNO: press C)", "  (可抓漏报: 按 C)"},
    {"ui.card_only", "Play card [1-{}] or D draw, U say UNO", "出牌 [1-{}] 或 D 摸牌, U 喊 UNO"},
    {"ui.hint_numbers", "Numbers", "数字键"},
    {"ui.choose_color", "Choose color: R red / Y yellow / G green / B blue", "选择颜色: R 红 / Y 黄 / G 绿 / B 蓝"},
    {"ui.choose_color_num", "Choose color: 1 red / 2 yellow / 3 green / 4 blue", "选择颜色: 1 红 / 2 黄 / 3 绿 / 4 蓝"},
    {"ui.challenge_prompt", "{} plays {} - challenge the Wild+4? (Y/N)",
     "{} 打出 {} - 要质疑万能加四吗? (Y/N)"},
    {"ui.pending_penalty", "You must take {} cards (Y accept / S stack +2)", "你必须吃 {} 张 (Y 接受 / S 叠 +2)"},
    {"ui.choosing", "{} is choosing...", "{} 正在选择..."},
    {"ui.wait", "Waiting for {}...", "等待 {}..."},
    {"ui.draw_and_play", "You drew {} - it is playable, play it or P to pass", "你摸到 {} - 可以打出, 出牌或 P 过牌"},
    {"ui.must_play", "You have a playable card and must play", "你有可出的牌, 必须出牌"},
    {"ui.no_play", "No legal play - draw a card", "无可出的牌 - 请摸牌"},
    {"ui.recycle", "Discard pile recycled", "弃牌堆已回洗"},
    {"ui.low_deck", "Only {} cards left in the draw pile", "牌堆仅剩 {} 张"},
    {"ui.finished", "Hand empty - you are out!", "手牌出完 - 你已过关!"},

    // ---- events --------------------------------------------------------
    {"ev.turn", "{} turn", "轮到 {}"},
    {"ev.joined", "{} joined", "{} 加入"},
    {"ev.left", "{} left", "{} 离开"},
    {"ev.drawn", "{} drew {} card(s)", "{} 摸了 {} 张"},
    {"ev.recycle", "deck recycled", "牌堆回洗"},
    {"ev.played", "{} played {}", "{} 打出 {}"},
    {"ev.color", "color -> {}", "颜色 -> {}"},
    {"ev.skipped", "{} is skipped", "{} 被跳过"},
    {"ev.reversed", "direction reversed", "方向反转"},
    {"ev.penalty", "{} took {} card(s)", "{} 吃了 {} 张"},
    {"ev.stacked", "{} stacked a +2 (now {})", "{} 叠了加二 (共 {})"},
    {"ev.chal_ok", "{} caught {} playing an illegal Wild+4", "{} 抓到 {} 违规出万能加四"},
    {"ev.chal_bad", "{}'s challenge failed", "{} 质疑失败"},
    {"ev.uno", "{} says UNO!", "{} 喊 UNO!"},
    {"ev.uno_miss", "{} forgot to say UNO - drew 2", "{} 漏报 UNO - 罚 2 张"},
    {"ev.caught", "{} caught {}", "{} 抓到 {}"},
    {"ev.swap", "hands swapped", "已交换手牌"},
    {"ev.out", "{} is out!", "{} 出完了!"},
    {"ev.round_over", "Round over", "本回合结束"},
    {"ev.match_over", "Match over", "整局结束"},
    {"ev.winner", "{} wins", "{} 获胜"},
    {"ev.disconnect", "{} disconnected (replaced by CPU)", "{} 掉线 (由电脑接管)"},

    // ---- match end -----------------------------------------------------
    {"end.title", "Result", "结算"},
    {"end.won", "YOU WIN!", "你赢了!"},
    {"end.lost", "You lost", "你输了"},
    {"end.draw", "Draw", "平局"},
    {"end.rank", "Final standings", "最终排名"},
    {"end.next", "Next round", "下一回合"},
    {"end.leave", "Leave room", "离开房间"},
    {"end.points_awarded", "{} scored {} pts", "{} 获得 {} 分"},
    {"end.to_target", "{} reaches {} and wins the match", "{} 达到 {} 分, 赢得整局"},
};

const std::map<std::string, std::pair<std::string, std::string>>& index() {
    static const std::map<std::string, std::pair<std::string, std::string>> m = [] {
        std::map<std::string, std::pair<std::string, std::string>> t;
        for (const Entry& e : kTable) t[e.key] = {e.en, e.zh};
        return t;
    }();
    return m;
}

}  // namespace

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

I18n::I18n() = default;

const std::string& I18n::tr(const char* key) const {
    static const std::string kEmpty;
    const auto& m = index();
    auto it = m.find(key);
    if (it == m.end()) return kEmpty;
    return (lang_ == Lang::Zh) ? it->second.second : it->second.first;
}

std::string I18n::fmt(const char* key) const {
    const std::string& s = tr(key);
    if (!s.empty()) return s;
    return std::string(key);
}

const std::string& tr(const char* key) { return I18n::instance().tr(key); }
std::string fmt(const char* key) { return I18n::instance().fmt(key); }

Lang parseLang(const std::string& s) {
    std::string low;
    for (char c : s) low += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (low == "zh" || low == "cn" || low == "zh-cn" || low == "chinese") return Lang::Zh;
    return Lang::En;
}

}  // namespace uno