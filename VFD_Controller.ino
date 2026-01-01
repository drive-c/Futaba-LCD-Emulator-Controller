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
  // TODO: I have VFD modules that are set for English/Katakana, but the English/European-set modules have a different character map. 
  // The underscore (0x5F) is the same, but 0xFF, 0x10, and 0x11 are all different.
  // We *could* creator our own cursor character in the 0x0X space, but that would require more code.
  // Can we probe the VFD for which mode it's in?

// --- HELPER: Advances the index of the array ---
inline int nextIndex(int index) {   // Take the index for the array and move to the next item. 
  return (index + 1) % BUF_SIZE;    // Return index + 1, but account for buffer rollover.
  }

// Function: Move to next line and first column
void moveToNextLine () {
  sendVFD(0x80 | 0x40, false); // Send commands 0x80 and 0x40
} 

void updateSerialBuffer(){
  while (Serial.available() > 0) {      // Do while bytes are waiting for serial buffer
    char c = Serial.read();             // Take a single character 
    int nextHead = nextIndex(bufHead);  // Find the position to put the character in the buffer

    if (nextHead != bufTail) {      // Check that the nextHead isn't going to overflow
      rxBuffer[bufHead] = c;        // Store the character in the rxBuffer array
      bufHead = nextHead;           // Advance the bufHead pointer to the next position
    }
    // Check for XOFF pause
    // Figure out how many characters are in the buffer.
    // Set bufferCount. If bufHead is >= bufTail, set bufferCount to bufHead - bufTail. If it has wrapped around, set to (BUF_SIZE - bufTail + bufHead).
    int bufferCount = (bufHead >= bufTail) ? (bufHead - bufTail) : (BUF_SIZE - bufTail + bufHead);
    if (bufferCount > HIGH_WATER_MARK && !senderPaused) {         // If bufferCount exceeds our high-watermark and senderPaused is not true...
      Serial.write(XOFF);                                         // Pause serial flow by setting to XOFF
      senderPaused = true;                                        // Set our senderPaused tracking bool to true.
    }
  }
}

// --- CURSOR BLINK LOGIC ---
void blinkLogic() {
  unsigned long currentMillis = millis();     // Check how long since this function started
  if (currentMillis - previousMillis >= blinkInterval) { // If it's been longer than the blinkInterval...
    previousMillis = currentMillis;           // Reset the timer
    cursorVisible = !cursorVisible;           // Toggle cursor visibility.

    if (cursorVisible) {                      // While the cursor is visible...
      sendVFD(cursorChar, true);              // Send the cursor character as a command
    } else {                                  // Otherwise...
      sendVFD(' ', true);                     // Send a blank space as a command... We could change this to be a different symbol, if we liked...
    }
    sendVFD(CMD_CURSOR_LEFT, false);          // This keeps the cursor in the same space instead of moving to the next space after each write.
  }
}

// --- STARTUP SEQUENCE ---
void startupSequence() {
  byte animChars[] = { 0x20, 0x10, 0x11, 0x12, 0x13, 0x14 };  // Set the following characters from the map: blank space, 1/5, 2/5, 3/5, 4/5, and 5/5 blocks.
    // TODO: Like the cursor, these are different on the English/European character map.
    // There is no equivalent for the blocks, so maybe we use the 0x0X custom character space?
    // Could see if we can probe for character type.
  sendVFD(CMD_CLEAR, false);                                  // Clear the display.
  delay(1);                                                   // Wait 1ms for display matrix to clear.
  for (int row = 0; row < MAX_LINES; row++) {                 // Do this for each line available to us...
    byte lineStart = (row == 0) ? 0x80 : (0x80 | 0x40);       // If row is 0, set to the first space in first row (0x80), if not set to first space in first row, then jump to second row.
    sendVFD(lineStart, false);                                // Send whichever of the commands we set
    for (int col = 0; col < MAX_COLS; col++) {                // For each one of these columns...
      for (int i = 0; i < 5; i++) {                           // Increment through the 6 characters that we set in animChars[].
        sendVFD(animChars[i], true);                          // Send index of animChars.
        sendVFD(CMD_CURSOR_LEFT, false);                      // Set the cursor back to the same spot.
        delay(1);                                             // Wait 1ms for VFD to do this action.
      }
      sendVFD(animChars[5], true);                            // Keep the last character on that spot and advance to next spot.
      delay(5);                                               // Wait 5ms for VFD to advance.
    }
  }
  delay(1000);                                                 // Keep the full bars on screen for 1 second.
}

