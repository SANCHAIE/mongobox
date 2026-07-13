/* ============================================================================
   MangoBox | STEP 0 — MOCK MODE (โหมดจำลอง)  *** ไม่ต้องมีเซ็นเซอร์เลย ***
   ----------------------------------------------------------------------------
   ใช้เมื่อ:
     1) ยังไม่ได้เซ็นเซอร์ แต่อยากทดสอบ "ตรรกะ" ทั้งระบบก่อน
     2) วันเวิร์กช็อป: กลุ่มไหนเซ็นเซอร์เสีย/ต่อไม่ติด --> เปิด MOCK แล้วเรียนต่อได้
     3) ซ้อมนำเสนอ / เดโมกรรมการ โดยไม่ต้องรอมะม่วงสุกจริง

   สิ่งที่ทดสอบได้ในโหมดนี้ (โดยไม่มีฮาร์ดแวร์ใด ๆ นอกจากบอร์ด):
     [x] สูตรคำนวณ (Hue -> %ความสุก, TVOC -> คะแนนแก๊ส, การถ่วงน้ำหนัก)
     [x] โหมด METER / LAB และการสลับโหมด
     [x] ปุ่ม BOOT (กดสั้น = บันทึก, กดค้าง 2 วิ = สลับโหมด)
     [x] การบันทึกข้ามวันลง NVS (ถอดปลั๊กแล้วข้อมูลไม่หาย)
     [x] ไฟ RGB บอกสถานะ (เขียว=ดิบ, เหลือง=พอดี, ส้ม=งอม)
     [x] CSV ออก Serial (เอาไป Serial Plotter / Excel ได้)
     [x] จอ OLED (ถ้ามี — โปรแกรมหาให้อัตโนมัติ ถ้าไม่มีก็ข้ามไป ไม่ค้าง)
     [x] การทดลอง 4 กล่อง A/B/C/D (จำลองว่ากล่องไหนสุกเร็ว/ช้า)

   บอร์ด: ESP32-S3 (N16R8) | Board = "ESP32S3 Dev Module"
          USB CDC On Boot = Enabled | Flash 16MB | PSRAM = OPI PSRAM
   ============================================================================ */

// ---------- (1) ตั้งค่า --------------------------------------------------------
#define MOCK        1        // 1 = จำลอง (ไม่ต้องมีเซ็นเซอร์) | 0 = ใช้เซ็นเซอร์จริง
#define BOX_ID     'B'       // <<< 'A' 'B' 'C' หรือ 'D' — ลองเปลี่ยนดูว่าสุกเร็ว/ช้าต่างกัน!
#define SIM_SECONDS_PER_DAY  20    // จำลอง: 20 วินาทีจริง = 1 วันในโลกจำลอง

#define RGB_PIN    48        // RGB LED บนบอร์ด (บอร์ดโคลนอยู่ขา 48 ไม่ใช่ 38)
#define BRIGHT     10        // ความสว่าง RGB (1-255) — 10 = นวลตา, 60 = แยงตา

// ---------- (2) เกณฑ์คาลิเบรต (เหมือนของจริงทุกอย่าง) --------------------------
#define HUE_RAW    120.0     // เขียว = ดิบ = 0%
#define HUE_RIPE    55.0     // เหลือง = สุก = 100%
#define TVOC_BASE    0.0     // อากาศสะอาด (ppb)
#define TVOC_HIGH  500.0     // แก๊สเยอะ = สุกจัด (ppb)
#define W_COLOR     0.60     // น้ำหนักสี  (โหมด METER)
#define W_GAS       0.40     // น้ำหนักแก๊ส (โหมด METER)

// ---------- (3) ขา ------------------------------------------------------------
#define I2C_SDA  8
#define I2C_SCL  9
#define FAN_PIN 10
#define BTN_PIN  0           // ปุ่ม BOOT (มีบนบอร์ดแล้ว)

#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Preferences prefs;
bool hasOLED = false;        // โปรแกรมจะหาจอเองว่ามีไหม

