/* ============================================================================
   MangoBox  |  ESP32-S3 + SGP30  |  2 โหมดในบอร์ดเดียว
   Workshop: "การประดิษฐ์วงจรบอร์ดเซ็นเซอร์เพื่อดูดซับแก๊สเอทิลีน"
   คณะวิศวกรรมศาสตร์ ม.บูรพา x โรงเรียนพลูตาหลวงวิทยา  |  13 ก.ค. 2569
   วิทยากร: ผศ.ดร.สัญชัย เอียดปราบ (ESCE, Burapha Engineering)
   ----------------------------------------------------------------------------
   *** สองโหมด — ข้อมูลชุดเดียวกัน ต่างกันแค่ "ชั้นแสดงผล" ***

   [1] โหมด METER (ใช้งานจริง) : "มะม่วงลูกนี้กินได้หรือยัง?"
       -> รวมสี + แก๊ส เป็นคะแนนความสุกเดียว 0-100%  บอก ดิบ/พอดี/งอม

   [2] โหมด LAB   (ทำโครงงาน) : "เอทิลีนเร่งการสุกจริงไหม?"
       -> แยก เหตุ (แก๊ส TVOC) ออกจาก ผล (อัตราการสุกจากสี) เด็ดขาด

   สลับโหมด: กดปุ่ม BOOT "ค้าง 2 วินาที"  (ปุ่มมีบนบอร์ดแล้ว ไม่ต้องต่อเพิ่ม)
   บันทึกประจำวัน (โหมด LAB): กดปุ่ม BOOT "สั้น ๆ" 1 ครั้ง
   ** ห้ามกด BOOT ค้างตอนกด RESET/เสียบ USB (จะเข้าโหมดอัปโหลด) **
   ----------------------------------------------------------------------------
   *** เซ็นเซอร์แก๊ส = SGP30 (I2C 0x58) ***
   - ให้ค่า TVOC (ppb) และ eCO2 (ppm)  ** ต่างจาก SGP40 ที่ให้ VOC Index **
   - ต้อง "อุ่นเครื่อง": 15 วินาทีแรกค่าจะเป็น 0 ppb / 400 ppm เป็นเรื่องปกติ
   - ต้องมี BASELINE ถึงจะแม่น -> โปรแกรมนี้ save/restore baseline ให้อัตโนมัติ
     (เก็บลง NVS ทุก 1 ชั่วโมง; ครั้งแรกควรเปิดทิ้งไว้ ~12 ชม. เพื่อ burn-in)
   - eCO2 เป็นค่า "ประมาณ" จากไฮโดรเจน ไม่ใช่ CO2 จริง -> ห้ามเคลมเกินในรายงาน
   - SGP30 วัด "สารระเหยรวม" ไม่ใช่เอทิลีนบริสุทธิ์ -> เป็นพร็อกซี่ของเอทิลีน
     สิ่งที่เชื่อถือได้คือ "การเปรียบเทียบระหว่างกล่อง" ไม่ใช่ตัวเลขสัมบูรณ์
   ----------------------------------------------------------------------------
   บอร์ด: ESP32-S3 (N16R8) | Board = "ESP32S3 Dev Module"
          USB CDC On Boot = Enabled | Flash 16MB | PSRAM = OPI PSRAM
   ขา: SDA=GPIO8, SCL=GPIO9, พัดลม=GPIO10, ปุ่ม BOOT=GPIO0
   ============================================================================ */

// ---------- (1) ตั้งค่าของกล่องนี้ ---------------------------------------------
#define BOX_ID   'B'          // <<< 'A' 'B' 'C' หรือ 'D' (ใช้ในโหมด LAB)
#define USE_WIFI 1            // 0 = ปิด WiFi, 1 = เปิด Dashboard
const char* WIFI_SSID = "MangoBox";      // <<< แก้เป็นชื่อ WiFi ของคุณ
const char* WIFI_PASS = "12345678";      // <<< แก้เป็นรหัส WiFi ของคุณ

