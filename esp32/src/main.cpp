#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <math.h>

// ============================================================
// FIRMWARE VERSION
// ============================================================

#define FIRMWARE_VERSION "2026-09-20-MICROBIT-BLE-HUMIDITY-1.1"

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID = "andre";
const char* WIFI_PASSWORD = "@Nd16727316";

// ============================================================
// MICRO:BIT UART
// ============================================================

#define UART_SERVICE_UUID \
    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

// micro:bit -> ESP32
#define MICROBIT_TX_UUID \
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

// ESP32 -> micro:bit
#define MICROBIT_RX_UUID \
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ============================================================
// MICRO:BIT
// ============================================================

const char* MICROBIT_NAME = "BBC micro:bit [vovag]";

// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);

// ============================================================
// BLE OBJECTS
// ============================================================

NimBLEClient* bleClient = nullptr;
NimBLEAdvertisedDevice* microbitDevice = nullptr;

NimBLERemoteService* uartService = nullptr;
NimBLERemoteCharacteristic* microbitTX = nullptr;
NimBLERemoteCharacteristic* microbitRX = nullptr;

// ============================================================
// STATES
// ============================================================

bool bleConnected = false;
bool bleReady = false;
bool shouldConnect = false;
bool shouldScan = true;
bool wifiConnected = false;

// ============================================================
// SENSOR DATA
// ============================================================

float temperature = NAN;
float humidity = NAN;

unsigned long lastTemperatureTime = 0;
unsigned long lastHumidityTime = 0;

// ============================================================
// MESSAGES
// ============================================================

String receiveBuffer = "";
String lastMessage = "";
String lastCommand = "--";

// ============================================================
// TIMERS
// ============================================================

unsigned long lastBLEAttempt = 0;
unsigned long lastWiFiAttempt = 0;

const unsigned long BLE_RETRY_INTERVAL = 5000;
const unsigned long WIFI_RETRY_INTERVAL = 5000;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void startBLEScan();
bool connectToMicrobit();

void processMessage(const String& message);

void notifyCallback(
    NimBLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
);

bool sendCommandToMicrobit(const String& command);

void connectWiFi();

void handleRoot();
void handleData();
void handleControl();
void handleNotFound();

void printCharacteristicInfo(
    NimBLERemoteCharacteristic* characteristic,
    const char* label
);

// ============================================================
// BLE SCAN CALLBACK
// ============================================================

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {

    void onResult(NimBLEAdvertisedDevice* device) override {

        String name = device->getName().c_str();

        Serial.println("--------------------------------");

        if (name.length() == 0) {

            Serial.println("BLE device without name");
            return;
        }

        Serial.print("BLE device found: ");
        Serial.println(name);

        if (
            name == MICROBIT_NAME ||
            name.indexOf("BBC micro:bit") >= 0 ||
            name.indexOf("micro:bit") >= 0
        ) {

            Serial.println();
            Serial.println(">>> MICRO:BIT FOUND <<<");
            Serial.println();

            Serial.print("Name: ");
            Serial.println(name);

            Serial.print("Address: ");
            Serial.println(
                device->getAddress().toString().c_str()
            );

            Serial.print("RSSI: ");
            Serial.println(device->getRSSI());

            if (microbitDevice != nullptr) {

                delete microbitDevice;
                microbitDevice = nullptr;
            }

            microbitDevice =
                new NimBLEAdvertisedDevice(*device);

            shouldConnect = true;

            NimBLEDevice::getScan()->stop();

            Serial.println(">>> MICRO:BIT SAVED <<<");
        }
    }
};

ScanCallbacks scanCallbacks;

// ============================================================
// BLE CLIENT CALLBACKS
// NimBLE-Arduino 1.4.3
// ============================================================

class ClientCallbacks : public NimBLEClientCallbacks {

    void onConnect(NimBLEClient* client) override {

        Serial.println();
        Serial.println("==============================");
        Serial.println("BLE CLIENT CONNECTED");
        Serial.println("==============================");
    }

    void onDisconnect(NimBLEClient* client) override {

        Serial.println();
        Serial.println("==============================");
        Serial.println("BLE DISCONNECTED");
        Serial.println("==============================");

        bleConnected = false;
        bleReady = false;

        microbitTX = nullptr;
        microbitRX = nullptr;
        uartService = nullptr;

        shouldConnect = false;
        shouldScan = true;
    }
};

ClientCallbacks clientCallbacks;

