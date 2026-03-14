import importlib.util
import sys
from pathlib import Path

import pytest


@pytest.fixture()
def app_module(monkeypatch, tmp_path):
    api_dir = Path(__file__).resolve().parents[1]
    monkeypatch.chdir(api_dir)
    monkeypatch.setenv("SECRET_KEY", "test-secret")

    module_name = "oinkspy_flockyou_test"
    spec = importlib.util.spec_from_file_location(module_name, api_dir / "flockyou.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)

    module.app.config["TESTING"] = True
    module.DATA_DIR = tmp_path
    module.SETTINGS_FILE = tmp_path / "settings.json"
    module.CUMULATIVE_DATA_FILE = tmp_path / "cumulative.pkl"
    module.detections.clear()
    module.cumulative_detections.clear()
    module.gps_data = None
    module.gps_history.clear()
    module.serial_connection = None
    module.gps_enabled = False
    module.flock_device_connected = False
    module.flock_device_port = None
    module.flock_serial_connection = None
    module.settings = module.DEFAULT_SETTINGS.copy()
    module.oui_database.clear()
    module.socketio.emit = lambda *args, **kwargs: None

    yield module

    sys.modules.pop(module_name, None)


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


def test_update_settings_normalizes_invalid_filter(app_module, client):
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
