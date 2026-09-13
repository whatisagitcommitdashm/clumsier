#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "preset.h"
#include "../../external/cjson/cJSON.h"

// We reject unknown and duplicate fields. Silently ignoring a misspelled delay
// or a newer feature would make a shared preset behave differently than intended.
static bool fields(const cJSON *object, const char *allowed) {
    const cJSON *item, *other;
    if (!cJSON_IsObject(object)) return false;
    cJSON_ArrayForEach(item, object) {
        char needle[96];
        if (!item->string || strlen(item->string) > 90 || strchr(item->string, '|')) return false;
        snprintf(needle, sizeof(needle), "|%s|", item->string);
        if (!strstr(allowed, needle)) return false;
        for (other = item->next; other; other = other->next)
            if (!strcmp(item->string, other->string)) return false;
    }
    return true;
}
static const cJSON *get(const cJSON *object, const char *key) { return cJSON_GetObjectItemCaseSensitive(object, key); }
static bool stringField(const cJSON *object, const char *key, char *text, size_t size) {
    const cJSON *item = get(object, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) return false;
    strcpy(text, item->valuestring); return true;
}
static bool numberField(const cJSON *object, const char *key, uint32_t *number, uint32_t limit) {
    const cJSON *item = get(object, key);
    double value;
    if (!cJSON_IsNumber(item)) return false;
    value = item->valuedouble;
    if (!(value >= 0 && value <= limit) || value != (double)(uint32_t)value) return false;
    *number = (uint32_t)value; return true;
}
static int choice(const cJSON *object, const char *key, const char *a, const char *b, const char *c) {
    const cJSON *item = get(object, key);
    if (!cJSON_IsString(item)) return -1;
    if (!strcmp(item->valuestring, a)) return 0;
    if (!strcmp(item->valuestring, b)) return 1;
    if (c && !strcmp(item->valuestring, c)) return 2;
    return -1;
}
bool presetParse(const char *json, size_t length, Preset *preset, char *error) {
    Preset parsed = {0};
    cJSON *root = NULL;
    char *copy = NULL;
    const cJSON *traffic, *steps, *item;
    uint32_t version, port;
    char effect[16];
    int index = 0;
    if (!json || !length || length > PRESET_JSON_MAX || memchr(json, 0, length)) goto invalid;
    // cJSON strings cannot represent embedded NUL. Reject that escape rather
    // than accepting a truncated name or filter. Literal backslash forms are
    // conservatively rejected too.
    copy = malloc(length + 1);
    if (!copy) goto invalid;
    memcpy(copy, json, length);
    copy[length] = '\0';
    if (strstr(copy, "\\u0000")) goto invalid;
    root = cJSON_ParseWithLengthOpts(copy, length + 1, NULL, 1);
    free(copy); copy = NULL;
    if (!fields(root, "|format_version|name|description|server|effect|mode|policy|loop|traffic|steps|")) goto invalid;
    if (get(root, "server") && !stringField(root, "server", parsed.server, sizeof(parsed.server))) goto invalid;
    if (!numberField(root, "format_version", &version, 1) || version != 1 ||
        !stringField(root, "effect", effect, sizeof(effect)) || strcmp(effect, "lag") ||
        !stringField(root, "name", parsed.name, sizeof(parsed.name)) ||
        !stringField(root, "description", parsed.description, sizeof(parsed.description)) || !cJSON_IsBool(get(root, "loop"))) goto invalid;
    parsed.loop = cJSON_IsTrue(get(root, "loop")) != 0;
    parsed.mode = (PresetMode)choice(root, "mode", "added_delay", "target_ping", NULL);
    parsed.policy = (DelayPolicy)choice(root, "policy", "inbound", "outbound", "both");
    traffic = get(root, "traffic");
    if (!fields(traffic, "|protocol|direction|remote_address|remote_port|native_backend|native_filter|")) goto invalid;
    parsed.target.traffic.protocol = (TrafficProtocol)choice(traffic, "protocol", "any", "tcp", "udp");
    parsed.target.traffic.direction = (TrafficDirection)choice(traffic, "direction", "both", "inbound", "outbound");
    if (!stringField(traffic, "remote_address", parsed.target.traffic.remote_address, TRAFFIC_ADDRESS_SIZE) ||
        !numberField(traffic, "remote_port", &port, 65535) ||
        !stringField(traffic, "native_backend", parsed.target.native_backend, sizeof(parsed.target.native_backend)) ||
        !stringField(traffic, "native_filter", parsed.target.native_filter, NATIVE_FILTER_SIZE)) goto invalid;
    parsed.target.traffic.remote_port = (uint16_t)port;
    steps = get(root, "steps");
    if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) < 1 || cJSON_GetArraySize(steps) > PRESET_MAX_STEPS) goto invalid;
    cJSON_ArrayForEach(item, steps) {
        PresetStep *step = &parsed.steps[index++];
        if (!fields(item, "|name|note|type|target_ms|inbound_ms|outbound_ms|")) goto invalid;
        if (!stringField(item, "name", step->name, sizeof(step->name)) || !stringField(item, "note", step->note, sizeof(step->note))) goto invalid;
        step->kind = (StepKind)choice(item, "type", "delay", "target", "lowest");
        if (step->kind == STEP_TARGET) {
            if (!numberField(item, "target_ms", &step->target_ms, PING_MAX_MS) || get(item, "inbound_ms") || get(item, "outbound_ms")) goto invalid;
        } else if (step->kind == STEP_DELAY) {
            if (!numberField(item, "inbound_ms", &step->inbound_ms, LAG_MAX_MS) ||
                !numberField(item, "outbound_ms", &step->outbound_ms, LAG_MAX_MS) || get(item, "target_ms")) goto invalid;
        } else if (step->kind != STEP_LOWEST || get(item, "target_ms") || get(item, "inbound_ms") || get(item, "outbound_ms")) goto invalid;
    }
    parsed.step_count = (size_t)index;
    cJSON_Delete(root);
    if (!presetValidate(&parsed, error)) return false;
    *preset = parsed; return true;
