#ifdef TUNNEL_MQTT_HOST_TEST
#include "mqtt_transport_stubs.h"
#else
#include <Arduino.h>
#include <ETH.h>
#include <mqtt_client.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <freertos/queue.h>
#endif
#include <atomic>
#include "EspMqtt.h"
#ifndef TUNNEL_MQTT_HOST_TEST
#include "EspFota.h"
#include "../../common/Version.h"
#endif
#include "../../protocol/mqtt/MqttApi.h"
#include "EspConfig.h"

static esp_mqtt_client_handle_t client = nullptr;
static tunnelmqtt::Api *api = nullptr;
static QueueHandle_t incoming = nullptr, outgoing = nullptr, reads = nullptr,
                     stateOutgoing = nullptr;
static std::atomic<bool> connected(false), fault(false);
static std::atomic<uint32_t> generation(0);
static uint32_t handledGeneration = 0;
static bool wasConnected = false;
bool EspMqttConnected() {
    return connected.load() && ETH.hasIP() && ETH.linkUp();
}
static char cmdTopic[80], getTopic[80], stateTopic[80], ackTopic[80], availabilityTopic[80];
static char otaCmdTopic[90], otaGetTopic[90], otaAckTopic[90], otaStateTopic[90];
#ifndef TUNNEL_MQTT_HOST_TEST
static char discoveryTopic[64], discoveryPayload[256];
static uint32_t discoveryAt = 0, discoveryGeneration = 0;
static bool discoverySent = false;
// Metadata only, on the MQTT sender task; never blocks the control loop.
static void announceDiscovery() {
    if (!EspMqttConnected() || !discoveryTopic[0] ||
        esp_mqtt_client_get_outbox_size(client) >= 4096)
        return;
    const uint32_t now = millis(), g = generation.load();
    if (discoverySent && discoveryGeneration == g && uint32_t(now - discoveryAt) < 15000)
        return;
    if (esp_mqtt_client_enqueue(client, discoveryTopic, discoveryPayload, 0, 1, 1, true) >= 0) {
        discoveryAt = now;
        discoveryGeneration = g;
        discoverySent = true;
    }
}
#endif
struct Incoming {
    uint32_t generation;
    uint16_t length;
    bool retained;
    char data[513];
};
struct Outgoing {
    uint32_t generation;
    bool ack;
    char data[1400];
};
struct ReadIncoming {
    uint32_t generation;
    uint16_t length;
    char data[129];
};