// ============================================================
// BLE NOTIFICATION CALLBACK
// ============================================================

void notifyCallback(
    NimBLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
) {

    Serial.println();
    Serial.println("================================");
    Serial.println("BLE DATA RECEIVED!");
    Serial.println("================================");

    if (characteristic != nullptr) {

        Serial.print("Characteristic: ");

        Serial.println(
            characteristic
                ->getUUID()
                .toString()
                .c_str()
        );
    }

    Serial.print("Type: ");

    if (isNotify) {
        Serial.println("NOTIFICATION");
    } else {
        Serial.println("INDICATION");
    }

    Serial.print("Length: ");
    Serial.println(length);

    // ========================================================
    // HEX
    // ========================================================

    Serial.print("HEX: ");

    for (size_t i = 0; i < length; i++) {

        if (data[i] < 16) {
            Serial.print("0");
        }

        Serial.print(data[i], HEX);
        Serial.print(" ");
    }

    Serial.println();

    // ========================================================
    // ASCII
    // ========================================================

    Serial.print("ASCII: ");

    for (size_t i = 0; i < length; i++) {

        char c = (char)data[i];

        if (c >= 32 && c <= 126) {
            Serial.print(c);
        } else {
            Serial.print(".");
        }
    }

    Serial.println();

    // ========================================================
    // BUFFER
    // ========================================================

    for (size_t i = 0; i < length; i++) {

        char c = (char)data[i];

        if (c == '\n' || c == '\r') {

            if (receiveBuffer.length() > 0) {

                Serial.print("COMPLETE MESSAGE: ");
                Serial.println(receiveBuffer);

                processMessage(receiveBuffer);

                receiveBuffer = "";
            }

        } else {

            if (receiveBuffer.length() < 200) {

                receiveBuffer += c;

            } else {

                Serial.println(
                    "WARNING: BLE buffer overflow"
                );

                receiveBuffer = "";
            }
        }
    }

    Serial.println("================================");
}

// ============================================================
// SEND COMMAND TO MICRO:BIT
// ============================================================

bool sendCommandToMicrobit(const String& command) {

    if (
        !bleReady ||
        !bleConnected ||
        microbitRX == nullptr
    ) {

        Serial.println();
        Serial.println(
            "Cannot send command: "
            "micro:bit is not ready."
        );

        return false;
    }

    if (!microbitRX->canWrite()) {

        Serial.println();
        Serial.println(
            "Cannot send command: "
            "RX characteristic is not writable."
        );

        return false;
    }

    String message = command + "\n";

    bool success =
        microbitRX->writeValue(
            (uint8_t*)message.c_str(),
            message.length(),
            false
        );

    if (success) {

        lastCommand = command;

        Serial.println();
        Serial.println("==============================");
        Serial.println("COMMAND SENT TO MICRO:BIT");
        Serial.println("==============================");

        Serial.print("Command: ");
        Serial.println(command);

        Serial.println("==============================");

    } else {

        Serial.println();
        Serial.println("==============================");
        Serial.println("ERROR SENDING COMMAND");
        Serial.println("==============================");
    }

    return success;
}

// ============================================================
// PROCESS MESSAGE
//
// Expected:
//
// TEMP:25, HUMIDADE:67
// ============================================================

void processMessage(const String& message) {

    Serial.println();
    Serial.println("==============================");
    Serial.println("PROCESSING MESSAGE");
    Serial.println("==============================");

    Serial.print("Message: ");
    Serial.println(message);

    lastMessage = message;

    String msg = message;
    msg.trim();

    // ========================================================
    // TEMPERATURE
    // ========================================================

    int tempStart = msg.indexOf("TEMP:");

    if (tempStart >= 0) {

        tempStart += 5;

        int tempEnd =
            msg.indexOf(",", tempStart);

        String tempString;

        if (tempEnd >= 0) {

            tempString =
                msg.substring(
                    tempStart,
                    tempEnd
                );

        } else {

            tempString =
                msg.substring(tempStart);
        }

        tempString.trim();

        if (tempString.length() > 0) {

            temperature =
                tempString.toFloat();

            lastTemperatureTime =
                millis();

            Serial.println();
            Serial.println("TEMPERATURE UPDATED");

            Serial.print("Temperature: ");
            Serial.print(temperature, 1);
            Serial.println(" °C");
        }
    }

    // ========================================================
    // HUMIDITY
    // ========================================================

    int humidityStart =
        msg.indexOf("HUMIDADE:");

    if (humidityStart >= 0) {

        humidityStart += 9;

        String humidityString =
            msg.substring(humidityStart);

        humidityString.trim();

        if (humidityString.length() > 0) {

            humidity =
                humidityString.toFloat();

            lastHumidityTime =
                millis();

            Serial.println();
            Serial.println("HUMIDITY UPDATED");

            Serial.print("Humidity: ");
            Serial.print(humidity, 1);
            Serial.println(" %");
        }
    }

    // ========================================================
    // SUMMARY
    // ========================================================

    Serial.println();
    Serial.println("------------------------------");

    Serial.print("Temperature: ");

    if (isnan(temperature)) {

        Serial.println("--");

    } else {

        Serial.print(temperature, 1);
        Serial.println(" °C");
    }

    Serial.print("Humidity: ");

    if (isnan(humidity)) {

        Serial.println("--");

    } else {

        Serial.print(humidity, 1);
        Serial.println(" %");
    }

    Serial.println("------------------------------");
}