// ---------- (2) เกณฑ์ที่ "คาลิเบรตได้" (ปรับจากข้อมูลจริงในโหมด LAB) -----------
#define HUE_RAW      120.0    // สีเขียว (ดิบ)   = 0%
#define HUE_RIPE      55.0    // สีเหลือง (สุก)  = 100%
#define TVOC_BASE      0.0    // TVOC อากาศสะอาด (ppb) = 0%
#define TVOC_HIGH    500.0    // TVOC ตอนผลไม้สุกจัด (ppb) = 100%   <-- ปรับตามจริง
#define W_COLOR       0.60    // น้ำหนักสี  (โหมด METER)
#define W_GAS         0.40    // น้ำหนักแก๊ส (โหมด METER)

// ---------- (3) ไลบรารี -------------------------------------------------------
#include <Wire.h>
#include <Adafruit_SGP30.h>          // <<< SGP30 (ไม่ใช่ SGP40)
#include <Adafruit_TCS34725.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

#if USE_WIFI
  #include <WiFi.h>
  #include <WebServer.h>
  WebServer server(80);
#endif

// ---------- (4) ขา ------------------------------------------------------------
#define I2C_SDA      8
#define I2C_SCL      9
#define FAN_PIN     10
#define BTN_PIN      0        // ปุ่ม BOOT (มีบนบอร์ดแล้ว)
#define RGB_PIN     48        // RGB LED บนบอร์ด (บอร์ดนี้ขา 48 ไม่ใช่ 38)
#define BRIGHT      10        // ความสว่าง RGB (60 = แยงตา)

// ---------- (5) จอ + เซ็นเซอร์ -------------------------------------------------
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool hasOLED = false;
Adafruit_SGP30 sgp;                                     // 0x58  <<<
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS,   // 0x29
                      TCS34725_GAIN_4X);
Adafruit_BME280 bme;                                    // 0x76 / 0x77
Preferences prefs;

// ---------- (6) โหมด ----------------------------------------------------------
enum Mode { MODE_METER = 0, MODE_LAB = 1 };
Mode gMode = MODE_METER;

// ---------- (7) ค่าที่วัดได้ ---------------------------------------------------
float gTempC = 25.0, gHum = 50.0;
int   gTVOC  = 0;             // เหตุ: TVOC (ppb) จาก SGP30
int   gECO2  = 400;           // ค่าประมาณ eCO2 (ppm) — ของแถม
float gHue   = 120.0;
int   gColorRipe = 0;         // ผล : ความสุกจาก "สี" ล้วน ๆ
int   gGasScore  = 0;         // คะแนนจากแก๊ส (ใช้รวมในโหมด METER)
int   gMeter     = 0;         // คะแนนรวม 0-100 (โหมด METER)
float gRate      = 0.0;       // อัตราการสุก %/วัน (โหมด LAB)
String gStage    = "-";
bool  gWarmedUp  = false;     // ผ่านช่วงอุ่นเครื่อง 15 วิ แล้วหรือยัง

// บันทึกข้ามวัน (NVS)
int      dayCount   = 0;
int      day0Ripe   = -1;
uint32_t hoursAccum = 0;

// baseline ของ SGP30
unsigned long lastBaselineSave = 0;
bool  gBaselineLoaded = false;

// ============================================================================
//  ฟังก์ชันช่วย
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
int mapClamp(float x, float inMin, float inMax) {
  float y = (x - inMin) / (inMax - inMin) * 100.0;
  if (y < 0) y = 0;
  if (y > 100) y = 100;
  return (int)y;
}
const char* modeName() { return (gMode == MODE_METER) ? "METER" : "LAB"; }

/* SGP30 ต้องการ "absolute humidity" (g/m3 -> ส่งเป็น mg/m3)
   เพื่อชดเชยค่าแก๊สให้แม่นขึ้น — คำนวณจาก temp/hum ของ BME280 */
uint32_t absoluteHumidity(float tempC, float humidity) {
  const float ah = 216.7f * ((humidity / 100.0f) * 6.112f *
                   exp((17.62f * tempC) / (243.12f + tempC)) / (273.15f + tempC));
  return (uint32_t)(1000.0f * ah);          // mg/m^3
}


/* === i2cAlive(): เช็ก ACK จริง — begin() ของบางไลบรารีโกหกได้! ===
   (SSD1306.begin() คืน true ทั้งที่ไม่ได้ต่อจอ — เจอมากับตัว) */
