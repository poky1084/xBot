// Casino sites the request proxy (index.js) is injected into. A mirror you use must be in
// this list AND in "host_permissions" in manifest.json (as "https://*.<domain>/*").
// "www." is covered automatically.
const targetSites = [
  // Stake / Primedice
  "stake.bet", "stake.com", "stake.pet", "stake.kim", "stake.us", "stake.games",
  "stake.mba", "stake.ac", "stake.jp", "stake.bz", "stake.krd", "stake.ceo",
  "staketr.com", "primedice.com", "primedice.games", "stake1021.com", "stake1067.com", "stake3075.com",
  // Shuffle, Goated, Wolfbet, Duel
  "shuffle.com", "shuffle.game", "goated.com", "wolfbet.com", "duel.com", "duel.net"
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
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || msg.type !== 'xbot-cookie')
    return false;
  const url = (sender.tab && sender.tab.url) || sender.url;
  if (!url) {
    sendResponse({ error: 'no tab url' });
    return false;
  }
  chrome.cookies.get({ url: url, name: String(msg.name) }, (cookie) => {
    const err = chrome.runtime.lastError;
    sendResponse(err ? { error: err.message } : { value: cookie ? cookie.value : null });
  });
  return true; // answered asynchronously
});
