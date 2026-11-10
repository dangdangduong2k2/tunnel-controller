#ifndef DEBUG_STATE_H
#define DEBUG_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DEBUG_LAST_FRAME_CAPACITY 258U
#define DEBUG_LAST_EPC_CAPACITY 64U
#define DEBUG_EPC_TEXT_CAPACITY ((DEBUG_LAST_EPC_CAPACITY * 2U) + 1U)
#define DEBUG_LAST_RAW_RX_CAPACITY 64U
#define DEBUG_LAST_TX_CAPACITY 16U

typedef enum {
    DEBUG_USB_DISCONNECTED = 0U,
    DEBUG_USB_CONNECTED,
    DEBUG_USB_CLASS_SELECTED,
    DEBUG_USB_CP210X_READY,
    DEBUG_USB_ERROR = 0xFFU
} DebugUsbEventTypeDef;

typedef enum {
    DEBUG_ERROR_NONE = 0U,
    DEBUG_ERROR_USB_UNRECOVERED,
    DEBUG_ERROR_NOT_SILABS_CP210X,
    DEBUG_ERROR_CP210X_BULK_ENDPOINT,
    DEBUG_ERROR_CP210X_PIPE_ALLOC,
    DEBUG_ERROR_CP210X_CONTROL_REQUEST,
    DEBUG_ERROR_CP210X_TRANSFER,
    DEBUG_ERROR_ZK_BAD_PARAMETER,
    DEBUG_ERROR_ZK_CHECKSUM,
    DEBUG_ERROR_ZK_FRAME_TOO_LONG
} DebugErrorTypeDef;

