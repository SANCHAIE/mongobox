/* ============================================================
   STEP 8 | รวมร่าง! เครื่องวัดความสุก (โหมด METER)
   ------------------------------------------------------------
   เป้าหมาย: เอาทุกอย่างจาก STEP 3-7 มารวมเป็น "เครื่องมือใช้จริง"
   สิ่งใหม่ที่ได้เรียน: การถ่วงน้ำหนัก (weighted score) / การตัดสินใจจากข้อมูล

   คำถามที่เครื่องนี้ตอบ: "มะม่วงลูกนี้กินได้หรือยัง?"
   ============================================================ */

#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 8
#define I2C_SCL 9
#define FAN_PIN 10

// ---- เกณฑ์ที่ปรับได้ (คาลิเบรตทีหลัง) ----
#define HUE_RAW    120.0    // เขียว = ดิบ
#define HUE_RIPE    55.0    // เหลือง = สุก
#define TVOC_BASE    0.0    // อากาศสะอาด
#define TVOC_HIGH  500.0    // แก๊สเยอะ = สุกจัด
#define W_COLOR     0.60    // <-- เชื่อ "สี" 60%
#define W_GAS       0.40    // <-- เชื่อ "แก๊ส" 40%

Adafruit_SGP30 sgp;
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);
Adafruit_BME280 bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

float rgbToHue(float r, float g, float b) {
  float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn;
  if (d < 0.0001) return 0;
  float h;
  if      (mx == r) h = 60.0 * fmod(((g - b) / d), 6.0);
  else if (mx == g) h = 60.0 * (((b - r) / d) + 2.0);
  else              h = 60.0 * (((r - g) / d) + 4.0);
  if (h < 0) h += 360.0;
  return h;
}
int mapClamp(float x, float lo, float hi) {
  float y = (x - lo) / (hi - lo) * 100.0;
  if (y < 0) y = 0;
  if (y > 100) y = 100;
  return (int)y;
}
uint32_t absoluteHumidity(float t, float h) {
  const float ah = 216.7f * ((h / 100.0f) * 6.112f *
                   exp((17.62f * t) / (243.12f + t)) / (273.15f + t));
  return (uint32_t)(1000.0f * ah);
}


/* === i2cAlive(): ตรวจว่ามีอุปกรณ์อยู่จริงไหม (เชื่อถือได้ 100%) ===
   *** บทเรียนสำคัญจากหน้างาน ***
   begin() ของบางไลบรารี (เช่น SSD1306) "โกหก" ได้ — คืนค่า true
   ทั้งที่ไม่ได้ต่อจอเลย! เพราะมันไม่เช็ก ACK จริง
   วิธีนี้เช็กที่ระดับฮาร์ดแวร์ --> โกหกไม่ได้ */
bool i2cAlive(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);        // เปิดพัดลมค้าง (กวนอากาศให้ค่านิ่ง)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);   // ลดเป็น 50kHz — ทนสายยาว/หลวมได้ดีกว่า 100kHz มาก
  delay(500);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  if (!sgp.begin()) Serial.println("[X] SGP30");
  if (!tcs.begin()) Serial.println("[X] TCS34725");
  if (!bme.begin(0x76) && !bme.begin(0x77)) Serial.println("[X] BME280");

  Serial.println("STEP 8: เครื่องวัดความสุก (METER)");
  Serial.println("millis,Temp,Hum,TVOC,Hue,ColorScore,GasScore,MeterScore");
}

void loop() {
  // --- 1) อ่านอุณหภูมิ/ความชื้น ---
  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  if (isnan(temp)) temp = 25.0;
  if (isnan(hum))  hum  = 50.0;

  // --- 2) อ่านแก๊ส (ชดเชยด้วยความชื้น) ---
  sgp.setHumidity(absoluteHumidity(temp, hum));
  int tvoc = 0;
  if (sgp.IAQmeasure()) tvoc = sgp.TVOC;

  // --- 3) อ่านสี ---
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  float rN = 0, gN = 0, bN = 0;
  if (c > 0) { rN = (float)r / c; gN = (float)g / c; bN = (float)b / c; }
  float hue = rgbToHue(rN, gN, bN);

  // --- 4) แปลงเป็นคะแนน แล้ว "ถ่วงน้ำหนัก" รวมกัน ---
  int colorScore = 100 - mapClamp(hue, HUE_RIPE, HUE_RAW);
  int gasScore   = mapClamp((float)tvoc, TVOC_BASE, TVOC_HIGH);
  int meter      = (int)(W_COLOR * colorScore + W_GAS * gasScore);

  // --- 5) แปลเป็นภาษาคน ---
  String stage;
  if      (meter < 30) stage = "DIB (raw)";
  else if (meter < 60) stage = "KUENG-SUK";
  else if (meter < 85) stage = "SUK POR-DEE";
  else                 stage = "SUK NGOM";

  Serial.print(millis());    Serial.print(',');
  Serial.print(temp, 1);     Serial.print(',');
  Serial.print(hum, 1);      Serial.print(',');
  Serial.print(tvoc);        Serial.print(',');
  Serial.print(hue, 0);      Serial.print(',');
  Serial.print(colorScore);  Serial.print(',');
  Serial.print(gasScore);    Serial.print(',');
  Serial.println(meter);

  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print("METER");
  oled.setCursor(62, 0);
  oled.print((int)temp); oled.print("C ");
  oled.print((int)hum);  oled.print("%");

  if (millis() < 16000) {
    oled.setCursor(0, 26); oled.print("SGP30 warming up...");
  } else {
    oled.setTextSize(3); oled.setCursor(0, 14);
    oled.print(meter); oled.print("%");
    oled.drawRect(0, 40, 128, 9, SSD1306_WHITE);
    oled.fillRect(0, 40, map(meter, 0, 100, 0, 128), 9, SSD1306_WHITE);
    oled.setTextSize(1); oled.setCursor(0, 54);
    oled.print(stage);
  }
  oled.display();
  delay(1000);
}

/* ------------------------------------------------------------
   ผลที่ควรได้: จ่อมะม่วงดิบ --> % ต่ำ / มะม่วงสุก --> % สูง

   ลองเล่นดู (สำคัญ!):
   1) เปลี่ยน W_COLOR = 1.00, W_GAS = 0.00 --> เชื่อสีอย่างเดียว ผลต่างไหม?
   2) เปลี่ยน W_COLOR = 0.00, W_GAS = 1.00 --> เชื่อแก๊สอย่างเดียว แม่นไหม?
   3) วัดมะม่วงหลายลูก จดคะแนน แล้วเทียบกับ "การชิมจริง" --> เครื่องแม่นแค่ไหน?

   คำถาม 1: ทำไมเราให้น้ำหนัก "สี" มากกว่า "แก๊ส"?
   คำถาม 2: ถ้าเอาเครื่องนี้ไปวัดมะม่วงคนละพันธุ์ จะยังแม่นไหม? ต้องแก้อะไร?
   ------------------------------------------------------------ */
