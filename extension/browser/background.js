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

chrome.webNavigation.onCompleted.addListener((details) => {
  if (details.frameId !== 0)
    return; // the main page only (index.js guards against running twice anyway)
  chrome.scripting.executeScript({
    target: { tabId: details.tabId },
    files: ['index.js']
  }).catch(() => {});
}, {
  url: targetSites.flatMap(host => [{ hostEquals: host }, { hostEquals: 'www.' + host }])
});
