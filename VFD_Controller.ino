#include <SPI.h>

// --- Pin Definitions ---
const int pinSTB = 10;

// --- VFD Constants ---
const int MAX_COLS = 16;
const int MAX_LINES = 2;

// --- VFD Commands ---
const byte CMD_CLEAR = 0x01;         // Clears the display
const byte CMD_HOME = 0x02;          // Returns the cursor to home
const byte CMD_ENTRY_MODE = 0x06;    // Increments cursor and doesn't shift.
const byte CMD_CURSOR_LEFT = 0x10;   // Moves the cursor back one space.
const byte CMD_FUNCTION_SET = 0x38;  // 8-bit, 2 Lines, 100% Brightness
const byte CMD_DISPLAY_ON = 0x0C;    // Display on, Cursor off, Blink off.

// --- BUFFER SETTINGS ---
// It must be a power of 2 for the bitwise wrap-around to work efficiently (128, 256).
const int BUF_SIZE = 1024;
char rxBuffer[BUF_SIZE];
int bufHead = 0;  // Where we write incoming data
int bufTail = 0;  // Where we read data to display

// --- FLOW CONTROL SETTINGS (NEW) ---
const byte XON = 0x11;   // ASCII Character 17 (Resume)
const byte XOFF = 0x13;  // ASCII Character 19 (Pause)
bool senderPaused = false;

// When buffer fills to 512, tell PC to STOP.
const int HIGH_WATER_MARK = 512;
// When buffer drains to 256, tell PC to RESUME.
const int LOW_WATER_MARK = 256;

// --- TYPING SPEED SETTINGS ---
const int typingDelay = 10;         // 10ms delay between chars
unsigned long lastCharMillis = 0;  // Timer tracker

// --- Global Variables ---
char screenBuffer[MAX_LINES][MAX_COLS];
int currentLine = 0;
int currentCol = 0;

// Blink Logic
unsigned long previousMillis = 0;
const long blinkInterval = 500;
bool cursorVisible = true;
const byte cursorChar = 0xFF;

void setup() {
  pinMode(pinSTB, OUTPUT);
  digitalWrite(pinSTB, HIGH);

  Serial.begin(9600);

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE3);
  SPI.setClockDivider(SPI_CLOCK_DIV32);

  delay(100);

  // Initialize VFD
  sendVFD(CMD_FUNCTION_SET, false);
  delayMicroseconds(150);
  sendVFD(CMD_DISPLAY_ON, false);
  delayMicroseconds(150);
  sendVFD(CMD_ENTRY_MODE, false);
  delayMicroseconds(150);

  // --- RUN STARTUP ANIMATION ---
  startupSequence();

  // Initialize Buffer & Screen
  clearScreenBuffer();

  // Print Test Message
  printVFD("Ready:");

  Serial.println("Ready for input.");
  Serial.println(" Commands:");
  Serial.println(" \\clear : Clear Screen");
  Serial.println(" \\new   : New Line");
  Serial.println(" \\F0    : Hex Code");
}

void loop() {
  unsigned long currentMillis = millis();

  // --- 1. HIGH PRIORITY: FILL BUFFER (PRODUCER) ---
  while (Serial.available() > 0) {
    char c = Serial.read();
    int nextHead = (bufHead + 1) % BUF_SIZE;

    // Only save if we have room
    if (nextHead != bufTail) {
      rxBuffer[bufHead] = c;
      bufHead = nextHead;
    }
    // (If buffer is full, we drop the char)

    // CHECK BUFFER LEVEL FOR PAUSE (XOFF)
    int bufferCount = (bufHead >= bufTail) ? (bufHead - bufTail) : (BUF_SIZE - bufTail + bufHead);

    if (bufferCount > HIGH_WATER_MARK && !senderPaused) {
      Serial.write(XOFF);
      senderPaused = true;
    }
  }  // <--- ERROR WAS HERE: You were missing this closing brace!

  // --- 2. PROCESS VFD (CONSUMER) ---
  // This must be OUTSIDE the while loop so it runs even when no data is arriving.
  if (bufHead != bufTail) {

    if (currentMillis - lastCharMillis >= typingDelay) {
      lastCharMillis = currentMillis;

      previousMillis = currentMillis;
      cursorVisible = true;

      char c = rxBuffer[bufTail];
      bufTail = (bufTail + 1) % BUF_SIZE;

      processBufferedChar(c);

      // CHECK BUFFER LEVEL FOR RESUME (XON)
      int bufferCount = (bufHead >= bufTail) ? (bufHead - bufTail) : (BUF_SIZE - bufTail + bufHead);

      if (bufferCount < LOW_WATER_MARK && senderPaused) {
        Serial.write(XON);
        senderPaused = false;
      }
    }
  }

  // --- 3. BLINK LOGIC ---
  if (bufHead == bufTail) {
    blinkLogic();
  }
}

// --- LOGIC: Process 1 character from Buffer ---
void processBufferedChar(char c) {
  // --- CASE 1: SLASH COMMANDS (\new, \clear, \hex) ---
  if (c == '\\') {
    parseSlashCommand();
    printStatus();
  }
  // --- CASE 2: Jump to Line 2 (Pipe) ---
  else if (c == '|') {
    sendVFD(' ', true);
    sendVFD(CMD_CURSOR_LEFT, false);
    currentLine = 1;
    currentCol = 0;
    sendVFD(0x80 | 0x40, false);
    printStatus();
  }
  // --- CASE 3: Newline ---
  else if (c == '\n' || c == '\r') {
    handleNewline();
    printStatus();
  }
  // --- CASE 4: Normal Typing ---
  else {
    writeChar(c);
    printStatus();
  }
}

