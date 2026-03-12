/*
 * Smart Canteen Management System - Arduino Controller
 * Optimized for low RAM usage on Arduino Uno/Nano
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>

// Pin Definitions
#define SS_PIN 9
#define RST_PIN 10
#define BUZZER A1
#define LED_OK A2
#define LED_DENY A3

// Memory optimization: Reduced array sizes
#define MAX_MENU 8
#define MAX_CART 4

// LCD Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RFID Setup
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Keypad Setup
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8, A0};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Compact Menu Item Structure
struct MenuItem {
  char key[3];
  char name[11];
  uint16_t price;  // Price in cents/paise
  uint8_t stock;
};

// Compact Cart Item Structure  
struct CartItem {
  uint8_t menuIdx;  // Index into menu array
  uint8_t qty;
};

// Global Variables
MenuItem menu[MAX_MENU];
uint8_t menuCount = 0;
uint8_t currentMenuIndex = 0;

CartItem cart[MAX_CART];
uint8_t cartCount = 0;

// User Session Variables
char userName[11] = "";
char userUID[12] = "";
uint16_t userBalance = 0;      // In cents/paise
uint16_t userCreditUsed = 0;
uint16_t userCreditLimit = 0;

// Flags packed into single byte
uint8_t flags = 0;
#define FLAG_SESSION   0x01
#define FLAG_SOUND     0x02
#define FLAG_RECHARGE  0x04
#define FLAG_PENDING   0x08

#define sessionActive  (flags & FLAG_SESSION)
#define soundEnabled   (flags & FLAG_SOUND)
#define rechargeMode   (flags & FLAG_RECHARGE)
#define hasPendingStatus (flags & FLAG_PENDING)

// Timing Variables
unsigned long lastActivityTime = 0;
unsigned long lastPresenceTime = 0;
unsigned long lastScanTime = 0;

#define SESSION_TIMEOUT 60000UL
#define PRESENCE_INTERVAL 5000UL
#define SCAN_COOLDOWN 2000UL

// Pending Order Status
char pendingOrderStatus[10] = "";

void setup() {
  Serial.begin(9600);
  
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_OK, OUTPUT);
  pinMode(LED_DENY, OUTPUT);
  
  digitalWrite(LED_OK, LOW);
  digitalWrite(LED_DENY, LOW);
  
  lcd.init();
  lcd.backlight();
  
  SPI.begin();
  mfrc522.PCD_Init();
  
  flags = FLAG_SOUND;  // Sound enabled by default
  
  bootScreen();
  showIdleScreen();
}

void loop() {
  pollSerialNotifications();
  showPendingOrderStatusIfAny();
  
  if (sessionActive) {
    if (millis() - lastActivityTime > SESSION_TIMEOUT) {
      handleSessionTimeout();
      return;
    }
    
    if (millis() - lastPresenceTime > PRESENCE_INTERVAL) {
      sendPresencePing();
      lastPresenceTime = millis();
    }
    
    menuHandler();
  } else {
    checkForCard();
  }
}

//===== BOOT & DISPLAY =====

void bootScreen() {
  lcd.clear();
  lcd.print(F("Smart Canteen"));
  lcd.setCursor(0, 1);
  lcd.print(F("System v1.0"));
  beepShort();
  delay(1000);
  
  lcd.clear();
  lcd.print(F("Initializing..."));
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 16; i++) {
    lcd.write(255);
    delay(80);
  }
  beepDouble();
  delay(300);
}

void showIdleScreen() {
  lcd.clear();
  lcd.print(F("  Scan Card..."));
  lcd.setCursor(0, 1);
  lcd.print(F("================"));
}

void showSystemLocked(String message) {
  digitalWrite(LED_DENY, HIGH);
  
  lcd.clear();
  lcd.print(F("System Locked!"));
  lcd.setCursor(0, 1);
  if (message.length() > 16) {
    message = message.substring(0, 16);
  }
  lcd.print(message);
  
  beepError();
  delay(2000);
  digitalWrite(LED_DENY, LOW);
  showIdleScreen();
}

void showAccessGranted() {
  digitalWrite(LED_OK, HIGH);
  digitalWrite(LED_DENY, LOW);
  
  lcd.clear();
  lcd.print(F("Access Granted!"));
  lcd.setCursor(0, 1);
  lcd.print(F("Hi "));
  lcd.print(userName);
  
  beepSuccess();
  delay(1200);
  digitalWrite(LED_OK, LOW);
}

void showAccessDenied() {
  digitalWrite(LED_DENY, HIGH);
  digitalWrite(LED_OK, LOW);
  
  lcd.clear();
  lcd.print(F("Access Denied!"));
  lcd.setCursor(0, 1);
  lcd.print(F("Unknown Card"));
  
  beepError();
  delay(1500);
  digitalWrite(LED_DENY, LOW);
  showIdleScreen();
}

//===== RFID =====

void checkForCard() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  
  if (millis() - lastScanTime < SCAN_COOLDOWN) {
    mfrc522.PICC_HaltA();
    return;
  }
  
  lastScanTime = millis();
  
  // Read UID - compact format
  uint8_t pos = 0;
  for (uint8_t i = 0; i < mfrc522.uid.size && pos < 10; i++) {
    uint8_t b = mfrc522.uid.uidByte[i];
    userUID[pos++] = "0123456789ABCDEF"[b >> 4];
    userUID[pos++] = "0123456789ABCDEF"[b & 0x0F];
  }
  userUID[pos] = '\0';
  
  mfrc522.PICC_HaltA();
  
  if (rechargeMode) {
    Serial.print(F("UID:"));
    Serial.println(userUID);
    flags &= ~FLAG_RECHARGE;
    showIdleScreen();
    return;
  }
  
  if (authenticateUser()) {
    showAccessGranted();
    beginUserSession();
  } else {
    showAccessDenied();
  }
}

//===== AUTHENTICATION =====

bool authenticateUser() {
  lcd.clear();
  lcd.print(F("Verifying..."));
  
  Serial.print(F("UID:"));
  Serial.println(userUID);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (Serial.available()) {
      String response = Serial.readStringUntil('\n');
      response.trim();
      
      if (response.startsWith(F("VALID,"))) {
        parseUserData(response);
        return true;
      } else if (response.startsWith(F("LOCKED,"))) {
        // System is locked - show message
        showSystemLocked(response.substring(7));
        return false;
      } else if (response.startsWith(F("INVALID"))) {
        return false;
      }
    }
  }
  return false;
}

void parseUserData(String &data) {
  // Format: VALID,NAME,BALANCE,CREDIT_USED,CREDIT_LIMIT
  int idx1 = data.indexOf(',');
  int idx2 = data.indexOf(',', idx1 + 1);
  int idx3 = data.indexOf(',', idx2 + 1);
  int idx4 = data.indexOf(',', idx3 + 1);
  
  String name = data.substring(idx1 + 1, idx2);
  name.toCharArray(userName, sizeof(userName));
  
  userBalance = (uint16_t)(data.substring(idx2 + 1, idx3 > 0 ? idx3 : data.length()).toFloat() * 100);
  
  if (idx3 > 0 && idx4 > 0) {
    userCreditUsed = (uint16_t)(data.substring(idx3 + 1, idx4).toFloat() * 100);
    userCreditLimit = (uint16_t)(data.substring(idx4 + 1).toFloat() * 100);
  } else {
    userCreditUsed = 0;
    userCreditLimit = 0;
  }
}

//===== SESSION MANAGEMENT =====

void beginUserSession() {
  flags |= FLAG_SESSION;
  lastActivityTime = millis();
  lastPresenceTime = millis();
  cartCount = 0;
  
  Serial.print(F("ONLINE:"));
  Serial.println(userUID);
  
  if (requestMenu()) {
    currentMenuIndex = 0;
    showMenuItem();
  } else {
    lcd.clear();
    lcd.print(F("Menu Error!"));
    delay(1500);
    endUserSession();
  }
}

void endUserSession() {
  Serial.print(F("OFFLINE:"));
  Serial.println(userUID);
  
  flags &= ~FLAG_SESSION;
  cartCount = 0;
  currentMenuIndex = 0;
  menuCount = 0;
  userName[0] = '\0';
  userUID[0] = '\0';
  userBalance = 0;
  userCreditUsed = 0;
  userCreditLimit = 0;
  
  showIdleScreen();
}

void handleSessionTimeout() {
  lcd.clear();
  lcd.print(F("Session Timeout"));
  beepWarning();
  delay(1500);
  endUserSession();
}

void sendPresencePing() {
  Serial.print(F("ONLINE:"));
  Serial.println(userUID);
}

//===== MENU =====

bool requestMenu() {
  lcd.clear();
  lcd.print(F("Loading Menu..."));
  
  Serial.println(F("MENU"));
  
  menuCount = 0;
  unsigned long startTime = millis();
  
  while (millis() - startTime < 8000) {
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      
      if (line.startsWith(F("ITEM,"))) {
        parseMenuItem(line);
        Serial.println(F("ACK"));
      } else if (line == F("MENU_END")) {
        return menuCount > 0;
      }
    }
  }
  return false;
}

void parseMenuItem(String &data) {
  if (menuCount >= MAX_MENU) return;
  
  int idx1 = data.indexOf(',');
  int idx2 = data.indexOf(',', idx1 + 1);
  int idx3 = data.indexOf(',', idx2 + 1);
  int idx4 = data.indexOf(',', idx3 + 1);
  int idx5 = data.indexOf(',', idx4 + 1);
  
  String key = data.substring(idx1 + 1, idx2);
  String name = data.substring(idx2 + 1, idx3);
  float price = data.substring(idx4 + 1, idx5 > 0 ? idx5 : data.length()).toFloat();
  int stock = idx5 > 0 ? data.substring(idx5 + 1).toInt() : 99;
  
  key.toCharArray(menu[menuCount].key, 3);
  name.toCharArray(menu[menuCount].name, 11);
  menu[menuCount].price = (uint16_t)(price * 100);
  menu[menuCount].stock = stock > 255 ? 255 : stock;
  
  menuCount++;
}

void showMenuItem() {
  if (menuCount == 0) return;
  
  lcd.clear();
  lcd.print(menu[currentMenuIndex].name);
  
  lcd.setCursor(0, 1);
  lcd.print(F("Rs"));
  lcd.print(menu[currentMenuIndex].price / 100);
  lcd.print(F(" S:"));
  lcd.print(menu[currentMenuIndex].stock);
  lcd.print(' ');
  lcd.print(currentMenuIndex + 1);
  lcd.print('/');
  lcd.print(menuCount);
}

//===== MENU HANDLER =====

void menuHandler() {
  char key = keypad.getKey();
  if (!key) return;
  
  lastActivityTime = millis();
  
  switch (key) {
    case 'A':  // Next item (with loop)
      currentMenuIndex = (currentMenuIndex + 1) % menuCount;
      showMenuItem(); beepShort();
      break;
    case 'B':  // Previous item (with loop)
      currentMenuIndex = (currentMenuIndex == 0) ? menuCount - 1 : currentMenuIndex - 1;
      showMenuItem(); beepShort();
      break;
    case 'C': selectQuantity(); break;
    case 'D': confirmExit(); break;
    case '*': confirmAndPlaceOrder(); break;
    case '#': viewCart(); break;
    case '1': showCartTotal(); break;
    case '2': showBalance(); break;
    case '3': clearCart(); break;
    case '4': cancelOrder(); break;
    case '5': editCart(); break;
    case '6': toggleSound(); break;
    case '7': deleteCartItem(); break;
    case '0': showHelp(); break;
  }
}

//===== QUANTITY SELECTION =====

void selectQuantity() {
  if (menu[currentMenuIndex].stock == 0) {
    lcd.clear();
    lcd.print(F("Out of Stock!"));
    beepError();
    delay(1200);
    showMenuItem();
    return;
  }
  
  lcd.clear();
  lcd.print(F("Qty:"));
  lcd.print(menu[currentMenuIndex].name);
  lcd.setCursor(0, 1);
  lcd.print(F("Enter: "));
  
  char qtyBuf[4] = "";
  uint8_t qtyLen = 0;
  uint8_t cursorPos = 7;
  
  while (true) {
    char key = keypad.getKey();
    if (!key) continue;
    
    lastActivityTime = millis();
    
    if (key >= '0' && key <= '9' && qtyLen < 2) {
      qtyBuf[qtyLen++] = key;
      qtyBuf[qtyLen] = '\0';
      lcd.setCursor(cursorPos++, 1);
      lcd.print(key);
      beepShort();
    } else if (key == 'C') {
      if (qtyLen > 0) {
        uint8_t qty = atoi(qtyBuf);
        if (qty > 0 && qty <= menu[currentMenuIndex].stock) {
          addToCart(currentMenuIndex, qty);
        } else {
          lcd.clear();
          lcd.print(F("Invalid Qty!"));
          beepError();
          delay(800);
        }
      }
      break;
    } else if (key == 'D') {
      showMenuItem();
      return;
    } else if (key == '*' && qtyLen > 0) {
      qtyBuf[--qtyLen] = '\0';
      lcd.setCursor(--cursorPos, 1);
      lcd.print(' ');
      beepShort();
    }
  }
  showMenuItem();
}

//===== CART =====

void addToCart(uint8_t menuIndex, uint8_t qty) {
  // Check if item already in cart
  for (uint8_t i = 0; i < cartCount; i++) {
    if (cart[i].menuIdx == menuIndex) {
      cart[i].qty += qty;
      lcd.clear();
      lcd.print(F("Added! Total:"));
      lcd.print(cart[i].qty);
      beepSuccess();
      delay(800);
      return;
    }
  }
  
  if (cartCount < MAX_CART) {
    cart[cartCount].menuIdx = menuIndex;
    cart[cartCount].qty = qty;
    cartCount++;
    
    lcd.clear();
    lcd.print(F("Added to Cart!"));
    beepSuccess();
    delay(800);
  } else {
    lcd.clear();
    lcd.print(F("Cart Full!"));
    beepError();
    delay(1000);
  }
}

void viewCart() {
  if (cartCount == 0) {
    lcd.clear();
    lcd.print(F("Cart Empty!"));
    delay(1200);
    showMenuItem();
    return;
  }
  
  uint8_t idx = 0;
  
  while (true) {
    uint8_t mi = cart[idx].menuIdx;
    lcd.clear();
    lcd.print(idx + 1);
    lcd.print('.');
    lcd.print(menu[mi].name);
    lcd.setCursor(0, 1);
    lcd.print(F("x"));
    lcd.print(cart[idx].qty);
    lcd.print(F(" Rs"));
    lcd.print((uint32_t)menu[mi].price * cart[idx].qty / 100);
    
    char key = 0;
    while (!key) {
      key = keypad.getKey();
      if (millis() - lastActivityTime > SESSION_TIMEOUT) {
        handleSessionTimeout();
        return;
      }
    }
    
    lastActivityTime = millis();
    
    if (key == 'A' && idx > 0) { idx--; beepShort(); }
    else if (key == 'B' && idx < cartCount - 1) { idx++; beepShort(); }
    else if (key == 'D' || key == '#') break;
  }
  showMenuItem();
}

uint32_t getCartTotal() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < cartCount; i++) {
    total += (uint32_t)menu[cart[i].menuIdx].price * cart[i].qty;
  }
  return total;
}

uint32_t availableSpend() {
  return (uint32_t)userBalance + (userCreditLimit - userCreditUsed);
}

void showCartTotal() {
  lcd.clear();
  lcd.print(F("Cart Total:"));
  lcd.setCursor(0, 1);
  lcd.print(F("Rs "));
  lcd.print(getCartTotal() / 100);
  delay(1800);
  showMenuItem();
}

void showBalance() {
  lcd.clear();
  lcd.print(F("Bal:Rs"));
  lcd.print(userBalance / 100);
  lcd.setCursor(0, 1);
  lcd.print(F("Crd:Rs"));
  lcd.print((userCreditLimit - userCreditUsed) / 100);
  delay(2000);
  showMenuItem();
}

void clearCart() {
  if (cartCount == 0) {
    lcd.clear();
    lcd.print(F("Cart Empty!"));
    delay(1000);
  } else {
    lcd.clear();
    lcd.print(F("Clear Cart?"));
    lcd.setCursor(0, 1);
    lcd.print(F("C=Yes D=No"));
    
    while (true) {
      char key = keypad.getKey();
      if (!key) continue;
      if (key == 'C') {
        cartCount = 0;
        lcd.clear();
        lcd.print(F("Cart Cleared!"));
        beepSuccess();
        delay(800);
        break;
      } else if (key == 'D') break;
    }
  }
  showMenuItem();
}

void cancelOrder() {
  lcd.clear();
  lcd.print(F("Cancel & Exit?"));
  lcd.setCursor(0, 1);
  lcd.print(F("C=Yes D=No"));
  
  while (true) {
    char key = keypad.getKey();
    if (!key) continue;
    if (key == 'C') {
      cartCount = 0;
      lcd.clear();
      lcd.print(F("Order Cancelled"));
      beepWarning();
      delay(1200);
      endUserSession();
      return;
    } else if (key == 'D') {
      showMenuItem();
      return;
    }
  }
}

void editCart() {
  if (cartCount == 0) {
    lcd.clear();
    lcd.print(F("Cart Empty!"));
    delay(1200);
    showMenuItem();
    return;
  }
  
  uint8_t idx = 0;
  
  while (true) {
    uint8_t mi = cart[idx].menuIdx;
    lcd.clear();
    lcd.print(F("Edit:"));
    lcd.print(menu[mi].name);
    lcd.setCursor(0, 1);
    lcd.print(F("Qty:"));
    lcd.print(cart[idx].qty);
    lcd.print(F(" C=Chg"));
    
    char key = 0;
    while (!key) {
      key = keypad.getKey();
      if (millis() - lastActivityTime > SESSION_TIMEOUT) {
        handleSessionTimeout();
        return;
      }
    }
    
    lastActivityTime = millis();
    
    if (key == 'A' && idx > 0) { idx--; beepShort(); }
    else if (key == 'B' && idx < cartCount - 1) { idx++; beepShort(); }
    else if (key == 'C') {
      // Edit quantity for this item
      lcd.clear();
      lcd.print(F("New Qty:"));
      lcd.print(menu[mi].name);
      lcd.setCursor(0, 1);
      lcd.print(F("Enter: "));
      
      char qtyBuf[4] = "";
      uint8_t qtyLen = 0;
      uint8_t cursorPos = 7;
      
      while (true) {
        char k = keypad.getKey();
        if (!k) continue;
        
        lastActivityTime = millis();
        
        if (k >= '0' && k <= '9' && qtyLen < 2) {
          qtyBuf[qtyLen++] = k;
          qtyBuf[qtyLen] = '\0';
          lcd.setCursor(cursorPos++, 1);
          lcd.print(k);
          beepShort();
        } else if (k == 'C') {
          if (qtyLen > 0) {
            uint8_t newQty = atoi(qtyBuf);
            if (newQty == 0) {
              // Remove item if qty is 0
              for (uint8_t i = idx; i < cartCount - 1; i++) cart[i] = cart[i + 1];
              cartCount--;
              lcd.clear();
              lcd.print(F("Item Removed!"));
              beepSuccess();
              delay(800);
              if (cartCount == 0) {
                showMenuItem();
                return;
              }
              if (idx >= cartCount) idx = cartCount - 1;
            } else if (newQty <= menu[mi].stock) {
              cart[idx].qty = newQty;
              lcd.clear();
              lcd.print(F("Qty Updated!"));
              beepSuccess();
              delay(800);
            } else {
              lcd.clear();
              lcd.print(F("Max Stock:"));
              lcd.print(menu[mi].stock);
              beepError();
              delay(1000);
            }
          }
          break;
        } else if (k == 'D') {
          break;
        } else if (k == '*' && qtyLen > 0) {
          qtyBuf[--qtyLen] = '\0';
          lcd.setCursor(--cursorPos, 1);
          lcd.print(' ');
          beepShort();
        }
      }
    } else if (key == 'D') break;
  }
  showMenuItem();
}

void deleteCartItem() {
  if (cartCount == 0) {
    lcd.clear();
    lcd.print(F("Cart Empty!"));
    delay(1200);
    showMenuItem();
    return;
  }
  
  uint8_t idx = 0;
  
  while (true) {
    lcd.clear();
    lcd.print(F("Del:"));
    lcd.print(menu[cart[idx].menuIdx].name);
    lcd.setCursor(0, 1);
    lcd.print(F("C=Del A/B=Nav"));
    
    char key = 0;
    while (!key) key = keypad.getKey();
    
    lastActivityTime = millis();
    
    if (key == 'A' && idx > 0) idx--;
    else if (key == 'B' && idx < cartCount - 1) idx++;
    else if (key == 'C') {
      for (uint8_t i = idx; i < cartCount - 1; i++) cart[i] = cart[i + 1];
      cartCount--;
      
      lcd.clear();
      lcd.print(F("Removed!"));
      beepSuccess();
      delay(800);
      
      if (cartCount == 0) break;
      if (idx >= cartCount) idx = cartCount - 1;
    } else if (key == 'D') break;
  }
  showMenuItem();
}

void showHelp() {
  const char* helpLines[] = {
    "A/B=Nav Menu",
    "C=Add D=Exit",
    "*=Order #=Cart",
    "1=Total 2=Bal",
    "3=Clear 4=Cncl",
    "5=Edit 7=Del"
  };
  
  uint8_t page = 0;
  uint8_t maxPages = 3;
  
  while (true) {
    lcd.clear();
    lcd.print(helpLines[page * 2]);
    lcd.setCursor(0, 1);
    lcd.print(helpLines[page * 2 + 1]);
    
    char key = 0;
    while (!key) {
      key = keypad.getKey();
      if (millis() - lastActivityTime > SESSION_TIMEOUT) {
        handleSessionTimeout();
        return;
      }
    }
    
    lastActivityTime = millis();
    
    if (key == 'A' || key == 'B') {
      page = (page + 1) % maxPages;
      beepShort();
    } else if (key == 'D' || key == '0') {
      break;
    }
  }
  showMenuItem();
}

//===== ORDER =====

void confirmAndPlaceOrder() {
  if (cartCount == 0) {
    lcd.clear();
    lcd.print(F("Cart Empty!"));
    beepWarning();
    delay(1200);
    showMenuItem();
    return;
  }
  
  uint32_t total = getCartTotal();
  uint32_t available = availableSpend();
  
  if (total > available) {
    lcd.clear();
    lcd.print(F("Insufficient"));
    lcd.setCursor(0, 1);
    lcd.print(F("Balance!"));
    beepError();
    delay(1500);
    showMenuItem();
    return;
  }
  
  lcd.clear();
  lcd.print(F("Total: Rs"));
  lcd.print(total / 100);
  lcd.setCursor(0, 1);
  lcd.print(F("C=OK D=Cancel"));
  
  while (true) {
    char key = keypad.getKey();
    if (!key) continue;
    
    lastActivityTime = millis();
    
    if (key == 'C') {
      placeOrder(total);
      return;
    } else if (key == 'D') {
      showMenuItem();
      return;
    }
  }
}

void placeOrder(uint32_t total) {
  lcd.clear();
  lcd.print(F("Placing Order..."));
  
  uint16_t walletPortion = (userBalance >= total) ? total : userBalance;
  uint16_t creditPortion = total - walletPortion;
  
  Serial.println(F("ORDER_START"));
  Serial.print(F("UID:"));
  Serial.println(userUID);
  
  for (uint8_t i = 0; i < cartCount; i++) {
    Serial.print(F("ITEM:"));
    Serial.println(menu[cart[i].menuIdx].key);
    Serial.print(F("QTY:"));
    Serial.println(cart[i].qty);
  }
  
  Serial.print(F("BALANCE:"));
  Serial.println((userBalance - walletPortion) / 100.0, 2);
  Serial.print(F("CREDIT_USED:"));
  Serial.println((userCreditUsed + creditPortion) / 100.0, 2);
  Serial.println(F("ORDER_END"));
  
  unsigned long startTime = millis();
  while (millis() - startTime < 8000) {
    if (Serial.available()) {
      String response = Serial.readStringUntil('\n');
      response.trim();
      
      if (response.startsWith(F("ORDER_OK"))) {
        int tokenIdx = response.indexOf(',');
        
        userBalance -= walletPortion;
        userCreditUsed += creditPortion;
        cartCount = 0;
        
        lcd.clear();
        lcd.print(F("Order Placed!"));
        if (tokenIdx > 0) {
          lcd.setCursor(0, 1);
          lcd.print(F("Token:"));
          lcd.print(response.substring(tokenIdx + 1));
        }
        beepSuccess();
        delay(2500);
        
        requestMenu();
        showMenuItem();
        return;
        
      } else if (response.startsWith(F("ORDER_FAIL"))) {
        lcd.clear();
        lcd.print(F("Order Failed!"));
        beepError();
        delay(1500);
        
        requestMenu();
        showMenuItem();
        return;
      }
    }
  }
  
  lcd.clear();
  lcd.print(F("Timeout!"));
  beepError();
  delay(1500);
  showMenuItem();
}

//===== UTILITY =====

void confirmExit() {
  lcd.clear();
  lcd.print(F("Exit Session?"));
  lcd.setCursor(0, 1);
  lcd.print(F("C=Yes D=No"));
  
  while (true) {
    char key = keypad.getKey();
    if (!key) continue;
    
    if (key == 'C') { endUserSession(); return; }
    else if (key == 'D') { showMenuItem(); return; }
  }
}

void toggleSound() {
  flags ^= FLAG_SOUND;
  lcd.clear();
  lcd.print(F("Sound: "));
  lcd.print(soundEnabled ? F("ON") : F("OFF"));
  delay(800);
  showMenuItem();
}

//===== SERIAL NOTIFICATIONS =====

void pollSerialNotifications() {
  if (!Serial.available()) return;
  
  String line = Serial.readStringUntil('\n');
  line.trim();
  
  if (line == F("RECHARGE_MODE")) {
    flags |= FLAG_RECHARGE;
    lcd.clear();
    lcd.print(F("Recharge Mode"));
    lcd.setCursor(0, 1);
    lcd.print(F("Scan Card..."));
    beepDouble();
  } else if (line.startsWith(F("ORDER_STATUS,"))) {
    int idx1 = line.indexOf(',');
    int idx2 = line.indexOf(',', idx1 + 1);
    
    String uid = line.substring(idx1 + 1, idx2);
    String status = line.substring(idx2 + 1);
    
    if (uid.equals(userUID) || !sessionActive) {
      status.toCharArray(pendingOrderStatus, sizeof(pendingOrderStatus));
      flags |= FLAG_PENDING;
    }
  }
}

void showPendingOrderStatusIfAny() {
  if (!hasPendingStatus) return;
  
  flags &= ~FLAG_PENDING;
  
  lcd.clear();
  lcd.print(F("Order:"));
  lcd.setCursor(0, 1);
  lcd.print(pendingOrderStatus);
  
  if (strcmp(pendingOrderStatus, "READY") == 0) {
    beepReady();
  } else {
    beepWarning();
  }
  
  delay(2500);
  
  if (sessionActive) showMenuItem();
  else showIdleScreen();
}

//===== BUZZER =====

void beepShort() {
  if (!soundEnabled) return;
  tone(BUZZER, 1000, 50);
}

void beepDouble() {
  if (!soundEnabled) return;
  tone(BUZZER, 1500, 100);
  delay(150);
  tone(BUZZER, 1500, 100);
}

void beepSuccess() {
  if (!soundEnabled) return;
  tone(BUZZER, 1000, 100);
  delay(100);
  tone(BUZZER, 1500, 100);
  delay(100);
  tone(BUZZER, 2000, 150);
}

void beepError() {
  if (!soundEnabled) return;
  tone(BUZZER, 400, 250);
  delay(100);
  tone(BUZZER, 300, 250);
}

void beepWarning() {
  if (!soundEnabled) return;
  tone(BUZZER, 800, 150);
  delay(100);
  tone(BUZZER, 800, 150);
}

void beepReady() {
  if (!soundEnabled) return;
  for (uint8_t i = 0; i < 3; i++) {
    tone(BUZZER, 2000, 180);
    delay(250);
  }
}
