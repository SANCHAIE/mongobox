/* ============================================================
   STEP 9 | โหมดนักวิทยาศาสตร์: แยก "เหตุ" ออกจาก "ผล"
   ------------------------------------------------------------
   เป้าหมาย: เปลี่ยนจาก "เครื่องมือ" เป็น "การทดลอง"
   สิ่งใหม่ที่ได้เรียน: ตัวแปรต้น-ตาม / การบันทึกข้ามวัน (NVS) / ปุ่มกด

   *** แนวคิดสำคัญที่สุดของทั้งโปรเจกต์ ***
   ใน STEP 8 เรา "รวม" สี+แก๊ส เป็นคะแนนเดียว --> ใช้ตัดสินว่ากินได้ไหม (ดี)
   แต่ถ้าจะ "พิสูจน์ว่าเอทิลีนเร่งการสุก" --> ห้ามรวม! เพราะจะเอาเหตุปนกับผล

      เหตุ (ตัวแปรต้น) = แก๊ส TVOC       <-- สาเหตุ
      ผล  (ตัวแปรตาม) = อัตราการสุก      <-- ผลลัพธ์ (วัดจากสีที่เปลี่ยนต่อวัน)

   ** ปุ่ม BOOT มีอยู่บนบอร์ดแล้ว ไม่ต้องต่อปุ่มเพิ่ม **
   ============================================================ */

#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>          // <-- ใหม่! เก็บข้อมูลแบบไฟดับไม่หาย

#define BOX_ID  'B'               // <<< เปลี่ยนตามกล่องของกลุ่ม: A/B/C/D
#define I2C_SDA  8
#define I2C_SCL  9
#define FAN_PIN 10
#define BTN_PIN  0                // ปุ่ม BOOT บนบอร์ด

#define HUE_RAW  120.0
#define HUE_RIPE  55.0

Adafruit_SGP30 sgp;
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);
Adafruit_BME280 bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Preferences prefs;

int   gTVOC = 0;          // เหตุ
int   gColorRipe = 0;     // ผล (จากสีล้วน ๆ — ไม่ปนแก๊ส!)
float gRate = 0.0;        // อัตราการสุก %/วัน
int   dayCount = 0, day0Ripe = -1;
uint32_t hoursAccum = 0;
unsigned long lastHourTick = 0;

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
  if (y < 0) y = 0; if (y > 100) y = 100;
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
  pinMode(FAN_PIN, OUTPUT); digitalWrite(FAN_PIN, HIGH);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);   // ลดเป็น 50kHz — ทนสายยาว/หลวมได้ดีกว่า 100kHz มาก
  delay(500);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  sgp.begin(); tcs.begin();
  if (!bme.begin(0x76)) bme.begin(0x77);

  // โหลดข้อมูลที่เคยบันทึกไว้ (ถอดปลั๊กแล้วยังอยู่!)
  prefs.begin("mangobox", false);
  dayCount   = prefs.getInt("dayCount", 0);
  day0Ripe   = prefs.getInt("day0Ripe", -1);
  hoursAccum = prefs.getUInt("hours", 0);

  Serial.print("STEP 9: โหมด LAB | กล่อง "); Serial.println((char)BOX_ID);
  Serial.println("กดปุ่ม BOOT = บันทึกผลประจำวัน | พิมพ์ p = ดูตาราง | r = ล้าง");
  Serial.println("millis,Box,TVOC,Hue,ColorRipe,RatePerDay");
}

void loop() {
  handleButton();
  handleSerial();

  // นับชั่วโมงสะสม (เอาไปหารเป็น "ต่อวัน")
  if (dayCount > 0 && millis() - lastHourTick >= 3600000UL) {
    lastHourTick = millis();
    hoursAccum++;
    prefs.putUInt("hours", hoursAccum);
  }

  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  float temp = bme.readTemperature(), hum = bme.readHumidity();
  if (isnan(temp)) temp = 25.0;
  if (isnan(hum))  hum  = 50.0;

  // --- เหตุ: แก๊ส ---
  sgp.setHumidity(absoluteHumidity(temp, hum));
  if (sgp.IAQmeasure()) gTVOC = sgp.TVOC;

  // --- ผล: ความสุกจากสีล้วน ๆ (ไม่เอาแก๊สมาปน!) ---
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  float rN = 0, gN = 0, bN = 0;
  if (c > 0) { rN = (float)r / c; gN = (float)g / c; bN = (float)b / c; }
  float hue = rgbToHue(rN, gN, bN);
  gColorRipe = 100 - mapClamp(hue, HUE_RIPE, HUE_RAW);

  // --- อัตราการสุก = ความสุกที่เพิ่มขึ้น / จำนวนวันที่ผ่านไป ---
  if (day0Ripe >= 0 && hoursAccum >= 1) {
    float days = hoursAccum / 24.0;
    if (days > 0.04) gRate = (gColorRipe - day0Ripe) / days;
  }

  Serial.print(millis());      Serial.print(',');
  Serial.print((char)BOX_ID);  Serial.print(',');
  Serial.print(gTVOC);         Serial.print(',');   // เหตุ
  Serial.print(hue, 0);        Serial.print(',');
  Serial.print(gColorRipe);    Serial.print(',');   // ผล
  Serial.println(gRate, 2);

  // จอ: แยกซ้าย/ขวา ให้เห็นชัดว่าเป็นคนละค่า
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);  oled.print("LAB  BOX "); oled.print((char)BOX_ID);
  oled.setCursor(0, 12); oled.print("TVOC ppb");
  oled.setTextSize(2); oled.setCursor(0, 21); oled.print(gTVOC);
  oled.setTextSize(1); oled.setCursor(72, 12); oled.print("RIPE(col)");
  oled.setTextSize(2); oled.setCursor(72, 21); oled.print(gColorRipe); oled.print("%");
  oled.drawFastHLine(0, 40, 128, SSD1306_WHITE);
  oled.setTextSize(1); oled.setCursor(0, 45);
  if (dayCount == 0) oled.print("Press BOOT = save");
  else {
    oled.print("Rate: "); oled.print(gRate, 1); oled.print(" %/day");
    oled.setCursor(0, 55);
    oled.print("Day:"); oled.print(hoursAccum / 24.0, 1);
    oled.print("  Log:"); oled.print(dayCount);
  }
  oled.display();
}

