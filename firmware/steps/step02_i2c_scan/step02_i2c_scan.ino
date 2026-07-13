/* ============================================================
   STEP 2 | เซ็นเซอร์ต่อติดหรือยัง? (I2C Scanner)
   ------------------------------------------------------------
   เป้าหมาย: ตรวจว่าเซ็นเซอร์ทุกตัวต่อสายถูกและบอร์ดเห็นมันจริง
   สิ่งใหม่ที่ได้เรียน: บัส I2C / ที่อยู่ (address) / Wire library

   ** ต่อสายให้ครบก่อน: VCC->3V3, GND->GND, SDA->GPIO8, SCL->GPIO9 **
   ============================================================ */

#include <Wire.h>

#define I2C_SDA 8
#define I2C_SCL 9


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
  Wire.setClock(50000);   // ลดเป็น 50kHz — ทนสายยาว/หลวมได้ดีกว่า 100kHz มาก   // เปิดบัส I2C บอกว่าใช้ขาไหน
  delay(800);
  Serial.println("\nSTEP 2: กำลังค้นหาอุปกรณ์บนสาย I2C...");
}

void loop() {
  int found = 0;

  // ไล่ "เคาะประตู" ทีละที่อยู่ ตั้งแต่ 1 ถึง 126
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {      // 0 = มีคนรับสาย!
      Serial.print("  พบอุปกรณ์ที่ 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);

      // แปลว่าตัวไหน
      if      (addr == 0x29) Serial.print("  <- TCS34725 (เซ็นเซอร์สี)");
      else if (addr == 0x3C) Serial.print("  <- OLED (จอ)");
      else if (addr == 0x58) Serial.print("  <- SGP30 (เซ็นเซอร์แก๊ส)");
      else if (addr == 0x76 || addr == 0x77) Serial.print("  <- BME280 (อุณหภูมิ/ชื้น)");
      Serial.println();
      found++;
    }
  }

  if (found == 0) Serial.println("  ไม่พบอะไรเลย! เช็กสาย SDA/SCL/ไฟเลี้ยง");
  else { Serial.print("  รวม "); Serial.print(found); Serial.println(" ตัว"); }

  Serial.println("---");
  delay(3000);
}

/* ------------------------------------------------------------
   ผลที่ควรได้: เจอครบ 4 ตัว -> 0x29, 0x3C, 0x58, 0x76(หรือ 0x77)

   ถ้าไม่เจอตัวไหน:
   - เช็ก VCC/GND ว่าไม่สลับกัน
   - เช็ก SDA เข้า GPIO8 และ SCL เข้า GPIO9 จริงไหม
   - ลองขยับสายบนเบรดบอร์ด (สายหลวมเป็นสาเหตุอันดับ 1)

   คำถาม: ทำไมอุปกรณ์ 4 ตัวใช้สายร่วมกันได้โดยไม่สับสน?
   ------------------------------------------------------------ */
