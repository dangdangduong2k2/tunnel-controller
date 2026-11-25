#include "FotaProtocol.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
namespace fota {
static bool hex(const char *s, size_t n) {
    if (strlen(s) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}
static bool string(cJSON *o, const char *name, char *out, size_t cap) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= cap)
        return false;
    for (const char *p = v->valuestring; *p; p++)
        if (*p < 33 || *p > 126 || *p == '"' || *p == '\\')
            return false;
    strcpy(out, v->valuestring);
    return true;
}
static bool number(cJSON *o, const char *name, uint32_t &out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > 4294967295.0 || floor(v->valuedouble) != v->valuedouble)
        return false;
    out = uint32_t(v->valuedouble);
    return true;
}
bool decode(const char *data, size_t length, Request &out) {
    if (!length || length > MAX_JSON || memchr(data, 0, length))
        return false;
    // No nesting/escapes: bounded parse depth and no embedded-NUL aliases.
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        if (c == '\\')
            return false;
        if (c == '"')
            quoted = !quoted;
        if (!quoted) {
            if (c == '[' || c == ']')
                return false;
            if (c == '{' && ++depth > 1)
                return false;
            if (c == '}' && (!depth--))
                return false;
        }
    }
    if (quoted || depth)
        return false;
    const char *end = nullptr;
    cJSON *o = cJSON_ParseWithLengthOpts(data, length, &end, false);
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        return false;
    }
    while (end < data + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
        ++end;
    bool ok = end == data + length;
    unsigned count = 0;
    for (cJSON *a = o->child; a; a = a->next) {
        ++count;
        for (cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string))
                ok = false;
    }
    Request q;
    ok = ok && count == 13 && string(o, "job", q.job, sizeof(q.job)) &&
         string(o, "session", q.session, sizeof(q.session)) &&
         string(o, "client", q.client, sizeof(q.client)) &&
         string(o, "url", q.url, sizeof(q.url)) &&
         string(o, "target", q.target, sizeof(q.target)) &&
         string(o, "sha256", q.sha256, sizeof(q.sha256)) &&
         string(o, "signature", q.signature, sizeof(q.signature)) && number(o, "epoch", q.epoch) &&
         number(o, "id", q.id) && number(o, "seen_ms", q.seen_ms) &&
         number(o, "version", q.version) && number(o, "size", q.size) &&
         number(o, "unix_time", q.unix_time);
    cJSON_Delete(o);
    size_t sig = strlen(q.signature);
    ok = ok && hex(q.job, 32) && hex(q.session, 16) && hex(q.client, 16) && hex(q.sha256, 64) &&
         sig >= 128 && sig <= 144 && sig % 2 == 0 && hex(q.signature, sig) && q.id;
    if (ok)
        out = q;
    return ok;
}
bool canonical(const Request &q, char *out, size_t size) {
    int n =
        snprintf(out, size, "TUNNEL-FOTA-1\n%s\n%s\n%lu\n%s\n%lu\n%lu\n%s\n%s\n%lu\n%lu\n%s\n%lu\n",
                 q.job, q.session, (unsigned long)q.epoch, q.client, (unsigned long)q.id,
                 (unsigned long)q.seen_ms, q.url, q.target, (unsigned long)q.version,
                 (unsigned long)q.size, q.sha256, (unsigned long)q.unix_time);
    return n > 0 && size_t(n) < size;
}
const char *validate(const Request &q, const char *session, uint32_t epoch, uint32_t now,
                     uint32_t /*running*/, size_t slot, const char *prefix) {
    if (strcmp(q.session, session) || q.epoch != epoch)
        return "session";
    if (uint32_t(now - q.seen_ms) > 5000)
        return "stale";
    if (strcmp(q.target, TARGET))
        return "target";
    // Version identifies the selected image, not an upgrade-only policy.
    // A fresh signed job may reinstall or downgrade a compatible application.
    if (!q.version)
        return "version";
    if (q.size < 256 || q.size > slot)
        return "size";
    if (strncmp(prefix, "https://", 8) || strlen(prefix) < 10 ||
        prefix[strlen(prefix) - 1] != '/' || strncmp(q.url, prefix, strlen(prefix)) ||
        strchr(q.url, '#') || strchr(q.url, '@') || strstr(q.url, ".."))
        return "url";
    // The allowed prefix is exact; the remaining path is one plain filename.
    // Reject percent-encoded traversal, alternate paths and query redirects.
    const char *leaf=q.url+strlen(prefix);
    if (!*leaf) return "url";
    for (const char *p=leaf;*p;++p)
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
              (*p>='0' && *p<='9') || *p=='.' || *p=='_' || *p=='-')) return "url";
    if (q.unix_time < 1735689600UL)
        return "time";
    return nullptr;
}
int Replay::check(const Request &q) {
    bool space = false;
    for (auto &e : entries) {
        if (!e.used) {
            space = true;
            continue;
        }
        if (!strcmp(e.q.client, q.client)) {
            if (q.id > e.q.id)
                return 0;
            char a[768], b[768];
            canonical(q, a, sizeof(a));
            canonical(e.q, b, sizeof(b));
            return q.id == e.q.id && !strcmp(a, b) && !strcmp(q.signature, e.q.signature) ? 1 : -1;
        }
    }
    return space ? 0 : -2;
}
void Replay::remember(const Request &q) {
    for (auto &e : entries)
        if (e.used && !strcmp(e.q.client, q.client)) {
            e.q = q;
            return;
        }
    for (auto &e : entries)
        if (!e.used) {
            e.used = true;
            e.q = q;
            return;
        }
}
void Replay::reset() {
    for (auto &e : entries)
        e = Entry{};
}
} // namespace fota
