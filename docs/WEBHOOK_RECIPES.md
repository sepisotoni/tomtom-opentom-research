# Webhook recipes (Face Studio -> TomTom notification)

Ready-to-use ways to push a short message onto the TomTom screen through the Face Studio webhook
(Live tab -> Webhook). **Status: written from reading `src/core/webhook.cpp`; none of these recipes
has been run yet.** Run `curl -sS http://127.0.0.1:18750/health` (expects `ok`) and then the first `curl` example below, and report anything that differs.

## What the webhook accepts

| Item | Value |
| --- | --- |
| URL | `POST http://127.0.0.1:18750/notify` (port is configurable in the app) |
| Auth | `Authorization: Bearer <token>` or `X-Webhook-Token: <token>`. Token is always required (>= 16 chars; the app generates 128 random bits). |
| JSON body | `{"text": "Build finished", "ttl": 10}`. Text key may also be `message` or `content` (so Discord-style `{"content": "..."}` works); TTL key may also be `seconds`. Content-Type must contain `json`. |
| Plain body | any other Content-Type: the body is the text; TTL via `?ttl=10` |
| Text limit | 32 printable ASCII characters reach the device. Longer text is shortened with `...`; accents are simplified; anything else becomes `?`; text with nothing displayable is rejected (400). |
| TTL | seconds on screen, clamped to 1-60, default 15 |
| Body limit | 1 KiB (413 above that); a `Content-Length` is required (411 without) |
| Rate limit | one accepted message per 500 ms (HTTP 429 `slow down` otherwise) |
| Other routes | `GET /health` returns `ok` without authentication; any other path gives 404 and any method other than POST on `/notify` gives 405 |
| Replies | 200 `ok`, 400 bad request/no displayable text, 401 wrong or missing token, 405, 411, 413, 429, 502 (could not reach the TomTom) |

## Security: read this before copying anything

* The listener is **loopback-only** (`127.0.0.1`). Only programs on the same PC can reach it.
* It becomes reachable from private networks (RFC 1918 / link-local) **only** if you tick **Allow private LAN** in the app.
  The token is still required.
* Traffic is **plain HTTP**: the token and message can be read by anyone who can see that network segment. Use it on a
  network you trust, never port-forward it, never put it behind a public tunnel.
* Do not paste the token into scripts you commit. The app stores it in `%APPDATA%\TomTomFaceStudio\settings.ini`
  (`[webhook]` section, key `token`). The recipes below read it from there or from a secret store.
* Everything ends at the TomTom's USB link, which is also plain text and unauthenticated. Keep it a direct USB connection.

## curl

```sh
# send a JSON notification (expects: ok)
curl -sS -X POST "http://127.0.0.1:18750/notify" \
  -H "Authorization: Bearer $TOMTOM_WEBHOOK_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"text": "Build finished", "ttl": 10}'

# Discord-style payload
curl -sS -X POST "http://127.0.0.1:18750/notify" \
  -H "Authorization: Bearer $TOMTOM_WEBHOOK_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"content": "Backup done"}'

# plain text with a TTL in the query string
curl -sS -X POST "http://127.0.0.1:18750/notify?ttl=20" \
  -H "Authorization: Bearer $TOMTOM_WEBHOOK_TOKEN" \
  -H "Content-Type: text/plain" \
  --data-binary "Coffee is ready"
```

On Windows use `curl.exe` (in PowerShell plain `curl` is an alias for `Invoke-WebRequest`). In `cmd.exe` the JSON needs
escaped quotes: `-d "{\"text\": \"Build finished\", \"ttl\": 10}"`.

## PowerShell

```powershell
$token = (Get-Content "$env:APPDATA\TomTomFaceStudio\settings.ini" |
          Where-Object { $_ -match '^token=(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)

Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:18750/notify" `
  -Headers @{ Authorization = "Bearer $token" } `
  -ContentType "application/json" `
  -Body (@{ text = "Build finished"; ttl = 10 } | ConvertTo-Json -Compress)
