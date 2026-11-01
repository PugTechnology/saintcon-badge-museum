/*
 * SAINTCON 2023 Badge ("Incidents") — In-Process Fake Game Server for Native `emu-cli`
 *
 * Mirrors `emulator/web/badges/2023/fake-server.js` using real ArduinoJson so
 * headless smoke scripts can exercise character registration, Incident selection,
 * dialogue, turn-based battles, inventory/minibadges, vending, level-up, and party.
 * Uses only fictional mascot, NPC, enemy, and bot-ally names.
 */

#include "emu_2023.h"
#include "include/Arduino.h"
#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kBotPartyCode = 232323;

struct MinibadgeDef {
    const char *label;
    const char *slot;
    const char *name;
    const char *stat;
    int hp;
    int atk;
    int def;
    int heal;
};

static const MinibadgeDef kCatalog[] = {
    {"Glasses ", "helmet", "Safety Glasses",    "+8 Max HP, +2 ATK",   8,  2,  0, 0},
    {"IDBadge ", "armor",  "Conference ID",     "+10 Max HP, +2 DEF", 10,  0,  2, 0},
    {"Polo    ", "armor",  "Responder Polo",    "+15 Max HP, +3 DEF", 15,  0,  3, 0},
    {"Keyboard", "weapon", "Standard Keyboard", "+6 ATK",              0,  6,  0, 0},
    {"KlipperH", "helmet", "Klipper Helm",      "+25 Max HP, +4 DEF", 25,  0,  4, 0},
    {"SCCap   ", "helmet", "SAINTCON Cap",      "+15 Max HP, +2 DEF", 15,  0,  2, 0},
    {"Mustache", "helmet", "Disguise Mustache", "+12 Max HP, +4 ATK", 12,  4,  0, 0},
    {"BlackHat", "helmet", "Black Hat",         "+10 Max HP, +10 ATK",10, 10,  0, 0},
    {"WhiteHat", "helmet", "White Hat",         "+20 Max HP, +5 DEF", 20,  0,  5, 0},
    {"Tinfoil ", "helmet", "Tinfoil Hat",       "+18 Max HP, +6 DEF", 18,  0,  6, 0},
    {"SCHoodie", "armor",  "SAINTCON Hoodie",   "+30 Max HP, +6 DEF", 30,  0,  6, 0},
    {"Hawaiian", "armor",  "Hawaiian Shirt",    "+22 Max HP, +4 ATK", 22,  4,  3, 0},
    {"T-Rex   ", "armor",  "Inflatable T-Rex",  "+40 Max HP, +7 DEF", 40,  0,  7, 0},
    {"Stress  ", "armor",  "Stress Ball Vest",  "+16 Max HP, +4 DEF", 16,  0,  4, 0},
    {"Sword   ", "weapon", "Pixel Broadsword",  "+14 ATK",             0, 14,  0, 0},
    {"CustKeeb", "weapon", "Custom Mech Keeb",  "+12 ATK, +5 Max HP",  5, 12,  0, 0},
    {"Lockpick", "weapon", "Titanium Picks",    "+11 ATK",             0, 11,  0, 0},
    {"Solderin", "weapon", "Soldering Iron",    "+16 ATK",             0, 16,  0, 0},
    {"Phone   ", "weapon", "Phreak Handset",    "+9 ATK, +10 Max HP", 10,  9,  0, 0},
    {"redactd ", "weapon", "Zero-Day Exploit",  "+20 ATK, +20 Max HP",20, 20,  0, 0},
    {"Coffee  ", "item",   "Hot Coffee",        "Use: Heal 40 HP",     0,  0,  0, 40},
    {"MtnDew  ", "item",   "Mountain Dew",      "Use: Heal 35 HP",     0,  0,  0, 35},
    {"Monster ", "item",   "Energy Drink",      "Use: Heal 45 HP",     0,  0,  0, 45},
    {"PepsiCan", "item",   "Chilled Soda",      "Use: Heal 30 HP",     0,  0,  0, 30},
    {"Pizza   ", "item",   "Hot Pizza Slice",   "Use: Heal 50 HP",     0,  0,  0, 50},
    {"Taco    ", "item",   "Street Taco",       "Use: Heal 45 HP",     0,  0,  0, 45},
    {"Cheetos ", "item",   "Cheetos Stash",     "Use: Heal 30 HP",     0,  0,  0, 30},
    {"Candy   ", "item",   "Sugar Rush Candy",  "Use: Heal 25 HP",     0,  0,  0, 25},
    {"1up     ", "item",   "1-UP Mushroom",     "Use: Heal 75 HP",     0,  0,  0, 75},
    {"CatVideo", "item",   "Cat Video Clip",    "Use: Heal 35 HP",     0,  0,  0, 35},
};

