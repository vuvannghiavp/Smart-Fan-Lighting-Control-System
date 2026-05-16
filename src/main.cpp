#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================================================================
// CHỌN CHẾ ĐỘ Ở ĐÂY
// ================================================================
#define phat // Comment dòng này nếu nạp cho trạm THU COM 8
//#define thu  // Uncomment dòng này nếu nạp cho trạm THU COM 6

// ================================================================
// CẤU HÌNH CHUNG
// ================================================================
RF24 radio(9, 10); 
// const uint64_t addressTX = 0xE8E8F0F0E1LL; 
// const uint64_t addressRX = 0xE8E8F0F0E2LL; 
const byte addressTX[6] = "00001";
const byte addressRX[6] = "00001";
typedef struct {
  float temp;      
  float humid;     
  int16_t light;   
} SensorData;

typedef struct {
  bool lightRelay; 
  bool motorRelay; 
} ControlData;

// ================================================================
// PHẦN 1: TRẠM PHÁT (SENDER)
// ================================================================
#ifdef phat

#define DHTPIN 2
#define DHTTYPE DHT22
#define LDR_PIN A0
#define RELAY_LIGHT 5
#define IN1 6
#define IN2 7

DHT dht(DHTPIN, DHTTYPE);
SensorData sData = {0.0, 0.0, 0};
ControlData cData = {false, false};
uint32_t lastSend = 0;

void setup() {
  Serial.begin(9600);
  delay(2000); 
  Serial.println(F("\n--- KHOI DONG TRAM PHAT ---"));

  dht.begin();
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(RELAY_LIGHT, LOW);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  if (!radio.begin()) {
    Serial.println(F("[RF24] LOI PHAN CUNG!"));
    while (1); 
  }

  radio.setPALevel(RF24_PA_MIN);    
  radio.setDataRate(RF24_250KBPS);  
  radio.setChannel(115);            
  radio.setRetries(15, 15);

  radio.openWritingPipe(addressTX);
  radio.openReadingPipe(1, addressRX);
  radio.startListening(); 
  
  Serial.println(F("[RF24] San sang!"));
}

void loop() {
  // 1. Gửi dữ liệu cảm biến định kỳ (2 giây/lần)
  if (millis() - lastSend >= 2000) {
    lastSend = millis();
    sData.temp = dht.readTemperature();
    sData.humid = dht.readHumidity();
    sData.light = analogRead(LDR_PIN);

    if (!isnan(sData.temp) && !isnan(sData.humid)) {
      radio.stopListening(); 
      delay(5); // Chờ ổn định
      bool ok = radio.write(&sData, sizeof(SensorData));
      radio.startListening(); // Quay lại nghe lệnh ngay lập tức

      if (ok) {
        Serial.print(F("[TX OK] T:")); Serial.print(sData.temp);
        Serial.print(F(" H:")); Serial.println(sData.humid);
      } else {
        Serial.println(F("[TX FAIL] Ben Thu ko ACK"));
      }
    }
  }

  // 2. Luôn lắng nghe lệnh từ bên Thu
  if (radio.available()) {
    radio.read(&cData, sizeof(ControlData));
    
    // Thực thi điều khiển
    digitalWrite(RELAY_LIGHT, cData.lightRelay ? HIGH : LOW);
    if (cData.motorRelay) {
      digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    } else {
      digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
    }
    
    Serial.print(F("[RX CMD] Den:")); Serial.print(cData.lightRelay);
    Serial.print(F(" Quat:")); Serial.println(cData.motorRelay);
  }
}
#endif

// ================================================================
// PHẦN 2: TRẠM THU (RECEIVER)
// ================================================================
#ifdef thu

LiquidCrystal_I2C lcd(0x27, 16, 2);
#define BTN_LIGHT A2
#define BTN_MOTOR 4

const int   LIGHT_THRESHOLD = 400;  
const float TEMP_THRESHOLD  = 32.0; 
const float HUMID_THRESHOLD = 75.0; 

