#include <Arduino.h>
#define WL_MAC_ADDR_LENGTH 6 // For some reason this is missing
#include <WiFiS3.h>
#define IR_RECEIVE_PIN      2
#define IR_SEND_PIN         3
#include <IRremote.hpp>
#include <ArduinoHA.h>

// Secrets (WiFi/MQTT creds)
#include "Secrets.h"

// Constants
constexpr uint8_t c_maxFanSpeed = 3;
constexpr uint8_t c_minFanSpeed = 1;
constexpr uint8_t c_minTempC = 16;
constexpr uint8_t c_maxTempC = 32;
constexpr uint8_t c_irRepeats = 0;

// Helper structures
enum class WhynterACMode
{
    AirCondition = 0x1,
    Heat         = 0x2,
    DeHumidify   = 0x4,
    Fan          = 0x8,
};

enum class WhynterACTempUnitKind
{
    Fahrenheit,
    Celcius,
};

struct WhynterACState
{
    bool Power;
    WhynterACMode Mode;
    uint8_t FanSpeed;
    WhynterACTempUnitKind TemperatureUnits;
    uint8_t Temperature;
};

// Statics
WiFiClient s_wifiClient;
HADevice s_haDevice;
HAMqtt s_mqttClient{ s_wifiClient, s_haDevice };
HAHVAC s_haHvacDevice{
    c_deviceName,
    (HAHVAC::TargetTemperatureFeature | HAHVAC::PowerFeature |
        HAHVAC::FanFeature | HAHVAC::ModesFeature)
};
WhynterACState s_acState{
    .Power = true,
    .Mode = WhynterACMode::AirCondition,
    .FanSpeed = 3,
    .TemperatureUnits = WhynterACTempUnitKind::Celcius,
    .Temperature = 22
};
bool s_lastMqttConnectedState = false;

// Home Assistant callbacks
void OnTargetTemperatureCommand(HANumeric temperature, HAHVAC* sender)
{
    uint8_t targetTemp = static_cast<uint8_t>(roundf(temperature.toFloat()));
    Serial.print("[HA] Target temperature: ");
    Serial.println(targetTemp);
    s_acState.Temperature = targetTemp;
    SendWhynterIRCommand(s_acState);
    sender->setTemperatureUnit(HAHVAC::TemperatureUnit::CelsiusUnit);
    sender->setTargetTemperature(targetTemp); // report target temperature back to the HA panel
}

void OnPowerCommand(bool state, HAHVAC* sender)
{
    Serial.print("[HA] Power: ");
    Serial.println(state ? "on" : "off");
    s_acState.Power = state;
    SendWhynterIRCommand(s_acState);
}

void OnModeCommand(HAHVAC::Mode mode, HAHVAC* sender)
{
    Serial.print("[HA] Mode: ");
    HAHVAC::Mode selectedMode = HAHVAC::Mode::CoolMode;
    switch (mode)
    {
    case HAHVAC::OffMode:
        selectedMode = HAHVAC::OffMode;
        s_acState.Power = false;
        Serial.println("off");
        break;
    case HAHVAC::HeatMode:
        selectedMode = HAHVAC::HeatMode;
        s_acState.Mode = WhynterACMode::Heat;
        s_acState.Power = true;
        Serial.println("heat");
        break;
    case HAHVAC::DryMode:
        selectedMode = HAHVAC::DryMode;
        s_acState.Mode = WhynterACMode::DeHumidify;
        s_acState.Power = true;
        Serial.println("dry");
        break;
    case HAHVAC::FanOnlyMode:
        selectedMode = HAHVAC::FanOnlyMode;
        s_acState.Mode = WhynterACMode::Fan;
        s_acState.Power = true;
        Serial.println("fan only");
        break;
    case HAHVAC::CoolMode:
    case HAHVAC::AutoMode:
    default:
        selectedMode = HAHVAC::CoolMode;
        s_acState.Mode = WhynterACMode::AirCondition;
        s_acState.Power = true;
        Serial.println("cool");
        break;
    }
    SendWhynterIRCommand(s_acState);
    sender->setMode(selectedMode); // report mode back to the HA panel
}

