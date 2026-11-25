#include "hmi.h"
#include "../common/debug_state.h"
#if defined(ARDUINO) || defined(HMI_STREAM_TEST)
#define HMI_STREAM_PORT 1
#include "../platform/compat/stm32f4xx_hal.h"
#include "../platform/esp32/EspPlatform.h"
#else
#include "main.h"
#endif

#include <stddef.h>

#define HMI_FRAME_HEADER_HIGH 0x5AU
#define HMI_FRAME_HEADER_LOW 0xA5U
#define HMI_COMMAND_WRITE 0x82U
#define HMI_COMMAND_READ 0x83U

#define HMI_RX_RING_CAPACITY 128U
#define HMI_RX_BODY_CAPACITY 32U
#define HMI_EVENT_QUEUE_CAPACITY 16U
#define HMI_UI_QUEUE_CAPACITY 32U
#define HMI_RX_FRAME_TIMEOUT_MS 50UL
#define HMI_CONFIG_RETRY_MS 250UL
#define HMI_CONFIG_VERIFY_MS 50UL
#define HMI_CONFIG_SLOW_RETRY_MS 1000UL
#define HMI_CONFIG_FAST_READS 16U
#define HMI_TX_TIMEOUT_MS 10UL

#define HMI_SYSTEM_VP_CONFIG 0x0080U
#define HMI_CONFIG_AUTO_UPLOAD_BIT 0x10U

#define HMI_CRC_UNKNOWN 0U
#define HMI_CRC_DISABLED 1U
#define HMI_CRC_ENABLED 2U

#define HMI_PRIORITY_NONE 0U
#define HMI_PRIORITY_RESET 1U
#define HMI_PRIORITY_EMERGENCY 2U

typedef enum {
    HMI_PARSE_HEADER_HIGH = 0U,
    HMI_PARSE_HEADER_LOW,
    HMI_PARSE_LENGTH,
    HMI_PARSE_BODY
} HmiParseState;

volatile HmiDiagnostics g_hmi_debug;

#ifndef HMI_STREAM_PORT
static uint8_t s_rx_byte;
#endif
static volatile uint8_t s_rx_ring[HMI_RX_RING_CAPACITY];
static volatile uint32_t s_rx_ring_generation[HMI_RX_RING_CAPACITY];
static volatile uint16_t s_rx_write_index;
static volatile uint16_t s_rx_read_index;
static volatile uint8_t s_rx_rearm_pending;
static volatile uint8_t s_rx_discard_pending;
static volatile uint32_t s_rx_generation;

static HmiParseState s_parse_state;
static uint8_t s_control_locked;
static uint8_t s_body[HMI_RX_BODY_CAPACITY];
static uint8_t s_body_length;
static uint8_t s_body_index;
static uint32_t s_frame_generation;
static uint32_t s_parser_last_byte_tick;

static HmiEvent s_event_queue[HMI_EVENT_QUEUE_CAPACITY];
static uint8_t s_event_head;
static uint8_t s_event_tail;
static uint8_t s_event_count;
static uint8_t s_priority_pending;

static uint16_t s_ui_queue[HMI_UI_QUEUE_CAPACITY];
static uint8_t s_ui_head;
static uint8_t s_ui_tail;
static uint8_t s_ui_count;
static uint16_t s_last_page;
static uint32_t s_page_write_count;

static uint8_t s_crc_mode;
static uint8_t s_config_read_attempts;
static uint8_t s_config_complete;
static uint8_t s_config_write_needed;
static uint8_t s_config_write_value;
static uint8_t s_config_probe_phase;
static uint32_t s_config_next_tick;