struct ClassBase {
    int hp;
    int atk;
    int def;
};

static const ClassBase kClasses[4] = {
    {110, 20, 4},
    {95,  25, 2},
    {100, 22, 3},
    {120, 19, 5},
};

struct StepDef {
    bool is_battle;
    const char *avatar_or_enemy;
    const char *text;
    int hp;
    int atk;
};

struct IncidentDef {
    int id;
    const char *name;
    int coins;
    int lootdrop;
    bool completes_training;
    std::vector<StepDef> steps;
};

static const IncidentDef kIncidents[7] = {
    {1, "1. Bot Training", 25, 1, true, {
        {false, "Klippy", "Welcome responder! Clear the training sim to unlock Party mode.", 0, 0},
        {true,  "TrainingBot1", "", 40, 7},
        {true,  "TrainingBot2", "", 55, 9},
        {false, "Klippy", "Final wave: defeat the Super Training Bot!", 0, 0},
        {true,  "SuperTrainingBot", "", 75, 11},
        {false, "Klippy", "Training complete! Party mode and Incident bounties are unlocked.", 0, 0},
    }},
    {2, "2. Packet Storm", 40, 1, false, {
        {false, "SentinelBot", "Core switch alert: malformed packets are flooding the uplink!", 0, 0},
        {true,  "PCAP", "", 80, 12},
        {true,  "DupACK", "", 95, 14},
        {false, "Player", "Duplicate ACKs cleared. Something is pinning the TCP window at zero!", 0, 0},
        {true,  "TCPZWindow", "", 115, 16},
        {false, "SentinelBot", "Link throughput restored. Packet capture archived to the vault.", 0, 0},
    }},
    {3, "3. Helpdesk Maze", 55, 1, false, {
        {false, "Receptionist", "Ticket queue critical! Callers are stuck in an endless IVR loop.", 0, 0},
        {true,  "PhoneTreeDeadends", "", 105, 15},
        {false, "Employee", "Pressing 0 just escalated us to the final boss of customer support!", 0, 0},
        {true,  "SavvyCustomerServiceRep", "", 135, 18},
        {false, "Receptionist", "Helpdesk queue cleared! Every open ticket is marked resolved.", 0, 0},
    }},
    {4, "4. Perimeter Breach", 75, 1, false, {
        {false, "SentinelBot", "Stealthy port scan detected probing the external DMZ!", 0, 0},
        {true,  "SneakyNMAP", "", 125, 18},
        {true,  "IPSentry", "", 145, 20},
        {true,  "WAF", "", 165, 22},
        {false, "Klippy", "Heads up! An unpatched legacy CVE is lurking behind the WAF!", 0, 0},
        {true,  "OldCVE", "", 190, 24},
        {false, "Klippy", "Emergency hotfix deployed. External perimeter hardened!", 0, 0},
    }},
    {5, "5. Physical Access", 95, 1, false, {
        {false, "Lockbot", "Motion sensors tripped along the datacenter corridor!", 0, 0},
        {true,  "RandomCeilingTile", "", 145, 21},
        {true,  "CrashCartWithABadWheel", "", 170, 23},
        {true,  "SecureDoor", "", 200, 26},
        {true,  "ForceFieldPadlock", "", 230, 28},
        {false, "Lockbot", "Mantrap sealed and badge readers verified.", 0, 0},
    }},
    {6, "6. Identity Vault", 120, 2, false, {
        {false, "Employee", "Legacy switch syntax and rogue auth tokens locked out the vault!", 0, 0},
        {true,  "DeprecatedSwitchSyntax", "", 175, 25},
        {true,  "RockyCoinChanger", "", 205, 27},
        {true,  "DoorControl", "", 235, 30},
        {true,  "CentralizedIdentityProvider", "", 270, 33},
        {false, "Klippy", "Signing keys rotated and Identity Vault access restored!", 0, 0},
    }},
    {7, "7. SOC Meltdown", 160, 2, false, {
        {false, "Reppilk", "Drown in raw telemetry! My minibadge swarm runs this SOC now!", 0, 0},
        {true,  "SIEMAlerts", "", 210, 30},
        {true,  "PilesAndPilesOfRawLogData", "", 260, 34},
        {false, "Klippy", "Alert storm parsed! Brace for the final boss: Minibadge Swarm!", 0, 0},
        {true,  "MinibadgeSwarm", "", 320, 38},
        {false, "Klippy", "All seven Incidents contained! The SAINTCON SOC is all green!", 0, 0},
    }},
};