bool i2cAlive(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}


// ไฟ RGB บอกสถานะความสุก — ใช้ได้แม้ไม่มีจอ
void showRGB() {
  if (!gWarmedUp) {
    int on = (millis() / 400) % 2;
    neopixelWrite(RGB_PIN, 0, 0, on ? BRIGHT : 0);   // น้ำเงินกะพริบ = อุ่นเครื่อง
    return;
  }
  int sc = (gMode == MODE_METER) ? gMeter : gColorRipe;
  if      (sc < 30) neopixelWrite(RGB_PIN, 0, BRIGHT, 0);
  else if (sc < 60) neopixelWrite(RGB_PIN, BRIGHT * 0.6, BRIGHT, 0);
  else if (sc < 85) neopixelWrite(RGB_PIN, BRIGHT, BRIGHT * 0.7, 0);
  else              neopixelWrite(RGB_PIN, BRIGHT, BRIGHT * 0.2, 0);
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);
  pinMode(BTN_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);        // 50kHz — ทนสายยาว/หลวมได้ดีกว่า

  // *** เช็ก ACK จริงก่อนเสมอ — อย่าเชื่อ begin() ***
  if      (i2cAlive(0x3C)) { hasOLED = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C); }
  else if (i2cAlive(0x3D)) { hasOLED = oled.begin(SSD1306_SWITCHCAPVCC, 0x3D); }
  Serial.println(hasOLED ? F("[OK] จอ OLED พร้อม")
                         : F("[X] ไม่พบจอ OLED — ลองเช็ก 0x3D / สายหลวม (ทำงานต่อได้)"));
  if (hasOLED) { oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); }

  Serial.println();
  Serial.println(F("=== MangoBox | SGP30 | 2 โหมด (METER / LAB) ==="));

  // -- SGP30 --
  if (!i2cAlive(0x58)) Serial.println(F("[X] ไม่พบ SGP30 (0x58) — บางโมดูลต้องใช้ไฟ 5V!"));
  else if (!sgp.begin()) Serial.println(F("[X] SGP30 init ไม่ผ่าน"));
  else {
    Serial.print(F("[OK] SGP30 พร้อม  serial #"));
    Serial.print(sgp.serialnumber[0], HEX);
    Serial.print(sgp.serialnumber[1], HEX);
    Serial.println(sgp.serialnumber[2], HEX);
  }
  if (i2cAlive(0x29)) { tcs.begin(); Serial.println(F("[OK] TCS34725 พร้อม")); }
  else Serial.println(F("[X] ไม่พบ TCS34725 (0x29)"));

  if      (i2cAlive(0x76)) { bme.begin(0x76); Serial.println(F("[OK] BME280 ที่ 0x76")); }
  else if (i2cAlive(0x77)) { bme.begin(0x77); Serial.println(F("[OK] BME280 ที่ 0x77")); }
  else Serial.println(F("[X] ไม่พบ BME280 (0x76/0x77)"));

  // -- โหลดค่าที่เก็บไว้ --
  prefs.begin("mangobox", false);
  gMode      = (Mode)prefs.getInt("mode", MODE_METER);
  dayCount   = prefs.getInt("dayCount", 0);
  day0Ripe   = prefs.getInt("day0Ripe", -1);
  hoursAccum = prefs.getUInt("hours", 0);

  // -- คืนค่า baseline ของ SGP30 (สำคัญมาก! ทำให้ค่า ppb แม่นตั้งแต่เปิดเครื่อง) --
  uint16_t bTVOC = prefs.getUShort("bTVOC", 0);
  uint16_t bECO2 = prefs.getUShort("bECO2", 0);
  if (bTVOC != 0 && bECO2 != 0) {
    sgp.setIAQBaseline(bECO2, bTVOC);
    gBaselineLoaded = true;
    Serial.print(F("[OK] คืนค่า baseline: eCO2=0x")); Serial.print(bECO2, HEX);
    Serial.print(F(" TVOC=0x")); Serial.println(bTVOC, HEX);
  } else {
    Serial.println(F("[!] ยังไม่มี baseline — ครั้งแรกควรเปิดทิ้งไว้ ~12 ชม. (burn-in)"));
  }

  Serial.print(F("โหมดปัจจุบัน: ")); Serial.println(modeName());