static uint8_t Hmi_TimeReached(uint32_t now, uint32_t deadline) {
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static uint16_t Hmi_Crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc;
    uint16_t index;
    uint8_t bit;

    crc = 0xFFFFU;
    for (index = 0U; index < length; index++) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static void Hmi_ClearDiagnostics(void) {
    volatile uint8_t *bytes;
    size_t index;

    bytes = (volatile uint8_t *)&g_hmi_debug;
    for (index = 0U; index < sizeof(g_hmi_debug); index++) {
        bytes[index] = 0U;
    }
}

static void Hmi_ResetParser(void) {
    s_parse_state = HMI_PARSE_HEADER_HIGH;
    s_body_length = 0U;
    s_body_index = 0U;
}

static uint8_t Hmi_Transmit(uint8_t *frame, uint16_t length) {
#ifdef HMI_STREAM_PORT
    if (EspHmiTransmit(frame, length) == 0U)
#else
    if (HAL_UART_Transmit(&huart1, frame, length, HMI_TX_TIMEOUT_MS) != HAL_OK)
#endif
    {
        g_hmi_debug.tx_error_count++;
        g_debug.hmi_tx_error_count++;
        return 0U;
    }

    g_hmi_debug.tx_frame_count++;
    return 1U;
}

static uint8_t Hmi_AppendCrc(uint8_t *frame, uint8_t body_length) {
    uint16_t crc;

    if (s_crc_mode != HMI_CRC_ENABLED) {
        frame[2] = body_length;
        return body_length;
    }

    crc = Hmi_Crc16(&frame[3], body_length);
    /* DGUS transmits the low byte of the Modbus CRC register first. */
    frame[3U + body_length] = (uint8_t)(crc & 0x00FFU);
    frame[4U + body_length] = (uint8_t)(crc >> 8U);
    frame[2] = (uint8_t)(body_length + 2U);
    return (uint8_t)(body_length + 2U);
}

static uint8_t Hmi_SendWords(uint16_t vp, const uint16_t *values, uint8_t word_count) {
    uint8_t frame[18];
    uint8_t body_length;
    uint8_t transmitted_body_length;
    uint8_t index;

    if ((values == NULL) || (word_count == 0U) || (word_count > 5U)) {
        return 0U;
    }

    frame[0] = HMI_FRAME_HEADER_HIGH;
    frame[1] = HMI_FRAME_HEADER_LOW;
    frame[3] = HMI_COMMAND_WRITE;
    frame[4] = (uint8_t)(vp >> 8U);
    frame[5] = (uint8_t)(vp & 0x00FFU);
    for (index = 0U; index < word_count; index++) {
        frame[6U + (2U * index)] = (uint8_t)(values[index] >> 8U);
        frame[7U + (2U * index)] = (uint8_t)(values[index] & 0x00FFU);
    }

    body_length = (uint8_t)(3U + (2U * word_count));
    transmitted_body_length = Hmi_AppendCrc(frame, body_length);
    return Hmi_Transmit(frame, (uint16_t)(3U + transmitted_body_length));
}

uint8_t Hmi_SendText(uint16_t vp, const char *text, uint16_t field_bytes) {
    uint8_t frame[136] = {0};
    uint16_t i = 0U;
    uint8_t body_length;
    if ((text == NULL) || (field_bytes < 2U) || (field_bytes > 128U) ||
        ((field_bytes & 1U) != 0U) || ((uint32_t)vp + field_bytes / 2U > 65536UL)) {
        return 0U;
    }
    frame[0] = HMI_FRAME_HEADER_HIGH;
    frame[1] = HMI_FRAME_HEADER_LOW;
    frame[3] = HMI_COMMAND_WRITE;
    frame[4] = (uint8_t)(vp >> 8U);
    frame[5] = (uint8_t)vp;
    while ((i < field_bytes - 2U) && (text[i] != '\0')) {
        uint8_t c = (uint8_t)text[i];
        frame[6U + i] = ((c >= 32U) && (c <= 126U)) ? c : (uint8_t)'?';
        ++i;
    }
    frame[6U + i] = 0xFFU;
    frame[7U + i] = 0xFFU;
    body_length = Hmi_AppendCrc(frame, (uint8_t)(3U + field_bytes));
    /* The ESP transport enqueues a whole frame or returns busy; never waits. */
    return Hmi_Transmit(frame, (uint16_t)(3U + body_length));
}

static uint8_t Hmi_SendConfigRead(uint8_t with_crc) {
    uint8_t frame[9];
    uint8_t body_length;
    uint16_t crc;

    frame[0] = HMI_FRAME_HEADER_HIGH;
    frame[1] = HMI_FRAME_HEADER_LOW;
    frame[3] = HMI_COMMAND_READ;
    frame[4] = (uint8_t)(HMI_SYSTEM_VP_CONFIG >> 8U);
    frame[5] = (uint8_t)(HMI_SYSTEM_VP_CONFIG & 0x00FFU);
    frame[6] = 2U;
    body_length = 4U;

    if (with_crc != 0U) {
        crc = Hmi_Crc16(&frame[3], body_length);
        frame[7] = (uint8_t)(crc & 0x00FFU);
        frame[8] = (uint8_t)(crc >> 8U);
        frame[2] = 6U;
        body_length = 6U;
    } else {
        frame[2] = body_length;
    }

    if (Hmi_Transmit(frame, (uint16_t)(3U + body_length)) == 0U) {
        return 0U;
    }

    g_hmi_debug.config_read_count++;
    return 1U;
}

static uint8_t Hmi_SendConfigWrite(uint8_t config) {
    uint16_t words[2];

    /* The documented SYS_CFG write key is 0x5A in D3.  Preserve the read D0
     * configuration byte and only enable its automatic-upload bit. */
    words[0] = 0x5A00U;
    words[1] = (uint16_t)config;
    if (Hmi_SendWords(HMI_SYSTEM_VP_CONFIG, words, 2U) == 0U) {
        return 0U;
    }

    g_hmi_debug.config_write_count++;
    return 1U;
}

static void Hmi_ArmReceive(void) {
#ifdef HMI_STREAM_PORT
    g_hmi_debug.rx_armed = 1U;
    s_rx_rearm_pending = 0U;
#else
    HAL_StatusTypeDef status;

    status = HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
    if ((status == HAL_OK) || (status == HAL_BUSY)) {
        g_hmi_debug.rx_armed = 1U;
        s_rx_rearm_pending = 0U;
    } else {
        g_hmi_debug.rx_armed = 0U;
        s_rx_rearm_pending = 1U;
        g_hmi_debug.rx_arm_error_count++;
    }
#endif
}

static uint8_t Hmi_IsWhitelistedVp(uint16_t vp) {
    switch (vp) {
    case HMI_VP_BUTTON_CONVEYOR:
    case HMI_VP_BUTTON_GATE:
    case HMI_VP_BUTTON_SCAN:
    case HMI_VP_BUTTON_AUTO:
    case HMI_VP_BUTTON_EMERGENCY:
    case HMI_VP_BUTTON_RESET:
    case HMI_VP_BUTTON_SEMI:
    case HMI_VP_BUTTON_CONFIRM:
    case HMI_VP_BUTTON_RESCAN:
        return 1U;

    default:
        return 0U;
    }
}

static void Hmi_ClearUiEvents(void) {
    s_ui_head = 0U;
    s_ui_tail = 0U;
    s_ui_count = 0U;
    g_hmi_debug.ui_queue_depth = 0U;
}

/* Settings/history touches. They cannot move the machine, so the ownership
 * lock and the state-transition fence do not apply; identical codes in a row
 * are distinct key presses. last_vp/last_value are left alone because the
 * value can be a password character. */
static void Hmi_QueueUiEvent(uint16_t value) {
    if (s_priority_pending == HMI_PRIORITY_EMERGENCY) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }
    if (s_ui_count >= HMI_UI_QUEUE_CAPACITY) {
        /* Keep the accepted prefix intact; the newest touch is lost. */
        g_hmi_debug.ui_queue_overflow_count++;
        return;
    }
    s_ui_queue[s_ui_tail] = value;
    s_ui_tail = (uint8_t)((s_ui_tail + 1U) % HMI_UI_QUEUE_CAPACITY);
    s_ui_count++;
    g_hmi_debug.ui_queue_depth = s_ui_count;
    g_hmi_debug.ui_event_count++;
}

