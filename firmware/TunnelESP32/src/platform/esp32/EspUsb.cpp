#include <Arduino.h>
#include <atomic>
#include <EspUsbReaderHost.h>
#include "freertos/queue.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "EspPlatform.h"
extern "C" {
#include "../compat/usb_host.h"
#include "../compat/usbh_cp210x.h"
#include "../../reader/zk7182.h"
}
USBH_HandleTypeDef hUsbHostFS = {};
struct Device {
    uint16_t vid, pid;
};
struct Transfer {
    uint32_t epoch, rfToken;
    uint8_t length, data[64];
};
struct Event {
    uint32_t epoch;
    uint8_t kind;
};
enum { CONNECTED, LOST, TX_OK, TX_FAIL };
static QueueHandle_t devices, rxQueue, txQueue, events;
static std::atomic<bool> lost(false), recover(false), overrun(false), wireReady(false);
static std::atomic<uint32_t> epoch(0);
static cdc_acm_dev_hdl_t handle;
static uint32_t ownerEpoch = 0;
static bool ownerReady = false, ownerTxPending = false, installed = false;
static uint8_t *receiveBuffer = nullptr;
static uint16_t receiveCapacity = 0, lastReceived = 0;

static void post(uint8_t kind, uint32_t generation) {
    Event e = {generation, kind};
    if (xQueueSend(events, &e, 0) != pdTRUE)
        overrun = true;
}
static bool filter(const usb_device_desc_t *, uint8_t *cfg) {
    *cfg = 1;
    return true;
}
static void discovered(usb_device_handle_t dev) {
    const usb_device_desc_t *d = nullptr;
    if (usb_host_get_device_descriptor(dev, &d) == ESP_OK) {
        Device n = {d->idVendor, d->idProduct};
        if (xQueueSend(devices, &n, 0) != pdTRUE)
            overrun = true;
    }
}
static bool rxCallback(const uint8_t *data, size_t size, void *) {
    while (size) {
        Transfer msg = {};
        msg.epoch = epoch.load();
        msg.length = uint8_t(size > 64 ? 64 : size);
        memcpy(msg.data, data, msg.length);
        if (xQueueSend(rxQueue, &msg, 0) != pdTRUE)
            overrun = true;
        size -= msg.length;
        data += msg.length;
    }
    return true;
}
static void eventCallback(const cdc_acm_host_dev_event_data_t *event, void *) {
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED || event->type == CDC_ACM_HOST_ERROR) {
        wireReady = false;
        lost = true;
    }
}
static void hostTask(void *) {
    for (;;) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            lost = true;
            vTaskDelay(1);
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            usb_host_device_free_all();
    }
}
static void closeDevice() {
    wireReady = false;
    ++epoch;
    if (handle) {
        cdc_acm_host_close(handle);
        handle = nullptr;
    }
    post(LOST, epoch.load());
}
static bool openDevice(Device dev) {
    if (dev.vid != SILICON_LABS_VID) {
        EspLog("USB device is not the CP210x reader");
        return false;
    }
    cdc_acm_host_device_config_t cfg = {};
    cfg.connection_timeout_ms = 500;
    cfg.in_buffer_size = 64;
    cfg.out_buffer_size = 64;
    cfg.data_cb = rxCallback;
    cfg.event_cb = eventCallback;
    lost = false;
    esp_err_t err = cp210x_vcp_open(dev.pid, 0, &cfg, &handle);
    cdc_acm_line_coding_t coding = {};
    coding.dwDTERate = 115200;
    coding.bDataBits = 8;
    if (err == ESP_OK)
        err = cdc_acm_host_line_coding_set(handle, &coding);
    uint8_t flow[16] = {1, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (err == ESP_OK)
        err = cdc_acm_host_send_custom_request(handle, 0x41, 0x13, 0, 0, sizeof(flow), flow);
    if (err == ESP_OK)
        err = cdc_acm_host_set_control_line_state(handle, true, true);
    cdc_acm_line_coding_t readback = {};
    if (err == ESP_OK)
        err = cdc_acm_host_line_coding_get(handle, &readback);
    if (err == ESP_OK && (readback.dwDTERate != 115200 || readback.bDataBits != 8 ||
                          readback.bParityType || readback.bCharFormat))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK || lost.load()) {
        closeDevice();
        return false;
    }
    ++epoch;
    wireReady = true;
    post(CONNECTED, epoch.load());
    EspLog("CP210x reader connected at 115200 8N1");
    return true;
}
static void worker(void *) {
    Device last = {};
    uint32_t retryAt = 0;
    for (;;) {
        bool deviceLost = lost.exchange(false), requestedRecovery = recover.exchange(false);
        if (deviceLost || requestedRecovery) {
            closeDevice();
            retryAt = millis() + 1000;
        }
        Device n;
        while (xQueueReceive(devices, &n, 0) == pdTRUE) {
            if (n.vid == SILICON_LABS_VID) {
                last = n;
                if (!handle) {
                    openDevice(n);
                    retryAt = millis() + 2000;
                }
            }
        }
        if (!handle && last.vid && int32_t(millis() - retryAt) >= 0) {
            openDevice(last);
            retryAt = millis() + 2000;
        }
        Transfer request;
        if (xQueueReceive(txQueue, &request, pdMS_TO_TICKS(2)) == pdTRUE) {
            bool allowed = handle && wireReady.load() && request.epoch == epoch.load();
            if (request.length >= 3 && request.data[2] == 0x50 &&
                !EspRfStartAllowed(request.rfToken))
                allowed = false;
            esp_err_t err =
                allowed ? cdc_acm_host_data_tx_blocking(handle, request.data, request.length, 500)
                        : ESP_ERR_INVALID_STATE;
            post(err == ESP_OK ? TX_OK : TX_FAIL, request.epoch);
        }
    }
}
extern "C" void EspUsbBegin(void) {
    devices = xQueueCreate(8, sizeof(Device));
    rxQueue = xQueueCreate(128, sizeof(Transfer));
    txQueue = xQueueCreate(2, sizeof(Transfer));
    events = xQueueCreate(32, sizeof(Event));
    if (!devices || !rxQueue || !txQueue || !events) {
        EspLog("USB queue allocation failed");
        return;
    }
    usb_host_config_t config = {};
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    config.enum_filter_cb = filter;
#endif
    if (usb_host_install(&config) != ESP_OK) {
        EspLog("USB host install failed");
        return;
    }
    if (xTaskCreatePinnedToCore(hostTask, "usb-events", 4096, nullptr, 20, nullptr, 0) != pdPASS)
        return;
    cdc_acm_host_driver_config_t cdc = {};
    cdc.driver_task_stack_size = 4096;
    cdc.driver_task_priority = 10;
    cdc.xCoreID = 0;
    cdc.new_dev_cb = discovered;
    if (cdc_acm_host_install(&cdc) != ESP_OK) {
        EspLog("CDC host install failed");
        return;
    }
    if (xTaskCreatePinnedToCore(worker, "reader-usb", 6144, nullptr, 3, nullptr, 0) != pdPASS)
        return;
    installed = true;
}
extern "C" void USB_HOST_RequestRecovery(void) {
    ownerReady = false;
    ownerTxPending = false;
    receiveBuffer = nullptr;
    recover = true;
}
extern "C" uint8_t USBH_CP210X_IsReady(USBH_HandleTypeDef *) {
    return ownerReady && wireReady.load();
}
extern "C" USBH_StatusTypeDef USBH_CP210X_Transmit(USBH_HandleTypeDef *, uint8_t *data,
                                                   uint32_t size) {
    if (!ownerReady || !wireReady.load() || size > 64 || !size)
        return USBH_FAIL;
    if (ownerTxPending)
        return USBH_BUSY;
    Transfer m = {};
    m.epoch = ownerEpoch;
    m.rfToken = EspRfStartToken();
    m.length = size;
    memcpy(m.data, data, size);
    if (xQueueSend(txQueue, &m, 0) != pdTRUE)
        return USBH_BUSY;
    ownerTxPending = true;
    return USBH_OK;
}
extern "C" USBH_StatusTypeDef USBH_CP210X_Receive(USBH_HandleTypeDef *, uint8_t *data,
                                                  uint32_t size) {
    if (!ownerReady || !data || size < 64 || receiveBuffer)
        return USBH_BUSY;
    receiveBuffer = data;
    receiveCapacity = size;
    return USBH_OK;
}
extern "C" uint16_t USBH_CP210X_GetLastReceivedDataSize(USBH_HandleTypeDef *) {
    return lastReceived;
}
extern "C" void EspUsbPoll(void) {
    if (!installed)
        return;
    if (overrun.exchange(false)) {
        ZK7182_CancelScan();
        ZK7182_TransportLost();
        USB_HOST_RequestRecovery();
        EspLog("USB overflow: scan cancelled, reconnecting");
    }
    Event event;
    while (xQueueReceive(events, &event, 0) == pdTRUE) {
        if (event.kind == CONNECTED) {
            ownerEpoch = event.epoch;
            ownerReady = true;
            ownerTxPending = false;
            receiveBuffer = nullptr;
            ZK7182_TransportReady();
        } else if (event.kind == LOST) {
            ownerEpoch = event.epoch;
            ownerReady = false;
            ownerTxPending = false;
            receiveBuffer = nullptr;
            ZK7182_TransportLost();
        } else if (event.epoch == ownerEpoch && ownerReady) {
            ownerTxPending = false;
            if (event.kind == TX_OK)
                USBH_CP210X_TransmitCallback(&hUsbHostFS);
            else
                USBH_CP210X_ErrorCallback(&hUsbHostFS);
        }
    }
    // RX may beat the lower-priority TX worker's completion event. Retain it
    // in the queue until the owner has armed the legacy response fence.
    if (ownerTxPending)
        return;
    for (unsigned i = 0; i < 16 && ownerReady; i++) {
        if (!receiveBuffer)
            ZK7182_Process();
        if (!receiveBuffer)
            break;
        Transfer data;
        if (xQueueReceive(rxQueue, &data, 0) != pdTRUE)
            break;
        if (data.epoch != ownerEpoch)
            continue;
        if (data.length > receiveCapacity) {
            overrun = true;
            break;
        }
        memcpy(receiveBuffer, data.data, data.length);
        receiveBuffer = nullptr;
        lastReceived = data.length;
        USBH_CP210X_ReceiveCallback(&hUsbHostFS);
    }
}
