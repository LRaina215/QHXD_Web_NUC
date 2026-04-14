from __future__ import annotations

import argparse
import json
import logging
import sys
import time
from pathlib import Path

from .config import DEFAULT_CONFIG_PATH, AppConfig, load_config
from .mission_server import serve_mission_api
from .rk3588_sender import Rk3588Sender
from .state_collector import build_collector
from .state_mapper import build_payload


LOGGER = logging.getLogger("nuc_state_uploader")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="NUC state uploader for RK3588 integration.")
    parser.add_argument(
        "--config",
        default=str(DEFAULT_CONFIG_PATH),
        help="Path to config JSON. Default: %(default)s",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="Continuously collect and upload state.")
    run_parser.add_argument("--dry-run", action="store_true", help="Build payloads without sending HTTP requests.")
    run_parser.add_argument(
        "--print-payload",
        action="store_true",
        help="Print the payload JSON before each send.",
    )

    send_once_parser = subparsers.add_parser("send-once", help="Collect and upload one state sample.")
    send_once_parser.add_argument("--dry-run", action="store_true", help="Build one payload without sending it.")
    send_once_parser.add_argument(
        "--print-payload",
        action="store_true",
        help="Print the payload JSON before sending.",
    )

    switch_mode_parser = subparsers.add_parser("switch-mode", help="Switch RK3588 backend mode.")
    switch_mode_parser.add_argument("--mode", default="real", choices=["real", "mock"])
    switch_mode_parser.add_argument("--source", default="nuc-uploader")
    switch_mode_parser.add_argument("--requested-by", default="operator")

    subparsers.add_parser("serve-mission", help="Start the NUC mission HTTP endpoint.")

    return parser.parse_args()


def configure_logging(level_name: str) -> None:
    level = getattr(logging, level_name.upper(), logging.INFO)
    logging.basicConfig(
        level=level,
        format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    )


def main() -> int:
    args = parse_args()
    config = load_config(Path(args.config))
    configure_logging(config.log_level)

    if args.command == "switch-mode":
        return run_switch_mode(config, args.mode, args.source, args.requested_by)
    if args.command == "serve-mission":
        return run_mission_server(config)

    return run_uploader(config, once=args.command == "send-once", dry_run=args.dry_run, print_payload=args.print_payload)


def run_switch_mode(config: AppConfig, mode: str, source: str, requested_by: str) -> int:
    sender = Rk3588Sender(config.rk3588, LOGGER)
    response = sender.switch_mode(mode=mode, source=source, requested_by=requested_by)
    if response.body_text:
        print(response.body_text)
    return 0 if response.ok else 1


def run_uploader(config: AppConfig, once: bool, dry_run: bool, print_payload: bool) -> int:
    collector = build_collector(config.collector, config.rtt_collector)
    sender = Rk3588Sender(config.rk3588, LOGGER)
    seq = 0

    try:
        while True:
            try:
                raw_state = collector.collect(seq)
                payload = build_payload(raw_state, default_source=config.collector.source_name)

                if print_payload or config.dump_payload:
                    print(json.dumps(payload, ensure_ascii=False, indent=2))

                if dry_run:
                    LOGGER.info("Dry-run enabled, payload built but not sent.")
                else:
                    response = sender.post_state(payload)
                    _log_sender_response(response)
                    if not response.ok:
                        LOGGER.error("State send failed.")
                seq += 1
            except Exception:
                LOGGER.exception("Uploader cycle failed.")

            if once:
                break
            time.sleep(config.send_interval_sec)
    except KeyboardInterrupt:
        LOGGER.info("Uploader interrupted by user.")
        return 0

    return 0


def run_mission_server(config: AppConfig) -> int:
    serve_mission_api(config.mission_server, LOGGER)
    return 0


def _log_sender_response(response: object) -> None:
    if not hasattr(response, "ok"):
        return

    if response.ok:
        accepted = None
        if response.body_json:
            accepted = response.body_json.get("data", {}).get("accepted")
        if accepted is False:
            LOGGER.warning("State was received but not accepted. RK3588 may still be in mock mode.")
        else:
            LOGGER.info("State sent successfully. status=%s accepted=%s", response.status_code, accepted)
        return

    LOGGER.warning("State send failed. status=%s body=%s", response.status_code, response.body_text)


if __name__ == "__main__":
    sys.exit(main())
