#include "../../common/OperationCatalog.h"
#include "../../common/Version.h"
#include "MqttApi.h"
#include <cJSON.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
namespace tunnelmqtt {
static bool integer(cJSON *o, double max, uint32_t &v) {
    if (!cJSON_IsNumber(o) || !isfinite(o->valuedouble) || o->valuedouble < 0 ||
        o->valuedouble > max || floor(o->valuedouble) != o->valuedouble)
        return false;
    v = uint32_t(o->valuedouble);
    return true;
}
static bool token(cJSON *o, char *s) {
    if (!cJSON_IsString(o) || strlen(o->valuestring) != 16)
        return false;
    for (unsigned i = 0; i < 16; i++) {
        char c = o->valuestring[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    memcpy(s, o->valuestring, 17);
    return true;
}
bool decode(const char *text, size_t length, Request &q) {
    if (!text || !length || length > 512 || memchr(text, 0, length))
        return false;
    // Reject deep nesting before entering cJSON (including malicious arrays).
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        // No escapes are needed by this ASCII command schema. This also rejects
        // embedded NUL/unicode escapes that would truncate C string comparisons.
        if (c == '\\')
            return false;
        if (c == '"' && !escape)
            quoted = !quoted;
        if (!quoted) {
            if (c == '{' || c == '[') {
                if (++depth > 2)
                    return false;
            }
            if (c == '}' || c == ']') {
                if (!depth)
                    return false;
                --depth;
            }
        }
    }
    if (quoted || depth)
        return false;
    char buffer[513];
    memcpy(buffer, text, length);
    buffer[length] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(buffer, length + 1, nullptr, true);
    if (!root)
        return false;
    bool ok = cJSON_IsObject(root);
    unsigned fields = 0;
    for (cJSON *p = root->child; p && ok; p = p->next) {
        ++fields;
        if (!p->string) {
            ok = false;
            break;
        }
        for (cJSON *other = p->next; other; other = other->next)
            if (other->string && !strcmp(p->string, other->string))
                ok = false;
        const char *names[] = {"session", "client", "epoch", "id",     "seen_ms",
                               "action",  "op",     "args",  "method", "params"};
        bool known = false;
        for (auto n : names)
            if (!strcmp(n, p->string))
                known = true;
        if (!known)
            ok = false;
    }
    Request v;
    auto item = [&](const char *k) { return cJSON_GetObjectItemCaseSensitive(root, k); };
    ok = ok && token(item("session"), v.session) && token(item("client"), v.client) &&
         integer(item("epoch"), 4294967295.0, v.epoch) && integer(item("id"), 4294967295.0, v.id) &&
         v.id && integer(item("seen_ms"), 4294967295.0, v.seen);
    cJSON *method = item("method");
    if (method) {
        // Exactly five envelope fields plus method and params; no mixed schemas.
        ok = ok && fields == 7 && cJSON_IsString(method) && !item("action") && !item("op") &&
             !item("args");
        const tunnelcontract::Operation *spec = nullptr;
        if (ok)
            for (const auto &o : tunnelcontract::OPERATIONS)
                if (!strcmp(method->valuestring, o.method)) {
                    spec = &o;
                    break;
                }
        cJSON *params = item("params");
        ok = ok && spec && cJSON_IsObject(params);
        if (ok) {
            unsigned count = 0;
            for (cJSON *p = params->child; p && ok; p = p->next) {
                ++count;
                bool known = false;
                for (unsigned i = 0; i < spec->count; i++)
                    if (p->string && !strcmp(p->string, spec->params[i].name))
                        known = true;
                if (!known)
                    ok = false;
                for (cJSON *other = p->next; other; other = other->next)
                    if (p->string && other->string && !strcmp(p->string, other->string))
                        ok = false;
            }
            ok = ok && count == spec->count;
            for (unsigned i = 0; i < spec->count && ok; i++) {
                uint32_t value = 0;
                const auto &p = spec->params[i];
                ok = integer(cJSON_GetObjectItemCaseSensitive(params, p.name), p.maximum, value) &&
                     value >= p.minimum;
                v.args[i] = uint16_t(value);
            }
            v.action = EXECUTE;
            v.op = spec->op;
        }
    } else {
        cJSON *a = item("action");
        if (!cJSON_IsString(a))
            ok = false;
        else {
            const char *actions[] = {"claim", "heartbeat", "release",
                                     "stop",  "emergency", "execute"};
            bool found = false;
            for (unsigned i = 0; i < 6; i++)
                if (!strcmp(a->valuestring, actions[i])) {
                    v.action = Action(i + 1);
                    found = true;
                }
            if (!found)
                ok = false;
        }
        if (v.action == EXECUTE) {
            uint32_t op = 0;
            cJSON *args = item("args");
            if (fields != 8 || item("params") || !integer(item("op"), 65535, op) ||
                !cJSON_IsArray(args) || cJSON_GetArraySize(args) != 6)
                ok = false;
            v.op = uint16_t(op);
            for (unsigned i = 0; i < 6 && ok; i++) {
                uint32_t n = 0;
                if (!integer(cJSON_GetArrayItem(args, int(i)), 65535, n))
                    ok = false;
                v.args[i] = uint16_t(n);
            }
        } else if (fields != 6 || item("params") || item("op") || item("args"))
            ok = false;
    }
    cJSON_Delete(root);
    if (ok)
        q = v;
    return ok;
}
bool decodeGet(const char *text, size_t length, ReadRequest &q) {
    if (!text || !length || length > 128 || memchr(text, 0, length))
        return false;
    // A flat ASCII object only. Bound parser depth before calling cJSON.
    unsigned depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\\')
            return false;
        if (c == '"')
            quoted = !quoted;
        if (!quoted) {
            if (c == '[' || c == ']')
                return false;
            if (c == '{' && ++depth > 1)
                return false;
            if (c == '}') {
                if (!depth)
                    return false;
                --depth;
            }
        }
    }
    if (depth || quoted)
        return false;
    char buffer[129];
    memcpy(buffer, text, length);
    buffer[length] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(buffer, length + 1, nullptr, true);
    if (!root)
        return false;
    bool ok = cJSON_IsObject(root);
    unsigned clients = 0, ids = 0;
    for (cJSON *p = root->child; p && ok; p = p->next) {
        if (p->string && !strcmp(p->string, "client"))
            ++clients;
        else if (p->string && !strcmp(p->string, "id"))
            ++ids;
        else
            ok = false;
    }
    ReadRequest v;
    ok = ok && clients == 1 && ids == 1 &&
         token(cJSON_GetObjectItemCaseSensitive(root, "client"), v.client) &&
         integer(cJSON_GetObjectItemCaseSensitive(root, "id"), 4294967295.0, v.id) && v.id;
    cJSON_Delete(root);
    if (ok)
        q = v;
    return ok;
}
bool encodeState(Api &api, const ReadRequest &q, uint32_t now, char *out, size_t size) {
    if (!out || !size)
        return false;
    uint16_t regs[96];
    api.snapshot(regs);
    int pos =
        snprintf(out, size,
                 "{\"firmware\":\"" TUNNEL_FIRMWARE_NAME
                 "\",\"api_version\":1,\"request_client\":\"%s\",\"request_id\":%lu,\"session\":\"%"
                 "s\",\"epoch\":%lu,\"ms\":%lu,\"owner_client\":\"%s\",\"registers\":[",
                 q.client, (unsigned long)q.id, api.session(), (unsigned long)api.generation(),
                 (unsigned long)now, api.owner());
    if (pos < 0 || size_t(pos) >= size)
        return false;
    for (unsigned i = 0; i < 96; i++) {
        int n = snprintf(out + pos, size - size_t(pos), "%s%u", i ? "," : "", regs[i]);
        if (n < 0 || size_t(n) >= size - size_t(pos))
            return false;
        pos += n;
    }
    // Named fields are additive. Numeric registers remain the compatibility contract.
    int n = snprintf(out + pos, size - size_t(pos),
                     "],\"machine_state\":%u,\"fault\":%u,\"mode\":%u,\"speed_level\":%u,\"epc_"
                     "count\":%u,\"total_reads\":%lu,\"scan_elapsed_ms\":%lu,\"reader_state\":%u,"
                     "\"owner\":%u,\"lease_ms\":%u,\"ip\":\"%u.%u.%u.%u\"}",
                     regs[2], regs[3], regs[4], regs[9], regs[10],
                     (unsigned long)((uint32_t(regs[11]) << 16) | regs[12]),
                     (unsigned long)((uint32_t(regs[13]) << 16) | regs[14]), regs[20], regs[16],
                     regs[17], regs[25] >> 8, regs[25] & 255, regs[26] >> 8, regs[26] & 255);
    return n >= 0 && size_t(n) < size - size_t(pos);
}
} // namespace tunnelmqtt
