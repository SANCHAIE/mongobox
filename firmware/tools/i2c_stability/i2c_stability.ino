/* ============================================================
   เครื่องมือไล่บั๊ก | I2C STABILITY TEST
   ------------------------------------------------------------
   ใช้เมื่อ: เซ็นเซอร์ "เจอบ้างไม่เจอบ้าง" (อาการสายหลวม)
   สแกน 20 รอบ แล้วบอกว่าแต่ละตัวเจอกี่รอบ

   20/20 = สายแน่น ใช้ได้
   บางรอบ = สายหลวม! (ปัญหาอันดับ 1 ของเบรดบอร์ด)
   0/20   = ไม่ได้ต่อ / ไม่มีไฟเลี้ยง / เซ็นเซอร์เสีย
   ============================================================ */

#include <Wire.h>
#define I2C_SDA 8
#define I2C_SCL 9
#define RGB_PIN 48
#define BRIGHT  10

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(50000);        // 50kHz — ทนสายยาว/หลวมดีกว่า 100kHz มาก
  delay(1000);
  Serial.println("\n=== I2C STABILITY TEST (สแกน 20 รอบ) ===");
  Serial.println("(ใช้ 50kHz เพื่อความทนทานต่อสายยาว)\n");
}

void loop() {
  static int round_ = 0;
  static int c29 = 0, c3C = 0, c3D = 0, c58 = 0, c76 = 0, c77 = 0;
  round_++;

  Serial.print("รอบ "); Serial.print(round_); Serial.print(": ");
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("0x");
      if (a < 16) Serial.print("0");
      Serial.print(a, HEX); Serial.print(" ");
      if (a == 0x29) c29++;
      if (a == 0x3C) c3C++;
      if (a == 0x3D) c3D++;       // จอบางตัวใช้ 0x3D!
      if (a == 0x58) c58++;
      if (a == 0x76) c76++;
      if (a == 0x77) c77++;
    }
  }
  Serial.println();

  if (round_ >= 20) {
    Serial.println("\n=== สรุป: เจอกี่รอบจาก 20 ===");
    Serial.print("  0x29 TCS34725 (สี)   : "); Serial.print(c29); Serial.println("/20");
    Serial.print("  0x3C OLED (จอ)       : "); Serial.print(c3C); Serial.println("/20");
    Serial.print("  0x3D OLED (แบบที่ 2) : "); Serial.print(c3D); Serial.println("/20");
    Serial.print("  0x58 SGP30 (แก๊ส)    : "); Serial.print(c58); Serial.println("/20");
    Serial.print("  0x76 BME280          : "); Serial.print(c76); Serial.println("/20");
    Serial.print("  0x77 BME280 (แบบ 2)  : "); Serial.print(c77); Serial.println("/20");

    int perfect = (c29 == 20) + ((c3C == 20) || (c3D == 20)) + (c58 == 20)
                + ((c76 == 20) || (c77 == 20));
    Serial.print("\n  นิ่งสนิท (20/20): "); Serial.print(perfect); Serial.println(" / 4 ตัว");

    if (perfect == 4) {
      Serial.println("  >>> ทุกตัวนิ่ง พร้อมทำ STEP ต่อไป <<<");
      neopixelWrite(RGB_PIN, 0, BRIGHT, 0);              // เขียว
    } else {
      Serial.println("\n  *** วิธีแก้ตามอาการ ***");
      Serial.println("  - เจอบางรอบ  = สายหลวม! กดสายให้แน่น / ใช้สายสั้นลง / ลดจุดต่อ");
      Serial.println("  - 0/20       = เช็กไฟเลี้ยง VCC ก่อนเลย (ไม่มีไฟ = ไม่ตอบ)");
      Serial.println("  - จอไม่เจอ   = ลองดูว่าเป็น 0x3D ไหม (จอบางรุ่นใช้ address นี้)");
      Serial.println("  - SGP30 ไม่เจอ = บางโมดูลต้องใช้ 5V ไม่ใช่ 3V3!");
      neopixelWrite(RGB_PIN, BRIGHT, BRIGHT * 0.4, 0);   // ส้ม
    }
    Serial.println("=========================================");
    while (1) delay(1000);
  }
  delay(300);
}
