/* Update check, shared by the game (update_check.c) and the Windows launcher (launcher/launcher.cpp):
 * asks GitHub for the newest release (its tag and notes) and compares it with this build's version; the launcher
 * can also download that release's zip. One HTTPS GET to api.github.com; nothing is sent beyond the request.
 * Implemented on Windows (WinHTTP); elsewhere the fetch fails. Header-only so that the C runtime and the C++
 * launcher share it without another build step. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATE_REPO "teclilla18/sonic-riders-recomp"
#define UPDATE_PAGE "https://github.com/" UPDATE_REPO "/releases/latest"

/* "v0.7.0", "v0.7.0-11-gabc (dirty)", "0.7" -> 1 and v[0..2]; anything else (a dev build without a tag) -> 0 */
static int update_parse_version(const char* s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return 0;
    for (int i = 0; i < 3; i++) {
        char* end;
        v[i] = (int)strtol(s, &end, 10);
        if (end == s) return i > 0;
        s = end;
        if (*s != '.') return 1;
        s++;
    }
    return 1;
}

/* 1 when tag `latest` is a newer release than `mine` (both as update_parse_version reads them) */
static int update_is_newer(const char* latest, const char* mine) {
    int a[3], b[3];
    if (!update_parse_version(latest, a) || !update_parse_version(mine, b)) return 0;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return 0;
}

/* the "tag_name" value of a GitHub release JSON object; 0 if missing */
static int update_tag_from_json(const char* json, char* tag, size_t n) {
    const char* p = strstr(json, "\"tag_name\"");
    if (!p) return 0;
    p = strchr(p + 10, '"');
    if (!p) return 0;
    p++;
    size_t k = 0;
    while (*p && *p != '"' && k + 1 < n) tag[k++] = *p++;
    tag[k] = 0;
    return *p == '"' && k > 0;
}

/* the value of a JSON string field after `from` (first match), unescaped (\n \" \\ \/ \t, \uXXXX as UTF-8);
 * malloc'd, or NULL */