static void Hmi_QueueEvent(uint16_t vp, uint16_t value, uint32_t frame_generation) {
    g_hmi_debug.last_vp = vp;
    g_hmi_debug.last_value = value;
    if ((vp == HMI_VP_BUTTON_EMERGENCY) && (value == 1U)) {
#ifdef HMI_STREAM_PORT
        // Only a complete, validated DWIN frame can reach this point.
        // Cut pulses/release EN before any further normal command processing.
        EspHmiEmergency();
#endif
        s_event_head = 0U;
        s_event_tail = 0U;
        s_event_count = 0U;
        s_priority_pending = HMI_PRIORITY_EMERGENCY;
        Hmi_ClearUiEvents();
        g_hmi_debug.event_queue_depth = 0U;
        g_hmi_debug.emergency_pending = 1U;
        g_hmi_debug.reset_pending = 0U;
        g_hmi_debug.rx_event_count++;
        return;
    }

    /* Ownership locks motion and recovery. A complete, validated Emergency
     * frame must still stop the machine while a remote controller owns it. */
    if (s_control_locked) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }

    if ((vp == HMI_VP_BUTTON_RESET) && (value == 1U)) {
        if (s_priority_pending != HMI_PRIORITY_EMERGENCY) {
            s_event_head = 0U;
            s_event_tail = 0U;
            s_event_count = 0U;
            s_priority_pending = HMI_PRIORITY_RESET;
            g_hmi_debug.event_queue_depth = 0U;
            g_hmi_debug.reset_pending = 1U;
            g_hmi_debug.rx_event_count++;
        } else {
            g_hmi_debug.rx_ignored_frame_count++;
        }
        return;
    }

    /* Flush establishes a receive fence at state transitions.  A normal
     * button frame whose header was already received before that fence is
     * stale, even if its remaining bytes arrived afterward.  RESET and
     * EMERGENCY are deliberately handled above so a safety/recovery request
     * can never be lost merely because it straddled a transition. */
    if (frame_generation != s_rx_generation) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }

    /* Do not let an ordinary event received later in the same UART burst get
     * ahead of a pending emergency event. */
    if (s_priority_pending != HMI_PRIORITY_NONE) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }

    if (s_event_count >= HMI_EVENT_QUEUE_CAPACITY) {
        s_event_head = (uint8_t)((s_event_head + 1U) % HMI_EVENT_QUEUE_CAPACITY);
        s_event_count--;
        g_hmi_debug.event_queue_overflow_count++;
    }

    s_event_queue[s_event_tail].vp = vp;
    s_event_queue[s_event_tail].value = value;
    s_event_tail = (uint8_t)((s_event_tail + 1U) % HMI_EVENT_QUEUE_CAPACITY);
    s_event_count++;
    g_hmi_debug.event_queue_depth = s_event_count;
    g_hmi_debug.rx_event_count++;
}

