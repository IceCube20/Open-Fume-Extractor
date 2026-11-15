"""Constrain the core-0 experiment to synchronous publishes, not live state."""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[2] / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


class MqttWorkerSourceTests(unittest.TestCase):
    def test_serial_client_owner_and_original_core_assignments(self):
        mqtt = (SRC / "MasterMqtt.inc.h").read_text(encoding="utf-8")
        service = mqtt.split("static void mqtt_service_task(", 1)[1].split("static void ui_config_load", 1)[0]
        self.assertIn("MqttConfigGuard guard;", service)
        self.assertIn("if (guard.locked()) mqtt_loop();", service)
        setup = (SRC / "MasterSetup.inc.h").read_text(encoding="utf-8")
        self.assertLess(setup.index("mqtt_start_publish_worker();"), setup.index('"mqtt", 8192'))
        self.assertIn('&web_service_task_handle, 1)', setup)
        self.assertIn('&mqtt_service_task_handle, 1)', setup)
        worker = (SRC / "MqttPublishWorker.h").read_text(encoding="utf-8")
        self.assertNotIn("registry", worker)
        self.assertNotIn("scheduler", worker)
        self.assertNotIn("String", worker)
        self.assertIn('"mqtt-send", 8192, this, 1, &task_, 0', worker)
        self.assertIn("vTaskDelay(1);", worker)

    def test_optional_worker_preserves_measurement_and_does_not_retry(self):
        mqtt = (SRC / "MasterMqtt.inc.h").read_text(encoding="utf-8")
        direct = mqtt.split("static bool mqtt_publish_direct(", 1)[1].split("static MqttPublishWorker", 1)[0]
        self.assertIn("MqttPerfPhase::Publish", direct)
        self.assertIn("mqtt_client.publish(topic, payload, retained)", direct)
        dispatch = mqtt.split("static bool mqtt_publish_measured(", 1)[1].split("static void mqtt_start_publish_worker", 1)[0]
        self.assertIn("mqtt_publish_worker.publish(topic, payload, retained)", dispatch)
        self.assertIn("MqttConfigGuard guard;", dispatch)
        self.assertIn("if (!guard.locked()) return false;", dispatch)
        shutdown = mqtt.split("static void mqtt_publish_all_availability_offline()", 1)[1].split("static bool mqtt_registry_has_addr", 1)[0]
        self.assertLess(shutdown.index("MqttConfigGuard guard;"), shutdown.index("mqtt_client.connected()"))
        start = mqtt.split("static void mqtt_start_publish_worker(", 1)[1].split("static void mqtt_client_loop_measured", 1)[0]
        self.assertIn("#if MASTER_MQTT_PUBLISH_CORE0", start)
        self.assertIn("if (!mqtt_enabled || attempted) return;", start)
        loop = mqtt.split("static void mqtt_loop()", 1)[1]
        self.assertLess(loop.index("if (!mqtt_enabled)"), loop.index("mqtt_start_publish_worker();"))
        worker = (SRC / "MqttPublishWorker.h").read_text(encoding="utf-8")
        self.assertNotIn("malloc", worker)
        self.assertNotIn("retry", worker)
        self.assertIn("while (xSemaphoreTake(completed_, portMAX_DELAY) != pdTRUE)", worker)


if __name__ == "__main__":
    unittest.main()
