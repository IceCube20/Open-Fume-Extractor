#include <assert.h>
#include <stdio.h>
#include <Adafruit_NeoPixel.h>
#include <OfeStatusLed.h>

int main() {
    OfeStatusLed led;
    led.begin();
    led.setBrightness(128);
    led.setBusOnline(true);
    led.setEcoMode(true);
    led.setModuleEvent(OFE_LED_EVENT_WARNING);
    for (unsigned i = 0; i < 10000; ++i) {
        test_millis = i;
        led.tick();
    }
    assert(led.ecoMode() && led.brightness() == 128);
    assert(led.busEvent() == OFE_LED_EVENT_BUS_ONLINE);
#if defined(EXPECT_OLD_LED_BUG)
    assert(led_constructs == 1 && led_begins == 0);
    assert(uninitialized_shows > 0);
    puts("CONFIRMED: original pin=-1 path invokes NeoPixel.show() without begin()");
#elif OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_COUNT > 0
    assert(led_constructs == 1 && led_begins == 1);
    assert(led_shows > 1 && uninitialized_shows == 0);
    const unsigned before = led_shows;
    led.tick();
    assert(led_shows == before);
    puts("PASS: configured LED still renders; unchanged color skips show()");
#else
    assert(led_constructs == 0 && led_begins == 0 && led_shows == 0);
    puts("PASS: disabled/unconfigured/empty LED never constructs or calls NeoPixel");
#endif
}
