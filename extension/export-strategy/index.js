const STORAGE_KEY = 'strategies_saved';

// ── 1. Inject styles ────────────────────────────────────────────────────────
const style = document.createElement('style');
style.textContent = `
  .strategy-actions {
    display: flex;
    gap: 10px;
    align-items: center;
    flex-wrap: wrap;
  }

  .strategy-btn {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 10px 20px;
    border-radius: 6px;
    font-family: 'Syne', sans-serif;
    font-size: 13px;
    font-weight: 700;
    letter-spacing: .04em;
    text-transform: uppercase;
    cursor: pointer;
    border: none;
    transition: transform .15s, box-shadow .15s, background .15s, border-color .15s;
    position: relative;
    overflow: hidden;
  }
  .strategy-btn::after {
    content: '';
    position: absolute; inset: 0;
    background: white;
    opacity: 0;
    transition: opacity .15s;
  }
  .strategy-btn:hover::after { opacity: .06; }
  .strategy-btn:active { transform: scale(.97); }
  .strategy-btn svg { flex-shrink: 0; }

  /* Import — ghost */
  .strategy-btn-import {
    background: transparent;
    color: #e8e8f0;
    border: 1.5px solid #1e1e2e;
  }
  .strategy-btn-import:hover { border-color: #5a5a72; }

  /* Export Selected — solid accent */
  .strategy-btn-export-selected {
    background: #e8ff3a;
    color: #0a0a0f;
    box-shadow: 0 0 20px rgba(232,255,58,.25);
  }
  .strategy-btn-export-selected:hover { box-shadow: 0 0 32px rgba(232,255,58,.4); }

  /* Export All — outlined accent */
  .strategy-btn-export-all {
    background: transparent;
    color: #e8ff3a;
    border: 1.5px solid rgba(232,255,58,.4);
  }
  .strategy-btn-export-all:hover {
    border-color: #e8ff3a;
    box-shadow: 0 0 16px rgba(232,255,58,.15);
  }

  /* Toast */
  #strategy-toast {
    position: fixed;
    bottom: 32px; right: 32px;
    background: #111118;
    border: 1px solid #1e1e2e;
    border-radius: 8px;
    padding: 14px 20px;
    font-family: 'Syne', sans-serif;
    font-size: 13px;
    font-weight: 600;
    display: flex;
    align-items: center;
    gap: 10px;
    transform: translateY(80px);
    opacity: 0;
    transition: transform .3s cubic-bezier(.34,1.56,.64,1), opacity .3s;
    z-index: 9999;
    max-width: 320px;
    box-shadow: 0 16px 40px rgba(0,0,0,.6);
    color: #e8e8f0;
    pointer-events: none;
  }
  #strategy-toast.show    { transform: translateY(0); opacity: 1; pointer-events: auto; }
  #strategy-toast.success { border-color: rgba(26,255,138,.3); }
  #strategy-toast.error   { border-color: rgba(255,77,106,.3); }
`;
document.head.appendChild(style);

// ── 2. SVG icons ────────────────────────────────────────────────────────────
const importSVG = `<svg width="15" height="15" viewBox="0 0 24 24" fill="none"
  stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
  <polyline points="17 8 12 3 7 8"/>
  <line x1="12" y1="3" x2="12" y2="15"/>
</svg>`;

const exportSVG = `<svg width="15" height="15" viewBox="0 0 24 24" fill="none"
  stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
  <polyline points="7 10 12 15 17 10"/>
  <line x1="12" y1="15" x2="12" y2="3"/>
</svg>`;

// ── 3. Build DOM elements ───────────────────────────────────────────────────
const actionsWrapper = document.createElement('div');
actionsWrapper.className = 'strategy-actions';
actionsWrapper.innerHTML = `
  <button class="strategy-btn strategy-btn-import" id="strategyImportBtn">
    ${importSVG} Import
  </button>
  <button class="strategy-btn strategy-btn-export-selected" id="strategyExportSelectedBtn">
    ${exportSVG} Export Selected
  </button>
  <button class="strategy-btn strategy-btn-export-all" id="strategyExportAllBtn">
    ${exportSVG} Export All
  </button>
`;

