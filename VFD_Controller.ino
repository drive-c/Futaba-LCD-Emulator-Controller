#include <SPI.h>                      // Library for Serial Peripheral Interface (SPI)

// --- Pin Definitions ---
const int pinSTB = 10;                // Sets the strobe pin required for SPI

// --- VFD Constants ---
const int MAX_COLS = 16;              // Sets the number of characters per row.
const int MAX_LINES = 2;              // Sets the number of lines.

// --- VFD Commands ---
const byte CMD_CLEAR = 0x01;          // Clears the display
const byte CMD_HOME = 0x02;           // Returns the cursor to home
const byte CMD_ENTRY_MODE = 0x06;     // Increments cursor and doesn't shift.
const byte CMD_CURSOR_LEFT = 0x10;    // Moves the cursor back one space.
const byte CMD_FUNCTION_SET = 0x38;   // 8-bit, 2 Lines, 100% Brightness
const byte CMD_DISPLAY_ON = 0x0C;     // Display on, Cursor off, Blink off.

// --- BUFFER SETTINGS ---

// NOTE: Arduino IDE doesn't really do flow control, so expect long strings to get garbled if using the Serial Monitor.
//       Other terminal emulators resepect the XON/XOFF control commands.
//       You many need to set BUF_SIZE lower if you are out of memory.

const int BUF_SIZE = 1024;            // Number of characters in buffer. Set to 128, 256, 512, or 1024.
const int HIGH_WATER_MARK = 512;      // Stop when buffer reaches this many characters.
const int LOW_WATER_MARK = 256;       // Resume loading when buffer reaches this many characters.

char rxBuffer[BUF_SIZE];              // Character buffer array.
int bufHead = 0;                      // Where we write incoming data
int bufTail = 0;                      // Where we read data to display

char screenBuffer[MAX_LINES][MAX_COLS]; // Character array for the 
int currentLine = 0;                  // Used to track which line the cursor is on.
int currentCol = 0;                   // Used to track the position on the line the cursor is on.

// --- FLOW CONTROL SETTINGS ---
const byte XON = 0x11;                // ASCII Character 17 (Resume)
const byte XOFF = 0x13;               // ASCII Character 19 (Pause)
bool senderPaused = false;            // Bool to check paused sending status.

// --- TYPING SPEED SETTINGS ---
  // Delay in ms from each character written on the display.
  // 10 is very fast, 20 is fast (default speed), 50 is pretty slow, 100 is very slow.
  // Your terminal emulator may also have character and line speed options.
const int typingDelay = 20;           // Time in ms to delay next character written on screen.
unsigned long lastCharMillis = 0;     // Timer tracker for character draw

// --- CURSOR BLINK VARIABLES ---
  // The default cursor on the VFD blinks too fast.
  // We use custom cursor blinking code.
unsigned long previousMillis = 0;     // Timer tracker for cursor draw
const long blinkInterval = 500;       // Time in ms that the cursor blinks on and off.
bool cursorVisible = true;            // Sets cursor visibility. We turn it off in some functions.
const byte cursorChar = 0xFF;         // Sets cursor character. 0xFF is full block (default), 0x5F is underscore, 0x10 is bar, 0x11 is thicker bar.

void setup() {
  pinMode(pinSTB, OUTPUT);            // Sets strobe pin as output (default is pin 10)
  digitalWrite(pinSTB, HIGH);         // Sets strobe pin to high

  Serial.begin(9600);                 // Sets the serial speed to 9600 baud.

  SPI.begin();                        // Initializes the SPI on the Arduino.
  
  // TODO: setBitOrder, setDataMode, and setClockDivider are deprecated and beginTransacation() should be used instead.
  SPI.setBitOrder(MSBFIRST);          // Set most significant bit first.
  SPI.setDataMode(SPI_MODE3);         // Sets data mode to 0x0C
  SPI.setClockDivider(SPI_CLOCK_DIV32); // Sets clock divider to 0x06

  delay(100);                       // Wait 100ms for device SPI to respond.

  // --- INITIALIZE THE VFD ---
  sendVFD(CMD_FUNCTION_SET, false); // Sets VFD brightness, byte size, and number of lines
  delayMicroseconds(150);           // Wait 150 microseconds for VFD to receive command.
  sendVFD(CMD_DISPLAY_ON, false);
  delayMicroseconds(150);           // Wait 150 microseconds for VFD to receive command.
  sendVFD(CMD_ENTRY_MODE, false);
  delayMicroseconds(150);           // Wait 150 microseconds for VFD to receive command.

  // --- VFD DISPLAY STARTUP SEQUENCE ---
  startupSequence();                // Show the startup animation.
  clearScreenBuffer();              // Clear the screen after the startup animation completes.
  printVFD("Ready:");               // Print "Ready:" on the VFD display.

  // --- DISPLAY COMMANDS IN TERMINAL EMULATOR ---
    // TODO: Consider shorter commands or contol keys
  Serial.println("Ready for input.");                             // Show that we are ready to accept input
  Serial.println(" Commands:");                                   // List the command hints
  Serial.println(" \\clear : Clear Screen");                      // Maybe \c instead?
  Serial.println(" \\new   : New Line");                          // Maybe \n instead?
  Serial.println(" \\HEX   : Table Characters e.g. \\5C for ¥."); // Maybe \h followed by hex code instead?
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

    // CHECK BUFFER LEVEL FOR PAUSE (XOFF)
    int bufferCount = (bufHead >= bufTail) ? (bufHead - bufTail) : (BUF_SIZE - bufTail + bufHead);
    if (bufferCount > HIGH_WATER_MARK && !senderPaused) {
      Serial.write(XOFF);
      senderPaused = true;
    }
  } // This brace correctly closes the 'while' loop

  // --- 2. PROCESS VFD (CONSUMER) ---
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
  } // This brace closes the 'if (bufHead != bufTail)' block

  // --- 3. BLINK LOGIC ---
  // This must be INSIDE the loop() function to work
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
