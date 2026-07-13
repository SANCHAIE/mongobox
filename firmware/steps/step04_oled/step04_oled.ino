/* ============================================================
   STEP 4 | มีจอแล้ว! แสดงค่าออกจอ (OLED SSD1306)
   ------------------------------------------------------------
   เป้าหมาย: เอาค่าจาก STEP 3 มาโชว์บนจอ ไม่ต้องพึ่งคอมพิวเตอร์
   สิ่งใหม่ที่ได้เรียน: การวาดบนจอ / พิกัด (x,y) / ขนาดตัวอักษร

   ไลบรารีเพิ่ม: Adafruit SSD1306 (+ Adafruit GFX)
   ============================================================ */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_BME280 bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool hasOLED = false;   // จอกว้าง 128 สูง 64 จุด


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

  // *** จอบางตัวเป็น 0x3D ไม่ใช่ 0x3C — เช็กทั้งสอง ***
  if      (i2cAlive(0x3C)) { hasOLED = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C); Serial.println("[OK] OLED ที่ 0x3C"); }
  else if (i2cAlive(0x3D)) { hasOLED = oled.begin(SSD1306_SWITCHCAPVCC, 0x3D); Serial.println("[OK] OLED ที่ 0x3D"); }
  else Serial.println("[X] ไม่พบจอ OLED — เช็กสาย (จอบางตัวใช้ address 0x3D!)");

  if      (i2cAlive(0x76)) bme.begin(0x76);
  else if (i2cAlive(0x77)) bme.begin(0x77);
  else Serial.println("[X] ไม่พบ BME280");

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("STEP 4 OK!");
  oled.display();          // <-- สำคัญ! ถ้าไม่เรียก display() จะไม่ขึ้นอะไรเลย
  delay(1500);
}

void loop() {
  if (!hasOLED) { Serial.println("(ไม่มีจอ — ข้ามการแสดงผล)"); delay(2000); return; }

  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();

  oled.clearDisplay();                 // ลบของเก่าก่อน

  oled.setTextSize(1);                 // ตัวเล็ก
  oled.setCursor(0, 0);                // ไปที่มุมซ้ายบน
  oled.println("MangoBox");

  oled.setTextSize(2);                 // ตัวใหญ่ขึ้น 2 เท่า
  oled.setCursor(0, 18);
  oled.print(temp, 1);
  oled.print(" C");

  oled.setCursor(0, 42);
  oled.print(hum, 0);
  oled.print(" %RH");

  oled.display();                      // ส่งภาพขึ้นจอจริง
  delay(1000);
}

/* ------------------------------------------------------------
   ผลที่ควรได้: จอโชว์อุณหภูมิตัวใหญ่ และความชื้นข้างล่าง

   ลองเล่นดู:
   1) ลบบรรทัด oled.display() ออก --> เกิดอะไรขึ้น? เพราะอะไร?
   2) ลบ oled.clearDisplay() ออก --> ตัวเลขจะทับกันมั่ว ลองดู!
   3) เปลี่ยน setCursor(0, 18) เป็น setCursor(30, 18) --> ขยับไปไหน?

   คำถาม: พิกัด (0,0) ของจออยู่มุมไหน? แล้ว y เพิ่มขึ้นไปทางไหน?
   ------------------------------------------------------------ */
