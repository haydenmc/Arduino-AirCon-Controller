#include <Arduino.h>
#define IR_RECEIVE_PIN      2
#define IR_SEND_PIN         3
#include <IRremote.hpp>

void setup()
{
    Serial.begin(115200);
    Serial.println(F("START " __FILE__ " from " __DATE__ "\r\nUsing library version " VERSION_IRREMOTE));
    Serial.print(F("Send IR signals at pin "));
    Serial.println(IR_SEND_PIN);
}

void loop()
{
    /*
     * Print current send values
     */
    Serial.println(F("Sending..."));
    Serial.flush();

    /**
     * My Whynter A/C uses the Onkyo protocol at address 0x1848.
     * Payload for ON/Fahrenheit/73/AirCondition/FanSpeed3 = 0x49A8
     * Up (73F)    0x49A8    0100 1001 1010 1000
     * Up (74F)    0x4AA8    0100 1010 1010 1000
     * Up (75F)    0x4BA8    0100 1011 1010 1000
     * OFF         0x4632    0100 0110 0011 0010
     *
     * Second nibble is temperature offset from 64degF ..?
     */
    IrSender.sendOnkyo(0x1848, 0x40A8, 0);

    Serial.println(F("Sent"));
    Serial.flush();

    delay(10000);  // delay must be greater than 5 ms (RECORD_GAP_MICROS), otherwise the receiver sees it as one long signal
    Serial.println(F("Delayed"));
}