// ---------- (4) โหมด ----------------------------------------------------------
enum Mode { MODE_METER = 0, MODE_LAB = 1 };
Mode gMode = MODE_METER;

// ---------- (5) ค่าที่ "วัดได้" (จริงหรือจำลอง) --------------------------------
float gTemp = 28.0, gHum = 60.0;
int   gTVOC = 0;             // เหตุ  (ppb)
float gHue  = 120.0;
int   gColorRipe = 0;        // ผล   (จากสีล้วน ๆ)
int   gGasScore  = 0;
int   gMeter     = 0;        // คะแนนรวม (METER)
float gRate      = 0.0;      // อัตราการสุก %/วัน
String gStage    = "-";

// บันทึกข้ามวัน (NVS)
int      dayCount = 0, day0Ripe = -1;
uint32_t hoursAccum = 0;
unsigned long lastHourTick = 0;

// ============================================================================
//  ฟังก์ชันช่วย (เหมือนโค้ดจริงทุกตัว)
// ============================================================================
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
const char* modeName() { return (gMode == MODE_METER) ? "METER" : "LAB"; }

// ============================================================================
//  *** หัวใจของ MOCK: จำลองมะม่วงที่ค่อย ๆ สุก ***
//  แต่ละกล่องสุกด้วย "ความเร็ว" ต่างกัน ตามปริมาณเอทิลีน
// ============================================================================
#if MOCK
void readSensorsMOCK() {
  // เวลาจำลอง (หน่วย "วัน")
  float simDay = millis() / 1000.0 / SIM_SECONDS_PER_DAY;

  // --- กล่องไหนสุกเร็วแค่ไหน? (นี่คือสมมติฐานของการทดลอง!) ---
  float speed;      // ความเร็วการสุก
  float ethBoost;   // เอทิลีนเสริมจากในกล่อง
  switch (BOX_ID) {
    case 'A': speed = 1.8; ethBoost = 220; break;   // + กล้วยสุกงอม -> เอทิลีนสูง สุกเร็วสุด
    case 'B': speed = 1.0; ethBoost = 100; break;   // control
    case 'C': speed = 0.4; ethBoost =  25; break;   // + ด่างทับทิม (ดูดซับ) -> สุกช้าสุด
    default:  speed = 0.0; ethBoost =   0; break;   // 'D' กล่องเปล่า -> ไม่มีอะไรเกิดขึ้น
  }

  // --- สี: เขียว(120) ค่อย ๆ ไหลไปเหลือง(50) ตามเวลา x ความเร็ว ---
  float progress = simDay * speed / 7.0;            // สุกเต็มที่ราว 7 วัน (ที่ speed=1)
  if (progress > 1.0) progress = 1.0;
  gHue = 120.0 - (70.0 * progress);                 // 120 -> 50
  gHue += random(-15, 16) / 10.0;                   // ใส่ noise เล็กน้อยให้เหมือนจริง

  // --- แก๊ส: ยิ่งสุก ยิ่งปล่อยมาก (โตแบบเร่ง) + เอทิลีนจากในกล่อง ---
  float tv = ethBoost * progress + 300.0 * progress * progress;
  tv += random(-8, 9);                              // noise
  if (tv < 0) tv = 0;
  gTVOC = (int)tv;
  if (millis() < 16000) gTVOC = 0;                  // จำลองช่วงอุ่นเครื่อง 15 วิ ของ SGP30

  // --- อุณหภูมิ/ความชื้น: แกว่งเบา ๆ (กล่องปิดชื้นขึ้นเรื่อย ๆ) ---
  gTemp = 28.0 + random(-10, 11) / 10.0;
  gHum  = 60.0 + progress * 15.0 + random(-20, 21) / 10.0;
}
#endif

