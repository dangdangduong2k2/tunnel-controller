import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
pkg = root
src = pkg / "firmware/TunnelESP32/src"
tests = pkg / "tests"
source_files = {p.name: p for p in src.rglob("*") if p.suffix in (".c", ".cpp", ".h")}


def source(name):
    return source_files[name]


include_paths = sorted({str(p.parent) for p in source_files.values()})
out = root / "test_build"
out.mkdir(exist_ok=True)
shared = [
    tests / "stubs_tunnel/tunnel_test_stubs.c",
    tests / "speed_store_fake.c",
    tests / "speed_store_legacy.c",
    source("tunnel.c"),
]
common = ["TUNNEL_HOST_TEST"]
real = common + ["TUNNEL_TEST_REAL_READER"]
dual = real + ["TUNNEL_TEST_REAL_HMI", "PC_CONTROL_TEST", "HMI_STREAM_TEST", "TUNNEL_DUAL_TEST"]
suites = [
    ("esp_dwin_update", [tests / "test_esp_dwin_update.cpp", source("EspDwinUpdate.cpp"), source("DwinPatch.cpp")], [], "dwin_stubs", True),
    ("dwin_patch", [tests / "test_dwin_patch.cpp", source("DwinPatch.cpp")], [], "vendor", True),
    ("esp_hmi_antenna", [tests / "test_esp_hmi_antenna.cpp", source("EspHmiAntenna.cpp"), source("AntennaSettings.cpp")], [], "esp_stubs", True),
    ("antenna_settings", [tests / "test_antenna_settings.cpp", source("AntennaSettings.cpp")], [], "vendor", True),
    ("fota_protocol", [tests / "test_fota_protocol.cpp", source("FotaProtocol.cpp"), source("FotaImage.cpp"), source("ModbusCore.cpp"), tests / "vendor/cJSON.c"], [], "vendor", True),
    (
        "reader_frequency",
        [tests / "test_reader_frequency.c", source("zk7182.c")],
        common + ["TUNNEL_READER_MEMORY_TEST"],
        "stubs_reader",
        False,
    ),
    (
        "mqtt_transport",
        [
            tests / "test_mqtt_transport.cpp",
            source("EspMqtt.cpp"),
            source("MqttApi.cpp"),
            source("MqttJson.cpp"),
            source("ModbusCore.cpp"),
            tests / "vendor/cJSON.c",
        ],
        ["TUNNEL_MQTT_HOST_TEST"],
        "vendor",
        True,
    ),
    (
        "hmi_network",
        [tests / "test_hmi_network.cpp", source("HmiNetwork.cpp")],
        [],
        "esp_stubs",
        True,
    ),
    (
        "hmi_ui",
        [
            tests / "test_hmi_ui.cpp",
            source("HmiUi.cpp"),
            source("History.cpp"),
            source("DeviceConfig.cpp"),
            tests / "vendor/cJSON.c",
        ],
        [],
        "vendor",
        True,
    ),
    ("history", [tests / "test_history.cpp", source("History.cpp")], [], "vendor", True),
    (
        "config_service",
        [tests / "test_config_service.cpp", source("DeviceConfig.cpp"), tests / "vendor/cJSON.c"],
        [],
        "config_stubs",
        True,
    ),
    (
        "device_config",
        [tests / "test_device_config.cpp", source("DeviceConfig.cpp"), tests / "vendor/cJSON.c"],
        [],
        "vendor",
        True,
    ),
    (
        "protocol_sim",
        [
            tests / "protocol_sim.cpp",
            source("MqttApi.cpp"),
            source("MqttJson.cpp"),
            source("ModbusCore.cpp"),
            tests / "vendor/cJSON.c",
        ],
        [],
        "vendor",
        True,
    ),
    (
        "mqtt_api",
        [
            tests / "test_mqtt_api.cpp",
            source("MqttApi.cpp"),
            source("MqttJson.cpp"),
            source("ModbusCore.cpp"),
            tests / "vendor/cJSON.c",
        ],
        [],
        "vendor",
        True,
    ),
    (
        "modbus_core",
        [tests / "test_modbus_core.cpp", source("ModbusCore.cpp")],
        [],
        "esp_stubs",
        True,
    ),
    (
        "profile",
        [tests / "test_motor_profile.c", source("motor_profile.c")],
        common,
        "stubs_tunnel",
        False,
    ),
    ("cycle", [tests / "test_tunnel_hmi.c"] + shared, common, "stubs_tunnel", False),
    ("finalize", [tests / "test_finalize_hook.c"] + shared, common, "stubs_tunnel", False),
    (
        "reader_cycle",
        [tests / "test_reader_tunnel.c"] + shared + [source("zk7182.c")],
        real,
        "stubs_tunnel",
        False,
    ),
    (
        "integration",
        [tests / "test_pc_control.c"] + shared + [source("zk7182.c"), source("pc_control.c")],
        real + ["PC_CONTROL_TEST"],
        "stubs_tunnel",
        False,
    ),
    (
        "hmi",
        [tests / "test_hmi.c", tests / "stubs_hmi/hmi_hal_stub.c", source("hmi.c")],
        [],
        "stubs_hmi",
        False,
    ),
    (
        "esp_hmi",
        [tests / "test_esp_hmi.c", source("hmi.c")],
        ["HMI_STREAM_TEST"],
        "stubs_hmi",
        False,
    ),
    (
        "hmi_ui_events",
        [tests / "test_hmi_ui_events.c", source("hmi.c")],
        ["HMI_STREAM_TEST"],
        "stubs_hmi",
        False,
    ),
    (
        "dual",
        [tests / "test_dual_control.c"]
        + shared
        + [source(n) for n in ["zk7182.c", "pc_control.c", "hmi.c", "dual_control.c"]],
        dual,
        "stubs_tunnel",
        False,
    ),
    (
        "shared_uart",
        [tests / "test_shared_uart.c"]
        + shared
        + [
            source(n)
            for n in ["zk7182.c", "pc_control.c", "hmi.c", "dual_control.c", "SharedUart.c"]
        ],
        dual + ["SHARED_UART_TEST"],
        "stubs_tunnel",
        False,
    ),
    (
        "dwin_manual",
        [tests / "test_dwin_manual.c"]
        + shared
        + [
            source(n)
            for n in ["zk7182.c", "pc_control.c", "hmi.c", "dual_control.c", "SharedUart.c"]
        ],
        dual + ["SHARED_UART_TEST"],
        "stubs_tunnel",
        False,
    ),
    ("two_doors", [tests / "test_two_doors.c"] + shared, common, "stubs_tunnel", False),
    (
        "in7_confirmation",
        [tests / "test_in7_confirmation.c"] + shared,
        common,
        "stubs_tunnel",
        False,
    ),
    ("esp_step", [tests / "test_esp_step.cpp", source("motor_profile.c")], [], "esp_stubs", True),
    (
        "esp_outputs",
        [tests / "test_esp_outputs.cpp", source("EspMotors.cpp"), source("motor_profile.c")],
        [],
        "esp_stubs",
        True,
    ),
    (
        "modbus_integration",
        [tests / "test_modbus_integration.c", tests / "modbus_test_adapter.cpp"]
        + shared
        + [
            source(n)
            for n in [
                "zk7182.c",
                "pc_control.c",
                "hmi.c",
                "dual_control.c",
                "SharedUart.c",
                "ModbusCore.cpp",
            ]
        ],
        dual + ["SHARED_UART_TEST", "TUNNEL_MODBUS_TEST", "TUNNEL_CONFIG_TEST"],
        "stubs_tunnel",
        True,
    ),
]