// Function: Move to a new line
void handleNewline() {
  sendVFD(' ', true);               // Send a space to the display
  sendVFD(CMD_CURSOR_LEFT, false);  // Tell the cursor to go back one space
  if (currentLine == 0) {           // If we are on line 0...
    currentLine = 1;                // Set line to 1
    currentCol = 0;                 // Set column to 0
    moveToNextLine();               // Move the cursor to next line
  } else {
    scrollDisplayUp();
  }
}

// Function: Write Character to a Screen
void writeChar(char c) {
  if (currentCol >= MAX_COLS) {     // If we are out of open columns...
    if (currentLine == 0) {         // If the current line is 0...
      currentLine = 1;              // Set the line to 1
      currentCol = 0;               // Set the column to 0
      moveToNextLine();             // Send command to move to next line
    } else {                        // If the line is greater than 0...
      scrollDisplayUp();            // Scroll the display up
    }
  }
  if (currentCol == 0) {            // If the current column is 0...
    byte addr = (currentLine == 0) ? 0x80 : (0x80 | 0x40);  // Set the byte address to match its current position.
    sendVFD(addr, false);           // Send the address that we just set.
  }
  screenBuffer[currentLine][currentCol] = c;  // Set the character for the current screenBuffer array position
  sendVFD(c, true);                           // Send the character.
  currentCol++;                               // Advance the column count
}

// Function: Scroll the display up a line
void scrollDisplayUp() {
  for (int i = 0; i < MAX_COLS; i++) {        // For each of the available columns...
    screenBuffer[0][i] = screenBuffer[1][i];  // Put the data from line 1, column i into the screenBuffer
    screenBuffer[1][i] = ' ';                 // Set line 1 to blank.
  }
  sendVFD(CMD_CLEAR, false);                  // Clear the screen
  delay(5);                                   // Wait 5ms
  for (int i = 0; i < MAX_COLS; i++) {        // For each of the available columns...
    sendVFD(screenBuffer[0][i], true);        // Send the screenBuffer to line 0
    delayMicroseconds(50);                    // Wait 50 microseconds for VFD to process
  }
  moveToNextLine();                           // After all that is complete, move the cursor to line 1
  currentLine = 1;                            // Set the currentLine to 1
  currentCol = 0;                             // set currentCol to 0
}

// Function: Clear the screen buffer
void clearScreenBuffer() {
  for (int y = 0; y < MAX_LINES; y++) {       // For each of the lines...
    for (int x = 0; x < MAX_COLS; x++) {      // For each of the columns...
      screenBuffer[y][x] = ' ';               // Set each to a blank space
    }
  }
  currentLine = 0;                            // Set currentLine to 0
  currentCol = 0;                             // Set currentCol to 0
  sendVFD(CMD_CLEAR, false);                  // Tell the VFD to clear the screen.
  delay(5);                                   // Wait 5ms before proceeding.
}

// Function: Convert hex values for characters to a byte value for the serial connection
byte hexToVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';             // Handle 0-9 hex values
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;        // Handle A-F hex values, uppercase
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;        // Handle A-F hex values, lowercase
  return 0;                                             // Return 0 if invalid hex character
}

// Function: Send data to VFD module
void sendVFD(byte data, bool isData) {        
  byte startByte = isData ? 0xFA : 0xF8;      // isData: True to send character data, False to send control command
  digitalWrite(pinSTB, LOW);                  // Set the strobe pin to tell the VFD to prepare for data
  SPI.transfer(startByte);                    // Send the startByte
  SPI.transfer(data);                         // Send the data
  digitalWrite(pinSTB, HIGH);                 // Set the strobe to high to signal the transaction is complete
  delayMicroseconds(60);                      // Wait 60 microseconds for VFD to respond to data
}

// Function: Print character on VFD
void printVFD(const char* str) {    // Take the string
  while (*str) {                    // While the string has data
    writeChar(*str++);              // Increment the string data position
  }
}
// --- FUNCTION: Check if character is a valid hex entry ---
bool isHex(char c) {
  return ((c >= '0' && c <= '9') || 
          (c >= 'A' && c <= 'F') || 
          (c >= 'a' && c <= 'f'));
}