// Hidden file input
const fileInput = document.createElement('input');
fileInput.type = 'file';
fileInput.accept = '.json,application/json';
fileInput.multiple = true;
fileInput.style.display = 'none';
fileInput.id = 'strategyFileInput';
document.body.appendChild(fileInput);

// Toast
const toast = document.createElement('div');
toast.id = 'strategy-toast';
toast.innerHTML = `<span id="strategy-toast-icon"></span><span id="strategy-toast-msg"></span>`;
document.body.appendChild(toast);

// ── 4. Helpers ──────────────────────────────────────────────────────────────
function getStrategies() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch { return []; }
}

function saveStrategies(arr) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(arr));
}

function showToast(msg, type) {
  type = type || 'success';
  const t    = document.getElementById('strategy-toast');
  const icon = document.getElementById('strategy-toast-icon');
  const txt  = document.getElementById('strategy-toast-msg');
  t.className = type;
  t.id = 'strategy-toast';
  icon.textContent = type === 'success' ? '\u2713' : '\u2715';
  txt.textContent  = msg;
  t.classList.add('show');
  clearTimeout(t._timer);
  t._timer = setTimeout(function() { t.classList.remove('show'); }, 3200);
}

function triggerDownload(data, filename) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  a.href     = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

// ── 5. Export Selected ──────────────────────────────────────────────────────
function exportSelected() {
  const strategies = getStrategies();
  if (!strategies.length) {
    showToast('Nothing to export \u2014 storage is empty.', 'error');
    return;
  }

  const selectEl = document.querySelector('[data-testid="select-strategy-option"]');
  const selectedLabel = selectEl ? selectEl.value : null;

  if (!selectedLabel) {
    showToast('No strategy selected.', 'error');
    return;
  }

  const toExport = strategies.filter(function(s) { return s.label === selectedLabel; });
  if (!toExport.length) {
    showToast('"' + selectedLabel + '" not found in storage.', 'error');
    return;
  }

  const safeName = selectedLabel.replace(/[^a-z0-9_-]/gi, '_');
  triggerDownload(toExport, 'strategy_' + safeName + '_' + Date.now() + '.json');
  showToast('Exported "' + selectedLabel + '".');
}

// ── 6. Export All ───────────────────────────────────────────────────────────
function exportAll() {
  const strategies = getStrategies();
  if (!strategies.length) {
    showToast('Nothing to export \u2014 storage is empty.', 'error');
    return;
  }
  triggerDownload(strategies, 'strategies_all_' + Date.now() + '.json');
  showToast('Exported ' + strategies.length + ' strateg' + (strategies.length === 1 ? 'y' : 'ies') + '.');
}

// ── 7. Import ───────────────────────────────────────────────────────────────
function triggerImport() {
  fileInput.value = '';
  fileInput.click();
}

const ALLOWED_DO_TYPES = new Set([
  'stop',
  'addToAmount',
  'decreaseWinChanceBy',
  'increaseWinChanceBy',
  'decreaseByPercentage',
  'increaseByPercentage',
  'resetWinChance',
  'resetAmount',
  'switchOverUnder',
  'setWinChance',
  'setAmount',
  'subtractFromWinChance',
  'addToWinChance',
  'subtractFromAmount'
]);

const ALLOWED_PROFIT_TYPES = new Set(['profit', 'balance', 'loss']);

function passesWhitelist(block) {
  if (!block.do || !ALLOWED_DO_TYPES.has(block.do.type)) return false;
  if (block.on && block.on.profitType !== undefined && !ALLOWED_PROFIT_TYPES.has(block.on.profitType)) return false;
  return true;
}

function transformStrategy(data) {
  const rawBlocks = data.options.conditions.blocks;
  if (!rawBlocks.every(passesWhitelist)) return null;
  const blocks = rawBlocks.map(function(block) {
    const { id, type, on, do: doAction } = block;
    return { id, type, on, do: doAction };
  });
  return { label: data.name, blocks };
}

