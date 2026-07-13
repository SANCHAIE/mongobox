/* ============================================================
   STEP 5 | วัด "สี" ของเปลือก --> แปลงเป็น % ความสุก
   ------------------------------------------------------------
   เป้าหมาย: อ่านสี RGB แล้วคำนวณเองว่า "เขียวแค่ไหน / เหลืองแค่ไหน"
   สิ่งใหม่ที่ได้เรียน: RGB / Hue / การเขียนฟังก์ชันของตัวเอง / map ค่า

   ไลบรารีเพิ่ม: Adafruit TCS34725
   ** เอาเซ็นเซอร์สีจ่อเปลือกมะม่วงใกล้ ๆ (~1 ซม.) **
   ============================================================ */

#include <Wire.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 8
#define I2C_SCL 9

// เกณฑ์สี: ปรับได้! (นี่คือค่าที่เราจะ "คาลิเบรต" ทีหลัง)
#define HUE_RAW   120.0     // เขียว = ดิบ = 0%
#define HUE_RIPE   55.0     // เหลือง = สุก = 100%

Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

/* ฟังก์ชันของเราเอง: แปลง RGB -> Hue (องศาสีบนวงล้อสี 0-360)
   เขียว ~120 องศา, เหลือง ~55 องศา, แดง ~0 องศา */
float rgbToHue(float r, float g, float b) {
  float mx = max(r, max(g, b));
  float mn = min(r, min(g, b));
  float d  = mx - mn;
  if (d < 0.0001) return 0;
  float h;
  if      (mx == r) h = 60.0 * fmod(((g - b) / d), 6.0);
  else if (mx == g) h = 60.0 * (((b - r) / d) + 2.0);
  else              h = 60.0 * (((r - g) / d) + 4.0);
  if (h < 0) h += 360.0;
  return h;
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
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);   // ลดเป็น 50kHz — ทนสายยาว/หลวมได้ดีกว่า 100kHz มาก
  delay(500);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);

  if (i2cAlive(0x29)) { tcs.begin(); Serial.println("[OK] TCS34725 พร้อม"); }
  else Serial.println("[X] ไม่พบ TCS34725 (0x29) — เช็กสาย");
  Serial.println("STEP 5: วัดสี --> ความสุก");
  Serial.println("R,G,B,Hue,Ripeness");
}

void loop() {
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);      // c = ความสว่างรวม (clear)

  // หาร c เพื่อ "ตัดผลของความสว่าง" ออก เหลือแต่สัดส่วนสีจริง ๆ
  float rN = 0, gN = 0, bN = 0;
  if (c > 0) { rN = (float)r / c;  gN = (float)g / c;  bN = (float)b / c; }

  float hue = rgbToHue(rN, gN, bN);

  // แปลง hue -> % ความสุก
  float pct = (hue - HUE_RIPE) / (HUE_RAW - HUE_RIPE) * 100.0;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int ripeness = 100 - (int)pct;       // เขียวมาก = สุกน้อย

  Serial.print(r); Serial.print(',');
  Serial.print(g); Serial.print(',');
  Serial.print(b); Serial.print(',');
  Serial.print(hue, 0); Serial.print(',');
  Serial.println(ripeness);

  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print("Hue: "); oled.print(hue, 0);
  oled.setTextSize(2); oled.setCursor(0, 20);
  oled.print("RIPE "); oled.print(ripeness); oled.print("%");
  oled.drawRect(0, 48, 128, 10, SSD1306_WHITE);
  oled.fillRect(0, 48, map(ripeness, 0, 100, 0, 128), 10, SSD1306_WHITE);
  oled.display();

  delay(500);
}

/* ------------------------------------------------------------
   ผลที่ควรได้: จ่อใบไม้เขียว --> ripeness ต่ำ / จ่อกระดาษเหลือง --> ripeness สูง

   ลองเล่นดู:
   1) จ่อของหลายสี (ใบไม้ เปลือกมะม่วงดิบ กระดาษเหลือง ส้ม) จด Hue แต่ละอัน
   2) เอาไฟฉายส่อง แล้วเอาออก --> ค่า Hue เปลี่ยนไหม? (ควรเปลี่ยนน้อย เพราะเราหาร c แล้ว)
   3) ลองลบการหารด้วย c ออก (ใช้ r,g,b ดิบ ๆ) --> ค่าจะเพี้ยนตามแสงไหม?

   คำถาม: ทำไมเราต้องหารด้วย c (clear) ก่อนคำนวณ Hue?
   ------------------------------------------------------------ */
