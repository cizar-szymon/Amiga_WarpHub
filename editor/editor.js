/* WARP HUB - web editor of catalog/games/<key>.txt (format: tools/game_files.py).
 * Reads the game files through the GitHub API, writes one commit with the
 * game file and its picture; GitHub Actions (catalog.yml) then builds
 * catalog/catalog.txt, which WARP HUB on the Amiga reads.
 * The token stays in this browser (sessionStorage, or localStorage when
 * "remember" is ticked) and is sent only to api.github.com. */
'use strict';

const OWNER = 'cizar-szymon';
const REPO = 'Amiga_WarpHub';
const BRANCH = 'main';
const GAMES = 'catalog/games';
const IMAGES = 'catalog/images';
const API = 'https://api.github.com';
const LANGS = ['en', 'pl', 'de'];
const TAGS = [
  ['apps', 'Programy'], ['warp1260', 'Warp 560/1260'], ['warp1240', 'Warp 1240'],
  ['warp1260x', 'Warp 1260X'], ['warp7060', 'Warp 7060'], ['ppc', 'PowerPC'],
  ['other', 'Inne'],
];
const KEY_OK = /^[a-z0-9][a-z0-9_-]{0,30}$/;
const REPO_OK = /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/;
const W = 224, H = 140;

const $ = (id) => document.getElementById(id);
let games = [];          // [{key, sha, game}]
let current = null;      // {key, sha, game, isNew}
let newPicture = null;   // base64 PNG of a picked picture
let pickedImage = null;  // the picked picture (Image)
let formApproved = null; // {version, commit} of the form, or null

/* --- the token ------------------------------------------------------------ */

function store(remember) { return remember ? localStorage : sessionStorage; }

function loadToken() {
  try {
    const t = localStorage.getItem('warphub-token');
    if (t) { $('token').value = t; $('remember').checked = true; return; }
    $('token').value = sessionStorage.getItem('warphub-token') || '';
  } catch (e) { /* storage blocked: type it each time */ }
}

function saveToken() {
  try {
    localStorage.removeItem('warphub-token');
    sessionStorage.removeItem('warphub-token');
    const t = $('token').value.trim();
    if (t) store($('remember').checked).setItem('warphub-token', t);
  } catch (e) { /* ignore */ }
}

/* --- GitHub API ------------------------------------------------------------ */

async function gh(method, path, body) {
  const headers = { 'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' };
  const token = $('token').value.trim();
  if (token) headers['Authorization'] = 'Bearer ' + token;
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  const r = await fetch(API + path, { method, headers, cache: 'no-store',
                                      body: body === undefined ? undefined : JSON.stringify(body) });
  if (r.status === 404 && method === 'GET') return null;
  if (!r.ok) {
    let msg = '';
    try { msg = (await r.json()).message || ''; } catch (e) { /* no body */ }
    const err = new Error(`GitHub ${r.status}: ${msg}`);
    err.status = r.status;
    throw err;
  }
  return r.status === 204 ? null : r.json();
}

function b64ToText(b64) {
  const bin = atob(b64.replace(/\n/g, ''));
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return new TextDecoder('utf-8').decode(bytes);
}

/* --- the game file (the same rules as tools/game_files.py) ----------------- */

