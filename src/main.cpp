// Required Libraries:
// 1. FastLED (for WS2812B control)
// 2. Wire (for I2C communication with MPU-6050)
#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>

// --- HARDWARE CONFIGURATION ---
#define LED_PIN 22    // Data pin for the WS2812B strip
#define NUM_LEDS 30   // Total number of LEDs in the strip
#define MPU_ADDR 0x68 // MPU-6050 I2C address
#define SDA_PIN 4     // I2C SDA pin for MPU-6050
#define SCL_PIN 5     // I2C SCL pin for MPU-6050

// --- ANIMATION CONSTANTS ---
const float BOUNCE_DAMPING = 0.85; // Velocity retention on bounce (0.0 to 1.0)
const float MAX_VELOCITY = 1.0;    // Maximum speed of the water flow
const float GLOW_DECAY = 0.5;      // Glow intensity decay rate
const CRGB WATER_COLOR = CRGB::Blue;
const int ASCII_STRIP_LENGTH = NUM_LEDS; // Length for Serial Monitor visualization

// --- SHARED DATA (Accessed by both cores) ---
// Note: For production code, use proper synchronization (mutex/spin-lock)
// Simple volatile variables are used here for inter-core communication

volatile float g_tilt_x = 0.0;           // Tilt value from MPU-6050 or simulator
volatile bool g_sensor_connected = false; // MPU-6050 connection status

// --- CORE 1 VARIABLES (LED and Visualization) ---
CRGB leds[NUM_LEDS];
float water_pos = (float)NUM_LEDS / 2.0; // Floating point position of water mass
float water_vel = 0.0;                   // Velocity of water flow
float flow_time_offset = 0.0;            // Time offset for shimmer effect

// =================================================================================
// CORE 0: SENSOR READING AND SIMULATION
// =================================================================================

// Read X-axis acceleration from MPU-6050
bool read_mpu6050(float &accX)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // ACCEL_XOUT_H register
  if (Wire.endTransmission(true) != 0)
  {
    return false; // I2C communication failed
  }

  if (Wire.requestFrom(MPU_ADDR, 6, true) != 6)
  {
    return false; // Not enough data received
  }

  int16_t rawX = Wire.read() << 8 | Wire.read();
  // int16_t rawY = Wire.read() << 8 | Wire.read();
  // int16_t rawZ = Wire.read() << 8 | Wire.read();

  // Convert to G's (±2G range, 16384 LSB/G)
  accX = (float)rawX / 16384.0;
  return true;
}

void setup()
{
  // Core 0 initializes I2C and attempts to find the sensor
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  Wire.begin();
  delay(100); // Allow I2C to stabilize

  // Check if MPU-6050 is present
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission(true) == 0)
  {
    // Wake up MPU-6050 (starts in sleep mode)
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x6B); // PWR_MGMT_1 register
    Wire.write(0x00); // Wake up (clear sleep bit)
    Wire.endTransmission(true);
    
    delay(50); // Allow sensor to wake up
    
    g_sensor_connected = true;
  }
  else
  {
    g_sensor_connected = false;
  }
}

void loop()
{
  // Read/Simulate sensor data and update the shared variable
  if (g_sensor_connected)
  {
    // --- Hardware Reading ---
    float new_accel_x;
    if (read_mpu6050(new_accel_x))
    {
      g_tilt_x = new_accel_x;
    }
  }
  else
  {
    // --- Simulated Fallback ---
    // Simulate a slow, gentle oscillation in tilt
    // Time-based wave movement
    float time_sec = millis() / 1000.0;
    // Oscillate between -1.0 G and +1.0 G tilt
    g_tilt_x = sin(time_sec * 0.5) * 0.8;
  }

  // Core 0 loop delay
  delay(20); // Read sensor/simulate at ~50Hz
}

// =================================================================================
// CORE 1: LED CONTROL AND SERIAL MONITOR
// =================================================================================

void setup1()
{
  // Core 1 initializes the LED strip and Serial Monitor
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(150);
  Serial.begin(115200);
}

// Update LED animation based on physics simulation
void update_led_strip()
{
  // 1. Physics Update
  float tilt_accel = g_tilt_x * 0.2; // Scale down the tilt value for animation sensitivity
  water_vel += tilt_accel;           // Apply gravity/tilt to velocity

  // Apply friction/drag to velocity
  water_vel *= 0.98;

  // Clamp velocity to prevent hyper-speed flow
  water_vel = constrain(water_vel, -MAX_VELOCITY, MAX_VELOCITY);

  // Update position
  water_pos += water_vel;

  // 2. Edge Reflection/Bounce Logic
  if (water_pos < 0)
  {
    water_pos = 0;
    water_vel = -water_vel * BOUNCE_DAMPING; // Reverse and dampen velocity
  }
  else if (water_pos >= NUM_LEDS)
  {
    water_pos = (float)NUM_LEDS - 0.001;     // Back just inside the edge
    water_vel = -water_vel * BOUNCE_DAMPING; // Reverse and dampen velocity
  }

  // 3. Clear strip and apply new glow
  FastLED.clear();

  // Update shimmer offset
  flow_time_offset += 0.05;

  for (int i = 0; i < NUM_LEDS; i++)
  {
    // Calculate distance from the water's center (water_pos)
    float dist = abs(i - water_pos);

    // Exponential/Gaussian decay for the glow intensity
    float intensity = exp(-dist * GLOW_DECAY);

    // Apply shimmer (fun touch)
    // Add a sine wave based on flow position and time for a subtle sparkle/shimmer
    float shimmer = sin((i * 0.3) + flow_time_offset) * 0.2 + 0.8;
    intensity *= shimmer;

    // Apply color and intensity
    CRGB color = WATER_COLOR;
    color.nscale8(intensity * 255); // Scale color by intensity (0-255)

    leds[i] = color;
  }
}

// Generate ASCII visualization for Serial Monitor
void create_ascii_visualization(char* buffer, int buffer_size)
{
  if (buffer_size < NUM_LEDS + 1) return;

  for (int i = 0; i < NUM_LEDS; i++)
  {
    // Get the brightness of the LED (V in HSV, or just max R/G/B here)
    uint8_t brightness = max(max(leds[i].r, leds[i].g), leds[i].b);

    if (brightness > 180)
    {
      buffer[i] = '~'; // Bright water glow
    }
    else if (brightness > 50)
    {
      buffer[i] = '='; // Medium glow/water body
    }
    else if (brightness > 10)
    {
      buffer[i] = '_'; // Background/dim edge
    }
    else
    {
      buffer[i] = ' '; // Off/empty space
    }
  }
  buffer[NUM_LEDS] = '\0'; // Null terminate
}

void loop1()
{
  // 1. Update LED State
  update_led_strip();

  // 2. Show LEDs
  FastLED.show();

  // 3. Serial Monitoring (MUST print on one line per frame)
  static char ascii_visual[NUM_LEDS + 1]; // Static buffer for ASCII visualization
  
  create_ascii_visualization(ascii_visual, sizeof(ascii_visual));

  // Use carriage return '\r' to move the cursor to the beginning of the line
  // and print without a newline to overwrite the previous line.
  Serial.print('\r');
  Serial.print(g_sensor_connected ? "[H/W]" : "[SIM]");
  Serial.print(" Tilt (X): ");
  Serial.print(g_tilt_x, 2);
  Serial.print(" | Pos: ");
  Serial.print(water_pos, 2);
  Serial.print(" | Flow: [");
  Serial.print(ascii_visual);
  Serial.print("] ");

  // Core 1 loop delay
  delay(33); // Run animation/display at ~30 FPS
}