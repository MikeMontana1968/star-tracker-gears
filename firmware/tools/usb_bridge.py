"""
USB bridge: the tracker's web API on this PC, over the USB serial cable.

    python tools/usb_bridge.py [--port COM10] [--http 8780] [--no-browser]

Serves http://localhost:8780/
    /              the phone UI (firmware/web/index.html), live from disk
    /studio.html   the schedule studio (firmware/web/studio.html)
    /api/...       forwarded to the board as one console line
                   `H <METHOD> <path> <body>` -- the same net::Api the
                   Wi-Fi server runs, so every endpoint behaves identically
    /bridge/info   which port, and whether the board is answering

The board must be awake to answer: on the bench (stay-awake) it always is;
deployed in deep sleep, press the config button first. Opening the port
does not reset the ESP32 (DTR/RTS are held low before the port opens).

Needs pyserial:  python -m pip install pyserial
"""
import argparse
import http.server
import json
import os
import queue
import socketserver
import sys
import threading
import time
import webbrowser

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is missing:  python -m pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.normpath(os.path.join(HERE, "..", "web"))
CH340_VID = 0x1A86


def find_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.vid == CH340_VID:
            return p.device
    for p in ports:                       # any USB serial that is not Bluetooth
        if p.vid and "bluetooth" not in (p.description or "").lower():
            return p.device
    return None


class Board:
    """One serial port, one request at a time; log lines are kept for /bridge/console."""

    def __init__(self, port):
        self.port = port
        self.lock = threading.Lock()
        self.replies = queue.Queue()
        self.console = []                 # recent non-reply lines
        self.capture = None               # lines of a K file dump in progress
        self.files = queue.Queue()
        self.ser = None
        self.error = None
        self.open()
        threading.Thread(target=self._reader, daemon=True).start()

    def open(self):
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = self.port, 115200, 0.2
            s.dtr = False                 # no auto-reset of the ESP32
            s.rts = False
            s.open()
            self.ser, self.error = s, None
        except Exception as e:            # noqa: BLE001 -- report whatever the OS says
            self.ser, self.error = None, str(e)

    def _reader(self):
        buf = b""
        while True:
            if self.ser is None:
                time.sleep(2)
                self.open()
                continue
            try:
                chunk = self.ser.read(4096)
            except Exception as e:        # noqa: BLE001 -- unplugged
                self.error = str(e)
                try:
                    self.ser.close()
                except Exception:         # noqa: BLE001
                    pass
                self.ser = None
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                if self.capture is not None:
                    if text == "END FILE":
                        self.files.put("\n".join(self.capture))
                        self.capture = None
                    else:
                        self.capture.append(text)
                    continue
                if text.startswith("BEGIN FILE "):
                    self.capture = []
                    continue
                if text.startswith("FILE MISSING"):
                    self.files.put(None)
                    continue
                if text.startswith("HTTP "):
                    self.replies.put(text)
                elif text:
                    self.console.append(text)
                    del self.console[:-200]

    def file(self, path, timeout=20.0):
        """A LittleFS file from the board (console K command); None if missing."""
        if self.ser is None:
            return None
        with self.lock:
            while not self.files.empty():
                self.files.get_nowait()
            self.ser.write(("K %s\n" % path).encode("utf-8"))
            try:
                return self.files.get(timeout=timeout)
            except queue.Empty:
                return None

    def request(self, method, path_q, body, timeout=8.0):
        if self.ser is None:
            return 503, json.dumps({"ok": False, "error": "board not connected: " + (self.error or "?")})
        body = (body or "").replace("\r", " ").replace("\n", " ").strip()
        if len(body) > 8200:
            return 413, json.dumps({"ok": False, "error": "body over 8 KB (the console line limit)"})
        with self.lock:
            while not self.replies.empty():
                self.replies.get_nowait()
            line = "H %s %s%s\n" % (method, path_q, (" " + body) if body else "")
            try:
                self.ser.write(line.encode("utf-8"))
            except Exception as e:        # noqa: BLE001
                return 503, json.dumps({"ok": False, "error": "serial write failed: %s" % e})
            try:
                reply = self.replies.get(timeout=timeout)
            except queue.Empty:
                return 504, json.dumps({"ok": False, "error": "the board did not answer (asleep? press the config button)"})
        # "HTTP 200 {...}"
        parts = reply.split(" ", 2)
        code = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 502
        return code, parts[2] if len(parts) > 2 else ""


def make_handler(board):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # quiet: one line per API call is plenty
            if "/api/status" not in self.path and "/api/log" not in self.path:
                sys.stderr.write("%s %s\n" % (self.command, self.path))

        def _send(self, code, body, ctype="application/json"):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _static(self, name):
            path = os.path.join(WEB, name)
            if not os.path.isfile(path):
                return self._send(404, "not found", "text/plain")
            with open(path, "rb") as f:
                self._send(200, f.read(), "text/html; charset=utf-8")

        def _api(self):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n).decode("utf-8") if n else ""
            code, out = board.request(self.command, self.path, body)
            self._send(code, out)

        def do_GET(self):
            if self.path.startswith("/api/"):
                return self._api()
            if self.path.startswith("/bridge/info"):
                return self._send(200, json.dumps({"port": board.port, "connected": board.ser is not None,
                                                   "error": board.error, "usb": True}))
            if self.path.startswith("/camera/") and self.path.endswith(".json") and ".." not in self.path:
                body = board.file("/camera_" + self.path[len("/camera/"):])
                return self._send(200, body) if body is not None else self._send(404, '{"error":"not on the board: run discover"}')
            if self.path.startswith("/bridge/console"):
                return self._send(200, json.dumps(board.console[-100:]))
            if self.path in ("/", "/index.html") or self.path.startswith("/?"):
                return self._static("index.html")
            if self.path.startswith("/studio"):
                return self._static("studio.html")
            if self.path == "/camera_profile.example.json":
                with open(os.path.join(WEB, "camera_profile.example.json"), "rb") as f:
                    return self._send(200, f.read())
            self._send(404, "not found", "text/plain")

        def do_POST(self):
            self._api() if self.path.startswith("/api/") else self._send(404, "", "text/plain")

        def do_PUT(self):
            self._api() if self.path.startswith("/api/") else self._send(404, "", "text/plain")

        def do_DELETE(self):
            self._api() if self.path.startswith("/api/") else self._send(404, "", "text/plain")

    return Handler


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="COM port (default: the first CH340)")
    ap.add_argument("--http", type=int, default=8780)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    port = a.port or find_port()
    if not port:
        sys.exit("no USB serial port found -- is the tracker plugged in?")
    board = Board(port)
    srv = Server(("127.0.0.1", a.http), make_handler(board))
    url = "http://localhost:%d/studio.html" % a.http
    print("star tracker USB bridge: %s on %s%s" % (url, port, "" if board.ser else "  (NOT OPEN: %s)" % board.error))
    print("Ctrl+C to stop")
    if not a.no_browser:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
