#!/usr/bin/env python3
"""Builds assets/shields/blocklist.txt for Kiwi Shields.

Source: the StevenBlack unified hosts list (ads + tracking + malware,
MIT-licensed, https://github.com/StevenBlack/hosts). The file ships next to
kiwi.exe and is loaded by src/shields_rules.cpp at startup; a missing file
just leaves the built-in mustela tracker list in charge.

The output is one plain domain per line so the loader stays trivial and the
list survives future format churn.
"""

import sys
import urllib.request

SOURCE = "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts"
OUTPUT = "assets/shields/blocklist.txt"

RESERVED = {
    "localhost", "localhost.localdomain", "local", "broadcasthost",
    "ip6-localhost", "ip6-loopback", "ip6-localnet", "ip6-mcastprefix",
    "ip6-allnodes", "ip6-allrouters", "0.0.0.0",
}


def parse_hosts(text):
    domains = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) == 2 and parts[0] in ("0.0.0.0", "127.0.0.1", "::1"):
            host = parts[1].lower().strip(".")
        elif len(parts) == 1:
            host = parts[0].lower().strip(".")
        else:
            continue
        if not host or host in RESERVED:
            continue
        if "." not in host:  # single-label names only exist on the LAN
            continue
        if not all(c.isalnum() or c in ".-" for c in host):
            continue
        if any(len(label) == 0 for label in host.split(".")):
            continue
        domains.add(host)
    return domains


def main():
    print(f"fetching {SOURCE} ...")
    request = urllib.request.Request(SOURCE, headers={"User-Agent": "kiwi-shields"})
    with urllib.request.urlopen(request, timeout=120) as response:
        text = response.read().decode("utf-8", "replace")
    domains = parse_hosts(text)
    ordered = sorted(domains)
    body = "\n".join(ordered)
    header = (
        "# Kiwi Shields blocklist — ads, trackers and malicious hosts.\n"
        f"# {len(ordered)} domains, derived from the StevenBlack unified hosts\n"
        "# list (https://github.com/StevenBlack/hosts, MIT). One domain per\n"
        "# line; subdomains match implicitly. Regenerate with\n"
        "# scripts/make_shields_list.py.\n"
    )
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as out:
        out.write(header)
        out.write(body)
        out.write("\n")
    print(f"wrote {OUTPUT}: {len(ordered)} domains")


if __name__ == "__main__":
    sys.exit(main())