static uint8_t Hmi_FrameHasValidCrc(uint8_t payload_length) {
    uint16_t expected;

    if (payload_length < 3U) {
        return 0U;
    }

    expected = Hmi_Crc16(s_body, (uint16_t)(payload_length - 2U));
    return (((uint8_t)(expected & 0x00FFU) == s_body[payload_length - 2U]) &&
            ((uint8_t)(expected >> 8U) == s_body[payload_length - 1U]))
               ? 1U
               : 0U;
}

static void Hmi_RecordCrcMode(uint8_t mode) {
    s_crc_mode = mode;
    g_hmi_debug.crc_mode = mode;
}

static void Hmi_HandleConfigResponse(uint8_t config) {
    uint8_t updated_config;

    g_hmi_debug.config_response_count++;
    g_hmi_debug.config_known = 1U;
    if ((config & HMI_CONFIG_AUTO_UPLOAD_BIT) != 0U) {
        g_hmi_debug.auto_upload_enabled = 1U;
        g_hmi_debug.config_write_pending = 0U;
        s_config_write_needed = 0U;
        s_config_complete = 1U;
        return;
    }

    g_hmi_debug.auto_upload_enabled = 0U;
    updated_config = (uint8_t)(config | HMI_CONFIG_AUTO_UPLOAD_BIT);
    s_config_write_value = updated_config;
    s_config_write_needed = 1U;
    g_hmi_debug.config_write_pending = 1U;
    s_config_next_tick = HAL_GetTick();
}

