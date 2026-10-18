#!/usr/bin/env python3
import os
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor

manifest = sys.argv[1]
destination = sys.argv[2]
commit = "8ece20377cd3f0aa0aadb9d61535a703c43f4612"
repo = "WebKit/WebKit"

paths = [
    "Source/WebKit/UIProcess/API/C",
    "Source/WebKit/UIProcess/API/C/win",
    "Source/WebKit/Shared/API/c",
    "Source/WebKit/Shared/API/c/win",
    "Source/WebKit/Shared/API/c/gtk",
    "Source/WebKit/Shared/API/c/mac",
    "Source/WebKit/Shared/API/c/wpe",
    "Source/WebKit/Shared/API/c/playstation",
]

with open(manifest) as fh:
    headers = [line.strip() for line in fh if line.strip()]

target = os.path.join(destination, "WebKit")
os.makedirs(target, exist_ok=True)

base = f"https://raw.githubusercontent.com/{repo}/{commit}"
opener = urllib.request.build_opener()
opener.addheaders = [("User-Agent", "kiwi-fetch-win-headers")]

def fetch(name):
    for folder in paths:
        url = f"{base}/{folder}/{name}"
        try:
            with opener.open(url, timeout=30) as resp:
                data = resp.read()
            if data.decode("utf-8", "replace").startswith("404: Not Found"):
                continue
            with open(os.path.join(target, name), "wb") as out:
                out.write(data)
            return name, True
        except urllib.error.HTTPError:
            continue
        except Exception:
            continue
    return name, False

with ThreadPoolExecutor(max_workers=12) as pool:
    results = list(pool.map(fetch, headers))

missing = [name for name, ok in results if not ok]
print(f"fetched {len(results) - len(missing)}/{len(results)} headers into {target}")
if missing:
    for name in missing:
        print(f"MISSING: {name}")
    sys.exit(1)
