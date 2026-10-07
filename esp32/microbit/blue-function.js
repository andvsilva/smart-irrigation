/**
 * ==========================================================
 * 
 * RECEIVE COMMAND FROM ESP32
 * 
 * ==========================================================
 */
/**
 * ==========================================================
 * 
 * MAIN LOOP
 * 
 * ==========================================================
 */
/**
 * ==========================================================
 * 
 * START BLUETOOTH UART
 * 
 * ==========================================================
 */
bluetooth.onBluetoothConnected(function () {
    connected = true
    basic.showIcon(IconNames.SmallHeart)
})
bluetooth.onBluetoothDisconnected(function () {
    connected = false
    basic.showIcon(IconNames.No)
})
bluetooth.onUartDataReceived(serial.delimiters(Delimiters.NewLine), function () {
    command = bluetooth.uartReadUntil(serial.delimiters(Delimiters.NewLine))
    command = command.trim()
command = command.toUpperCase()
// ======================================================
    // COMMAND: ON
    // ======================================================
    // --------------------------------------------------
    // ADD YOUR DEVICE CONTROL HERE
    // Example:
    // 
    // pins.digitalWritePin(DigitalPin.P0, 1)
    // 
    // --------------------------------------------------
    // ======================================================
    // COMMAND: OFF
    // ======================================================
    // --------------------------------------------------
    // ADD YOUR DEVICE CONTROL HERE
    // Example:
    // 
    // pins.digitalWritePin(DigitalPin.P0, 0)
    // 
    // --------------------------------------------------
    if (command == "ON") {
        basic.showString("ON")
    } else if (command == "OFF") {
        basic.showString("OFF")
    }
})
let temperature = 0
let connected = false
let command = ""
bluetooth.startUartService()
basic.showIcon(IconNames.Yes)
basic.pause(1000)
/**
 * ==========================================================
 * 
 * BLUETOOTH CONNECTED
 * 
 * ==========================================================
 */
/**
 * ==========================================================
 * 
 * BLUETOOTH DISCONNECTED
 * 
 * ==========================================================
 */
basic.forever(function () {
    if (connected) {
        // ==================================================
        // MICRO:BIT INTERNAL TEMPERATURE SENSOR
        // ==================================================
        temperature = input.temperature()
        // ==================================================
        // SEND TEMPERATURE TO ESP32
        // ==================================================
        bluetooth.uartWriteLine("TEMP:" + temperature + ", HUMIDADE: " + "99999")
        // ==================================================
        // DISPLAY TEMPERATURE
        // ==================================================
        basic.showNumber(temperature)
        // ==================================================
        // SEND EVERY 2 SECONDS
        // ==================================================
        basic.pause(2000)
    } else {
        basic.pause(500)
    }
})
