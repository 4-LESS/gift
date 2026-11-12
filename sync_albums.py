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


def fetch_ytm_browse(browse_id: str) -> str:
    url = "https://music.youtube.com/youtubei/v1/browse?prettyPrint=false"
    payload = json.dumps({
        "context": {
            "client": {
                "clientName": "WEB_REMIX",
                "clientVersion": "1.20241101.01.00",
                "hl": "es",
                "gl": "CL",
            }
        },
        "browseId": browse_id,
    }).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=payload,
        headers={**HEADERS, "Content-Type": "application/json"},
        method="POST",
    )
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
        return "PLAYLIST"
    s = re.sub(r"^Album\s*-\s*", "", raw, flags=re.IGNORECASE)
    s = re.sub(r"\(Full Album.*?\)", "", s, flags=re.IGNORECASE)
    s = re.sub(r"\[Full Album.*?\]", "", s, flags=re.IGNORECASE)
    s = re.sub(r"Full Album", "", s, flags=re.IGNORECASE)
    return s.strip().upper()


def resolve_playlist_oembed(playlist_id: str) -> dict:
    clean_id = playlist_id[2:] if playlist_id.startswith("VL") else playlist_id
    pl_url = f"https://www.youtube.com/playlist?list={clean_id}"
    oembed_url = f"https://www.youtube.com/oembed?url={urllib.parse.quote(pl_url, safe='')}&format=json"
    try:
        raw = fetch_text(oembed_url)
        data = json.loads(raw)
        cover = data.get("thumbnail_url", "")
        artist = clean_artist(data.get("author_name", ""))
        # En álbumes OLAK5uy_, author_name viene vacío en el oembed de playlist;
        # lo obtenemos del primer video usando el ID que viene en thumbnail_url
        if not artist and cover:
            m_vid = re.search(r"/vi/([a-zA-Z0-9_-]{11})/", cover)
            if m_vid:
                vid_url = f"https://www.youtube.com/watch?v={m_vid.group(1)}"
                vid_oembed = f"https://www.youtube.com/oembed?url={urllib.parse.quote(vid_url, safe='')}&format=json"
                try:
                    vdata = json.loads(fetch_text(vid_oembed))
                    artist = clean_artist(vdata.get("author_name", ""))
                except Exception:
                    pass
        return {
            "titulo": clean_title(data.get("title", "")),
            "artista": artist,
            "url": pl_url,
            "cover": cover,
        }
    except Exception:
        return {"url": pl_url}


def extract_playlists_from_channel(channel_line: str) -> list[str]:
    url = channel_line.strip().replace("music.youtube.com", "www.youtube.com")
    if url.startswith("@"):
        url = f"https://www.youtube.com/{url}"
    base_url = re.sub(r"/(playlists|releases|featured|videos)/?$", "", url.rstrip("/"))

    seen = []
    try:
        main_html = fetch_text(base_url)
        m_cid = re.search(r'"externalId":"(UC[a-zA-Z0-9_-]+)"', main_html)
        cid = m_cid.group(1) if m_cid else None

        pl_html = fetch_text(base_url + "/playlists")
        for pid in re.findall(r'"playlistId":"((?:PL|OLAK5uy_)[a-zA-Z0-9_-]+)"', pl_html):
            if pid not in seen:
                seen.append(pid)

        if cid:
            ytm_json = fetch_ytm_browse(cid)
            for pid in re.findall(r'"(?:playlistId|browseId)":"((?:VLPL|PL|OLAK5uy_)[a-zA-Z0-9_-]+)"', ytm_json):
                clean_id = pid[2:] if pid.startswith("VL") else pid
                if clean_id not in seen:
                    seen.append(clean_id)
    except Exception:
        pass
    return seen


def main():
    all_playlists = []

    with open("canal_youtube.txt", "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.search(r"[?&]list=([a-zA-Z0-9_-]+)", line)
            if m:
                pid = m.group(1)
                if pid not in all_playlists:
                    all_playlists.append(pid)
            elif re.match(r"^(?:PL|OLAK5uy_)[a-zA-Z0-9_-]+$", line):
                if line not in all_playlists:
                    all_playlists.append(line)
            else:
                for pid in extract_playlists_from_channel(line):
                    if pid not in all_playlists:
                        all_playlists.append(pid)

    albums = [resolve_playlist_oembed(pid) for pid in all_playlists]

    if albums:
        with open("albumes.json", "w", encoding="utf-8") as out:
            json.dump(albums, out, indent=2, ensure_ascii=False)
            out.write("\n")


if __name__ == "__main__":
    main()