// MQTT task only copies messages; all machine calls run on the control loop.
static void event(void *, esp_event_base_t, int32_t id, void *data) {
    auto e = static_cast<esp_mqtt_event_handle_t>(data);
    if (id == MQTT_EVENT_CONNECTED) {
        ++generation;
        connected = true;
        esp_mqtt_client_subscribe(client, cmdTopic, 1);
        esp_mqtt_client_subscribe(client, getTopic, 1);
#ifndef TUNNEL_MQTT_HOST_TEST
        esp_mqtt_client_subscribe(client, otaCmdTopic, 1);
        esp_mqtt_client_subscribe(client, otaGetTopic, 1);
        EspFotaGet();
#endif
        esp_mqtt_client_enqueue(client, availabilityTopic, "{\"online\":true}", 0, 1, 1, true);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        connected = false;
        ++generation;
    } else if (id == MQTT_EVENT_DATA) {
        if (!e->topic || !e->data)
            return;
#ifndef TUNNEL_MQTT_HOST_TEST
        if (e->topic_len == int(strlen(otaCmdTopic)) && !memcmp(e->topic,otaCmdTopic,e->topic_len)) {
            if (!e->current_data_offset && e->total_data_len == e->data_len && e->data_len > 0)
                EspFotaReceive(e->data,e->data_len,e->retain);
            return;
        }
        if (e->topic_len == int(strlen(otaGetTopic)) && !memcmp(e->topic,otaGetTopic,e->topic_len)) {
            if (!e->retain && !e->current_data_offset && e->total_data_len == e->data_len && e->data_len == 2 && !memcmp(e->data,"{}",2)) EspFotaGet();
            return;
        }
#endif
        bool get =
            e->topic_len == int(strlen(getTopic)) && !memcmp(e->topic, getTopic, strlen(getTopic));
        if (get) {
            if (e->retain || e->current_data_offset || e->total_data_len != e->data_len ||
                e->data_len < 1 || e->data_len > 128)
                return;
            ReadIncoming q = {};
            q.generation = generation.load();
            q.length = uint16_t(e->data_len);
            memcpy(q.data, e->data, q.length);
            // Read traffic cannot fill the command queue or trigger a control fault.
            xQueueSend(reads, &q, 0);
            return;
        }
        if (e->topic_len != int(strlen(cmdTopic)) || memcmp(e->topic, cmdTopic, strlen(cmdTopic)))
            return;
        // Buffer is larger than the command limit: fragmented/oversized commands are never
        // executed.
        if (e->current_data_offset || e->total_data_len != e->data_len || e->data_len < 1 ||
            e->data_len > 512)
            return;
        Incoming q = {};
        q.generation = generation.load();
        q.length = uint16_t(e->data_len);
        q.retained = e->retain;
        memcpy(q.data, e->data, q.length);
        if (xQueueSend(incoming, &q, 0) != pdTRUE)
            fault = true;
    }
}
// Even enqueue can wait for the MQTT library's lock; keep it off the motor loop.
static void transmit(const Outgoing &out) {
    if (!connected || out.generation != generation.load())
        return;
    // Reserve half the bounded MQTT outbox for ACKs/control during read bursts.
    if (!out.ack && esp_mqtt_client_get_outbox_size(client) >= 4096)
        return;
    if (esp_mqtt_client_enqueue(client, out.ack ? ackTopic : stateTopic, out.data, 0, 1, 0, true) <
            0 &&
        out.ack)
        fault = true;
}
static void drain(uint32_t wait) {
    Outgoing out;
    if (xQueueReceive(outgoing, &out, wait) == pdTRUE)
        transmit(out);
    if (xQueueReceive(stateOutgoing, &out, 0) == pdTRUE)
        transmit(out);
#ifndef TUNNEL_MQTT_HOST_TEST
    EspFotaTransmit();
    announceDiscovery();
#endif
}
static void sender(void *) {
    for (;;)
        drain(pdMS_TO_TICKS(10));
}
#ifdef TUNNEL_MQTT_HOST_TEST
void EspMqttTestDrain() {
    drain(0);
}
#endif
static void send(bool ack, const char *data) {
    Outgoing out = {};
    out.ack = ack;
    out.generation = handledGeneration;
    strncpy(out.data, data, sizeof(out.data) - 1);
    if (xQueueSend(ack ? outgoing : stateOutgoing, &out, 0) != pdTRUE && ack)
        fault = true;
}
void EspMqttFotaPublish(bool ack,const char *data) {
    if (!EspMqttConnected() || esp_mqtt_client_get_outbox_size(client)>=4096) return;
    esp_mqtt_client_enqueue(client,ack?otaAckTopic:otaStateTopic,data,0,1,0,true);
}
void EspMqttBegin(mb::Core &shared) {
    static tunnelmqtt::Api instance(shared);
    api = &instance;
    const cfg::Settings &settings = EspConfigGet();
    if (!settings.mqttEnabled || !settings.host[0])
        return;
    snprintf(cmdTopic, sizeof(cmdTopic), "%s/cmd", settings.root);
    snprintf(getTopic, sizeof(getTopic), "%s/get", settings.root);
    snprintf(stateTopic, sizeof(stateTopic), "%s/state", settings.root);
    snprintf(ackTopic, sizeof(ackTopic), "%s/ack", settings.root);
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", settings.root);
    snprintf(otaCmdTopic,sizeof(otaCmdTopic),"%s/ota/cmd",settings.root);
    snprintf(otaGetTopic,sizeof(otaGetTopic),"%s/ota/get",settings.root);
    snprintf(otaAckTopic,sizeof(otaAckTopic),"%s/ota/ack",settings.root);
    snprintf(otaStateTopic,sizeof(otaStateTopic),"%s/ota/state",settings.root);
#ifndef TUNNEL_MQTT_HOST_TEST
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char deviceId[13];
        snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(discoveryTopic, sizeof(discoveryTopic), "tunnel/discovery/v1/%s", deviceId);
        snprintf(discoveryPayload, sizeof(discoveryPayload),
                 "{\"discovery_version\":1,\"device_id\":\"%s\",\"model\":\"tunnel-esp32s3-v1\","
                 "\"root\":\"%s\",\"firmware\":%u}", deviceId, settings.root, TUNNEL_FIRMWARE_REGISTER);
    }