#if USE_WIFI
  startWiFi();
#endif

  Serial.println();
  Serial.println(F("--- ปุ่ม BOOT: กดสั้น = บันทึกประจำวัน (LAB) | กดค้าง 2 วิ = สลับโหมด ---"));
  Serial.println(F("--- Serial: m = สลับโหมด | p = ดูตาราง | b = ดู baseline | r = ล้างข้อมูล ---"));
  Serial.println();
  Serial.println(F("millis,Mode,Box,Temp,Hum,TVOC_ppb,eCO2_ppm,Hue,ColorRipe,MeterScore,RatePerDay"));
  delay(600);
}

// ============================================================================
//  LOOP
// ============================================================================
unsigned long lastSample = 0, lastHourTick = 0;

void loop() {
  handleButton();
  showRGB();
  handleSerialCmd();
#if USE_WIFI
  server.handleClient();
#endif

  if (dayCount > 0 && millis() - lastHourTick >= 3600000UL) {
    lastHourTick = millis();
    hoursAccum++;
    prefs.putUInt("hours", hoursAccum);
  }

  // -- เก็บ baseline ของ SGP30 ทุก 1 ชั่วโมง --
  if (millis() - lastBaselineSave >= 3600000UL) {
    lastBaselineSave = millis();
    saveBaseline();
  }

  if (millis() - lastSample < 1000) return;
  lastSample = millis();

  // ---- อุณหภูมิ / ความชื้น ----
  gTempC = bme.readTemperature();
  gHum   = bme.readHumidity();
  if (isnan(gTempC)) gTempC = 25.0;
  if (isnan(gHum))   gHum   = 50.0;

  // ---- ป้อนความชื้นสัมบูรณ์ให้ SGP30 ชดเชย แล้วอ่านค่า ----
  sgp.setHumidity(absoluteHumidity(gTempC, gHum));
  if (sgp.IAQmeasure()) {
    gTVOC = sgp.TVOC;          // ppb   <-- เหตุ
    gECO2 = sgp.eCO2;          // ppm   (ค่าประมาณ)
  }
  // 15 วินาทีแรก SGP30 จะคืน 0 ppb / 400 ppm เสมอ (ช่วงอุ่นเครื่อง)
  if (!gWarmedUp && millis() > 16000) gWarmedUp = true;

  // ---- สี ----
  uint16_t rc, gc, bc, cc;
  tcs.getRawData(&rc, &gc, &bc, &cc);
  float rN = 0, gN = 0, bN = 0;
  if (cc > 0) { rN = (float)rc / cc; gN = (float)gc / cc; bN = (float)bc / cc; }
  gHue = rgbToHue(rN, gN, bN);

  // ---- คำนวณทั้งสองแบบไว้เสมอ (โหมด = แค่จะโชว์อะไร) ----
  gColorRipe = 100 - mapClamp(gHue, HUE_RIPE, HUE_RAW);          // ผล (สีล้วน ๆ)
  gGasScore  = mapClamp((float)gTVOC, TVOC_BASE, TVOC_HIGH);
  gMeter     = (int)(W_COLOR * gColorRipe + W_GAS * gGasScore);  // รวม (METER)

  if (day0Ripe >= 0 && hoursAccum >= 1) {
    float days = hoursAccum / 24.0;
    if (days > 0.04) gRate = (gColorRipe - day0Ripe) / days;
  }

  if      (gMeter < 30) gStage = "DIB (raw)";
  else if (gMeter < 60) gStage = "KUENG-SUK";
  else if (gMeter < 85) gStage = "SUK POR-DEE";
  else                  gStage = "SUK NGOM";

  if (gMode == MODE_METER) showMeter();
  else                     showLab();
  printCSV();
}

