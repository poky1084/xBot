# xBot

xBot is a desktop dice, limbo and casino-games betting bot for **Windows**, written in C++ with Qt 6. It can bet on nine casinos: **Stake**, **Shuffle**, **Goated**, **Wolfbet**, **Duel**, **Paradice**, **Pasino**, **Tower.bet** and **Winna**. You can bet by hand, run auto-bet with on-win and on-loss rules, build rules in the **Condition Builder**, or write your own strategy in **Lua** or **JavaScript**. The layout is modelled on qBittorrent.

Every new launch starts in **simulation mode**, which uses play money and a provably fair seed generator that works like Stake's. Real-money betting only starts after you turn it on yourself in *Tools → Options → Live betting*.

Source code and exe: <https://github.com/poky1084/xbot> <br />
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
    - [Stake games in code mode](#stake-games-in-code-mode)
    - [Game examples (JavaScript)](#game-examples-javascript)
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
| **Stake.com** (and Primedice) | Dice, Limbo + Stake's games (see [Stake games in code mode](#stake-games-in-code-mode)) | *Cookie (cf_clearance)* → hidden WebView2 browser, or *Browser extension* | API key (`x-access-token`), optional `cf_clearance` | ✅ | Editable (`stake.com`, `stake.bet`, `stake.us`, `stake.games`, `stake.pet`, `stake.kim`, `stake.mba`, `stake.ac`, `stake.jp`, `stake.bz`, `stake.krd`, `stake.ceo`, `staketr.com`, `stake1021.com`, `stake1067.com`, `stake3075.com`, `primedice.com`, `primedice.games`, …) |
| **Shuffle.com** | Dice, Limbo, Keno, Mines, Plinko, Wheel, Tower, Chicken, Hilo, Blackjack | *Direct mode* (HTTPS + WebSocket from the app), or *Browser extension* | Token, `CORRELATION_USER_ID`, `BROWSER_ID` | ✅ | Editable (`shuffle.com` or a mirror) |
| **Goated.com** | Dice, Limbo, Keno, Mines, Plinko, Wheel, Diamonds, Tower, Goat Run, Hilo, Blackjack | *Direct mode* (HTTPS), or *Browser extension* | Token (`g3_token`) | ✅ | Fixed (`goated.com`) |
| **Wolfbet.com** | Dice, Limbo, Keno, Plinko, Hilo | *Direct mode* (HTTPS), or *Browser extension* | Token (the `token` cookie) | ❌ | Fixed (`wolfbet.com`) |
| **Duel.com** | Dice, Limbo, Keno, Mines, Plinko, Beef (cross-road), Hilo, Blackjack | *Direct mode* (hidden WebView2 page), or *Browser extension* | Token (`duel` cookie), UUID (`security:uuid`), optional `cf_clearance` | ❌ | Editable (`duel.com` or a mirror) |
| **Paradice.in** | Dice | *Direct mode* (HTTPS GraphQL), or *Browser extension* | Token (localStorage `token`, a JWT) | ❌ | Fixed (`paradice.in`) |
| **Pasino.com** | Dice, Limbo, Keno, Mines, Plinko, Blackjack (no insurance) | *Direct mode* (HTTPS), or *Browser extension* | Token (localStorage `token`, 64 hex characters) | ❌ | Fixed (`pasino.com`) |
| **Tower.bet** | Dice, Limbo, Plinko, Wheel (risk only, no segments) | *Direct mode* (HTTPS behind Cloudflare), or *Browser extension* | Token (`__tt` cookie), `cf_clearance`, User-Agent | ❌ | Fixed (`tower.bet`) |
| **Winna.com** | Dice, Limbo | *Direct mode* (HTTPS behind Cloudflare), or *Browser extension* | Session (`connect.sid` + the other winna.com cookies), `cf_clearance`, User-Agent, Device ID (`wn-antifraud-device-id`) | ✅ | Fixed (`winna.com`) |

**House edge:** the app's win chance and multiplier assume a 1 % house edge (multiplier = 99 / chance). Stake, Shuffle, Goated, Wolfbet, Paradice and Winna dice use the same edge. **Duel (0.1 %)**, **Tower (2 %)** and **Pasino (5 %)** dice use a different one, so xBot converts the win chance it sends so that the multiplier you set is the one paid: 2x is sent as 49.95 % on Duel, 49.000 % on Tower (over 51.000) and 47.50 % on Pasino. The bet list still shows the app's target. Limbo sends the multiplier itself, so it is exact everywhere.

**Seeds:** Stake, Shuffle, Goated, Duel and **Winna** set the client seed you choose. Pasino sends the client seed with every bet, so setting it only changes the seed used from then on. Wolfbet and Paradice can only renew the server seed, so xBot bets on with the current client seed. Tower has no seed support in xBot yet.

**Games on the other casinos:** besides dice and limbo, xBot plays the casinos' own versions of Stake's games (table above). It turns every casino's answers into Stake's format, so the Manual / Auto Bet tabs, the bet list, scripts' `bet` / `currentBet` objects and `round()` work the same everywhere. Notes per casino:

- **Wolfbet:** Keno and Plinko risk low / medium / high (no *classic* / *expert*). Hilo: *high* = higher or same, *low* = lower or same (an Ace: *high* is strictly higher; a King: *low* is strictly lower; *Equal* only on an Ace or a King); skip and cash out supported; an open round is loaded with `hilo/status`.
- **Duel:** Keno risk classic / low / medium / high; Mines and Beef are played in one request (autobet); Plinko risk low / medium / high; Hilo has all six guesses (higher, lower, higher or same, lower or same, equal, skip) and cash out; Blackjack with insurance; open hilo rounds and blackjack hands are loaded (`hilo/active`, `GET blackjack`) and finished first.
- **Pasino:** Blackjack has **no insurance** (scripts' `BLACKJACK_INSURANCE` is refused, `BLACKJACK_NOINSURANCE` does nothing); an open hand is loaded with `blackjack/session`. When Pasino answers *"active session"* / *"finish your manual session"* right after a bet (it is still settling the last one), xBot quietly sends the bet again after a short pause.
- **Tower.bet:** Plinko risk low / medium / high; Wheel uses only the risk (the Segments input is hidden and scripts don't need `segments`). Tower's `isWin` is true for any payout above 0, so xBot decides wins by the payout.
- **Blackjack on Duel and Pasino:** a *double* or *split* the casino does not offer at that moment (e.g. double after a hit) is played as *hit* below 17 and *stand* from 17.
- A game a casino does not have fails with *"Blackjack is not available on Winna – pick another game or switch the casino."*

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
| **Menu bar** | **File** (Exit) · **Casino** (Stake / Shuffle / Goated / Wolfbet / Duel / Paradice / Pasino / Tower / Winna) · **Game** (the games the selected casino offers: Dice / Limbo, on Stake also its other games, on Shuffle also Keno, Mines, Plinko, Wheel, Tower, Chicken, Hilo and Blackjack, on Goated also Keno, Mines, Plinko, Wheel, Diamonds, Tower, Goat Run, Hilo and Blackjack, on Wolfbet also Keno, Plinko and Hilo, on Duel also Keno, Mines, Plinko, Beef, Hilo and Blackjack, on Pasino also Keno, Mines, Plinko and Blackjack, on Tower also Plinko and Wheel) · **View** (Show sidebar, Show bet list, Show bottom panel, Show log, Strategy editor, Dark theme) · **Bot** (Start / Pause / Stop auto-bet, Roll once, Reset session, Clear history, Rotate seed) · **Tools** (Statistics, Options, Condition builder) · **Help** (About) |
| **Toolbar** | New window (+), Reset session, Start, Pause, Stop, Rotate seed, Statistics, Options, Condition builder, Strategy editor (#), and a filter box for the bet list (*Filter by nonce or roll…*) |
| **Status / Session sidebar** (dockable) | **Status:** mode (Simulation / Live), casino, connection state. **Session:** Balance, Profit, Wager, Wins, Losses, RTP (payouts ÷ wagered, cut to 2 decimals; 100.00 % before the first bet, green from 100 %), Big wins (the session's 20 highest-multiplier wins, highest first; the latest first on equal multipliers), Highest bet, highest and lowest profit, Highest / Lowest / Current streak, Running time |
| **Bet list** | Time, Bet ID, Casino, Game, Nonce, Amount, Target, Roll, Multiplier, Payout, Profit. All columns are bold; winning rows are green, losing rows grey (the Roll column keeps its own colors). The road-crossing games (Chicken, Goat Run, Beef) show as `cross`. Right-click a live bet to **copy its Bet ID** (on Stake also the IID) or **open the bet in the browser** (not available for Duel, Paradice, Pasino, Tower and Winna bets, which only offer copying the Bet ID). The list follows new bets until you scroll, and follows again when you scroll back to the top. |
| **Bottom panel tabs** | **Manual Bet** · **Auto Bet** · **Profit Chart** (the profit of the last 100 bets; green above 0, red below, shaded; hover shows `#bet profit`) · **Provably Fair** |
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
- **Other games** (keno, mines, plinko, …): the game's own inputs replace Win chance / Multiplier. **Rows** (plinko) and **Steps** (cross / chicken / goat run / beef) are drop-down lists that only offer what the casino allows for the chosen difficulty. Inputs a casino does not use are hidden (e.g. Segments on Tower.bet's wheel), and risks / difficulties it lacks are left out (e.g. Duel's beef shows *extreme* instead of *expert*).
- **Hilo and blackjack:** play with the buttons under the bet amount; the cards and each hand's moves are shown on the right (no big result line). A round still open on the casino is shown when you pick the game. The tab is only as tall as its content.

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

Click the **#** toolbar button (*Strategy editor*). You can pick **Lua** or **JavaScript**, and each language keeps its own script. Use **Run** to start the strategy as an auto-bet and **Stop** to end it. The **Console** tab shows `print()` output and errors. At its bottom, a command line sends a Lua / JavaScript snippet to the running script (**Send** or Enter; Up / Down recall earlier commands): `nextbet = 0.001`, `print(balance)`, `stop()`… An expression's value is printed (`balance`, `lastBet.roll`). With no script running, commands run in the Console's own script in the editor's language (Lua or JavaScript): its variables stay between commands and `balance` is the current balance, so it works as a calculator or for trying out code; betting functions do nothing there. **Open… / Save / Save as…** work with `.lua` and `.js` files, and **Help** shows the in-app reference. Scripts are also saved automatically between launches.

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
| `game` | string | no | `"dice"` or `"limbo"` (default: the game selected in the Game menu). On Stake also `"keno"`, `"mines"`, `"plinko"`, `"wheel"`, `"hilo"`, `"blackjack"`, `"baccarat"`, `"roulette"`, `"tower"` (Stake's Dragon Tower), `"flip"`, `"rps"`, `"pump"`, `"snakes"`, `"cases"`, `"darts"`, `"diamonds"`, `"videopoker"`, `"bluesamurai"`, `"scarabspin"`, `"tomeoflife"`, `"bars"`, `"chicken"`, `"tarot"`, `"drill"`, `"packs"`, `"moles"`, `"primedice"` (Primedice X); `"cross"` — the casino's road-crossing game: Chicken on Stake / Shuffle, Goat Run on Goated, Beef on Duel (`"chicken"`, `"goatrun"`, `"beef"` still work) — each with its own variables, see [Stake games in code mode](#stake-games-in-code-mode). Set at the top of the script, it also switches the Game menu (and the tabs and window title) when the run starts; changed in `dobet()`, only the window title follows. |
| `casino` | string | no | `"stake"`, `"shuffle"`, `"goated"`, `"wolfbet"`, `"duel"`, `"paradice"`, `"pasino"`, `"tower"` or `"winna"`. Switches the casino the next bets go to, **without resetting the session**. The casino must already be logged in in Options. |
| `currency` | string | no | Currency code, e.g. `"btc"` or `"usdt"` (default: the current bet currency). In live mode the account needs a balance in it. The session is kept (bets, profit, history and graph go on); only the balance becomes the new currency's. |
| `serverSeed`, `clientSeed`, `nonce` | string, string, number | no | Simulation only: the seed pair and the next nonce the simulation bets with. When the script starts they are the **Verify a roll** seeds and nonce if *Use these seeds for simulation* is ticked, otherwise the latest revealed seed pair from nonce 1, otherwise the active pair (the log says which). `nonce` follows every bet; setting any of them (in the body or in `dobet()`) makes the next bets use those seeds / that nonce. Not set in live mode. |

You can also create your own variables (e.g. `basebet`); they persist between `dobet()` calls.

### Read-only variables (refreshed before every `dobet()`)

| Variable | Description |
|---|---|
| `win` | `true` if the last bet won |
| `previousbet` | Amount of the last bet |
| `lastprofit`, `currentprofit` | Profit of the last bet (negative on a loss) |
| `roll` | Roll of the last bet, the full value as the casino reported it (in limbo: the result multiplier; in Stake's other games: see the per-game table). The bet list shows it cut to 2 decimals. |
| `lastBet.amount`, `lastBet.roll` (also `lastBet.Roll`), `lastBet.profit`, `lastBet.payout`, `lastBet.payoutMultiplier`, `lastBet.win`, `lastBet.id`, `lastBet.name`, `lastBet.result` | Details of the last bet. `lastBet.result` is the game's own result as Stake reports it: baccarat `"banker"` / `"player"` / `"tie"`, roulette's winning number, wheel / cases result, the dice or Primedice X roll, the limbo multiplier; for games without one (video poker, mines, keno, …) and in simulation it is the same as `lastBet.roll`. Video poker: `lastBet.roll` / `lastBet.Roll` are the hand's outcome, e.g. `"twoPair"`; mines: the mine positions; keno: the drawn numbers (both 1-based lists). |
| `bet` | Stake's full bet object of the last bet (e.g. `bet.payoutMultiplier`, `bet.state.playerHand`, `bet.state.drawnNumbers`). Positions inside it are Stake's own, 0-based. In simulation it holds the simulated Stake bet (empty for dice and limbo). |
| `balance` | Current balance |
| `profit` | Profit of this run so far |
| `bets`, `wins`, `losses` | Counters for this run |
| `wagered` | Total amount wagered in this run |
| `currentstreak` | > 0 = consecutive wins, < 0 = consecutive losses |
| `winstreak`, `losestreak` | Current number of wins / losses in a row (0 when the streak is the other way) |
| `startbalance` | Balance when the run started (or at the last `resetstats()`) |

### Functions

| Function | Description |
|---|---|
| `dobet()` | You define it; it's called after every bet |
| `stop([reason])` | Stops the run (the reason is shown in the log) |
| `print(...)` / `log(...)` | Write to the Console tab. Numbers are printed in full, never rounded or in exponent form (`0.00000226`, `1.987654`), also inside lists and objects: Lua tables print their contents (`{1,5,12}`, `{banker=0.001,player=0.001,tie=0}`), JavaScript objects / arrays print as JSON. |
| `console.log / info / warn / debug / error(...)` | JavaScript only; objects are printed as JSON |
| `resetstats()` | Resets the session statistics (bets, wins, losses, wagered, profit, streaks). The balance is kept, and `startbalance` becomes the current balance. |
| `resetseed([clientseed])` | Rotates the seed pair, e.g. `resetseed("123xcv123")`. Without an argument, a random 10-character client seed is used. Safe to call at any time: in live mode it waits for a bet in flight, and the next bet waits for the server. See *Seeds* under [Supported casinos](#supported-casinos) for what each casino supports (on Winna, xBot reads the active seed first and then rotates it). |
| `vault(amount)` | Deposits `amount` of the current currency into the vault, e.g. `vault(0.001)`. Live mode only; supported on **Stake, Shuffle, Goated and Winna**. Winna has a minimum deposit (1 USD); a refused deposit shows the site's message in the log. |
| `os.time()`, `os.clock()` | Lua only: Unix time in seconds, and CPU time |
| `beep()`, `ding()` | Play a bell tone (the reference bot's beep), e.g. when a target hits |
| `round()` | You define it (hilo and blackjack only); called for every move of an open game, see below |
| `checkTargetHit(hand, combos)` | Video poker: the label of the first combo the hand contains (`"3xA + 2xK"`), else `nil` / `null`. Lua and JavaScript. |
| `getOptimalHold(hand)`, `getTargetedHold(hand, combos)`, `evaluateHand(hand)` | JavaScript only: the video poker strategy helpers. The first two return `{hold, reason}`. |

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

### Stake games in code mode

Besides dice and limbo, scripts can play Stake's own games. Set `game` to one of the names below plus that game's variables, the same way you set `chance` for dice. Crash and Slide are not supported.

- **The casino must have the game.** On a casino without it the bet fails with *"… is not available on <casino>"*. In simulation, Stake's games are played with Stake's rules (see [Provably fair / simulation](#provably-fair--simulation)). Besides Stake: **keno**, **mines**, **plinko**, **wheel**, **tower**, **cross** (chicken), **hilo** and **blackjack** play on **Shuffle**; **keno**, **mines**, **plinko**, **wheel**, **diamonds**, **tower**, **cross** (goat run), **hilo** and **blackjack** on **Goated**; **keno**, **plinko** and **hilo** on **Wolfbet**; **keno**, **mines**, **plinko**, **cross** (beef), **hilo** and **blackjack** on **Duel**; **keno**, **mines**, **plinko** and **blackjack** on **Pasino**; **plinko** and **wheel** on **Tower.bet** — with the same variables, `round()` moves, `bet` object and results (xBot turns every casino's answers into Stake's format; blackjack side bets are not played). On Shuffle, Goated and Wolfbet, hilo's *Equal* is only possible on an Ace or a King. Pasino blackjack has no insurance. See [Supported casinos](#supported-casinos) for the risks / difficulties each casino offers.
- **No defaults in code mode.** Every variable listed for the game must be set by the script (in the script body, or in `dobet()` before it is read back). If one is missing, the run doesn't start, or stops, with *"keno needs these variables set in the script: numbers, risk"*. The only optional one is video poker's `target` (`nil` / `null` = the optimal hold). The Manual / Auto Bet tabs are not used in code mode.
- **Variables** are read back after the script body and after every `dobet()`, like `nextbet`.
- **Positions are 1-based** (1 = the first tile / egg / hole); xBot subtracts 1 before sending them to Stake. The `bet` object you get back still holds Stake's 0-based numbers.
- **Results:** `win` is true when the payout multiplier is ≥ 1x. `roll` / `lastBet.roll` hold the value in the *Roll* column below; the full result is always in `bet`.

| `game` | Variables the script must set (besides `nextbet`) | `roll` / `lastBet.roll` / bet list |
|---|---|---|
| `"keno"` | `numbers` — 1 to 10 numbers, 1–40 (duplicates and out-of-range numbers are skipped) · `risk` — `"low"`, `"classic"`, `"medium"` or `"high"` | `roll` = number of hits · `lastBet.roll` = the drawn numbers, 1-based, e.g. `{3, 17, 22, …}` |
| `"mines"` | `fields` — tiles to reveal, 1 (top-left) – 25 (bottom-right), row by row · `mines` — mines count 1–24. A game left open is cashed out. | `roll` = payout multiplier · `lastBet.roll` = the mine positions, 1-based, e.g. `{4, 12, 25}` (Roll column: `safe` or `mine N`) |
| `"plinko"` | `rows` — 8–16 · `risk` — `"low"`, `"medium"`, `"high"` or `"expert"` | Payout multiplier |
| `"wheel"` | `segments` — 10, 20, 30, 40 or 50 (not needed on Tower.bet) · `risk` — `"low"`, `"medium"` or `"high"` | Result segment |
| `"hilo"` | `startcard` — `{rank = "A", suit = "H"}` (Lua) / `{rank: "A", suit: "H"}` (JS), or text such as `"A"`, `"KH"`, `"10S"` · a `round()` function that decides every move (see below) | Payout multiplier (Target column: the cards) |
| `"blackjack"` | a `round()` function that decides every move (see below) | Payout multiplier |
| `"baccarat"` | `nextbet = {player = 0, banker = 0.004, tie = 0}` — the stake on each side; the total is the bet. Or a number in `nextbet` **plus** `side` — `"player"`, `"banker"` or `"tie"`. | Payout multiplier (Roll column: the result) |
| `"roulette"` | `chips` — a list of `{value = "colorRed", amount = 0.0001}`; `nextbet` is not needed (the bet is the sum of the chips). Values: `colorRed` / `colorBlack`, `number0` – `number36`, `parityEven` / `parityOdd`, `range0112`, `range1324`, `range2536`, `range0118`, `range1936`, `row1` – `row3`. (Chip names without amounts split `nextbet` evenly between them.) | Winning number |
| `"tower"` | `eggs` — the egg picked on each floor, from the bottom, 1-based · `difficulty` — `"easy"`, `"medium"`, `"hard"`, `"expert"` or `"master"` | Floor reached |
| `"flip"` | `guesses` — list of `"heads"` / `"tails"`, one per flip | Payout multiplier |
| `"rps"` | `guesses` — list of `"rock"` / `"paper"` / `"scissors"`, one per round | Payout multiplier |
| `"pump"` | `pumps` — 1–20 · `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` | Pop point |
| `"snakes"` | `rolls` — 1–20 · `difficulty` — `"easy"`, `"medium"`, `"hard"`, `"expert"` or `"master"` | Winning rolls |
| `"cases"` | `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` | Case result |
| `"darts"` | `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` | Payout multiplier |
| `"tarot"` | `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` | Payout multiplier |
| `"cross"` (also `"chicken"`, `"goatrun"`, `"beef"`) | `steps` — lanes to cross · `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` (`"extreme"` = `"expert"`). Steps at most: Stake 20; Shuffle and Duel 19 / 17 / 15 / 10; Goated 24 / 22 / 20 / 15 (easy / medium / hard / expert). Plays Chicken on Stake / Shuffle, Goat Run on Goated, Beef on Duel. | Death point (Duel: the lane hit, 1-based) |
| `"drill"` | `target` — target multiplier 1.01–999 · `pick` — 1 (blue), 2 (yellow) or 3 (green) | Picked drill's multiplier |
| `"moles"` | `moles` — 1–10 · `picks` — one hole per round, 1-based (repeats allowed, e.g. `{1, 2, 2, 2, 3, 7}`) | Last round with a hit |
| `"bars"` | `tiles` — list of tiles, sent as written · `difficulty` — `"easy"`, `"medium"`, `"hard"` or `"expert"` | Payout multiplier |
| `"scarabspin"` | `lines` — 1–20 | Payout multiplier |
| `"tomeoflife"` | `lines` — 1–20; `bonusbuy` — `true` / `false` (or `"true"` / `"false"`, optional, default false): buys the 15 free spins for 37× `nextbet` | Payout multiplier |
| `"videopoker"` | *optional:* `target` — hold combos to chase, e.g. `{{A = 3, K = 2}, {A = 2, K = 3}}` (Lua) / `[{A: 3, K: 2}]` (JS); `nil` / `null` or not set = the optimal 9/6 Jacks-or-Better hold | `lastBet.roll` = the hand: `"royalFlush"`, `"straightFlush"`, `"fourOfAKind"`, `"fullHouse"`, `"flush"`, `"straight"`, `"threeOfAKind"`, `"twoPair"`, `"onePair"`, … (`roll` = payout multiplier) |
| `"primedice"` | `condition` — `"rollBetween"`, `"rollOutside"` or `"rollBetweenTwo"` · `target1`, `target2` · `target3`, `target4` — only for `"rollBetweenTwo"` | Roll 0.00–100.00 |
| `"diamonds"`, `"packs"`, `"bluesamurai"` | nothing else (blue samurai's bonus spins are played automatically) | Payout multiplier |

#### Hilo and blackjack: `round()`

A hilo or blackjack hand takes several moves. After the deal and after every move, while the hand is still open, xBot sets **`currentBet`** to Stake's open bet and calls your **`round()`** function. Its return value is the next move. `dobet()` runs only once the hand is settled. Hilo and blackjack scripts must define `round()`. (A `pattern` list, as in the examples, is just your own variable that `round()` reads.)

| Game | `round()` returns | Constants |
|---|---|---|
| Hilo | `HILO_BET_HIGH` (5), `HILO_BET_LOW` (4), `HILO_BET_EQUAL` (2), `HILO_SKIP` (7), `HILO_CASHOUT` (3) — or a guess as text: `"higher"`, `"lower"`, `"equal"`, `"higherEqual"`, `"lowerEqual"`, `"skip"`, `"cashout"` | For the current card, HIGH means *higher or equal* (an Ace: *higher*, a King: *equal*), LOW means *lower or equal* (an Ace: *equal*, a King: *lower*). |
| Blackjack | `"BLACKJACK_HIT"`, `"BLACKJACK_STAND"`, `"BLACKJACK_DOUBLE"`, `"BLACKJACK_SPLIT"`, `"BLACKJACK_INSURANCE"`, `"BLACKJACK_NOINSURANCE"` (or `"hit"`, `"stand"`, …) | If Stake refuses the move as invalid, xBot tries *no insurance*, then *stand*. On Duel and Pasino a double / split the casino does not offer right now is played as *hit* (below 17) or *stand*. Pasino has no insurance: `BLACKJACK_INSURANCE` stops the run with an error, `BLACKJACK_NOINSURANCE` does nothing. |

Useful fields of `currentBet`: hilo `currentBet.state.startCard.rank`, `currentBet.state.rounds` (each with `card.rank`, `card.suit`, `guess`, `payoutMultiplier`); blackjack `currentBet.state.player[1].value` / `.actions` and `currentBet.state.dealer[1].cards` (Lua tables start at 1; in JavaScript use `[0]`).

If an earlier hilo, blackjack, video poker or mines game is still open on Stake (e.g. after a lost connection), the next bet first fetches it and finishes it. The same goes for open hilo / blackjack rounds on Shuffle, Goated, Wolfbet (hilo), Duel and Pasino (blackjack); the Manual Bet tab shows such a round when you pick the game.

#### Example: keno (Lua)

```lua
game    = "keno"
risk    = "classic"
numbers = {1, 2, 3, 4, 5, 6, 7, 8}   -- 1-based: 1 .. 40
basebet = 0.00000100
nextbet = basebet

function dobet()
    if win then nextbet = basebet else nextbet = previousbet * 1.5 end
    if roll >= 7 then beep(); stop("7+ hits") end   -- roll = number of hits
end
```

#### Example: mines (JavaScript)

```javascript
game = "mines";
mines = 3;
fields = [1, 2, 3];                 // top-left row, 1-based
nextbet = 0.000001;

function dobet() {
    if (!win) fields = [21, 22, 23]; // bottom row after a loss
}
```

#### Example: hilo with `round()` (JavaScript)

```javascript
game = "hilo";
nextbet = 0;
startcard = {rank: "A", suit: "H"};
pattern = [5, 5];                   // HIGH, HIGH, then cash out

function round() {
    const rounds = currentBet.state.rounds;
    if (rounds.length < pattern.length) return pattern[rounds.length];
    return HILO_CASHOUT;
}

function dobet() {
    const ranks = ["A","2","3","4","5","6","7","8","9","10","J","Q","K"];
    startcard = {rank: ranks[Math.floor(Math.random() * 13)], suit: "H"};
}
```

#### Example: blackjack with `round()` (Lua)

```lua
game    = "blackjack"
nextbet = 0

function round()
    local hand   = currentBet.state.player[1]
    local dealer = currentBet.state.dealer[1]
    local insuranceDone = false
    for _, a in ipairs(hand.actions) do
        if a == "insurance" or a == "noInsurance" then insuranceDone = true end
    end
    if dealer.cards[1].rank == "A" and not insuranceDone then
        return "BLACKJACK_NOINSURANCE"
    end
    if hand.value < 17 then return "BLACKJACK_HIT" end
    return "BLACKJACK_STAND"
end
```

#### Example: video poker (Lua)

```lua
game    = "videopoker"
nextbet = 0
target  = {{A = 3, K = 2}, {A = 2, K = 3}}   -- chase aces full of kings (nil = optimal hold)

function dobet()
    if checkTargetHit(bet.state.playerHand, target) then beep(); stop("target hand") end
    if lastBet.roll == "royalFlush" then beep(); stop("royal flush") end
end
```

#### Example: baccarat, roulette, drill (Lua)

```lua
game    = "baccarat"
nextbet = {player = 0, banker = 0.0004, tie = 0}

-- game  = "roulette"
-- chips = {{value = "colorRed", amount = 0.0001}, {value = "number0", amount = 0.0001}}

-- game   = "drill"
-- target = 2        -- target multiplier
-- pick   = 2        -- 1 = blue, 2 = yellow, 3 = green
```

### Game examples (JavaScript)

Ready-to-run starting points for every game, adapted from the reference bot's examples. Paste one into the Strategy editor (JavaScript), set your `nextbet` and press **Run**. Except dice and limbo, they need **live betting on Stake**.

`dobet` can be declared either way:

```javascript
nextbet = 0.00001   // in crypto, not USD

dobet = function () {
    // strategy code
}

// or:
function dobet() {
    // strategy code
}
```

#### Dice

```javascript
game = "dice"
chance = 49.5
nextbet = 0
bethigh = false
```

#### Limbo

```javascript
game = "limbo"
target = 2
nextbet = 0
```

#### Primedice X

```javascript
game = "primedice"
condition = "rollOutside"  // "rollBetween", "rollBetweenTwo" or "rollOutside"
target1 = 5
target2 = 24
target3 = 45               // target3 / target4: only used by "rollBetweenTwo"
target4 = 68
nextbet = 0
```

#### Blackjack

```javascript
game = "blackjack"
nextbet = 0
basebet = nextbet
nextAction = "BLACKJACK_STAND" // or "BLACKJACK_DOUBLE", "BLACKJACK_HIT", "BLACKJACK_SPLIT",
                               //    "BLACKJACK_INSURANCE", "BLACKJACK_NOINSURANCE"

function dobet() {
    if (win) {
        nextbet = basebet
    }
}

function round() {
    if (currentBet.state.dealer[0].value === 11) {
        nextAction = "BLACKJACK_NOINSURANCE"
    } else {
        nextAction = "BLACKJACK_STAND"
    }
    if (currentBet.state.player[0].actions.includes("insurance") ||
        currentBet.state.player[0].actions.includes("noInsurance")) {
        nextAction = "BLACKJACK_STAND"
    }
    return nextAction
}
```

#### Video poker #1 — stop on exact hands

```javascript
game = "videopoker"
nextbet = 0.0
target = null
hunt1 = ["2", "2", "2", "K", "K"];
hunt2 = ["2", "2", "K", "K", "K"];

function dobet() {
    const currentHand = bet.state.playerHand.map(card => card.rank);

    const isHit = [...new Set(hunt1)].every(r =>
        currentHand.filter(card => card === r).length === hunt1.filter(card => card === r).length
    );

    const isHit2 = [...new Set(hunt2)].every(r =>
        currentHand.filter(card => card === r).length === hunt2.filter(card => card === r).length
    );

    if (isHit) {
        stop();
        beep();
    }

    if (isHit2) {
        stop();
        beep();
    }
}
```

#### Video poker #2 — hunt a pattern with `checkTargetHit`

```javascript
game = "videopoker"
nextbet = 0.0
target = null
hunting = [
    { 10: 2, A: 3 },
    { 10: 3, A: 2 }
]
// the pattern to hunt: it stops when these cards are in the player's hand

function dobet() {
    const finalHand = bet.state.playerHand;
    const hitLabel = checkTargetHit(finalHand, hunting);
    if (hitLabel) {
        stop();
        beep();
    }
}
```

#### Video poker #3 — hold for a target, stop on big hands

```javascript
game = "videopoker"
nextbet = 0.0
target = [
    { A: 2, K: 3 },
    { A: 3, K: 2 }
]
// target = null
// null = the optimal strategy; or list card ranks and how many to find: { card: amount, Q: 3 }

function dobet() {
    const finalHand = bet.state.playerHand;
    const hitLabel = checkTargetHit(finalHand, target);
    if (hitLabel) {
        stop();
        beep();
    }

    if (bet.payoutMultiplier >= 799) {
        stop();
        beep();
    }

    const handresult = bet.state.handResult;   // also in lastBet.roll
    if (handresult == "fourOfAKind") {
        stop();
        beep();
    }
}

// Possible values:
//   "royalFlush"     → 800x
//   "straightFlush"  → 60x
//   "fourOfAKind"    → 22x
//   "fullHouse"      → 9x
//   "flush"          → 6x
//   "straight"       → 4x
//   "threeOfAKind"   → 3x
//   "twoPair"        → 2x
//   "onePair"        → 1x
```

#### Hilo

```javascript
game = "hilo"
nextbet = 0
startcard = {"rank": "A", "suit": "H"}
pattern = [5, 5]
//selection = [4, 5]

function dobet() {
    card = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"].sort(() => Math.random() - 0.5).slice(0, 1).toString();
    startcard = {"rank": card, "suit": "H"}
    //pattern = selection.sort(() => Math.random() - 0.5).slice(0, 1)
}

function round() {
    const rounds = currentBet.state.rounds;
    const last = rounds.length > 0 ? rounds[rounds.length - 1] : null;
    currentCardRank = last ? last.card.rank : currentBet.state.startCard.rank;
    payoutMultiplier = last ? last.payoutMultiplier : 0;
    skippedCards = rounds.filter(r => r.guess === 'skip').length;

    if (rounds.length < pattern.length) {
        guessing = pattern[rounds.length]
    } else {
        return HILO_CASHOUT;
    }

    if (currentCardRank === "A" && guessing === 4) return HILO_BET_LOW;
    if (currentCardRank === "J" && guessing === 4) return HILO_BET_HIGH;
    if (currentCardRank === "Q" && guessing === 4) return HILO_BET_HIGH;
    if (currentCardRank === "K" && guessing === 4) return HILO_BET_HIGH;
    if (parseInt(currentCardRank) <= 10 && parseInt(currentCardRank) >= 7 && guessing === 4) return HILO_BET_HIGH;
    if (parseInt(currentCardRank) < 7 && guessing === 4) return HILO_BET_LOW;

    if (currentCardRank === "A" && guessing === 5) return HILO_BET_HIGH;
    if (currentCardRank === "J" && guessing === 5) return HILO_BET_LOW;
    if (currentCardRank === "Q" && guessing === 5) return HILO_BET_LOW;
    if (currentCardRank === "K" && guessing === 5) return HILO_BET_LOW;
    if (parseInt(currentCardRank) <= 10 && parseInt(currentCardRank) >= 7 && guessing === 5) return HILO_BET_LOW;
    if (parseInt(currentCardRank) < 7 && guessing === 5) return HILO_BET_HIGH;

    if (guessing === 2) return HILO_BET_EQUAL;

    return HILO_SKIP;
}
```

#### Keno

```javascript
game = "keno"
numbers = [1, 2, 3, 4, 5, 6, 7, 8]   // 1 .. 40
risk = "low"
nextbet = 0
```

#### Mines

```javascript
game = "mines"
fields = [1, 2, 3, 4, 5, 6, 7, 8]    // 1 (top-left) .. 25 (bottom-right)
mines = 3
nextbet = 0
```

#### Plinko

```javascript
game = "plinko"
rows = 8
risk = "low"
nextbet = 0
```

#### Wheel

```javascript
game = "wheel"
nextbet = 0
risk = "low"
segments = 10
```

#### Baccarat

```javascript
game = "baccarat"
nextbet = { player: 0, banker: 0.004, tie: 0 }
```

#### Roulette

```javascript
game = "roulette"
chips = [{"value": "number0", "amount": 0.001}, {"value": "colorBlack", "amount": 0.001}]
```

Values you can put in `chips` (each as `{"value": …, "amount": …}`):

```
ranges   : range2536, range1324, range0112, range0118, range1936
parities : parityOdd, parityEven
colors   : colorRed, colorBlack
rows     : row3, row2, row1
numbers  : number0, number1, number2, … number36
```

#### Tower

```javascript
game = "tower"   // Stake's Dragon Tower ("dragontower" still works)
nextbet = 0
difficulty = "easy"
eggs = [1, 2]       // 1-based: the egg picked on each floor (the reference bot's [0,1])
```

#### Moles

```javascript
game = "moles"
nextbet = 0
moles = 6
picks = [1, 2, 2, 2, 3, 7]   // 1-based (the reference bot's [0,1,1,1,2,6])
```

#### Drill

```javascript
game = "drill"
nextbet = 0
target = 2
pick = 1            // 1 = blue, 2 = yellow, 3 = green
```

#### Tarot

```javascript
game = "tarot"
difficulty = "medium"
nextbet = 0
```

#### Cross (Chicken / Goat Run / Beef)

```javascript
game = "cross"        // Chicken on Stake / Shuffle, Goat Run on Goated, Beef on Duel
steps = 1
difficulty = "medium"
nextbet = 0
```

#### Packs

```javascript
game = "packs"
nextbet = 0
```

#### Bars

```javascript
game = "bars"
difficulty = "easy"
tiles = [2, 3, 4, 5, 6]
nextbet = 0
```

#### Flip

```javascript
game = "flip"
nextbet = 0
guesses = ["tails", "heads"]
```

#### Rock, paper, scissors

```javascript
game = "rps"
guesses = ["rock", "paper", "scissors"]
nextbet = 0
```

#### Pump

```javascript
game = "pump"
difficulty = "easy"
pumps = 1
nextbet = 0
```

#### Snakes

```javascript
game = "snakes"
difficulty = "easy"
rolls = 1
nextbet = 0
```

#### Cases

```javascript
game = "cases"
difficulty = "easy"
nextbet = 0
```

#### Darts

```javascript
game = "darts"
difficulty = "easy"
nextbet = 0
```

#### Diamonds

```javascript
game = "diamonds"
nextbet = 0
```

#### Blue samurai

```javascript
game = "bluesamurai"
nextbet = 0
```

#### Scarab spin

```javascript
game = "scarabspin"
lines = 1
nextbet = 0
```

#### Tome of life

```javascript
game = "tomeoflife"
lines = 1
nextbet = 0
```

---

## Provably fair / simulation

Simulation uses the same algorithm as Stake: `HMAC_SHA256(serverSeed, "clientSeed:nonce:round")` gives 32 bytes per round (round 0, 1, 2, …), every 4 bytes become a float between 0 and 1, and the game turns the floats into its result (a roll, cards, tiles, a ball path, …). Nonces start at 1 and every bet takes the next one. The server seed stays hidden behind its SHA-256 hash until it's rotated.

**Simulated games.** Besides dice and limbo, the simulation plays Stake's **Primedice X**, **keno**, **mines**, **plinko** (low / medium / high / expert), **wheel**, **roulette**, **baccarat**, **hilo**, **blackjack**, **video poker**, **diamonds**, **tower**, **flip**, **drill**, **pump**, **chicken / cross**, **moles**, **rock paper scissors**, **snakes** (1 to 5 rolls), **packs**, **cases**, **bars**, **tarot**, **darts** and the slots **Scarab Spin**, **Tome of Life** and **Blue Samurai** with Stake's payout tables — every Stake game, in the Manual Bet tab, auto-bet and code mode alike (scripts' `round()` and the `bet` object work as live). Hilo, blackjack, video poker and mines rounds stay open until they're finished, like on Stake. Slots play the whole round in one bet (free spins and Blue Samurai's special feature included). Tome of Life's **bonus buy** (the *Bonus buy* check box on the Manual / Auto Bet tab, which shows its cost: 37× the bet amount; `bonusbuy` in code mode) plays the 15 free spins straight away for 37× the bet; live it's Stake's `slots-tome-of-life/bonus` request. Bars' `tiles` are the selected bars, 0 to 29 (1 to 5 of them).

Simulated blackjack deals player, dealer, player, dealer (the hole card), then every hit, double, split card and dealer draw takes the next card. Blackjack pays 3:2, insurance 2:1, the dealer stands on all 17s (soft 17 too) and checks for blackjack, one split is allowed (split aces get one card each), and you can double on any two cards. Hilo pays 1 ÷ the chance of each correct guess, multiplied along the chain, with the 1 % edge taken once (on the first correct guess). Tower pays 0.98 ÷ (eggs ÷ tiles)^floors. Chicken runs on a road of 20 places (1 / 3 / 5 / 10 losing ones: 19 / 17 / 15 / 10 steps at most), pump on 25 places; both pay 0.98 ÷ the chance of surviving. Moles places the moles on a fresh board of 7 holes each stage and pays 0.98 ÷ (moles ÷ 7)^stages. Rock paper scissors pays 0.98 × 2^wins (a draw keeps the chain going, a loss ends it); each float shows rock, paper or scissors (float × 3, in that order). Snakes rolls two dice per roll (float × 6 + 1 each) and multiplies each roll's factor from its table; a snake total (easy 7, medium 6–8, hard 5–9, expert 4–10, master 3–11) loses. Packs draws 5 cards from its 240-card table and pays the sum of their multipliers.

The **Provably Fair** tab shows:
- **Active seeds:** client seed, hashed server seed, nonce, and a **Rotate server seed** button (the previous seed is revealed).
- **Verify a roll:** enter a server seed, client seed and nonce. It shows the SHA-256 hash of the server seed and the result for the game selected in the Game menu: the dice / Primedice X roll, the limbo multiplier, keno's drawn numbers, the mines on the board, plinko's path and slot, the wheel segment, the roulette pocket, baccarat's hands, blackjack's and hilo's cards in dealing order, video poker's hand and draw cards, the diamonds gems, the tower's eggs per floor, the flips, the drill multipliers, where pump / chicken ends, the moles of each stage, the rock paper scissors opponent, the snakes dice, the 5 pack cards, the case, tarot cards, dart hit and the 30 bars, and a slot round's total. For keno, mines, plinko, wheel, tower, pump / chicken, Primedice X and moles, it uses the settings on the Manual Bet tab (numbers, mines count, rows, risk, difficulty, …) and shows what they would have paid. 
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
                            Pasino, Tower, Winna, WebView, Extension), NoCookieJar.h,
                            StakeFair (Stake's provably fair games: verify + simulation)
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
