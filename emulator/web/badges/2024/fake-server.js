/**
 * SAINTCON 2024 Wrist Badge ("Tower Battle") — Per-Visitor In-Browser Fake Tower Server
 *
 * Called synchronously from `emulator/cores/2024/platform/http_bridge.c`
 * (`esp_http_client_perform`) via `window.__sc24FakeServer.handleRequest(...)`.
 * All visitor state lives in `localStorage['saintcon_2024_stock_server']`
 * (and firmware NVS lives in `localStorage['saintcon_2024_stock_nvs']`).
 * When `?mock=1` is present in the page URL, `localStorage` is left completely untouched.
 *
 * ============================================================================
 * FAITHFUL TO UPSTREAM FIRMWARE (`compukidmike/Saintcon2024`, `Firmware/`)
 * ============================================================================
 * - Endpoint URLs, HTTP methods, and JSON envelopes (`{"status": bool, "detail": ..., "result": ...}`)
 *   match `components/api/api.cpp`, `components/api/api_client.cpp`, and `components/api/include/types.h`:
 *     * `GET /badge`, `POST /badge/register` (`{"handle": ...}`)
 *     * `GET /badge/firmware_version` (reports `"2.6.9"`, matching `Firmware/version.txt`), `GET /badge/firmware`
 *     * `GET /badge/all_tower_status`, `GET /badge/tower_status?tower_id=<id>` & `?ir_code=<code>`
 *     * `POST /badge/join_tower` (`{"tower_ir_code": u32}`), `POST /badge/leave_tower`, `POST /badge/join_battle`
 *     * `POST /badge/ir_code` (`{"codes": [u32, ...]}`)
 *     * `POST /badge/equip` (`{"slot1": ..., "slot2": ...}`)
 *     * `POST /badge/levelup` (`{"level": int}` -> `result.code`)
 *     * `POST /auth/request_auth_code`, `POST /auth/request_levelup_code`, `POST /auth/auth_status`
 *     * `GET /vend/items`, `POST /vend/buy` (`{"item_id": int}`)
 *     * `POST /battle/attack` (`{"battle_id", "length", "amount", "time"}` -> `result.percent`)
 *     * `POST /battle/fail`, `GET /battle/status/<id>`, `POST /battle/self_save`
 *       (including upstream's exact `"strategem_min"`, `"strategem_max"`, `"strategem_amount"` spelling
 *       and array `"savior_handle"`)
 *     * `GET /battle/savior` (`result.ir_code`), `POST /battle/aar`
 * - Exact tower count (`TOTAL_TOWERS = 11` in `components/badge/towers.h`), tower status enum strings
 *   (`"VULNERABLE"`, `"INVULNERABLE"`, `"OFFLINE"`, `"DEFEATED"`), player battle status strings
 *   (`"JOINED"`, `"BATTLE"`, `"DISCONNECTED"`), IR code types (`"TOWER"`, `"LEVELUP"`, `"SAVIOR"`,
 *   `"PVP"`, `"VENDING"`, `"AUTH"`), and minibadge buff types (`"QUAD_DAMAGE"`, `"SPELLCHECK"`,
 *   `"STRAT_SIMPLIFY"`, `"REVERSE_SHELL"`, `"SAVING_THROW"`, `"HYDRA"`, etc.).
 * - NEC 32-bit IR framing and XOR obfuscation match `components/badge/ir.h` (`IR_KEY = 0xDEADBEEF`,
 *   message type nibble in bits 31..28 of `code ^ 0xDEADBEEF`: `0x0` = Tower, `0x1` = Level-Up,
 *   `0x2` = Savior, `0x4` = Vending, `0x5` = Auth; `address = code >>> 16`, `command = code & 0xFFFF`).
 * - Conference floor names (`Ground Level`, `Ballroom Level`, `Level 3`) and room labels match the
 *   firmware's SPIFFS map screens (`components/ui/pages/map.c` and `spiffs_source/LVL{1,2,3}.png`).
 *
 * ============================================================================
 * INVENTED FOR SOLO MUSEUM PLAY (ORIGINAL `sc24.redactd.net` SERVER IS GONE)
 * ============================================================================
 * - All 11 tower names, levels (`1..5`), HP pools (`240..900`, tuned for solo play in 2-5 attacks),
 *   and deterministic tower IR payloads (`0x00240101..0x0024010B ^ 0xDEADBEEF`) are reconstructed
 *   from the 11 labeled rooms on the firmware's own floor maps (`Communities`, `Ground Contests`,
 *   `Ballroom A/B/C`, `EduSec`, `Chill Lounge`, `ICS`, `Briefs`, `HAM`, `Social`).
 * - Virtual "Towers" panel on the emulator page stands in for walking up to the 11 physical IR tower
 *   beacons (plus Community Level-Up and Savior Revive IR beacons). Clicking a defeated tower in the
 *   Towers panel respawns it at full health so solo visitors can replay any station.
 * - Note on upstream `render_battle_result()` (`tower_battle.c:1482`): in firmware v2.6.9,
 *   `render_battle_result()` is an empty `TODO` stub if `battle_active` becomes `false` mid-modal.
 *   To avoid stranding players on an empty `"In battle..."` screen, `battle_active` stays `true`
 *   while the player's session is open; when an attack reduces a tower's HP to `0`, the tower status
 *   immediately becomes `"DEFEATED"` (`health: 0`) with bonus XP/coins awarded, and leaving returns
 *   cleanly to the Tower Battle list showing the defeated tower.
 * - Shop power-ups (`GET /vend/items`, `POST /vend/buy`) automatically equip purchased buffs into the
 *   player's virtual `slot1` / `slot2` (`POST /badge/equip`) so solo players without physical I2C
 *   Klip-on minibadges can earn coins and use `QUAD_DAMAGE`, `SPELLCHECK`, `STRAT_SIMPLIFY`,
 *   `REVERSE_SHELL`, `SAVING_THROW`, and `HYDRA` in battle.
 */
