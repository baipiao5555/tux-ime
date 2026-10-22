import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

STATE = {"json": None, "etag": '"1"', "files": {}}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write((fmt % args) + "\n")

    def _body(self):
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length)

    def do_GET(self):
        if self.path.startswith("/file/"):
            name = self.path.split("/file/", 1)[1]
            data = STATE["files"].get(name)
            if data is None:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path.rstrip("/").endswith("SyncClipboard.json"):
            if STATE["json"] is None:
                self.send_error(404)
                return
            if self.headers.get("If-None-Match") == STATE["etag"]:
                self.send_response(304)
                self.send_header("ETag", STATE["etag"])
                self.end_headers()
                return
            body = STATE["json"].encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("ETag", STATE["etag"])
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_error(404)

    def do_PUT(self):
        body = self._body()
        if self.path.startswith("/file/"):
            name = self.path.split("/file/", 1)[1]
            STATE["files"][name] = body
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path.rstrip("/").endswith("SyncClipboard.json"):
            STATE["json"] = body.decode("utf-8")
            STATE["etag"] = f'"{len(STATE["json"])}"'
            self.send_response(200)
            self.send_header("ETag", STATE["etag"])
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_error(404)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
