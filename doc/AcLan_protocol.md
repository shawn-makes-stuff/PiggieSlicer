# Anycubic LAN printing protocol (open, local, no cloud)

This documents the fully-local protocol used by `src/slic3r/Utils/AcLan.{hpp,cpp}`
to discover Anycubic FDM printers and send prints over the LAN — no Anycubic
cloud, account, or proprietary DLLs. Verified against an Anycubic Kobra 3
(firmware 2.4.4.3). The mechanism is model-agnostic: model/printer ids and
credentials all come from the printer at runtime; nothing is hardcoded.

## 1. Discovery (SSDP / UPnP, UDP 239.255.255.250:1900)

Send a standard `M-SEARCH` with `ST: ac:3dprinter:fdm`. Anycubic FDM printers
reply with `SERVER: ac:fdm:ssdp`, `USN: uuid:fdm:<MAC>`, and a `LOCATION`:

```
http://<ip>:18910/info?...&modelId=20024&modelName=Anycubic+Kobra+3&cn=<cn>&token=<t>&ctrlType=lan&...
```

Manual add (when multicast is blocked): just use the printer IP and call `/info`.

## 2. Printer info (HTTP, port 18910)

`GET http://<ip>:18910/info` → JSON:
`modelId, modelName, cn, usn, ip, ctrlType, ctrlInfoUrl (=/ctrl), token,
fileUploadurl (= /gcode_upload?s=<session>), rtspUrl`. `token` and the upload
`s=` session rotate per call — fetch fresh before each session.

## 3. Local credential fetch (signed /ctrl + AES)

```
ts    = current time in ms
nonce = 6 random [A-Za-z0-9]
did   = 32 random [A-Z0-9]
sign  = encodeURIComponent(encodeURIComponent( md5( md5(token[0:16]) + ts + nonce ) ))
        (md5 output is hex, so the URL-encoding is effectively identity)

POST http://<ip>:18910/ctrl?ts=<ts>&nonce=<nonce>&sign=<sign>&did=<did>   (empty body)
 -> { "code":200, "message":"success",
      "data": { "token": <local_token>, "info": <base64 AES-CBC ciphertext> } }
```

Decrypt `info`: **AES-128-CBC, PKCS7**, `key = token[16:32]` (bytes),
`iv = local_token` (bytes, padded/truncated to 16). Plaintext JSON:

```json
{ "broker": "mqtts://<ip>:9883", "deviceId": "<printerId>",
  "username": "<mqtt user>", "password": "<mqtt pass>",
  "devicecrt": "<PEM>", "devicepk": "<PEM>", "modeId": "20024", "ip": "<ip>" }
```

`deviceId` is the `printerId` used in MQTT topics. The creds are intrinsic to the
printer (persist across reboots; not account-bound) and re-fetchable any time.

## 4. Upload the file (HTTP)

`POST <fileUploadurl>` (the `/gcode_upload?s=<session>` from `/info`),
`multipart/form-data` with fields `filename=<name>` and `gcode=<file bytes>`.
A **raw `.gcode`** is accepted (no `.3mf` wrapper needed). Note the file's md5.

## 5. Start the print (MQTT, port 9883, TLS self-signed)

Connect to `<ip>:9883` over TLS (self-signed cert; `verify_none`, or mutual-TLS
with `devicecrt`/`devicepk`), MQTT 3.1.1, username/password from step 3.

Publish to `anycubic/anycubicCloud/v1/slicer/printer/<modelId>/<printerId>/print`:

```json
{ "type":"print", "action":"start", "msgid":"<uuid>", "timestamp":<ms>,
  "data": { "taskid":"-1", "filename":"<name>.gcode", "md5":"<file md5>",
            "filepath":null, "filetype":1 } }
```

For multi-color (ACE / FullSpectrum), the official client also sends
`data.ams_settings.ams_box_mapping = [{paint_index, ams_index, paint_color[rgba],
ams_color[rgba], material_type}]` mapping each paint color to an ACE slot.

## 6. Status / monitoring (MQTT)

Subscribe `anycubic/anycubicCloud/v1/printer/public/<modelId>/<printerId>/#`.
The printer publishes `…/print/report`, `…/status/report`, `…/info/report`,
`…/tempature/report`, `…/multiColorBox/report` (ACE slot/material/color), etc.
Query any subsystem by publishing `…/web/printer/<modelId>/<printerId>/<sub>`
with `{"type":<sub>,"action":"query"|"getInfo","msgid","timestamp","data":null}`.