SensorData sData = {0.0, 0.0, 1023}; 
ControlData cData = {false, false}; 
uint32_t lastRecv = 0;
uint32_t lastCmdSend = 0;

bool lastAutoLight = false;
bool lastAutoMotor = false;

// Hàm gửi lệnh riêng biệt để gọi khi cần
void sendCommand() {
  radio.stopListening();
  delay(10); // Chờ chip chuyển trạng thái
  bool ok = radio.write(&cData, sizeof(ControlData));
  radio.startListening();
  
  if (ok) Serial.println(F("[TX CMD OK] Da gui lenh thanh cong"));
  else Serial.println(F("[TX CMD FAIL] Khong gui duoc lenh"));
}

void setup() {
  Serial.begin(9600);
  delay(2000);
  Serial.println(F("\n--- KHOI DONG TRAM THU ---"));

  pinMode(BTN_LIGHT, INPUT_PULLUP);
  pinMode(BTN_MOTOR, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print(F("Cho ket noi..."));

  if (!radio.begin()) {
    lcd.setCursor(0,1); lcd.print(F("RF24 LOI!"));
    while (1);
  }

  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(115);
  radio.setRetries(15, 15);

  radio.openReadingPipe(1, addressTX);
  radio.openWritingPipe(addressRX);
  radio.startListening();

  Serial.println(F("[RF24] Dang nghe..."));
}

void loop() {
  // 1. Nhận dữ liệu từ bên Phát
  if (radio.available()) {
    radio.read(&sData, sizeof(SensorData));
    lastRecv = millis();
    
    // Logic tự động
    bool currentAutoLight = (sData.light > LIGHT_THRESHOLD);
    if (currentAutoLight != lastAutoLight) {
      cData.lightRelay = currentAutoLight;
      lastAutoLight = currentAutoLight;
      sendCommand(); // Gửi lệnh ngay khi tự động thay đổi
    }

    bool currentAutoMotor = (sData.temp > TEMP_THRESHOLD || sData.humid > HUMID_THRESHOLD);
    if (currentAutoMotor != lastAutoMotor) {
      cData.motorRelay = currentAutoMotor;
      lastAutoMotor = currentAutoMotor;
      sendCommand(); // Gửi lệnh ngay khi tự động thay đổi
    }

    // Cập nhật LCD
    lcd.setCursor(0,0);
    lcd.print("T:"); lcd.print(sData.temp,1); lcd.print("C ");
    lcd.print("H:"); lcd.print(sData.humid,1); lcd.print("%  ");
    lcd.setCursor(0,1);
    lcd.print("L:"); lcd.print(sData.light);
    lcd.print(" D:"); lcd.print(cData.lightRelay ? "ON " : "OF ");
    lcd.print("M:"); lcd.print(cData.motorRelay ? "ON " : "OF ");
  }

  // 2. Xử lý nút bấm (Gửi lệnh NGAY LẬP TỨC khi nhấn)
  if (digitalRead(BTN_LIGHT) == LOW) {
    delay(200); // Debounce
    cData.lightRelay = !cData.lightRelay;
    Serial.print(F("Nut Bam Den -> ")); Serial.println(cData.lightRelay);
    sendCommand(); // Gửi ngay
  }
  
  if (digitalRead(BTN_MOTOR) == LOW) {
    delay(200); // Debounce
    cData.motorRelay = !cData.motorRelay;
    Serial.print(F("Nut Bam Quat -> ")); Serial.println(cData.motorRelay);
    sendCommand(); // Gửi ngay
  }

  // 3. Gửi lệnh định kỳ để duy trì trạng thái (mỗi 3 giây)
  if (millis() - lastCmdSend >= 3000) {
    lastCmdSend = millis();
    sendCommand();
  }

  // 4. Cảnh báo mất kết nối
  if (lastRecv > 0 && (millis() - lastRecv > 7000)) {
    lcd.setCursor(0,1);
    lcd.print(F("MAT KET NOI...  "));
  }
}
#endif