```

A reusable function (keep it ASCII; 429 means you sent twice within 500 ms):

```powershell
function Send-TomTom([string]$Text, [int]$Ttl = 10) {
    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:18750/notify" `
      -Headers @{ Authorization = "Bearer $script:token" } `
      -ContentType "application/json" `
      -Body (@{ text = $Text; ttl = $Ttl } | ConvertTo-Json -Compress)
}
```

## GitHub Actions

A GitHub-hosted runner can **not** reach `127.0.0.1` on your PC, and you must not expose the webhook to the internet to
make it work. Use a **self-hosted runner installed on the same PC** (loopback stays loopback). Store the token as the
repository secret `TOMTOM_WEBHOOK_TOKEN`.

```yaml
jobs:
  build:
    runs-on: self-hosted          # the Windows PC that runs Face Studio
    steps:
      # ... your build steps ...
      - name: Tell the TomTom
        if: always()
        shell: pwsh
        env:
          TOKEN: ${{ secrets.TOMTOM_WEBHOOK_TOKEN }}
          RESULT: ${{ job.status }}
        run: |
          Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:18750/notify" `
            -Headers @{ Authorization = "Bearer $env:TOKEN" } `
            -ContentType "application/json" `
            -Body (@{ text = "CI $env:RESULT"; ttl = 15 } | ConvertTo-Json -Compress)
```

If the runner is a different machine on your home LAN you would have to tick **Allow private LAN** and use the PC's LAN
address, which sends the token over plain HTTP across that LAN. Prefer the same-PC runner.

## Home Assistant

Home Assistant runs on another machine, so this needs **Allow private LAN** ticked in the app and the PC's private IP
(give the PC a fixed address). Plain HTTP over your LAN; do not use this across the internet or a VPN you do not control.

`secrets.yaml`:

```yaml
tomtom_bearer: "Bearer PASTE_TOKEN_HERE"
```

`configuration.yaml`:

```yaml
rest_command:
  tomtom_notify:
    url: "http://192.168.1.50:18750/notify"     # your PC's private address
    method: post
    content_type: "application/json"
    headers:
      Authorization: !secret tomtom_bearer
    payload: '{"text": "{{ message }}", "ttl": {{ ttl | default(10) }}}'
    timeout: 5
```

Call it from an automation:

```yaml
action:
  - service: rest_command.tomtom_notify
    data:
      message: "Front door open"
      ttl: 15
```

Keep `message` plain ASCII without double quotes or backslashes (they would break the JSON built in `payload`).

## Windows Task Scheduler

1. Save this as `C:\Tools\tomtom-notify.ps1` (no token inside; it reads the app's settings):

```powershell
param([string]$Text = "Reminder", [int]$Ttl = 15)
$token = (Get-Content "$env:APPDATA\TomTomFaceStudio\settings.ini" |
          Where-Object { $_ -match '^token=(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:18750/notify" `
  -Headers @{ Authorization = "Bearer $token" } -ContentType "application/json" `
  -Body (@{ text = $Text; ttl = $Ttl } | ConvertTo-Json -Compress)
```

2. Create the task (runs as you, only while you are logged on, so `%APPDATA%` is yours):

```bat
schtasks /Create /TN "TomTom stand-up reminder" /SC DAILY /ST 09:55 ^
  /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Tools\tomtom-notify.ps1 -Text \"Stand-up in 5 min\" -Ttl 20"
```

3. Test it: `schtasks /Run /TN "TomTom stand-up reminder"`. The webhook only works while Face Studio is running with the
   webhook enabled; otherwise the connection is refused.

## Troubleshooting

| You see | Meaning |
| --- | --- |
| connection refused | Face Studio is not running, the webhook is not enabled, or the port differs |
| 401 | wrong/missing token (press "New token" in the app invalidates old scripts) |
| 400 `no displayable text` | the text was empty or all non-ASCII (emoji, CJK) |
| 429 | two messages within 500 ms |
| 502 | the app accepted it but could not send to the TomTom (check the USB IP in the Device tab) |