function handleImport(e) {
  const files = Array.from(e.target.files);
  if (!files.length) return;

  const fileReads = files.map(function(file) {
    return new Promise(function(resolve, reject) {
      const reader = new FileReader();
      reader.onload = function(ev) {
        try {
          const parsed = JSON.parse(ev.target.result);
          resolve(Array.isArray(parsed) ? parsed : [parsed]);
        } catch (_) {
          reject(new Error('Invalid JSON in file "' + file.name + '"'));
        }
      };
      reader.onerror = function() { reject(new Error('Could not read "' + file.name + '"')); };
      reader.readAsText(file);
    });
  });

  Promise.all(fileReads).then(function(results) {
    const rawArr = results.flat();
    try {

      const arr = rawArr.reduce(function(acc, s) {
        const hasBlocks =
          (s.options && s.options.conditions && Array.isArray(s.options.conditions.blocks)) ||
          Array.isArray(s.blocks);

        if (!hasBlocks) return acc; // skip entries with no "blocks"

        try {
          if (s.name && s.options && s.options.conditions && s.options.conditions.blocks) {
            const transformed = transformStrategy(s);
            if (transformed) acc.push(transformed);
          } else {
            // Already in stored shape — reject if any block fails the whitelist
            if ((s.blocks || []).every(passesWhitelist)) acc.push(s);
          }
        } catch (transformErr) {
          throw new Error('Failed to transform strategy "' + (s.name || s.label || '?') + '": ' + transformErr.message);
        }
        return acc;
      }, []);

      if (!arr.length) {
        showToast('No valid strategies found.', 'error');
        return;
      }

      const existing = getStrategies();
      const existingLabels = new Set(existing.map(function(s) { return s.label; }));
      let added = 0;
      let skipped = 0;

      arr.forEach(function(s) {
        if (existingLabels.has(s.label)) {
          skipped++;
        } else {
          existing.push(s);
          existingLabels.add(s.label);
          added++;
        }
      });

      saveStrategies(existing);
      const parts = [];
      if (added)   parts.push('Imported ' + added + ' new strateg' + (added === 1 ? 'y' : 'ies'));
      if (skipped) parts.push(skipped + ' already exist' + (skipped === 1 ? 's' : '') + ' — skipped');
      showToast(parts.length ? parts.join(', ') + '.' : 'Nothing to import.');

      document.dispatchEvent(new CustomEvent('strategiesUpdated', {
        detail: { strategies: getStrategies() }
      }));

    } catch (err) {
      showToast(err.message || 'Import failed.', 'error');
    }
  }).catch(function(err) {
    showToast(err.message || 'Could not read file.', 'error');
  });
}

// ── 8. Wire events (once) ───────────────────────────────────────────────────
actionsWrapper.querySelector('#strategyImportBtn').addEventListener('click', triggerImport);
actionsWrapper.querySelector('#strategyExportSelectedBtn').addEventListener('click', exportSelected);
actionsWrapper.querySelector('#strategyExportAllBtn').addEventListener('click', exportAll);
fileInput.addEventListener('change', handleImport);

// ── 9. Mount only on /casino/games/dice ─────────────────────────────────────
const DICE_PATH = /^\/casino\/games\/stake-dice\/?$/;

function isDicePage() {
  return DICE_PATH.test(location.pathname);
}

function sync() {
  //if (!isDicePage()) {
    //if (actionsWrapper.isConnected) actionsWrapper.remove();
    //return;
  //}
  const sidebar = document.querySelector('.game-sidebar');
  if (sidebar && actionsWrapper.parentElement !== sidebar) {
    sidebar.appendChild(actionsWrapper);
  }
}

// Debounce to one run per frame
let scheduled = false;
function scheduleSync() {
  if (scheduled) return;
  scheduled = true;
  requestAnimationFrame(function() {
    scheduled = false;
    sync();
  });
}

// Covers initial load, late rendering, re-renders and SPA navigation
new MutationObserver(scheduleSync).observe(document.documentElement, {
  childList: true,
  subtree: true
});
window.addEventListener('popstate', scheduleSync);

sync();
