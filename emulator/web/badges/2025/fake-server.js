/**
 * SAINTCON 2025 Stock Badge — Per-Visitor In-Browser Fake Game Server
 *
 * Called synchronously from `emulator/cores/2025-stock/platform/http_bridge.c`
 * (`esp_http_client_perform`) via `window.__sc25FakeServer.handleRequest(...)`.
 * All visitor state lives in `localStorage['saintcon_2025_stock_server']`.
 *
 * ============================================================================
 * FAITHFUL TO UPSTREAM FIRMWARE (`compukidmike/Saintcon2025`, `Firmware/badge`)
 * ============================================================================
 * - Endpoint URLs, HTTP methods, and JSON schemas match `components/api/api.cpp`
 *   and `components/api/api_client.cpp` verbatim:
 *     * `HEAD /api/firmware/badge` & `GET /api/firmware/version` (reports `0.2.1`)
 *     * `POST /api/badge/challenge`, `/provision`, `/provision/verify`, `/provision/wireless-psk`
 *     * `POST /api/badge/register` (`{"player_name": ...}`) & `GET /api/badge/status`
 *       (returns HTTP 404 before registration, matching `badge.c:776`)
 *     * `POST /api/badge/redeem_code` (`{"code": "XXXXX"}`, 5-char `TI_BASE32_NC`
 *       alphabet `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` — no `0`, `1`, `I`, `O`)
 *     * `GET /topology/data` (root-level `nodes`, `connections`, `port_status`,
 *       `path_status`, `team_path_scores`, `timestamp` — not wrapped in `data`)
 *     * `POST /api/badge/request_credit_swap` (`{"credit_amount": N}`),
 *       `GET /api/badge/credit_swap_status/<code>`,
 *       `DELETE /api/badge/cancel_credit_swap/<code>`
 *     * `GET /api/badge/achievements`, `GET /api/badge/messages`,
 *       `POST /api/badge/messages/read`, `GET /api/badge/files/list`,
 *       `POST /api/badge/konami`, `POST /api/badge/dawn-accord`,
 *       `GET /api/badge/ctf-hints-password`
 * - All 31 nodes (`1` CORE, `2..7` RING1, `8..19` RING2, `20..25` HQ, `26..31` TOWER)
 *   and all 54 undirected wired port connections match `components/badge_game/nodes.c`
 *   with 1-based port numbers (`1..6`).
 * - All 6 factions, `team_id` (`1..6`) -> `faction_id_t` mappings, HQ node IDs (`20..25`),
 *   and hex colors match `components/badge_game/factions.h` & `factions.c`:
 *     1 = Aether Watch (HQ 20, #00AAE9), 2 = Verdant Pact (HQ 21, #00A359),
 *     3 = Dawn Accord  (HQ 22, #FFF34A), 4 = Iron Howl    (HQ 23, #EA983E),
 *     5 = Ember Claw   (HQ 24, #DA3832), 6 = Dream Coil   (HQ 25, #862F8B).
 * - Faction `team_id` is pre-assigned on the server at registration while `sorting_hat`
 *   starts `false` (`menu.c:151` shows "Faction Unassigned" until a Sorting Hat Nut is
 *   redeemed, at which point `redeem_code.c:61` reads `badge_config.team_id` to show
 *   "Welcome to <Faction>").
 * - Physical Nut NFC format (`components/badge/ops/nfc.c`): NFC Forum Type-5 CC header
 *   (`E1 40 40 01`) + NDEF TLV (`0x03`) containing two Well-Known Text (`"T"`, `"en"`)
 *   records: the 5-character one-time code and `"NT:<NUT_TYPE>"` (`SORTING_HAT`,
 *   `TELECOM`, `COMMUNITY`, `CONTEST`, `EVENT`, `VENDOR`, `MISC`), after which the badge
 *   overwrites the tag with the `MUSEUM` (`MUSEUM` / `MUSEUM-DEMO`) Wi-Fi NDEF record (`nfc.c:157`).
 *
 * ============================================================================
 * INVENTED FOR SOLO MUSEUM PLAY (ORIGINAL SERVER CODE WAS NOT OPEN-SOURCED)
 * ============================================================================
 * - Cryptographic provisioning secrets (`hmac_secret`, `hash_prefix`, `ctf_*`,
 *   `wireless-psk`, `ctf-hints-password`) are deterministic dummy values; zero real
 *   SAINTCON 2025 conference keys, seeds, or Wi-Fi passwords exist here. Request
 *   `X-Badge-HMAC` headers are accepted without server-side key verification because
 *   all state is local to the visitor's browser.
 * - Solo Nut Dispenser (`writeNutToEeprom` / "Tap Nut" button): generates valid one-time
 *   5-character codes on demand and writes them into the 512-byte ST25DV EEPROM just like
 *   pressing a physical conference Nut onto the badge's NFC coil.
 * - Credit & Level progression rules: starting balance 100 credits, +50..150 credits per
 *   Nut depending on type (`SORTING_HAT` +100, `TELECOM` +75, `COMMUNITY` +100 & +1 level,
 *   `CONTEST` +150, `EVENT`/`VENDOR`/`MISC` +50).
 * - Solo Topology progression: two rival factions populate baseline links so the 31-node
 *   canvas looks like an active conference grid, and each `TELECOM` Nut redeemed by the
 *   visitor unlocks the next hop from their faction's HQ -> Ring 2 + Tower -> Ring 1 ->
 *   Core (`1`).
 * - Solo Credit Swap completion: polling `GET /api/badge/credit_swap_status/<code>`
 *   twice auto-marks a pending swap as redeemed by `"Museum Visitor"` and deducts the
 *   escrowed credits so the Credit Swap UI flow can be experienced solo.
 */
