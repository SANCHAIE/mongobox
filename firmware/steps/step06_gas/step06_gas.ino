/* ============================================================
   STEP 6 | วัด "แก๊ส" ที่ผลไม้ปล่อยออกมา (SGP30)
   ------------------------------------------------------------
   เป้าหมาย: เห็นด้วยตาตัวเองว่า "ผลไม้สุกปล่อยแก๊สจริง ๆ"
   สิ่งใหม่ที่ได้เรียน: TVOC (ppb) / การอุ่นเครื่อง / การชดเชยความชื้น

   ไลบรารีเพิ่ม: Adafruit SGP30
   ** การทดลองสำคัญ: ปิดฝากล่องที่มีกล้วยสุกงอม แล้วดูตัวเลขไต่ขึ้น! **
   ============================================================ */

#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_SGP30 sgp;
Adafruit_BME280 bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

/* SGP30 อยากรู้ "ความชื้นสัมบูรณ์" เพื่อชดเชยค่าให้แม่นขึ้น
   เราคำนวณจาก temp/hum ที่ได้จาก BME280 ใน STEP 3 */
uint32_t absoluteHumidity(float tempC, float humidity) {
  const float ah = 216.7f * ((humidity / 100.0f) * 6.112f *
                   exp((17.62f * tempC) / (243.12f + tempC)) / (273.15f + tempC));
  return (uint32_t)(1000.0f * ah);      // mg/m^3
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

  if (i2cAlive(0x58)) { sgp.begin(); Serial.println("[OK] SGP30 พร้อม"); }
  else Serial.println("[X] ไม่พบ SGP30 (0x58) — เช็กไฟเลี้ยง! บางโมดูลต้องใช้ 5V ไม่ใช่ 3V3");

  if      (i2cAlive(0x76)) bme.begin(0x76);
  else if (i2cAlive(0x77)) bme.begin(0x77);
  else Serial.println("[!] ไม่พบ BME280 (จะไม่ชดเชยความชื้น)");

  Serial.println("STEP 6: วัดแก๊ส TVOC");
  Serial.println(">>> 15 วินาทีแรกจะขึ้น 0 ppb เป็นเรื่องปกติ (กำลังอุ่นเครื่อง) <<<");
  Serial.println("millis,TVOC_ppb,eCO2_ppm");
}

void loop() {
  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  if (isnan(temp)) temp = 25.0;
  if (isnan(hum))  hum  = 50.0;

  sgp.setHumidity(absoluteHumidity(temp, hum));   // ป้อนค่าชดเชย

  if (!sgp.IAQmeasure()) {
    Serial.println("[X] อ่านค่าไม่สำเร็จ");
    delay(1000);
    return;
  }

  int tvoc = sgp.TVOC;      // ppb  <-- ตัวนี้แหละที่เราสนใจ
  int eco2 = sgp.eCO2;      // ppm  (ค่า "ประมาณ" ไม่ใช่ CO2 จริง!)

  Serial.print(millis()); Serial.print(',');
  Serial.print(tvoc);     Serial.print(',');
  Serial.println(eco2);

  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print("GAS SENSOR");
  oled.setTextSize(2); oled.setCursor(0, 16);
  oled.print(tvoc); oled.setTextSize(1); oled.print(" ppb");
  oled.setTextSize(1); oled.setCursor(0, 40);
  oled.print("eCO2: "); oled.print(eco2); oled.print(" ppm");
  oled.setCursor(0, 52);
  oled.print(millis() < 16000 ? "warming up..." : "ready");
  oled.display();

  delay(1000);
}

/* ------------------------------------------------------------
   การทดลองที่ต้องทำ (นี่คือหัวใจ!):
   1) เปิดฝากล่อง วางไว้เฉย ๆ 2 นาที --> จด TVOC (นี่คือ "อากาศปกติ")
   2) ใส่กล้วยสุกงอมลงกล่อง ปิดฝา --> เปิด Serial Plotter ดูกราฟ
   3) รอ 10-20 นาที --> TVOC ขึ้นไหม? ขึ้นเร็วแค่ไหน?
   4) เปิดฝา --> ตัวเลขตกลงไหม? เพราะอะไร?

   ลองเปรียบเทียบ: กล่องมีผลไม้ VS กล่องเปล่า --> ต่างกันเท่าไร?

   คำถาม 1: ทำไมต้อง "ปิดฝา" ถึงจะวัดได้ชัด?
   คำถาม 2: SGP30 วัด "เอทิลีน" โดยตรงหรือเปล่า? (คำตอบ: ไม่! มันวัดสารระเหยรวม
            เราใช้มันเป็น "ตัวแทน" ของเอทิลีนเท่านั้น - ต้องเขียนข้อนี้ในรายงาน)
   ------------------------------------------------------------ */