invalid:
    free(copy);
    cJSON_Delete(root);
    strcpy(error, "Invalid preset JSON: check version, required fields, types, duplicates, and limits."); return false;
}
char *presetSerialize(const Preset *preset, char *error) {
    cJSON *root = NULL, *traffic, *steps, *step;
    char *text;
    size_t i;
    const char *protocols[] = {"any", "tcp", "udp"}, *directions[] = {"both", "inbound", "outbound"};
    const char *policies[] = {"inbound", "outbound", "both"}, *types[] = {"delay", "target", "lowest"};
    if (!presetValidate(preset, error)) return NULL;
    root = cJSON_CreateObject();
    if (!root) goto failed;
#define STRING(o,k,v) if (!cJSON_AddStringToObject(o,k,v)) goto failed
#define NUMBER(o,k,v) if (!cJSON_AddNumberToObject(o,k,v)) goto failed
    NUMBER(root,"format_version",1); STRING(root,"name",preset->name); STRING(root,"description",preset->description);
    if (preset->server[0]) { STRING(root,"server",preset->server); }
    STRING(root,"effect","lag"); STRING(root,"mode",preset->mode == PRESET_TARGET_PING ? "target_ping" : "added_delay");
    STRING(root,"policy",policies[preset->policy]);
    if (!cJSON_AddBoolToObject(root,"loop",preset->loop)) goto failed;
    traffic = cJSON_AddObjectToObject(root,"traffic"); if (!traffic) goto failed;
    STRING(traffic,"protocol",protocols[preset->target.traffic.protocol]);
    STRING(traffic,"direction",directions[preset->target.traffic.direction]);
    STRING(traffic,"remote_address",preset->target.traffic.remote_address); NUMBER(traffic,"remote_port",preset->target.traffic.remote_port);
    STRING(traffic,"native_backend",preset->target.native_backend); STRING(traffic,"native_filter",preset->target.native_filter);
    steps = cJSON_AddArrayToObject(root,"steps"); if (!steps) goto failed;
    for (i = 0; i < preset->step_count; ++i) {
        const PresetStep *value = &preset->steps[i];
        step = cJSON_CreateObject(); if (!step) goto failed;
        if (!cJSON_AddItemToArray(steps,step)) { cJSON_Delete(step); goto failed; }
        STRING(step,"name",value->name); STRING(step,"note",value->note); STRING(step,"type",types[value->kind]);
        if (value->kind == STEP_TARGET) { NUMBER(step,"target_ms",value->target_ms); }
        else if (value->kind == STEP_DELAY) { NUMBER(step,"inbound_ms",value->inbound_ms); NUMBER(step,"outbound_ms",value->outbound_ms); }
    }
    text = cJSON_Print(root); cJSON_Delete(root);
    if (text) return text;
    strcpy(error,"Could not allocate preset JSON."); return NULL;
failed:
    cJSON_Delete(root); strcpy(error,"Could not allocate preset JSON."); return NULL;
#undef STRING
#undef NUMBER
}