// ============================================================================
//  ไฟ RGB บอกสถานะความสุก (ของที่ใช้ได้จริงตอนนี้!)
// ============================================================================
void showRGB() {
  if (millis() < 16000) {                            // อุ่นเครื่อง = น้ำเงินกะพริบ
    int on = (millis() / 400) % 2;
    neopixelWrite(RGB_PIN, 0, 0, on ? BRIGHT : 0);
    return;
  }
  int score = (gMode == MODE_METER) ? gMeter : gColorRipe;
  if      (score < 30) neopixelWrite(RGB_PIN, 0, BRIGHT, 0);                    // เขียว = ดิบ
  else if (score < 60) neopixelWrite(RGB_PIN, BRIGHT * 0.6, BRIGHT, 0);         // เขียวอมเหลือง
  else if (score < 85) neopixelWrite(RGB_PIN, BRIGHT, BRIGHT * 0.7, 0);         // เหลือง = พอดี
  else                 neopixelWrite(RGB_PIN, BRIGHT, BRIGHT * 0.2, 0);         // ส้ม = งอม
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);
  pinMode(BTN_PIN, INPUT_PULLUP);
  randomSeed(esp_random());
  delay(600);

  Serial.println();
  Serial.println("==============================================");
#if MOCK
  Serial.println("   MangoBox — MOCK MODE (โหมดจำลอง)");
  Serial.println("   *** ไม่ใช้เซ็นเซอร์จริง — ค่าทั้งหมดเป็นค่าจำลอง ***");
#else
  Serial.println("   MangoBox — โหมดเซ็นเซอร์จริง");
#endif
  Serial.print  ("   กล่อง: "); Serial.print((char)BOX_ID);
  switch (BOX_ID) {
    case 'A': Serial.println("  (+ กล้วยสุกงอม -> เอทิลีนสูง คาดว่าสุกเร็วสุด)"); break;
    case 'B': Serial.println("  (control -> สุกปกติ)"); break;
    case 'C': Serial.println("  (+ ด่างทับทิม -> ดูดซับ คาดว่าสุกช้าสุด)"); break;
    default:  Serial.println("  (กล่องเปล่า -> ไม่มีการสุก)"); break;
  }
  Serial.print  ("   จำลอง: "); Serial.print(SIM_SECONDS_PER_DAY);
  Serial.println(" วินาทีจริง = 1 วันจำลอง");
  Serial.println("==============================================");

  // -- หาจอ OLED เอง: ถ้าไม่มีก็ไม่ค้าง ทำงานต่อได้ --
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) {
    hasOLED = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  }
  Serial.println(hasOLED ? "[OK] เจอจอ OLED — จะแสดงผลบนจอด้วย"
                         : "[--] ไม่มีจอ OLED — ใช้ Serial + RGB แทน (ไม่เป็นไร)");
  if (hasOLED) oled.setTextColor(SSD1306_WHITE);

  // -- โหลดค่าที่เคยบันทึก --
  prefs.begin("mangobox", false);
  gMode      = (Mode)prefs.getInt("mode", MODE_METER);
  dayCount   = prefs.getInt("dayCount", 0);
  day0Ripe   = prefs.getInt("day0Ripe", -1);
  hoursAccum = prefs.getUInt("hours", 0);
  Serial.print("โหมดปัจจุบัน: "); Serial.println(modeName());
  if (dayCount > 0) {
    Serial.print("[NVS] มีบันทึกเดิม "); Serial.print(dayCount);
    Serial.println(" ครั้ง (ถอดปลั๊กแล้วข้อมูลยังอยู่!)");
  }

  Serial.println();
  Serial.println("--- ปุ่ม BOOT: กดสั้น = บันทึกประจำวัน (โหมด LAB) | กดค้าง 2 วิ = สลับโหมด ---");
  Serial.println("--- Serial:  m = สลับโหมด | p = ดูตารางบันทึก | r = ล้างข้อมูล ---");
  Serial.println();
  Serial.println("millis,Mode,Box,Temp,Hum,TVOC_ppb,Hue,ColorRipe,MeterScore,RatePerDay");
  delay(500);
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  handleButton();
  handleSerialCmd();
  showRGB();

  // นับชั่วโมงสะสม -- ในโหมด MOCK ใช้เวลาจำลอง (เร็วขึ้นมาก จะได้เห็นผลทันที)