// ============================================================
// PRINT CHARACTERISTIC INFO
// ============================================================

void printCharacteristicInfo(
    NimBLERemoteCharacteristic* characteristic,
    const char* label
) {

    Serial.println();
    Serial.println("--------------------------------");
    Serial.println(label);
    Serial.println("--------------------------------");

    if (characteristic == nullptr) {

        Serial.println("NOT FOUND");
        return;
    }

    Serial.print("UUID: ");

    Serial.println(
        characteristic
            ->getUUID()
            .toString()
            .c_str()
    );

    Serial.print("Can READ: ");
    Serial.println(
        characteristic->canRead()
            ? "YES"
            : "NO"
    );

    Serial.print("Can WRITE: ");
    Serial.println(
        characteristic->canWrite()
            ? "YES"
            : "NO"
    );

    Serial.print("Can NOTIFY: ");
    Serial.println(
        characteristic->canNotify()
            ? "YES"
            : "NO"
    );

    Serial.print("Can INDICATE: ");
    Serial.println(
        characteristic->canIndicate()
            ? "YES"
            : "NO"
    );

    Serial.println("--------------------------------");
}

// ============================================================
// CONNECT TO MICRO:BIT
// ============================================================

bool connectToMicrobit() {

    if (microbitDevice == nullptr) {

        Serial.println(
            "ERROR: micro:bit device is null"
        );

        return false;
    }

    Serial.println();
    Serial.println("==============================");
    Serial.println("Connecting to micro:bit...");
    Serial.println("==============================");

    if (bleClient == nullptr) {

        bleClient =
            NimBLEDevice::createClient();

        if (bleClient == nullptr) {

            Serial.println(
                "ERROR: Cannot create BLE client"
            );

            return false;
        }

        bleClient->setClientCallbacks(
            &clientCallbacks,
            false
        );

        bleClient->setConnectTimeout(10);
    }

    Serial.println("Connecting...");

    if (!bleClient->connect(microbitDevice)) {

        Serial.println(
            "ERROR: BLE connection failed"
        );

        bleConnected = false;
        bleReady = false;

        return false;
    }

    bleConnected = true;

    Serial.println("BLE CONNECTED!");
    Serial.println("Searching for UART service...");

    uartService =
        bleClient->getService(
            UART_SERVICE_UUID
        );

    if (uartService == nullptr) {

        Serial.println(
            "ERROR: UART service NOT FOUND!"
        );

        bleClient->disconnect();

        return false;
    }

    Serial.println("UART service found.");

    Serial.println();
    Serial.println("==============================");
    Serial.println("UART CHARACTERISTICS");
    Serial.println("==============================");

    microbitTX =
        uartService->getCharacteristic(
            MICROBIT_TX_UUID
        );

    microbitRX =
        uartService->getCharacteristic(
            MICROBIT_RX_UUID
        );

    printCharacteristicInfo(
        microbitTX,
        "MICRO:BIT TX - 6E400002"
    );

    printCharacteristicInfo(
        microbitRX,
        "MICRO:BIT RX - 6E400003"
    );

    if (microbitTX == nullptr) {

        Serial.println();
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Serial.println("ERROR: 6E400002 NOT FOUND");
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

        bleClient->disconnect();

        return false;
    }

    bool canNotify =
        microbitTX->canNotify();

    bool canIndicate =
        microbitTX->canIndicate();

    Serial.println();
    Serial.println("==============================");
    Serial.println("TX SUBSCRIPTION CAPABILITIES");
    Serial.println("==============================");

    Serial.print("Can NOTIFY: ");
    Serial.println(
        canNotify ? "YES" : "NO"
    );

    Serial.print("Can INDICATE: ");
    Serial.println(
        canIndicate ? "YES" : "NO"
    );

    Serial.println("==============================");

    bool subscribed = false;

    if (canNotify) {

        Serial.println();
        Serial.println(
            "Trying NOTIFICATION subscription..."
        );

        subscribed =
            microbitTX->subscribe(
                true,
                notifyCallback
            );

        if (subscribed) {

            Serial.println(
                "NOTIFICATION SUBSCRIPTION SUCCESS!"
            );
        }

    } else if (canIndicate) {

        Serial.println();
        Serial.println(
            "Trying INDICATION subscription..."
        );

        subscribed =
            microbitTX->subscribe(
                false,
                notifyCallback
            );

        if (subscribed) {

            Serial.println(
                "INDICATION SUBSCRIPTION SUCCESS!"
            );
        }
    }

    if (!subscribed) {

        Serial.println();
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        Serial.println("ERROR: CANNOT SUBSCRIBE");
        Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

        bleReady = false;

        bleClient->disconnect();

        return false;
    }

    bleReady = true;

    Serial.println();
    Serial.println("==============================");
    Serial.println("MICRO:BIT READY!");
    Serial.println("==============================");

    Serial.println(
        "Waiting for temperature and humidity..."
    );

    Serial.println("Expected message:");
    Serial.println("TEMP:25, HUMIDADE:67");

    Serial.println("==============================");

    return true;
}

