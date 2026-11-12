import json
import re
import urllib.parse
import urllib.request

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
    "Accept-Language": "es-ES,es;q=0.9,en;q=0.8",
}


def fetch_text(url: str) -> str:
    req = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=15) as resp:
        return resp.read().decode("utf-8", errors="ignore")


def clean_artist(raw: str) -> str:
    if not raw:
        return ""
    s = re.sub(r"\s*-\s*Topic$", "", raw, flags=re.IGNORECASE)
    s = re.sub(r"VEVO$", "", s, flags=re.IGNORECASE)
    s = re.sub(r"Official$", "", s, flags=re.IGNORECASE)
    return s.strip().upper()


def clean_title(raw: str) -> str:
    if not raw:
        return "ALBUM"
    s = re.sub(r"^Album\s*-\s*", "", raw, flags=re.IGNORECASE)
    s = re.sub(r"\(Full Album.*?\)", "", s, flags=re.IGNORECASE)
    s = re.sub(r"\[Full Album.*?\]", "", s, flags=re.IGNORECASE)
    s = re.sub(r"Full Album", "", s, flags=re.IGNORECASE)
    return s.strip().upper()


def resolve_playlist_oembed(playlist_id: str) -> dict | None:
    pl_url = f"https://www.youtube.com/playlist?list={playlist_id}"
    oembed_url = f"https://www.youtube.com/oembed?url={urllib.parse.quote(pl_url, safe='')}&format=json"
    try:
        raw = fetch_text(oembed_url)
        data = json.loads(raw)
        return {
            "titulo": clean_title(data.get("title", "")),
            "artista": clean_artist(data.get("author_name", "")),
            "url": pl_url,
            "cover": data.get("thumbnail_url", ""),
        }
    except Exception:
        return None


def extract_playlists_from_channel(channel_line: str) -> list[str]:
    url = channel_line.strip()
    if url.startswith("@"):
        url = f"https://www.youtube.com/{url}/playlists"
    elif "youtube.com/" in url and "/playlists" not in url and "list=" not in url:
        url = url.rstrip("/") + "/playlists"
    try:
        html = fetch_text(url)
        found = re.findall(r'"playlistId":"((?:PL|OLAK5uy_)[a-zA-Z0-9_-]+)"', html)
        seen = []
        for pid in found:
            if pid not in seen:
                seen.append(pid)
        return seen
    except Exception:
        return []


def main():
    playlist_ids = []
    with open("canal_youtube.txt", "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.search(r"[?&]list=([a-zA-Z0-9_-]+)", line)
            if m:
                pid = m.group(1)
                if pid not in playlist_ids:
                    playlist_ids.append(pid)
            elif re.match(r"^(?:PL|OLAK5uy_)[a-zA-Z0-9_-]+$", line):
                if line not in playlist_ids:
                    playlist_ids.append(line)
            else:
                for pid in extract_playlists_from_channel(line):
                    if pid not in playlist_ids:
                        playlist_ids.append(pid)

    albums = []
    for pid in playlist_ids:
        meta = resolve_playlist_oembed(pid)
        if meta:
            albums.append(meta)

    if albums:
        with open("albumes.json", "w", encoding="utf-8") as out:
            json.dump(albums, out, indent=2, ensure_ascii=False)
            out.write("\n")


if __name__ == "__main__":
    main()
