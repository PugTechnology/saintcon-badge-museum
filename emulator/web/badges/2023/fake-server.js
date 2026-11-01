/**
 * SAINTCON 2023 Badge ("Incidents") — Per-Visitor In-Browser Fake Game Server
 *
 * Called synchronously from `emulator/cores/2023/platform/` (`AsyncHTTPSRequest`)
 * via `window.__sc23FakeServer.handleRequest(method, url, headersStr, bodyStr)`.
 * All visitor state lives in `localStorage['saintcon_2023_stock_server']`
 * (and firmware Wi-Fi EEPROM lives in `localStorage['saintcon_2023_stock_eeprom']`).
 * When `?mock=1` is present in the page URL, `localStorage` is left untouched.
 *
 * ============================================================================
 * FAITHFUL TO UPSTREAM FIRMWARE (`compukidmike/Saintcon2023`, `Firmware/Source/`)
 * ============================================================================
 * - Base URL (`https://sc23-api.redactd.net:8443`), `x-auth-id` MAC header, HTTP
 *   methods (`GET` when request payload is null, `POST` otherwise), and JSON
 *   envelope (`{"status": true, "response": ...}` / `{"status": false, "error": "..."}`)
 *   match `src/request.cpp`:
 *     * `GET  /badge/firmware_version` -> `response.version = 1.4` (skips OTA)
 *     * `GET  /badge/whois` -> `"unregistered"` until registered, then profile object
 *       (`character`, `email`, `eyecolor`, `haircolor`, `handle`, `class`,
 *       `completed_training`, `staff`, `level`)
 *     * `POST /badge/register` (`class`, `character`, `handle`, `email`, colors)
 *     * `POST /badge/equip` (`slot1`..`slot4` with 8-char minibadge EEPROM labels and
 *       optional 32-hex `serial`) -> `helmet`, `armor`, `weapon`, `item` (with both
 *       `"usable"` for `Battle.cpp:80` and `"useable"` for `Battle.cpp:128`)
 *     * `POST /badge/levelup` (`levelup_code`) & `POST /badge/generate_levelup`
 *     * `GET  /dungeon`, `POST /dungeon/enter/<id>`, `GET|POST /dungeon/update`
 *       (`{"action": "confirm_dialogue"}`), `POST /dungeon/battle`
 *       (`{"type": "attack"|"cower"|"use_item"}`), `GET /dungeon/battle/update`
 *       (top-level `"battle_result": true` + `response.{type:"round", latest_update, data}`),
 *       `POST /dungeon/exit/<id>` (`response.data.{coins, lootdrop}`)
 *     * `GET  /party` (including upstream `"party_onwer"` spelling at `PartyManager.cpp:31`),
 *       `POST /party/create`, `POST /party/join`, `POST /party/leave`, `POST /party/destroy`
 *     * `GET  /bank/statement` (`response.balance`), `GET /vend` (`response.pending_lootdrop`),
 *       `POST /vend/unlock` (`response.unlock_code`)
 * - All 29 8-character minibadge labels from `src/badge.cpp` (`simpleMinibadges[4]` +
 *   `minibadgeList[25]`) plus `"redactd "` (`0x13` special serial in `i2cmanager.cpp`),
 *   all 4 player classes (`Blue Team`, `Red Team`, `Social Engineer`, `Physical Security`),
 *   levels `1..4`, battle backgrounds `1..7` (`Battle.cpp:41-53`), and animated GIF
 *   sprite names from `src/AnimatedSprite.cpp` match the compiled firmware.
 *
 * ============================================================================
 * INVENTED FOR SOLO MUSEUM PLAY (ORIGINAL `sc23-api.redactd.net` SERVER IS GONE)
 * ============================================================================
 * - Seven Incident campaigns (`1. Bot Training` through `7. SOC Meltdown`), their
 *   dialogue lines, enemy HP/ATK curves, coin/loot rewards, and minibadge-to-gear
 *   stat mappings are reconstructed from the enemy/NPC GIF assets compiled into
 *   `Firmware/Source/include/assets/bosses/` so every Incident is playable solo.
 * - Although the firmware binary also embeds committee portrait GIFs
 *   (`include/assets/ui/committee.h`), this stand-in server uses only fictional
 *   mascot, NPC, and enemy sprites (`Player`, `Klippy`, `Receptionist`, `Employee`,
 *   `SentinelBot`, `Lockbot`, `Reppilk`, and the Incident boss sprites) and never
 *   impersonates real people.
 * - Solo party & station helpers exposed to the emulator page (`window.__sc23FakeServer`):
 *     * Fictional robot allies (`BotAlpha`, `BotBravo`) can join the visitor's party
 *       either by beaming virtual Bot Party IR code `232323` on `Find Party` or by
 *       toggling bot allies in the companion panel after completing `1. Bot Training`.
 *     * `issueLevelUpCode()` generates a 6-digit level-up code to beam via virtual NEC
 *       IR on the `Level-up` screen (levels `1 -> 4`).
 *     * Redeeming a loot drop at `Vending machine` (`POST /vend/unlock`) generates a
 *       6-digit vending unlock code and awards `+25` bonus coins.
 */
