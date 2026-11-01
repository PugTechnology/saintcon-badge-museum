#include "platform_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {

struct FactionDef {
    int team_id;
    const char *name;
    const char *color;
    int hq_node;
    int r2_main;
    int r2_tower;
    int tower_node;
    int r1_node;
};

static const FactionDef kFactions[6] = {
    {1, "Aether Watch", "#00AAE9", 20, 19,  8, 26, 2},
    {2, "Verdant Pact", "#00A359", 21,  9, 10, 27, 3},
    {3, "Dawn Accord",  "#FFF34A", 22, 11, 12, 28, 4},
    {4, "Iron Howl",    "#EA983E", 23, 13, 14, 29, 5},
    {5, "Ember Claw",   "#DA3832", 24, 15, 16, 30, 6},
    {6, "Dream Coil",   "#862F8B", 25, 17, 18, 31, 7},
};

/* Exact 31-node port uplift table from components/badge_game/nodes.c (1-based node IDs, 0 = none) */
static const int kNodeUplinks[32][6] = {
    { 0,  0,  0,  0,  0,  0}, // 0 (unused)
    { 2,  3,  4,  5,  6,  7}, // 1: CORE
    { 1,  3,  7,  8,  9, 19}, // 2: RING1
    { 1,  2,  4,  9, 10, 11}, // 3: RING1
    { 1,  3,  5, 11, 12, 13}, // 4: RING1
    { 1,  4,  6, 13, 14, 15}, // 5: RING1
    { 1,  5,  7, 15, 16, 17}, // 6: RING1
    { 1,  2,  6, 17, 18, 19}, // 7: RING1
    { 2,  9, 19, 20, 21, 26}, // 8: RING2
    { 2,  3,  8, 10, 21,  0}, // 9: RING2
    { 3,  9, 11, 21, 22, 27}, // 10: RING2
    { 3,  4, 10, 12, 22,  0}, // 11: RING2
    { 4, 11, 13, 22, 23, 28}, // 12: RING2
    { 4,  5, 12, 14, 23,  0}, // 13: RING2
    { 5, 13, 15, 23, 24, 29}, // 14: RING2
    { 5,  6, 14, 16, 24,  0}, // 15: RING2
    { 6, 15, 17, 24, 25, 30}, // 16: RING2
    { 6,  7, 16, 18, 25,  0}, // 17: RING2
    { 7, 17, 19, 20, 25, 31}, // 18: RING2
    { 2,  7,  8, 18, 20,  0}, // 19: RING2
    { 8, 18, 19,  0,  0,  0}, // 20: HQ (Team 1 - Aether Watch)
    { 8,  9, 10,  0,  0,  0}, // 21: HQ (Team 2 - Verdant Pact)
    {10, 11, 12,  0,  0,  0}, // 22: HQ (Team 3 - Dawn Accord)
    {12, 13, 14,  0,  0,  0}, // 23: HQ (Team 4 - Iron Howl)
    {14, 15, 16,  0,  0,  0}, // 24: HQ (Team 5 - Ember Claw)
    {16, 17, 18,  0,  0,  0}, // 25: HQ (Team 6 - Dream Coil)
    { 8,  0,  0,  0,  0,  0}, // 26: TOWER
    {10,  0,  0,  0,  0,  0}, // 27: TOWER
    {12,  0,  0,  0,  0,  0}, // 28: TOWER
    {14,  0,  0,  0,  0,  0}, // 29: TOWER
    {16,  0,  0,  0,  0,  0}, // 30: TOWER
    {18,  0,  0,  0,  0,  0}, // 31: TOWER
};

struct NutEntry {
    std::string type;
    int faction_override = 0;
    bool redeemed = false;
};

struct SwapEntry {
    std::string code;
    int credits = 0;
    int polls = 0;
    bool redeemed = false;
    bool cancelled = false;
};

