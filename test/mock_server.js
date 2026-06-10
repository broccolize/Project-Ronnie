/**
 * Ronnie Mock API Server
 * Run: node test/mock_server.js   (from project root)
 * Then open: http://localhost:3000
 *
 * Simulates every endpoint the ESP32 web server exposes so you can
 * fully test the portal UI without any hardware.
 * Uses the actual data/ files as initial state, all changes are in-memory.
 */
const http = require('http');
const fs   = require('fs');
const path = require('path');

const PORT     = 3000;
const DATA_DIR = path.join(__dirname, '..', 'data');

// ── State (loaded from data/ files, mutations kept in memory) ──────────────

let config    = {};
let alarms    = { alarms: [] };
let reminders = { reminders: [] };

function loadInitialState() {
    const read = (file, fallback) => {
        try { return JSON.parse(fs.readFileSync(path.join(DATA_DIR, file), 'utf8')); }
        catch { return fallback; }
    };
    config    = read('config.json',    { city:'London', country:'', timezone_offset:0, owm_key:'', tts_key:'', volume:25, tasks:[] });
    alarms    = read('alarms.json',    { alarms: [] });
    reminders = read('reminders.json', { reminders: [] });
    if (!config.tasks) config.tasks = [];
}

loadInitialState();

// ── Helpers ────────────────────────────────────────────────────────────────

function readBody(req) {
    return new Promise(resolve => {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => resolve(body));
    });
}

function json(res, data, status = 200) {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(data, null, 2));
}

function text(res, msg, status = 200) {
    res.writeHead(status, { 'Content-Type': 'text/plain' });
    res.end(msg);
}

function log(method, url, extra = '') {
    const ts = new Date().toTimeString().slice(0, 8);
    const colour = method === 'GET' ? '\x1b[36m' : '\x1b[33m';
    console.log(`  ${colour}${method}\x1b[0m \x1b[90m${ts}\x1b[0m  ${url.padEnd(20)} ${extra}`);
}

// ── Server ─────────────────────────────────────────────────────────────────

const server = http.createServer(async (req, res) => {
    const url    = req.url.split('?')[0];
    const method = req.method;

    // CORS (required when browser JS fetches from localhost)
    res.setHeader('Access-Control-Allow-Origin',  '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    if (method === 'OPTIONS') { res.writeHead(200); res.end(); return; }

    // ── GET / → serve index.html ──────────────────────────────────────────
    if (url === '/' && method === 'GET') {
        log(method, url);
        const html = fs.readFileSync(path.join(DATA_DIR, 'index.html'), 'utf8');
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(html);
        return;
    }

    // ── /config ───────────────────────────────────────────────────────────
    if (url === '/config' && method === 'GET') {
        log(method, url);
        json(res, config);
        return;
    }
    if (url === '/config' && method === 'POST') {
        const incoming = JSON.parse(await readBody(req));
        Object.assign(config, incoming);
        if (!config.tasks) config.tasks = [];  // never clobber tasks
        log(method, url, `city="${config.city}" tts_key=${config.tts_key ? '[set]' : '[empty]'}`);
        text(res, 'Config saved. Restart Ronnie.');
        return;
    }

    // ── /alarms ───────────────────────────────────────────────────────────
    if (url === '/alarms' && method === 'GET') {
        log(method, url, `${alarms.alarms.length} alarm(s)`);
        json(res, alarms);
        return;
    }
    if (url === '/alarms' && method === 'POST') {
        alarms = JSON.parse(await readBody(req));
        log(method, url, `saved ${alarms.alarms.length} alarm(s)`);
        text(res, 'Saved');
        return;
    }

    // ── /reminders ────────────────────────────────────────────────────────
    if (url === '/reminders' && method === 'GET') {
        log(method, url, `${(reminders.reminders||[]).length} reminder(s)`);
        json(res, reminders);
        return;
    }
    if (url === '/reminders' && method === 'POST') {
        reminders = JSON.parse(await readBody(req));
        log(method, url, `saved ${(reminders.reminders||[]).length} reminder(s)`);
        text(res, 'Saved');
        return;
    }

    // ── /tasks ────────────────────────────────────────────────────────────
    if (url === '/tasks' && method === 'GET') {
        log(method, url, `${config.tasks.length} task(s)`);
        json(res, config.tasks);
        return;
    }
    if (url === '/tasks' && method === 'POST') {
        const { label, text: taskText, mp3 } = JSON.parse(await readBody(req));
        if (!label || !taskText) { text(res, 'Missing label or text', 400); return; }
        if (config.tasks.find(t => t.label === label)) {
            text(res, `Task "${label}" already exists`, 400); return;
        }
        config.tasks.push({ label, text: taskText, mp3: mp3 || 10, enrolled: false });
        log(method, url, `added task "${label}" → "${taskText}"`);
        text(res, 'Task saved. Now enroll a voice sample.');
        return;
    }

    // ── /tasks/delete ─────────────────────────────────────────────────────
    if (url === '/tasks/delete' && method === 'POST') {
        const { label } = JSON.parse(await readBody(req));
        const before = config.tasks.length;
        config.tasks = config.tasks.filter(t => t.label !== label);
        log(method, url, `deleted "${label}" (${before} → ${config.tasks.length} tasks)`);
        text(res, 'Deleted');
        return;
    }

    // ── /enroll ───────────────────────────────────────────────────────────
    if (url === '/enroll' && method === 'POST') {
        const { label } = JSON.parse(await readBody(req));
        log(method, url, `starting enrollment for "${label}"`);
        console.log(`\n  \x1b[35m🎙  ENROLL\x1b[0m  Simulating recording for word \x1b[1m"${label}"\x1b[0m`);
        console.log(`             [mock] Playing "Say the word clearly after the beep..."`);
        console.log(`             [mock] Beep!`);
        console.log(`             [mock] Recording 1 second of audio...`);

        // Mark as enrolled after 3 s to simulate the hardware flow
        setTimeout(() => {
            const task = config.tasks.find(t => t.label === label);
            if (task) {
                task.enrolled = true;
                console.log(`\n  \x1b[32m✓ "${label}" enrolled!\x1b[0m  Refresh the Tasks tab to see the updated status.\n`);
            } else {
                console.log(`\n  \x1b[31m✗ Task "${label}" not found in task list\x1b[0m\n`);
            }
        }, 3000);

        text(res, 'Enrollment started. Ronnie will beep — say the word now.');
        return;
    }

    // ── 404 ───────────────────────────────────────────────────────────────
    log(method, url, '\x1b[31m404\x1b[0m');
    text(res, `Not found: ${method} ${url}`, 404);
});

server.listen(PORT, () => {
    console.log('\n\x1b[1m🤖 Ronnie Mock Portal Server\x1b[0m');
    console.log('─'.repeat(40));
    console.log(`  URL:   \x1b[36mhttp://localhost:${PORT}\x1b[0m`);
    console.log(`  Data:  ${DATA_DIR}`);
    console.log('\n  Endpoints available:');
    [
        'GET  /', 'GET  /config', 'POST /config',
        'GET  /alarms', 'POST /alarms',
        'GET  /reminders', 'POST /reminders',
        'GET  /tasks', 'POST /tasks',
        'POST /tasks/delete', 'POST /enroll'
    ].forEach(e => console.log('    ' + e));
    console.log('\n  Open the URL in your browser. All changes are in-memory only.');
    console.log('─'.repeat(40) + '\n');
});