static void Hmi_HandleFrame(void) {
    uint8_t data_length;
    uint8_t frame_crc_mode;
    uint16_t vp;
    uint16_t value;

    g_hmi_debug.rx_frame_count++;
    data_length = s_body_length;
    frame_crc_mode = HMI_CRC_DISABLED;

    if (((data_length == 5U) && (s_body[0] == HMI_COMMAND_WRITE)) ||
        ((data_length == 8U) && (s_body[0] == HMI_COMMAND_READ) && (s_body[3] == 1U)) ||
        ((data_length == 10U) && (s_body[0] == HMI_COMMAND_READ) && (s_body[3] == 2U))) {
        if (Hmi_FrameHasValidCrc(data_length) != 0U) {
            data_length = (uint8_t)(data_length - 2U);
            frame_crc_mode = HMI_CRC_ENABLED;
        }
    }

    if ((s_body[0] == HMI_COMMAND_WRITE) && (data_length == 3U) && (s_body[1] == 0x4FU) &&
        (s_body[2] == 0x4BU)) {
        Hmi_RecordCrcMode(frame_crc_mode);
        g_hmi_debug.rx_ack_count++;
        return;
    }

    if (s_body[0] != HMI_COMMAND_READ) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }

    if ((data_length == 8U) && (s_body[1] == (uint8_t)(HMI_SYSTEM_VP_CONFIG >> 8U)) &&
        (s_body[2] == (uint8_t)(HMI_SYSTEM_VP_CONFIG & 0x00FFU)) && (s_body[3] == 2U)) {
        Hmi_RecordCrcMode(frame_crc_mode);
        Hmi_HandleConfigResponse(s_body[7]);
        return;
    }

    if (data_length != 6U) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }
    if (s_body[3] != 1U) {
        g_hmi_debug.rx_malformed_frame_count++;
        return;
    }

    vp = (uint16_t)(((uint16_t)s_body[1] << 8U) | s_body[2]);
    value = (uint16_t)(((uint16_t)s_body[4] << 8U) | s_body[5]);
    if (vp == HMI_VP_UI_EVENT) {
        Hmi_RecordCrcMode(frame_crc_mode);
        Hmi_QueueUiEvent(value);
        return;
    }
    if (Hmi_IsWhitelistedVp(vp) == 0U) {
        g_hmi_debug.rx_ignored_frame_count++;
        return;
    }

    Hmi_RecordCrcMode(frame_crc_mode);
    Hmi_QueueEvent(vp, value, s_frame_generation);
}

static void Hmi_ParseByte(uint8_t byte, uint32_t byte_generation, uint32_t now) {
    s_parser_last_byte_tick = now;

    switch (s_parse_state) {
    case HMI_PARSE_HEADER_HIGH:
        if (byte == HMI_FRAME_HEADER_HIGH) {
            s_frame_generation = byte_generation;
            s_parse_state = HMI_PARSE_HEADER_LOW;
        }
        break;

    case HMI_PARSE_HEADER_LOW:
        if (byte == HMI_FRAME_HEADER_LOW) {
            s_parse_state = HMI_PARSE_LENGTH;
        } else if (byte == HMI_FRAME_HEADER_HIGH) {
            /* The newest 0x5A is also a valid start when headers overlap. */
            s_frame_generation = byte_generation;
        } else {
            s_parse_state = HMI_PARSE_HEADER_HIGH;
        }
        break;

    case HMI_PARSE_LENGTH:
        if ((byte == 0U) || (byte > HMI_RX_BODY_CAPACITY)) {
            g_hmi_debug.rx_malformed_frame_count++;
            Hmi_ResetParser();
        } else {
            s_body_length = byte;
            s_body_index = 0U;
            s_parse_state = HMI_PARSE_BODY;
        }
        break;

    case HMI_PARSE_BODY:
        s_body[s_body_index] = byte;
        s_body_index++;
        if (s_body_index >= s_body_length) {
            Hmi_HandleFrame();
            Hmi_ResetParser();
        }
        break;

    default:
        Hmi_ResetParser();
        break;
    }
}

