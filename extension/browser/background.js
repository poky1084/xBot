// Casino sites the request proxy (index.js) is injected into. A mirror you use must be in
// this list AND in "host_permissions" in manifest.json (as "https://*.<domain>/*").
// "www." is covered automatically.
const targetSites = [
  // Stake / Primedice
  "stake.bet", "stake.com", "stake.pet", "stake.kim", "stake.us", "stake.games",
  "stake.mba", "stake.ac", "stake.jp", "stake.bz", "stake.krd", "stake.ceo",
  "staketr.com", "primedice.com", "primedice.games", "stake1021.com", "stake1067.com", "stake3075.com",
  // Shuffle, Goated, Wolfbet, Duel
  "shuffle.com", "shuffle.game", "goated.com", "wolfbet.com", "duel.com", "duel.net", "paradice.in", "pasino.com"
];

function inject(tabId) {
  chrome.scripting.executeScript({
    target: { tabId: tabId },
    files: ['index.js']
  }).catch(() => {});
}

// Pages loaded from now on.
chrome.webNavigation.onCompleted.addListener((details) => {
  if (details.frameId !== 0)
    return; // the main page only (index.js guards against running twice anyway)
  inject(details.tabId);
}, {
  url: targetSites.flatMap(host => [{ hostEquals: host }, { hostEquals: 'www.' + host }])
});

// Casino tabs that were already open when the extension was installed / reloaded or the
// browser started: no need to refresh them.
function injectOpenTabs() {
  chrome.tabs.query({ url: targetSites.map(host => 'https://*.' + host + '/*') }, (tabs) => {
    for (const tab of tabs || [])
      inject(tab.id);
  });
}
chrome.runtime.onInstalled.addListener(injectOpenTabs);
chrome.runtime.onStartup.addListener(injectOpenTabs);

// index.js asks for a cookie of its own site (httpOnly cookies such as Stake's "session" or
// Duel's "duel" are not visible to the page; the cookies API can read them).
// Cloudflare may set cf_clearance as a *partitioned* cookie (CHIPS), which a plain
// cookies.get() does not return, so the partitioned stores are searched too.
function cookieMatchesHost(cookie, host) {
  const d = String(cookie.domain || '').replace(/^\./, '').toLowerCase();
  return d === host || host.endsWith('.' + d) || d.endsWith('.' + host);
}

function getAll(details) {
  return new Promise((resolve) => {
    try {
      chrome.cookies.getAll(details, (cookies) => {
        resolve(chrome.runtime.lastError ? [] : (cookies || []));
      });
    } catch (e) {
      resolve([]); // e.g. partitionKey not supported by this Chrome version
    }
  });
}

async function findCookie(url, name) {
  const u = new URL(url);
  const host = u.hostname.toLowerCase();
  const bare = host.replace(/^www\./, '');
  const origins = ['https://' + bare, 'https://www.' + bare];

  // 1. the normal (unpartitioned) cookie for this page
  let found = (await getAll({ url: url, name: name }))[0];
  // 2. the same, for the site with / without "www."
  for (const o of origins) {
    if (!found)
      found = (await getAll({ url: o + '/', name: name }))[0];
  }
  // 3. partitioned cookies whose top-level site is this site
  for (const o of origins) {
    if (!found)
      found = (await getAll({ url: o + '/', name: name, partitionKey: { topLevelSite: o } }))[0];
  }
  // 4. any partition (Chrome 119+: an empty partitionKey returns all of them)
  if (!found)
    found = (await getAll({ name: name, partitionKey: {} })).find((c) => cookieMatchesHost(c, bare));
  return found ? found.value : null;
}

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || msg.type !== 'xbot-cookie')
    return false;
  const url = (sender.tab && sender.tab.url) || sender.url;
  if (!url) {
    sendResponse({ error: 'no tab url' });
    return false;
  }
  findCookie(url, String(msg.name))
    .then((value) => sendResponse({ value: value }))
    .catch((e) => sendResponse({ error: String((e && e.message) || e) }));
  return true; // answered asynchronously
});
