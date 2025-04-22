
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

//Định nghĩa các chân cho module RC522
#define RST_PIN1 2
#define SS_PIN1  5
#define RST_PIN2 4
#define SS_PIN2  14
#define SERVO_PIN 27
#define SERVO_PIN1 32

 Servo servo0;
 Servo servo1;
const char* ssid = "TT";
const char* password = "111222000";
 //Thông tin WiFi
//const char* ssid = "huylatui";
//const char* password = "0bietgihet";

// URL và IP của API để gửi dữ liệu
const char* serverIP = "172.20.10.2";
const int serverPort = 3000;

// Khởi tạo hai đối tượng MFRC522
MFRC522 rfid1(SS_PIN1, RST_PIN1);
MFRC522 rfid2(SS_PIN2, RST_PIN2);

WiFiClient client;

// Semaphore để điều phối việc truy cập WiFi
SemaphoreHandle_t wifiSemaphore;

// Khởi tạo đối tượng LCD
LiquidCrystal_I2C lcd(0x27, 20, 4); // Địa chỉ I2C mặc định là 0x27

void setup() {
  Serial.begin(115200);
  SPI.begin();

  rfid1.PCD_Init();
  rfid2.PCD_Init();

  // Khởi tạo LCD
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System initializing");
  servo0.attach(SERVO_PIN);
  servo1.attach(SERVO_PIN1);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected!");

  // Tạo Semaphore
  wifiSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(wifiSemaphore); // Semaphore khởi tạo sẵn sàng

  // Tạo hai task để quét thẻ từ
  xTaskCreate(rfidTask1, "RFID Task 1", 2048, NULL, 1, NULL);
  xTaskCreate(rfidTask2, "RFID Task 2", 2048, NULL, 1, NULL);

  Serial.println("Ready to scan RFID cards.");
}

void loop() {
  // ESP32 không cần code trong loop khi sử dụng FreeRTOS
}

// Task xử lý quét thẻ từ Module 1
void rfidTask1(void* pvParameters) {
  while (true) {
    checkRFID(rfid1, "Module 1");
    vTaskDelay(200 / portTICK_PERIOD_MS); // Delay để tránh quét liên tục
  }
}

// Task xử lý quét thẻ từ Module 2
void rfidTask2(void* pvParameters) {
  while (true) {
    checkRFID(rfid2, "Module 2");
    vTaskDelay(200 / portTICK_PERIOD_MS); // Delay để tránh quét liên tục
  }
}

// Hàm kiểm tra và gửi UID từ từng module
void checkRFID(MFRC522 &rfid, String moduleName) {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    // In UID của thẻ
    Serial.print(moduleName + " - Card UID: ");
    String cardUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      cardUID += String(rfid.uid.uidByte[i], HEX);
    }
    cardUID.toUpperCase();
    Serial.println(cardUID);

    // Gửi UID lên API
    if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY)) {
      sendUIDToAPI(cardUID);
      xSemaphoreGive(wifiSemaphore);
    }

    // Ngắt kết nối thẻ
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

// Gửi UID đến API và xử lý phản hồi
void sendUIDToAPI(String uid) {
  if (client.connect(serverIP, serverPort)) {
    Serial.println("Sending data to API...");
    String postData = "card_id=" + uid;

    client.println("POST /card/parkinout HTTP/1.1");
    client.print("Host: ");
    client.println(serverIP);
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(postData.length());
    client.println();
    client.println(postData);

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout!");
        client.stop();
        return;
      }
    }

    // Đọc phản hồi từ API
    String response = "";
    while (client.available()) {
      String line = client.readStringUntil('\n');
      response += line;
    }
    Serial.println("Response from server: " + response);

    // Phân tích phản hồi từ API
    if (response.indexOf("parked in") >= 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Welcome!");
       servo0.write(0); 
       delay(5000);
       servo0.write(90); 
    } else if (response.indexOf("parked out") >= 0) {
  // Tìm và lấy giá trị 'cost' từ phản hồi JSON
  int costIndex = response.indexOf("\"cost\":");
  if (costIndex >= 0) {
    // Cắt chuỗi bắt đầu từ vị trí sau "cost":
    String costString = response.substring(costIndex + 7);
    
    // Tìm dấu phẩy (",") hoặc dấu ngoặc đóng "}" sau giá trị cost, để cắt giá trị chính xác
    int endIndex = costString.indexOf(",");
    if (endIndex == -1) {
      endIndex = costString.indexOf("}");
    }
    String cost = costString.substring(0, endIndex);
    cost.trim();  // Loại bỏ khoảng trắng thừa

    // Hiển thị lên LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Thank you!");  // Hiển thị "Thank you!" ở dòng 1
    lcd.setCursor(0, 1);
    lcd.print("Cost: ");      // Hiển thị "Cost: " ở dòng 2
    lcd.print(cost);      
    servo1.write(0);    // Hiển thị giá trị cost
   delay(5000);
     servo1.write(90); 
  }
}


    client.stop();
  } else {
    Serial.println("Connection to server failed.");
  }
}
