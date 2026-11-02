# xBot

xBot is a desktop dice and limbo betting bot for **Windows**, written in C++ with Qt 6. It can bet on nine casinos: **Stake**, **Shuffle**, **Goated**, **Wolfbet**, **Duel**, **Paradice**, **Pasino**, **Tower.bet** and **Winna**. You can bet by hand, run auto-bet with on-win and on-loss rules, build rules in the **Condition Builder**, or write your own strategy in **Lua** or **JavaScript**. The layout is modelled on qBittorrent.

Every new launch starts in **simulation mode**, which uses play money and a provably fair seed generator that works like Stake's. Real-money betting only starts after you turn it on yourself in *Tools → Options → Live betting*.

Source code and exe: <https://bitbucket.org/poky1084/xbot>
<b>Donate1:</b> https://cwallet.com/t/87SPDPGG <br />
<b>Donate2:</b> https://ko-fi.com/poky <br />

<b>Discord</b>: fisk_992 <br />
<b>Telegram:</b>: https://t.me/poky_1084

> ⚠️ Gambling with real money carries real risk. No strategy can beat the house edge. Only use money you can afford to lose, and follow the terms of service and the laws that apply where you live.

---

## Table of contents

1. [Requirements (Windows only, WebView2)](#requirements)
2. [Supported casinos](#supported-casinos)
3. [Installing and first start](#installing-and-first-start)
4. [Main window overview](#main-window-overview)
5. [Options dialog](#options-dialog)
6. [Logging in to each casino (button click = auto-fill)](#logging-in-to-each-casino)
7. [Browser extension mode (auto-filled login)](#browser-extension-mode)
8. [Manual bet](#manual-bet)
9. [Auto bet](#auto-bet)
10. [Condition Builder](#condition-builder)
    - [Importing Stake strategies (export-strategy extension)](#importing-stake-strategies-export-strategy-extension)
11. [Code mode — Lua and JavaScript strategies](#code-mode--lua-and-javascript-strategies)
12. [Provably fair / simulation](#provably-fair--simulation)
13. [Multiple windows](#multiple-windows)
14. [Where data is stored / clearing data](#where-data-is-stored)
15. [Keyboard shortcuts](#keyboard-shortcuts)
16. [Building from source](#building-from-source)
17. [Troubleshooting](#troubleshooting)

---

## Requirements

| Requirement | Why |
|---|---|
| **Windows 10 / 11 (64-bit)** | xBot only runs on Windows. It uses the Windows registry for settings and Win32 APIs, and its embedded browser is Microsoft Edge WebView2. |
| **Microsoft Edge WebView2 Runtime** | Used for the "Open … & Log in" windows (all casinos except Stake), Stake's hidden browser (Cookie mode) and Duel's hidden browser (Direct mode). It is already installed on Windows 11 and on most Windows 10 PCs. If it is missing, download the *Evergreen Runtime* from Microsoft: <https://developer.microsoft.com/microsoft-edge/webview2/> |
| **A Chromium browser (Chrome, Edge, Brave…)** *(optional)* | Only needed for [Browser extension mode](#browser-extension-mode). |

If the WebView2 Runtime is missing, the login buttons are disabled, and Stake (Cookie mode) and Duel (Direct mode) report *"Is the WebView2 Runtime installed?"*. In that case, use the Browser extension mode for those casinos.

---

## Supported casinos

| Casino | Games | Login methods | Login data | Vault `vault()` | Site / mirror |
|---|---|---|---|---|---|
| **Stake.com** (and Primedice) | Dice, Limbo | *Cookie (cf_clearance)* → hidden WebView2 browser, or *Browser extension* | API key (`x-access-token`), optional `cf_clearance` | ✅ | Editable (`stake.com`, `stake.bet`, `stake.us`, `stake.games`, `stake.pet`, `stake.kim`, `stake.mba`, `stake.ac`, `stake.jp`, `stake.bz`, `stake.krd`, `stake.ceo`, `staketr.com`, `stake1021.com`, `stake1067.com`, `stake3075.com`, `primedice.com`, `primedice.games`, …) |
| **Shuffle.com** | Dice, Limbo | *Direct mode* (HTTPS + WebSocket from the app), or *Browser extension* | Token, `CORRELATION_USER_ID`, `BROWSER_ID` | ✅ | Editable (`shuffle.com` or a mirror) |
| **Goated.com** | Dice, Limbo | *Direct mode* (HTTPS), or *Browser extension* | Token (`g3_token`) | ✅ | Fixed (`goated.com`) |
| **Wolfbet.com** | Dice, Limbo | *Direct mode* (HTTPS), or *Browser extension* | Token (the `token` cookie) | ❌ | Fixed (`wolfbet.com`) |
| **Duel.com** | Dice, Limbo | *Direct mode* (hidden WebView2 page), or *Browser extension* | Token (`duel` cookie), UUID (`security:uuid`), optional `cf_clearance` | ❌ | Editable (`duel.com` or a mirror) |
| **Paradice.in** | Dice | *Direct mode* (HTTPS GraphQL), or *Browser extension* | Token (localStorage `token`, a JWT) | ❌ | Fixed (`paradice.in`) |
| **Pasino.com** | Dice, Limbo | *Direct mode* (HTTPS), or *Browser extension* | Token (localStorage `token`, 64 hex characters) | ❌ | Fixed (`pasino.com`) |
| **Tower.bet** | Dice, Limbo | *Direct mode* (HTTPS behind Cloudflare), or *Browser extension* | Token (`__tt` cookie), `cf_clearance`, User-Agent | ❌ | Fixed (`tower.bet`) |
| **Winna.com** | Dice, Limbo | *Direct mode* (HTTPS behind Cloudflare), or *Browser extension* | Session (`connect.sid` + the other winna.com cookies), `cf_clearance`, User-Agent, Device ID (`wn-antifraud-device-id`) | ✅ | Fixed (`winna.com`) |

**House edge:** the app's win chance and multiplier assume a 1 % house edge (multiplier = 99 / chance). Stake, Shuffle, Goated, Wolfbet, Paradice and Winna dice use the same edge. **Duel (0.1 %)**, **Tower (2 %)** and **Pasino (5 %)** dice use a different one, so xBot converts the win chance it sends so that the multiplier you set is the one paid: 2x is sent as 49.95 % on Duel, 49.000 % on Tower (over 51.000) and 47.50 % on Pasino. The bet list still shows the app's target. Limbo sends the multiplier itself, so it is exact everywhere.

**Seeds:** Stake, Shuffle, Goated, Duel and **Winna** set the client seed you choose. Pasino sends the client seed with every bet, so setting it only changes the seed used from then on. Wolfbet and Paradice can only renew the server seed, so xBot bets on with the current client seed. Tower has no seed support in xBot yet.

**Balance after a bet:** Tower (`newBalance`) and Pasino (`balance`) report the balance after every bet, and xBot sets the session balance to that figure.

**Switching casino:** use the **Casino** menu, the *Casino* selector in Options, or set `casino = "..."` from a script. Every casino keeps its own login data and its own bet currency. While you bet on one casino, xBot keeps reading the balances of the others in the background (Shuffle over its WebSocket, the rest over HTTPS, the extension or the hidden browser), so switching shows the right balance straight away. Switching during an auto-bet keeps the session going: the next bet waits until the new casino's connection is ready.

**Currencies:** each casino has a *Bet currency* dropdown that you can also type into.
- **Shuffle:** fixed list (btc, eth, usdt, usdc, shfl, sol, ltc, xrp, trx, doge, pol, avax, bnb, gram (sent as TON), bonk, shib, wif, pump, trump, dai, cash).
- **Goated:** public exchange-rate list.
- **Wolfbet:** every currency on the account (needs the token).
- **Stake:** the currencies the account holds, collected after login.
- **Duel:** the site's wallet list plus the account's currencies.
- **Paradice:** every wallet on the account (needs the token).
- **Pasino:** every coin on the account (needs the token), e.g. GEM, BTC, TRX.
- **Tower:** the site's currency list (`/api/currencies`, needs the token). Only currencies the account holds appear in its balances; any other is 0.
- **Winna:** every coin in the account's wallets. Each coin is listed twice (main wallet and vault); the main wallet is the betting balance.

Lists are cached, so the dropdown fills straight away next time. A currency the account doesn't hold has a balance of 0.

---

## Installing and first start

1. Install the **WebView2 Runtime** if your PC doesn't already have it (see [Requirements](#requirements)).
2. Download or build `xBot.exe` and keep it in its own folder. Condition Builder strategies (`strategies.json`) are saved next to the exe.
3. Run `xBot.exe`. It opens in **simulation** mode with a play-money balance (1000 USDT by default).
4. To bet for real, open **Tools → Options** (or the gear button on the toolbar), pick a casino, log in (see below), tick **Enable live betting** and press **OK**.

---

## Main window overview

| Area | Contents |
|---|---|
| **Menu bar** | **File** (Exit) · **Casino** (Stake / Shuffle / Goated / Wolfbet / Duel / Paradice / Pasino / Tower / Winna) · **Game** (Dice / Limbo) · **View** (Show sidebar, Show bet list, Show bottom panel, Show log, Strategy editor, Dark theme) · **Bot** (Start / Pause / Stop auto-bet, Roll once, Reset session, Clear history, Rotate seed) · **Tools** (Statistics, Options, Condition builder) · **Help** (About) |
| **Toolbar** | New window (+), Reset session, Start, Pause, Stop, Rotate seed, Statistics, Options, Condition builder, Strategy editor (#), and a filter box for the bet list (*Filter by nonce or roll…*) |
| **Status / Session sidebar** (dockable) | **Status:** mode (Simulation / Live), casino, connection state. **Session:** Balance, Profit, Wager, Wins, Losses, Win rate, Big wins (10x+), Highest bet, highest and lowest profit, Highest / Lowest / Current streak, Running time |
| **Bet list** | Time, Bet ID, Casino, Game, Nonce, Amount, Target, Roll, Multiplier, Payout, Profit. Right-click a live bet to **copy its Bet ID** (on Stake also the IID) or **open the bet in the browser** (not available for Duel, Paradice, Pasino, Tower and Winna bets, which only offer copying the Bet ID). The list follows new bets until you scroll, and follows again when you scroll back to the top. |
| **Bottom panel tabs** | **Manual Bet** · **Auto Bet** · **Profit Chart** (running profit graph) · **Provably Fair** |
| **Log** (dockable, hidden by default) | Time / Level / Message, with Copy and Clear log |
| **Strategy editor** (dockable, `#` button) | The Lua / JavaScript editor with Run, Stop, Open, Save, Save as, Help, and a Console tab |
| **Status bar** | Bets, P/L, Wagered, betting speed |

Every dock and splitter can be moved, and the layout is saved per window. **Dark theme** is available in View or in Options → Appearance.

---

## Options dialog

Open it from **Tools → Options**. The window has two columns and can be resized.

### Live betting box

| Field | Meaning |
|---|---|
| **Enable live betting** | Off = simulation (play money). This is never saved, so every launch starts in simulation. |
| **Casino** | Which casino this box shows and bets on. It is the same setting as the Casino menu. Values typed for one casino are kept when you switch to another. |
| **Login method** | Stake: **Cookie (cf_clearance)** or **Browser extension**. All other casinos: **Direct mode** or **Browser extension**. |
| **Site / mirror** | Domain to use (Stake, Shuffle, Duel). It is saved as you type. The other casinos use a fixed site. |
| **API key / Token / Session** | Stake: API key (`x-access-token`). Shuffle: auth token (JWT). Goated: `g3_token` (JWT). Wolfbet: `token` cookie value. Duel: `duel` cookie value. Paradice: localStorage `token` (JWT). Pasino: localStorage `token`. Tower: `__tt` cookie value. Winna (**Session**): the `connect.sid` cookie, or the whole winna.com cookie list (`connect.sid=…; chat.jwt=…; …`) as the login window captures it. |
| **USER_ID / BROWSER_ID** | Shuffle only: `CORRELATION_USER_ID` and `BROWSER_ID` from localStorage. |
| **UUID / cf_clearance** | Duel only: `security:uuid` from localStorage, plus the optional `cf_clearance` cookie. |
| **cf_clearance** | Stake (optional). Tower and Winna have their own **cf_clearance** and **User-Agent** rows: Cloudflare only accepts `cf_clearance` together with the User-Agent of the browser that got it. Not needed in Browser extension mode. |
| **Device ID** | Winna only: the `id` inside localStorage `wn-antifraud-device-id` (sent as `x-antifraud-device-id`). The whole JSON value can be pasted. Winna ties the session to this id, so keep the session and the device id from the same login. |
| **Open … & Log in** buttons | Shuffle, Goated, Wolfbet, Duel, Paradice, Pasino, Tower, Winna: open the site in a WebView2 window and **fill in the fields automatically** after you log in (see the next section). Clicking a button first **empties the login fields**, so they are filled with fresh data, and the captured login is **saved straight away** (even if you close Options without OK). |
| **Extension** | Status of the browser extension server: *server off*, *waiting for a … tab*, or *connected (… tab)*. |
| **Bet currency** | Currency to bet in, remembered separately for each casino. Picking or typing a currency fetches its balance. |
| **Log in** | Checks the login and loads the balances and currencies. The status text next to it shows the result, e.g. *Logged in — 0.0123 BTC available*. |

Every secret field has an **eye** button to show or hide its text.

**Auto fetch:** once every required field for the selected casino is filled in (typed, pasted or auto-filled), the dialog logs in **by itself** after a short pause and loads the currencies, so you don't have to press *Log in*. This is skipped for Shuffle (its currency list is built in) and while a bet is running.

### Game box

| Field | Meaning |
|---|---|
| **Simulation balance** | Play-money balance granted when the session is reset. |
| **Client seed** | Client seed for manual and auto bets. xBot sets it by rotating the seed pair before an auto-bet starts, or before the first manual bet after you change it. Empty = keep the current one. Lua / JavaScript strategies don't use it; use `resetseed()` instead. |
| **Random client seed (10 characters)** | Uses a fresh random alphanumeric seed each time instead of the typed one. |
| **Delays between bet** | Pause between bets in ms (0 = as fast as possible). Applies to auto-bet, the Condition Builder and scripts. |
| **Retry bet when bet higher than balance** | Live: if the next bet is larger than the balance, keep the run alive and check again every 5 s (for example until a deposit arrives) instead of stopping. |

### Appearance box
- **Theme:** Light / Dark.

### Data box
- **Clear data from PC:** deletes everything xBot saved (the registry key `HKEY_CURRENT_USER\Software\xBot`): settings, layout, API keys and tokens, and the Lua and JavaScript scripts. xBot then closes.

---

## Logging in to each casino

For every casino except Stake, the easiest way to log in is the **"Open … & Log in"** button in Options. It opens the casino in an embedded Edge (WebView2) window. **You log in there as usual** (including 2FA and captcha). When the site has stored its session, xBot reads the values it needs, **fills the inputs**, and closes the window. Then the auto fetch loads your currencies and balance.

### Step by step: button click → auto-filled inputs

1. Open **Tools → Options**.
2. Choose a **Casino**.
3. Leave **Login method** on *Direct mode*.
4. Click **Open <Casino> & Log in**.
5. Log in on the page that opens. The window closes by itself after login.
6. The fields are now filled in, and the status shows *"… token captured"*. The currencies load automatically, or you can press **Log in**.
7. Pick a **Bet currency**, tick **Enable live betting**, and press **OK**.

| Casino | Button | What gets filled in automatically |
|---|---|---|
| **Shuffle** | *Open Shuffle & Log in - to autofill data* | **Token** ← `JSON.parse(JSON.parse(localStorage["persist:root"]).auth).accessToken` · **USER_ID** ← `localStorage["CORRELATION_USER_ID"]` · **BROWSER_ID** ← `localStorage["BROWSER_ID"]` |
| **Goated** | *Open Goated & Log in - to autofill token* | **Token** ← `localStorage["g3_token"]` |
| **Wolfbet** | *Open Wolfbet & Log in - to autofill token* | **Token** ← the `value` field of the URL-encoded JSON in the `token` cookie |
| **Duel** | *Open Duel.com & Log in - to autofill data* | **Token** ← the `duel` cookie (httpOnly, read through the WebView2 cookie manager) · **UUID** ← `localStorage["security:uuid"]` · **cf_clearance** ← `cf_clearance` cookie (if Cloudflare set one) · plus the browser's User-Agent (saved in the background, because `cf_clearance` only works with the same User-Agent). *Use captured data* takes whatever has been found so far. |
| **Paradice** | *Open Paradice & Log in - to autofill token* | **Token** ← `localStorage["token"]` (JWT) |
| **Pasino** | *Open Pasino & Log in - to autofill token* | **Token** ← `localStorage["token"]` (64 hex characters) |
| **Tower** | *Open Tower & Log in - to autofill token* | **Token** ← the `__tt` cookie · **cf_clearance** ← `cf_clearance` cookie (httpOnly) · **User-Agent** ← the browser's User-Agent. The window closes once `/api/accounts` accepts the token. |
| **Winna** | *Open Winna & Log in - to autofill data* | **Session** ← every winna.com cookie, `connect.sid` first (httpOnly ones too) · **cf_clearance** ← `cf_clearance` cookie · **Device ID** ← `localStorage["wn-antifraud-device-id"].id` · **User-Agent** ← the browser's User-Agent. The window **first deletes winna.com's old cookies and site data** from its browser, so you always log in fresh (old stale `connect.sid` cookies caused 401 errors). The window closes once the wallets request works. |
| **Stake** | — (no login button) | Type the **Site / mirror** and the **API key**. Create the API key on Stake under *Settings → API*. In Cookie mode, requests go through a hidden WebView2 browser on the Stake site. Or use the [Browser extension mode](#browser-extension-mode), which reads everything from your Stake tab. |

The login windows share one WebView2 profile with the hidden browsers, so after you log in once, the Duel hidden browser and the Stake hidden browser are logged in too. Logins persist between launches.

### Typing the values manually

You can paste every value yourself. Open the casino in your browser, then DevTools (F12):

- **localStorage values:** *Application → Local Storage*, or the Console: `localStorage.getItem('g3_token')`.
- **Cookies:** *Application → Cookies* (httpOnly cookies such as `duel`, Stake's `session`, Winna's `connect.sid` or `cf_clearance` only appear there).
- **A whole cookie header** (Winna): copy the `cookie:` request header of an `api2.winna.com` request from the *Network* tab and paste it into **Session**.

---

## Browser extension mode

In **Browser extension** mode, xBot doesn't talk to the casino directly. All requests (bets, balances, seed rotation, vault) are sent through **your own logged-in casino tab in your normal browser**. That means the browser's real cookies, Cloudflare session, TLS fingerprint and User-Agent are used, which helps when the site's Cloudflare blocks the app's direct connection.

**In extension mode the login inputs are filled in automatically.** You don't need to type a token: xBot reads it from the tab.

### How it works

```
 ┌──────────────┐   ws://127.0.0.1:4444   ┌──────────────────────────┐   fetch() with the page's   ┌──────────┐
 │    xBot      │ ◄─────────────────────► │ Extension content script │ ──── cookies / session ───► │  Casino  │
 │ (WS server)  │   JSON request/answer   │ in the casino tab        │ ◄────────────────────────── │   API    │
 └──────────────┘                         └──────────────────────────┘                             └──────────┘
```

1. When a casino uses *Browser extension* as its login method, xBot starts a small **WebSocket server on `127.0.0.1:4444`**. It is bound to localhost only and is never reachable from the LAN.
2. The extension runs in every tab of a supported casino (Stake and its mirrors, Primedice, Shuffle, Goated, Wolfbet, Duel, Paradice, Pasino, Tower, Winna, plus any mirror typed in *Site / mirror*). Each tab connects to the server and says which site it is on: `{"type":"hello","host":"duel.com","version":"…"}`.
3. **Security:** the server only accepts connections whose `Origin` is a browser extension (`chrome-extension://`, `moz-extension://`) or one of the casino sites. This stops other web pages from connecting to localhost and impersonating the extension.
4. For each request, xBot sends `{"id", "payload": {"raw": true, "url", "options": {method, headers, body}, "timeoutMs"}}` to the tab of the right casino. The tab runs `fetch()` inside the page and answers `{"id", "status", "body"}`.
5. **Auto-filled login:** xBot asks the tab for its localStorage values and cookies, including httpOnly ones (`{"op": "storage", "key": …}` and the cookie read). It reads:
   - **Stake:** `session` cookie → API key, plus `cf_clearance`
   - **Shuffle:** `persist:root` → accessToken, `CORRELATION_USER_ID`, `BROWSER_ID`
   - **Goated:** `g3_token`
   - **Wolfbet:** `token` cookie
   - **Duel:** `security:uuid`, `duel` cookie, `cf_clearance`
   - **Paradice:** `token` (localStorage)
   - **Pasino:** `token` (localStorage)
   - **Tower:** `__tt` cookie
   - **Winna:** `wn-antifraud-device-id` (localStorage) and the `connect.sid` cookie. Requests to `api2.winna.com` are sent with the tab's cookies (`credentials: "include"`).

   These values appear in the Options fields and are saved when you press **OK**.
6. **One tab per site** is used. A second tab of the same site is put on standby (close code 4001) and takes over when the first one closes.
7. The login is read again whenever the tab connects, and every 5 s while you're not logged in yet. So if you log in in the browser after starting xBot, it picks that up by itself.

**Extension version:** reading localStorage needs extension **2.0.1+**, and reading cookies (needed for Stake, Wolfbet and Duel) needs **2.1.0+**. **2.2.0+** also reads cookies the page itself can see (needed for Tower's `__tt`), and **2.3.0+** adds winna.com. Paradice, Pasino and Tower need their sites in the extension's `host_permissions` and `targetSites` list (included from 2.2.0). Shuffle's balance WebSocket stays in the app even in extension mode.

**Winna in extension mode:** balances and the account requests work through the tab. Dice and limbo bets go to `originals.winna.com`, which only expects requests from Winna's own game page; if extension-mode bets fail with *Failed to fetch*, use Direct mode with the login button.

### Installing the extension (Chrome / Edge / Brave / other Chromium browsers)

1. Get the xBot browser extension folder (the folder that contains `manifest.json`) from the xBot download or repository, and unzip it somewhere permanent.
2. Open the extensions page: `chrome://extensions` (Chrome / Brave) or `edge://extensions` (Edge).
3. Turn on **Developer mode** (a toggle at the top right in Chrome, or in the left sidebar in Edge).
4. Click **Load unpacked** and select the extension folder.
5. Make sure the extension is enabled and allowed on the casino sites. If you use a mirror domain, allow the extension on it too.
6. If casino tabs were already open, reload them.

### Using the extension

1. In xBot, go to **Tools → Options**, choose the **Casino**, and set **Login method → Browser extension**.
2. Press **Log in** once. This starts the server, and the *Extension* status shows *waiting for a … tab (ws://127.0.0.1:4444)*.
3. In your browser, open the casino site (the same domain as *Site / mirror*) and **log in there**.
4. The status changes to **connected (… tab)**. xBot reads your login from the tab, fills in the fields, and loads the currencies and balance automatically.
5. Pick a **Bet currency**, tick **Enable live betting**, and press **OK**.
6. **Keep the casino tab open** while betting. If it disconnects, the log shows *"Browser extension: the … tab disconnected."* and bets wait until a tab reconnects.

---

## Manual bet

**Manual Bet** tab:
- **Bet amount**, **Win chance %**, **Multiplier**, **Profit on win**, and a range bar that shows the win/lose zones and where the last roll landed.
- **Dice:** the **Roll Under ⇄ / Roll Over ⇄** button flips the side. Then press **Roll Dice** (or Enter).
- **Limbo:** set the target multiplier, then press **Bet Limbo**.

### Rules

| | Dice | Limbo |
|---|---|---|
| Result | Roll 0.00–100.00 | Multiplier ≥ 1.00 |
| Win | Under: roll < target · Over: roll > target | result ≥ target |
| Chance | 0.01 % – 99.99 % | 99 / target |
| Payout | `floor(99 / chance, 4 dp)` (1 % house edge) | = target (1.01 – 1,000,000) |

A bet only counts as a **win** if its payout multiplier is ≥ 1x. Bet amounts are cut to 8 decimals.

---

## Auto bet

**Auto Bet** tab (settings are locked in when you press Start):

| Field | Meaning |
|---|---|
| Base bet, Win chance %, Multiplier | Starting bet |
| **On win** / **On loss** | *Reset* to base bet, or *Increase by %* (a negative value decreases) |
| Stop on profit / Stop on loss | 0 = off |
| Number of bets | 0 = ∞ |
| **Use Condition Builder** | Lets the Condition Builder blocks drive the run instead of the on-win / on-loss rules |

Use **Start (F5) / Pause–Resume (F6) / Stop (F8)**. You can change the game (Game menu) and the casino (Casino menu) **while a run is going**. The current bet finishes first, then the next ones use the new setting.

---

## Condition Builder

Open it from **Tools → Condition builder**. You build a list of **"When … Then …"** blocks. They're checked in order after every bet, and every block that matches applies its action to the next bet.

**Block types**

- **Bets** — *When* `every` · `everyStreakOf` · `firstStreakOf` · `streakGreaterThan` · `streakLowerThan` **N** — of `bet` (any bet) · `win` · `lose`
- **Profit** — *When* `greaterThan` · `greaterThanOrEqualTo` · `lowerThan` · `lowerThanOrEqualTo` **value** — of `profit` · `loss` · `balance`

**Then (actions):** `increaseByPercentage`, `decreaseByPercentage`, `addToAmount`, `subtractFromAmount`, `setAmount`, `resetAmount`, `increaseWinChanceBy` (%), `decreaseWinChanceBy` (%), `addToWinChance`, `subtractFromWinChance`, `setWinChance`, `resetWinChance`, `switchOverUnder` (dice only), `stop`.

Strategies have names and can be saved, loaded, deleted, **exported** (the selected one or all of them, as JSON) and **imported** (one strategy or a whole collection). The file format is the same as Stake's, so strategies can be moved to and from Stake with the [export-strategy extension](#importing-stake-strategies-export-strategy-extension). They're stored in `strategies.json` next to the exe, with one file per window (`strategies_2.json`, …).

Example: *Bets · everyStreakOf 1 · lose → increaseByPercentage 100* plus *Bets · every 1 · win → resetAmount* is a classic Martingale.

### Importing Stake strategies (export-strategy extension)

Stake saves its *Advanced* auto-bet strategies only in your browser, under the localStorage key `strategies_saved`, and doesn't offer an export button. The **export-strategy** browser extension (`stake-strategy`) adds **Import**, **Export Selected** and **Export All** buttons to the game sidebar on Stake. Its files use the same JSON format as xBot's Condition Builder, so you can move strategies **from Stake into xBot** and **from xBot back to Stake**.

#### Install the export-strategy extension

1. Unzip `export-strategy.zip` somewhere permanent. You get a folder `export-strategy/` with `manifest.json`, `index.js` and `icon.png`.
2. Open `chrome://extensions` (Chrome / Brave) or `edge://extensions` (Edge).
3. Turn on **Developer mode**.
4. Click **Load unpacked** and select the `export-strategy` folder.
5. Open or reload a Stake game page, e.g. `https://stake.com/casino/games/dice`. The three buttons appear at the bottom of the game sidebar.

The extension runs on `stake.com`, `stake.bet`, `stake.us`, `stake.games`, `stake.pet`, `stake.kim`, `stake.mba`, `stake.ac`, `stake.jp`, `stake.bz`, `stake.ceo`, `stake.krd`, `staketr.com`, `primedice.com`, `stake1067.com` and `stake3075.com`.

#### Stake → xBot

1. On Stake, open the **Auto** tab and switch on **Advanced** (strategy) mode. Select the strategy you want in the strategy dropdown.
2. Click **Export Selected** to download that strategy, or **Export All** to download every saved strategy. You get a file such as `strategy_<name>_<time>.json` or `strategies_all_<time>.json`.
3. In xBot, open **Tools → Condition builder → Import** and choose the file.
   - If the file has one strategy, it's loaded directly.
   - If the file has several, choose **Load Selected** for one, or **Import All**.
4. Press **Save** and **OK**. Then tick **Use Condition Builder** on the Auto Bet tab and press **Start** (F5).

#### xBot → Stake

1. In xBot, open **Condition builder → Export**, then choose **EXPORT SELECTED** or **EXPORT ALL**.
2. On Stake, click **Import** and pick one or more `.json` files.
3. A message shows how many strategies were imported. Strategies whose name already exists on Stake are **skipped**, not overwritten. Reload the page if the new strategy doesn't show up in the dropdown straight away.

#### File format (shared by both)

```json
[
  {
    "label": "Martingale",
    "blocks": [
      { "id": "a1", "type": "bets",
        "on": { "type": "everyStreakOf", "value": 1, "betType": "lose", "profitType": "profit" },
        "do": { "type": "increaseByPercentage", "value": 100 } },
      { "id": "a2", "type": "bets",
        "on": { "type": "every", "value": 1, "betType": "win", "profitType": "profit" },
        "do": { "type": "resetAmount", "value": 0 } }
    ]
  }
]
```

The extension's Import also accepts Stake's own share format (`{ "name": …, "options": { "conditions": { "blocks": [ … ] } } }`) and converts it.

For safety, the extension rejects any strategy that contains an action outside the Condition Builder list. That list is: `stop`, `addToAmount`, `subtractFromAmount`, `setAmount`, `resetAmount`, `increaseByPercentage`, `decreaseByPercentage`, `increaseWinChanceBy`, `decreaseWinChanceBy`, `addToWinChance`, `subtractFromWinChance`, `setWinChance`, `resetWinChance`, `switchOverUnder`. It also rejects a `profitType` other than `profit`, `loss` or `balance`.

A Stake strategy only holds the blocks. The base bet, win chance, side and stop limits come from xBot's **Auto Bet** tab.

---

## Code mode — Lua and JavaScript strategies

Click the **#** toolbar button (*Strategy editor*). You can pick **Lua** or **JavaScript**, and each language keeps its own script. Use **Run** to start the strategy as an auto-bet and **Stop** to end it. The **Console** tab shows `print()` output and errors. **Open… / Save / Save as…** work with `.lua` and `.js` files, and **Help** shows the in-app reference. Scripts are also saved automatically between launches.

### How a script runs

1. The **script body runs once** when you press Run. Use it to set the first bet.
2. If the script defines a global function **`dobet()`**, it's called **after every finished bet**. Change `nextbet` / `chance` / `bethigh` / `target` (and optionally `game`, `casino`, `currency`) inside it to steer the next bet.
3. After each call, xBot reads the variables back and places the next bet.

Scripts don't use the Manual or Auto Bet tabs, or the Options client seed. They have to set `nextbet` and `chance` (dice) or `target` (limbo) themselves.

### Variables you set

| Variable | Type | Required | Description |
|---|---|---|---|
| `nextbet` | number | **yes** | Amount of the next bet (≥ 0, cut to 8 decimals) |
| `chance` | number | **dice** | Win chance in % (0.01 – 99.99, rounded to 2 dp) |
| `bethigh` | boolean | dice | `true` = roll over, `false` = roll under (default) |
| `target` | number | **limbo** | Target multiplier (1.01 – 1,000,000). `chance` and `bethigh` are ignored in limbo. |
| `game` | string | no | `"dice"` or `"limbo"` (default: the game selected in the Game menu). Can be changed in `dobet()`. |
| `casino` | string | no | `"stake"`, `"shuffle"`, `"goated"`, `"wolfbet"`, `"duel"`, `"paradice"`, `"pasino"`, `"tower"` or `"winna"`. Switches the casino the next bets go to, **without resetting the session**. The casino must already be logged in in Options. |
| `currency` | string | no | Currency code, e.g. `"btc"` or `"usdt"` (default: the current bet currency). In live mode the account needs a balance in it, and the session statistics restart in the new currency. |

You can also create your own variables (e.g. `basebet`); they persist between `dobet()` calls.

### Read-only variables (refreshed before every `dobet()`)

| Variable | Description |
|---|---|
| `win` | `true` if the last bet won |
| `previousbet` | Amount of the last bet |
| `lastprofit` | Profit of the last bet (negative on a loss) |
| `roll` | Roll of the last bet (in limbo: the result multiplier) |
| `lastBet.amount`, `lastBet.roll`, `lastBet.profit`, `lastBet.payout` | Details of the last bet |
| `balance` | Current balance |
| `profit` | Profit of this run so far |
| `bets`, `wins`, `losses` | Counters for this run |
| `wagered` | Total amount wagered in this run |
| `currentstreak` | > 0 = consecutive wins, < 0 = consecutive losses |
| `startbalance` | Balance when the run started (or at the last `resetstats()`) |

### Functions

| Function | Description |
|---|---|
| `dobet()` | You define it; it's called after every bet |
| `stop([reason])` | Stops the run (the reason is shown in the log) |
| `print(...)` / `log(...)` | Write to the Console tab (small numbers are printed in full, e.g. `0.00000226`) |
| `console.log / info / warn / debug / error(...)` | JavaScript only; objects are printed as JSON |
| `resetstats()` | Resets the session statistics (bets, wins, losses, wagered, profit, streaks). The balance is kept, and `startbalance` becomes the current balance. |
| `resetseed([clientseed])` | Rotates the seed pair, e.g. `resetseed("123xcv123")`. Without an argument, a random 10-character client seed is used. Safe to call at any time: in live mode it waits for a bet in flight, and the next bet waits for the server. See *Seeds* under [Supported casinos](#supported-casinos) for what each casino supports (on Winna, xBot reads the active seed first and then rotates it). |
| `vault(amount)` | Deposits `amount` of the current currency into the vault, e.g. `vault(0.001)`. Live mode only; supported on **Stake, Shuffle, Goated and Winna**. Winna has a minimum deposit (1 USD); a refused deposit shows the site's message in the log. |
| `os.time()`, `os.clock()` | Lua only: Unix time in seconds, and CPU time |

### Lua syntax extras

The Lua engine is **Lua 5.4** with these additions:
- `!x` → `not x`, and `!=` → `~=` (e.g. `bethigh = !bethigh`).
- Compound assignments: `+=  -=  *=  /=  //=  %=  ^=  ..=` (e.g. `nextbet *= 2`). Write one per line (or end it with `;`). Don't put function calls on the left side, because it's evaluated twice.

### JavaScript notes

- Plain ECMAScript (Qt's QJSEngine).
- Set the bot variables as **globals**: `nextbet = 0.0001;` or `var nextbet = 0.0001;`. A top-level `let` / `const` is **not visible** to the bot, so only use those inside functions.

### Sandbox / limits

- **Lua:** no `io`, `os.execute`, `require`, `dofile`, `loadfile` or `debug`. Each call (the script body or one `dobet()`) is limited to **2 seconds**, and the script can use at most **128 MB** of memory.
- **JavaScript:** no file, network or OS access, and the same 2-second limit per call.

A script that runs too long is aborted with *"script took longer than 2000 ms and was aborted"*.

### Example: Martingale (Lua)

```lua
game    = "dice"
chance  = 49.5
bethigh = true
basebet = 0.00000100
nextbet = basebet

function dobet()
    if win then
        nextbet = basebet
    else
        nextbet *= 2
    end
    if profit >= 0.0001 then stop("target profit reached") end
end
```

### Example: Martingale (JavaScript)

```javascript
game = "dice";
chance = 49.5;
bethigh = false;
basebet = 0.000001;
nextbet = basebet;

function dobet() {
    if (win) nextbet = basebet;
    else nextbet = previousbet * 2;
    if (currentstreak <= -10) stop("10 losses in a row");
}
```

### Example: Limbo, seed reset, vault

```lua
game    = "limbo"
target  = 2
basebet = 0.00000100
nextbet = basebet

function dobet()
    if win then nextbet = basebet else nextbet = previousbet * 2 end
    if bets % 500 == 0 then resetseed() end          -- new seed every 500 bets
    if profit > 0.001 then vault(0.001); resetstats() end
    print("bet", bets, "roll", roll, "profit", profit)
end
```

### Example: switching game, casino and currency from code

```lua
game = "dice"; chance = 49.5; nextbet = 0.0000001

function dobet()
    if losses == 20 then game = "limbo"; target = 2 end   -- change game mid-run
    if bets == 1000 then casino = "shuffle" end           -- continue on Shuffle (session kept)
    if bets == 2000 then currency = "usdt" end            -- bet in USDT from now on
end
```

---

## Provably fair / simulation

Simulation uses the same algorithm as Stake: `HMAC_SHA256(serverSeed, "clientSeed:nonce:0")`, where the first 4 bytes give the roll, and nonces start at 1. The server seed stays hidden behind its SHA-256 hash until it's rotated.

The **Provably Fair** tab shows:
- **Active seeds:** client seed, hashed server seed, nonce, and a **Rotate server seed** button (the previous seed is revealed).
- **Verify a roll:** enter a server seed, client seed and nonce, then press Verify. The SHA-256 hash of the server seed is shown.
- **Use these seeds for simulation:** run the simulation on a fixed seed pair.

**Rotate seed** on the toolbar rotates the seed pair of the current mode. In live mode, the casino's server handles the rotation.

---

## Multiple windows

The **+** toolbar button (*New window*) opens another independent xBot window: `xBot #2`, `#3`, and so on. Each window has its **own settings, tokens, scripts, layout, strategies and WebView2 browser profile**, so you can be logged into different accounts or casinos at the same time. A window number keeps its data between launches. Only the dark theme setting is shared.

---

## Where data is stored

| What | Where |
|---|---|
| Settings, layout, tokens / API keys, scripts | Registry: `HKEY_CURRENT_USER\Software\xBot\xBot` (group `Windows/<n>` per window) |
| Condition Builder strategies | `strategies.json` next to `xBot.exe` (`strategies_2.json`, … for other windows) |
| WebView2 browser profiles | WebView2 default folder (main window), and `%LOCALAPPDATA%\xBot\xBot\WebView2Sessions\window-<n>` for other windows |

To delete all of it, use **Options → Data → Clear data from PC**. Your login tokens are stored on this PC, so keep it secure.

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| **Enter** | Roll once (manual bet) |
| **F5** | Start auto-bet |
| **F6** | Pause / Resume auto-bet |
| **F8** | Stop auto-bet |

---

## Building from source

Requirements:
- **Windows** with MSVC (Visual Studio 2019/2022)
- **Qt 6.5+** with the modules *Widgets*, *Network*, *WebSockets* and *Qml* (for QJSEngine)
- **Lua 5.4** sources or library (`lua.h`, `lauxlib.h`, `lualib.h`)
- **Microsoft WebView2 SDK** (`WebView2.h`, the WebView2 loader) and WRL
- Links against `advapi32.lib` (for clearing the registry key)

Build it with Qt Creator or CMake/qmake against those dependencies. The icons come from a Qt resource file (`:/icons/…`).

Project layout:

```
main.cpp, MainWindow.*      application and main window
bot/                        engine, scripting (LuaStrategy, JsStrategy), Condition runner,
                            casino transports (Stake, Shuffle, Goated, Wolfbet, Duel, Paradice,
                            Pasino, Tower, Winna, WebView, Extension), NoCookieJar.h
ui/                         panels, Options, Condition Builder, login dialogs, editor, chart, log
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Login buttons are greyed out, or "Is the WebView2 Runtime installed?" | Install the Microsoft Edge WebView2 Runtime, or use Browser extension mode. |
| *Loading browser…* never finishes (Stake Cookie mode) | A Cloudflare check or network problem. Press Log in again, or switch to Browser extension mode. |
| *waiting for a … tab with the extension* | Open the casino on the **same domain** as *Site / mirror*, check that the extension is enabled, and reload the tab. |
| *Not logged in on the … tab (… not found)* | Log in on the site in your browser. xBot picks it up within a few seconds. |
| *Could not start the extension server on 127.0.0.1:4444* | Another program (or another xBot window using the extension) is already using the port. |
| The mouse pointer disappears after a login window closes | A known WebView2 runtime bug. xBot works around it automatically; moving the mouse also brings the pointer back. |
| *HTTP 403: blocked by Cloudflare* (Tower, Winna) | `cf_clearance` expired or does not match the User-Agent. Click the casino's **Open … & Log in** button again, or use Browser extension mode. |
| *HTTP 401: unauthorized* on Winna | The saved session no longer works (for example after logging in elsewhere). Click **Open Winna & Log in**: it clears winna.com's old cookies first and saves the new login at once. |
| A Tower bet pays 1.98x instead of 2x (old versions) | Tower uses a 2 % house edge; current versions convert the win chance so the multiplier you set is paid. The same applies to Duel and Pasino. |
| *Not logged in on the tower.bet tab (the "__tt" cookie not found)* in extension mode | Update the browser extension to 2.2.0+, reload it in `chrome://extensions`, and refresh the casino tab. |
| A bet is larger than the balance and the run stops | Turn on *Retry bet when bet higher than balance* in Options → Game. |
| Script error *"nextbet must be set to a number"* | Set `nextbet` (and `chance`, or `target` for limbo) in the script body. In JavaScript, don't use a top-level `let` / `const` for them. |

---

## About

The UI is styled after [qBittorrent](https://github.com/qbittorrent/qBittorrent).
