#pragma once
// Host-only replacements for ESP MQTT/FreeRTOS. Production EspMqtt.cpp is exercised unchanged.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include <string>
inline uint32_t fakeTime = 0;
inline unsigned fakeOutbox = 0;
inline uint32_t millis() {
    return fakeTime;
}
inline uint32_t esp_random() {
    static uint32_t n = 100;
    return ++n;
}
struct Ethernet {
    bool hasIP() {
        return true;
    }
    bool linkUp() {
        return true;
    }
};
inline Ethernet ETH;
using esp_event_base_t = const char *;
using esp_mqtt_client_handle_t = void *;
struct MqttEvent {
    const char *topic = nullptr, *data = nullptr;
    int topic_len = 0, data_len = 0, total_data_len = 0, current_data_offset = 0;
    bool retain = false;
};
using esp_mqtt_event_handle_t = MqttEvent *;
enum {
    MQTT_EVENT_CONNECTED = 1,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_ANY,
    MQTT_TRANSPORT_OVER_TCP,
    MQTT_PROTOCOL_V_3_1_1
};
struct esp_mqtt_client_config_t {
    struct {
        struct {
            const char *hostname = nullptr;
            int port = 0, transport = 0;
        } address;
    } broker;
    struct {
        const char *username = nullptr;
        struct {
            const char *password = nullptr;
        } authentication;
    } credentials;
    struct {
        int protocol_ver = 0, keepalive = 0;
        struct {
            const char *topic = nullptr, *msg = nullptr;
            int qos = 0, retain = 0;
        } last_will;
    } session;
    struct {
        int reconnect_timeout_ms = 0, timeout_ms = 0;
    } network;
    struct {
        int size = 0, out_size = 0;
    } buffer;
    struct {
        int limit = 0;
    } outbox;
    struct {
        int stack_size = 0;
    } task;
};
using Handler = void (*)(void *, esp_event_base_t, int32_t, void *);
inline Handler fakeHandler = nullptr;
struct Published {
    std::string topic, data;
    int qos, retain;
};
inline std::vector<Published> published;
inline std::vector<std::string> subscriptions;
inline esp_mqtt_client_handle_t esp_mqtt_client_init(esp_mqtt_client_config_t *) {
    return reinterpret_cast<void *>(1);
}
inline void esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int, Handler h, void *) {
    fakeHandler = h;
}
inline void esp_mqtt_client_start(esp_mqtt_client_handle_t) {}
inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char *topic, int) {
    subscriptions.push_back(topic);
    return 1;
}
inline int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t) {
    return int(fakeOutbox);
}
inline int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char *topic, const char *data,
                                   int, int qos, int retain, bool) {
    published.push_back({topic, data, qos, retain});
    return 1;
}
struct Queue {
    size_t capacity, bytes;
    std::deque<std::vector<char>> items;
};
using QueueHandle_t = Queue *;
constexpr int pdTRUE = 1, pdPASS = 1;
inline uint32_t pdMS_TO_TICKS(uint32_t n) {
    return n;
}
inline QueueHandle_t xQueueCreate(size_t count, size_t bytes) {
    return new Queue{count, bytes, {}};
}
inline int xQueueSend(QueueHandle_t q, const void *data, uint32_t) {
    if (q->items.size() == q->capacity)
        return 0;
    const char *p = static_cast<const char *>(data);
    q->items.emplace_back(p, p + q->bytes);
    return 1;
}
inline int xQueueReceive(QueueHandle_t q, void *data, uint32_t) {
    if (q->items.empty())
        return 0;
    memcpy(data, q->items.front().data(), q->bytes);
    q->items.pop_front();
    return 1;
}
inline void xQueueReset(QueueHandle_t q) {
    q->items.clear();
}
inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, void *) {
    return 1;
}
void EspMqttTestDrain();