void OnFanModeCommand(HAHVAC::FanMode fanMode, HAHVAC* sender)
{
    Serial.print("[HA] Fan mode: ");
    HAHVAC::FanMode targetFanMode = HAHVAC::FanMode::HighFanMode;
    switch (fanMode)
    {
    case HAHVAC::FanMode::LowFanMode:
        targetFanMode = fanMode;
        s_acState.FanSpeed = 1;
        Serial.println("low");
        break;
    case HAHVAC::FanMode::MediumFanMode:
        targetFanMode = fanMode;
        s_acState.FanSpeed = 2;
        Serial.println("medium");
        break;
    case HAHVAC::FanMode::HighFanMode:
        targetFanMode = fanMode;
        s_acState.FanSpeed = 3;
        Serial.println("high");
        break;
    default:
        targetFanMode = HAHVAC::FanMode::HighFanMode;
        s_acState.FanSpeed = 3;
        Serial.println("high");
        break;
    }
    SendWhynterIRCommand(s_acState);
    sender->setFanMode(targetFanMode);
}

uint32_t GetWhynterIRPayload(const WhynterACState& state)
{
    // Parse payload
    uint8_t hexFanSpeed = 0x2;
    switch (state.FanSpeed)
    {
    case 2:
        hexFanSpeed = 0x4;
        break;
    case 3:
        hexFanSpeed = 0x8;
        break;
    default:
        break;
    }

    uint32_t payload = 0x48; // Always ends in 0x48
    // Apply fan speed
    payload |= (static_cast<uint32_t>(hexFanSpeed) << 8);
    // Apply mode
    payload |= (static_cast<uint32_t>(state.Mode) << 12);

    if (state.TemperatureUnits == WhynterACTempUnitKind::Celcius)
    {
        uint32_t tempDelta = 0;
        if (state.Temperature >= c_minTempC && state.Temperature <= c_maxTempC)
        {
            tempDelta = state.Temperature - c_minTempC;
        }
        if (state.Power)
        {
            payload |= (0x00880000); // 0x88 for "on/celcius"
        }
        else
        {
            payload |= (0x00120000); // 0x12 for "off/celcius"
        }
        payload |= (static_cast<uint32_t>(tempDelta) << 24);
    }
    return payload;
}

void SendWhynterIRCommand(const WhynterACState& state)
{
#ifdef DEBUG
    Serial.print(F("Sending IR air conditioner payload..."));
    Serial.print(F("\n\tPower: "));
    Serial.print(state.Power ? "On" : "Off");
    Serial.print(F("\n\tMode: "));
    switch (state.Mode)
    {
    case WhynterACMode::AirCondition:
        Serial.print(F("Air Condition"));
        break;
    case WhynterACMode::Heat:
        Serial.print(F("Heat"));
        break;
    case WhynterACMode::DeHumidify:
        Serial.print(F("DeHumidify"));
        break;
    case WhynterACMode::Fan:
        Serial.print(F("Fan"));
        break;
    default:
        Serial.print(F("UNKNOWN"));
        break;
    }
    Serial.print(F("\n\tFan Speed: "));
    Serial.print(state.FanSpeed, DEC);
    Serial.print(F("\n\tTemperature Units: "));
    switch (state.TemperatureUnits)
    {
    case WhynterACTempUnitKind::Fahrenheit:
        Serial.print(F("Fahrenheit"));
        break;
    case WhynterACTempUnitKind::Celcius:
        Serial.print(F("Celcius"));
        break;
    default:
        Serial.print(F("UNKNOWN"));
        break;
    }
    Serial.print(F("\n\tTemperature: "));
    Serial.print(state.Temperature);
    Serial.println();
#endif
    uint32_t payload = GetWhynterIRPayload(state);
    uint16_t address = static_cast<uint16_t>(payload);
    uint16_t command = static_cast<uint16_t>(payload >> 16);
    IrSender.sendOnkyo(address, command, c_irRepeats);

#ifdef DEBUG
    Serial.print(F("Address: 0x"));
    Serial.print(address, HEX);
    Serial.print(F(", Command: 0x"));
    Serial.print(command, HEX);
    Serial.println();
#endif
}

