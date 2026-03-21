import importlib.util
import json
import pickle
import sys
import uuid
from pathlib import Path

import pytest


def load_app_module(monkeypatch, tmp_path, secret_key="test-secret"):
    api_dir = Path(__file__).resolve().parents[1]
    monkeypatch.chdir(api_dir)
    monkeypatch.delenv("OINKSPY_SETTINGS_FILE", raising=False)
    if secret_key is None:
        monkeypatch.delenv("SECRET_KEY", raising=False)
    else:
        monkeypatch.setenv("SECRET_KEY", secret_key)

    module_name = f"oinkspy_flockyou_test_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(module_name, api_dir / "flockyou.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)

    module.app.config["TESTING"] = True
    module.DATA_DIR = tmp_path
    module.EXPORTS_DIR = tmp_path / "exports"
    module.SETTINGS_SAMPLE_FILE = tmp_path / "settings.json"
    module.SETTINGS_FILE = tmp_path / "settings.local.json"
    module.CUMULATIVE_DATA_FILE = tmp_path / "cumulative.json"
    module.LEGACY_CUMULATIVE_DATA_FILE = tmp_path / "cumulative.pkl"
    module.DATA_DIR.mkdir(exist_ok=True)
    module.detections.clear()
    module.cumulative_detections.clear()
    module.gps_data = None
    module.gps_history.clear()
    module.serial_connection = None
    module.gps_enabled = False
    module.gps_source = None
    module.flock_device_connected = False
    module.flock_device_port = None
    module.flock_serial_connection = None
    module.settings = json.loads(json.dumps(module.DEFAULT_SETTINGS))
    module.oui_database.clear()
    module.socketio.emit = lambda *args, **kwargs: None

    return module, module_name


def unload_app_module(module_name):
    sys.modules.pop(module_name, None)


@pytest.fixture()
def app_module(monkeypatch, tmp_path):
    module, module_name = load_app_module(monkeypatch, tmp_path)
    try:
        yield module
    finally:
        unload_app_module(module_name)


@pytest.fixture()
def client(app_module):
    return app_module.app.test_client()


def test_help_endpoint_returns_quickstart(client):
    response = client.get("/api/help")

    assert response.status_code == 200
    payload = response.get_json()
    assert payload["status"] == "success"
    assert "quickstart" in payload
    assert payload["endpoints"]["status"] == "/api/status"
    assert payload["configuration"]["settings_file"].endswith("settings.local.json")
    assert payload["configuration"]["settings_sample_file"].endswith("settings.json")
    assert "OINKSPY_SERIAL_PORTS" in payload["troubleshooting"][1]


def test_status_returns_recommended_action_and_port_counts(app_module, client, monkeypatch):
    monkeypatch.setattr(
        app_module,
        "discover_serial_ports",
        lambda: [
            {"device": "/dev/ttyUSB0", "description": "USB Serial"},
            {"device": "/dev/ttyACM0", "description": "CDC ACM"},
        ],
    )

    response = client.get("/api/status")

    assert response.status_code == 200
    payload = response.get_json()
    assert payload["gps_ports_available"] == 2
    assert payload["flock_ports_available"] == 2
    assert payload["recommended_action"] == "Connect the OinkSpy sniffer to start streaming detections."


def test_add_detection_requires_mac_address(client):
    response = client.post(
        "/api/detections",
        json={"protocol": "wifi", "detection_method": "probe_request"},
    )

    assert response.status_code == 400
    payload = response.get_json()
    assert payload["status"] == "error"
    assert "mac_address" in payload["message"]


def test_connect_gps_requires_port(client):
    response = client.post("/api/gps/connect", json={})

    assert response.status_code == 400
    payload = response.get_json()
    assert payload["status"] == "error"
    assert payload["message"] == "GPS port is required."
    assert "OINKSPY_SERIAL_PORTS" in payload["hint"]


def test_update_settings_normalizes_invalid_filter_and_writes_local_override(app_module, client):
    response = client.post(
        "/api/settings",
        json={"filter": "not-a-real-filter", "gps_port": "/dev/ttyUSB0"},
    )

    assert response.status_code == 200
    payload = response.get_json()
    assert payload["status"] == "success"
    assert payload["settings"]["filter"] == "all"
    assert payload["settings"]["gps_port"] == "/dev/ttyUSB0"
    assert app_module.SETTINGS_FILE.exists()
    assert not app_module.SETTINGS_SAMPLE_FILE.exists()


def test_detection_patterns_endpoint_returns_default_groups(client):
    response = client.get("/api/patterns")

    assert response.status_code == 200
    payload = response.get_json()
    assert payload["macs"]["enabled"] is True
    assert "58:8e:81" in payload["macs"]["values"]
    assert payload["mfr"]["values"] == ["0x09C8"]


def test_update_detection_patterns_normalizes_values(client):
    response = client.post(
        "/api/patterns",
        json={
            "macs": {"enabled": True, "values": ["588E81", "58:8E:81"]},
            "macs_mfr": {"enabled": False, "values": ["F46ADD"]},
            "macs_soundthinking": {"enabled": True, "values": ["d411d6"]},
            "names": {"enabled": True, "values": ["  My Tracker  "]},
            "mfr": {"enabled": True, "values": ["09c8"]},
            "raven": {"enabled": True, "values": ["00003100-0000-1000-8000-00805F9B34FB"]},
        },
    )

    assert response.status_code == 200
    payload = response.get_json()
    assert payload["patterns"]["macs"]["values"] == ["58:8e:81"]
    assert payload["patterns"]["macs_mfr"]["enabled"] is False
    assert payload["patterns"]["mfr"]["values"] == ["0x09C8"]
    assert payload["patterns"]["raven"]["values"] == ["00003100-0000-1000-8000-00805f9b34fb"]


def test_app_uses_ephemeral_secret_key_when_unset(monkeypatch, tmp_path):
    module, module_name = load_app_module(monkeypatch, tmp_path, secret_key=None)
    try:
        assert module.app.config["SECRET_KEY_SOURCE"] == "ephemeral"
        assert isinstance(module.app.config["SECRET_KEY"], str)
        assert len(module.app.config["SECRET_KEY"]) >= 20
    finally:
        unload_app_module(module_name)


def test_load_cumulative_detections_migrates_legacy_pickle(monkeypatch, tmp_path):
    module, module_name = load_app_module(monkeypatch, tmp_path)
    try:
        legacy_payload = [{"mac_address": "AA:BB:CC:DD:EE:FF", "detection_method": "probe_request"}]
        with open(module.LEGACY_CUMULATIVE_DATA_FILE, "wb") as handle:
            pickle.dump(legacy_payload, handle)

        module.load_cumulative_detections()

        assert module.cumulative_detections == legacy_payload
        assert module.CUMULATIVE_DATA_FILE.exists()
        assert json.loads(module.CUMULATIVE_DATA_FILE.read_text(encoding="utf-8")) == legacy_payload
    finally:
        unload_app_module(module_name)


def test_load_cumulative_detections_handles_bad_legacy_pickle(monkeypatch, tmp_path):
    module, module_name = load_app_module(monkeypatch, tmp_path)
    try:
        module.LEGACY_CUMULATIVE_DATA_FILE.write_bytes(b"not-a-pickle")

        module.load_cumulative_detections()

        assert module.cumulative_detections == []
        assert not module.CUMULATIVE_DATA_FILE.exists()
    finally:
        unload_app_module(module_name)