function parse(key, text) {
  const g = { key, name: {}, desc: {}, tags: [], req: {}, order: 1000, fixed: [] };
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const sp = line.indexOf(' ');
    const word = sp < 0 ? line : line.slice(0, sp);
    const rest = sp < 0 ? '' : line.slice(sp + 1).trim();
    if (word === 'NAME' || word === 'DESC' || word === 'REQ') {
      const s2 = rest.indexOf(' ');
      const lang = s2 < 0 ? rest : rest.slice(0, s2);
      const value = s2 < 0 ? '' : rest.slice(s2 + 1).trim();
      if (!LANGS.includes(lang) || !value) throw new Error(`${key}: zła linia: ${line}`);
      if (word === 'REQ') (g.req[lang] = g.req[lang] || []).push(value);
      else g[word.toLowerCase()][lang] = value;
    } else if (word === 'TAGS') {
      g.tags = rest.split(/[,\s]+/).filter(Boolean);
    } else if (word === 'REPO') {
      g.repo = rest;
    } else if (word === 'BRANCH') {
      g.branch = rest;
    } else if (word === 'APPROVED') {
      const p = rest.split(/\s+/);
      if (p.length !== 2 || !/^[0-9a-f]{40}$/.test(p[1])) throw new Error(`${key}: zła linia: ${line}`);
      g.approved = { version: p[0], commit: p[1] };
    } else if (word === 'ORDER') {
      g.order = parseInt(rest, 10);
    } else if (word === 'FLAT' && !rest) {
      g.flat = true;
    } else if (word === 'LOCAL' || word === 'FULLNAME' || word === 'IMAGE-SOURCE') {
      g.fixed.push(line);          // PC only: kept as it is
    } else {
      throw new Error(`${key}: nieznana linia: ${line}`);
    }
  }
  return g;
}

function write(g) {
  const out = [`# WARP HUB: ${g.name.en || g.key} (format: tools/game_files.py)`];
  for (const l of LANGS) if (g.name[l]) out.push(`NAME ${l} ${g.name[l]}`);
  for (const l of LANGS) if (g.desc[l]) out.push(`DESC ${l} ${g.desc[l]}`);
  if (g.tags.length) out.push('TAGS ' + g.tags.join(','));
  for (const l of LANGS) for (const r of g.req[l] || []) out.push(`REQ ${l} ${r}`);
  if (g.repo) out.push('REPO ' + g.repo);
  if (g.branch) out.push('BRANCH ' + g.branch);
  if (g.approved) out.push(`APPROVED ${g.approved.version} ${g.approved.commit}`);
  if (g.flat) out.push('FLAT');
  out.push('ORDER ' + g.order);
  const local = g.fixed.filter((l) => l.startsWith('LOCAL '));
  const full = g.fixed.filter((l) => l.startsWith('FULLNAME '));
  const src = g.fixed.filter((l) => l.startsWith('IMAGE-SOURCE '));
  out.push(...local, ...full, ...src);
  return out.join('\n') + '\n';
}

/* --- the form ---------------------------------------------------------------- */

const oneLine = (s) => s.replace(/\s+/g, ' ').trim();

function fromForm() {
  const g = { key: $('key').value.trim(), name: {}, desc: {}, req: {}, tags: [],
              order: parseInt($('order').value || '1000', 10),
              repo: $('repo').value.trim(), branch: $('branch').value.trim(),
              fixed: current ? current.game.fixed : [] };
  if (formApproved) g.approved = formApproved;
  if (current && current.game.flat) g.flat = true;   // kept as it is
  for (const l of LANGS) {
    const n = oneLine($('name_' + l).value);
    if (n && (l === 'en' || n !== oneLine($('name_en').value))) g.name[l] = n;
    const d = oneLine($('desc_' + l).value);
    if (d) g.desc[l] = d;
    const r = $('req_' + l).value.split(/\r?\n/).map(oneLine).filter(Boolean);
    if (r.length) g.req[l] = r;
  }
  for (const [t] of TAGS) if ($('tag_' + t).checked) g.tags.push(t);
  return g;
}

function problems(g) {
  const p = [];
  if (!KEY_OK.test(g.key)) p.push('Klucz: małe litery, cyfry, _ i - (do 31 znaków), na początku litera lub cyfra.');
  if (current && current.isNew && games.some((x) => x.key === g.key)) p.push(`Klucz „${g.key}” już jest.`);
  if (!g.name.en) p.push('Brak nazwy po angielsku.');
  if (!Number.isInteger(g.order) || g.order < 0) p.push('Miejsce w galerii: liczba od 0.');
  if (g.repo && !REPO_OK.test(g.repo)) p.push('Repo: w postaci właściciel/nazwa, np. cizar-szymon/Amiga_game_Settlers.');
  if (g.branch && !/^[A-Za-z0-9_.\/-]+$/.test(g.branch)) p.push('Gałąź: bez spacji.');
  return p;
}