(function () {
  'use strict';

  const STORAGE_KEY = 'saintcon_2024_stock_server';
  const NVS_STORAGE_KEY = 'saintcon_2024_stock_nvs';
  const IR_KEY = 0xDEADBEEF >>> 0;

  function isMockMode() {
    try {
      if (typeof window !== 'undefined' && window.location) {
        return new URLSearchParams(window.location.search).get('mock') === '1';
      }
    } catch (_) {}
    return false;
  }

  function encodeIr(mti, payload28) {
    const decoded = (((mti & 0xF) << 28) | (payload28 & 0x0FFFFFFF)) >>> 0;
    return (decoded ^ IR_KEY) >>> 0;
  }

  function decodeIr(rawCode) {
    const code = (Number(rawCode) >>> 0);
    const decoded = (code ^ IR_KEY) >>> 0;
    const mti = (decoded >>> 28) & 0xF;
    const payload = decoded & 0x0FFFFFFF;
    return { code, decoded, mti, payload };
  }

  // 11 towers matching TOTAL_TOWERS = 11 in components/badge/towers.h and the 11 labeled rooms
  // across spiffs_source/LVL1.png (Ground Level), LVL2.png (Ballroom Level), and LVL3.png (Level 3).
  const TOWER_DEFS = [
    { id: 1,  name: 'Communities',     location: 'Ground Level',   level: 1, max_health: 240, payload: 0x00240101 },
    { id: 2,  name: 'Ground Contests', location: 'Ground Level',   level: 1, max_health: 280, payload: 0x00240102 },
    { id: 3,  name: 'Ballroom A',      location: 'Ballroom Level', level: 2, max_health: 360, payload: 0x00240103 },
    { id: 4,  name: 'Ballroom B',      location: 'Ballroom Level', level: 2, max_health: 400, payload: 0x00240104 },
    { id: 5,  name: 'Ballroom C',      location: 'Ballroom Level', level: 3, max_health: 480, payload: 0x00240105 },
    { id: 6,  name: 'EduSec',          location: 'Level 3',        level: 2, max_health: 360, payload: 0x00240106 },
    { id: 7,  name: 'Chill Lounge',    location: 'Level 3',        level: 3, max_health: 520, payload: 0x00240107 },
    { id: 8,  name: 'ICS Village',     location: 'Level 3',        level: 4, max_health: 640, payload: 0x00240108 },
    { id: 9,  name: 'Briefs',          location: 'Level 3',        level: 4, max_health: 720, payload: 0x00240109 },
    { id: 10, name: 'HAM Radio',       location: 'Level 3',        level: 3, max_health: 500, payload: 0x0024010A },
    { id: 11, name: 'Social Eng',      location: 'Level 3',        level: 5, max_health: 900, payload: 0x0024010B },
  ];

  const SHOP_DEFS = [
    {
      item_id: 1,
      item_name: 'Spellchecker',
      shortname: 'SPEL',
      buff_type: 'SPELLCHECK',
      buff_value: 1.0,
      item_price: 50,
      available_stock: 99,
      version: 1,
    },
    {
      item_id: 2,
      item_name: 'Strat Simplify',
      shortname: 'SIMP',
      buff_type: 'STRAT_SIMPLIFY',
      buff_value: 1.0,
      item_price: 60,
      available_stock: 99,
      version: 1,
    },
    {
      item_id: 3,
      item_name: 'Quad Damage',
      shortname: 'QUAD',
      buff_type: 'QUAD_DAMAGE',
      buff_value: 4.0,
      item_price: 75,
      available_stock: 99,
      version: 1,
    },
    {
      item_id: 4,
      item_name: 'Saving Throw',
      shortname: 'SAVE',
      buff_type: 'SAVING_THROW',
      buff_value: 1.0,
      item_price: 80,
      available_stock: 99,
      version: 1,
    },
    {
      item_id: 5,
      item_name: 'Reverse Shell',
      shortname: 'RSHL',
      buff_type: 'REVERSE_SHELL',
      buff_value: 1.0,
      item_price: 90,
      available_stock: 99,
      version: 1,
    },
    {
      item_id: 6,
      item_name: 'Hydra Revival',
      shortname: 'HYDR',
      buff_type: 'HYDRA',
      buff_value: 1.0,
      item_price: 100,
      available_stock: 99,
      version: 1,
    },
  ];

  function createDefaultState() {
    const bootIso = '2024-10-22T09:00:00Z';
    return {
      schema: 1,
      player: {
        id: 2024,
        badge_id: '24:5C:20:24:00:01',
        handle: '',
        registered: false,
        xp: 0,
        level: 1,
        coins: 120,
        towers_defeated: 0,
        can_level: true,
        community: 'SAINTCON Museum',
        community_levels: 3,
      },
      equipped: [null, null], // item_ids equipped into slot1, slot2
      purchasedIds: [],
      joinedTowerId: 0,
      nextBattleId: 101,
      beaconSeq: 1,
      activeTowerId: 0,
      battle: null,
      towers: TOWER_DEFS.map((t) => ({
        id: t.id,
        name: t.name,
        location: t.location,
        level: t.level,
        health: t.max_health,
        max_health: t.max_health,
        boot_time: bootIso,
        ir_code: encodeIr(0x0, t.payload),
        enabled: true,
        status: 'VULNERABLE',
        players_in_range: 0,
        players_in_battle: 0,
        players_joined_tower: 0,
        players_disconnected: 0,
      })),
    };
  }

  let state = loadState();
  const listeners = new Set();

  function loadState() {
    const fresh = createDefaultState();
    if (isMockMode()) return fresh;
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return fresh;
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== 'object' || !Array.isArray(parsed.towers) || parsed.towers.length !== TOWER_DEFS.length) {
        return fresh;
      }
      fresh.player = Object.assign(fresh.player, parsed.player || {});
      if (Array.isArray(parsed.equipped) && parsed.equipped.length === 2) {
        fresh.equipped = [parsed.equipped[0] || null, parsed.equipped[1] || null];
      }
      if (Array.isArray(parsed.purchasedIds)) {
        fresh.purchasedIds = parsed.purchasedIds.filter((n) => Number.isInteger(n));
      }
      fresh.joinedTowerId = Number.isInteger(parsed.joinedTowerId) ? parsed.joinedTowerId : 0;
      fresh.nextBattleId = Number.isInteger(parsed.nextBattleId) && parsed.nextBattleId > 0 ? parsed.nextBattleId : 101;
      fresh.beaconSeq = Number.isInteger(parsed.beaconSeq) && parsed.beaconSeq > 0 ? parsed.beaconSeq : 1;
      fresh.activeTowerId = Number.isInteger(parsed.activeTowerId) ? parsed.activeTowerId : 0;
      fresh.battle = parsed.battle && typeof parsed.battle === 'object' ? parsed.battle : null;
      fresh.towers = TOWER_DEFS.map((def, idx) => {
        const saved = parsed.towers[idx] || {};
        const maxHp = def.max_health;
        const hp = typeof saved.health === 'number' ? Math.max(0, Math.min(maxHp, Math.round(saved.health))) : maxHp;
        return {
          id: def.id,
          name: def.name,
          location: def.location,
          level: def.level,
          health: hp,
          max_health: maxHp,
          boot_time: saved.boot_time || '2024-10-22T09:00:00Z',
          ir_code: encodeIr(0x0, def.payload),
          enabled: true,
          status: hp <= 0 ? 'DEFEATED' : (saved.status || 'VULNERABLE'),
          players_in_range: saved.players_in_range || 0,
          players_in_battle: saved.players_in_battle || 0,
          players_joined_tower: saved.players_joined_tower || 0,
          players_disconnected: saved.players_disconnected || 0,
        };
      });
      return fresh;
    } catch (_) {
      return fresh;
    }
  }

  function saveState() {
    if (!isMockMode()) {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
      } catch (_) {}
    }
    notifyListeners();
  }

  function notifyListeners() {
    for (const fn of listeners) {
      try { fn(getStateSummary()); } catch (_) {}
    }
  }

  function hasBuff(buffType) {
    for (const itemId of state.equipped) {
      if (!itemId) continue;
      const def = SHOP_DEFS.find((d) => d.item_id === itemId);
      if (def && def.buff_type === buffType) return true;
    }
    return false;
  }

  function buildSlotObj(itemId) {
    if (!itemId) return null;
    const def = SHOP_DEFS.find((d) => d.item_id === itemId);
    if (!def) return null;
    return {
      name: def.item_name,
      shortname: def.shortname,
      buff_type: def.buff_type,
      buff_value: def.buff_value,
      valid: true,
      rewards: 'Museum Shop Power-Up',
    };
  }

  function findTowerById(id) {
    return state.towers.find((t) => t.id === Number(id)) || null;
  }

  function findTowerByIr(irCode) {
    const u = (Number(irCode) >>> 0);
    let tower = state.towers.find((t) => (t.ir_code >>> 0) === u);
    if (!tower) {
      const dec = decodeIr(u);
      if (dec.mti === 0x0) {
        const low = dec.payload & 0xFF;
        tower = findTowerById(low);
      }
    }
    return tower || null;
  }

  function stratParamsForLevel(level) {
    const simplify = hasBuff('STRAT_SIMPLIFY') ? 1 : 0;
    const table = {
      1: { min: 3, max: 4, amount: 2 },
      2: { min: 4, max: 5, amount: 2 },
      3: { min: 4, max: 5, amount: 3 },
      4: { min: 5, max: 6, amount: 3 },
      5: { min: 5, max: 7, amount: 4 },
    };
    const row = table[level] || table[2];
    const min = Math.max(2, row.min - simplify);
    const max = Math.max(min, row.max - simplify);
    const amount = Math.max(1, row.amount - (simplify && row.amount > 2 ? 1 : 0));
    return { min, max, amount };
  }

  function buildBattleStatusResult(battle) {
    const tower = (battle && findTowerById(battle.tower_id)) || findTowerById(state.joinedTowerId) || state.towers[0];
    const strat = stratParamsForLevel(tower.level);
    const disconnected = battle ? battle.player_status === 'DISCONNECTED' : false;
    const isSavior = battle ? Boolean(battle.is_savior) : false;
    return {
      battle_active: true,
      player_level: battle ? battle.player_conn : 3,
      player_hp: battle ? battle.player_max_conn : 3,
      player_status: battle ? battle.player_status : 'JOINED',
      is_savior: isSavior,
      tower_ir_code: tower.ir_code >>> 0,
      tower_level: tower.level,
      tower_health: tower.health,
      tower_max_health: tower.max_health,
      players_in_range: Math.max(1, tower.players_in_range),
      players_joined_tower: Math.max(1, tower.players_joined_tower),
      players_in_battle: disconnected ? 0 : 1,
      strategem_min: strat.min,
      strategem_max: strat.max,
      strategem_amount: strat.amount,
      players_disconnected: disconnected ? 1 : 0,
      savior_handle: (disconnected && !isSavior) ? ['MuseumSavior'] : [],
    };
  }

  function awardXp(amount) {
    state.player.xp += amount;
    const computedLevel = 1 + Math.floor(state.player.xp / 250);
    if (computedLevel > state.player.level) {
      state.player.level = Math.min(99, computedLevel);
    }
  }

  function okResponse(resultObj, detail) {
    return {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        status: true,
        detail: detail === undefined ? null : detail,
        result: resultObj === undefined ? {} : resultObj,
      }),
    };
  }

  function errResponse(httpStatus, detail) {
    return {
      status: httpStatus,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        status: false,
        detail: detail || 'Request failed',
        result: null,
      }),
    };
  }

  function parseUrl(rawUrl) {
    try {
      const u = new URL(rawUrl, 'https://sc24.redactd.net');
      return { pathname: u.pathname, searchParams: u.searchParams };
    } catch (_) {
      const qIdx = rawUrl.indexOf('?');
      const pathPart = qIdx >= 0 ? rawUrl.slice(0, qIdx) : rawUrl;
      const queryPart = qIdx >= 0 ? rawUrl.slice(qIdx + 1) : '';
      const pathname = pathPart.replace(/^https?:\/\/[^/]+/, '') || '/';
      return { pathname, searchParams: new URLSearchParams(queryPart) };
    }
  }

  function parseJsonSafe(bodyStr) {
    if (!bodyStr || typeof bodyStr !== 'string') return {};
    try {
      return JSON.parse(bodyStr) || {};
    } catch (_) {
      return {};
    }
  }

  function nextDynamicIrCode(mti) {
    state.beaconSeq = ((state.beaconSeq || 1) + 1) & 0xFFFF;
    const payload = (0x00240000 | state.beaconSeq) >>> 0;
    const code = encodeIr(mti, payload);
    saveState();
    return code;
  }

  function handleRequest(method, url, _headersStr, bodyStr) {
    const m = String(method || 'GET').toUpperCase();
    const { pathname, searchParams } = parseUrl(String(url || '/'));
    const payload = m === 'POST' ? parseJsonSafe(bodyStr) : {};

    // --- Auth endpoints ---
    if (m === 'POST' && pathname === '/auth/request_auth_code') {
      return okResponse({ code: nextDynamicIrCode(0x5) });
    }
    if (m === 'POST' && pathname === '/auth/request_levelup_code') {
      return okResponse({ code: nextDynamicIrCode(0x1) });
    }
    if (m === 'POST' && pathname === '/auth/auth_status') {
      const code = (Number(payload.ir_code) >>> 0);
      return okResponse({
        code,
        claimed: true,
        request_time: '2024-10-24T12:00:00Z',
        claimed_by: state.player.handle || 'MuseumVisitor',
        claim_time: '2024-10-24T12:00:02Z',
      });
    }

    // --- Badge endpoints ---
    if (m === 'GET' && pathname === '/badge') {
      const inSavior = Boolean(state.battle && state.battle.is_savior);
      return okResponse({
        id: state.player.id,
        badge_id: state.player.badge_id,
        handle: state.player.handle ? state.player.handle : null,
        xp: state.player.xp,
        level: state.player.level,
        enabled: true,
        badge_team: true,
        staff: false,
        blackbadge: false,
        can_level: Boolean(state.player.can_level),
        community: state.player.community || 'SAINTCON Museum',
        community_levels: state.player.community_levels || 3,
        is_savior: inSavior,
        coins: state.player.coins,
      });
    }

    if (m === 'POST' && pathname === '/badge/register') {
      const handle = String(payload.handle || '').trim().slice(0, 31);
      if (!handle) {
        return errResponse(400, 'Handle cannot be empty');
      }
      state.player.handle = handle;
      state.player.registered = true;
      saveState();
      return okResponse({});
    }

    if (m === 'GET' && pathname === '/badge/firmware_version') {
      return okResponse({
        setting: 'firmware_version',
        value: '2.6.9',
      });
    }

    if (m === 'GET' && pathname === '/badge/firmware') {
      return errResponse(404, 'Firmware is already up to date');
    }

    if (m === 'GET' && pathname === '/badge/all_tower_status') {
      return okResponse(state.towers);
    }

    if (m === 'GET' && pathname === '/badge/tower_status') {
      let tower = null;
      if (searchParams.has('tower_id')) {
        tower = findTowerById(Number(searchParams.get('tower_id')));
      } else if (searchParams.has('ir_code')) {
        tower = findTowerByIr(Number(searchParams.get('ir_code')));
      }
      if (!tower) return errResponse(404, 'Tower not found');
      return okResponse(tower);
    }

    if (m === 'POST' && pathname === '/badge/join_tower') {
      const tower = findTowerByIr(payload.tower_ir_code);
      if (!tower) return errResponse(404, 'Tower not found');
      for (const t of state.towers) {
        if (t.id !== tower.id) {
          t.players_joined_tower = 0;
          t.players_in_battle = 0;
          t.players_disconnected = 0;
        }
      }
      state.joinedTowerId = tower.id;
      state.activeTowerId = tower.id;
      tower.players_in_range = 1;
      tower.players_joined_tower = 1;
      saveState();
      return okResponse({});
    }

    if (m === 'POST' && pathname === '/badge/leave_tower') {
      for (const t of state.towers) {
        t.players_joined_tower = 0;
        t.players_in_battle = 0;
        t.players_disconnected = 0;
      }
      state.joinedTowerId = 0;
      state.battle = null;
      saveState();
      return okResponse({});
    }

    if (m === 'POST' && pathname === '/badge/join_battle') {
      const tower = findTowerById(state.joinedTowerId) || state.towers[0];
      state.joinedTowerId = tower.id;
      if (tower.health <= 0) {
        tower.health = tower.max_health;
        tower.status = 'VULNERABLE';
      }
      tower.players_in_range = 1;
      tower.players_joined_tower = 1;
      tower.players_in_battle = 1;
      tower.players_disconnected = 0;

      if (state.battle && state.battle.tower_id === tower.id) {
        state.battle.player_conn = state.battle.player_max_conn;
        state.battle.player_status = 'BATTLE';
        state.battle.is_savior = false;
      } else {
        const battleId = state.nextBattleId++;
        state.battle = {
          battle_id: battleId,
          tower_id: tower.id,
          player_conn: 3,
          player_max_conn: 3,
          player_status: 'BATTLE',
          is_savior: false,
          total_attacks: 0,
          total_damage: 0,
          xp_earned: 0,
          coins_earned: 0,
          failures: 0,
          perf_sum: 0,
        };
      }
      saveState();
      return okResponse({ battle_id: state.battle.battle_id });
    }

    if (m === 'POST' && pathname === '/badge/ir_code') {
      const codes = Array.isArray(payload.codes) ? payload.codes : [];
      const results = codes.map((raw) => {
        const code = (Number(raw) >>> 0);
        const dec = decodeIr(code);
        if (dec.mti === 0x0) {
          const tower = findTowerByIr(code);
          if (tower) {
            tower.players_in_range = 1;
            state.activeTowerId = tower.id;
            return {
              code,
              is_valid: true,
              type: 'TOWER',
              name: tower.name,
              response: 'Tower beacon detected',
              tower_id: tower.id,
            };
          }
        } else if (dec.mti === 0x1) {
          state.player.level = Math.min(99, state.player.level + 1);
          state.player.xp += 100;
          state.player.coins += 50;
          return {
            code,
            is_valid: true,
            type: 'LEVELUP',
            name: 'Community Level Up',
            response: 'Level up received!',
            tower_id: 0,
          };
        } else if (dec.mti === 0x2) {
          if (state.battle) {
            state.battle.player_conn = state.battle.player_max_conn;
            state.battle.player_status = 'JOINED';
            state.battle.is_savior = false;
            const t = findTowerById(state.battle.tower_id);
            if (t) {
              t.players_disconnected = 0;
              t.players_joined_tower = 1;
            }
          }
          return {
            code,
            is_valid: true,
            type: 'SAVIOR',
            name: 'Savior Beacon',
            response: 'Revived by Museum Savior! Ready to rejoin the battle.',
            tower_id: 0,
          };
        } else if (dec.mti === 0x4 || dec.mti === 0x5) {
          return {
            code,
            is_valid: true,
            type: dec.mti === 0x4 ? 'VENDING' : 'AUTH',
            name: 'Museum Kiosk',
            response: 'Authenticated',
            tower_id: 0,
          };
        }
        return {
          code,
          is_valid: false,
          type: 'UNKNOWN',
          name: null,
          response: null,
          tower_id: 0,
        };
      });
      saveState();
      return okResponse(results);
    }

    if (m === 'POST' && pathname === '/badge/equip') {
      return okResponse({
        slot1: buildSlotObj(state.equipped[0]),
        slot2: buildSlotObj(state.equipped[1]),
      });
    }

    if (m === 'POST' && pathname === '/badge/levelup') {
      return okResponse({ code: nextDynamicIrCode(0x1) });
    }

    // --- Vending / Shop endpoints ---
    if (m === 'GET' && pathname === '/vend/items') {
      const items = SHOP_DEFS.map((def) => {
        const purchased = state.purchasedIds.includes(def.item_id);
        return {
          item_id: def.item_id,
          item_name: def.item_name,
          item_price: def.item_price,
          available_stock: def.available_stock,
          purchased,
          sold_out: false,
          image_url: '',
          version: def.version,
        };
      });
      return okResponse(items);
    }

    if (m === 'POST' && pathname === '/vend/buy') {
      const itemId = Number(payload.item_id);
      const def = SHOP_DEFS.find((d) => d.item_id === itemId);
      if (!def) return errResponse(404, 'Item not found');
      const alreadyOwned = state.purchasedIds.includes(itemId);
      if (!alreadyOwned) {
        if (state.player.coins < def.item_price) {
          return errResponse(400, 'Not enough coins');
        }
        state.player.coins -= def.item_price;
        state.purchasedIds.push(itemId);
      }
      // Equip into slot1/slot2 so the buff takes effect immediately in Tower Battle
      if (state.equipped[0] !== itemId && state.equipped[1] !== itemId) {
        if (!state.equipped[0]) {
          state.equipped[0] = itemId;
        } else if (!state.equipped[1]) {
          state.equipped[1] = itemId;
        } else {
          state.equipped[0] = state.equipped[1];
          state.equipped[1] = itemId;
        }
      }
      saveState();
      return okResponse({});
    }

    // --- Battle endpoints ---
    if (m === 'POST' && pathname === '/battle/attack') {
      const battle = state.battle;
      const tower = (battle && findTowerById(battle.tower_id)) || findTowerById(state.joinedTowerId) || state.towers[0];
      // If attacking a tower that already hit 0 HP in a prior round, start a fresh wave on it
      if (tower.health <= 0) {
        tower.health = tower.max_health;
        tower.status = 'VULNERABLE';
      }

      const len = Math.max(1, Number(payload.length) || 4);
      const amt = Math.max(1, Number(payload.amount) || 2);
      const secs = Math.max(0.25, Number(payload.time) || 3.0);
      const arrows = len * amt;

      let mult = 1;
      if (hasBuff('QUAD_DAMAGE')) mult *= 4;
      if (hasBuff('GLASS_CANNON')) mult *= 2;
      const levelBonus = 1 + Math.min(1.5, (state.player.level - 1) * 0.1);
      const damage = Math.max(40, Math.round(arrows * 22 * levelBonus * mult));

      const prevHp = tower.health;
      tower.health = Math.max(0, tower.health - damage);

      const targetSecs = arrows * 0.65;
      const ratio = targetSecs / secs;
      const percent = Math.max(55, Math.min(99, Math.round(72 + Math.min(1.5, ratio) * 18)));

      const lootMult = (hasBuff('LOOT') || hasBuff('GREED')) ? 2 : 1;
      let xpGain = 30;
      let coinGain = 15 * lootMult;

      if (prevHp > 0 && tower.health === 0) {
        tower.status = 'DEFEATED';
        state.player.towers_defeated = (state.player.towers_defeated || 0) + 1;
        xpGain += 100 * tower.level;
        coinGain += 45 * tower.level * lootMult;
      }

      awardXp(xpGain);
      state.player.coins += coinGain;

      if (battle) {
        battle.total_attacks += 1;
        battle.total_damage += damage;
        battle.xp_earned += xpGain;
        battle.coins_earned += coinGain;
        battle.perf_sum += percent;
      }
      saveState();
      return okResponse({ percent });
    }

    if (m === 'POST' && pathname === '/battle/fail') {
      const battle = state.battle;
      if (battle) {
        battle.failures += 1;
        const drop = hasBuff('GLASS_CANNON') ? battle.player_max_conn : 1;
        battle.player_conn = Math.max(0, battle.player_conn - drop);
        if (battle.player_conn <= 0) {
          battle.player_status = 'DISCONNECTED';
          battle.is_savior = hasBuff('HYDRA');
          const t = findTowerById(battle.tower_id);
          if (t) {
            t.players_in_battle = 0;
            t.players_disconnected = 1;
          }
        }
      }
      saveState();
      return okResponse(buildBattleStatusResult(battle));
    }

    if (m === 'GET' && pathname.startsWith('/battle/status/')) {
      return okResponse(buildBattleStatusResult(state.battle));
    }

    if (m === 'GET' && pathname === '/battle/savior') {
      return okResponse({ ir_code: nextDynamicIrCode(0x2) });
    }

    if (m === 'POST' && pathname === '/battle/self_save') {
      if (state.battle) {
        state.battle.player_conn = state.battle.player_max_conn;
        state.battle.player_status = 'BATTLE';
        state.battle.is_savior = false;
        const t = findTowerById(state.battle.tower_id);
        if (t) {
          t.players_disconnected = 0;
          t.players_in_battle = 1;
        }
      }
      saveState();
      return okResponse(buildBattleStatusResult(state.battle));
    }

    if (m === 'POST' && pathname === '/battle/aar') {
      const b = state.battle;
      const attacks = b ? b.total_attacks : 0;
      return okResponse({
        total_attacks: attacks,
        total_damage: b ? b.total_damage : 0,
        xp_earned: b ? b.xp_earned : 0,
        avg_performance: attacks > 0 ? Math.round(b.perf_sum / attacks) : 0,
        failures: b ? b.failures : 0,
        coins_earned: b ? b.coins_earned : 0,
      });
    }

    return errResponse(404, 'Unknown endpoint: ' + m + ' ' + pathname);
  }

  // Called by the web UI when the visitor clicks a tower station in the "Towers" panel.
  // Returns the 32-bit NEC IR code to feed into `_emu_send_ir(code)`.
  // If a tower was previously defeated (0 HP) and is not currently in an active battle,
  // clicking it again respawns it so solo visitors can re-challenge any tower.
  function walkUpToTower(towerId) {
    const tower = findTowerById(towerId);
    if (!tower) return 0;
    const inActiveBattleHere = Boolean(state.battle && state.battle.tower_id === tower.id);
    if (tower.health <= 0 && !inActiveBattleHere) {
      tower.health = tower.max_health;
      tower.status = 'VULNERABLE';
    }
    tower.players_in_range = 1;
    state.activeTowerId = tower.id;
    saveState();
    return tower.ir_code >>> 0;
  }

  function respawnAllTowers() {
    for (const t of state.towers) {
      t.health = t.max_health;
      t.status = 'VULNERABLE';
    }
    saveState();
  }

  function resetServer() {
    state = createDefaultState();
    if (!isMockMode()) {
      try {
        localStorage.removeItem(STORAGE_KEY);
        localStorage.removeItem(NVS_STORAGE_KEY);
      } catch (_) {}
    }
    notifyListeners();
  }

  function getTowers() {
    return state.towers.map((t) => Object.assign({}, t));
  }

  function getStateSummary() {
    const slot1 = buildSlotObj(state.equipped[0]);
    const slot2 = buildSlotObj(state.equipped[1]);
    return {
      player: Object.assign({}, state.player),
      activeTowerId: state.activeTowerId,
      joinedTowerId: state.joinedTowerId,
      equipped: [slot1 ? slot1.item_name || slot1.name : null, slot2 ? slot2.item_name || slot2.name : null],
      battle: state.battle ? Object.assign({}, state.battle) : null,
      towers: getTowers(),
    };
  }

  function onChange(cb) {
    if (typeof cb === 'function') {
      listeners.add(cb);
      return () => listeners.delete(cb);
    }
    return () => {};
  }

  const api = {
    STORAGE_KEY,
    NVS_STORAGE_KEY,
    handleRequest,
    walkUpToTower,
    getLevelUpBeaconCode: () => nextDynamicIrCode(0x1),
    getSaviorBeaconCode: () => nextDynamicIrCode(0x2),
    respawnAllTowers,
    resetServer,
    getTowers,
    getStateSummary,
    onChange,
  };

  if (typeof window !== 'undefined') {
    window.__sc24FakeServer = api;
  }
  if (typeof globalThis !== 'undefined') {
    globalThis.__sc24FakeServer = api;
  }
})();
