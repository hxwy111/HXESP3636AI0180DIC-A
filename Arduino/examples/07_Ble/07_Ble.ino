#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Custom UART-like GATT service. Keep these UUIDs unchanged when migrating to ESP-IDF.
static constexpr char kDeviceName[] = "HXESP3636-BLE";
static constexpr char kServiceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr char kRxUuid[]      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr char kTxUuid[]      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

BLECharacteristic *g_tx = nullptr;
volatile bool g_connected = false;
uint32_t g_counter = 0;
uint32_t g_next_notify = 0;

class ServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    g_connected = true;
    Serial.println("BLE: client connected");
  }

  void onDisconnect(BLEServer *server) override {
    g_connected = false;
    Serial.println("BLE: client disconnected");
    delay(20);
    server->startAdvertising();
    Serial.println("BLE: advertising restarted");
  }
};

class RxCallbacks final : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    if (value.length() == 0) {
      return;
    }
    Serial.printf("BLE RX (%u): ", static_cast<unsigned>(value.length()));
    Serial.write(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    Serial.println();

    // Echo received data through the notify characteristic.
    if (g_connected && g_tx != nullptr) {
      g_tx->setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
      g_tx->notify();
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("HXESP3636 BLE test starting");

  BLEDevice::init(kDeviceName);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(kServiceUuid);
  BLECharacteristic *rx = service->createCharacteristic(
      kRxUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_tx = service->createCharacteristic(
      kTxUuid, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  g_tx->addDescriptor(new BLE2902());
  rx->setCallbacks(new RxCallbacks());
  g_tx->setValue("HXESP3636 BLE ready");

  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.printf("BLE: name=%s\n", kDeviceName);
  Serial.println("BLE: advertising started");
}

void loop() {
  const uint32_t now = millis();
  if (g_connected && static_cast<int32_t>(now - g_next_notify) >= 0) {
    char message[48];
    const int length = snprintf(message, sizeof(message), "tick=%lu", static_cast<unsigned long>(g_counter++));
    g_tx->setValue(reinterpret_cast<const uint8_t *>(message), length);
    g_tx->notify();
    g_next_notify = now + 1000;
  }
  delay(10);
}