void PublishCurrentACState()
{
    HAHVAC::Mode mode = HAHVAC::Mode::CoolMode;
    HAHVAC::FanMode fanMode = HAHVAC::FanMode::HighFanMode;
    switch (s_acState.Mode)
    {
    case WhynterACMode::Heat:
        mode = HAHVAC::Mode::HeatMode;
        break;
    case WhynterACMode::DeHumidify:
        mode = HAHVAC::Mode::DryMode;
        break;
    case WhynterACMode::Fan:
        mode = HAHVAC::Mode::FanOnlyMode;
        break;
    default:
    case WhynterACMode::AirCondition:
        mode = HAHVAC::Mode::CoolMode;
        break;
    }
    switch (s_acState.FanSpeed)
    {
    case 1:
        fanMode = HAHVAC::FanMode::LowFanMode;
        break;
    case 2:
        fanMode = HAHVAC::FanMode::MediumFanMode;
        break;
    default:
    case 3:
        fanMode = HAHVAC::FanMode::HighFanMode;
        break;
    }
    s_haHvacDevice.setMode(mode);
    s_haHvacDevice.setFanMode(fanMode);
    s_haHvacDevice.setTargetTemperature(s_acState.Temperature);
}

void setup()
{
    Serial.begin(115200);
    Serial.println(F("START " __FILE__ " from " __DATE__ "\r\n"));
    Serial.print(F("IR pin: "));
    Serial.println(IR_SEND_PIN);
    Serial.print(F("WiFi SSID: "));
    Serial.println(c_wifiSsid);
    Serial.print(F("Hostname: "));
    Serial.println(c_myWifiHostname);
    Serial.print(F("Home Assistant host: "));
    Serial.println(c_mqttBrokerHostname);

    // Check to make sure we have a WiFi module
    if (WiFi.status() == WL_NO_MODULE)
    {
        Serial.println("Communication with WiFi module failed!");
        while (true);
    }

    // Attempt to connect to WiFi network:
    WiFi.setHostname(c_myWifiHostname);
    Serial.print("Connecting to WiFi network...");
    WiFi.begin(c_wifiSsid, c_wifiPassword);
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.println("Connected.");

    // Set the AC unit to the default state
    Serial.println(F("Transmitting default state to AC unit"));
    SendWhynterIRCommand(s_acState);

    // Assign details of HA device
    byte mac[WL_MAC_ADDR_LENGTH];
    WiFi.macAddress(mac);
    s_haDevice.setUniqueId(mac, sizeof(mac));
    s_haDevice.setName(c_deviceName);

    // Assign details of AC device
    s_haHvacDevice.onTargetTemperatureCommand(OnTargetTemperatureCommand);
    s_haHvacDevice.onPowerCommand(OnPowerCommand);
    s_haHvacDevice.onModeCommand(OnModeCommand);
    s_haHvacDevice.onFanModeCommand(OnFanModeCommand);
    s_haHvacDevice.setTemperatureUnit(HAHVAC::TemperatureUnit::CelsiusUnit);
    s_haHvacDevice.setMinTemp(static_cast<float>(c_minTempC));
    s_haHvacDevice.setMaxTemp(static_cast<float>(c_maxTempC));
    s_haHvacDevice.setTempStep(1);
    s_haHvacDevice.setModes(HAHVAC::Mode::HeatMode | HAHVAC::Mode::DryMode |
        HAHVAC::Mode::FanOnlyMode | HAHVAC::Mode::CoolMode | HAHVAC::Mode::OffMode);
    s_haHvacDevice.setFanModes(HAHVAC::FanMode::LowFanMode |
        HAHVAC::FanMode::MediumFanMode | HAHVAC::FanMode::HighFanMode);
    //s_haHvacDevice.setRetain(true); // Not really sure what this does yet.

    // Set state of AC device
    PublishCurrentACState();

    // Start mqtt
    Serial.println("Starting MQTT client...");
    s_mqttClient.begin(c_mqttBrokerHostname, c_mqttBrokerUsername, c_mqttBrokerPassword);
}

void loop()
{
    s_mqttClient.loop();
    if (s_lastMqttConnectedState != s_mqttClient.isConnected())
    {
        s_lastMqttConnectedState = !s_lastMqttConnectedState;
        if (s_lastMqttConnectedState)
        {
            Serial.println("MQTT connected");
        }
        else
        {
            Serial.println("MQTT disconnected");
        }
    }
}
