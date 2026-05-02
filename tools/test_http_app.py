#!/usr/bin/env python3
"""Regression checks for the embedded Ore HTTP client source."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.mkorefs import ROOT_FILES


def require(source: str, needle: str, message: str) -> None:
    if needle not in source:
        raise SystemExit(message)


def main() -> None:
    source = ROOT_FILES["apps/http.ore"].decode()

    require(
        source,
        'copy_str_cap(&path, "/ip", 64);',
        "default http command must request /ip from ifconfig.me",
    )
    require(
        source,
        "payload_len = write_http_payload(buf, &host, &path);",
        "HTTP request payload must be generated from host/path arguments",
    )
    require(
        source,
        "24 | (payload_len << 8)",
        "HTTP GET must send PSH+ACK with the generated payload length",
    )
    require(
        source,
        "if (addr[0] != 0)",
        "HTTP client must honor an explicit connect-ip argument",
    )
    require(
        source,
        "dst_ip = wait_dns(buf, dns_ip);",
        "HTTP client must resolve hostnames when connect-ip is omitted",
    )
    require(
        source,
        "http: gateway ARP timeout",
        "HTTP client must report gateway ARP failure before TCP connect",
    )
    require(
        source,
        "http: tcp connect timeout",
        "HTTP client must report SYN-ACK timeout separately",
    )
    require(
        source,
        "header_done",
        "wait_http must track whether HTTP headers have been consumed",
    )
    require(
        source,
        "print_buf(buf + body_off, body_len);",
        "wait_http must print the response body, not the raw first TCP segment",
    )


if __name__ == "__main__":
    main()