#ifdef HMI_STREAM_PORT
void Hmi_ReceiveByte(uint8_t byte) {
    uint32_t now = HAL_GetTick();
    if ((s_parse_state != HMI_PARSE_HEADER_HIGH) &&
        ((uint32_t)(now - s_parser_last_byte_tick) > HMI_RX_FRAME_TIMEOUT_MS)) {
        g_hmi_debug.rx_timeout_count++;
        Hmi_ResetParser();
    }
    g_hmi_debug.rx_byte_count++;
    Hmi_ParseByte(byte, s_rx_generation, now);
}

void Hmi_ReceiveError(void) {
    g_hmi_debug.uart_error_count++;
    Hmi_ResetParser();
    s_event_head = s_event_tail = s_event_count = 0U;
    s_priority_pending = 0U;
    Hmi_ClearUiEvents();
    g_hmi_debug.event_queue_depth = 0U;
    g_hmi_debug.emergency_pending = 0U;
    g_hmi_debug.reset_pending = 0U;
    EspRequestSafetyStop(STOP_UART);
}
#endif

static uint8_t Hmi_PopRxByte(uint8_t *byte, uint32_t *generation) {
    uint16_t read_index;

    read_index = s_rx_read_index;
    if (read_index == s_rx_write_index) {
        return 0U;
    }

    *byte = s_rx_ring[read_index];
    *generation = s_rx_ring_generation[read_index];
    s_rx_read_index = (uint16_t)((read_index + 1U) % HMI_RX_RING_CAPACITY);
    return 1U;
}

static void Hmi_DiscardRxRing(void) {
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_rx_read_index = s_rx_write_index;
    s_rx_discard_pending = 0U;
    if (primask == 0U) {
        __enable_irq();
    }
    Hmi_ResetParser();
}

static void Hmi_ProcessConfig(uint32_t now) {
    uint8_t use_crc;

    if ((s_config_complete != 0U) || (Hmi_TimeReached(now, s_config_next_tick) == 0U)) {
        return;
    }

    if (s_config_write_needed != 0U) {
        if (Hmi_SendConfigWrite(s_config_write_value) != 0U) {
            s_config_write_needed = 0U;
            /* Do not claim success on HAL_OK alone.  A later 0x0080 readback must
             * confirm that the display applied bit 4. */
            s_config_next_tick = now + HMI_CONFIG_VERIFY_MS;
        } else {
            s_config_next_tick = now + HMI_CONFIG_RETRY_MS;
        }
        return;
    }

    if (s_crc_mode == HMI_CRC_ENABLED) {
        use_crc = 1U;
    } else if (s_crc_mode == HMI_CRC_DISABLED) {
        use_crc = 0U;
    } else {
        /* Read-only probes alternate between both documented protocol modes until
         * the installed display configuration is known. */
        use_crc = s_config_probe_phase;
        s_config_probe_phase ^= 1U;
    }

    (void)Hmi_SendConfigRead(use_crc);
    if (s_config_read_attempts < HMI_CONFIG_FAST_READS) {
        s_config_read_attempts++;
    }
    s_config_next_tick =
        now + ((s_config_read_attempts < HMI_CONFIG_FAST_READS) ? HMI_CONFIG_RETRY_MS
                                                                : HMI_CONFIG_SLOW_RETRY_MS);
}