typedef struct {
    uint32_t uptime_ms;
    uint32_t main_loop_count;
    uint32_t usb_connect_count;
    uint32_t usb_disconnect_count;
    uint32_t tx_frame_count;
    uint32_t tx_bytes_total;
    uint32_t rx_bytes_total;
    uint32_t valid_frame_count;
    uint32_t checksum_error_count;
    uint32_t usb_sof_count;
    uint32_t usb_systick_count;
    uint32_t usb_connect_callback_count;
    uint32_t usb_disconnect_callback_count;
    uint32_t usb_disconnect_ignored_count;
    uint32_t usb_port_enabled_callback_count;
    uint32_t usb_port_disabled_callback_count;
    uint32_t usb_port_disabled_ignored_count;
    uint32_t usb_port_reset_count;
    uint32_t usb_port_reset_retry_count;
    uint32_t usb_port_reset_failure_count;
    uint32_t usb_reset_pulse_ms;
    uint32_t usb_reset_profile_index;
    uint32_t usb_reset_event_sequence;
    uint32_t usb_stack_init_count;
    uint32_t usb_recovery_count;
    uint32_t usb_last_recovery_tick;
    uint32_t usb_frame_change_count;
    uint32_t cp210x_transfer_error_count;

    uint32_t hclk_hz;
    uint32_t rcc_cr;
    uint32_t rcc_pllcfgr;
    uint32_t rcc_cfgr;
    uint32_t usb_host_timer;
    uint32_t usb_gintsts;
    uint32_t usb_gintmsk;
    uint32_t usb_hprt;
    uint32_t usb_hprt_before_reset;
    uint32_t usb_hprt_after_reset;
    uint32_t usb_last_port_event_hprt;
    uint32_t usb_hcfg;
    uint32_t usb_hfir;
    uint32_t usb_hfnum;
    uint32_t usb_control_hcchar;
    uint32_t usb_control_hcint;
    uint32_t usb_control_hcintmsk;
    uint32_t usb_control_hctsiz;

    uint32_t cp210x_baud;
    uint16_t cp210x_vid;
    uint16_t cp210x_pid;
    uint16_t last_frame_length;

    uint8_t usb_event;
    uint8_t usb_host_state;
    uint8_t cp210x_ready;
    uint8_t rx_armed;
    uint8_t tx_busy;
    uint8_t last_error;
    uint8_t last_command;
    uint8_t last_status;

    uint8_t usb_enum_state;
    uint8_t usb_control_state;
    uint8_t usb_request_state;
    uint8_t usb_control_pipe_out;
    uint8_t usb_control_pipe_in;
    uint8_t usb_device_address;
    uint8_t usb_device_speed;
    uint8_t usb_device_connected;
    uint8_t usb_port_enabled;
    uint8_t usb_recovery_pending;
    uint8_t pa11_dm_level;
    uint8_t pa12_dp_level;

    uint8_t last_antenna;
    uint8_t last_rssi;
    uint8_t last_epc_length;
    uint8_t reserved;

    uint8_t last_frame[DEBUG_LAST_FRAME_CAPACITY];
    uint8_t last_epc[DEBUG_LAST_EPC_CAPACITY];

    /* Inventory values intended for IAR Live Watch. */
    uint32_t epc_count;
    uint32_t unique_epc_count;
    uint32_t duplicate_epc_count;
    uint32_t unique_epc_overflow_count;
    uint32_t scan_session_count;
    uint32_t scan_duration_ms;
    uint32_t hmi_tx_error_count;
    uint32_t inventory_tx_count;
    uint32_t inventory_rx_frame_count;
    uint32_t inventory_round_count;
    uint32_t inventory_error_count;
    uint32_t inventory_timeout_count;
    uint32_t inventory_hard_timeout_count;
    uint32_t inventory_quarantine_count;
    uint32_t inventory_late_frame_count;
    uint32_t inventory_stale_frame_count;
    uint32_t inventory_malformed_frame_count;
    uint32_t inventory_stop_drain_timeout_count;
    uint32_t inventory_rejected_frame_count;
    uint32_t reader_config_retry_count;
    uint32_t reader_config_fallback_count;
    uint32_t scan_failure_count;
    uint32_t inventory_last_total;
    uint16_t inventory_last_rate;
    uint8_t inventory_running;
    uint8_t inventory_last_error;
    uint8_t scan_active;
    uint8_t scan_complete;
    uint8_t scan_result;
    uint8_t scan_antenna_mask;
    uint8_t reader_config_state;
    uint8_t reader_expected_command;
    char last_epc_text[DEBUG_EPC_TEXT_CAPACITY];

    /* Values read back from the CP210x control interface. */
    uint32_t cp210x_reported_baud;
    uint32_t cp210x_flow_handshake;
    uint32_t cp210x_flow_replace;
    uint32_t cp210x_comm_poll_count;
    uint32_t cp210x_comm_errors;
    uint32_t cp210x_hold_reasons;
    uint32_t cp210x_rx_queue;
    uint32_t cp210x_tx_queue;
    uint16_t cp210x_reported_line_ctl;
    uint8_t cp210x_comm_valid;
    uint8_t cp210x_diag_error;

    /* Raw serial packets and protocol identification for Live Watch. */
    uint32_t raw_rx_packet_count;
    uint16_t last_raw_rx_length;
    uint16_t last_tx_frame_length;
    uint16_t reader_fw_version;
    uint8_t reader_type;
    uint8_t reader_address;
    uint8_t last_raw_rx[DEBUG_LAST_RAW_RX_CAPACITY];
    uint8_t last_tx_frame[DEBUG_LAST_TX_CAPACITY];
    uint32_t cp210x_rx_resubmit_count;
    uint8_t cp210x_rx_urb_state;

    /* Current tunnel-cycle values intended for IAR Live Watch. */
    uint32_t tunnel_step_pulse_count;
    uint32_t tunnel_step_close_pulse_count;
    uint32_t tunnel_step_open_pulse_count;
    uint32_t tunnel_step_period_us;
    uint16_t tunnel_display_quantity;
    uint8_t tunnel_state;
    uint8_t tunnel_fault;
    uint8_t tunnel_ss1_active;
    uint8_t tunnel_ss2_active;
    uint8_t tunnel_ss3_active;
    uint8_t tunnel_step_running;
    uint8_t tunnel_conveyor_running;
    uint8_t tunnel_scan_paused;
    uint8_t tunnel_mode;
    uint8_t tunnel_cycle_semi;
    uint8_t tunnel_gate_homed;
    uint8_t tunnel_home_reason;
    uint8_t tunnel_manual_scan_requested;
    uint32_t tunnel_rescan_count;
    uint32_t tunnel_hmi_ignored_count;

    /* RF sweep diagnostics. kHz is acknowledged, not just requested.
     * fallback: 0=none, 1=no info, 2=invalid range, 3=single channel,
     * 4=set rejected, 5=timeout, 6=restore not verified. */
    uint32_t rf_frequency_khz;
    uint32_t rf_set_tx_count;
    uint32_t rf_set_ack_count;
    uint32_t rf_wrap_count;
    uint32_t rf_error_count;
    uint32_t rf_restore_count;
    uint8_t rf_state;
    uint8_t rf_sweep_enabled;
    uint8_t rf_band;
    uint8_t rf_min_channel;
    uint8_t rf_max_channel;
    uint8_t rf_current_channel;
    uint8_t rf_last_status;
    uint8_t rf_fallback_reason;
    uint8_t rf_restore_failed;
    /* Per-parcel timing only; never persisted or reused for another parcel. */
    uint32_t tunnel_first_travel_ms;
    uint32_t tunnel_center_reverse_target_ms;
    uint32_t tunnel_center_reverse_actual_ms;
    uint32_t tunnel_center_hold_ms;
    /* Legacy CMD7F diagnostics; unused in START-only firmware.
     * error: 0=none, 1=timeout, 2=reader rejected, 3=profile mismatch.
     * current/status=0xFF until a valid reply / when the command times out. */
    uint32_t rf_profile_set_tx_count;
    uint32_t rf_profile_set_ack_count;
    uint32_t rf_profile_verify_count;
    uint32_t rf_profile_error_count;
    uint8_t rf_profile_requested;
    uint8_t rf_profile_current;
    uint8_t rf_profile_verified;
    uint8_t rf_profile_last_status;
    uint8_t rf_profile_error;
    /* START-only: all RF configuration fields above remain unverified. */
    uint8_t reader_start_only;
    uint32_t inventory_start_ack_count;
    uint32_t inventory_stop_tx_count;
    uint32_t inventory_stop_ack_count;
    uint32_t inventory_start_data_confirm_count;
    uint32_t inventory_silence_restart_count;
} DebugStateTypeDef;

extern volatile DebugStateTypeDef g_debug;

void DebugState_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_STATE_H */