// ============================================================
// BLE SCAN
// ============================================================

void startBLEScan() {

    Serial.println();
    Serial.println("==============================");
    Serial.println("Scanning for micro:bit...");
    Serial.println("==============================");

    NimBLEScan* scan =
        NimBLEDevice::getScan();

    scan->setAdvertisedDeviceCallbacks(
        &scanCallbacks,
        false
    );

    scan->setActiveScan(true);

    scan->setInterval(100);
    scan->setWindow(80);

    shouldConnect = false;

    scan->start(
        5,
        false
    );

    Serial.println("BLE scan finished.");

    if (!shouldConnect) {

        Serial.println(
            "micro:bit not found."
        );

        shouldScan = true;
    }
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

    if (WiFi.status() == WL_CONNECTED) {

        wifiConnected = true;

        Serial.println();
        Serial.println("========================================");
        Serial.println("WIFI ALREADY CONNECTED");
        Serial.println("========================================");

        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.print("Dashboard: http://");
        Serial.print(WiFi.localIP());
        Serial.println("/");

        Serial.println("========================================");

        return;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("CONNECTING TO WIFI");
    Serial.println("========================================");

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    unsigned long start =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 15000
    ) {

        delay(500);

        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {

        wifiConnected = true;

        Serial.println();
        Serial.println("========================================");
        Serial.println("           WIFI CONNECTED");
        Serial.println("========================================");

        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());

        Serial.print("Signal RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");

        Serial.println();
        Serial.println("========================================");
        Serial.println("          ESP32 DASHBOARD");
        Serial.println("========================================");

        Serial.print("Open in your browser: ");
        Serial.print("http://");
        Serial.print(WiFi.localIP());
        Serial.println("/");

        Serial.println("========================================");
        Serial.println();

        Serial.println();
        Serial.println("========== IP DIAGNOSTIC ==========");

        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());

        Serial.print("Connected: ");
        Serial.println(
            WiFi.status() == WL_CONNECTED ? "YES" : "NO"
        );

        Serial.print("Local IP: ");
        Serial.println(WiFi.localIP());

        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());

        Serial.println("==================================");

    } else {

        wifiConnected = false;

        Serial.println();
        Serial.println("========================================");
        Serial.println("WIFI CONNECTION FAILED");
        Serial.println("========================================");
    }
}

// ============================================================
// WEB DASHBOARD
// ============================================================

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>

<html lang="en">

<head>

<meta charset="UTF-8">

<meta
    name="viewport"
    content="width=device-width, initial-scale=1.0"
>

<title>Micro:bit Environment Monitor</title>

<style>

* {
    box-sizing: border-box;
}

body {

    margin: 0;

    min-height: 100vh;

    display: flex;

    align-items: center;

    justify-content: center;

    font-family:
        Arial,
        Helvetica,
        sans-serif;

    background: #101010;

    color: white;
}

