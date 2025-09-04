#!/usr/bin/env python3
import requests
import csv
import sys
import re

# ---------- Config ----------
OWNER = "MicrosoftDocs"
REPO = "azure-compute-docs"
SIZES_ROOT = "articles/virtual-machines/sizes"
SUBFOLDERS = [
    "general-purpose",
    "compute-optimized",
    "memory-optimized",
    "storage-optimized",
    "gpu-accelerated",
    "fpga-accelerated",
]

GITHUB_API = "https://api.github.com"
RAW_ROOT = "https://raw.githubusercontent.com/{owner}/{repo}/main/{path}"

HEADERS_API = {"User-Agent": "AnyBlob", "Accept": "application/vnd.github+json"}
HEADERS_RAW = {"User-Agent": "AnyBlob"}
REQ_TIMEOUT = 30

# ---------- Tiny helpers ----------
def getJson(url, debug=False):
    try:
        r = requests.get(url, headers=HEADERS_API, timeout=REQ_TIMEOUT)
        if r.status_code == 200:
            return r.json()
        if debug:
            print(f"[warn] {url} -> {r.status_code}")
    except Exception as e:
        if debug:
            print(f"[error] {url}: {e}")
    return None

def getTextRaw(path, debug=False):
    url = RAW_ROOT.format(owner=OWNER, repo=REPO, path=path)
    try:
        r = requests.get(url, headers=HEADERS_RAW, timeout=REQ_TIMEOUT)
        if r.status_code == 200:
            return r.text
        if debug:
            print(f"[warn] {url} -> {r.status_code}")
    except Exception as e:
        if debug:
            print(f"[error] {url}: {e}")
    return ""

def normHeader(h):
    t = h.strip().lower()
    if "size" in t: return "Size"
    if "vcpu" in t or "vcpus" in t or "core" in t: return "vC"
    if "memory" in t or "ram" in t: return "Memory"
    if "network" in t and "bandwidth" in t: return "Network Bandwidth"
    return h.strip()

# Best-effort integer extraction: "16,000+", "16k", "16,000 Mb/s", "672 GB" -> 16000, 16000, 16000, 672
def toIntGuess(value):
    if not value:
        return None
    s = value.strip().lower()
    # Handle shorthand like "16k", "50k", "1.5k"
    m = re.match(r"^\s*([0-9]+(?:\.[0-9]+)?)\s*k\b", s)
    if m:
        try:
            return int(float(m.group(1)) * 1000)
        except:
            pass
    # Find all numeric chunks; pick the largest (common when notes add multiple numbers)
    nums = []
    for n in re.findall(r"[0-9][0-9,\.]*", s):
        try:
            n_clean = n.replace(",", "")
            # if decimal, floor
            nums.append(int(float(n_clean)))
        except:
            continue
    if nums:
        return max(nums)
    return None

def extractTables(mdText):
    # contiguous blocks of lines that start with '|'
    blocks = re.findall(r"(?:^\|.*\n)+", mdText, flags=re.MULTILINE)
    tables = []
    for block in blocks:
        lines = [ln.rstrip() for ln in block.strip().splitlines() if ln.strip().startswith("|")]
        if len(lines) >= 2:
            tables.append(lines)
    return tables

# ---------- Discovery ----------
def discoverSeriesMarkdownPaths(debug=False):
    seriesPaths = set()
    for sub in SUBFOLDERS:
        url = f"{GITHUB_API}/repos/{OWNER}/{REPO}/contents/{SIZES_ROOT}/{sub}"
        data = getJson(url, debug=debug)
        if not isinstance(data, list):
            continue
        for item in data:
            if item.get("type") == "file":
                name = item.get("name", "")
                path = item.get("path", "")
                if name.endswith(".md") and "-series.md" in name:
                    seriesPaths.add(path)
    if debug:
        print(f"[info] discovered series files: {len(seriesPaths)}")
    return sorted(seriesPaths)

# ---------- Parsing ----------
def parseSeriesMarkdown(path, debug=False):
    text = getTextRaw(path, debug=debug)
    if not text:
        return []

    basics = {}  # size -> {"Size":..., "vC":..., "Memory":...}
    nets = {}    # size -> {"Network Bandwidth":...}

    for lines in extractTables(text):
        headers = [c.strip() for c in lines[0].strip("|").split("|")]
        hNorm = [normHeader(h) for h in headers]
        col = {h: i for i, h in enumerate(hNorm)}

        hasSize = "Size" in col
        hasVC = "vC" in col
        hasMem = "Memory" in col
        hasNet = "Network Bandwidth" in col
        if not hasSize:
            continue

        isBasics = hasSize and (hasVC or hasMem)
        isNetwork = hasSize and hasNet
        if not (isBasics or isNetwork):
            continue

        for line in lines[2:]:
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < len(hNorm):
                continue
            size = cells[col["Size"]]
            if not size:
                continue

            if isBasics:
                vc = toIntGuess(cells[col["vC"]]) if hasVC else None
                mem = toIntGuess(cells[col["Memory"]]) if hasMem else None
                cur = basics.get(size, {"Size": size, "vC": None, "Memory": None})
                if vc is not None:   cur["vC"] = vc
                if mem is not None:  cur["Memory"] = mem
                basics[size] = cur

            if isNetwork:
                nbw = toIntGuess(cells[col["Network Bandwidth"]]) if hasNet else None
                if nbw is not None:
                    nets[size] = {"Network Bandwidth": nbw}

    # join by Size
    rows = []
    for size, b in basics.items():
        nbw = nets.get(size, {}).get("Network Bandwidth")
        if b.get("vC") is not None and b.get("Memory") is not None and nbw is not None:
            rows.append([b["Size"], b["vC"], b["Memory"], nbw])

    if debug:
        print(f"[info] {path}: basics={len(basics)} nets={len(nets)} rows={len(rows)}")
    return rows

# ---------- Main ----------
def scrapeAndWriteToCsv(csvFilename, debug=False):
    header = ["Size", "vC", "Memory", "Network Bandwidth"]
    seriesPaths = discoverSeriesMarkdownPaths(debug=debug)
    if not seriesPaths:
        raise SystemExit("No series markdown files found via GitHub API.")

    total = 0
    with open(csvFilename, "w", newline="", encoding="utf-8") as f:
        wr = csv.writer(f)
        wr.writerow(header)
        for path in seriesPaths:
            rows = parseSeriesMarkdown(path, debug=debug)
            for r in rows:
                wr.writerow(r)
            total += len(rows)

    if debug:
        print(f"[done] wrote {total} rows to {csvFilename}")

if __name__ == "__main__":
    debug = "--debug" in sys.argv
    scrapeAndWriteToCsv("azure.csv", debug=debug)

