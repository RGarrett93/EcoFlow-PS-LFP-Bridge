#include "can.h"
#include "ecoflow.h"
#include <esp_err.h>

// --- CAN fast pipeline counters ---
volatile uint32_t can_rx_count   = 0;
volatile uint32_t can_rx_dropped = 0;
volatile uint32_t can_decoded    = 0;

// --- RX queue ---
QueueHandle_t canRxQ = nullptr;

// --- Driver status ---
bool twai_ok = false;

// ---------------- Driver init ----------------
void canInitDriver() {
  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);

  g.rx_queue_len = TWAI_RXQ;
  g.tx_queue_len = TWAI_TXQ;
  g.intr_flags   = 0;  // don't force IRAM

  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  Serial.printf("TWAI pins TX=%d RX=%d, rxQ=%d txQ=%d\n",
                (int)g.tx_io, (int)g.rx_io, g.rx_queue_len, g.tx_queue_len);

  esp_err_t err = twai_driver_install(&g, &t, &f);
  if (err != ESP_OK) {
    Serial.printf("TWAI install failed: %s (rxQ=%d txQ=%d) FreeHeap=%u\n",
                  esp_err_to_name(err), g.rx_queue_len, g.tx_queue_len, (unsigned)ESP.getFreeHeap());
    twai_ok = false;
    return;
  }

  err = twai_start();
  if (err != ESP_OK) {
    Serial.printf("TWAI start failed: %s\n", esp_err_to_name(err));
    twai_driver_uninstall();
    twai_ok = false;
    return;
  }

  uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL |
                    TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_ERR_PASS |
                    TWAI_ALERT_BUS_ERROR | TWAI_ALERT_ARB_LOST |
                    TWAI_ALERT_TX_FAILED | TWAI_ALERT_TX_SUCCESS;
  twai_reconfigure_alerts(alerts, NULL);

  twai_ok = true;
  Serial.println("TWAI CAN initialized (1Mbps)");
}

// ---------------- TX primitive ----------------
bool sendCANFrame(uint32_t can_id, const uint8_t* data, uint8_t len) {
  if (!data || len == 0 || len > 8) {
    Serial.printf("sendCANFrame: bad args (data=%p len=%u)\n", data, len);
    return false;
  }
  twai_message_t msg = {};
  msg.identifier       = can_id & 0x1FFFFFFF;
  msg.extd             = 1;
  msg.rtr              = 0;
  msg.data_length_code = len;
  memcpy(msg.data, data, len);

  esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    return false;
  }
  return true;
}

// ---------------- Tasks ----------------
static void canRxTask(void *arg) {
  twai_message_t msg;
  for (;;) {
    if (!twai_ok) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    esp_err_t r = twai_receive(&msg, pdMS_TO_TICKS(20));

    if (r == ESP_OK) {
      can_rx_count++;

      // RX enabled gate (identical logic)
      if (!config.canRxEnabled) {
        continue;
      }

      if (canRxQ && xQueueSend(canRxQ, &msg, 0) != pdTRUE) {
        twai_message_t dump;
        xQueueReceive(canRxQ, &dump, 0);
        xQueueSend(canRxQ, &msg, 0);
        can_rx_dropped++;
      }

    } else if (r == ESP_ERR_TIMEOUT) {
      // idle
    } else {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

static void canDecodeTask(void *arg) {
  twai_message_t msg;
  for (;;) {
    if (xQueueReceive(canRxQ, &msg, portMAX_DELAY) == pdTRUE) {
      processEcoFlowCAN(msg);
      can_decoded++;
    }
  }
}

// ---------------- Start helpers ----------------
void canStartTasks() {
  if (!twai_ok) return;

  if (!canRxQ) {
    canRxQ = xQueueCreate(256, sizeof(twai_message_t));
  }

  // Same priorities/cores as your current baseline
  xTaskCreatePinnedToCore(canRxTask,     "canRx",     4096, nullptr, 8, nullptr, 0);
  xTaskCreatePinnedToCore(canDecodeTask, "canDecode", 6144, nullptr, 7, nullptr, 0);
}

bool canTryInitAndStart() {
  if (!twai_ok) {
    canInitDriver();
  }
  if (twai_ok) {
    canStartTasks();
    return true;
  }
  return false;
}