// --- LOGIC: Process 1 character from Buffer ---
void processBufferedChar(char c) {
  // --- Case 1: Check for possible slash commands ---
  if (c == '\\') {                  // If the character is a backslash...
    parseSlashCommand();            // Run it as a slash command
  }
  // --- CASE 2: Line Feed or Carriage Return ---
    // TODO: Not sure if we want to send CR and LF as commands...
    // Windows uses CRLF, but UNIX and Linux use LF, so Windows would likely do two lines for each Enter vs. macOS doing a single line for Return.
  else if (c == '\n' || c == '\r') {  // Check for \n and \r for new line and carriage return
    handleNewline();                // Run the new line function.
  }
  // --- CASE 3: Normal Typing ---
  else {                            // All other characters...
    writeChar(c);                   // Run writeChar function.
  }
}

// --- SLASH COMMAND PARSER ---
void parseSlashCommand() {
  String cmd = "";                    // Initialize cmd as a blank string.
  unsigned long startWait = millis(); // Start a timer since the function started.
  
  // 1. READ COMMAND
  while (true) {                      // We originally looked for command length, but we've removed it.
    if (cmd.length() > 10) {          // If we accidentally parse something as a command, we want to stop 
      Serial.println("This command is too long.");
      cmd = "";
      if (millis() - startWait > 3000) { // Timeout if longer than 3 seconds.
        Serial.println("Command timeout.");
        break; 
      }
      if (bufHead != bufTail) {          // If the buffer isn't empty...
        char trash = rxBuffer[bufTail]; // Get the character at the end of rxBuffer[]
        if (trash == ' ' || trash == '\n' || trash == '\r') { // Check for delimiters
          bufTail = nextIndex(bufTail); // Consume the delimiter
          break;
        }
        else if (trash == '\\') {        // We want to break if we see another backslash
          break;
        }
          bufTail = nextIndex(bufTail); 
        }
      updateSerialBuffer();
      }

    // Read from Buffer
    if (bufHead != bufTail) {         // If the buffer isn't empty...
      char nextC = rxBuffer[bufTail]; // Check the *next* character in the buffer.
      
      // Stop at delimiter
      if (nextC == ' ' || nextC == '\n' || nextC == '\r') { // If we see a space, carriage return, or line feed...
        bufTail = nextIndex(bufTail);           // Remove it from the buffer.
        break;                                  // Break out of the function.
      }
      
      cmd += nextC;                   // Add the next character to the cmd string.
      bufTail = nextIndex(bufTail);   // Remove the character from the buffer.
    }
    updateSerialBuffer();             // Keep filling the buffer.
  }

  // 2. CATCH LAGGY NEWLINES
  // Wait 10ms to ensure the \n part of a \r\n pair has time to arrive
  unsigned long cleanupStart = millis();          // Start tracking time since we got to this step.
  while(millis() - cleanupStart < 10) {           // If greater than 10ms...
    updateSerialBuffer();
       }

 
  // 3. REMOVE TRAILING CR and LF
  if (bufHead != bufTail) {                   // If the buffer isn't empty...
    char peek = rxBuffer[bufTail];            // Check the tail of the rxBuffer array
    if (peek == '\n' || peek == '\r') {       // If it is a CR or LF...
      bufTail = nextIndex(bufTail);           // Remove the character from the buffer.
    }
  }
  
  // 4. EXECUTE COMMANDS
  if (cmd.equalsIgnoreCase("c")) {            // If the command is \c...
    clearScreenBuffer();                      // Clear the screen.
  }
  else if (cmd.equalsIgnoreCase("n")) {       // If the command is \n...
    handleNewline();                          // Move to a new line.
  }
  // If the command is \h and there are two more characters...
  else if ((cmd.charAt(0) == 'h' || cmd.charAt(0) == 'H') && cmd.length() == 3) {
      char h = cmd.charAt(1);                   // Set higher nibble to h
      char l = cmd.charAt(2);                   // Set lower nibble to l
      if (isHex(h) && isHex(l)) {
        byte rawByte = (hexToVal(h) << 4) + hexToVal(l); // Move the higher nibble left and fill the remaining space with lower nibble
        writeChar(rawByte);                     // Write the rawByte value as the character to display.
      }
      else{
        Serial.println("Invalid hex code.");
      }
    }
  else {
    Serial.println("Invalid command.");
  }
}
  