struct ServerState {
    uint32_t seed = 0x2025u;
    bool registered = false;
    std::string player_name;
    int team_id = 3; // Default pre-assigned team (1..6), revealed on Sorting Hat
    int credits = 100;
    int level = 1;
    bool sorting_hat = false;
    bool konami_done = false;
    bool dawn_accord_reported = false;
    int telecom_unlocks = 0;

    std::map<std::string, NutEntry> issued_nuts;
    std::set<std::string> redeemed_codes;
    std::map<std::string, SwapEntry> swaps;
    std::set<int> read_messages;
};

static ServerState g_srv;

static const FactionDef &get_faction_def(int team_id) {
    if (team_id < 1 || team_id > 6) return kFactions[2];
    return kFactions[team_id - 1];
}

static int find_port_for_peer(int node_a, int node_b) {
    for (int p = 0; p < 6; p++) {
        if (kNodeUplinks[node_a][p] == node_b) {
            return p + 1; // 1-based port number
        }
    }
    return 0;
}

static std::string extract_path(const std::string &url) {
    size_t pos = url.find("://");
    size_t start = (pos == std::string::npos) ? 0 : url.find('/', pos + 3);
    if (start == std::string::npos) return "/";
    size_t q = url.find('?', start);
    return (q == std::string::npos) ? url.substr(start) : url.substr(start, q - start);
}

static json build_status_data() {
    const auto &f = get_faction_def(g_srv.team_id);
    return {
        {"player_name", g_srv.player_name},
        {"team_id", g_srv.team_id},
        {"team_name", f.name},
        {"credits", g_srv.credits},
        {"level", g_srv.level},
        {"registered_at", "2025-10-21T09:00:00Z"},
        {"sorting_hat", g_srv.sorting_hat},
        {"has_messages", g_srv.read_messages.size() < 2},
        {"firmware_info", {
            {"current_version", "0.2.1"},
            {"latest_version", "0.2.1"},
            {"update_available", false}
        }}
    };
}

