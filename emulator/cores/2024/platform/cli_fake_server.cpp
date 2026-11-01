#ifndef __EMSCRIPTEN__

#include "platform_internal.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr uint32_t IR_KEY = 0xDEADBEEFu;

uint32_t encode_ir(uint8_t mti, uint32_t payload28) {
    uint32_t decoded = ((static_cast<uint32_t>(mti & 0x0Fu) << 28) | (payload28 & 0x0FFFFFFFu));
    return decoded ^ IR_KEY;
}

void decode_ir(uint32_t code, uint8_t &mti, uint32_t &payload28) {
    uint32_t decoded = code ^ IR_KEY;
    mti = static_cast<uint8_t>((decoded >> 28) & 0x0Fu);
    payload28 = decoded & 0x0FFFFFFFu;
}

struct TowerRecord {
    int id;
    std::string name;
    std::string location;
    int level;
    int health;
    int max_health;
    std::string boot_time;
    uint32_t ir_code;
    bool enabled;
    std::string status;
    int players_in_range;
    int players_in_battle;
    int players_joined_tower;
    int players_disconnected;
};

struct ShopDef {
    int item_id;
    const char *item_name;
    const char *shortname;
    const char *buff_type;
    float buff_value;
    int item_price;
    int available_stock;
    int version;
};

constexpr ShopDef kShopDefs[] = {
    {1, "Spellchecker",   "SPEL", "SPELLCHECK",     1.0f,  50, 99, 1},
    {2, "Strat Simplify", "SIMP", "STRAT_SIMPLIFY", 1.0f,  60, 99, 1},
    {3, "Quad Damage",    "QUAD", "QUAD_DAMAGE",    4.0f,  75, 99, 1},
    {4, "Saving Throw",   "SAVE", "SAVING_THROW",   1.0f,  80, 99, 1},
    {5, "Reverse Shell",  "RSHL", "REVERSE_SHELL",  1.0f,  90, 99, 1},
    {6, "Hydra Revival",  "HYDR", "HYDRA",          1.0f, 100, 99, 1},
};

struct BattleSession {
    bool present = false;
    int battle_id = 0;
    int tower_id = 0;
    int player_conn = 3;
    int player_max_conn = 3;
    std::string player_status = "BATTLE";
    bool is_savior = false;
    int total_attacks = 0;
    int total_damage = 0;
    int xp_earned = 0;
    int coins_earned = 0;
    int failures = 0;
    int perf_sum = 0;
};

struct ServerState {
    int player_id = 2024;
    std::string badge_id = "24:5C:20:24:00:01";
    std::string handle;
    bool registered = false;
    int xp = 0;
    int level = 1;
    int coins = 120;
    int towers_defeated = 0;
    bool can_level = true;
    std::string community = "SAINTCON Museum";
    int community_levels = 3;

    int equipped[2] = {0, 0};
    std::vector<int> purchased_ids;
    int joined_tower_id = 0;
    int next_battle_id = 101;
    uint32_t beacon_seq = 1;
    BattleSession battle;
    std::vector<TowerRecord> towers;
};

ServerState g_srv;
bool g_srv_inited = false;

void init_default_state() {
    g_srv = ServerState{};
    struct Def {
        int id;
        const char *name;
        const char *location;
        int level;
        int max_hp;
        uint32_t payload;
    };
    static const Def defs[] = {
        { 1, "Communities",     "Ground Level",   1, 240, 0x00240101u},
        { 2, "Ground Contests", "Ground Level",   1, 280, 0x00240102u},
        { 3, "Ballroom A",      "Ballroom Level", 2, 360, 0x00240103u},
        { 4, "Ballroom B",      "Ballroom Level", 2, 400, 0x00240104u},
        { 5, "Ballroom C",      "Ballroom Level", 3, 480, 0x00240105u},
        { 6, "EduSec",          "Level 3",        2, 360, 0x00240106u},
        { 7, "Chill Lounge",    "Level 3",        3, 520, 0x00240107u},
        { 8, "ICS Village",     "Level 3",        4, 640, 0x00240108u},
        { 9, "Briefs",          "Level 3",        4, 720, 0x00240109u},
        {10, "HAM Radio",       "Level 3",        3, 500, 0x0024010Au},
        {11, "Social Eng",      "Level 3",        5, 900, 0x0024010Bu},
    };
    for (const auto &d : defs) {
        g_srv.towers.push_back(TowerRecord{
            d.id,
            d.name,
            d.location,
            d.level,
            d.max_hp,
            d.max_hp,
            "2024-10-22T09:00:00Z",
            encode_ir(0x0, d.payload),
            true,
            "VULNERABLE",
            0,
            0,
            0,
            0,
        });
    }
    g_srv_inited = true;
}

