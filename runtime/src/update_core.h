/* Update check, shared by the game (update_check.c) and the Windows launcher (launcher/launcher.cpp):
 * asks GitHub for the newest release's tag and compares it with this build's version.
 * Only the release tag is fetched (one HTTPS GET to api.github.com, no data sent beyond the request).
 * Fetching is implemented on Windows (WinHTTP); elsewhere update_fetch_latest fails. Header-only so
 * that the C runtime and the C++ launcher share it without another build step. */
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

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
/* blocking (a few seconds at most: timeouts set); call from a helper thread. 0 on success */
static int update_fetch_latest(char* tag, size_t n) {
    int ok = -1;
    HINTERNET s = WinHttpOpen(L"sonic-riders-recomp-update-check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return -1;
    WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/teclilla18/sonic-riders-recomp/releases/latest", NULL,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : NULL;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            size_t cap = 1 << 16, used = 0;  /* tag_name is near the start of the object */
            char* buf = (char*)malloc(cap + 1);
            DWORD got = 0;
            while (buf && used < cap && WinHttpReadData(r, buf + used, (DWORD)(cap - used), &got) && got) used += got;
            if (buf) {
                buf[used] = 0;
                if (update_tag_from_json(buf, tag, n)) ok = 0;
                free(buf);
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#else
static int update_fetch_latest(char* tag, size_t n) {
    (void)tag;
    (void)n;
    return -1;
}
#endif