#endif
    incoming = xQueueCreate(8, sizeof(Incoming));
    outgoing = xQueueCreate(4, sizeof(Outgoing));
    reads = xQueueCreate(4, sizeof(ReadIncoming));
    stateOutgoing = xQueueCreate(4, sizeof(Outgoing));
    if (!incoming || !outgoing || !reads || !stateOutgoing)
        return;
    esp_mqtt_client_config_t config = {};
    config.broker.address.hostname = settings.host;
    config.broker.address.port = settings.mqttPort;
    config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    if (settings.user[0])
        config.credentials.username = settings.user;
    if (settings.password[0])
        config.credentials.authentication.password = settings.password;
    config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.session.keepalive = 5;
    config.session.last_will.topic = availabilityTopic;
    config.session.last_will.msg = "{\"online\":false}";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;
    config.network.reconnect_timeout_ms = 3000;
    config.network.timeout_ms = 1000;
    config.buffer.size = 2048;
    config.buffer.out_size = 1600;
    config.outbox.limit = 8192;
    config.task.stack_size = 6144;
    client = esp_mqtt_client_init(&config);
    if (!client)
        return;
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, event, nullptr);
    if (xTaskCreate(sender, "tunnel_mqtt_tx", 4096, nullptr, 1, nullptr) != pdPASS)
        return;
    esp_mqtt_client_start(client);
}
const char *EspMqttSession() { return api ? api->session() : ""; }
uint32_t EspMqttEpoch() { return api ? api->generation() : 0; }
void EspMqttFence() {
    if (!api)
        return;
    api->fence();
    if (incoming)
        xQueueReset(incoming);
}
void EspMqttPoll() {
    if (!client || !api)
        return;
    const uint32_t g = generation.load();
    bool live = connected && ETH.linkUp() && ETH.hasIP();
    if (g != handledGeneration || wasConnected != live) {
        api->disconnect();
        if (incoming)
            xQueueReset(incoming);
        xQueueReset(reads);
        xQueueReset(stateOutgoing);
        xQueueReset(outgoing);
        handledGeneration = g;
        if (live) {
            char session[17];
            snprintf(session, sizeof(session), "%08lx%08lx", (unsigned long)esp_random(),
                     (unsigned long)esp_random());
            api->connect(session);
        }
        wasConnected = live;
        return; // allow the coordinator to process a disconnect stop first
    }
    if (fault.exchange(false)) {
        api->disconnect();
        xQueueReset(incoming);
        xQueueReset(reads);
        return;
    }
    if (!live)
        return;
    Incoming q;
    if (xQueueReceive(incoming, &q, 0) == pdTRUE && q.generation == handledGeneration) {
        tunnelmqtt::Request request;
        if (tunnelmqtt::decode(q.data, q.length, request)) {
            auto reply = api->process(request, q.retained, millis());
            char ack[320];
            snprintf(ack, sizeof(ack),
                     "{\"api_version\":1,\"session\":\"%s\",\"epoch\":%lu,\"client\":\"%s\",\"id\":"
                     "%lu,\"code\":%u,\"exception\":%u,\"result\":%u,\"sequence\":%u}",
                     request.session, (unsigned long)request.epoch, request.client,
                     (unsigned long)request.id, reply.code, reply.exception, reply.result,
                     reply.sequence);
            send(true, ack);
            return; // publish state only after the coordinator has applied any STOP fence
        }
    }
    ReadIncoming get;
    if (xQueueReceive(reads, &get, 0) == pdTRUE && get.generation == handledGeneration) {
        tunnelmqtt::ReadRequest request;
        char json[1400];
        if (tunnelmqtt::decodeGet(get.data, get.length, request) &&
            tunnelmqtt::encodeState(*api, request, millis(), json, sizeof(json)))
            send(false, json);
    }
}