int bufferCounter(int currentBufHead, int currentBufTail) {
  int bufferCount = (currentBufHead - currentBufTail + BUF_SIZE) % BUF_SIZE;
  return bufferCount;
}

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
  sendVFD(CMD_DISPLAY_ON, false);   // Sets display and hardware cursor mode (on, no cursor or blink).
  delayMicroseconds(150);           // Wait 150 microseconds for VFD to receive command.
  sendVFD(CMD_ENTRY_MODE, false);   // Sets character input mode (increment, no shift)
  delayMicroseconds(150);           // Wait 150 microseconds for VFD to receive command.

  // --- VFD DISPLAY STARTUP SEQUENCE ---
  startupSequence();                // Show the startup animation.
  clearScreenBuffer();              // Clear the screen after the startup animation completes.
  printVFD("Ready:");               // Print "Ready:" on the VFD display.

  // --- DISPLAY COMMANDS IN TERMINAL EMULATOR ---
    // TODO: Consider shorter commands or contol keys
  Serial.println("Ready for input.");                             // Show that we are ready to accept input
  Serial.println(" Commands:");                                   // List the command hints
  Serial.println(" \\c : Clear Screen");
  Serial.println(" \\n : New Line");
  Serial.println(" \\h : Table Characters e.g. \\h5C for ¥.");
}

void loop() {
  unsigned long currentMillis = millis(); // We need to track time for cursor and character draw speed.
  updateSerialBuffer();
  
  // --- 1. FILL CHARACTER BUFFER ---
  while (Serial.available() > 0) {  // Do while bytes are waiting for serial buffer
    char c = Serial.read();         // Take a single character 
    int nextHead = (bufHead + 1) % BUF_SIZE;  // Find the position to put the character in the buffer

    if (nextHead != bufTail) {      // Check that the nextHead isn't going to overflow
      rxBuffer[bufHead] = c;        // Store the character in the rxBuffer array
      bufHead = nextHead;           // Advance the bufHead pointer to the next position
    }

    // Check for XOFF pause
      // Figure out how many characters are in the buffer.
      // Set bufferCount. If bufHead is >= bufTail, set bufferCount to bufHead - bufTail. If it has wrapped around, set to (BUF_SIZE - bufTail + bufHead).
    int bufferCount = bufferCounter(bufHead, bufTail);
    if (bufferCount > HIGH_WATER_MARK && !senderPaused) {         // If bufferCount exceeds our high-watermark and senderPaused is not true...
      Serial.write(XOFF);                                         // Pause serial flow by setting to XOFF
      senderPaused = true;                                        // Set our senderPaused tracking bool to true.
    }
  }

  // --- 2. SEND BUFFERED CHARACTERS  ---
  if (bufHead != bufTail) {                                       // Make sure the buffer isn't empty by seeing if bufHead and bufTail don't match
    if (currentMillis - lastCharMillis >= typingDelay) {          // Make sure we've waited time equal to the typingDelay time.
      lastCharMillis = currentMillis;                             // Reset typing delay timer.
      previousMillis = currentMillis;                             // Reset the blinking cursor timer.
      cursorVisible = true;                                       // Show the cursor.

      char c = rxBuffer[bufTail];                                 // Get the next character from the buffer.
      bufTail = (bufTail + 1) % BUF_SIZE;                         // Move the tail pointer to the next spot and ensure rollover.

      processBufferedChar(c);                                     // Process the character.

      // Check for XON resume
        // TODO: Change bufferCount check to a function
      int bufferCount = bufferCounter(bufHead, bufTail);
      if (bufferCount < LOW_WATER_MARK && senderPaused) {         // If we drop below our low-watermark and senderPaused is true...
        Serial.write(XON);                                        // Resume serial flow by setting to XON
        senderPaused = false;                                     // Set our senderPased tracking bool to true.
      }
    }
  }

  // --- LOGIC: CURSOR BLINKING ---
  if (bufHead == bufTail) {         // If the buffer is empty...
    blinkLogic();                   // Start the cursor blinking again.
  }
}