// ---- ปุ่ม BOOT: กดสั้น = บันทึกประจำวัน ----
void handleButton() {
  static bool prev = HIGH;
  bool now = digitalRead(BTN_PIN);
  if (prev == HIGH && now == LOW) {
    delay(50);                                  // กันเด้ง
    if (digitalRead(BTN_PIN) == LOW) saveDay();
  }
  prev = now;
}

void saveDay() {
  if (dayCount == 0) {                          // ครั้งแรก = ตั้งจุดอ้างอิง
    day0Ripe = gColorRipe;
    hoursAccum = 0;
    prefs.putInt("day0Ripe", day0Ripe);
    prefs.putUInt("hours", 0);
    lastHourTick = millis();
  }
  char k1[12], k2[12], k3[12];
  sprintf(k1, "d%dR", dayCount); sprintf(k2, "d%dE", dayCount); sprintf(k3, "d%dH", dayCount);
  prefs.putInt(k1, gColorRipe);
  prefs.putInt(k2, gTVOC);
  prefs.putUInt(k3, hoursAccum);
  dayCount++;
  prefs.putInt("dayCount", dayCount);

  Serial.print("\n>>> บันทึกครั้งที่ "); Serial.print(dayCount);
  Serial.print(" | TVOC="); Serial.print(gTVOC);
  Serial.print(" | ความสุก="); Serial.print(gColorRipe); Serial.println("%\n");

  oled.clearDisplay(); oled.setTextSize(2); oled.setCursor(0, 20);
  oled.print("SAVED #"); oled.println(dayCount);
  oled.display(); delay(1500);
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'p' || c == 'P') {
    Serial.println("\nครั้งที่,ชั่วโมง,วัน,TVOC,ColorRipe");
    for (int i = 0; i < dayCount; i++) {
      char k1[12], k2[12], k3[12];
      sprintf(k1, "d%dR", i); sprintf(k2, "d%dE", i); sprintf(k3, "d%dH", i);
      Serial.print(i + 1); Serial.print(',');
      Serial.print(prefs.getUInt(k3, 0)); Serial.print(',');
      Serial.print(prefs.getUInt(k3, 0) / 24.0, 2); Serial.print(',');
      Serial.print(prefs.getInt(k2, 0)); Serial.print(',');
      Serial.println(prefs.getInt(k1, 0));
    }
    Serial.print("อัตราการสุก = "); Serial.print(gRate, 1); Serial.println(" %/วัน\n");
  }
  else if (c == 'r' || c == 'R') {
    prefs.clear();
    dayCount = 0; day0Ripe = -1; hoursAccum = 0; gRate = 0;
    Serial.println(">>> ล้างข้อมูลแล้ว");
  }
}

/* ------------------------------------------------------------
   วิธีใช้ในการทดลองจริง:
   - 4 กลุ่ม 4 กล่อง: A(+กล้วยสุก)  B(control)  C(+ด่างทับทิม)  D(เปล่า)
   - เปลี่ยน BOX_ID ให้ตรงกับกล่องตัวเอง
   - วันนี้: กดปุ่ม BOOT 1 ครั้ง (= จุดเริ่มต้น)
   - อีก 3-5 วัน: กดวันละครั้ง เวลาเดิม
   - วันสุดท้าย: พิมพ์ p --> เอาตัวเลขไปพล็อตกราฟ

   *** สิ่งที่ต้องเห็น ***
   ถ้า TVOC:      A > B > C
   และ อัตราการสุก: A > B > C   ไปในทางเดียวกัน
   = พิสูจน์ได้ว่า "เอทิลีนเร่งการสุก" จริง!

   คำถาม 1: ทำไม STEP 8 รวมสี+แก๊สได้ แต่ STEP 9 ห้ามรวม?
   คำถาม 2: กล่อง D ไม่มีผลไม้เลย มีไว้ทำไม?
   คำถาม 3: ทำไมต้องกดปุ่ม "เวลาเดิม" ทุกวัน?
   ------------------------------------------------------------ */