// ============================================================================
//  baseline ของ SGP30 -> เก็บลง NVS (ไฟดับไม่หาย)
// ============================================================================
void saveBaseline() {
  uint16_t eco2_base, tvoc_base;
  if (!sgp.getIAQBaseline(&eco2_base, &tvoc_base)) {
    Serial.println(F("[!] อ่าน baseline ไม่สำเร็จ"));
    return;
  }
  prefs.putUShort("bECO2", eco2_base);
  prefs.putUShort("bTVOC", tvoc_base);
  gBaselineLoaded = true;
  Serial.print(F("[baseline saved] eCO2=0x")); Serial.print(eco2_base, HEX);
  Serial.print(F(" TVOC=0x")); Serial.println(tvoc_base, HEX);
}

// ============================================================================
//  ปุ่ม BOOT
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
      else {
        oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 20);
        oled.println(F("Hold 2s = switch"));
        oled.println(F("to LAB mode"));
        oled.display(); delay(900);
      }
    }
  }
  prev = now;
}

void toggleMode() {
  gMode = (gMode == MODE_METER) ? MODE_LAB : MODE_METER;
  prefs.putInt("mode", (int)gMode);
  Serial.print(F(">>> สลับเป็นโหมด ")); Serial.println(modeName());

  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 8);
  oled.println(F("SWITCH MODE ->"));
  oled.setTextSize(2); oled.setCursor(0, 26);
  oled.println(modeName());
  oled.setTextSize(1); oled.setCursor(0, 52);
  oled.print((gMode == MODE_METER) ? F("use: check fruit") : F("use: experiment"));
  oled.display();
  delay(1400);
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
  saveBaseline();                          // เก็บ baseline ไปพร้อมกันเลย

  Serial.println();
  Serial.print(F(">>> บันทึกครั้งที่ ")); Serial.print(dayCount);
  Serial.print(F(" | กล่อง ")); Serial.print((char)BOX_ID);
  Serial.print(F(" | TVOC = ")); Serial.print(gTVOC); Serial.print(F(" ppb"));
  Serial.print(F(" | ความสุก(สี) = ")); Serial.print(gColorRipe); Serial.println(F("%"));
  Serial.println();

  oled.clearDisplay(); oled.setTextSize(2); oled.setCursor(0, 12);
  oled.print(F("SAVED #")); oled.println(dayCount);
  oled.setTextSize(1); oled.setCursor(0, 44);
  oled.print(F("TVOC:")); oled.print(gTVOC);
  oled.print(F("  Ripe:")); oled.print(gColorRipe); oled.print(F("%"));
  oled.display();
  delay(1500);
}

// ============================================================================
//  คำสั่ง Serial
// ============================================================================
void handleSerialCmd() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'm' || c == 'M') toggleMode();
  else if (c == 'b' || c == 'B') saveBaseline();
  else if (c == 'p' || c == 'P') {
    Serial.println();
    Serial.print(F("=== ตารางบันทึกประจำวัน | กล่อง ")); Serial.print((char)BOX_ID); Serial.println(F(" ==="));
    Serial.println(F("ครั้งที่,ชั่วโมงสะสม,วันที่ผ่านไป,TVOC_ppb,ColorRipe"));
    for (int i = 0; i < dayCount; i++) {
      char k1[12], k2[12], k3[12];
      sprintf(k1, "d%dR", i); sprintf(k2, "d%dE", i); sprintf(k3, "d%dH", i);
      int R = prefs.getInt(k1, 0), E = prefs.getInt(k2, 0);
      uint32_t Hh = prefs.getUInt(k3, 0);
      Serial.print(i + 1);        Serial.print(',');
      Serial.print(Hh);           Serial.print(',');
      Serial.print(Hh / 24.0, 2); Serial.print(',');
      Serial.print(E);            Serial.print(',');
      Serial.println(R);
    }
    Serial.print(F("อัตราการสุกล่าสุด = ")); Serial.print(gRate, 1);
    Serial.println(F(" % ต่อวัน"));
    Serial.println();
  }
  else if (c == 'r' || c == 'R') {
    prefs.remove("dayCount"); prefs.remove("day0Ripe"); prefs.remove("hours");
    dayCount = 0; day0Ripe = -1; hoursAccum = 0; gRate = 0;
    Serial.println(F(">>> ล้างข้อมูลบันทึกแล้ว (baseline และโหมดยังอยู่)"));
  }
}