struct RosterMember {
    std::string badge_id;
    std::string handle;
    int character;
    int max_hp;
    int current_hp;
    int atk;
    int def;
    int in_burnout;
    bool is_bot;
};

struct RoundActionEntry {
    bool is_enemy;
    std::string actor;
    std::string action;
    int damage_dealt;
    bool target_died;
    int heal;
    std::vector<std::pair<std::string, std::pair<int, bool>>> enemy_targets;
};

struct CliServerState {
    std::string badge_id = "202353430001";
    bool registered = false;
    std::string handle = "Responder";
    std::string email = "museum@sc23.local";
    int character = 0;
    int pclass = 0;
    int level = 1;
    int completed_training = 0;
    int coins = 15;
    int pending_lootdrop = 1;
    std::string equipped_slots[4];
    bool in_party = false;
    int party_code = 0;
    std::string party_owner;
    bool bot_allies = false;
    std::vector<int> issued_level_codes;
    uint32_t rng = 17;

    bool in_dungeon = false;
    int incident_id = 1;
    size_t step_index = 0;
    bool pending_enemy_defeated_ack = false;
    bool pending_recovery_ack = false;
    std::vector<RosterMember> roster;

    bool in_battle = false;
    std::string enemy_name;
    int enemy_hp = 0;
    int enemy_atk = 0;
    bool item_used = false;
    std::string battle_outcome;
    bool has_last_round = false;
    std::vector<RoundActionEntry> last_round;
};

static CliServerState g_srv;

static uint32_t next_roll(uint32_t span) {
    g_srv.rng = g_srv.rng * 1103515245u + 12345u;
    if (span <= 1) return 0;
    return (g_srv.rng >> 16) % span;
}

static const MinibadgeDef *find_mb(const std::string &lbl) {
    if (lbl.empty()) return nullptr;
    for (const auto &m : kCatalog) {
        if (lbl == m.label) return &m;
        std::string trimmed = m.label;
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        std::string ltrim = lbl;
        while (!ltrim.empty() && ltrim.back() == ' ') ltrim.pop_back();
        if (!ltrim.empty() && ltrim == trimmed) return &m;
    }
    return nullptr;
}

struct EquippedBest {
    const MinibadgeDef *helmet = nullptr;
    const MinibadgeDef *armor  = nullptr;
    const MinibadgeDef *weapon = nullptr;
    const MinibadgeDef *item   = nullptr;
};

static EquippedBest compute_equipped() {
    EquippedBest best;
    auto score_of = [](const MinibadgeDef *d) {
        return d ? (d->hp + d->atk * 3 + d->def * 3 + d->heal * 2) : -1;
    };
    for (int i = 0; i < 4; i++) {
        const MinibadgeDef *m = find_mb(g_srv.equipped_slots[i]);
        if (!m) continue;
        int sc = score_of(m);
        if (strcmp(m->slot, "helmet") == 0 && sc > score_of(best.helmet)) best.helmet = m;
        else if (strcmp(m->slot, "armor") == 0 && sc > score_of(best.armor)) best.armor = m;
        else if (strcmp(m->slot, "weapon") == 0 && sc > score_of(best.weapon)) best.weapon = m;
        else if (strcmp(m->slot, "item") == 0 && sc > score_of(best.item)) best.item = m;
    }
    return best;
}

struct CombatStats {
    int max_hp;
    int atk;
    int def;
    int heal;
    bool has_item;
};

