#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

#define PIN 6

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(88, PIN, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

void setup() {
  // This is for Trinket 5V 16MHz, you can remove these three lines if you are not using a Trinket
#if defined (__AVR_ATtiny85__)
  if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
#endif
  // End of trinket special code


  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  danny_test();
}

void danny_test()
{
  uint32_t c;

  uint8_t led[3];
  uint8_t ci = 0; //Color index
  uint32_t li = 0; //Led index
  uint8_t hs_done = 0;

  uint8_t handshake[5] = {'d', 'a', 'n', 'n', 'y'};
  int hsi = 0;

  Serial.begin(115200);


  for (;;)
  {
    if (Serial.available() > 0)
    {
      int sInput  = Serial.read();

      if (hsi < 5)
      {
        if (sInput != handshake[hsi++])
        {
          hsi = 0;
        }

        if (hsi == 5)
        {
          li = 0;
          ci = 0;
          hs_done = 1;
          hsi = 0;
          continue;
        }
      }

      if (hs_done == 1)
      {
        led[ci++] = (uint8_t)sInput;
        if (ci == 3)
        {
          ci = 0;
          c = strip.Color(led[0], led[1], led[2]);

          strip.setPixelColor(li++, c);
          if (li == (88))
          {
            li = 0;
            ci = 0;
            hsi = 0;
            hs_done = 0;
            Serial.write('k');
            strip.show();
          }
        }
      }

    }
  }
}

void loop()
{

}


