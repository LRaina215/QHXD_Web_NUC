from __future__ import annotations

import json
import logging
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .config import MissionServerConfig
from .runtime_state import RuntimeStateStore


def serve_mission_api(config: MissionServerConfig, logger: logging.Logger | None = None) -> None:
    runtime_store = RuntimeStateStore(config.runtime_state_file)
    runtime_store.reset()
    service_logger = logger or logging.getLogger("nuc_mission_server")
    handler_cls = _build_handler(config.path, runtime_store, service_logger)
    server = ThreadingHTTPServer((config.host, config.port), handler_cls)
    service_logger.info("Mission server listening on %s:%s%s", config.host, config.port, config.path)
    try:
        server.serve_forever()
    finally:
        server.server_close()


def _build_handler(
    mission_path: str,
    runtime_store: RuntimeStateStore,
    logger: logging.Logger,
) -> type[BaseHTTPRequestHandler]:
    class MissionHandler(BaseHTTPRequestHandler):
        server_version = "NUCMissionServer/0.1"

        def do_POST(self) -> None:  # noqa: N802
            if self.path != mission_path:
                self._send_json(
                    HTTPStatus.NOT_FOUND,
                    {"success": False, "error": f"unsupported path: {self.path}"},
                )
                return

            try:
                request_json = self._read_json()
            except ValueError as exc:
                logger.warning("Mission server received invalid JSON: %s", exc)
                self._send_json(
                    HTTPStatus.BAD_REQUEST,
                    {"success": False, "error": str(exc)},
                )
                return

            command = str(request_json.get("command") or "").strip()
            source = str(request_json.get("source") or "web")
            requested_by = request_json.get("requested_by")
            payload = request_json.get("payload")
            if payload is None:
                payload = {}
            if not isinstance(payload, dict):
                self._send_json(
                    HTTPStatus.BAD_REQUEST,
                    {"success": False, "error": "payload must be a JSON object"},
                )
                return

            response = runtime_store.apply_command(command, payload, source, requested_by)
            logger.info(
                "Mission received: command=%s source=%s requested_by=%s payload=%s accepted=%s",
                command,
                source,
                requested_by,
                payload,
                response["data"]["accepted"],
            )
            self._send_json(HTTPStatus.OK, response)

        def log_message(self, format: str, *args: object) -> None:  # noqa: A003
            logger.info("%s - %s", self.address_string(), format % args)

        def _read_json(self) -> dict:
            content_length = int(self.headers.get("Content-Length", "0"))
            raw_body = self.rfile.read(content_length)
            try:
                body = json.loads(raw_body.decode("utf-8"))
            except json.JSONDecodeError as exc:
                raise ValueError(f"invalid JSON body: {exc}") from exc
            if not isinstance(body, dict):
                raise ValueError("request body must be a JSON object")
            return body

        def _send_json(self, status: HTTPStatus, payload: dict) -> None:
            response = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)

    return MissionHandler