#if MOCK
  static float lastSimHour = 0;
  float simHour = millis() / 1000.0 / SIM_SECONDS_PER_DAY * 24.0;
  if (dayCount > 0 && simHour - lastSimHour >= 1.0) {
    lastSimHour = simHour;
    hoursAccum++;
    prefs.putUInt("hours", hoursAccum);
  }
#else
  if (dayCount > 0 && millis() - lastHourTick >= 3600000UL) {
    lastHourTick = millis();
    hoursAccum++;
    prefs.putUInt("hours", hoursAccum);
  }
#endif

  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  // ---- อ่านค่า (จำลอง หรือ ของจริง) ----
#if MOCK
  readSensorsMOCK();
#else
  // ที่นี่คือจุดที่จะใส่โค้ดอ่านเซ็นเซอร์จริง (STEP 3-6)
#endif

  // ---- คำนวณ (ส่วนนี้เหมือนของจริง 100% — นี่แหละที่เรากำลังทดสอบ) ----
  gColorRipe = 100 - mapClamp(gHue, HUE_RIPE, HUE_RAW);
  gGasScore  = mapClamp((float)gTVOC, TVOC_BASE, TVOC_HIGH);
  gMeter     = (int)(W_COLOR * gColorRipe + W_GAS * gGasScore);

  if (day0Ripe >= 0 && hoursAccum >= 1) {
    float days = hoursAccum / 24.0;
    if (days > 0.04) gRate = (gColorRipe - day0Ripe) / days;
  }

  if      (gMeter < 30) gStage = "DIB (raw)";
  else if (gMeter < 60) gStage = "KUENG-SUK";
  else if (gMeter < 85) gStage = "SUK POR-DEE";
  else                  gStage = "SUK NGOM";

  printCSV();
  if (hasOLED) { (gMode == MODE_METER) ? showMeterOLED() : showLabOLED(); }
}

// ============================================================================
//  ปุ่ม BOOT: กดสั้น = บันทึก | กดค้าง 2 วิ = สลับโหมด
// ============================================================================
void handleButton() {
  static bool prev = HIGH;
  static unsigned long tDown = 0;
  static bool longDone = false;
  bool now = digitalRead(BTN_PIN);

  if (prev == HIGH && now == LOW) { tDown = millis(); longDone = false; }
  if (now == LOW && !longDone && millis() - tDown > 2000) { toggleMode(); longDone = true; }
  if (prev == LOW && now == HIGH) {
    if (!longDone && millis() - tDown > 50) {
      if (gMode == MODE_LAB) saveDailyRecord();
      else Serial.println(">>> (โหมด METER) กดค้าง 2 วิ เพื่อสลับไปโหมด LAB");
    }
  }
  prev = now;
}

void toggleMode() {
  gMode = (gMode == MODE_METER) ? MODE_LAB : MODE_METER;
  prefs.putInt("mode", (int)gMode);
  Serial.print("\n>>> สลับเป็นโหมด "); Serial.println(modeName());
  Serial.println();

  // ไฟขาวกะพริบ 2 ที = ยืนยันว่าสลับแล้ว
  for (int i = 0; i < 2; i++) {
    neopixelWrite(RGB_PIN, BRIGHT, BRIGHT, BRIGHT); delay(120);
    neopixelWrite(RGB_PIN, 0, 0, 0);                delay(120);
  }
}