function toForm(entry) {
  const g = entry.game;
  $('key').value = g.key;
  $('key').readOnly = !entry.isNew;
  $('order').value = g.order;
  for (const l of LANGS) {
    $('name_' + l).value = g.name[l] || '';
    $('desc_' + l).value = g.desc[l] || '';
    $('req_' + l).value = (g.req[l] || []).join('\n');
  }
  for (const [t] of TAGS) $('tag_' + t).checked = g.tags.includes(t);
  $('repo').value = g.repo || '';
  $('branch').value = g.branch || '';
  formApproved = g.approved || null;
  showApproved();
  $('checkout').textContent = '';
  $('fixedbox').hidden = g.fixed.length === 0;
  $('fixed').textContent = g.fixed.join('\n');
  $('file').value = '';
  newPicture = null;
  pickedImage = null;
  $('delete').hidden = entry.isNew;
  $('title').textContent = entry.isNew ? 'Nowa gra / program' : (g.name.en || g.key);
  $('form').hidden = false;
  status('', '');
  clearPicture();
  if (!entry.isNew) showStoredPicture(g.key);
  preview();
}

function preview() {
  const g = fromForm();
  $('fname').textContent = g.key || '…';
  $('out').textContent = write(g);
  $('tile_name').textContent = g.name.pl || g.name.en || '–';
}

function status(kind, text) {
  const s = $('status');
  s.className = kind;
  s.textContent = text;
}

/* --- the list --------------------------------------------------------------- */

async function loadList() {
  const ul = $('games');
  ul.innerHTML = '<li class="hint">wczytywanie…</li>';
  try {
    const list = await gh('GET', `/repos/${OWNER}/${REPO}/contents/${GAMES}?ref=${BRANCH}`) || [];
    const files = list.filter((f) => f.type === 'file' && f.name.endsWith('.txt'));
    games = await Promise.all(files.map(async (f) => {
      const d = await gh('GET', `/repos/${OWNER}/${REPO}/contents/${f.path}?ref=${BRANCH}`);
      const key = f.name.slice(0, -4);
      return { key, sha: d.sha, game: parse(key, b64ToText(d.content)) };
    }));
    games.sort((a, b) => a.game.order - b.game.order || a.key.localeCompare(b.key));
    drawList();
  } catch (e) {
    ul.innerHTML = '';
    const li = document.createElement('li');
    li.className = 'hint';
    li.textContent = 'Nie udało się wczytać listy: ' + e.message +
      (e.status === 403 ? ' (limit GitHuba bez tokenu – wpisz token)' : '');
    ul.appendChild(li);
  }
}

function drawList() {
  const ul = $('games');
  ul.innerHTML = '';
  for (const e of games) {
    const li = document.createElement('li');
    if (current && !current.isNew && current.key === e.key) li.className = 'sel';
    const n = document.createElement('span');
    n.textContent = e.game.name.pl || e.game.name.en;
    const k = document.createElement('span');
    k.className = 'k';
    k.textContent = e.key;
    li.append(n, k);
    li.addEventListener('click', () => {
      current = { ...e, isNew: false };
      toForm(current);
      drawList();
    });
    ul.appendChild(li);
  }
}

/* --- the picture ----------------------------------------------------------- */

function clearPicture() {
  const c = $('pic').getContext('2d');
  c.fillStyle = '#000';
  c.fillRect(0, 0, W, H);
}

async function showStoredPicture(key) {
  try {
    const d = await gh('GET', `/repos/${OWNER}/${REPO}/contents/${IMAGES}/${key}.png?ref=${BRANCH}`);
    if (!d || !current || current.key !== key || newPicture) return;
    const img = new Image();
    img.onload = () => { if (!newPicture) $('pic').getContext('2d').drawImage(img, 0, 0, W, H); };
    img.src = 'data:image/png;base64,' + d.content.replace(/\n/g, '');
  } catch (e) { /* no picture yet */ }
}