(function () {
  'use strict';

  const STORAGE_KEY = 'saintcon_2025_stock_server';
  const CODE_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

  const FACTIONS = [
    { team_id: 1, name: 'Aether Watch', color: '#00AAE9', hq_node: 20, r2_main: 19, r2_tower: 8,  tower_node: 26, r1_node: 2 },
    { team_id: 2, name: 'Verdant Pact', color: '#00A359', hq_node: 21, r2_main: 9,  r2_tower: 10, tower_node: 27, r1_node: 3 },
    { team_id: 3, name: 'Dawn Accord',  color: '#FFF34A', hq_node: 22, r2_main: 11, r2_tower: 12, tower_node: 28, r1_node: 4 },
    { team_id: 4, name: 'Iron Howl',    color: '#EA983E', hq_node: 23, r2_main: 13, r2_tower: 14, tower_node: 29, r1_node: 5 },
    { team_id: 5, name: 'Ember Claw',   color: '#DA3832', hq_node: 24, r2_main: 15, r2_tower: 16, tower_node: 30, r1_node: 6 },
    { team_id: 6, name: 'Dream Coil',   color: '#862F8B', hq_node: 25, r2_main: 17, r2_tower: 18, tower_node: 31, r1_node: 7 },
  ];

  // Exact 31-node port uplink table from components/badge_game/nodes.c (index = node_id 1..31)
  const NODE_UPLINKS = [
    [0, 0, 0, 0, 0, 0],         // 0 (unused)
    [2, 3, 4, 5, 6, 7],         // 1: CORE
    [1, 3, 7, 8, 9, 19],        // 2: RING1
    [1, 2, 4, 9, 10, 11],       // 3: RING1
    [1, 3, 5, 11, 12, 13],      // 4: RING1
    [1, 4, 6, 13, 14, 15],      // 5: RING1
    [1, 5, 7, 15, 16, 17],      // 6: RING1
    [1, 2, 6, 17, 18, 19],      // 7: RING1
    [2, 9, 19, 20, 21, 26],     // 8: RING2
    [2, 3, 8, 10, 21, 0],       // 9: RING2
    [3, 9, 11, 21, 22, 27],     // 10: RING2
    [3, 4, 10, 12, 22, 0],      // 11: RING2
    [4, 11, 13, 22, 23, 28],    // 12: RING2
    [4, 5, 12, 14, 23, 0],      // 13: RING2
    [5, 13, 15, 23, 24, 29],    // 14: RING2
    [5, 6, 14, 16, 24, 0],      // 15: RING2
    [6, 15, 17, 24, 25, 30],    // 16: RING2
    [6, 7, 16, 18, 25, 0],      // 17: RING2
    [7, 17, 19, 20, 25, 31],    // 18: RING2
    [2, 7, 8, 18, 20, 0],       // 19: RING2
    [8, 18, 19, 0, 0, 0],       // 20: HQ (Team 1 - Aether Watch)
    [8, 9, 10, 0, 0, 0],        // 21: HQ (Team 2 - Verdant Pact)
    [10, 11, 12, 0, 0, 0],      // 22: HQ (Team 3 - Dawn Accord)
    [12, 13, 14, 0, 0, 0],      // 23: HQ (Team 4 - Iron Howl)
    [14, 15, 16, 0, 0, 0],      // 24: HQ (Team 5 - Ember Claw)
    [16, 17, 18, 0, 0, 0],      // 25: HQ (Team 6 - Dream Coil)
    [8, 0, 0, 0, 0, 0],         // 26: TOWER
    [10, 0, 0, 0, 0, 0],        // 27: TOWER
    [12, 0, 0, 0, 0, 0],        // 28: TOWER
    [14, 0, 0, 0, 0, 0],        // 29: TOWER
    [16, 0, 0, 0, 0, 0],        // 30: TOWER
    [18, 0, 0, 0, 0, 0],        // 31: TOWER
  ];

  function defaultState() {
    const randTeam = Math.floor(Math.random() * 6) + 1;
    return {
      version: 1,
      registered: false,
      player_name: '',
      team_id: randTeam,
      credits: 100,
      level: 1,
      sorting_hat: false,
      konami_done: false,
      dawn_accord_reported: false,
      telecom_unlocks: 0,
      issued_nuts: {},
      redeemed_codes: [],
      swaps: {},
      read_messages: [],
      last_nut: null,
      tap_salt: 1,
    };
  }

  let state = null;
  const listeners = new Set();

  function isMockMode() {
    try {
      const p = new URLSearchParams(window.location.search);
      return p.get('mock') === '1';
    } catch (_) {
      return false;
    }
  }

  function loadState() {
    if (isMockMode()) {
      return defaultState();
    }
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === 'object') {
          return Object.assign(defaultState(), parsed);
        }
      }
    } catch (_) {}
    return defaultState();
  }

  function ensureState() {
    if (!state) {
      state = loadState();
    }
    return state;
  }

  function saveState() {
    ensureState();
    if (!isMockMode()) {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
      } catch (_) {
        if (typeof window.__onBadgeStorageError === 'function') {
          try { window.__onBadgeStorageError(); } catch (_) {}
        }
      }
    }
    for (const fn of listeners) {
      try { fn(getPublicState()); } catch (_) {}
    }
  }

  function getFaction(teamId) {
    return FACTIONS[(teamId >= 1 && teamId <= 6 ? teamId : 3) - 1];
  }

  function randomCode5() {
    let out = '';
    for (let i = 0; i < 5; i++) {
      out += CODE_ALPHABET[Math.floor(Math.random() * CODE_ALPHABET.length)];
    }
    return out;
  }

  function uniqueCode5() {
    for (let tries = 0; tries < 64; tries++) {
      const c = randomCode5();
      if (!state.issued_nuts[c] && !state.redeemed_codes.includes(c) && !state.swaps[c]) {
        return c;
      }
    }
    return randomCode5();
  }

  function findPortForPeer(nodeA, nodeB) {
    const row = NODE_UPLINKS[nodeA];
    if (!row) return 0;
    for (let p = 0; p < 6; p++) {
      if (row[p] === nodeB) return p + 1;
    }
    return 0;
  }

  function buildStatusData() {
    const f = getFaction(state.team_id);
    return {
      player_name: state.player_name,
      team_id: state.team_id,
      team_name: f.name,
      credits: state.credits,
      level: state.level,
      registered_at: '2025-10-21T09:00:00Z',
      sorting_hat: Boolean(state.sorting_hat),
      has_messages: state.read_messages.length < 1,
      firmware_info: {
        current_version: '0.2.1',
        latest_version: '0.2.1',
        update_available: false,
      },
    };
  }

  function buildTopologyResponse() {
    const nodeOwner = new Array(32).fill(-1);
    for (let t = 1; t <= 6; t++) {
      nodeOwner[19 + t] = t;
    }

    const portLinks = new Map();
    const setLink = (a, b, stA, stB) => {
      const pa = findPortForPeer(a, b);
      const pb = findPortForPeer(b, a);
      if (pa > 0) portLinks.set(`${a}:${pa}`, stA);
      if (pb > 0) portLinks.set(`${b}:${pb}`, stB);
    };

    const rival1 = state.team_id === 1 ? 2 : 1;
    const rival2 = state.team_id === 5 ? 4 : 5;
    const rf1 = getFaction(rival1);
    const rf2 = getFaction(rival2);

    nodeOwner[rf1.r2_tower] = rf1.team_id;
    nodeOwner[rf1.tower_node] = rf1.team_id;
    setLink(rf1.hq_node, rf1.r2_tower, 'FULL', 'FULL');
    setLink(rf1.r2_tower, rf1.tower_node, 'FULL', 'FULL');
    setLink(rf1.r2_tower, rf1.r1_node, 'PARTIAL', 'NONE');

    nodeOwner[rf2.r2_main] = rf2.team_id;
    nodeOwner[rf2.r1_node] = rf2.team_id;
    setLink(rf2.hq_node, rf2.r2_main, 'FULL', 'FULL');
    setLink(rf2.r2_main, rf2.r1_node, 'FULL', 'FULL');
    setLink(rf2.r1_node, 1, 'PARTIAL', 'NONE');

    const pf = getFaction(state.team_id);
    setLink(pf.hq_node, pf.r2_main, 'PARTIAL', 'NONE');

    if (state.telecom_unlocks >= 1) {
      nodeOwner[pf.r2_main] = pf.team_id;
      nodeOwner[pf.r2_tower] = pf.team_id;
      nodeOwner[pf.tower_node] = pf.team_id;
      setLink(pf.hq_node, pf.r2_main, 'FULL', 'FULL');
      setLink(pf.hq_node, pf.r2_tower, 'FULL', 'FULL');
      setLink(pf.r2_tower, pf.tower_node, 'FULL', 'FULL');
      setLink(pf.r2_main, pf.r1_node, 'PARTIAL', 'NONE');
    }
    if (state.telecom_unlocks >= 2) {
      nodeOwner[pf.r1_node] = pf.team_id;
      setLink(pf.r2_main, pf.r1_node, 'FULL', 'FULL');
      setLink(pf.r2_tower, pf.r1_node, 'FULL', 'FULL');
      setLink(pf.r1_node, 1, 'PARTIAL', 'NONE');
    }
    if (state.telecom_unlocks >= 3) {
      setLink(pf.r1_node, 1, 'FULL', 'FULL');
    }

    const nodes = [];
    for (let id = 1; id <= 31; id++) {
      let nodeType = 'RING2';
      let portCount = 6;
      if (id === 1) {
        nodeType = 'CORE';
      } else if (id >= 2 && id <= 7) {
        nodeType = 'RING1';
      } else if (id >= 20 && id <= 25) {
        nodeType = 'HQ';
        portCount = 3;
      } else if (id >= 26 && id <= 31) {
        nodeType = 'TOWER';
        portCount = 1;
      }
      const owner = nodeOwner[id];
      const col = owner > 0 ? getFaction(owner).color : '#88929E';
      nodes.push({
        id,
        node_type: nodeType,
        x_coord: 0.0,
        y_coord: 0.0,
        port_count: portCount,
        team_id: owner > 0 ? owner : null,
        color: col,
        is_online: true,
        halo_color: col,
        is_unlocked: 1,
      });
    }

    const connections = [];
    const port_status = [];
    let connId = 1;
    for (let a = 1; a <= 31; a++) {
      for (let p = 0; p < 6; p++) {
        const b = NODE_UPLINKS[a][p];
        if (!b) continue;
        const pa = p + 1;
        const link_status = portLinks.get(`${a}:${pa}`) || 'NONE';
        port_status.push({
          node_id: a,
          port_number: pa,
          connected: link_status === 'NONE' ? 0 : 1,
          link_status,
          updated_at: '2025-10-21T12:00:00Z',
        });
        if (a < b) {
          connections.push({
            id: connId++,
            node_a_id: a,
            node_a_port: pa,
            node_b_id: b,
            node_b_port: findPortForPeer(b, a),
            connection_type: 'WIRED',
          });
        }
      }
    }

    const hasCore = state.telecom_unlocks >= 3;
    const path_status = [{
      id: 1,
      team_id: state.team_id,
      has_path_to_core: hasCore ? 1 : 0,
      path_via_tower: state.telecom_unlocks >= 1 ? 1 : 0,
      path_nodes: `${pf.hq_node},${pf.r2_main},${pf.r1_node},1`,
      updated_at: '2025-10-21T12:00:00Z',
      is_scored_path: hasCore ? 1 : 0,
    }];
    const team_path_scores = hasCore ? [{
      team_id: state.team_id,
      first_node: pf.hq_node,
      last_node: 1,
      created_at: '2025-10-21T12:05:00Z',
    }] : [];

    return {
      status: 'ok',
      timestamp: new Date().toISOString(),
      nodes,
      connections,
      port_status,
      path_status,
      team_path_scores,
    };
  }

  function extractPath(url) {
    try {
      const u = new URL(url, 'https://badge.museum.local');
      return u.pathname;
    } catch (_) {
      const q = String(url || '/').split('?')[0];
      return q.startsWith('/') ? q : '/' + q;
    }
  }

  function jsonReply(status, obj, extraHeaders) {
    return {
      status,
      headers: Object.assign({ 'Content-Type': 'application/json' }, extraHeaders || {}),
      body: JSON.stringify(obj),
    };
  }

  function handleRequest(methodRaw, urlRaw, _headersStr, bodyStr) {
    ensureState();
    const method = String(methodRaw || 'GET').toUpperCase();
    const path = extractPath(urlRaw);
    let req = {};
    if (bodyStr && typeof bodyStr === 'string') {
      try { req = JSON.parse(bodyStr); } catch (_) { req = {}; }
    }

    if (method === 'HEAD' && path === '/api/firmware/badge') {
      return {
        status: 200,
        headers: {
          'Content-Type': 'application/octet-stream',
          'Content-Length': '1048576',
          'X-Firmware-Version': '0.2.1',
          'X-Device-Type': 'badge',
          'Last-Modified': 'Tue, 11 Nov 2025 00:00:00 GMT',
        },
        body: '',
      };
    }

    if (path === '/api/firmware/version') {
      return jsonReply(200, {
        status: 'ok',
        data: { device_type: 'badge', setting: 'firmware_version_badge', value: '0.2.1' },
      });
    }

    if (path === '/api/badge/challenge') {
      return jsonReply(200, {
        status: 'ok',
        data: { nonce: '0123456789abcdef0123456789abcdef' },
      });
    }

    if (path === '/api/badge/provision') {
      return jsonReply(200, {
        status: 'ok',
        data: {
          hmac_secret: '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f',
          hash_prefix: 'a0a1a2a3a4a5a6a7a8a9aaabacad0e0f',
          ctf_hmac_secret: '202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f',
          ctf_hash_prefix: 'b0b1b2b3b4b5b6b7b8b9babbbcbdbebf',
        },
      });
    }

    if (path === '/api/badge/provision/verify') {
      return jsonReply(200, { status: 'ok', data: {} });
    }

    if (path === '/api/badge/provision/wireless-psk') {
      return jsonReply(200, { status: 'ok', data: { psk: 'museum-fake-psk' } });
    }

    if (path === '/api/badge/register') {
      const pname = typeof req.player_name === 'string' ? req.player_name.trim() : '';
      if (!pname) {
        return jsonReply(400, { status: 'error', detail: 'Player name is required' });
      }
      state.player_name = pname.slice(0, 32);
      state.registered = true;
      saveState();
      return jsonReply(200, { status: 'ok', data: buildStatusData() });
    }

    if (path === '/api/badge/status') {
      if (!state.registered || !state.player_name) {
        return jsonReply(404, { status: 'error', detail: 'Badge not registered' });
      }
      return jsonReply(200, { status: 'ok', data: buildStatusData() });
    }

    if (path === '/api/badge/redeem_code') {
      const code = String(req.code || '').trim().toUpperCase();
      if (code.length !== 5 || ![...code].every((ch) => CODE_ALPHABET.includes(ch))) {
        return jsonReply(400, { status: 'error', detail: 'Invalid 5-character code.' });
      }
      if (state.redeemed_codes.includes(code)) {
        return jsonReply(400, { status: 'error', detail: 'Code already redeemed.' });
      }

      // Check if visitor entered a pending credit swap code
      if (state.swaps[code] && !state.swaps[code].cancelled && !state.swaps[code].redeemed) {
        const sw = state.swaps[code];
        sw.redeemed = true;
        state.redeemed_codes.push(code);
        saveState();
        return jsonReply(200, { status: 'ok', data: { credits_awarded: sw.credits } });
      }

      state.redeemed_codes.push(code);
      const entry = state.issued_nuts[code];
      const ntype = entry ? entry.type : 'MISC';
      if (entry) {
        entry.redeemed = true;
        if (entry.faction_override >= 1 && entry.faction_override <= 6) {
          state.team_id = entry.faction_override;
        }
      }

      let awarded = 50;
      if (ntype === 'SORTING_HAT') {
        state.sorting_hat = true;
        awarded = 100;
      } else if (ntype === 'TELECOM') {
        state.telecom_unlocks = (state.telecom_unlocks || 0) + 1;
        awarded = 75;
      } else if (ntype === 'COMMUNITY') {
        state.level = (state.level || 1) + 1;
        awarded = 100;
      } else if (ntype === 'CONTEST') {
        awarded = 150;
      }

      state.credits = (state.credits || 0) + awarded;
      const computedLevel = Math.floor(state.credits / 250) + 1;
      if (computedLevel > state.level) state.level = computedLevel;
      saveState();

      return jsonReply(200, {
        status: 'ok',
        data: { credits_awarded: awarded, nut_type: ntype },
      });
    }

    if (path === '/topology/data') {
      return jsonReply(200, buildTopologyResponse());
    }

    if (path === '/api/badge/files/list') {
      return jsonReply(200, { status: 'ok', data: { files: [] } });
    }

    if (path.startsWith('/api/badge/files/download/')) {
      return jsonReply(404, { status: 'error', detail: 'No extra files queued' });
    }

    if (path === '/api/badge/messages') {
      return jsonReply(200, {
        status: 'ok',
        data: {
          badge_id: '025C25000001',
          timestamp: '2025-10-21T12:00:00Z',
          messages: [{
            id: 1,
            title: 'Welcome to SAINTCON 2025',
            content: 'Use Tap Nut (NFC) to consult the Sorting Hat and unlock Telecom Nodes!',
            priority: 1,
            created_at: '2025-10-21T09:00:00Z',
          }],
        },
      });
    }

    if (path === '/api/badge/messages/read') {
      const mid = Number(req.message_id) || 1;
      if (!state.read_messages.includes(mid)) state.read_messages.push(mid);
      saveState();
      return jsonReply(200, {
        status: 'ok',
        data: {
          badge_id: '025C25000001',
          message_id: mid,
          success: true,
          timestamp: '2025-10-21T12:00:00Z',
        },
      });
    }

    if (path === '/api/badge/konami') {
      const first = !state.konami_done;
      state.konami_done = true;
      saveState();
      return jsonReply(200, { status: 'ok', data: { first_time: first } });
    }

    if (path === '/api/badge/dawn-accord') {
      state.dawn_accord_reported = true;
      saveState();
      return jsonReply(200, { status: 'ok', data: {} });
    }

    if (path === '/api/badge/achievements') {
      return jsonReply(200, {
        status: 'ok',
        data: [
          {
            id: 1,
            short_name: 'Registered',
            description: 'Register your badge handle',
            is_public: true,
            status: state.registered ? 'COMPLETED' : 'NOT_STARTED',
            completed_at: state.registered ? '2025-10-21T09:05:00Z' : null,
          },
          {
            id: 2,
            short_name: 'Sorted',
            description: 'Consult the Sorting Hat Nut',
            is_public: true,
            status: state.sorting_hat ? 'COMPLETED' : 'NOT_STARTED',
            completed_at: state.sorting_hat ? '2025-10-21T09:10:00Z' : null,
          },
          {
            id: 3,
            short_name: 'Telecom Operator',
            description: 'Unlock a Telecom Node',
            is_public: true,
            status: state.telecom_unlocks > 0 ? 'COMPLETED' : 'IN_PROGRESS',
            completed_at: state.telecom_unlocks > 0 ? '2025-10-21T10:00:00Z' : null,
          },
          {
            id: 4,
            short_name: 'Core Uplink',
            description: 'Complete a faction path to Core Node 1',
            is_public: true,
            status: state.telecom_unlocks >= 3 ? 'COMPLETED' : 'IN_PROGRESS',
            completed_at: state.telecom_unlocks >= 3 ? '2025-10-21T12:05:00Z' : null,
          },
        ],
      });
    }

    if (path === '/api/badge/ctf-hints-password') {
      return jsonReply(200, { status: 'ok', data: { password: 'MUSEUM-DEMO-HINTS' } });
    }

    if (path === '/api/badge/request_credit_swap') {
      const amt = Number(req.credit_amount) || 0;
      if (amt <= 0 || amt > state.credits) {
        return jsonReply(400, { status: 'error', detail: 'Insufficient credits for swap.' });
      }
      const code = uniqueCode5();
      state.swaps[code] = {
        code,
        credits: amt,
        polls: 0,
        redeemed: false,
        cancelled: false,
      };
      saveState();
      return jsonReply(200, {
        status: 'ok',
        data: {
          code,
          credits: amt,
          expires_at: '2025-10-24T23:59:59Z',
          status: 'pending',
        },
      });
    }

    if (path.startsWith('/api/badge/credit_swap_status/')) {
      const code = path.slice('/api/badge/credit_swap_status/'.length).toUpperCase();
      const sw = state.swaps[code];
      if (!sw) {
        return jsonReply(404, { status: 'error', detail: 'Credit swap code not found.' });
      }
      sw.polls = (sw.polls || 0) + 1;
      if (!sw.cancelled && !sw.redeemed && sw.polls >= 2) {
        sw.redeemed = true;
        state.credits = Math.max(0, state.credits - sw.credits);
        saveState();
      }
      const st = sw.cancelled ? 'cancelled' : (sw.redeemed ? 'redeemed' : 'pending');
      return jsonReply(200, {
        status: 'ok',
        data: {
          code,
          credits: sw.credits,
          status: st,
          redeemed: Boolean(sw.redeemed),
          redeemed_at: sw.redeemed ? '2025-10-21T12:00:00Z' : '',
          redeemed_by: sw.redeemed ? 'Museum Visitor' : '',
          expires_at: '2025-10-24T23:59:59Z',
          issued_at: '2025-10-21T11:55:00Z',
        },
      });
    }

    if (path.startsWith('/api/badge/cancel_credit_swap/')) {
      const code = path.slice('/api/badge/cancel_credit_swap/'.length).toUpperCase();
      if (state.swaps[code]) {
        state.swaps[code].cancelled = true;
        saveState();
      }
      return jsonReply(200, { status: 'ok', data: {} });
    }

    return jsonReply(404, { status: 'error', detail: 'Unknown endpoint: ' + path });
  }

  function makeNdefTextRecord(text, isFirst, isLast) {
    const enc = new TextEncoder();
    const langBytes = enc.encode('en');
    const textBytes = enc.encode(text);
    const payloadLen = 1 + langBytes.length + textBytes.length;
    const hdr = (isFirst ? 0x80 : 0x00) | (isLast ? 0x40 : 0x00) | 0x10 | 0x01; // SR | TNF_WELL_KNOWN
    const out = new Uint8Array(4 + payloadLen);
    out[0] = hdr;
    out[1] = 1; // 'T'
    out[2] = payloadLen;
    out[3] = 0x54; // 'T'
    out[4] = langBytes.length & 0x3f;
    out.set(langBytes, 5);
    out.set(textBytes, 5 + langBytes.length);
    return out;
  }

  function dispenseNut(nutTypeRaw, factionOverride) {
    ensureState();
    const nutType = String(nutTypeRaw || 'TELECOM').toUpperCase();
    const code = uniqueCode5();
    const fOverride = Number(factionOverride) || 0;
    state.issued_nuts[code] = {
      type: nutType,
      faction_override: fOverride >= 1 && fOverride <= 6 ? fOverride : 0,
      redeemed: false,
    };
    if (nutType === 'SORTING_HAT' && fOverride >= 1 && fOverride <= 6) {
      state.team_id = fOverride;
    }
    state.last_nut = { code, type: nutType };
    saveState();
    return { code, type: nutType };
  }

  function writeNutToEeprom(eepromU8, nutTypeRaw, factionOverride) {
    if (!eepromU8 || eepromU8.length < 128) return null;
    const nut = dispenseNut(nutTypeRaw, factionOverride);
    const rec1 = makeNdefTextRecord(nut.code, true, false);
    const rec2 = makeNdefTextRecord('NT:' + nut.type, false, true);
    const msgLen = rec1.length + rec2.length;

    eepromU8.fill(0);
    // NFC Forum Type-5 4-byte Capability Container for 512-byte ST25DV04K
    eepromU8[0] = 0xe1;
    eepromU8[1] = 0x40;
    eepromU8[2] = 0x40;
    eepromU8[3] = 0x05;
    // NDEF Message TLV (0x03)
    eepromU8[4] = 0x03;
    eepromU8[5] = msgLen;
    eepromU8.set(rec1, 6);
    eepromU8.set(rec2, 6 + rec1.length);
    eepromU8[6 + msgLen] = 0xfe; // Terminator TLV

    // Monotonic salt byte at end of 512-byte EEPROM guarantees external-write detection
    state.tap_salt = ((state.tap_salt || 1) + 1) & 0xff || 1;
    eepromU8[eepromU8.length - 1] = state.tap_salt;
    saveState();
    return nut;
  }

  function getPublicState() {
    ensureState();
    const f = getFaction(state.team_id);
    return {
      registered: state.registered,
      player_name: state.player_name,
      team_id: state.team_id,
      team_name: f.name,
      team_color: f.color,
      credits: state.credits,
      level: state.level,
      sorting_hat: state.sorting_hat,
      telecom_unlocks: state.telecom_unlocks,
      last_nut: state.last_nut,
    };
  }

  function wipe() {
    if (isMockMode()) return false;
    try {
      localStorage.removeItem(STORAGE_KEY);
      localStorage.removeItem('saintcon_2025_stock_nvs');
      localStorage.removeItem('saintcon_2025_stock_se');
    } catch (_) {
      if (typeof window.__onBadgeStorageError === 'function') {
        try { window.__onBadgeStorageError(); } catch (_) {}
      }
      return false;
    }
    state = defaultState();
    for (const fn of listeners) {
      try { fn(getPublicState()); } catch (_) {}
    }
    return true;
  }

  window.__sc25FakeServer = {
    FACTIONS,
    handleRequest,
    dispenseNut,
    writeNutToEeprom,
    getState: getPublicState,
    onStateChange(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    wipe,
  };
})();