// ============================================================================
//  จอ: โหมด METER
// ============================================================================
void showMeter() {
  if (!hasOLED) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(F("METER"));
  oled.setCursor(62, 0);
  oled.print((int)gTempC); oled.print((char)247); oled.print(F("C "));
  oled.print((int)gHum);   oled.print(F("%"));

  if (!gWarmedUp) {                       // ช่วงอุ่นเครื่อง 15 วิ
    oled.setTextSize(1); oled.setCursor(0, 26);
    oled.println(F("SGP30 warming up..."));
    oled.print(F("(15s)"));
    oled.display();
    return;
  }

  oled.setTextSize(3);
  oled.setCursor(0, 14);
  oled.print(gMeter); oled.print(F("%"));

  oled.drawRect(0, 40, 128, 9, SSD1306_WHITE);
  oled.fillRect(0, 40, map(gMeter, 0, 100, 0, 128), 9, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 54);
  oled.print(gStage);
  oled.display();
}

// ============================================================================
//  จอ: โหมด LAB — แยก "เหตุ" กับ "ผล"
// ============================================================================
void showLab() {
  if (!hasOLED) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(F("LAB  BOX ")); oled.print((char)BOX_ID);
  oled.setCursor(74, 0);
  oled.print((int)gTempC); oled.print((char)247); oled.print(F("C"));

  oled.setCursor(0, 12);  oled.print(F("TVOC ppb"));
  oled.setTextSize(2);    oled.setCursor(0, 21);  oled.print(gTVOC);

  oled.setTextSize(1);    oled.setCursor(72, 12); oled.print(F("RIPE(col)"));
  oled.setTextSize(2);    oled.setCursor(72, 21); oled.print(gColorRipe); oled.print(F("%"));

  oled.drawFastHLine(0, 40, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 45);
  if (!gWarmedUp)       oled.print(F("warming up (15s)"));
  else if (dayCount == 0) oled.print(F("Press BOOT = save"));
  else {
    oled.print(F("Rate: ")); oled.print(gRate, 1); oled.print(F(" %/day"));
    oled.setCursor(0, 55);
    oled.print(F("Day:")); oled.print(hoursAccum / 24.0, 1);
    oled.print(F("  Log:")); oled.print(dayCount);
  }
  oled.display();
}

// ============================================================================
//  CSV — ทุกคอลัมน์แยกเสมอ
// ============================================================================
void printCSV() {
  Serial.print(millis());       Serial.print(',');
  Serial.print(modeName());     Serial.print(',');
  Serial.print((char)BOX_ID);   Serial.print(',');
  Serial.print(gTempC, 1);      Serial.print(',');
  Serial.print(gHum, 1);        Serial.print(',');
  Serial.print(gTVOC);          Serial.print(',');   // เหตุ (ppb)
  Serial.print(gECO2);          Serial.print(',');   // eCO2 (ประมาณ)
  Serial.print(gHue, 0);        Serial.print(',');
  Serial.print(gColorRipe);     Serial.print(',');   // ผล (สีล้วน)
  Serial.print(gMeter);         Serial.print(',');   // คะแนนรวม
  Serial.println(gRate, 2);                          // อัตราการสุก
}

// ============================================================================
//  WiFi Dashboard
// ============================================================================
#if USE_WIFI
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("กำลังเชื่อม WiFi"));
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) { delay(500); Serial.print('.'); t++; }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("\n[OK] เปิดดูที่  http://"));
    Serial.println(WiFi.localIP());
    oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
    oled.println(F("WiFi OK:"));
    oled.println(WiFi.localIP());
    oled.display(); delay(1500);
  } else {
    Serial.println(F("\n[!] ต่อ WiFi ไม่ได้ — ทำงานต่อโดยไม่มี Dashboard"));
  }
  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void handleData() {
  String j = "{";
  j += "\"mode\":\"" + String(modeName())   + "\",";
  j += "\"box\":\""  + String((char)BOX_ID) + "\",";
  j += "\"meter\":"  + String(gMeter)       + ",";
  j += "\"stage\":\"" + gStage + "\",";
  j += "\"tvoc\":"   + String(gTVOC)        + ",";
  j += "\"eco2\":"   + String(gECO2)        + ",";
  j += "\"ripe\":"   + String(gColorRipe)   + ",";
  j += "\"rate\":"   + String(gRate, 1)     + ",";
  j += "\"days\":"   + String(hoursAccum / 24.0, 1) + ",";
  j += "\"temp\":"   + String(gTempC, 1)    + ",";
  j += "\"hum\":"    + String(gHum, 1)      + "}";
  server.send(200, "application/json", j);
}