void saveDailyRecord() {
  if (dayCount == 0) {
    day0Ripe   = gColorRipe;
    hoursAccum = 0;
    prefs.putInt("day0Ripe", day0Ripe);
    prefs.putUInt("hours", 0);
    lastHourTick = millis();
  }
  char k1[12], k2[12], k3[12];
  sprintf(k1, "d%dR", dayCount);
  sprintf(k2, "d%dE", dayCount);
  sprintf(k3, "d%dH", dayCount);
  prefs.putInt(k1, gColorRipe);
  prefs.putInt(k2, gTVOC);
  prefs.putUInt(k3, hoursAccum);
  dayCount++;
  prefs.putInt("dayCount", dayCount);

  Serial.println();
  Serial.print(">>> บันทึกครั้งที่ "); Serial.print(dayCount);
  Serial.print(" | กล่อง "); Serial.print((char)BOX_ID);
  Serial.print(" | TVOC = "); Serial.print(gTVOC); Serial.print(" ppb");
  Serial.print(" | ความสุก(สี) = "); Serial.print(gColorRipe); Serial.println(" %");
  Serial.println();

  for (int i = 0; i < 3; i++) {                       // ไฟกะพริบ 3 ที = บันทึกแล้ว
    neopixelWrite(RGB_PIN, 0, 0, BRIGHT); delay(100);
    neopixelWrite(RGB_PIN, 0, 0, 0);      delay(100);
  }
}

// ============================================================================
//  คำสั่ง Serial
// ============================================================================
void handleSerialCmd() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'm' || c == 'M') toggleMode();
  else if (c == 'p' || c == 'P') {
    Serial.println();
    Serial.print("=== ตารางบันทึก | กล่อง "); Serial.print((char)BOX_ID); Serial.println(" ===");
    Serial.println("ครั้งที่,ชั่วโมงสะสม,วันที่ผ่านไป,TVOC_ppb,ColorRipe");
    for (int i = 0; i < dayCount; i++) {
      char k1[12], k2[12], k3[12];
      sprintf(k1, "d%dR", i); sprintf(k2, "d%dE", i); sprintf(k3, "d%dH", i);
      uint32_t hh = prefs.getUInt(k3, 0);
      Serial.print(i + 1);              Serial.print(',');
      Serial.print(hh);                 Serial.print(',');
      Serial.print(hh / 24.0, 2);       Serial.print(',');
      Serial.print(prefs.getInt(k2, 0)); Serial.print(',');
      Serial.println(prefs.getInt(k1, 0));
    }
    Serial.print("อัตราการสุกล่าสุด = "); Serial.print(gRate, 1);
    Serial.println(" % ต่อวัน");
    Serial.println();
  }
  else if (c == 'r' || c == 'R') {
    prefs.remove("dayCount"); prefs.remove("day0Ripe"); prefs.remove("hours");
    dayCount = 0; day0Ripe = -1; hoursAccum = 0; gRate = 0;
    Serial.println(">>> ล้างข้อมูลบันทึกแล้ว (โหมดยังคงเดิม)");
  }
}

// ============================================================================
//  CSV
// ============================================================================
void printCSV() {
  Serial.print(millis());      Serial.print(',');
  Serial.print(modeName());    Serial.print(',');
  Serial.print((char)BOX_ID);  Serial.print(',');
  Serial.print(gTemp, 1);      Serial.print(',');
  Serial.print(gHum, 1);       Serial.print(',');
  Serial.print(gTVOC);         Serial.print(',');   // เหตุ
  Serial.print(gHue, 0);       Serial.print(',');
  Serial.print(gColorRipe);    Serial.print(',');   // ผล
  Serial.print(gMeter);        Serial.print(',');   // คะแนนรวม
  Serial.println(gRate, 2);
}

// ============================================================================
//  จอ OLED (ถ้ามี)
// ============================================================================
void showMeterOLED() {
  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print("METER");
#if MOCK
  oled.setCursor(48, 0); oled.print("[MOCK]");
#endif
  oled.setCursor(96, 0);
  oled.print((int)gTemp); oled.print("C");

  oled.setTextSize(3); oled.setCursor(0, 14);
  oled.print(gMeter); oled.print("%");
  oled.drawRect(0, 40, 128, 9, SSD1306_WHITE);
  oled.fillRect(0, 40, map(gMeter, 0, 100, 0, 128), 9, SSD1306_WHITE);
  oled.setTextSize(1); oled.setCursor(0, 54);
  oled.print(gStage);
  oled.display();
}