void Hmi_Init(void) {
    s_control_locked = 0U;
    s_rx_write_index = 0U;
    s_rx_read_index = 0U;
    s_rx_rearm_pending = 0U;
    s_rx_discard_pending = 0U;
    s_rx_generation = 0U;
    Hmi_ResetParser();
    s_frame_generation = 0U;
    s_parser_last_byte_tick = HAL_GetTick();

    s_event_head = 0U;
    s_event_tail = 0U;
    s_event_count = 0U;
    s_priority_pending = HMI_PRIORITY_NONE;
    s_ui_head = 0U;
    s_ui_tail = 0U;
    s_ui_count = 0U;
    s_last_page = 0U;
    s_page_write_count = 0U;

    s_crc_mode = HMI_CRC_UNKNOWN;
    s_config_read_attempts = 0U;
    s_config_complete = 0U;
    s_config_write_needed = 0U;
    s_config_write_value = 0U;
    s_config_probe_phase = 0U;
    s_config_next_tick = HAL_GetTick();

    Hmi_ClearDiagnostics();
    Hmi_ArmReceive();
}

void Hmi_Process(void) {
    uint8_t byte;
    uint32_t byte_generation;
    uint32_t now;

    now = HAL_GetTick();
    if (s_rx_rearm_pending != 0U) {
        Hmi_ArmReceive();
    }

    if (s_rx_discard_pending != 0U) {
        Hmi_DiscardRxRing();
    } else if ((s_parse_state != HMI_PARSE_HEADER_HIGH) &&
               ((uint32_t)(now - s_parser_last_byte_tick) > HMI_RX_FRAME_TIMEOUT_MS)) {
        g_hmi_debug.rx_timeout_count++;
        Hmi_ResetParser();
    }

    while (Hmi_PopRxByte(&byte, &byte_generation) != 0U) {
        now = HAL_GetTick();
        Hmi_ParseByte(byte, byte_generation, now);
    }

    if (s_rx_discard_pending != 0U) {
        Hmi_DiscardRxRing();
    }
    if (s_priority_pending != HMI_PRIORITY_NONE) {
        return;
    }
    Hmi_ProcessConfig(HAL_GetTick());
}

uint8_t Hmi_PopEvent(HmiEvent *event) {
    if (event == NULL) {
        return 0U;
    }

    if (s_priority_pending == HMI_PRIORITY_EMERGENCY) {
        event->vp = HMI_VP_BUTTON_EMERGENCY;
        event->value = 1U;
        s_priority_pending = HMI_PRIORITY_NONE;
        g_hmi_debug.emergency_pending = 0U;
        return 1U;
    }
    if (s_priority_pending == HMI_PRIORITY_RESET) {
        event->vp = HMI_VP_BUTTON_RESET;
        event->value = 1U;
        s_priority_pending = HMI_PRIORITY_NONE;
        g_hmi_debug.reset_pending = 0U;
        return 1U;
    }

    if (s_event_count == 0U) {
        return 0U;
    }

    *event = s_event_queue[s_event_head];
    s_event_head = (uint8_t)((s_event_head + 1U) % HMI_EVENT_QUEUE_CAPACITY);
    s_event_count--;
    g_hmi_debug.event_queue_depth = s_event_count;
    return 1U;
}

uint8_t Hmi_PopUiEvent(uint16_t *value) {
    if ((value == NULL) || (s_ui_count == 0U)) {
        return 0U;
    }
    *value = s_ui_queue[s_ui_head];
    s_ui_head = (uint8_t)((s_ui_head + 1U) % HMI_UI_QUEUE_CAPACITY);
    s_ui_count--;
    g_hmi_debug.ui_queue_depth = s_ui_count;
    return 1U;
}

