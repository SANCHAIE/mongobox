/* ============================================================
   STEP 10 | ดูค่าจากมือถือ (WiFi Dashboard)
   ------------------------------------------------------------
   เป้าหมาย: เปิดดูค่าเซ็นเซอร์บนมือถือ ไม่ต้องเสียบคอม
   สิ่งใหม่ที่ได้เรียน: WiFi / web server / JSON / บอร์ดเป็น "เว็บไซต์"

   *** ใช้ตอนนำเสนอ: ให้กรรมการเปิดดูค่าสด ๆ บนมือถือตัวเอง! ***
   ============================================================ */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

const char* WIFI_SSID = "ชื่อ_WiFi_ของคุณ";     // <<< แก้ตรงนี้
const char* WIFI_PASS = "รหัสผ่าน";              // <<< แก้ตรงนี้

#define I2C_SDA  8
#define I2C_SCL  9
#define FAN_PIN 10
#define HUE_RAW  120.0
#define HUE_RIPE  55.0

WebServer server(80);          // บอร์ดกลายเป็นเว็บเซิร์ฟเวอร์ ที่พอร์ต 80
Adafruit_SGP30 sgp;
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);
Adafruit_BME280 bme;

int gTVOC = 0, gRipe = 0;
float gTemp = 25, gHum = 50;

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

// หน้าเว็บ (HTML) — บอร์ดส่งข้อความนี้ให้มือถือ
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>MangoBox</title><style>"
    "body{font-family:sans-serif;background:#14281d;color:#fff;text-align:center;padding:30px}"
    "h1{color:#f4c430}.big{font-size:60px;font-weight:800;color:#f4c430}"
    ".card{background:#1e3527;border-radius:14px;padding:16px;margin:12px auto;max-width:340px}"
    ".l{font-size:13px;color:#9fb89f}</style></head><body>"
    "<h1>MangoBox</h1>"
    "<div class='card'><div class='l'>แก๊สที่ระเหย (TVOC)</div>"
    "<div class='big'><span id='tvoc'>--</span></div><div class='l'>ppb</div></div>"
    "<div class='card'><div class='l'>ความสุกจากสี</div>"
    "<div class='big'><span id='ripe'>--</span>%</div></div>"
    "<div class='card'><span id='t'>--</span> C &nbsp;|&nbsp; <span id='h'>--</span> %RH</div>"
    "<script>"
    "async function u(){let d=await(await fetch('/data')).json();"
    "tvoc.textContent=d.tvoc;ripe.textContent=d.ripe;"
    "t.textContent=d.temp;h.textContent=d.hum;}"
    "setInterval(u,1000);u();</script></body></html>";
  server.send(200, "text/html", html);
}

// ส่งข้อมูลเป็น JSON ให้หน้าเว็บดึงไปอัปเดตทุก 1 วินาที
void handleData() {
  String j = "{";
  j += "\"tvoc\":" + String(gTVOC) + ",";
  j += "\"ripe\":" + String(gRipe) + ",";
  j += "\"temp\":" + String(gTemp, 1) + ",";
  j += "\"hum\":"  + String(gHum, 1) + "}";
  server.send(200, "application/json", j);
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
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);   // ลดเป็น 50kHz — ทนสายยาว/หลวมได้ดีกว่า 100kHz มาก
  delay(500);
  sgp.begin(); tcs.begin();
  if (!bme.begin(0x76)) bme.begin(0x77);

  Serial.print("กำลังเชื่อม WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) { delay(500); Serial.print("."); t++; }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n[OK] เปิดมือถือเข้าเว็บนี้:  http://");
    Serial.println(WiFi.localIP());     // <<< เอา IP นี้ไปเปิดในมือถือ!
  } else {
    Serial.println("\n[X] ต่อ WiFi ไม่ได้ — เช็กชื่อ/รหัส (ต้องเป็น WiFi 2.4GHz)");
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  server.handleClient();          // <-- ต้องเรียกบ่อย ๆ ไม่งั้นเว็บค้าง

  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  gTemp = bme.readTemperature();
  gHum  = bme.readHumidity();
  if (isnan(gTemp)) gTemp = 25.0;
  if (isnan(gHum))  gHum  = 50.0;

  sgp.setHumidity(absoluteHumidity(gTemp, gHum));
  if (sgp.IAQmeasure()) gTVOC = sgp.TVOC;

  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  float rN = 0, gN = 0, bN = 0;
  if (c > 0) { rN = (float)r / c; gN = (float)g / c; bN = (float)b / c; }
  gRipe = 100 - mapClamp(rgbToHue(rN, gN, bN), HUE_RIPE, HUE_RAW);
}

/* ------------------------------------------------------------
   วิธีใช้:
   1) แก้ WIFI_SSID และ WIFI_PASS ให้ตรงกับ WiFi ที่ใช้
      ** ต้องเป็น WiFi 2.4GHz เท่านั้น ESP32 ต่อ 5GHz ไม่ได้! **
   2) อัปโหลด แล้วดู Serial Monitor จะได้เลข IP เช่น 192.168.1.42
   3) เอามือถือ (ต่อ WiFi วงเดียวกัน) เปิดเบราว์เซอร์ พิมพ์ http://192.168.1.42

   คำถาม 1: ทำไมมือถือต้องอยู่ WiFi วงเดียวกับบอร์ด?
   คำถาม 2: /data ส่งอะไรกลับมา? ลองเปิด http://<IP>/data ในเบราว์เซอร์ดู
   คำถาม 3: ถ้าลบ server.handleClient() ออกจาก loop() จะเกิดอะไรขึ้น?
   ------------------------------------------------------------ */
