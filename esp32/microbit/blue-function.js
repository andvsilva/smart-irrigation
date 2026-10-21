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
    command = command.trim().toUpperCase()
if (command == "ON") {
        pins.digitalWritePin(DigitalPin.P12, 1)
    } else if (command == "OFF") {
        pins.digitalWritePin(DigitalPin.P12, 0)
    }
})
let temperature = 0
let humidity = 0
let connected = false
let command = ""
bluetooth.startUartService()
basic.showIcon(IconNames.Yes)
basic.forever(function () {
    // Leitura dos sensores
    humidity = pins.analogReadPin(AnalogPin.P1) / 10
    temperature = input.temperature()
    // Envio dos dados ao ESP32
    if (connected) {
        bluetooth.uartWriteLine("TEMP:" + temperature + ", HUMIDADE:" + Math.round(humidity))
    }
    // Controle das saídas
    if (humidity < 30) {
        pins.digitalWritePin(DigitalPin.P2, 0)
        pins.digitalWritePin(DigitalPin.P8, 1)
        pins.digitalWritePin(DigitalPin.P12, 1)
        basic.pause(2000)
        pins.digitalWritePin(DigitalPin.P12, 0)
    } else {
        pins.digitalWritePin(DigitalPin.P8, 0)
        pins.digitalWritePin(DigitalPin.P2, 1)
        basic.pause(2000)
    }
    // Exibição dos valores no display LED
    basic.showNumber(Math.round(humidity))
    basic.pause(500)
    basic.showNumber(temperature)
    basic.pause(500)
})