function drawPicked() {
  if (!pickedImage) return;
  const img = pickedImage, mode = $('fit').value;
  const c = $('pic').getContext('2d');
  c.fillStyle = '#000';
  c.fillRect(0, 0, W, H);
  let k;
  if (mode === 'pixel') {
    k = Math.max(1, Math.floor(Math.min(W / img.width, H / img.height)));
    if (img.width > W || img.height > H) k = Math.min(W / img.width, H / img.height);
  } else if (mode === 'cover') {
    k = Math.max(W / img.width, H / img.height);
  } else {
    k = Math.min(W / img.width, H / img.height);
  }
  c.imageSmoothingEnabled = mode !== 'pixel';
  c.imageSmoothingQuality = 'high';
  const w = img.width * k, h = img.height * k;
  c.drawImage(img, (W - w) / 2, (H - h) / 2, w, h);
  newPicture = $('pic').toDataURL('image/png').split(',')[1];
}

/* --- writing: one commit (Git Data API) --------------------------------------- */

async function commit(message, changes) {
  // changes: [{path, text} | {path, base64} | {path, remove: true}]
  for (let attempt = 0; attempt < 2; attempt++) {
    const ref = await gh('GET', `/repos/${OWNER}/${REPO}/git/ref/heads/${BRANCH}`);
    const head = ref.object.sha;
    const base = await gh('GET', `/repos/${OWNER}/${REPO}/git/commits/${head}`);
    const tree = [];
    for (const c of changes) {
      if (c.remove) {
        tree.push({ path: c.path, mode: '100644', type: 'blob', sha: null });
        continue;
      }
      const blob = await gh('POST', `/repos/${OWNER}/${REPO}/git/blobs`,
        c.text !== undefined ? { content: c.text, encoding: 'utf-8' }
                             : { content: c.base64, encoding: 'base64' });
      tree.push({ path: c.path, mode: '100644', type: 'blob', sha: blob.sha });
    }
    const t = await gh('POST', `/repos/${OWNER}/${REPO}/git/trees`, { base_tree: base.tree.sha, tree });
    const cm = await gh('POST', `/repos/${OWNER}/${REPO}/git/commits`,
                        { message, tree: t.sha, parents: [head] });
    try {
      await gh('PATCH', `/repos/${OWNER}/${REPO}/git/refs/heads/${BRANCH}`, { sha: cm.sha });
      return cm.sha;
    } catch (e) {
      if (e.status !== 422 || attempt === 1) throw e;   // main moved meanwhile: once more
    }
  }
  return null;
}

async function exists(path) {
  return (await gh('GET', `/repos/${OWNER}/${REPO}/contents/${path}?ref=${BRANCH}`)) !== null;
}

