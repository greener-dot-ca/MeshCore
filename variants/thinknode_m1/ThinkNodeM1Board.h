#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (4)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class ThinkNodeM1Board : public NRF52Board {
public:
  ThinkNodeM1Board() : NRF52Board("THINKNODE_M1_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  #if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
  #endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode-M1";
  }

  void powerOff() override {

    // turn off all leds, sd_power_system_off will not do this for us
    #ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, LOW);
    #endif

    // Arm the two function keys (active-low, internal pull-up) as SYSTEM OFF wake
    // sources, so a press cold-boots the board out of Hibernate. Both are armed so
    // either key wakes it. Mirrors MeshtinyBoard / TechoCardBoard.
    nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_BUTTON1], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_BUTTON2], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

    // Enter SYSTEM OFF. Prefer the SoftDevice call when it's enabled; fall back to the
    // POWER register directly so we always actually power down (a bare sd_power_system_off
    // is a no-op when the SoftDevice is disabled, which would leave the device running).
    uint8_t sd_enabled = 0;
    sd_softdevice_is_enabled(&sd_enabled);
    if (sd_enabled) {
      sd_power_system_off();
    } else {
      NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
    }

  }
};
