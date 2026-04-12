from __future__ import annotations

import json
import logging
import time
from dataclasses import dataclass
from typing import Any
from urllib import error, request

from .config import Rk3588Config


@dataclass(slots=True)
class SenderResponse:
    ok: bool
    status_code: int | None
    body_text: str
    body_json: dict[str, Any] | None


class Rk3588Sender:
    def __init__(self, config: Rk3588Config, logger: logging.Logger | None = None) -> None:
        self.config = config
        self.logger = logger or logging.getLogger(__name__)
        # Local NUC -> RK3588 communication is expected to stay inside the LAN.
        # Ignore host proxy environment variables so 127.0.0.1 / private IP requests
        # are not accidentally routed through an unavailable SOCKS/HTTP proxy.
        self.opener = request.build_opener(request.ProxyHandler({}))

    def post_state(self, payload: dict[str, Any]) -> SenderResponse:
        url = f"{self.config.base_url}{self.config.state_endpoint}"
        return self._post_json(url, payload, retry_http_5xx=True)

    def switch_mode(
        self,
        mode: str = "real",
        source: str = "nuc-uploader",
        requested_by: str = "operator",
    ) -> SenderResponse:
        payload = {
            "mode": mode,
            "source": source,
            "requested_by": requested_by,
        }
        url = f"{self.config.base_url}{self.config.mode_switch_endpoint}"
        return self._post_json(url, payload, retry_http_5xx=False)

    def _post_json(
        self,
        url: str,
        payload: dict[str, Any],
        retry_http_5xx: bool,
    ) -> SenderResponse:
        total_attempts = self.config.retry_count + 1
        data = json.dumps(payload).encode("utf-8")

        for attempt in range(1, total_attempts + 1):
            req = request.Request(
                url,
                data=data,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            try:
                with self.opener.open(req, timeout=self.config.timeout_sec) as resp:
                    body_text = resp.read().decode("utf-8", errors="replace")
                    body_json = _parse_json(body_text)
                    return SenderResponse(True, resp.status, body_text, body_json)
            except error.HTTPError as exc:
                body_text = exc.read().decode("utf-8", errors="replace")
                body_json = _parse_json(body_text)
                should_retry = retry_http_5xx and 500 <= exc.code < 600 and attempt < total_attempts
                self.logger.warning(
                    "HTTP error when posting to %s (attempt %s/%s): status=%s body=%s",
                    url,
                    attempt,
                    total_attempts,
                    exc.code,
                    body_text,
                )
                if should_retry:
                    time.sleep(self.config.retry_backoff_sec)
                    continue
                return SenderResponse(False, exc.code, body_text, body_json)
            except error.URLError as exc:
                self.logger.warning(
                    "Network error when posting to %s (attempt %s/%s): %s",
                    url,
                    attempt,
                    total_attempts,
                    exc,
                )
                if attempt < total_attempts:
                    time.sleep(self.config.retry_backoff_sec)
                    continue
                return SenderResponse(False, None, str(exc), None)
            except TimeoutError as exc:
                self.logger.warning(
                    "Timeout when posting to %s (attempt %s/%s): %s",
                    url,
                    attempt,
                    total_attempts,
                    exc,
                )
                if attempt < total_attempts:
                    time.sleep(self.config.retry_backoff_sec)
                    continue
                return SenderResponse(False, None, str(exc), None)

        return SenderResponse(False, None, "exhausted retries", None)


def _parse_json(body_text: str) -> dict[str, Any] | None:
    try:
        parsed = json.loads(body_text)
    except json.JSONDecodeError:
        return None
    return parsed if isinstance(parsed, dict) else None