static CombatStats compute_player_stats() {
    int cid = std::max(0, std::min(3, g_srv.pclass));
    int lvl = std::max(1, std::min(4, g_srv.level));
    EquippedBest best = compute_equipped();
    CombatStats st{
        kClasses[cid].hp + (lvl - 1) * 25,
        kClasses[cid].atk + (lvl - 1) * 7,
        kClasses[cid].def + (lvl - 1) * 2,
        30,
        false
    };
    for (const MinibadgeDef *g : {best.helmet, best.armor, best.weapon, best.item}) {
        if (!g) continue;
        st.max_hp += g->hp;
        st.atk += g->atk;
        st.def += g->def;
    }
    if (best.item && best.item->heal > 0) {
        st.heal = best.item->heal;
        st.has_item = true;
    }
    return st;
}

static std::vector<RosterMember> build_roster() {
    CombatStats st = compute_player_stats();
    std::vector<RosterMember> out;
    out.push_back({g_srv.badge_id, g_srv.handle.empty() ? "Responder" : g_srv.handle,
                   g_srv.character, st.max_hp, st.max_hp, st.atk, st.def, 0, false});
    if (g_srv.in_party && g_srv.bot_allies) {
        int lvl = std::max(1, std::min(4, g_srv.level));
        out.push_back({"B07A11A00001", "BotAlpha", 1, 85 + (lvl - 1) * 20, 85 + (lvl - 1) * 20, 15 + (lvl - 1) * 5, 3 + (lvl - 1), 0, true});
        out.push_back({"B07A11A00002", "BotBravo", 2, 95 + (lvl - 1) * 20, 95 + (lvl - 1) * 20, 17 + (lvl - 1) * 5, 3 + (lvl - 1), 0, true});
    }
    return out;
}

static const IncidentDef *find_incident(int id) {
    for (const auto &inc : kIncidents) {
        if (inc.id == id) return &inc;
    }
    return nullptr;
}

static std::string extract_path(const char *url) {
    if (!url) return "/";
    const char *p = strstr(url, "://");
    if (p) {
        const char *slash = strchr(p + 3, '/');
        return slash ? std::string(slash) : "/";
    }
    return std::string(url);
}

static void parse_auth_header(const char *headers) {
    if (!headers) return;
    std::string h(headers);
    size_t pos = 0;
    while (pos < h.size()) {
        size_t nl = h.find('\n', pos);
        std::string line = (nl == std::string::npos) ? h.substr(pos) : h.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? h.size() : nl + 1;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            for (char &c : k) c = (char)tolower((unsigned char)c);
            if (k == "x-auth-id") {
                std::string v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
                if (!v.empty()) g_srv.badge_id = v;
            }
        }
    }
}

static void start_battle(const StepDef &step) {
    CombatStats st = compute_player_stats();
    if (!g_srv.roster.empty()) {
        auto &p0 = g_srv.roster[0];
        int delta = st.max_hp - p0.max_hp;
        p0.max_hp = st.max_hp;
        p0.atk = st.atk;
        p0.def = st.def;
        if (delta > 0 && p0.current_hp > 0) p0.current_hp = std::min(p0.max_hp, p0.current_hp + delta);
        else p0.current_hp = std::min(p0.max_hp, p0.current_hp);
    }
    g_srv.in_battle = true;
    g_srv.enemy_name = step.avatar_or_enemy;
    g_srv.enemy_hp = step.hp;
    g_srv.enemy_atk = step.atk;
    g_srv.item_used = false;
    g_srv.battle_outcome.clear();
    g_srv.has_last_round = false;
    g_srv.last_round.clear();
}

static void fill_battle_init(JsonObject data) {
    JsonObject enemy = data.createNestedObject("enemy");
    enemy["name"] = g_srv.enemy_name.c_str();
    enemy["hp"] = g_srv.enemy_hp;
    JsonArray chars = data.createNestedArray("characters");
    for (const auto &m : g_srv.roster) {
        JsonObject c = chars.createNestedObject();
        c["character"] = m.character;
        c["badge_id"] = m.badge_id.c_str();
        c["current_hp"] = m.current_hp;
        c["max_hp"] = m.max_hp;
        c["handle"] = m.handle.c_str();
        c["in_burnout"] = m.in_burnout;
    }
}