async function save(ev) {
  ev.preventDefault();
  const g = fromForm();
  const p = problems(g);
  if (p.length) { status('bad', p.join('\n')); return; }
  if (!$('token').value.trim()) { status('bad', 'Wpisz token (po lewej), żeby zapisać zmiany.'); return; }
  saveToken();
  $('save').disabled = true;
  status('info', 'Wysyłanie…');
  try {
    if (!current.isNew && current.sha) {
      // somebody else (or Claude, or a second tab) saved this game since the
      // form was loaded: saving now would undo that change
      const now = await gh('GET', `/repos/${OWNER}/${REPO}/contents/${GAMES}/${g.key}.txt?ref=${BRANCH}`);
      if (now && now.sha !== current.sha) {
        status('bad', 'Ten plik zmienił się na GitHubie, odkąd otworzyłeś formularz (inna osoba, ' +
               'inna karta przeglądarki albo poprawka). Nic nie zapisano.\n' +
               'Kliknij ↻ przy liście, otwórz grę jeszcze raz i wprowadź zmianę ponownie.');
        return;
      }
    }
    const changes = [{ path: `${GAMES}/${g.key}.txt`, text: write(g) }];
    if (newPicture) changes.push({ path: `${IMAGES}/${g.key}.png`, base64: newPicture });
    const what = current.isNew ? 'add' : 'update';
    const sha = await commit(`catalog: ${what} ${g.key} (web editor)`, changes);
    current = { key: g.key, game: g, isNew: false };
    await loadList();
    current = games.find((x) => x.key === g.key) || current;
    if (current) toForm({ ...current, isNew: false });
    status('ok', `Zapisane (commit ${sha.slice(0, 7)}).\nAutomat GitHub Actions zrobi z tego catalog.txt ` +
           'w ciągu kilku minut; potem WARP HUB na Amidze pokaże zmiany po naciśnięciu Odśwież.');
    appendActionsLink();
  } catch (e) {
    status('bad', 'Nie zapisano: ' + e.message +
           (e.status === 401 ? '\nToken jest zły albo wygasł.' : '') +
           (e.status === 403 || e.status === 404 ? '\nToken nie ma prawa zapisu w Amiga_WarpHub (Contents: Read and write).' : ''));
  } finally {
    $('save').disabled = false;
  }
}

function appendActionsLink() {
  const a = document.createElement('a');
  a.href = `https://github.com/${OWNER}/${REPO}/actions/workflows/catalog.yml`;
  a.rel = 'noopener';
  a.textContent = 'Zobacz automat na GitHubie';
  $('status').append('\n', a);
}

async function remove() {
  if (!current || current.isNew) return;
  const key = current.key;
  if (!confirm(`Usunąć „${current.game.name.en}” z katalogu WARP HUB?\n` +
               'Gra zniknie z listy na Amigach; u graczy nic nie zostanie skasowane.')) return;
  if (!$('token').value.trim()) { status('bad', 'Wpisz token (po lewej), żeby zapisać zmiany.'); return; }
  saveToken();
  status('info', 'Usuwanie…');
  try {
    const changes = [{ path: `${GAMES}/${key}.txt`, remove: true }];
    if (await exists(`${IMAGES}/${key}.png`)) changes.push({ path: `${IMAGES}/${key}.png`, remove: true });
    const sha = await commit(`catalog: remove ${key} (web editor)`, changes);
    current = null;
    $('form').hidden = true;
    $('title').textContent = `Usunięto ${key} (commit ${sha.slice(0, 7)})`;
    await loadList();
  } catch (e) {
    status('bad', 'Nie usunięto: ' + e.message);
  }
}

/* --- the approved release ---------------------------------------------------- */

function versionKey(v) { return v.split('.').map((x) => parseInt(x, 10)); }

function newer(a, b) {             // a > b, as version_key in tools/make_catalog.py
  const x = versionKey(a), y = versionKey(b);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    const p = x[i] === undefined ? -1 : x[i], q = y[i] === undefined ? -1 : y[i];
    if (p !== q) return p > q;
  }
  return false;
}

/* the author's newest release now: {branch, commit, version, zip, dirs} */
async function newestRelease(repo, branchWanted) {
  const info = await gh('GET', `/repos/${repo}`);
  if (!info) throw new Error('Repo nie istnieje albo nie jest publiczne – Amiga go nie pobierze.');
  if (info.private) throw new Error('Repo jest prywatne – Amiga go nie pobierze.');
  const branch = branchWanted || info.default_branch;
  const head = await gh('GET', `/repos/${repo}/commits/${encodeURIComponent(branch)}`);
  if (!head) throw new Error(`Nie ma gałęzi ${branch}.`);
  const list = await gh('GET', `/repos/${repo}/contents/releases/amiga?ref=${head.sha}`);
  if (!list) throw new Error(`Brak releases/amiga na gałęzi ${branch}.`);
  let best = null;
  for (const f of list) {
    const m = /^([A-Za-z0-9_]+)-(\d+(?:\.\d+)+)\.zip$/.exec(f.name);
    if (m && (!best || newer(m[2], best.version))) best = { zip: f.name, version: m[2] };
  }
  if (!best) throw new Error(`Na gałęzi ${branch} nie ma releases/amiga/<Gra>-<wersja>.zip.`);
  return { branch, commit: head.sha, version: best.version, zip: best.zip,
           dirs: list.filter((f) => f.type === 'dir').map((f) => f.name) };
}

