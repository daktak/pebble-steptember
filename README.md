# Pebble-Steptember

Daily sync your Pebble steps to [Steptember](https://www.steptember.org.au) at your chosen time (default 23:00) via health `steps 00:00→sync` and `Clay` config.

## Features

- **Watchapp** (Pebble Time / Basalt, Chalk, Diorite, Emery — no `aplite`) with `health` + `wakeup` + `configurable`
- **Clay** (`@rebble/clay`) for `EMAIL`, `PASSWORD` (`base-64` obfuscation, no security expectation), `SYNC_HOUR`/`SYNC_MINUTE` (default `23:00`)
- **Wakeup** (`wakeup_schedule`) launches even if app not foreground (`APP_LAUNCH_WAKEUP` → auto `health_service_sum_today` → `AppMessage STEPS/STEPS_DATE` → phone `PROXY` → Steptember). Once-per-day (`LAST_SYNC_DATE`), `SELECT` forces manual sync, `QUEUED_PENDING` retry every 5 min, auto-close 2s after `OK` if wakeup-launched
- **Proxy** (`proxy.js` Node 26.3, no deps) mirrors `log_steptember.py` `fetchWithCookies` jar (`CSRFToken`/`history_id`/`web_validatesteps`/`web_addactivity`) to bypass `PKJS` `file://` `CORS *`/`SameSite=Lax` block; no vibration

## Quick Start

1. **Phone:** Install Rebble app, pair watch, `pebble install --emulator basalt build/pebble-steptember.pbw --logs`
2. **Watch:** Open `Steptember` → `Clay` config (gear) → set `EMAIL`/`PASSWORD`/`SYNC_HOUR`/`SYNC_MINUTE` → Save (pushes `SYNC_HOUR/MINUTE` via `AppMessage`, `wakeup` rescheduled, handles `TUPLE_CSTRING`/`int32`)
3. **Steps:** At `SYNC_TIME` (or `SELECT`) watch queues `steps` + `YYYY-MM-DD` → phone `POST PROXY/log` → Steptember `Add your steps manually` (not activity)

## Proxy — Stable Nginx Sub-Location

The watch `PKJS` `file://` cannot directly `POST https://www.steptember.org.au` due to `CORS *` without `Allow-Credentials` and `SameSite=Lax` `ci_session`/`member_token` (Python `requests.Session` handles server-side, `PKJS` does not). `proxy.js` runs server-side (`Node 26.3` or `Python 3.14`) and is exposed via your public `nginx 1.31.3` as a **sub-location** under your existing domain (no new `server_name`/`cert`).

### A. Run `proxy.js` on Nginx Host

```bash
scp proxy.js <user>@<host>:/opt/pebble-steptember/proxy.js
# proxy.js listens 127.0.0.1:3000, no npm deps, uses http/https built-in
sudo tee /etc/systemd/system/pebble-steptember-proxy.service >/dev/null <<'EOF'
[Unit]
Description=Pebble Steptember Proxy
After=network.target
[Service]
ExecStart=/usr/bin/node /opt/pebble-steptember/proxy.js
Restart=always
Environment=PORT=3000
User=www-data
[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now pebble-steptember-proxy.service
curl http://127.0.0.1:3000/health # → ok
```

Alternative **Python 3.14** backend: `Flask`/`gunicorn` wrapping `log_steptember.py` → `gunicorn -w 2 -b 127.0.0.1:3000 proxy:app`.

### B. Nginx Sub-Location (Add to Existing `server { listen 443 ssl; server_name <your-domain>; }`)

```nginx
# Inside existing server { listen 443 ssl http2; server_name <your-domain>; ssl_certificate ...; }

# Pebble Steptember proxy — https://<your-domain>/steptember/log
location /steptember/ {
    rewrite ^/steptember/(.*)$ /$1 break;  # /steptember/log → /log for proxy.js

    proxy_pass http://127.0.0.1:3000;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;

    # Pebble PKJS file:// CORS
    add_header Access-Control-Allow-Origin * always;
    add_header Access-Control-Allow-Methods "GET, POST, OPTIONS" always;
    add_header Access-Control-Allow-Headers "Content-Type, Authorization" always;
    if ($request_method = OPTIONS) { return 204; }
}
# Health: https://<your-domain>/steptember/health → ok
```

```bash
sudo nginx -t && sudo systemctl reload nginx
```

### C. Point Watch to Stable Proxy

In `src/pkjs/index.js:11`:

```js
var PROXY = "https://<your-domain>/steptember/log";
```

`pebble build` → `pebble install` → test:

```bash
curl -X POST https://<your-domain>/steptember/log \
  -H "Content-Type: application/json" \
  -d '{"email":"you@example.com","password":"...","steps":7337,"date":"2026-09-05"}'
# → {"success":true,"result":{"ok":true}}
```

Set Clay `20:46` (future), close watchapp (Back) → `log.txt` `wakeup fired` → `proxy resp 200` via `<your-domain>` → `OK` → auto-close (2s) without `SELECT`.

## Development

- `pebble build` (requires `pebble-sdk` 4.33.1, `watchapp` `wakeup`, no `worker_src` — `wakeup` in main app, not `AppWorker`, so no `Background Apps` entry)
- `proxy.js` local dev: `node proxy.js` → `http://localhost:3000/health`

## Notes

- `aplite` dropped (no `wakeup`/`health` on `2.x`), `Node 26.3` `proxy.js` handles `SameSite=Lax` server-side, `base-64` is obfuscation only.
- `schedule_wakeup` validates `h 0-23/m 0-59` (fixes `TUPLE_CSTRING "20"` vs `int32` garbage `369111090` → `id -4` `E_INVALID_ARGUMENT`), `target = today_start + h*3600+m*60` (`time_start_of_today()` local), `+86400` if past.
