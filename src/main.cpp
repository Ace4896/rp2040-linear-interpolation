#include <Arduino.h>
#include <pico/stdlib.h>
#include <hardware/interp.h>

void times_table();
void blending();
void adc_linear_interpolation();

void setup()
{
    pinMode(PIN_LED, OUTPUT);
}

void loop()
{
    Serial.println("Enabling LED");
    digitalWrite(PIN_LED, HIGH);
    delay(1000);

    Serial.println("Disabling LED");
    digitalWrite(PIN_LED, LOW);
    delay(1000);

    // times_table();
    // blending();
    adc_linear_interpolation();
}

void times_table()
{
    Serial.println("9 times table:");

    // Initialise lane 0 on interp0 for the current core (should be core 0)
    interp_config cfg = interp_default_config();
    interp_set_config(interp0, 0, &cfg);

    // From the diagram, we have the following inputs:
    // - Accumulator 0 and 1
    // - Base 0, 1, and 2
    //
    // And the following outputs:
    // - Result 0, 1, and 2

    // For the 9 times table, we just want to repeatedly add values
    // This can be achieved by:
    // - Setting accumulator 0 to 0
    // - Setting increment to 9
    interp0->accum[0] = 0;
    interp0->base[0] = 9;

    // If we were to peek the result without updating the accmulator, we can see the result
    // This will do 0 + 9 = 9
    Serial.printf("Peek: 9 x 1 = %d\r\n", (int) interp0->peek[0]);

    // If we repeatedly pop the results (i.e. peek + update accumulator), we can see the values going up
    // It'll remain in this state until we re-initialise the interpolator
    for(int i = 1; i <= 10; i++)
    {
        Serial.printf("Pop: 9 x %d = %d\r\n", i, (int) interp0->pop[0]);
    }
}

void blending()
{
    Serial.println("Blending:");

    // Interp0 on each core is capable of enabling blend mode, which performs linear interpolation
    // x = x0 + a(x1 - x0)
    //
    // Where:
    // 0 <= a < 1
    // x0 = Base 0 register
    // x1 = Base 1 register
    // a = Fractional value formed from least significant 8 bits of the lane 1 shift/mask value
    //
    // Some things to keep in mind:
    // - PEEK0 / POP0 return the 8-bit value a (bits )
    // - PEEK1 / POP1 return the linear interpolation between base 0 and base 1
    // - PEEK2 / POP2 do not include the lane 1 result in the addition (i.e. it returns Base 2 + lane 0 shift/mask value)

    // Setup lane 0 w/ blending enabled
    interp_config cfg = interp_default_config();
    interp_config_set_blend(&cfg, true);
    interp_set_config(interp0, 0, &cfg);

    interp0->base[0] = 500;     // x0
    interp0->base[1] = 1000;    // x1

    // Setup lane 1
    cfg = interp_default_config();
    interp_set_config(interp0, 1, &cfg);

    for (int i = 0; i <= 6; i++)
    {
        // Set the fraction to between 0 and 255
        interp0->accum[1] = 255 * i / 6;

        // 500 + (1000 - 500) * i / 6
        // One key restriction is that 'a' can't be 1
        // So we get something that is close, but not quite there
        Serial.printf("%d\n", (int) interp0->peek[1]);
    }
}

void adc_linear_interpolation()
{
    Serial.println("ADC Linear Interpolation");

    // For my use case, I need to interpolate the ADC values to get them to match
    // The first step is determining the scale
    // - Expected ADC Range: [1000, 3000]
    // - Calibrated ADC Range: [900, 2800]
    //
    // The expected scale is 3000 - 1000 = 2000 steps
    // The calibrated scale is 2800 - 900 = 1900 steps
    //
    // If the raw value is 1500, then the fraction, a, would be (1500 - 900) / 1900 = 600 / 1900
    // To map this to the expected value:
    //
    // x = 1000 + (3000 - 1000) * 600 / 1900
    //   = 1000 + 2000 * 6/19
    //   ~= 1631.579
    int expected_lo = 1000;
    int expected_hi = 3000;
    int calibrated_lo = 900;
    int calibrated_hi = 2800;
    int raw_val = 1500;

    // Setup lane 0 w/ blending enabled
    interp_config cfg = interp_default_config();
    interp_config_set_blend(&cfg, true);
    interp_set_config(interp0, 0, &cfg);

    interp0->base[0] = expected_lo;
    interp0->base[1] = expected_hi;

    // Setup lane 1
    cfg = interp_default_config();
    interp_set_config(interp0, 1, &cfg);

    // Set the value of the fraction
    interp0->accum[1] = 255 * (raw_val - calibrated_lo) / (calibrated_hi - calibrated_lo);

    // Read the result
    // The values will always be 99.6% of the expected value, but calculating in hardware is significantly faster
    // We can correct it by adding 1/256 of the approximated value, though there's still an error margin
    int software = expected_lo + (expected_hi - expected_lo) * (raw_val - calibrated_lo) / (calibrated_hi - calibrated_lo);
    int hardware = (int) interp0->peek[1];
    int hardware_corrected = hardware + (hardware >> 8);    // Add 1/256 of the approximate value

    Serial.printf("Software: %d\r\n", software);
    Serial.printf("HW Accelerated: %d\r\n", hardware);
    Serial.printf("HW Accelerated (Corrected): %d\r\n", hardware_corrected);

    // Repeated version
    for (int adc_val = 1000; adc_val <= calibrated_hi; adc_val += 100)
    {
        interp0->accum[1] = 255 * (adc_val - calibrated_lo) / (calibrated_hi - calibrated_lo);

        // A few of these calculations can be done ahead of time
        // expected_hi and expected_lo are compile-time constants, so we know this already
        // calibrated_hi and calibrated_lo is constant if manual calibration is used
        int soft = expected_lo + (expected_hi - expected_lo) * (adc_val - calibrated_lo) / (calibrated_hi - calibrated_lo);
        int hw = (int) interp0 -> peek[1];
        int hw_corrected = hw + (hw >> 8);

        Serial.printf("Interpolating %d (actual) between calibrated range (%d, %d) and mapping to expected range (%d, %d)\r\n",
            adc_val,
            calibrated_lo, calibrated_hi,
            expected_lo, expected_hi
        );

        Serial.printf("- Software: %d\r\n", soft);
        Serial.printf("- HW Accelerated: %d\r\n", hw);
        Serial.printf("- HW Accelerated (Corrected): %d\r\n\r\n", hw_corrected);
    }
}