static void resolve_round(const std::string &act_type) {
    CombatStats st = compute_player_stats();
    g_srv.last_round.clear();
    std::vector<bool> cowering(g_srv.roster.size(), false);
    std::vector<int> pending_heal(g_srv.roster.size(), 0);

    for (size_t i = 0; i < g_srv.roster.size(); i++) {
        auto &m = g_srv.roster[i];
        if (m.in_burnout || m.current_hp <= 0) continue;
        std::string act = m.is_bot ? "attack" : act_type;
        if (act == "cower") {
            cowering[i] = true;
            g_srv.last_round.push_back({false, m.badge_id, "cower", 0, false, 0, {}});
        } else if (act == "use_item") {
            int raw_heal = (!g_srv.item_used && st.has_item) ? st.heal : 25;
            g_srv.item_used = true;
            pending_heal[i] = raw_heal;
            g_srv.last_round.push_back({false, m.badge_id, "use_item", 0, false, raw_heal, {}});
        } else {
            int dmg = 0;
            bool killed = false;
            if (g_srv.enemy_hp > 0) {
                dmg = std::max(5, m.atk + (int)next_roll(5));
                if (dmg >= g_srv.enemy_hp) {
                    g_srv.enemy_hp = 0;
                    killed = true;
                } else {
                    g_srv.enemy_hp -= dmg;
                }
            }
            g_srv.last_round.push_back({false, m.badge_id, "attack", dmg, killed, 0, {}});
        }
    }

    if (g_srv.enemy_hp <= 0) {
        g_srv.battle_outcome = "victory";
        g_srv.has_last_round = true;
        return;
    }

    std::vector<size_t> alive;
    for (size_t i = 0; i < g_srv.roster.size(); i++) {
        if (!g_srv.roster[i].in_burnout && g_srv.roster[i].current_hp > 0) alive.push_back(i);
    }

    RoundActionEntry enemy_act{true, "enemy", "", 0, false, 0, {}};
    if (!alive.empty()) {
        size_t target_pick = alive[next_roll((uint32_t)alive.size())];
        for (size_t idx : alive) {
            auto &m = g_srv.roster[idx];
            if (idx != target_pick) {
                enemy_act.enemy_targets.push_back({m.badge_id, {0, false}});
                continue;
            }
            int dmg = std::max(2, g_srv.enemy_atk - m.def + (int)next_roll(4) - 1);
            if (cowering[idx]) dmg = std::max(1, (dmg * 35) / 100);
            bool died = false;
            if (dmg >= m.current_hp) {
                dmg = m.current_hp;
                m.current_hp = 0;
                m.in_burnout = 1;
                died = true;
            } else {
                m.current_hp -= dmg;
            }
            enemy_act.enemy_targets.push_back({m.badge_id, {dmg, died}});
        }
    }

    for (auto &entry : g_srv.last_round) {
        if (!entry.is_enemy && entry.action == "use_item") {
            for (size_t i = 0; i < g_srv.roster.size(); i++) {
                if (g_srv.roster[i].badge_id == entry.actor) {
                    auto &m = g_srv.roster[i];
                    if (m.in_burnout || m.current_hp <= 0) {
                        entry.heal = 0;
                    } else {
                        int actual = std::max(0, std::min(pending_heal[i], m.max_hp - m.current_hp));
                        m.current_hp += actual;
                        entry.heal = actual;
                    }
                }
            }
        }
    }

    g_srv.last_round.push_back(enemy_act);
    bool any_alive = false;
    for (const auto &m : g_srv.roster) {
        if (!m.in_burnout && m.current_hp > 0) any_alive = true;
    }
    if (!any_alive) g_srv.battle_outcome = "defeat";
    g_srv.has_last_round = true;
}

static int finish_doc(const JsonDocument &doc, char **out_body) {
    String s;
    serializeJson(doc, s);
    if (out_body) {
        *out_body = strdup(s.c_str());
    }
    return 200;
}

static int err_doc(const char *msg, char **out_body) {
    DynamicJsonDocument doc(512);
    doc["status"] = false;
    doc["error"] = msg;
    return finish_doc(doc, out_body);
}

} /* namespace */