.container {

    width: 92%;

    max-width: 600px;

    padding: 30px;

    background: #1b1b1b;

    border-radius: 20px;

    box-shadow:
        0 10px 40px
        rgba(0,0,0,0.5);

    text-align: center;
}

h1 {

    margin-top: 0;

    font-size: 28px;
}

.sensor-card {

    margin: 20px 0;

    padding: 20px;

    background: #252525;

    border-radius: 15px;
}

.sensor-label {

    color: #999;

    font-size: 16px;

    margin-bottom: 5px;
}

.sensor-value {

    font-size: 56px;

    font-weight: bold;
}

.temperature-value {

    color: #ffb347;
}

.humidity-value {

    color: #66ccff;
}

.status {

    padding: 12px;

    border-radius: 10px;

    margin: 10px 0;
}

.connected {

    background: #123d24;

    color: #55e889;
}

.disconnected {

    background: #3d1616;

    color: #ff6b6b;
}

.info {

    margin-top: 25px;

    padding: 20px;

    background: #252525;

    border-radius: 12px;

    text-align: left;

    line-height: 1.8;
}

.label {

    color: #999;
}

.value {

    color: white;
}

.control {

    margin-top: 20px;

    padding: 20px;

    background: #252525;

    border-radius: 12px;

    text-align: center;
}

.control-title {

    font-size: 18px;

    font-weight: bold;

    margin-bottom: 15px;
}

.control-buttons {

    display: flex;

    gap: 12px;

    justify-content: center;

    flex-wrap: wrap;
}

.control-button {

    border: none;

    border-radius: 10px;

    padding: 14px 24px;

    font-size: 16px;

    font-weight: bold;

    cursor: pointer;

    min-width: 130px;

    transition: 0.2s;
}

.control-button:hover:not(:disabled) {

    transform: translateY(-2px);

    filter: brightness(1.1);
}

.control-button:disabled {

    opacity: 0.35;

    cursor: not-allowed;
}

.control-button.on {

    background: #16803c;

    color: white;
}

.control-button.off {

    background: #a52222;

    color: white;
}

.command-status {

    margin-top: 14px;

    font-size: 14px;

    color: #aaa;

    font-family: monospace;
}

.footer {

    margin-top: 20px;

    color: #777;

    font-size: 12px;
}

.utc-clock {

    margin-top: 8px;

    font-size: 14px;

    color: #aaa;

    font-family: monospace;
}

</style>

</head>

<body>

<div class="container">

<h1>
🌱 Smart Irrigation Monitor
</h1>

<div class="sensor-card">

<div class="sensor-label">
🌡️ Temperature
</div>

<div
    id="temperature"
    class="sensor-value temperature-value"
>
--.- °C
</div>

</div>

<div class="sensor-card">

<div class="sensor-label">
💧 Humidity
</div>

<div
    id="humidity"
    class="sensor-value humidity-value"
>
--.- %
</div>

</div>

<div
    id="status"
    class="status disconnected"
>
Connecting...
</div>

<div class="info">

<div>

<span class="label">
Device:
</span>

<span
    id="device"
    class="value"
>
MB01
</span>

</div>

<div>

<span class="label">
BLE:
</span>

<span
    id="ble"
    class="value"
>
--
</span>

</div>

<div>

<span class="label">
Last update:
</span>

<span
    id="lastUpdate"
    class="value"
>
--
</span>

</div>

<div>

<span class="label">
Last message:
</span>

<span
    id="message"
    class="value"
>
--
</span>

</div>

</div>

<div class="control">

<div class="control-title">
Control Water Pump
</div>

<div class="control-buttons">

<button
    id="buttonOn"
    class="control-button on"
    onclick="sendCommand('ON')"
>
TURN ON
</button>

<button
    id="buttonOff"
    class="control-button off"
    onclick="sendCommand('OFF')"
>
TURN OFF
</button>

</div>

<div
    id="commandStatus"
    class="command-status"
>
Command: --
</div>

</div>

<div class="footer">

<div>
ESP32 + micro:bit + BLE UART
</div>

<div class="utc-clock">

UTC:
<span id="utcTime">
--:--:--
</span>

</div>

</div>

</div>

<script>

// ============================================================
// UPDATE DATA
// ============================================================