// --- COMMAND PARSER (Modified for Ring Buffer) ---
void parseSlashCommand() {
  String cmd = "";
  unsigned long startWait = millis();
  
  // 1. READ COMMAND
  while (cmd.length() < 5) {
    if (millis() - startWait > 3000) break; // Timeout

    // Read from Buffer
    if (bufHead != bufTail) {
      char nextC = rxBuffer[bufTail];
      
      // Stop at delimiter
      if (nextC == ' ' || nextC == '\n' || nextC == '\r') {
        bufTail = (bufTail + 1) % BUF_SIZE; // Consume delimiter
        break; 
      }
      
      cmd += nextC;
      bufTail = (bufTail + 1) % BUF_SIZE; // Consume char
    }

    // Keep filling Buffer from Hardware
    while (Serial.available() > 0) {
       char c = Serial.read();
       int nextHead = (bufHead + 1) % BUF_SIZE;
       if (nextHead != bufTail) {
         rxBuffer[bufHead] = c;
         bufHead = nextHead;
       }
    }
  }

  // 2. CRITICAL: CATCH LAGGY NEWLINES
  // Wait 10ms to ensure the \n part of a \r\n pair has time to arrive
  unsigned long cleanupStart = millis();
  while(millis() - cleanupStart < 10) {
      while (Serial.available() > 0) {
       char c = Serial.read();
       int nextHead = (bufHead + 1) % BUF_SIZE;
       if (nextHead != bufTail) {
         rxBuffer[bufHead] = c;
         bufHead = nextHead;
       }
    }
  }

  // 3. EAT TRAILING NEWLINES
  // If the very next char is a newline, it belongs to this command -> Trash it.
  if (bufHead != bufTail) {
    char peek = rxBuffer[bufTail];
    if (peek == '\n' || peek == '\r') {
      bufTail = (bufTail + 1) % BUF_SIZE; 
    }
  }
  
  // 4. EXECUTE
  if (cmd.equalsIgnoreCase("clear")) {
    clearScreenBuffer();
  }
  else if (cmd.equalsIgnoreCase("new")) {
    handleNewline();
  }
  else if (cmd.length() == 2) {
    char h = cmd.charAt(0);
    char l = cmd.charAt(1);
    byte rawByte = (hexToVal(h) << 4) + hexToVal(l);
    writeChar(rawByte);
  }
}

// --- BLINK LOGIC ---
void blinkLogic() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    cursorVisible = !cursorVisible;

    if (cursorVisible) {
      sendVFD(cursorChar, true);
    } else {
      sendVFD(' ', true);
    }
    sendVFD(CMD_CURSOR_LEFT, false);
  }
}

// --- STANDARD HELPERS (Unchanged) ---
void startupSequence() {
  byte animChars[] = { 0x20, 0x10, 0x11, 0x12, 0x13, 0x14 };
  sendVFD(CMD_CLEAR, false);
  delay(1);
  for (int row = 0; row < MAX_LINES; row++) {
    byte lineStart = (row == 0) ? 0x80 : (0x80 | 0x40);
    sendVFD(lineStart, false);
    for (int col = 0; col < MAX_COLS; col++) {
      for (int i = 0; i < 5; i++) {
        sendVFD(animChars[i], true);
        sendVFD(CMD_CURSOR_LEFT, false);
        delay(1);
      }
      sendVFD(animChars[5], true);
      delay(5);
    }
  }
  delay(500);
}

void handleNewline() {
  sendVFD(' ', true);
  sendVFD(CMD_CURSOR_LEFT, false);
  if (currentLine == 0) {
    currentLine = 1;
    currentCol = 0;
    sendVFD(0x80 | 0x40, false);
  } else {
    scrollDisplayUp();
  }
}

void writeChar(char c) {
  if (currentCol >= MAX_COLS) {
    if (currentLine == 0) {
      currentLine = 1;
      currentCol = 0;
      sendVFD(0x80 | 0x40, false);
    } else {
      scrollDisplayUp();
    }
  }
  if (currentCol == 0) {
    byte addr = (currentLine == 0) ? 0x80 : (0x80 | 0x40);
    sendVFD(addr, false);
  }
  screenBuffer[currentLine][currentCol] = c;
  sendVFD(c, true);
  currentCol++;
}

void scrollDisplayUp() {
  for (int i = 0; i < MAX_COLS; i++) {
    screenBuffer[0][i] = screenBuffer[1][i];
    screenBuffer[1][i] = ' ';
  }
  sendVFD(CMD_CLEAR, false);
  delay(5);
  for (int i = 0; i < MAX_COLS; i++) {
    sendVFD(screenBuffer[0][i], true);
    delayMicroseconds(50);
  }
  sendVFD(0x80 | 0x40, false);
  currentLine = 1;
  currentCol = 0;
}

void clearScreenBuffer() {
  for (int y = 0; y < MAX_LINES; y++) {
    for (int x = 0; x < MAX_COLS; x++) {
      screenBuffer[y][x] = ' ';
    }
  }
  currentLine = 0;
  currentCol = 0;
  sendVFD(CMD_CLEAR, false);
  delay(5);
}

byte hexToVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

void printStatus() {
  // Commented out to reduce serial traffic during fast pasting
  // Serial.print("Pos: L"); Serial.print(currentLine);
  // Serial.print(" C"); Serial.println(currentCol);
}

void sendVFD(byte data, bool isData) {
  byte startByte = isData ? 0xFA : 0xF8;
  digitalWrite(pinSTB, LOW);
  SPI.transfer(startByte);
  SPI.transfer(data);
  digitalWrite(pinSTB, HIGH);
  delayMicroseconds(60);
}

void printVFD(const char* str) {
  while (*str) {
    writeChar(*str++);
  }
}