(function () {
  'use strict';

  const STORAGE_KEY = 'saintcon_2023_stock_server';
  const BOT_PARTY_CODE = 232323;
  const STATIC_LEVEL_CODES = [202301, 202302, 202303, 202304];

  function isMockMode() {
    try {
      if (typeof window !== 'undefined' && window.location) {
        return new URLSearchParams(window.location.search).get('mock') === '1';
      }
    } catch (_) {}
    return false;
  }

  /* All 29 firmware minibadge 8-char labels (src/badge.cpp) + "redactd " */
  const MINIBADGE_CATALOG = [
    /* simpleMinibadges[4] */
    { label: 'Glasses ', slot: 'helmet', name: 'Safety Glasses',      stat: '+8 Max HP, +2 ATK',       hp: 8,  atk: 2,  def: 0, heal: 0 },
    { label: 'IDBadge ', slot: 'armor',  name: 'Conference ID',       stat: '+10 Max HP, +2 DEF',      hp: 10, atk: 0,  def: 2, heal: 0 },
    { label: 'Polo    ', slot: 'armor',  name: 'Responder Polo',      stat: '+15 Max HP, +3 DEF',      hp: 15, atk: 0,  def: 3, heal: 0 },
    { label: 'Keyboard', slot: 'weapon', name: 'Standard Keyboard',   stat: '+6 ATK',                  hp: 0,  atk: 6,  def: 0, heal: 0 },

    /* minibadgeList[25] — Helmets */
    { label: 'KlipperH', slot: 'helmet', name: 'Klipper Helm',        stat: '+25 Max HP, +4 DEF',      hp: 25, atk: 0,  def: 4, heal: 0 },
    { label: 'SCCap   ', slot: 'helmet', name: 'SAINTCON Cap',        stat: '+15 Max HP, +2 DEF',      hp: 15, atk: 0,  def: 2, heal: 0 },
    { label: 'Mustache', slot: 'helmet', name: 'Disguise Mustache',   stat: '+12 Max HP, +4 ATK',      hp: 12, atk: 4,  def: 0, heal: 0 },
    { label: 'BlackHat', slot: 'helmet', name: 'Black Hat',           stat: '+10 Max HP, +10 ATK',     hp: 10, atk: 10, def: 0, heal: 0 },
    { label: 'WhiteHat', slot: 'helmet', name: 'White Hat',           stat: '+20 Max HP, +5 DEF',      hp: 20, atk: 0,  def: 5, heal: 0 },
    { label: 'Tinfoil ', slot: 'helmet', name: 'Tinfoil Hat',         stat: '+18 Max HP, +6 DEF',      hp: 18, atk: 0,  def: 6, heal: 0 },

    /* minibadgeList[25] — Armor */
    { label: 'SCHoodie', slot: 'armor',  name: 'SAINTCON Hoodie',     stat: '+30 Max HP, +6 DEF',      hp: 30, atk: 0,  def: 6, heal: 0 },
    { label: 'Hawaiian', slot: 'armor',  name: 'Hawaiian Shirt',      stat: '+22 Max HP, +4 ATK',      hp: 22, atk: 4,  def: 3, heal: 0 },
    { label: 'T-Rex   ', slot: 'armor',  name: 'Inflatable T-Rex',    stat: '+40 Max HP, +7 DEF',      hp: 40, atk: 0,  def: 7, heal: 0 },
    { label: 'Stress  ', slot: 'armor',  name: 'Stress Ball Vest',    stat: '+16 Max HP, +4 DEF',      hp: 16, atk: 0,  def: 4, heal: 0 },

    /* minibadgeList[25] — Weapons */
    { label: 'Sword   ', slot: 'weapon', name: 'Pixel Broadsword',    stat: '+14 ATK',                 hp: 0,  atk: 14, def: 0, heal: 0 },
    { label: 'CustKeeb', slot: 'weapon', name: 'Custom Mech Keeb',    stat: '+12 ATK, +5 Max HP',      hp: 5,  atk: 12, def: 0, heal: 0 },
    { label: 'Lockpick', slot: 'weapon', name: 'Titanium Picks',      stat: '+11 ATK',                 hp: 0,  atk: 11, def: 0, heal: 0 },
    { label: 'Solderin', slot: 'weapon', name: 'Soldering Iron',      stat: '+16 ATK',                 hp: 0,  atk: 16, def: 0, heal: 0 },
    { label: 'Phone   ', slot: 'weapon', name: 'Phreak Handset',      stat: '+9 ATK, +10 Max HP',      hp: 10, atk: 9,  def: 0, heal: 0 },
    { label: 'redactd ', slot: 'weapon', name: 'Zero-Day Exploit',    stat: '+20 ATK, +20 Max HP',     hp: 20, atk: 20, def: 0, heal: 0 },

    /* minibadgeList[25] — Usable Battle Items */
    { label: 'Coffee  ', slot: 'item',   name: 'Hot Coffee',          stat: 'Use: Heal 40 HP',         hp: 0,  atk: 0,  def: 0, heal: 40 },
    { label: 'MtnDew  ', slot: 'item',   name: 'Mountain Dew',        stat: 'Use: Heal 35 HP',         hp: 0,  atk: 0,  def: 0, heal: 35 },
    { label: 'Monster ', slot: 'item',   name: 'Energy Drink',        stat: 'Use: Heal 45 HP',         hp: 0,  atk: 0,  def: 0, heal: 45 },
    { label: 'PepsiCan', slot: 'item',   name: 'Chilled Soda',        stat: 'Use: Heal 30 HP',         hp: 0,  atk: 0,  def: 0, heal: 30 },
    { label: 'Pizza   ', slot: 'item',   name: 'Hot Pizza Slice',     stat: 'Use: Heal 50 HP',         hp: 0,  atk: 0,  def: 0, heal: 50 },
    { label: 'Taco    ', slot: 'item',   name: 'Street Taco',         stat: 'Use: Heal 45 HP',         hp: 0,  atk: 0,  def: 0, heal: 45 },
    { label: 'Cheetos ', slot: 'item',   name: 'Cheetos Stash',       stat: 'Use: Heal 30 HP',         hp: 0,  atk: 0,  def: 0, heal: 30 },
    { label: 'Candy   ', slot: 'item',   name: 'Sugar Rush Candy',    stat: 'Use: Heal 25 HP',         hp: 0,  atk: 0,  def: 0, heal: 25 },
    { label: '1up     ', slot: 'item',   name: '1-UP Mushroom',       stat: 'Use: Heal 75 HP',         hp: 0,  atk: 0,  def: 0, heal: 75 },
    { label: 'CatVideo', slot: 'item',   name: 'Cat Video Clip',      stat: 'Use: Heal 35 HP',         hp: 0,  atk: 0,  def: 0, heal: 35 }
  ];

  const MINIBADGE_BY_LABEL = Object.create(null);
  const FIRMWARE_MINIBADGE_LABELS = [
    'IDBadge ', 'Keyboard', 'Glasses ', 'Polo    ',
    'KlipperH', 'Sword   ', 'CustKeeb', 'SCHoodie', 'SCCap   ',
    'Pizza   ', 'Mustache', 'MtnDew  ', 'Monster ', 'T-Rex   ',
    'Coffee  ', 'Stress  ', '1up     ', 'CatVideo', 'Cheetos ',
    'Taco    ', 'Phone   ', 'BlackHat', 'WhiteHat', 'Tinfoil ',
    'Lockpick', 'Solderin', 'Candy   ', 'Hawaiian', 'PepsiCan',
    'redactd '
  ];
  for (const mb of MINIBADGE_CATALOG) {
    mb.wasm_index = FIRMWARE_MINIBADGE_LABELS.indexOf(mb.label);
    MINIBADGE_BY_LABEL[mb.label] = mb;
    MINIBADGE_BY_LABEL[mb.label.trim()] = mb;
  }

  /* 4 classes from src/NewCharacter.cpp */
  const CLASS_BASE_STATS = [
    { name: 'Blue Team',         hp: 110, atk: 20, def: 4 },
    { name: 'Red Team',          hp: 95,  atk: 25, def: 2 },
    { name: 'Social Engineer',   hp: 100, atk: 22, def: 3 },
    { name: 'Physical Security', hp: 120, atk: 19, def: 5 }
  ];

  /* 7 Incidents — IDs 1..7 map cleanly to Battle.cpp background switch & 320x240 Menu bounds */
  const INCIDENTS = {
    1: {
      id: 1,
      name: '1. Bot Training',
      coins: 25,
      lootdrop: 1,
      completes_training: true,
      steps: [
        { type: 'dialogue', avatar: 'Klippy', text: 'Welcome responder! Clear the training sim to unlock Party mode.' },
        { type: 'battle',   enemy: { name: 'TrainingBot1', hp: 40, atk: 7 } },
        { type: 'battle',   enemy: { name: 'TrainingBot2', hp: 55, atk: 9 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Final wave: defeat the Super Training Bot!' },
        { type: 'battle',   enemy: { name: 'SuperTrainingBot', hp: 75, atk: 11 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Training complete! Party mode and Incident bounties are unlocked.' }
      ]
    },
    2: {
      id: 2,
      name: '2. Packet Storm',
      coins: 40,
      lootdrop: 1,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'SentinelBot', text: 'Core switch alert: malformed packets are flooding the uplink!' },
        { type: 'battle',   enemy: { name: 'PCAP', hp: 80, atk: 12 } },
        { type: 'battle',   enemy: { name: 'DupACK', hp: 95, atk: 14 } },
        { type: 'dialogue', avatar: 'Player', text: 'Duplicate ACKs cleared. Something is pinning the TCP window at zero!' },
        { type: 'battle',   enemy: { name: 'TCPZWindow', hp: 115, atk: 16 } },
        { type: 'dialogue', avatar: 'SentinelBot', text: 'Link throughput restored. Packet capture archived to the vault.' }
      ]
    },
    3: {
      id: 3,
      name: '3. Helpdesk Maze',
      coins: 55,
      lootdrop: 1,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'Receptionist', text: 'Ticket queue critical! Callers are stuck in an endless IVR loop.' },
        { type: 'battle',   enemy: { name: 'PhoneTreeDeadends', hp: 105, atk: 15 } },
        { type: 'dialogue', avatar: 'Employee', text: 'Pressing 0 just escalated us to the final boss of customer support!' },
        { type: 'battle',   enemy: { name: 'SavvyCustomerServiceRep', hp: 135, atk: 18 } },
        { type: 'dialogue', avatar: 'Receptionist', text: 'Helpdesk queue cleared! Every open ticket is marked resolved.' }
      ]
    },
    4: {
      id: 4,
      name: '4. Perimeter Breach',
      coins: 75,
      lootdrop: 1,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'SentinelBot', text: 'Stealthy port scan detected probing the external DMZ!' },
        { type: 'battle',   enemy: { name: 'SneakyNMAP', hp: 125, atk: 18 } },
        { type: 'battle',   enemy: { name: 'IPSentry', hp: 145, atk: 20 } },
        { type: 'battle',   enemy: { name: 'WAF', hp: 165, atk: 22 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Heads up! An unpatched legacy CVE is lurking behind the WAF!' },
        { type: 'battle',   enemy: { name: 'OldCVE', hp: 190, atk: 24 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Emergency hotfix deployed. External perimeter hardened!' }
      ]
    },
    5: {
      id: 5,
      name: '5. Physical Access',
      coins: 95,
      lootdrop: 1,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'Lockbot', text: 'Motion sensors tripped along the datacenter corridor!' },
        { type: 'battle',   enemy: { name: 'RandomCeilingTile', hp: 145, atk: 21 } },
        { type: 'battle',   enemy: { name: 'CrashCartWithABadWheel', hp: 170, atk: 23 } },
        { type: 'battle',   enemy: { name: 'SecureDoor', hp: 200, atk: 26 } },
        { type: 'battle',   enemy: { name: 'ForceFieldPadlock', hp: 230, atk: 28 } },
        { type: 'dialogue', avatar: 'Lockbot', text: 'Mantrap sealed and badge readers verified.' }
      ]
    },
    6: {
      id: 6,
      name: '6. Identity Vault',
      coins: 120,
      lootdrop: 2,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'Employee', text: 'Legacy switch syntax and rogue auth tokens locked out the vault!' },
        { type: 'battle',   enemy: { name: 'DeprecatedSwitchSyntax', hp: 175, atk: 25 } },
        { type: 'battle',   enemy: { name: 'RockyCoinChanger', hp: 205, atk: 27 } },
        { type: 'battle',   enemy: { name: 'DoorControl', hp: 235, atk: 30 } },
        { type: 'battle',   enemy: { name: 'CentralizedIdentityProvider', hp: 270, atk: 33 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Signing keys rotated and Identity Vault access restored!' }
      ]
    },
    7: {
      id: 7,
      name: '7. SOC Meltdown',
      coins: 160,
      lootdrop: 2,
      completes_training: false,
      steps: [
        { type: 'dialogue', avatar: 'Reppilk', text: 'Drown in raw telemetry! My minibadge swarm runs this SOC now!' },
        { type: 'battle',   enemy: { name: 'SIEMAlerts', hp: 210, atk: 30 } },
        { type: 'battle',   enemy: { name: 'PilesAndPilesOfRawLogData', hp: 260, atk: 34 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'Alert storm parsed! Brace for the final boss: Minibadge Swarm!' },
        { type: 'battle',   enemy: { name: 'MinibadgeSwarm', hp: 320, atk: 38 } },
        { type: 'dialogue', avatar: 'Klippy', text: 'All seven Incidents contained! The SAINTCON SOC is all green!' }
      ]
    }
  };

  const BOT_ALLIES = [
    { badge_id: 'B07A11A00001', handle: 'BotAlpha', character: 1 },
    { badge_id: 'B07A11A00002', handle: 'BotBravo', character: 2 }
  ];

  function createDefaultState() {
    return {
      schema: 1,
      badge_id: '202353430001',
      registered: false,
      profile: {
        handle: 'Responder',
        email: 'museum@sc23.local',
        character: 0,
        class: 0,
        haircolor: 1,
        clothingcolor: 1,
        eyecolor: 1,
        completed_training: 0,
        staff: 0,
        level: 1
      },
      coins: 15,
      pending_lootdrop: 1,
      last_vend_code: null,
      equipped_slots: [null, null, null, null],
      gear: {},
      party: {
        in_party: false,
        party_code: 0,
        owner_id: '',
        bot_allies: false
      },
      issued_level_codes: [],
      incidents_cleared: {},
      dungeon_run: null,
      rng_counter: 17
    };
  }

  let state = loadState();

  function loadState() {
    const fresh = createDefaultState();
    if (isMockMode()) return fresh;
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return fresh;
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== 'object') return fresh;
      return Object.assign(fresh, parsed, {
        profile: Object.assign(fresh.profile, parsed.profile || {}),
        party: Object.assign(fresh.party, parsed.party || {})
      });
    } catch (_) {
      return fresh;
    }
  }

  function saveState() {
    if (isMockMode()) return;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch (_) {}
    notifyListeners();
  }

  const listeners = new Set();
  function notifyListeners() {
    for (const fn of listeners) {
      try { fn(getStateSummary()); } catch (_) {}
    }
  }

  function nextRoll(span) {
    state.rng_counter = ((state.rng_counter * 1103515245 + 12345) >>> 0);
    if (!span || span <= 1) return 0;
    return (state.rng_counter >>> 16) % span;
  }

  function computeEquippedGear(slotLabels) {
    const best = { helmet: null, armor: null, weapon: null, item: null };
    for (const raw of slotLabels) {
      if (!raw || typeof raw !== 'string') continue;
      const mb = MINIBADGE_BY_LABEL[raw] || MINIBADGE_BY_LABEL[raw.trim()];
      if (!mb) continue;
      const score = mb.hp + mb.atk * 3 + mb.def * 3 + mb.heal * 2;
      const cur = best[mb.slot];
      const curScore = cur ? (cur.hp + cur.atk * 3 + cur.def * 3 + cur.heal * 2) : -1;
      if (score > curScore) {
        best[mb.slot] = mb;
      }
    }
    return best;
  }

  function computePlayerCombatStats() {
    const cls = CLASS_BASE_STATS[state.profile.class] || CLASS_BASE_STATS[0];
    const lvl = Math.max(1, Math.min(4, Number(state.profile.level) || 1));
    const best = computeEquippedGear(state.equipped_slots || []);
    let maxHp = cls.hp + (lvl - 1) * 25;
    let atk = cls.atk + (lvl - 1) * 7;
    let def = cls.def + (lvl - 1) * 2;
    let heal = 30;
    let hasItem = false;
    for (const slot of ['helmet', 'armor', 'weapon', 'item']) {
      const g = best[slot];
      if (!g) continue;
      maxHp += g.hp;
      atk += g.atk;
      def += g.def;
      if (slot === 'item' && g.heal > 0) {
        heal = g.heal;
        hasItem = true;
      }
    }
    return { max_hp: maxHp, atk, def, heal, has_item: hasItem, best };
  }

  function buildPartyRoster(playerStats) {
    const bid = state.badge_id || '202353430001';
    const handle = (state.profile.handle && state.profile.handle.trim()) ? state.profile.handle.trim() : 'Responder';
    const roster = [
      {
        badge_id: bid,
        handle: handle,
        character: Number(state.profile.character) || 0,
        max_hp: playerStats.max_hp,
        current_hp: playerStats.max_hp,
        atk: playerStats.atk,
        def: playerStats.def,
        in_burnout: 0,
        is_bot: false
      }
    ];
    if (state.party.in_party && state.party.bot_allies) {
      const lvl = Math.max(1, Math.min(4, Number(state.profile.level) || 1));
      for (let i = 0; i < BOT_ALLIES.length; i++) {
        const bot = BOT_ALLIES[i];
        const botMax = 85 + (lvl - 1) * 20 + i * 10;
        roster.push({
          badge_id: bot.badge_id,
          handle: bot.handle,
          character: bot.character,
          max_hp: botMax,
          current_hp: botMax,
          atk: 15 + (lvl - 1) * 5 + i * 2,
          def: 3 + (lvl - 1),
          in_burnout: 0,
          is_bot: true
        });
      }
    }
    return roster;
  }

  function startBattleForStep(run, step) {
    const pStats = computePlayerCombatStats();
    /* Refresh player max_hp/atk/def if minibadges changed mid-dungeon */
    if (run.roster && run.roster.length > 0) {
      const p0 = run.roster[0];
      const deltaMax = pStats.max_hp - p0.max_hp;
      p0.max_hp = pStats.max_hp;
      p0.atk = pStats.atk;
      p0.def = pStats.def;
      if (deltaMax > 0 && p0.current_hp > 0) {
        p0.current_hp = Math.min(p0.max_hp, p0.current_hp + deltaMax);
      } else {
        p0.current_hp = Math.min(p0.max_hp, p0.current_hp);
      }
      if (p0.current_hp <= 0) {
        p0.current_hp = Math.max(1, Math.floor(p0.max_hp / 2));
        p0.in_burnout = 0;
      }
    }
    run.battle = {
      enemy_name: step.enemy.name,
      enemy_hp: step.enemy.hp,
      enemy_max_hp: step.enemy.hp,
      enemy_atk: step.enemy.atk,
      item_used: false,
      last_round: null,
      outcome: null
    };
  }

  function buildBattleInitData(run) {
    return {
      enemy: {
        name: run.battle.enemy_name,
        hp: run.battle.enemy_hp
      },
      characters: run.roster.map((c) => ({
        character: c.character,
        badge_id: c.badge_id,
        current_hp: c.current_hp,
        max_hp: c.max_hp,
        handle: c.handle,
        in_burnout: c.in_burnout ? 1 : 0
      }))
    };
  }

  function resolveBattleRound(run, actionType) {
    const b = run.battle;
    const pStats = computePlayerCombatStats();
    const roundActions = [];
    const cowering = Object.create(null);
    const pendingHeals = Object.create(null);

    for (let i = 0; i < run.roster.length; i++) {
      const member = run.roster[i];
      if (member.in_burnout || member.current_hp <= 0) continue;

      let act = 'attack';
      if (!member.is_bot) {
        if (actionType === 'cower' || actionType === 'use_item' || actionType === 'attack') {
          act = actionType;
        }
      } else {
        /* Bot allies cower briefly if below 25% HP, otherwise attack */
        act = (member.current_hp * 4 < member.max_hp && (nextRoll(3) === 0)) ? 'cower' : 'attack';
      }

      if (act === 'cower') {
        cowering[member.badge_id] = true;
        roundActions.push({
          actor: member.badge_id,
          action: 'cower',
          damage_dealt: 0,
          target_died: false,
          heal: 0
        });
      } else if (act === 'use_item') {
        const rawHeal = (!b.item_used && pStats.has_item) ? pStats.heal : 25;
        b.item_used = true;
        pendingHeals[member.badge_id] = rawHeal;
        roundActions.push({
          actor: member.badge_id,
          action: 'use_item',
          damage_dealt: 0,
          target_died: false,
          heal: rawHeal
        });
      } else {
        let dmg = 0;
        let killed = false;
        if (b.enemy_hp > 0) {
          dmg = Math.max(5, member.atk + nextRoll(5));
          if (dmg >= b.enemy_hp) {
            b.enemy_hp = 0;
            killed = true;
          } else {
            b.enemy_hp -= dmg;
          }
        }
        roundActions.push({
          actor: member.badge_id,
          action: 'attack',
          damage_dealt: dmg,
          target_died: killed,
          heal: 0
        });
      }
    }

    if (b.enemy_hp <= 0) {
      /* Battle.cpp:308 transitions directly from ActionEnemyDamage to ActionEnemyDefeat
       * without running ActionEnemyAttack or ActionResolveRound, so no enemy counterattack
       * or post-round heal occurs on the killing blow. */
      b.outcome = 'victory';
      b.last_round = roundActions;
      return;
    }

    /* Enemy counterattack against living roster members */
    const alive = run.roster.filter((m) => !m.in_burnout && m.current_hp > 0);
    const targetsObj = {};
    if (alive.length > 0) {
      /* Pick primary target (weighted toward front, occasionally splash in later Incidents) */
      const primaryIdx = nextRoll(alive.length);
      const splashAll = (run.incident_id >= 6 && alive.length > 1 && nextRoll(3) === 0);
      for (let i = 0; i < alive.length; i++) {
        const m = alive[i];
        if (!splashAll && i !== primaryIdx) {
          targetsObj[m.badge_id] = { damage_dealt: 0, target_died: false };
          continue;
        }
        const rawAtk = splashAll ? Math.round(b.enemy_atk * 0.7) : b.enemy_atk;
        let dmg = Math.max(2, rawAtk - m.def + nextRoll(4) - 1);
        if (cowering[m.badge_id]) {
          dmg = Math.max(1, Math.floor(dmg * 0.35));
        }
        let died = false;
        if (dmg >= m.current_hp) {
          dmg = m.current_hp;
          m.current_hp = 0;
          m.in_burnout = 1;
          died = true;
        } else {
          m.current_hp -= dmg;
        }
        targetsObj[m.badge_id] = { damage_dealt: dmg, target_died: died };
      }
    }

    /* Clamp any use_item heal so client-side Battle.cpp:234 (health += healing after
     * ActionEnemyAttack subtracts round_dmg_taken) matches server current_hp and never
     * overflows max_hp. */
    for (const actEntry of roundActions) {
      if (actEntry.action === 'use_item') {
        const member = run.roster.find((m) => m.badge_id === actEntry.actor);
        if (member) {
          if (member.in_burnout || member.current_hp <= 0) {
            actEntry.heal = 0;
          } else {
            const actualHeal = Math.max(0, Math.min(pendingHeals[member.badge_id] || 0, member.max_hp - member.current_hp));
            member.current_hp += actualHeal;
            actEntry.heal = actualHeal;
          }
        }
      }
    }

    roundActions.push({
      actor: 'enemy',
      targets: targetsObj
    });

    const anyAlive = run.roster.some((m) => !m.in_burnout && m.current_hp > 0);
    if (!anyAlive) {
      b.outcome = 'defeat';
    }
    b.last_round = roundActions;
  }

  function parseAuthBadgeId(headersStr) {
    if (!headersStr || typeof headersStr !== 'string') return null;
    const lines = headersStr.split(/\r?\n/);
    for (const line of lines) {
      const idx = line.indexOf(':');
      if (idx < 0) continue;
      const k = line.slice(0, idx).trim().toLowerCase();
      if (k === 'x-auth-id') {
        const v = line.slice(idx + 1).trim();
        if (v) return v;
      }
    }
    return null;
  }

  function jsonResp(obj, httpStatus) {
    return {
      status: httpStatus || 200,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(obj)
    };
  }

  function okResp(responseVal, extraTopLevel) {
    return jsonResp(Object.assign({ status: true, response: responseVal }, extraTopLevel || {}));
  }

  function errResp(message) {
    return jsonResp({ status: false, error: String(message || 'Request failed') });
  }

  function extractPath(urlStr) {
    try {
      const u = new URL(urlStr, 'https://sc23-api.redactd.net:8443');
      return u.pathname;
    } catch (_) {
      const m = String(urlStr || '').match(/^https?:\/\/[^/]+(\/.*)$/i);
      return m ? m[1] : String(urlStr || '/');
    }
  }

  function handleRequest(method, url, headersStr, bodyStr) {
    const authId = parseAuthBadgeId(headersStr);
    if (authId && authId !== state.badge_id) {
      const prevId = state.badge_id;
      state.badge_id = authId;
      if (state.party.owner_id === prevId) {
        state.party.owner_id = authId;
      }
    }

    const path = extractPath(url);
    const upperMethod = String(method || 'GET').toUpperCase();
    let payload = {};
    if (bodyStr && typeof bodyStr === 'string' && bodyStr.trim().length > 0) {
      try { payload = JSON.parse(bodyStr); } catch (_) { payload = {}; }
    }

    /* 1. Firmware OTA check — return 1.4 so firmware never enters OTA loop */
    if (path === '/badge/firmware_version') {
      return okResp({ version: 1.4 });
    }

    /* 2. Badge profile lookup */
    if (path === '/badge/whois') {
      if (!state.registered) {
        return okResp('unregistered');
      }
      return okResp({
        character: Number(state.profile.character) || 0,
        email: String(state.profile.email || ''),
        eyecolor: Number(state.profile.eyecolor) || 1,
        haircolor: Number(state.profile.haircolor) || 1,
        handle: String(state.profile.handle || 'Responder'),
        class: Number(state.profile.class) || 0,
        completed_training: state.profile.completed_training ? 1 : 0,
        staff: state.profile.staff ? 1 : 0,
        level: Math.max(1, Math.min(4, Number(state.profile.level) || 1))
      });
    }

    /* 3. Character registration / Edit Character */
    if (path === '/badge/register') {
      const rawHandle = (typeof payload.handle === 'string' && payload.handle.trim().length > 0)
        ? payload.handle.trim().slice(0, 25)
        : 'Responder';
      const rawEmail = (typeof payload.email === 'string') ? payload.email.trim().slice(0, 50) : '';
      state.profile.class = Math.max(0, Math.min(3, Number(payload.class) || 0));
      state.profile.character = Math.max(0, Math.min(3, Number(payload.character) || 0));
      state.profile.handle = rawHandle;
      state.profile.email = rawEmail;
      state.profile.haircolor = Number(payload.haircolor) || 1;
      state.profile.clothingcolor = Number(payload.clothingcolor) || 1;
      state.profile.eyecolor = Number(payload.eyecolor) || 1;
      state.registered = true;
      saveState();
      return okResp('registered');
    }

    /* 4. Minibadge equip */
    if (path === '/badge/equip') {
      const slotNames = ['slot1', 'slot2', 'slot3', 'slot4'];
      const labels = [null, null, null, null];
      for (let i = 0; i < 4; i++) {
        const s = payload[slotNames[i]];
        if (s && typeof s.name === 'string' && s.name.length > 0) {
          labels[i] = s.name;
        }
      }
      state.equipped_slots = labels;
      const best = computeEquippedGear(labels);
      const gearResp = {};
      for (const slot of ['helmet', 'armor', 'weapon', 'item']) {
        const g = best[slot];
        if (!g) continue;
        if (slot === 'item') {
          gearResp.item = {
            name: g.name,
            stat: g.stat,
            usable: true,
            useable: true
          };
        } else {
          gearResp[slot] = {
            name: g.name,
            stat: g.stat
          };
        }
      }
      state.gear = gearResp;
      if (state.dungeon_run && state.dungeon_run.roster && state.dungeon_run.roster.length > 0) {
        const pStats = computePlayerCombatStats();
        state.dungeon_run.roster[0].atk = pStats.atk;
        state.dungeon_run.roster[0].def = pStats.def;
      }
      saveState();
      return okResp(gearResp);
    }

    /* 5. Level-up code verification & generation */
    if (path === '/badge/levelup') {
      const code = Number(payload.levelup_code) || 0;
      const curLvl = Math.max(1, Math.min(4, Number(state.profile.level) || 1));
      if (curLvl >= 4) {
        return errResp('Already at max level 4');
      }
      const idx = state.issued_level_codes.indexOf(code);
      const isStatic = STATIC_LEVEL_CODES.indexOf(code) >= 0;
      if (idx < 0 && !isStatic) {
        return errResp('Invalid level code');
      }
      if (idx >= 0) {
        state.issued_level_codes.splice(idx, 1);
      }
      state.profile.level = Math.min(4, curLvl + 1);
      saveState();
      return okResp({ level: state.profile.level });
    }

    if (path === '/badge/generate_levelup') {
      const code = issueLevelUpCode();
      return okResp({ levelup_code: code });
    }

    /* 6. Incident (Dungeon) list */
    if (path === '/dungeon') {
      const list = {};
      for (const idStr of Object.keys(INCIDENTS)) {
        const inc = INCIDENTS[idStr];
        list[idStr] = {
          name: inc.name,
          available: true
        };
      }
      return okResp(list);
    }

    /* 7. Enter Incident */
    const enterMatch = path.match(/^\/dungeon\/enter\/(\d+)$/);
    if (enterMatch) {
      const id = Number(enterMatch[1]);
      const inc = INCIDENTS[id];
      if (!inc) return errResp('Unknown Incident');
      const pStats = computePlayerCombatStats();
      state.dungeon_run = {
        incident_id: id,
        step_index: 0,
        roster: buildPartyRoster(pStats),
        pending_enemy_defeated_ack: false,
        pending_recovery_ack: false,
        battle: null
      };
      saveState();
      return okResp('entered');
    }

    /* 8. Incident step progression */
    if (path === '/dungeon/update') {
      const run = state.dungeon_run;
      if (!run || !INCIDENTS[run.incident_id]) {
        return errResp('Not in an Incident');
      }
      const inc = INCIDENTS[run.incident_id];
      const isConfirm = (upperMethod === 'POST' && payload && payload.action === 'confirm_dialogue');

      /* Handle post-defeat med-bay recovery dialogue confirmation */
      if (run.pending_recovery_ack) {
        if (isConfirm) {
          run.pending_recovery_ack = false;
          const pStats = computePlayerCombatStats();
          run.roster = buildPartyRoster(pStats);
          const retryStep = inc.steps[run.step_index];
          if (retryStep && retryStep.type === 'battle') {
            startBattleForStep(run, retryStep);
            saveState();
            return okResp({
              type: 'battle',
              data: buildBattleInitData(run)
            });
          }
        } else {
          return okResp({
            type: 'dialogue',
            data: {
              avatar: 'Klippy',
              text: 'Burnout recovery complete! HP restored to 100% for another try.'
            }
          });
        }
      }

      /* Handle post-battle outcome when Battle::Run() returns to Dungeon::Run() */
      if (run.battle && run.battle.outcome === 'victory') {
        run.battle = null;
        run.pending_enemy_defeated_ack = true;
        /* Recover 25% HP for surviving members between waves */
        for (const m of run.roster) {
          if (!m.in_burnout && m.current_hp > 0) {
            m.current_hp = Math.min(m.max_hp, m.current_hp + Math.max(8, Math.floor(m.max_hp * 0.25)));
          }
        }
        saveState();
        return okResp({
          type: 'admin',
          message: 'enemy_defeated',
          data: {}
        });
      }

      if (run.battle && run.battle.outcome === 'defeat') {
        run.battle = null;
        run.pending_recovery_ack = true;
        saveState();
        return okResp({
          type: 'dialogue',
          data: {
            avatar: 'Klippy',
            text: 'Burnout recovery complete! HP restored to 100% for another try.'
          }
        });
      }

      if (run.pending_enemy_defeated_ack) {
        if (isConfirm) {
          run.pending_enemy_defeated_ack = false;
          run.step_index += 1;
        }
      } else if (isConfirm) {
        const curStep = inc.steps[run.step_index];
        if (curStep && curStep.type === 'dialogue') {
          run.step_index += 1;
        }
      }

      if (run.step_index >= inc.steps.length) {
        saveState();
        return okResp({
          type: 'admin',
          message: 'dungeon_complete',
          data: {}
        });
      }

      const step = inc.steps[run.step_index];
      if (step.type === 'dialogue') {
        saveState();
        return okResp({
          type: 'dialogue',
          data: {
            avatar: step.avatar,
            text: step.text
          }
        });
      }

      if (step.type === 'battle') {
        if (!run.battle) {
          startBattleForStep(run, step);
        }
        saveState();
        return okResp({
          type: 'battle',
          data: buildBattleInitData(run)
        });
      }

      return errResp('Invalid Incident step');
    }

    /* 9. Submit battle action */
    if (path === '/dungeon/battle') {
      const run = state.dungeon_run;
      if (!run || !run.battle) {
        return errResp('enemy defeated');
      }
      if (run.battle.enemy_hp <= 0 || run.battle.outcome === 'victory') {
        return errResp('enemy defeated');
      }
      const actType = String(payload.type || 'attack');
      resolveBattleRound(run, actType);
      saveState();
      return okResp('action_recorded');
    }

    /* 10. Poll battle round resolution */
    if (path === '/dungeon/battle/update') {
      const run = state.dungeon_run;
      if (!run || !run.battle) {
        return errResp('No active battle');
      }
      if (!run.battle.last_round) {
        return okResp(
          { type: 'admin', latest_update: true, message: 'send_action' },
          { battle_result: false }
        );
      }
      const roundData = run.battle.last_round;
      return okResp(
        {
          type: 'round',
          latest_update: true,
          data: roundData
        },
        { battle_result: true }
      );
    }

    /* 11. Exit completed Incident & award loot */
    const exitMatch = path.match(/^\/dungeon\/exit\/(\d+)$/);
    if (exitMatch) {
      const id = Number(exitMatch[1]);
      const inc = INCIDENTS[id] || (state.dungeon_run && INCIDENTS[state.dungeon_run.incident_id]);
      const coinsAwarded = inc ? inc.coins : 25;
      const dropsAwarded = inc ? inc.lootdrop : 1;
      state.coins = (Number(state.coins) || 0) + coinsAwarded;
      state.pending_lootdrop = (Number(state.pending_lootdrop) || 0) + dropsAwarded;
      if (inc && inc.completes_training) {
        state.profile.completed_training = 1;
      }
      if (inc) {
        state.incidents_cleared[inc.id] = (Number(state.incidents_cleared[inc.id]) || 0) + 1;
      }
      state.dungeon_run = null;
      saveState();
      return okResp({
        data: {
          coins: coinsAwarded,
          lootdrop: dropsAwarded
        }
      });
    }

    /* 12. Party endpoints */
    if (path === '/party') {
      if (!state.party.in_party) {
        return errResp('not in party');
      }
      const handle = (state.profile.handle && state.profile.handle.trim()) ? state.profile.handle.trim() : 'Responder';
      const members = [
        { handle: handle, character: Number(state.profile.character) || 0 }
      ];
      if (state.party.bot_allies) {
        for (const bot of BOT_ALLIES) {
          members.push({ handle: bot.handle, character: bot.character });
        }
      }
      const ownerId = state.party.owner_id || state.badge_id;
      return okResp(members, {
        party_code: state.party.party_code || BOT_PARTY_CODE,
        party_onwer: ownerId, /* Upstream spelling in PartyManager.cpp:31 */
        party_owner: ownerId
      });
    }

    if (path === '/party/create') {
      const code = 230000 + 100 + nextRoll(8900);
      state.party.in_party = true;
      state.party.party_code = code;
      state.party.owner_id = state.badge_id;
      saveState();
      return okResp({ code: code });
    }

    if (path === '/party/join') {
      const code = Number(payload.party_code) || 0;
      if (code === BOT_PARTY_CODE) {
        state.party.in_party = true;
        state.party.party_code = BOT_PARTY_CODE;
        state.party.owner_id = BOT_ALLIES[0].badge_id;
        state.party.bot_allies = true;
        saveState();
        return okResp('joined');
      }
      if (state.party.in_party && code === state.party.party_code) {
        return okResp('joined');
      }
      if (code >= 100000 && code <= 999999) {
        state.party.in_party = true;
        state.party.party_code = code;
        state.party.owner_id = BOT_ALLIES[0].badge_id;
        state.party.bot_allies = true;
        saveState();
        return okResp('joined');
      }
      return errResp('Party not found');
    }

    if (path === '/party/leave' || path === '/party/destroy') {
      state.party.in_party = false;
      state.party.party_code = 0;
      state.party.owner_id = '';
      state.party.bot_allies = false;
      saveState();
      return okResp('left');
    }

    /* 13. Bank & Vending Machine endpoints */
    if (path === '/bank/statement') {
      return okResp({ balance: Number(state.coins) || 0 });
    }

    if (path === '/vend') {
      return okResp({ pending_lootdrop: Number(state.pending_lootdrop) || 0 });
    }

    if (path === '/vend/unlock') {
      const drops = Number(state.pending_lootdrop) || 0;
      if (drops <= 0) {
        return errResp('No loot drops left');
      }
      state.pending_lootdrop = drops - 1;
      state.coins = (Number(state.coins) || 0) + 25;
      const unlockCode = 600000 + 1000 + nextRoll(98000);
      state.last_vend_code = unlockCode;
      saveState();
      return okResp({ unlock_code: unlockCode });
    }

    return errResp('Unknown endpoint: ' + path);
  }

  /* Companion UI helpers for solo museum play */
  function issueLevelUpCode() {
    const code = 410000 + 100 + nextRoll(89000);
    state.issued_level_codes.push(code);
    if (state.issued_level_codes.length > 8) {
      state.issued_level_codes.shift();
    }
    saveState();
    return code;
  }

  function setPartyBotAllies(enabled) {
    const want = Boolean(enabled);
    if (want && !state.profile.completed_training) {
      // Bots only join after "1. Bot Training" is cleared, as on the real badge.
      return Object.assign({}, getStateSummary(), { needs_training: true });
    }
    if (want) {
      if (!state.party.in_party) {
        state.party.in_party = true;
        state.party.party_code = BOT_PARTY_CODE;
        state.party.owner_id = state.badge_id;
      }
      state.party.bot_allies = true;
    } else {
      state.party.bot_allies = false;
    }
    saveState();
    return getStateSummary();
  }

  function grantLootDrop() {
    state.pending_lootdrop = (Number(state.pending_lootdrop) || 0) + 1;
    saveState();
    return getStateSummary();
  }

  function syncMinibadgeIndices(indices) {
    if (!Array.isArray(indices)) return getStateSummary();
    const labels = [null, null, null, null];
    for (let i = 0; i < 4; i++) {
      const idx = Number(indices[i]);
      if (Number.isInteger(idx) && idx >= 0 && idx < FIRMWARE_MINIBADGE_LABELS.length) {
        labels[i] = FIRMWARE_MINIBADGE_LABELS[idx];
      }
    }
    state.equipped_slots = labels;
    if (state.dungeon_run && state.dungeon_run.roster && state.dungeon_run.roster.length > 0) {
      const pStats = computePlayerCombatStats();
      state.dungeon_run.roster[0].atk = pStats.atk;
      state.dungeon_run.roster[0].def = pStats.def;
    }
    saveState();
    return getStateSummary();
  }

  function resetState() {
    state = createDefaultState();
    if (!isMockMode()) {
      try { localStorage.removeItem(STORAGE_KEY); } catch (_) {}
    }
    notifyListeners();
    return getStateSummary();
  }

  function getStateSummary() {
    const pStats = computePlayerCombatStats();
    return {
      registered: state.registered,
      handle: state.profile.handle,
      level: state.profile.level,
      class_id: state.profile.class,
      class_name: (CLASS_BASE_STATS[state.profile.class] || CLASS_BASE_STATS[0]).name,
      trained: Boolean(state.profile.completed_training),
      coins: state.coins,
      pending_lootdrop: state.pending_lootdrop,
      max_hp: pStats.max_hp,
      atk: pStats.atk,
      def: pStats.def,
      in_party: state.party.in_party,
      party_code: state.party.party_code,
      bot_allies: state.party.bot_allies,
      bot_party_ir_code: BOT_PARTY_CODE,
      equipped_slots: (state.equipped_slots || []).slice()
    };
  }

  function onChange(fn) {
    if (typeof fn === 'function') listeners.add(fn);
    return function () { listeners.delete(fn); };
  }

  const api = {
    handleRequest: handleRequest,
    issueLevelUpCode: issueLevelUpCode,
    setPartyBotAllies: setPartyBotAllies,
    grantLootDrop: grantLootDrop,
    syncMinibadgeIndices: syncMinibadgeIndices,
    resetState: resetState,
    getStateSummary: getStateSummary,
    onChange: onChange,
    getMinibadgeCatalog: function () { return MINIBADGE_CATALOG.slice(); },
    BOT_PARTY_CODE: BOT_PARTY_CODE
  };

  if (typeof window !== 'undefined') {
    window.__sc23FakeServer = api;
  }
  if (typeof globalThis !== 'undefined') {
    globalThis.__sc23FakeServer = api;
  }
})();