static char* update_json_string(const char* from, const char* key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(from, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), '"');
    if (!p) return NULL;
    p++;
    size_t cap = strlen(p) + 1, k = 0;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            char e = *p++;
            if (e == 'n') out[k++] = '\n';
            else if (e == 't') out[k++] = ' ';
            else if (e == 'r') continue;
            else if (e == 'u' && p[0] && p[1] && p[2] && p[3]) {
                char hex[5] = {p[0], p[1], p[2], p[3], 0};
                unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u' && p[2] && p[3] && p[4] && p[5]) {
                    char lo[5] = {p[2], p[3], p[4], p[5], 0};  /* a surrogate pair: emoji */
                    unsigned l = (unsigned)strtoul(lo, NULL, 16);
                    if (l >= 0xDC00 && l < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (l - 0xDC00);
                        p += 6;
                    }
                }
                if (cp >= 0x10000) {
                    out[k++] = (char)(0xF0 | (cp >> 18));
                    out[k++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    out[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[k++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x80) out[k++] = (char)cp;
                else if (cp < 0x800) {
                    out[k++] = (char)(0xC0 | (cp >> 6));
                    out[k++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[k++] = (char)(0xE0 | (cp >> 12));
                    out[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[k++] = (char)(0x80 | (cp & 0x3F));
                }
            } else out[k++] = e;
        } else out[k++] = *p++;
    }
    out[k] = 0;
    return out;
}

/* the code point of the UTF-8 sequence at s and its length in *n (1 for a stray byte) */
static unsigned update_utf8(const char* s, int* n) {
    const unsigned char* u = (const unsigned char*)s;
    if (u[0] < 0x80) { *n = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { *n = 2; return ((u[0] & 31u) << 6) | (u[1] & 63u); }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *n = 3;
        return ((u[0] & 15u) << 12) | ((u[1] & 63u) << 6) | (u[2] & 63u);
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *n = 4;
        return ((u[0] & 7u) << 18) | ((u[1] & 63u) << 12) | ((u[2] & 63u) << 6) | (u[3] & 63u);
    }
    *n = 1;
    return u[0];
}
/* invisible emoji helpers no font draws on its own (they would show as "?"): variation selectors, zero-width
 * joiner, skin tones, keycap */
static int update_is_emoji_joiner(unsigned cp) {
    return (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}
/* pictographs (emoji): the game's menu font has none */
static int update_is_emoji(unsigned cp) {
    return cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
           update_is_emoji_joiner(cp);
}

/* release notes (GitHub markdown) as plain text for the game's menu, in place: no ** or backticks,
 * [text](url) -> text, no pictures (![alt](url), <img>) or other HTML tags, no "#" heading marks, no emoji */
static void update_plain_text(char* s) {
    char* w = s;
    int line_start = 1;
    for (char* r = s; *r;) {
        if (line_start) {  /* "### Heading" -> "Heading", "> quote" -> "quote" */
            line_start = 0;
            char* q = r;
            while (*q == '#') q++;
            if (q > r && *q == ' ') r = q + 1;
            else if (r[0] == '>' && r[1] == ' ') r += 2;
            while (*r == ' ' && (r[1] == ' ' || (unsigned char)r[1] >= 0xE0)) r++;
            continue;
        }
        if (*r == '\n') { *w++ = *r++; line_start = 1; continue; }
        if (r[0] == '*' && r[1] == '*') { r += 2; continue; }
        if (*r == '`') { r++; continue; }
        if (*r == '*' && ((r[1] && r[1] != ' ') || (w > s && w[-1] != ' '))) { r++; continue; }  /* *italic* */
        if (r[0] == '!' && r[1] == '[') {  /* a picture: dropped */
            char* close = strchr(r, ']');
            if (close && close[1] == '(' && strchr(close, ')')) { r = strchr(close, ')') + 1; continue; }
        }
        if (*r == '<' && (r[1] == '/' || (r[1] >= 'a' && r[1] <= 'z'))) {  /* an HTML tag: dropped */
            char* close = strchr(r, '>');
            char* nl = strchr(r, '\n');
            if (close && (!nl || close < nl)) { r = close + 1; continue; }
        }
        if (*r == '[') {  /* [text](url) */
            char* close = strchr(r, ']');
            if (close && close[1] == '(' && strchr(close, ')')) {
                size_t n = (size_t)(close - r - 1);
                memmove(w, r + 1, n);
                w += n;
                r = strchr(close, ')') + 1;
                continue;
            }
        }
        if ((unsigned char)*r >= 0xE0) {  /* an emoji (and the space after it): dropped */
            int n;
            unsigned cp = update_utf8(r, &n);
            if (update_is_emoji(cp)) {
                r += n;
                if (*r == ' ' && (w == s || w[-1] == ' ' || w[-1] == '\n')) r++;
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* the newest release on GitHub: its tag, its notes (plain text, malloc'd) and the download address of one asset */
typedef struct {
    char tag[32];
    char* notes;
    char zip_url[512];
} UpdateInfo;

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

/* GET https://host/path into a malloc'd, 0-terminated buffer (up to `cap` bytes); 0 on success */
static int update_https_get(const wchar_t* host, const wchar_t* path, char** body, size_t cap) {
    int ok = -1;
    *body = NULL;
    HINTERNET s = WinHttpOpen(L"sonic-riders-recomp-update-check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return -1;
    WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE)
                    : NULL;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            size_t used = 0;
            char* buf = (char*)malloc(cap + 1);
            DWORD got = 0;
            while (buf && used < cap && WinHttpReadData(r, buf + used, (DWORD)(cap - used), &got) && got) used += got;
            if (buf) {
                buf[used] = 0;
                *body = buf;
                ok = 0;
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

/* blocking (seconds at most); call from a helper thread. asset_fmt: the wanted asset's name with %s for the tag
 * (e.g. "SonicRiders-PC-Setup-%s.zip"), or NULL. 0 on success; free info->notes after */
static int update_fetch_info_ex(UpdateInfo* info, const char* asset_fmt, int markdown) {
    memset(info, 0, sizeof *info);
    char* json;
    if (update_https_get(L"api.github.com", L"/repos/teclilla18/sonic-riders-recomp/releases/latest", &json, 1 << 20))
        return -1;
    int ok = update_tag_from_json(json, info->tag, sizeof info->tag) ? 0 : -1;
    if (ok == 0) {
        info->notes = update_json_string(json, "body");
        if (info->notes && !markdown) update_plain_text(info->notes);
        if (asset_fmt) {
            char name[128], pat[160];
            snprintf(name, sizeof name, asset_fmt, info->tag);
            snprintf(pat, sizeof pat, "\"name\":\"%s\"", name);  /* the asset object, then its download address */
            const char* a = strstr(json, pat);
            char* url = a ? update_json_string(a, "browser_download_url") : NULL;
            if (url && strlen(url) < sizeof info->zip_url) strcpy(info->zip_url, url);
            free(url);
        }
    }
    free(json);
    return ok;
}

static int update_fetch_info(UpdateInfo* info, const char* asset_fmt) { return update_fetch_info_ex(info, asset_fmt, 0); }

/* only the tag */
static int update_fetch_latest(char* tag, size_t n) {
    UpdateInfo info;
    if (update_fetch_info(&info, NULL)) return -1;
    snprintf(tag, n, "%s", info.tag);
    free(info.notes);
    return 0;
}

/* downloads an https URL (redirects followed) to `file`; *got / *total (bytes) are updated as it goes and a
 * non-zero *cancel stops it. 0 on success; the file is removed on failure */
static int update_download(const char* url, const wchar_t* file, volatile long long* got, volatile long long* total,
                           volatile int* cancel) {
    wchar_t wurl[1024], host[256], path[1024];
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024);
    URL_COMPONENTS u;
    memset(&u, 0, sizeof u);
    u.dwStructSize = sizeof u;
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &u)) return -1;
    int ok = -1;
    HINTERNET s = WinHttpOpen(L"sonic-riders-recomp-update", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return -1;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, u.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE)
                    : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        wchar_t cl[32];
        len = sizeof cl;
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &len,
                                WINHTTP_NO_HEADER_INDEX))
            *total = _wtoi64(cl);
        HANDLE f = status == 200 ? CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)
                                 : INVALID_HANDLE_VALUE;
        if (f != INVALID_HANDLE_VALUE) {
            char* buf = (char*)malloc(65536);  /* per call: the launcher downloads release-note pictures in parallel */
            DWORD n = 0, wr;
            ok = buf ? 0 : -1;
            *got = 0;
            for (; buf;) {
                if (*cancel || !WinHttpReadData(r, buf, 65536, &n)) { ok = -1; break; }
                if (!n) break;
                if (!WriteFile(f, buf, n, &wr, NULL) || wr != n) { ok = -1; break; }
                *got += n;
            }
            free(buf);
            if (ok == 0 && *total && *got != *total) ok = -1;  /* cut short */
            CloseHandle(f);
            if (ok) DeleteFileW(file);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#else
static int update_fetch_info(UpdateInfo* info, const char* asset_fmt) {
    (void)asset_fmt;
    memset(info, 0, sizeof *info);
    return -1;
}
static int update_fetch_latest(char* tag, size_t n) {
    (void)tag;
    (void)n;
    return -1;
}
#endif