async function updateData() {

    try {

        const response =
            await fetch(
                "/data",
                {
                    cache: "no-store"
                }
            );

        const data =
            await response.json();

        // ====================================================
        // TEMPERATURE
        // ====================================================

        if (data.temperature !== null) {

            document
                .getElementById("temperature")
                .innerText =
                data.temperature.toFixed(1)
                + " °C";

        } else {

            document
                .getElementById("temperature")
                .innerText =
                "--.- °C";
        }

        // ====================================================
        // HUMIDITY
        // ====================================================

        if (data.humidity !== null) {

            document
                .getElementById("humidity")
                .innerText =
                data.humidity.toFixed(1)
                + " %";

        } else {

            document
                .getElementById("humidity")
                .innerText =
                "--.- %";
        }

        // ====================================================
        // STATUS
        // ====================================================

        const status =
            document.getElementById("status");

        if (data.bleReady) {

            status.innerText =
                "🟢 Micro:bit ready";

            status.className =
                "status connected";

        } else if (data.bleConnected) {

            status.innerText =
                "🟡 BLE connected";

            status.className =
                "status connected";

        } else {

            status.innerText =
                "🔴 Micro:bit disconnected";

            status.className =
                "status disconnected";
        }

        // ====================================================
        // BLE
        // ====================================================

        document
            .getElementById("ble")
            .innerText =
            data.bleReady
                ? "READY"
                :
                (
                    data.bleConnected
                        ? "CONNECTED"
                        : "DISCONNECTED"
                );

        // ====================================================
        // LAST UPDATE
        // ====================================================

        if (data.lastTemperatureTime > 0) {

            document
                .getElementById("lastUpdate")
                .innerText =
                data.secondsSinceUpdate
                + " seconds ago";

        } else {

            document
                .getElementById("lastUpdate")
                .innerText =
                "Waiting...";
        }

        // ====================================================
        // LAST MESSAGE
        // ====================================================

        document
            .getElementById("message")
            .innerText =
            data.lastMessage || "--";

        // ====================================================
        // BUTTONS
        // ====================================================

        const buttonOn =
            document.getElementById("buttonOn");

        const buttonOff =
            document.getElementById("buttonOff");

        const commandStatus =
            document.getElementById("commandStatus");

        if (buttonOn) {

            buttonOn.disabled =
                !data.bleReady;
        }

        if (buttonOff) {

            buttonOff.disabled =
                !data.bleReady;
        }

        if (
            commandStatus &&
            data.lastCommand &&
            data.lastCommand !== "--"
        ) {

            commandStatus.innerText =
                "Last command: "
                + data.lastCommand;
        }

    } catch (error) {

        console.error(error);
    }
}

// ============================================================
// SEND COMMAND
// ============================================================

async function sendCommand(command) {

    if (!command) {
        return;
    }

    const status =
        document.getElementById(
            "commandStatus"
        );

    if (status) {

        status.innerText =
            "Sending: "
            + command
            + "...";
    }

    try {

        const response =
            await fetch(
                "/control?cmd="
                + encodeURIComponent(command),
                {
                    cache: "no-store"
                }
            );

        const data =
            await response.json();

        if (data.success) {

            if (status) {

                status.innerText =
                    "Command sent: "
                    + command;
            }

        } else {

            if (status) {

                status.innerText =
                    "Command failed: "
                    + (
                        data.error ||
                        "unknown error"
                    );
            }
        }

    } catch (error) {

        console.error(error);

        if (status) {

            status.innerText =
                "Connection error";
        }
    }
}

// ============================================================
// UTC CLOCK
// ============================================================

function updateUTCClock() {

    const now = new Date();

    const hours =
        String(
            now.getUTCHours()
        ).padStart(2, "0");

    const minutes =
        String(
            now.getUTCMinutes()
        ).padStart(2, "0");

    const seconds =
        String(
            now.getUTCSeconds()
        ).padStart(2, "0");

    document
        .getElementById("utcTime")
        .innerText =
        hours
        + ":"
        + minutes
        + ":"
        + seconds;
}

// ============================================================
// INITIALIZE
// ============================================================

updateData();
updateUTCClock();

// ============================================================
// UPDATE EVERY SECOND
// ============================================================

setInterval(
    updateData,
    1000
);

setInterval(
    updateUTCClock,
    1000
);

</script>

</body>

</html>
)rawliteral";

// ============================================================
// HTTP ROOT
// ============================================================

void handleRoot() {

    server.send_P(
        200,
        "text/html",
        MAIN_PAGE
    );
}

// ============================================================
// HTTP DATA
// ============================================================