void ensure_init() {
    if (!g_srv_inited) init_default_state();
}

const ShopDef *find_shop_def(int item_id) {
    for (const auto &d : kShopDefs) {
        if (d.item_id == item_id) return &d;
    }
    return nullptr;
}

bool has_buff(const char *buff_type) {
    for (int slot : g_srv.equipped) {
        const ShopDef *d = find_shop_def(slot);
        if (d && std::strcmp(d->buff_type, buff_type) == 0) return true;
    }
    return false;
}

json build_slot_json(int item_id) {
    const ShopDef *d = find_shop_def(item_id);
    if (!d) return nullptr;
    return json{
        {"name", d->item_name},
        {"shortname", d->shortname},
        {"buff_type", d->buff_type},
        {"buff_value", d->buff_value},
        {"valid", true},
        {"rewards", "Museum Shop Power-Up"},
    };
}

TowerRecord *find_tower_by_id(int id) {
    for (auto &t : g_srv.towers) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

TowerRecord *find_tower_by_ir(uint32_t ir_code) {
    for (auto &t : g_srv.towers) {
        if (t.ir_code == ir_code) return &t;
    }
    uint8_t mti = 0;
    uint32_t payload = 0;
    decode_ir(ir_code, mti, payload);
    if (mti == 0x0) {
        return find_tower_by_id(static_cast<int>(payload & 0xFFu));
    }
    return nullptr;
}

json tower_to_json(const TowerRecord &t) {
    return json{
        {"id", t.id},
        {"name", t.name},
        {"location", t.location},
        {"level", t.level},
        {"health", t.health},
        {"max_health", t.max_health},
        {"boot_time", t.boot_time},
        {"ir_code", t.ir_code},
        {"enabled", t.enabled},
        {"status", t.status},
        {"players_in_range", t.players_in_range},
        {"players_in_battle", t.players_in_battle},
        {"players_joined_tower", t.players_joined_tower},
        {"players_disconnected", t.players_disconnected},
    };
}

json build_battle_status_json() {
    TowerRecord *t = nullptr;
    if (g_srv.battle.present) t = find_tower_by_id(g_srv.battle.tower_id);
    if (!t) t = find_tower_by_id(g_srv.joined_tower_id);
    if (!t) t = &g_srv.towers[0];

    int simplify = has_buff("STRAT_SIMPLIFY") ? 1 : 0;
    int smin = 3, smax = 4, samt = 2;
    switch (t->level) {
        case 1: smin = 3; smax = 4; samt = 2; break;
        case 2: smin = 4; smax = 5; samt = 2; break;
        case 3: smin = 4; smax = 5; samt = 3; break;
        case 4: smin = 5; smax = 6; samt = 3; break;
        default: smin = 5; smax = 7; samt = 4; break;
    }
    smin = std::max(2, smin - simplify);
    smax = std::max(smin, smax - simplify);
    if (simplify && samt > 2) samt -= 1;

    bool disc = g_srv.battle.present && g_srv.battle.player_status == "DISCONNECTED";
    bool savior = g_srv.battle.present && g_srv.battle.is_savior;
    json saviors = json::array();
    if (disc && !savior) {
        saviors.push_back("MuseumSavior");
    }

    return json{
        {"battle_active", true},
        {"player_level", g_srv.battle.present ? g_srv.battle.player_conn : 3},
        {"player_hp", g_srv.battle.present ? g_srv.battle.player_max_conn : 3},
        {"player_status", g_srv.battle.present ? g_srv.battle.player_status : std::string("JOINED")},
        {"is_savior", savior},
        {"tower_ir_code", t->ir_code},
        {"tower_level", t->level},
        {"tower_health", t->health},
        {"tower_max_health", t->max_health},
        {"players_in_range", std::max(1, t->players_in_range)},
        {"players_joined_tower", std::max(1, t->players_joined_tower)},
        {"players_in_battle", disc ? 0 : 1},
        {"strategem_min", smin},
        {"strategem_max", smax},
        {"strategem_amount", samt},
        {"players_disconnected", disc ? 1 : 0},
        {"savior_handle", saviors},
    };
}

uint32_t next_dynamic_ir(uint8_t mti) {
    g_srv.beacon_seq = (g_srv.beacon_seq + 1u) & 0xFFFFu;
    return encode_ir(mti, 0x00240000u | g_srv.beacon_seq);
}

void award_xp(int amount) {
    g_srv.xp += amount;
    int computed = 1 + (g_srv.xp / 250);
    if (computed > g_srv.level) {
        g_srv.level = std::min(99, computed);
    }
}

void parse_url(const std::string &raw, std::string &pathname, std::string &query) {
    std::string s = raw;
    auto scheme = s.find("://");
    if (scheme != std::string::npos) {
        auto slash = s.find('/', scheme + 3);
        s = (slash != std::string::npos) ? s.substr(slash) : "/";
    }
    auto q = s.find('?');
    if (q != std::string::npos) {
        pathname = s.substr(0, q);
        query = s.substr(q + 1);
    } else {
        pathname = s;
        query.clear();
    }
}

std::string query_param(const std::string &query, const char *key) {
    std::string prefix = std::string(key) + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string part = (amp == std::string::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
        if (part.rfind(prefix, 0) == 0) {
            return part.substr(prefix.size());
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

int write_reply(int http_status, const json &envelope, char **out_hdrs, char **out_body, size_t *out_len) {
    std::string body = envelope.dump();
    char hdr_buf[256];
    std::snprintf(hdr_buf, sizeof(hdr_buf),
                  "Content-Type: application/json\nContent-Length: %zu\n", body.size());
    if (out_hdrs) *out_hdrs = strdup(hdr_buf);
    if (out_body) {
        *out_body = static_cast<char *>(std::malloc(body.size() + 1));
        if (*out_body) {
            std::memcpy(*out_body, body.data(), body.size() + 1);
        }
    }
    if (out_len) *out_len = body.size();
    return http_status;
}

} // namespace

extern "C" void cli_fake_server_reset(uint32_t seed) {
    (void)seed;
    init_default_state();
}

extern "C" uint32_t cli_fake_server_tower_ir_code(int tower_id) {
    ensure_init();
    TowerRecord *t = find_tower_by_id(tower_id);
    if (!t) return 0;
    bool active_here = g_srv.battle.present && g_srv.battle.tower_id == t->id;
    if (t->health <= 0 && !active_here) {
        t->health = t->max_health;
        t->status = "VULNERABLE";
    }
    t->players_in_range = 1;
    return t->ir_code;
}

extern "C" uint32_t cli_fake_server_beacon_ir_code(const char *kind) {
    ensure_init();
    if (!kind) return 0;
    if (std::strcmp(kind, "levelup") == 0) return next_dynamic_ir(0x1);
    if (std::strcmp(kind, "savior") == 0) return next_dynamic_ir(0x2);
    if (std::strcmp(kind, "auth") == 0) return next_dynamic_ir(0x5);
    return 0;
}

extern "C" int cli_fake_server_handle(const char *method,
                                      const char *url,
                                      const char *body,
                                      size_t body_len,
                                      char **out_headers_json,
                                      char **out_body,
                                      size_t *out_body_len) {
    ensure_init();
    std::string m = method ? method : "GET";
    std::string pathname, query;
    parse_url(url ? url : "/", pathname, query);

    json payload = json::object();
    if (m == "POST" && body && body_len > 0) {
        std::string raw(body, body_len);
        if (json::accept(raw)) {
            payload = json::parse(raw);
        }
    }

    auto ok = [&](const json &res) {
        return write_reply(200, json{{"status", true}, {"detail", nullptr}, {"result", res}},
                           out_headers_json, out_body, out_body_len);
    };
    auto err = [&](int code, const char *detail) {
        return write_reply(code, json{{"status", false}, {"detail", detail}, {"result", nullptr}},
                           out_headers_json, out_body, out_body_len);
    };

    if (m == "POST" && pathname == "/auth/request_auth_code") {
        return ok(json{{"code", next_dynamic_ir(0x5)}});
    }
    if (m == "POST" && pathname == "/auth/request_levelup_code") {
        return ok(json{{"code", next_dynamic_ir(0x1)}});
    }
    if (m == "POST" && pathname == "/auth/auth_status") {
        uint32_t code = payload.value("ir_code", 0u);
        return ok(json{
            {"code", code},
            {"claimed", true},
            {"request_time", "2024-10-24T12:00:00Z"},
            {"claimed_by", g_srv.handle.empty() ? "MuseumVisitor" : g_srv.handle},
            {"claim_time", "2024-10-24T12:00:02Z"},
        });
    }

    if (m == "GET" && pathname == "/badge") {
        return ok(json{
            {"id", g_srv.player_id},
            {"badge_id", g_srv.badge_id},
            {"handle", g_srv.handle.empty() ? json(nullptr) : json(g_srv.handle)},
            {"xp", g_srv.xp},
            {"level", g_srv.level},
            {"enabled", true},
            {"badge_team", true},
            {"staff", false},
            {"blackbadge", false},
            {"can_level", g_srv.can_level},
            {"community", g_srv.community},
            {"community_levels", g_srv.community_levels},
            {"is_savior", g_srv.battle.present && g_srv.battle.is_savior},
            {"coins", g_srv.coins},
        });
    }

    if (m == "POST" && pathname == "/badge/register") {
        std::string h = payload.value("handle", "");
        if (h.empty()) return err(400, "Handle cannot be empty");
        if (h.size() > 31) h.resize(31);
        g_srv.handle = h;
        g_srv.registered = true;
        return ok(json::object());
    }

    if (m == "GET" && pathname == "/badge/firmware_version") {
        return ok(json{{"setting", "firmware_version"}, {"value", "2.6.9"}});
    }

    if (m == "GET" && pathname == "/badge/firmware") {
        return err(404, "Firmware is already up to date");
    }

    if (m == "GET" && pathname == "/badge/all_tower_status") {
        json arr = json::array();
        for (const auto &t : g_srv.towers) arr.push_back(tower_to_json(t));
        return ok(arr);
    }

    if (m == "GET" && pathname == "/badge/tower_status") {
        TowerRecord *t = nullptr;
        std::string tid = query_param(query, "tower_id");
        std::string irc = query_param(query, "ir_code");
        if (!tid.empty()) {
            t = find_tower_by_id(std::atoi(tid.c_str()));
        } else if (!irc.empty()) {
            t = find_tower_by_ir(static_cast<uint32_t>(std::strtoul(irc.c_str(), nullptr, 10)));
        }
        if (!t) return err(404, "Tower not found");
        return ok(tower_to_json(*t));
    }

    if (m == "POST" && pathname == "/badge/join_tower") {
        uint32_t irc = payload.value("tower_ir_code", 0u);
        TowerRecord *t = find_tower_by_ir(irc);
        if (!t) return err(404, "Tower not found");
        for (auto &other : g_srv.towers) {
            if (other.id != t->id) {
                other.players_joined_tower = 0;
                other.players_in_battle = 0;
                other.players_disconnected = 0;
            }
        }
        g_srv.joined_tower_id = t->id;
        t->players_in_range = 1;
        t->players_joined_tower = 1;
        return ok(json::object());
    }

    if (m == "POST" && pathname == "/badge/leave_tower") {
        for (auto &t : g_srv.towers) {
            t.players_joined_tower = 0;
            t.players_in_battle = 0;
            t.players_disconnected = 0;
        }
        g_srv.joined_tower_id = 0;
        g_srv.battle = BattleSession{};
        return ok(json::object());
    }

    if (m == "POST" && pathname == "/badge/join_battle") {
        TowerRecord *t = find_tower_by_id(g_srv.joined_tower_id);
        if (!t) t = &g_srv.towers[0];
        g_srv.joined_tower_id = t->id;
        if (t->health <= 0) {
            t->health = t->max_health;
            t->status = "VULNERABLE";
        }
        t->players_in_range = 1;
        t->players_joined_tower = 1;
        t->players_in_battle = 1;
        t->players_disconnected = 0;

        if (g_srv.battle.present && g_srv.battle.tower_id == t->id) {
            g_srv.battle.player_conn = g_srv.battle.player_max_conn;
            g_srv.battle.player_status = "BATTLE";
            g_srv.battle.is_savior = false;
        } else {
            g_srv.battle = BattleSession{};
            g_srv.battle.present = true;
            g_srv.battle.battle_id = g_srv.next_battle_id++;
            g_srv.battle.tower_id = t->id;
            g_srv.battle.player_conn = 3;
            g_srv.battle.player_max_conn = 3;
            g_srv.battle.player_status = "BATTLE";
        }
        return ok(json{{"battle_id", g_srv.battle.battle_id}});
    }

    if (m == "POST" && pathname == "/badge/ir_code") {
        json arr = json::array();
        if (payload.contains("codes") && payload["codes"].is_array()) {
            for (const auto &c : payload["codes"]) {
                uint32_t code = c.get<uint32_t>();
                uint8_t mti = 0;
                uint32_t p28 = 0;
                decode_ir(code, mti, p28);
                if (mti == 0x0) {
                    TowerRecord *t = find_tower_by_ir(code);
                    if (t) {
                        t->players_in_range = 1;
                        arr.push_back(json{
                            {"code", code},
                            {"is_valid", true},
                            {"type", "TOWER"},
                            {"name", t->name},
                            {"response", "Tower beacon detected"},
                            {"tower_id", t->id},
                        });
                        continue;
                    }
                } else if (mti == 0x1) {
                    g_srv.level = std::min(99, g_srv.level + 1);
                    g_srv.xp += 100;
                    g_srv.coins += 50;
                    arr.push_back(json{
                        {"code", code},
                        {"is_valid", true},
                        {"type", "LEVELUP"},
                        {"name", "Community Level Up"},
                        {"response", "Level up received!"},
                        {"tower_id", 0},
                    });
                    continue;
                } else if (mti == 0x2) {
                    if (g_srv.battle.present) {
                        g_srv.battle.player_conn = g_srv.battle.player_max_conn;
                        g_srv.battle.player_status = "JOINED";
                        g_srv.battle.is_savior = false;
                        if (TowerRecord *t = find_tower_by_id(g_srv.battle.tower_id)) {
                            t->players_disconnected = 0;
                            t->players_joined_tower = 1;
                        }
                    }
                    arr.push_back(json{
                        {"code", code},
                        {"is_valid", true},
                        {"type", "SAVIOR"},
                        {"name", "Savior Beacon"},
                        {"response", "Revived by Museum Savior! Ready to rejoin the battle."},
                        {"tower_id", 0},
                    });
                    continue;
                } else if (mti == 0x4 || mti == 0x5) {
                    arr.push_back(json{
                        {"code", code},
                        {"is_valid", true},
                        {"type", mti == 0x4 ? "VENDING" : "AUTH"},
                        {"name", "Museum Kiosk"},
                        {"response", "Authenticated"},
                        {"tower_id", 0},
                    });
                    continue;
                }
                arr.push_back(json{
                    {"code", code},
                    {"is_valid", false},
                    {"type", "UNKNOWN"},
                    {"name", nullptr},
                    {"response", nullptr},
                    {"tower_id", 0},
                });
            }
        }
        return ok(arr);
    }

    if (m == "POST" && pathname == "/badge/equip") {
        return ok(json{
            {"slot1", build_slot_json(g_srv.equipped[0])},
            {"slot2", build_slot_json(g_srv.equipped[1])},
        });
    }

    if (m == "POST" && pathname == "/badge/levelup") {
        return ok(json{{"code", next_dynamic_ir(0x1)}});
    }

    if (m == "GET" && pathname == "/vend/items") {
        json arr = json::array();
        for (const auto &d : kShopDefs) {
            bool owned = std::find(g_srv.purchased_ids.begin(), g_srv.purchased_ids.end(), d.item_id) !=
                         g_srv.purchased_ids.end();
            arr.push_back(json{
                {"item_id", d.item_id},
                {"item_name", d.item_name},
                {"item_price", d.item_price},
                {"available_stock", d.available_stock},
                {"purchased", owned},
                {"sold_out", false},
                {"image_url", ""},
                {"version", d.version},
            });
        }
        return ok(arr);
    }

    if (m == "POST" && pathname == "/vend/buy") {
        int item_id = payload.value("item_id", 0);
        const ShopDef *d = find_shop_def(item_id);
        if (!d) return err(404, "Item not found");
        bool owned = std::find(g_srv.purchased_ids.begin(), g_srv.purchased_ids.end(), item_id) !=
                     g_srv.purchased_ids.end();
        if (!owned) {
            if (g_srv.coins < d->item_price) return err(400, "Not enough coins");
            g_srv.coins -= d->item_price;
            g_srv.purchased_ids.push_back(item_id);
        }
        if (g_srv.equipped[0] != item_id && g_srv.equipped[1] != item_id) {
            if (!g_srv.equipped[0]) {
                g_srv.equipped[0] = item_id;
            } else if (!g_srv.equipped[1]) {
                g_srv.equipped[1] = item_id;
            } else {
                g_srv.equipped[0] = g_srv.equipped[1];
                g_srv.equipped[1] = item_id;
            }
        }
        return ok(json::object());
    }

    if (m == "POST" && pathname == "/battle/attack") {
        TowerRecord *t = nullptr;
        if (g_srv.battle.present) t = find_tower_by_id(g_srv.battle.tower_id);
        if (!t) t = find_tower_by_id(g_srv.joined_tower_id);
        if (!t) t = &g_srv.towers[0];
        if (t->health <= 0) {
            t->health = t->max_health;
            t->status = "VULNERABLE";
        }
        int len = std::max(1, payload.value("length", 4));
        int amt = std::max(1, payload.value("amount", 2));
        double secs = std::max(0.25, payload.value("time", 3.0));
        int arrows = len * amt;

        int mult = has_buff("QUAD_DAMAGE") ? 4 : 1;
        double lvl_bonus = 1.0 + std::min(1.5, (g_srv.level - 1) * 0.1);
        int damage = std::max(40, static_cast<int>(std::lround(arrows * 22.0 * lvl_bonus * mult)));

        int prev_hp = t->health;
        t->health = std::max(0, t->health - damage);

        double ratio = (arrows * 0.65) / secs;
        int percent = std::max(55, std::min(99, static_cast<int>(std::lround(72.0 + std::min(1.5, ratio) * 18.0))));

        int xp_gain = 30;
        int coin_gain = 15;
        if (prev_hp > 0 && t->health == 0) {
            t->status = "DEFEATED";
            g_srv.towers_defeated += 1;
            xp_gain += 100 * t->level;
            coin_gain += 45 * t->level;
        }
        award_xp(xp_gain);
        g_srv.coins += coin_gain;

        if (g_srv.battle.present) {
            g_srv.battle.total_attacks += 1;
            g_srv.battle.total_damage += damage;
            g_srv.battle.xp_earned += xp_gain;
            g_srv.battle.coins_earned += coin_gain;
            g_srv.battle.perf_sum += percent;
        }
        return ok(json{{"percent", percent}});
    }

    if (m == "POST" && pathname == "/battle/fail") {
        if (g_srv.battle.present) {
            g_srv.battle.failures += 1;
            g_srv.battle.player_conn = std::max(0, g_srv.battle.player_conn - 1);
            if (g_srv.battle.player_conn <= 0) {
                g_srv.battle.player_status = "DISCONNECTED";
                g_srv.battle.is_savior = has_buff("HYDRA");
                if (TowerRecord *t = find_tower_by_id(g_srv.battle.tower_id)) {
                    t->players_in_battle = 0;
                    t->players_disconnected = 1;
                }
            }
        }
        return ok(build_battle_status_json());
    }

    if (m == "GET" && pathname.rfind("/battle/status/", 0) == 0) {
        return ok(build_battle_status_json());
    }

    if (m == "GET" && pathname == "/battle/savior") {
        return ok(json{{"ir_code", next_dynamic_ir(0x2)}});
    }

    if (m == "POST" && pathname == "/battle/self_save") {
        if (g_srv.battle.present) {
            g_srv.battle.player_conn = g_srv.battle.player_max_conn;
            g_srv.battle.player_status = "BATTLE";
            g_srv.battle.is_savior = false;
            if (TowerRecord *t = find_tower_by_id(g_srv.battle.tower_id)) {
                t->players_disconnected = 0;
                t->players_in_battle = 1;
            }
        }
        return ok(build_battle_status_json());
    }

    if (m == "POST" && pathname == "/battle/aar") {
        int attacks = g_srv.battle.present ? g_srv.battle.total_attacks : 0;
        return ok(json{
            {"total_attacks", attacks},
            {"total_damage", g_srv.battle.present ? g_srv.battle.total_damage : 0},
            {"xp_earned", g_srv.battle.present ? g_srv.battle.xp_earned : 0},
            {"avg_performance", attacks > 0 ? (g_srv.battle.perf_sum / attacks) : 0},
            {"failures", g_srv.battle.present ? g_srv.battle.failures : 0},
            {"coins_earned", g_srv.battle.present ? g_srv.battle.coins_earned : 0},
        });
    }

    return err(404, "Unknown endpoint");
}

#endif /* !__EMSCRIPTEN__ */