def run(args, cwd):
    p = subprocess.run(
        [str(a) for a in args],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    if p.returncode:
        print(p.stdout)
        raise SystemExit(p.returncode)
    return p.stdout


unknown = set(sys.argv[1:]) - {s[0] for s in suites}
if unknown:
    raise SystemExit("Unknown suites: " + ", ".join(sorted(unknown)))
log = []
for name, files, defines, stub, cpp in suites:
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    directory = out / name
    directory.mkdir(exist_ok=True)
    flags = (
        [
            "cl",
            "/nologo",
            "/W3",
            "/utf-8",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/I" + str(tests / stub),
            "/I" + str(tests / "vendor"),
            "/I" + str(src),
            "/I" + str(tests),
        ]
        + ["/I" + p for p in include_paths]
        + ["/D" + d for d in defines]
    )
    # Compile C separately even in C++ suites, to preserve the firmware ABI.
    objects = []
    for i, file in enumerate(files):
        obj = directory / (str(i) + ".obj")
        objects.append(obj)
        lang = ["/std:c++17", "/EHsc"] if file.suffix == ".cpp" else ["/std:c11", "/TC"]
        run(flags + lang + ["/c", file, "/Fo" + str(obj)], directory)
    exe = directory / (name + ".exe")
    run(["link", "/nologo", "/OUT:" + str(exe)] + objects, directory)
    result = run([exe], directory)
    print(name + ": " + result.strip(), flush=True)
    log.append(name + ": " + result)
(out / "results.txt").write_text("\n".join(log), encoding="utf-8")
print("PASS selected regression suites")