extern "C" {

void sc23_cli_fake_server_reset(void) {
    g_srv = CliServerState{};
}

uint32_t sc23_cli_issue_levelup_code(void) {
    int code = 410100 + (int)next_roll(89000);
    g_srv.issued_level_codes.push_back(code);
    return (uint32_t)code;
}

void sc23_cli_set_bot_allies(int enabled) {
    if (enabled && !g_srv.completed_training) {
        return;  // bots join only after "1. Bot Training", as in the browser stand-in
    }
    if (enabled) {
        g_srv.in_party = true;
        g_srv.party_code = kBotPartyCode;
        g_srv.party_owner = g_srv.badge_id;
        g_srv.bot_allies = true;
    } else {
        g_srv.bot_allies = false;
    }
}

int sc23_cli_fake_server_handle(const char *method,
                                const char *url,
                                const char *headers,
                                const char *body,
                                char **out_body) {
    parse_auth_header(headers);
    std::string path = extract_path(url);
    bool is_post = method && (strcmp(method, "POST") == 0 || strcmp(method, "post") == 0);

    DynamicJsonDocument req(2048);
    if (body && body[0] != '\0') {
        deserializeJson(req, body);
    }

    DynamicJsonDocument resp(8192);
    resp["status"] = true;

    if (path == "/badge/firmware_version") {
        resp["response"]["version"] = 1.4;
        return finish_doc(resp, out_body);
    }

    if (path == "/badge/whois") {
        if (!g_srv.registered) {
            resp["response"] = "unregistered";
        } else {
            JsonObject r = resp.createNestedObject("response");
            r["character"] = g_srv.character;
            r["email"] = g_srv.email.c_str();
            r["eyecolor"] = 1;
            r["haircolor"] = 1;
            r["handle"] = g_srv.handle.c_str();
            r["class"] = g_srv.pclass;
            r["completed_training"] = g_srv.completed_training;
            r["staff"] = 0;
            r["level"] = g_srv.level;
        }
        return finish_doc(resp, out_body);
    }

    if (path == "/badge/register") {
        g_srv.pclass = std::max(0, std::min(3, req["class"].as<int>()));
        g_srv.character = std::max(0, std::min(3, req["character"].as<int>()));
        const char *h = req["handle"] | "Responder";
        const char *e = req["email"] | "";
        g_srv.handle = (h && h[0] != '\0') ? h : "Responder";
        g_srv.email = e ? e : "";
        g_srv.registered = true;
        resp["response"] = "registered";
        return finish_doc(resp, out_body);
    }

    if (path == "/badge/equip") {
        const char *snames[4] = {"slot1", "slot2", "slot3", "slot4"};
        for (int i = 0; i < 4; i++) {
            const char *nm = req[snames[i]]["name"] | "";
            g_srv.equipped_slots[i] = nm ? nm : "";
        }
        EquippedBest best = compute_equipped();
        JsonObject r = resp.createNestedObject("response");
        auto put_gear = [&](const char *key, const MinibadgeDef *d, bool is_item) {
            if (!d) return;
            JsonObject g = r.createNestedObject(key);
            g["name"] = d->name;
            g["stat"] = d->stat;
            if (is_item) {
                g["usable"] = true;
                g["useable"] = true;
            }
        };
        put_gear("helmet", best.helmet, false);
        put_gear("armor",  best.armor,  false);
        put_gear("weapon", best.weapon, false);
        put_gear("item",   best.item,   true);
        return finish_doc(resp, out_body);
    }

    if (path == "/badge/levelup") {
        int code = req["levelup_code"].as<int>();
        if (g_srv.level >= 4) return err_doc("Already at max level 4", out_body);
        bool ok = (code >= 202301 && code <= 202304);
        auto it = std::find(g_srv.issued_level_codes.begin(), g_srv.issued_level_codes.end(), code);
        if (it != g_srv.issued_level_codes.end()) {
            ok = true;
            g_srv.issued_level_codes.erase(it);
        }
        if (!ok) return err_doc("Invalid level code", out_body);
        g_srv.level = std::min(4, g_srv.level + 1);
        resp["response"]["level"] = g_srv.level;
        return finish_doc(resp, out_body);
    }

    if (path == "/badge/generate_levelup") {
        resp["response"]["levelup_code"] = (int)sc23_cli_issue_levelup_code();
        return finish_doc(resp, out_body);
    }

    if (path == "/dungeon") {
        JsonObject r = resp.createNestedObject("response");
        for (const auto &inc : kIncidents) {
            char key[8];
            snprintf(key, sizeof(key), "%d", inc.id);
            JsonObject d = r.createNestedObject(key);
            d["name"] = inc.name;
            d["available"] = true;
        }
        return finish_doc(resp, out_body);
    }

    if (path.rfind("/dungeon/enter/", 0) == 0) {
        int id = atoi(path.c_str() + strlen("/dungeon/enter/"));
        const IncidentDef *inc = find_incident(id);
        if (!inc) return err_doc("Unknown Incident", out_body);
        g_srv.in_dungeon = true;
        g_srv.incident_id = id;
        g_srv.step_index = 0;
        g_srv.pending_enemy_defeated_ack = false;
        g_srv.pending_recovery_ack = false;
        g_srv.roster = build_roster();
        g_srv.in_battle = false;
        resp["response"] = "entered";
        return finish_doc(resp, out_body);
    }

    if (path == "/dungeon/update") {
        const IncidentDef *inc = find_incident(g_srv.incident_id);
        if (!g_srv.in_dungeon || !inc) return err_doc("Not in an Incident", out_body);
        const char *act = req["action"] | "";
        bool is_confirm = is_post && strcmp(act, "confirm_dialogue") == 0;

        if (g_srv.pending_recovery_ack) {
            if (is_confirm) {
                g_srv.pending_recovery_ack = false;
                g_srv.roster = build_roster();
                start_battle(inc->steps[g_srv.step_index]);
                JsonObject r = resp.createNestedObject("response");
                r["type"] = "battle";
                fill_battle_init(r.createNestedObject("data"));
                return finish_doc(resp, out_body);
            }
            JsonObject r = resp.createNestedObject("response");
            r["type"] = "dialogue";
            r["data"]["avatar"] = "Klippy";
            r["data"]["text"] = "Burnout recovery complete! HP restored to 100% for another try.";
            return finish_doc(resp, out_body);
        }

        if (g_srv.in_battle && g_srv.battle_outcome == "victory") {
            g_srv.in_battle = false;
            g_srv.pending_enemy_defeated_ack = true;
            for (auto &m : g_srv.roster) {
                if (!m.in_burnout && m.current_hp > 0) {
                    m.current_hp = std::min(m.max_hp, m.current_hp + std::max(8, m.max_hp / 4));
                }
            }
            JsonObject r = resp.createNestedObject("response");
            r["type"] = "admin";
            r["message"] = "enemy_defeated";
            r.createNestedObject("data");
            return finish_doc(resp, out_body);
        }

        if (g_srv.in_battle && g_srv.battle_outcome == "defeat") {
            g_srv.in_battle = false;
            g_srv.pending_recovery_ack = true;
            JsonObject r = resp.createNestedObject("response");
            r["type"] = "dialogue";
            r["data"]["avatar"] = "Klippy";
            r["data"]["text"] = "Burnout recovery complete! HP restored to 100% for another try.";
            return finish_doc(resp, out_body);
        }

        if (g_srv.pending_enemy_defeated_ack) {
            if (is_confirm) {
                g_srv.pending_enemy_defeated_ack = false;
                g_srv.step_index++;
            }
        } else if (is_confirm) {
            if (g_srv.step_index < inc->steps.size() && !inc->steps[g_srv.step_index].is_battle) {
                g_srv.step_index++;
            }
        }

        if (g_srv.step_index >= inc->steps.size()) {
            JsonObject r = resp.createNestedObject("response");
            r["type"] = "admin";
            r["message"] = "dungeon_complete";
            r.createNestedObject("data");
            return finish_doc(resp, out_body);
        }

        const StepDef &step = inc->steps[g_srv.step_index];
        JsonObject r = resp.createNestedObject("response");
        if (!step.is_battle) {
            r["type"] = "dialogue";
            r["data"]["avatar"] = step.avatar_or_enemy;
            r["data"]["text"] = step.text;
        } else {
            if (!g_srv.in_battle) start_battle(step);
            r["type"] = "battle";
            fill_battle_init(r.createNestedObject("data"));
        }
        return finish_doc(resp, out_body);
    }

    if (path == "/dungeon/battle") {
        if (!g_srv.in_battle || g_srv.enemy_hp <= 0 || g_srv.battle_outcome == "victory") {
            return err_doc("enemy defeated", out_body);
        }
        const char *t = req["type"] | "attack";
        resolve_round(t);
        resp["response"] = "action_recorded";
        return finish_doc(resp, out_body);
    }

    if (path == "/dungeon/battle/update") {
        if (!g_srv.in_battle) return err_doc("No active battle", out_body);
        if (!g_srv.has_last_round) {
            resp["battle_result"] = false;
            JsonObject r = resp.createNestedObject("response");
            r["type"] = "admin";
            r["latest_update"] = true;
            r["message"] = "send_action";
            return finish_doc(resp, out_body);
        }
        resp["battle_result"] = true;
        JsonObject r = resp.createNestedObject("response");
        r["type"] = "round";
        r["latest_update"] = true;
        JsonArray arr = r.createNestedArray("data");
        for (const auto &e : g_srv.last_round) {
            JsonObject o = arr.createNestedObject();
            if (e.is_enemy) {
                o["actor"] = "enemy";
                JsonObject tgts = o.createNestedObject("targets");
                for (const auto &kv : e.enemy_targets) {
                    JsonObject to = tgts.createNestedObject(kv.first.c_str());
                    to["damage_dealt"] = kv.second.first;
                    to["target_died"] = kv.second.second;
                }
            } else {
                o["actor"] = e.actor.c_str();
                o["action"] = e.action.c_str();
                o["damage_dealt"] = e.damage_dealt;
                o["target_died"] = e.target_died;
                o["heal"] = e.heal;
            }
        }
        return finish_doc(resp, out_body);
    }

    if (path.rfind("/dungeon/exit/", 0) == 0) {
        int id = atoi(path.c_str() + strlen("/dungeon/exit/"));
        const IncidentDef *inc = find_incident(id);
        int coins = inc ? inc->coins : 25;
        int drops = inc ? inc->lootdrop : 1;
        g_srv.coins += coins;
        g_srv.pending_lootdrop += drops;
        if (inc && inc->completes_training) g_srv.completed_training = 1;
        g_srv.in_dungeon = false;
        g_srv.in_battle = false;
        JsonObject d = resp.createNestedObject("response").createNestedObject("data");
        d["coins"] = coins;
        d["lootdrop"] = drops;
        return finish_doc(resp, out_body);
    }

    if (path == "/party") {
        if (!g_srv.in_party) return err_doc("not in party", out_body);
        std::string owner = g_srv.party_owner.empty() ? g_srv.badge_id : g_srv.party_owner;
        resp["party_code"] = g_srv.party_code ? g_srv.party_code : kBotPartyCode;
        resp["party_onwer"] = owner.c_str();
        resp["party_owner"] = owner.c_str();
        JsonArray arr = resp.createNestedArray("response");
        JsonObject p0 = arr.createNestedObject();
        p0["handle"] = g_srv.handle.c_str();
        p0["character"] = g_srv.character;
        if (g_srv.bot_allies) {
            JsonObject b1 = arr.createNestedObject();
            b1["handle"] = "BotAlpha";
            b1["character"] = 1;
            JsonObject b2 = arr.createNestedObject();
            b2["handle"] = "BotBravo";
            b2["character"] = 2;
        }
        return finish_doc(resp, out_body);
    }

    if (path == "/party/create") {
        g_srv.in_party = true;
        g_srv.party_code = 230100 + (int)next_roll(8900);
        g_srv.party_owner = g_srv.badge_id;
        resp["response"]["code"] = g_srv.party_code;
        return finish_doc(resp, out_body);
    }

    if (path == "/party/join") {
        int code = req["party_code"].as<int>();
        if (code < 100000 || code > 999999) return err_doc("Party not found", out_body);
        g_srv.in_party = true;
        g_srv.party_code = code;
        g_srv.party_owner = "B07A11A00001";
        g_srv.bot_allies = true;
        resp["response"] = "joined";
        return finish_doc(resp, out_body);
    }

    if (path == "/party/leave" || path == "/party/destroy") {
        g_srv.in_party = false;
        g_srv.party_code = 0;
        g_srv.bot_allies = false;
        resp["response"] = "left";
        return finish_doc(resp, out_body);
    }

    if (path == "/bank/statement") {
        resp["response"]["balance"] = g_srv.coins;
        return finish_doc(resp, out_body);
    }

    if (path == "/vend") {
        resp["response"]["pending_lootdrop"] = g_srv.pending_lootdrop;
        return finish_doc(resp, out_body);
    }

    if (path == "/vend/unlock") {
        if (g_srv.pending_lootdrop <= 0) return err_doc("No loot drops left", out_body);
        g_srv.pending_lootdrop--;
        g_srv.coins += 25;
        resp["response"]["unlock_code"] = 601000 + (int)next_roll(98000);
        return finish_doc(resp, out_body);
    }

    return err_doc("Unknown endpoint", out_body);
}

} /* extern "C" */