static json build_topology_response() {
    int node_owner[32];
    for (int i = 0; i < 32; i++) node_owner[i] = -1;

    // All 6 Faction HQs belong to teams 1..6
    for (int t = 1; t <= 6; t++) {
        node_owner[19 + t] = t;
    }

    // Key: (node_id, port_1based) -> "NONE" | "PARTIAL" | "FULL"
    std::map<std::pair<int, int>, std::string> port_links;
    auto set_link = [&](int a, int b, const std::string &st_a, const std::string &st_b) {
        int pa = find_port_for_peer(a, b);
        int pb = find_port_for_peer(b, a);
        if (pa > 0) port_links[{a, pa}] = st_a;
        if (pb > 0) port_links[{b, pb}] = st_b;
    };

    // Baseline simulated conference activity from rival factions so the grid looks alive
    int rival1 = (g_srv.team_id == 1) ? 2 : 1;
    int rival2 = (g_srv.team_id == 5) ? 4 : 5;
    const auto &rf1 = get_faction_def(rival1);
    const auto &rf2 = get_faction_def(rival2);

    node_owner[rf1.r2_tower] = rf1.team_id;
    node_owner[rf1.tower_node] = rf1.team_id;
    set_link(rf1.hq_node, rf1.r2_tower, "FULL", "FULL");
    set_link(rf1.r2_tower, rf1.tower_node, "FULL", "FULL");
    set_link(rf1.r2_tower, rf1.r1_node, "PARTIAL", "NONE");

    node_owner[rf2.r2_main] = rf2.team_id;
    node_owner[rf2.r1_node] = rf2.team_id;
    set_link(rf2.hq_node, rf2.r2_main, "FULL", "FULL");
    set_link(rf2.r2_main, rf2.r1_node, "FULL", "FULL");
    set_link(rf2.r1_node, 1, "PARTIAL", "NONE");

    // Player faction topology progression driven by Telecom Nuts
    const auto &pf = get_faction_def(g_srv.team_id);
    set_link(pf.hq_node, pf.r2_main, "PARTIAL", "NONE");

    if (g_srv.telecom_unlocks >= 1) {
        node_owner[pf.r2_main] = pf.team_id;
        node_owner[pf.r2_tower] = pf.team_id;
        node_owner[pf.tower_node] = pf.team_id;
        set_link(pf.hq_node, pf.r2_main, "FULL", "FULL");
        set_link(pf.hq_node, pf.r2_tower, "FULL", "FULL");
        set_link(pf.r2_tower, pf.tower_node, "FULL", "FULL");
        set_link(pf.r2_main, pf.r1_node, "PARTIAL", "NONE");
    }
    if (g_srv.telecom_unlocks >= 2) {
        node_owner[pf.r1_node] = pf.team_id;
        set_link(pf.r2_main, pf.r1_node, "FULL", "FULL");
        set_link(pf.r2_tower, pf.r1_node, "FULL", "FULL");
        set_link(pf.r1_node, 1, "PARTIAL", "NONE");
    }
    if (g_srv.telecom_unlocks >= 3) {
        set_link(pf.r1_node, 1, "FULL", "FULL");
    }

    json nodes_arr = json::array();
    for (int id = 1; id <= 31; id++) {
        const char *ntype = "RING2";
        int pcount = 6;
        if (id == 1) {
            ntype = "CORE";
        } else if (id >= 2 && id <= 7) {
            ntype = "RING1";
        } else if (id >= 20 && id <= 25) {
            ntype = "HQ";
            pcount = 3;
        } else if (id >= 26 && id <= 31) {
            ntype = "TOWER";
            pcount = 1;
        }
        int owner = node_owner[id];
        json team_val = (owner > 0) ? json(owner) : json(nullptr);
        const char *col = (owner > 0) ? get_faction_def(owner).color : "#88929E";
        nodes_arr.push_back({
            {"id", id},
            {"node_type", ntype},
            {"x_coord", 0.0},
            {"y_coord", 0.0},
            {"port_count", pcount},
            {"team_id", team_val},
            {"color", col},
            {"is_online", true},
            {"halo_color", col},
            {"is_unlocked", 1}
        });
    }

    json conns_arr = json::array();
    json ports_arr = json::array();
    int conn_id = 1;
    for (int a = 1; a <= 31; a++) {
        for (int p = 0; p < 6; p++) {
            int b = kNodeUplinks[a][p];
            if (b <= 0) continue;
            int pa = p + 1;
            auto it = port_links.find({a, pa});
            std::string lstat = (it != port_links.end()) ? it->second : "NONE";
            ports_arr.push_back({
                {"node_id", a},
                {"port_number", pa},
                {"connected", lstat == "NONE" ? 0 : 1},
                {"link_status", lstat},
                {"updated_at", "2025-10-21T12:00:00Z"}
            });
            if (a < b) {
                int pb = find_port_for_peer(b, a);
                conns_arr.push_back({
                    {"id", conn_id++},
                    {"node_a_id", a},
                    {"node_a_port", pa},
                    {"node_b_id", b},
                    {"node_b_port", pb},
                    {"connection_type", "WIRED"}
                });
            }
        }
    }

    bool has_core = (g_srv.telecom_unlocks >= 3);
    json path_arr = json::array();
    path_arr.push_back({
        {"id", 1},
        {"team_id", g_srv.team_id},
        {"has_path_to_core", has_core ? 1 : 0},
        {"path_via_tower", g_srv.telecom_unlocks >= 1 ? 1 : 0},
        {"path_nodes", std::to_string(pf.hq_node) + "," + std::to_string(pf.r2_main) + "," +
                       std::to_string(pf.r1_node) + ",1"},
        {"updated_at", "2025-10-21T12:00:00Z"},
        {"is_scored_path", has_core ? 1 : 0}
    });

    json scores_arr = json::array();
    if (has_core) {
        scores_arr.push_back({
            {"team_id", g_srv.team_id},
            {"first_node", pf.hq_node},
            {"last_node", 1},
            {"created_at", "2025-10-21T12:05:00Z"}
        });
    }

    return {
        {"status", "ok"},
        {"timestamp", "2025-10-21T12:05:00Z"},
        {"nodes", nodes_arr},
        {"connections", conns_arr},
        {"port_status", ports_arr},
        {"path_status", path_arr},
        {"team_path_scores", scores_arr}
    };
}

} // namespace