function showApproved() {
  $('approved_text').textContent = formApproved
    ? `Zatwierdzona: ${formApproved.version} (stan repo ${formApproved.commit.slice(0, 7)})`
    : 'Bez zatwierdzania: gracze dostają zawsze najnowsze wydanie autora.';
}

async function approve() {
  const repo = $('repo').value.trim();
  if (!REPO_OK.test(repo)) { status('bad', 'Najpierw wpisz repo (właściciel/nazwa).'); return; }
  status('info', 'Sprawdzanie wydania…');
  try {
    const r = await newestRelease(repo, $('branch').value.trim());
    formApproved = { version: r.version, commit: r.commit };
    showApproved();
    preview();
    status('ok', `Zatwierdzona wersja ${r.version} (${r.zip}). Zapisz grę przyciskiem „Wyślij na GitHub”.`);
  } catch (e) {
    status('bad', e.message);
  }
}

/* --- checking the repo of a game ------------------------------------------- */

async function checkRepo() {
  const repo = $('repo').value.trim();
  const out = $('checkout');
  if (!REPO_OK.test(repo)) { out.textContent = 'Wpisz repo w postaci właściciel/nazwa.'; return; }
  out.textContent = 'sprawdzanie…';
  try {
    const r = await newestRelease(repo, $('branch').value.trim());
    let t = `✓ gałąź ${r.branch}: najnowsze wydanie ${r.zip}; szuflady: ${r.dirs.join(', ') || 'brak!'}`;
    if (formApproved && formApproved.version !== r.version) {
      t += `\n⚠ autor wydał ${r.version}, zatwierdzona jest ${formApproved.version} – ` +
           'sprawdź nową wersję i kliknij „Zatwierdź obecne wydanie”.';
    }
    out.textContent = t;
  } catch (e) {
    out.textContent = '✗ ' + e.message;
  }
}

/* --- start ------------------------------------------------------------------- */

function init() {
  const box = $('tags');
  for (const [t, label] of TAGS) {
    const l = document.createElement('label');
    const c = document.createElement('input');
    c.type = 'checkbox';
    c.id = 'tag_' + t;
    l.append(c, ' ' + label);
    box.appendChild(l);
  }
  loadToken();
  $('remember').addEventListener('change', saveToken);
  $('token').addEventListener('change', saveToken);
  $('form').addEventListener('input', preview);
  $('form').addEventListener('change', preview);
  $('form').addEventListener('submit', save);
  $('delete').addEventListener('click', remove);
  $('check').addEventListener('click', checkRepo);
  $('approve').addEventListener('click', approve);
  $('unapprove').addEventListener('click', () => { formApproved = null; showApproved(); preview(); });
  $('reload').addEventListener('click', loadList);
  $('new').addEventListener('click', () => {
    const last = games.length ? games[games.length - 1].game.order : 0;
    current = { key: '', isNew: true,
                game: { key: '', name: {}, desc: {}, tags: [], req: {}, order: last + 10, fixed: [] } };
    toForm(current);
    drawList();
    $('key').focus();
  });
  $('file').addEventListener('change', () => {
    const f = $('file').files[0];
    if (!f) return;
    const url = URL.createObjectURL(f);
    const img = new Image();
    img.onload = () => { pickedImage = img; drawPicked(); URL.revokeObjectURL(url); };
    img.onerror = () => status('bad', 'Tego pliku nie da się otworzyć jako obrazka.');
    img.src = url;
  });
  $('fit').addEventListener('change', drawPicked);
  loadList();
}

init();
