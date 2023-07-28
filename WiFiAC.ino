#include <Arduino.h>
#define IR_RECEIVE_PIN      2
#define IR_SEND_PIN         3
#include <IRremote.hpp>

#define DEBUG

constexpr uint8_t c_maxFanSpeed = 3;
constexpr uint8_t c_minFanSpeed = 1;
constexpr uint8_t c_minTempC = 16;
constexpr uint8_t c_maxTempC = 32;
constexpr uint8_t c_irRepeats = 0;

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

void setup()
{
    Serial.begin(115200);
    Serial.println(F("START " __FILE__ " from " __DATE__ "\r\nUsing library version " VERSION_IRREMOTE));
    Serial.print(F("Send IR signals at pin "));
    Serial.println(IR_SEND_PIN);

    WhynterACState state;
    state.Power = true;
    state.Mode = WhynterACMode::AirCondition;
    state.FanSpeed = 3;
    state.TemperatureUnits = WhynterACTempUnitKind::Celcius;
    state.Temperature = 20;

    SendWhynterIRCommand(state);
}

void loop()
{ }