extern "C" void cli_fake_server_reset(uint32_t seed) {
    g_srv = ServerState{};
    g_srv.seed = seed ? seed : 0x2025u;
    g_srv.team_id = 3; // Dawn Accord by default in CLI (or overridden by Sorting Hat nut)
    g_srv.credits = 100;
    g_srv.level = 1;
}

extern "C" void cli_fake_server_register_nut(const char *code5, const char *nut_type_str, int faction_override) {
    if (!code5) return;
    std::string code(code5);
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);
    NutEntry e;
    e.type = nut_type_str ? nut_type_str : "MISC";
    e.faction_override = faction_override;
    e.redeemed = false;
    g_srv.issued_nuts[code] = e;
    if (e.type == "SORTING_HAT" && faction_override >= 1 && faction_override <= 6) {
        g_srv.team_id = faction_override;
    }
}

extern "C" int cli_fake_server_handle(const char *method_c,
                                      const char *url_c,
                                      const char *body_c,
                                      size_t body_len,
                                      char **out_headers_lines,
                                      char **out_body,
                                      size_t *out_body_len) {
    std::string method = method_c ? method_c : "GET";
    std::string path = extract_path(url_c ? url_c : "/");
    std::string req_str = (body_c && body_len > 0) ? std::string(body_c, body_len) : "";
    json req_json = json::object();
    if (!req_str.empty() && json::accept(req_str)) {
        req_json = json::parse(req_str);
    }

    int status = 200;
    std::string hdrs;
    std::string resp_body;

    if (method == "HEAD" && path == "/api/firmware/badge") {
        hdrs = "Content-Type: application/octet-stream\n"
               "Content-Length: 1048576\n"
               "X-Firmware-Version: 0.2.1\n"
               "X-Device-Type: badge\n"
               "Last-Modified: Tue, 11 Nov 2025 00:00:00 GMT\n";
        resp_body = "";
    } else if (path == "/api/firmware/version") {
        resp_body = json({
            {"status", "ok"},
            {"data", {
                {"device_type", "badge"},
                {"setting", "firmware_version_badge"},
                {"value", "0.2.1"}
            }}
        }).dump();
    } else if (path == "/api/badge/challenge") {
        resp_body = json({
            {"status", "ok"},
            {"data", {{"nonce", "0123456789abcdef0123456789abcdef"}}}
        }).dump();
    } else if (path == "/api/badge/provision") {
        resp_body = json({
            {"status", "ok"},
            {"data", {
                {"hmac_secret", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"},
                {"hash_prefix", "a0a1a2a3a4a5a6a7a8a9aaabacad0e0f"},
                {"ctf_hmac_secret", "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"},
                {"ctf_hash_prefix", "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"}
            }}
        }).dump();
    } else if (path == "/api/badge/provision/verify") {
        resp_body = json({{"status", "ok"}, {"data", json::object()}}).dump();
    } else if (path == "/api/badge/provision/wireless-psk") {
        resp_body = json({{"status", "ok"}, {"data", {{"psk", "museum-fake-psk"}}}}).dump();
    } else if (path == "/api/badge/register") {
        std::string pname = req_json.value("player_name", "");
        if (pname.empty()) {
            status = 400;
            resp_body = json({{"status", "error"}, {"detail", "Player name is required"}}).dump();
        } else {
            g_srv.player_name = pname;
            g_srv.registered = true;
            resp_body = json({{"status", "ok"}, {"data", build_status_data()}}).dump();
        }
    } else if (path == "/api/badge/status") {
        if (!g_srv.registered || g_srv.player_name.empty()) {
            status = 404;
            resp_body = json({{"status", "error"}, {"detail", "Badge not registered"}}).dump();
        } else {
            resp_body = json({{"status", "ok"}, {"data", build_status_data()}}).dump();
        }
    } else if (path == "/api/badge/redeem_code") {
        std::string code = req_json.value("code", "");
        std::transform(code.begin(), code.end(), code.begin(), ::toupper);
        if (code.size() != 5) {
            status = 400;
            resp_body = json({{"status", "error"}, {"detail", "Invalid 5-character code"}}).dump();
        } else if (g_srv.redeemed_codes.count(code)) {
            status = 400;
            resp_body = json({{"status", "error"}, {"detail", "Code already redeemed"}}).dump();
        } else {
            g_srv.redeemed_codes.insert(code);
            std::string ntype = "MISC";
            auto it = g_srv.issued_nuts.find(code);
            if (it != g_srv.issued_nuts.end()) {
                ntype = it->second.type;
                it->second.redeemed = true;
                if (it->second.faction_override >= 1 && it->second.faction_override <= 6) {
                    g_srv.team_id = it->second.faction_override;
                }
            }
            int awarded = 50;
            if (ntype == "SORTING_HAT") {
                g_srv.sorting_hat = true;
                awarded = 100;
            } else if (ntype == "TELECOM") {
                g_srv.telecom_unlocks++;
                awarded = 75;
            } else if (ntype == "COMMUNITY") {
                g_srv.level++;
                awarded = 100;
            } else if (ntype == "CONTEST") {
                awarded = 150;
            }
            g_srv.credits += awarded;
            if (g_srv.credits >= g_srv.level * 250) {
                g_srv.level = std::max(g_srv.level, (g_srv.credits / 250) + 1);
            }
            resp_body = json({
                {"status", "ok"},
                {"data", {{"credits_awarded", awarded}, {"nut_type", ntype}}}
            }).dump();
        }
    } else if (path == "/topology/data") {
        resp_body = build_topology_response().dump();
    } else if (path == "/api/badge/files/list") {
        resp_body = json({{"status", "ok"}, {"data", {{"files", json::array()}}}}).dump();
    } else if (path == "/api/badge/messages") {
        json msgs = json::array();
        msgs.push_back({
            {"id", 1},
            {"title", "Welcome to SAINTCON 2025"},
            {"content", "Tap the Sorting Hat Nut to reveal your faction, then unlock Telecom Nodes!"},
            {"priority", 1},
            {"created_at", "2025-10-21T09:00:00Z"}
        });
        resp_body = json({
            {"status", "ok"},
            {"data", {
                {"badge_id", "025C25000001"},
                {"timestamp", "2025-10-21T12:00:00Z"},
                {"messages", msgs}
            }}
        }).dump();
    } else if (path == "/api/badge/messages/read") {
        int mid = req_json.value("message_id", 1);
        g_srv.read_messages.insert(mid);
        resp_body = json({
            {"status", "ok"},
            {"data", {
                {"badge_id", "025C25000001"},
                {"message_id", mid},
                {"success", true},
                {"timestamp", "2025-10-21T12:00:00Z"}
            }}
        }).dump();
    } else if (path == "/api/badge/konami") {
        bool first = !g_srv.konami_done;
        g_srv.konami_done = true;
        resp_body = json({{"status", "ok"}, {"data", {{"first_time", first}}}}).dump();
    } else if (path == "/api/badge/dawn-accord") {
        g_srv.dawn_accord_reported = true;
        resp_body = json({{"status", "ok"}, {"data", json::object()}}).dump();
    } else if (path == "/api/badge/achievements") {
        json achs = json::array({
            {
                {"id", 1},
                {"short_name", "Registered"},
                {"description", "Register your badge handle"},
                {"is_public", true},
                {"status", g_srv.registered ? "COMPLETED" : "NOT_STARTED"},
                {"completed_at", g_srv.registered ? json("2025-10-21T09:05:00Z") : json(nullptr)}
            },
            {
                {"id", 2},
                {"short_name", "Sorted"},
                {"description", "Consult the Sorting Hat Nut"},
                {"is_public", true},
                {"status", g_srv.sorting_hat ? "COMPLETED" : "NOT_STARTED"},
                {"completed_at", g_srv.sorting_hat ? json("2025-10-21T09:10:00Z") : json(nullptr)}
            },
            {
                {"id", 3},
                {"short_name", "Telecom Operator"},
                {"description", "Unlock a Telecom Node"},
                {"is_public", true},
                {"status", g_srv.telecom_unlocks > 0 ? "COMPLETED" : "IN_PROGRESS"},
                {"completed_at", g_srv.telecom_unlocks > 0 ? json("2025-10-21T10:00:00Z") : json(nullptr)}
            }
        });
        resp_body = json({{"status", "ok"}, {"data", achs}}).dump();
    } else if (path == "/api/badge/ctf-hints-password") {
        resp_body = json({{"status", "ok"}, {"data", {{"password", "MUSEUM-DEMO-HINTS"}}}}).dump();
    } else if (path == "/api/badge/request_credit_swap") {
        int amt = req_json.value("credit_amount", 10);
        if (amt <= 0 || amt > g_srv.credits) {
            status = 400;
            resp_body = json({{"status", "error"}, {"detail", "Insufficient credits"}}).dump();
        } else {
            static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            std::string scode;
            for (int i = 0; i < 5; i++) {
                scode.push_back(kAlphabet[(platform_rand_u32() >> 3) % 32]);
            }
            g_srv.swaps[scode] = SwapEntry{scode, amt, 0, false, false};
            resp_body = json({
                {"status", "ok"},
                {"data", {
                    {"code", scode},
                    {"credits", amt},
                    {"expires_at", "2025-10-24T23:59:59Z"},
                    {"status", "pending"}
                }}
            }).dump();
        }
    } else if (path.rfind("/api/badge/credit_swap_status/", 0) == 0) {
        std::string scode = path.substr(std::strlen("/api/badge/credit_swap_status/"));
        auto it = g_srv.swaps.find(scode);
        if (it == g_srv.swaps.end()) {
            status = 404;
            resp_body = json({{"status", "error"}, {"detail", "Swap code not found"}}).dump();
        } else {
            it->second.polls++;
            if (!it->second.cancelled && !it->second.redeemed && it->second.polls >= 2) {
                it->second.redeemed = true;
                g_srv.credits = std::max(0, g_srv.credits - it->second.credits);
            }
            std::string st = it->second.cancelled ? "cancelled" : (it->second.redeemed ? "redeemed" : "pending");
            resp_body = json({
                {"status", "ok"},
                {"data", {
                    {"code", scode},
                    {"credits", it->second.credits},
                    {"status", st},
                    {"redeemed", it->second.redeemed},
                    {"redeemed_at", it->second.redeemed ? "2025-10-21T12:00:00Z" : ""},
                    {"redeemed_by", it->second.redeemed ? "Museum Visitor" : ""},
                    {"expires_at", "2025-10-24T23:59:59Z"},
                    {"issued_at", "2025-10-21T11:55:00Z"}
                }}
            }).dump();
        }
    } else if (path.rfind("/api/badge/cancel_credit_swap/", 0) == 0) {
        std::string scode = path.substr(std::strlen("/api/badge/cancel_credit_swap/"));
        auto it = g_srv.swaps.find(scode);
        if (it != g_srv.swaps.end()) {
            it->second.cancelled = true;
        }
        resp_body = json({{"status", "ok"}, {"data", json::object()}}).dump();
    } else {
        status = 404;
        resp_body = json({{"status", "error"}, {"detail", "Unknown endpoint: " + path}}).dump();
    }

    if (hdrs.empty()) {
        hdrs = "Content-Type: application/json\nContent-Length: " + std::to_string(resp_body.size()) + "\n";
    }

    if (out_headers_lines) *out_headers_lines = strdup(hdrs.c_str());
    if (out_body) {
        *out_body = (char *)malloc(resp_body.size() + 1);
        if (*out_body) {
            std::memcpy(*out_body, resp_body.data(), resp_body.size());
            (*out_body)[resp_body.size()] = '\0';
        }
    }
    if (out_body_len) *out_body_len = resp_body.size();
    return status;
}