void handleRoot() {
  String html = F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>MangoBox</title><style>"
    "body{font-family:sans-serif;background:#14281d;color:#fff;text-align:center;margin:0;padding:20px}"
    "h1{color:#f4c430;margin:0}.tag{display:inline-block;background:#2e7d32;color:#fff;"
    "border-radius:20px;padding:3px 14px;font-size:12px;margin:8px 0 16px}"
    ".panel{background:#1e3527;border-radius:16px;padding:16px;margin:10px auto;max-width:440px}"
    ".lbl{font-size:12px;color:#9fb89f;letter-spacing:1px}"
    ".big{font-size:50px;font-weight:800;color:#f4c430;line-height:1.15}"
    ".unit{font-size:18px;color:#9fb89f}"
    ".stage{font-size:20px;color:#e57e25;font-weight:700}"
    ".cause{border-left:5px solid #e57e25}.effect{border-left:5px solid #7cb342}"
    ".bar{height:16px;background:#2c4a34;border-radius:8px;overflow:hidden;margin-top:8px}"
    ".fill{height:100%;background:linear-gradient(90deg,#7cb342,#f4c430,#e57e25);transition:.5s}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px;max-width:440px;margin:10px auto}"
    ".card{background:#1e3527;border-radius:12px;padding:12px}"
    ".v{font-size:22px;font-weight:700;color:#f4c430}.l{font-size:11px;color:#9fb89f}"
    "h3{color:#9fb89f;font-size:13px;margin:18px 0 4px;letter-spacing:1px}</style></head><body>"
    "<h1>MangoBox <span id='box'>-</span></h1>"
    "<div class='tag'>โหมด: <span id='mode'>-</span></div>"

    "<h3>เครื่องวัดความสุก (METER)</h3>"
    "<div class='panel'><div class='big'><span id='meter'>--</span>%</div>"
    "<div class='stage' id='stage'>-</div>"
    "<div class='bar'><div class='fill' id='f' style='width:0%'></div></div></div>"

    "<h3>ข้อมูลการทดลอง (LAB)</h3>"
    "<div class='panel cause'><div class='lbl'>เหตุ &middot; แก๊สที่ระเหย (พร็อกซี่ของเอทิลีน)</div>"
    "<div class='big'><span id='tvoc'>--</span><span class='unit'> ppb</span></div></div>"
    "<div class='panel effect'><div class='lbl'>ผล &middot; ความสุกจากสี</div>"
    "<div class='big'><span id='ripe'>--</span>%</div></div>"
    "<div class='row'>"
    "<div class='card'><div class='v'><span id='rate'>--</span></div><div class='l'>อัตราการสุก (%/วัน)</div></div>"
    "<div class='card'><div class='v'><span id='days'>--</span></div><div class='l'>ผ่านไป (วัน)</div></div>"
    "<div class='card'><div class='v'><span id='eco2'>--</span></div><div class='l'>eCO2 (ppm, ค่าประมาณ)</div></div>"
    "<div class='card'><div class='v'><span id='t'>--</span>&deg;C</div><div class='l'>อุณหภูมิ</div></div>"
    "</div><script>"
    "async function u(){try{let d=await(await fetch('/data')).json();"
    "box.textContent=d.box;mode.textContent=d.mode;meter.textContent=d.meter;"
    "stage.textContent=d.stage;f.style.width=d.meter+'%';tvoc.textContent=d.tvoc;"
    "ripe.textContent=d.ripe;rate.textContent=d.rate;days.textContent=d.days;"
    "eco2.textContent=d.eco2;t.textContent=d.temp;}catch(e){}}"
    "setInterval(u,1000);u();</script></body></html>");
  server.send(200, "text/html", html);
}
#endif
