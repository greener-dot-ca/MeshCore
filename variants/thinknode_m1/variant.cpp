#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const int MISO = PIN_SPI1_MISO;
const int MOSI = PIN_SPI1_MOSI;
const int SCK = PIN_SPI1_SCK;

const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant() {
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);

  // LEDs: the blue "Status" LED (pin 13) is set up by ThinkNodeM1Board::begin()
  // as the LoRa-TX indicator; the red "Power" LED (pin 36) is hardware-managed.
  // Nothing to configure here.

  pinMode(PIN_TXCO, OUTPUT);
  digitalWrite(PIN_TXCO, HIGH);

  // shutdown gps
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, LOW);
}