void handleData() {

    String json = "{";

    // ========================================================
    // TEMPERATURE
    // ========================================================

    json += "\"temperature\":";

    if (isnan(temperature)) {

        json += "null";

    } else {

        json += String(
            temperature,
            1
        );
    }

    json += ",";

    // ========================================================
    // HUMIDITY
    // ========================================================

    json += "\"humidity\":";

    if (isnan(humidity)) {

        json += "null";

    } else {

        json += String(
            humidity,
            1
        );
    }

    json += ",";

    // ========================================================
    // BLE CONNECTED
    // ========================================================

    json += "\"bleConnected\":";

    json +=
        bleConnected
            ? "true"
            : "false";

    json += ",";

    // ========================================================
    // BLE READY
    // ========================================================

    json += "\"bleReady\":";

    json +=
        bleReady
            ? "true"
            : "false";

    json += ",";

    // ========================================================
    // LAST TEMPERATURE TIME
    // ========================================================

    json += "\"lastTemperatureTime\":";

    json += String(
        lastTemperatureTime
    );

    json += ",";

    // ========================================================
    // SECONDS SINCE UPDATE
    // ========================================================

    unsigned long secondsSinceUpdate = 0;

    if (lastTemperatureTime > 0) {

        secondsSinceUpdate =
            (
                millis() -
                lastTemperatureTime
            ) / 1000;
    }

    json += "\"secondsSinceUpdate\":";

    json += String(
        secondsSinceUpdate
    );

    json += ",";

    // ========================================================
    // LAST MESSAGE
    // ========================================================

    json += "\"lastMessage\":\"";

    String safeMessage =
        lastMessage;

    safeMessage.replace(
        "\\",
        "\\\\"
    );

    safeMessage.replace(
        "\"",
        "\\\""
    );

    safeMessage.replace(
        "\n",
        "\\n"
    );

    safeMessage.replace(
        "\r",
        "\\r"
    );

    json += safeMessage;

    json += "\",";

    // ========================================================
    // LAST COMMAND
    // ========================================================

    json += "\"lastCommand\":\"";

    String safeCommand =
        lastCommand;

    safeCommand.replace(
        "\\",
        "\\\\"
    );

    safeCommand.replace(
        "\"",
        "\\\""
    );

    json += safeCommand;

    json += "\"";

    // ========================================================
    // END JSON
    // ========================================================

    json += "}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ============================================================
// HTTP CONTROL
// ============================================================

void handleControl() {

    if (!server.hasArg("cmd")) {

        server.send(
            400,
            "application/json",
            "{\"success\":false,"
            "\"error\":\"Missing cmd\"}"
        );

        return;
    }

    String command =
        server.arg("cmd");

    command.trim();
    command.toUpperCase();

    if (
        command != "ON" &&
        command != "OFF"
    ) {

        server.send(
            400,
            "application/json",
            "{\"success\":false,"
            "\"error\":\"Invalid command\"}"
        );

        return;
    }

    bool success =
        sendCommandToMicrobit(
            command
        );

    if (success) {

        String json =
            "{\"success\":true,"
            "\"command\":\""
            + command +
            "\"}";

        server.send(
            200,
            "application/json",
            json
        );

    } else {

        server.send(
            503,
            "application/json",
            "{\"success\":false,"
            "\"error\":\"micro:bit not ready\"}"
        );
    }
}

// ============================================================
// HTTP 404
// ============================================================

void handleNotFound() {

    server.send(
        404,
        "text/plain",
        "Not found"
    );
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println();

    Serial.println(
        "########################################"
    );

    Serial.println(
        " ESP32 + MICRO:BIT"
    );

    Serial.println(
        " TEMPERATURE + HUMIDITY SYSTEM"
    );

    Serial.println(
        "########################################"
    );

    Serial.print("Firmware: ");
    Serial.println(
        FIRMWARE_VERSION
    );

    Serial.println();

    // ========================================================
    // WIFI
    // ========================================================

    connectWiFi();

    // ========================================================
    // WEB SERVER
    // ========================================================

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/data",
        HTTP_GET,
        handleData
    );

    server.on(
        "/control",
        HTTP_GET,
        handleControl
    );

    server.onNotFound(
        handleNotFound
    );

    server.begin();

    Serial.println(
        "Web server started."
    );

    if (WiFi.status() == WL_CONNECTED) {

        Serial.println();
        Serial.println(
            "========================================"
        );

        Serial.println(
            "          WEB SERVER READY"
        );

        Serial.println(
            "========================================"
        );

        Serial.print(
            "Dashboard URL: http://"
        );

        Serial.print(
            WiFi.localIP()
        );

        Serial.println("/");

        Serial.println(
            "========================================"
        );

        Serial.println();
    }

    // ========================================================
    // BLE
    // ========================================================

    Serial.println();
    Serial.println(
        "Initializing BLE..."
    );

    NimBLEDevice::init(
        "ESP32-Microbit"
    );

    NimBLEDevice::setPower(
        ESP_PWR_LVL_P9
    );

    NimBLEScan* scan =
        NimBLEDevice::getScan();

    scan->setAdvertisedDeviceCallbacks(
        &scanCallbacks,
        false
    );

    scan->setActiveScan(true);

    scan->setInterval(100);
    scan->setWindow(80);

    Serial.println(
        "BLE initialized."
    );

    Serial.println();
    Serial.println(
        "System ready."
    );

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "SYSTEM INFORMATION"
    );

    Serial.println(
        "========================================"
    );

    if (WiFi.status() == WL_CONNECTED) {

        Serial.print(
            "Dashboard: http://"
        );

        Serial.print(
            WiFi.localIP()
        );

        Serial.println("/");
    }

    Serial.println(
        "========================================"
    );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    // ========================================================
    // WEB SERVER
    // ========================================================

    server.handleClient();

    // ========================================================
    // WIFI
    // ========================================================

    if (WiFi.status() != WL_CONNECTED) {

        wifiConnected = false;

        if (
            millis() - lastWiFiAttempt >
            WIFI_RETRY_INTERVAL
        ) {

            lastWiFiAttempt =
                millis();

            Serial.println();
            Serial.println(
                "WiFi disconnected."
            );

            Serial.println(
                "Reconnecting..."
            );

            WiFi.disconnect();

            WiFi.begin(
                WIFI_SSID,
                WIFI_PASSWORD
            );
        }

    } else {

        // Detect Wi-Fi reconnection

        if (!wifiConnected) {

            wifiConnected = true;

            Serial.println();
            Serial.println(
                "========================================"
            );

            Serial.println(
                "          WIFI RECONNECTED"
            );

            Serial.println(
                "========================================"
            );

            Serial.print(
                "SSID: "
            );

            Serial.println(
                WiFi.SSID()
            );

            Serial.print(
                "IP address: "
            );

            Serial.println(
                WiFi.localIP()
            );

            Serial.print(
                "Dashboard: http://"
            );

            Serial.print(
                WiFi.localIP()
            );

            Serial.println("/");

            Serial.print(
                "RSSI: "
            );

            Serial.print(
                WiFi.RSSI()
            );

            Serial.println(
                " dBm"
            );

            Serial.println(
                "========================================"
            );
        }
    }

    // ========================================================
    // BLE CONNECTION CHECK
    // ========================================================

    if (
        bleConnected &&
        bleClient != nullptr
    ) {

        if (!bleClient->isConnected()) {

            bleConnected = false;
            bleReady = false;

            shouldScan = true;
        }
    }

    // ========================================================
    // CONNECT TO MICRO:BIT
    // ========================================================

    if (
        shouldConnect &&
        microbitDevice != nullptr
    ) {

        shouldConnect = false;

        bool success =
            connectToMicrobit();

        if (!success) {

            Serial.println();
            Serial.println(
                "BLE connection setup failed."
            );

            shouldScan = true;
        }
    }

    // ========================================================
    // BLE SCAN
    // ========================================================

    if (
        shouldScan &&
        !bleConnected
    ) {

        if (
            millis() -
            lastBLEAttempt >
            BLE_RETRY_INTERVAL
        ) {

            lastBLEAttempt =
                millis();

            shouldScan = false;

            startBLEScan();
        }
    }

    // ========================================================
    // SENSOR TIMEOUT
    // ========================================================

    if (
        bleReady &&
        lastTemperatureTime > 0
    ) {

        unsigned long elapsed =
            millis() -
            lastTemperatureTime;

        if (elapsed > 10000) {

            static unsigned long
                lastWarning = 0;

            if (
                millis() -
                lastWarning >
                5000
            ) {

                lastWarning =
                    millis();

                Serial.println();
                Serial.println(
                    "WARNING:"
                );

                Serial.println(
                    "No sensor data received"
                );

                Serial.println(
                    "for more than 10 seconds."
                );
            }
        }
    }

    delay(10);
}