void Hmi_FlushEvents(void) {
    uint32_t primask;
#ifdef HMI_STREAM_PORT
    // Capture bytes already received by the UART before advancing the fence.
    // Preserve Emergency/Reset, but never turn an old touch into a new action.
    EspHmiCapture();
#endif

    /* Advance the RX fence atomically with respect to the one-byte receive
     * callback. Bytes already in the ring retain their old generation and can
     * therefore finish a RESET/EMERGENCY frame, but cannot become a fresh
     * ordinary action in the next tunnel state. */
    primask = __get_PRIMASK();
    __disable_irq();
    s_rx_generation++;
    if (primask == 0U) {
        __enable_irq();
    }

    s_event_head = 0U;
    s_event_tail = 0U;
    s_event_count = 0U;
    g_hmi_debug.event_queue_depth = 0U;
}

uint8_t Hmi_SendWord(uint16_t vp, uint16_t value) {
    return Hmi_SendWords(vp, &value, 1U);
}

uint8_t Hmi_IsControlLocked(void) {
    return s_control_locked;
}
void Hmi_SetControlLocked(uint8_t locked) {
    s_control_locked = locked != 0U;
    Hmi_DiscardActionsAfterStop();
}

void Hmi_DiscardActionsAfterStop(void) {
    // Both UARTs have been fenced after STOP. An already queued Reset from
    // the other controller must not immediately undo that STOP.
    s_rx_read_index = s_rx_write_index;
    s_rx_discard_pending = 0U;
    ++s_rx_generation;
    Hmi_ResetParser();
    s_event_head = s_event_tail = s_event_count = 0U;
    s_priority_pending = HMI_PRIORITY_NONE;
    Hmi_ClearUiEvents();
    g_hmi_debug.event_queue_depth = 0U;
    g_hmi_debug.emergency_pending = g_hmi_debug.reset_pending = 0U;
}

uint8_t Hmi_SendPage(uint16_t page) {
    uint16_t values[2];

    values[0] = 0x5A01U;
    values[1] = page;
    if (Hmi_SendWords(0x0084U, values, 2U) == 0U) {
        return 0U;
    }
    s_last_page = page;
    s_page_write_count++;
    return 1U;
}

uint16_t Hmi_GetLastPage(uint32_t *write_count) {
    if (write_count != NULL) {
        *write_count = s_page_write_count;
    }
    return s_last_page;
}

uint8_t Hmi_SendQuantity(uint16_t quantity) {
    return Hmi_SendWord(HMI_VP_QUANTITY, quantity);
}

uint8_t Hmi_SendIndicators(uint16_t conveyor, uint16_t gate, uint16_t scan, uint16_t auto_mode,
                           uint16_t semi_mode) {
    uint16_t values[5];

    values[0] = conveyor;
    values[1] = gate;
    values[2] = scan;
    values[3] = auto_mode;
    values[4] = semi_mode;
    return Hmi_SendWords(HMI_VP_INDICATOR_CONVEYOR, values, 5U);
}

#ifndef HMI_STREAM_PORT
void Hmi_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    uint16_t write_index;
    uint16_t next_index;

    if (huart != &huart1) {
        return;
    }

    g_hmi_debug.rx_armed = 0U;
    write_index = s_rx_write_index;
    next_index = (uint16_t)((write_index + 1U) % HMI_RX_RING_CAPACITY);
    if (next_index == s_rx_read_index) {
        g_hmi_debug.rx_ring_overflow_count++;
        s_rx_discard_pending = 1U;
    } else {
        s_rx_ring[write_index] = s_rx_byte;
        s_rx_ring_generation[write_index] = s_rx_generation;
        s_rx_write_index = next_index;
        g_hmi_debug.rx_byte_count++;
    }

    Hmi_ArmReceive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart != &huart1) {
        return;
    }

    g_hmi_debug.uart_error_count++;
    g_hmi_debug.last_uart_error = HAL_UART_GetError(huart);
    g_hmi_debug.rx_armed = 0U;
    s_rx_discard_pending = 1U;
    Hmi_ArmReceive();
}
#endif