void showLabOLED() {
  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print("LAB BOX "); oled.print((char)BOX_ID);
#if MOCK
  oled.setCursor(78, 0); oled.print("[MOCK]");
#endif
  oled.setCursor(0, 12);  oled.print("TVOC ppb");
  oled.setTextSize(2);    oled.setCursor(0, 21); oled.print(gTVOC);
  oled.setTextSize(1);    oled.setCursor(72, 12); oled.print("RIPE(col)");
  oled.setTextSize(2);    oled.setCursor(72, 21); oled.print(gColorRipe); oled.print("%");
  oled.drawFastHLine(0, 40, 128, SSD1306_WHITE);
  oled.setTextSize(1);    oled.setCursor(0, 45);
  if (dayCount == 0) oled.print("Press BOOT = save");
  else {
    oled.print("Rate: "); oled.print(gRate, 1); oled.print(" %/day");
    oled.setCursor(0, 55);
    oled.print("Day:"); oled.print(hoursAccum / 24.0, 1);
    oled.print("  Log:"); oled.print(dayCount);
  }
  oled.display();
}

/* ============================================================================
   *** วิธีทดสอบ (ทำตามลำดับ) ***

   [T1] เปิด Serial Monitor (115200) — ควรเห็น CSV ไหลออกมาทุก 1 วินาที
        และค่า TVOC / ColorRipe ค่อย ๆ เพิ่มขึ้นเรื่อย ๆ (มะม่วงกำลังสุก!)
        ไฟ RGB จะไล่จาก เขียว -> เหลือง -> ส้ม ภายในราว 2 นาที

   [T2] ทดสอบปุ่ม BOOT — กดค้าง 2 วินาที
        --> ไฟขาวกะพริบ 2 ที + Serial ขึ้น ">>> สลับเป็นโหมด LAB"

   [T3] ทดสอบการบันทึก — (อยู่โหมด LAB) กดปุ่ม BOOT สั้น ๆ 1 ครั้ง
        --> ไฟน้ำเงินกะพริบ 3 ที + Serial ขึ้น ">>> บันทึกครั้งที่ 1"
        กดอีก 2-3 ครั้ง เว้นระยะสัก 20-30 วินาที

   [T4] ดูตาราง — พิมพ์ p ใน Serial Monitor แล้วกด Enter
        --> ควรได้ตารางบันทึกทั้งหมด + อัตราการสุก %/วัน

   [T5] *** ทดสอบว่า NVS ใช้ได้จริง *** — ถอดสาย USB ออกเลย แล้วเสียบใหม่
        --> Serial ต้องขึ้น "[NVS] มีบันทึกเดิม N ครั้ง" และยังอยู่โหมด LAB
        (ถ้าข้อมูลหาย = NVS มีปัญหา ต้องบอกผม)

   [T6] ล้างข้อมูล — พิมพ์ r แล้ว Enter

   [T7] *** ทดลอง 4 กล่อง (สนุกสุด!) ***
        เปลี่ยน BOX_ID เป็น 'A' แล้วอัปโหลด --> จับเวลาว่ากี่วินาทีถึง 100%
        เปลี่ยนเป็น 'C' แล้วอัปโหลด --> เทียบกัน
        --> A ควรสุกเร็วกว่า C อย่างชัดเจน (เพราะเอทิลีนต่างกัน)
        นี่คือผลที่นักเรียนควรได้จากการทดลองจริงเป๊ะ ๆ

   [T8] Serial Plotter — เปิด Tools > Serial Plotter จะเห็นกราฟ TVOC และ
        ColorRipe ไต่ขึ้นพร้อมกัน (เหตุ -> ผล)

   ----------------------------------------------------------------------------
   *** พอเซ็นเซอร์มาถึง ***
   เปลี่ยนแค่บรรทัดเดียว:  #define MOCK 0
   แล้วเอาโค้ดอ่านเซ็นเซอร์จาก STEP 3-6 มาใส่ในช่อง #else ของ loop()
   ส่วนที่เหลือ (สูตร/โหมด/ปุ่ม/NVS/RGB/OLED) ไม่ต้องแก้อะไรเลย
   ============================================================================ */
