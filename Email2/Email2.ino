/*
    Email Notification 1.0 (esp8266)

    Processes the security system status and demonstrates how to send an email when the status has changed.  Configure
    the email SMTP server settings in sendMail().

    Email is sent using SMTPS (port 465) with SSL for encryption - this is necessary on the ESP8266 until STARTTLS can
    be supported.  For example, this will work with Gmail after changing the account settings to allow less secure
    apps: https://support.google.com/accounts/answer/6010255

    Wiring:
        DSC Aux(-) --- esp8266 ground

                                           +--- dscClockPin (esp8266: D1, D2, D8)
        DSC Yellow --- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground

                                           +--- dscReadPin (esp8266: D1, D2, D8)
        DSC Green ---- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground

    Virtual keypad (optional):
        DSC Green ---- NPN collector --\
                                        |-- NPN base --- 1k ohm resistor --- dscWritePin (esp8266: D1, D2, D8)
              Ground --- NPN emitter --/

    Power (when disconnected from USB):
        DSC Aux(+) ---+--- 5v voltage regulator --- esp8266 development board 5v pin (NodeMCU, Wemos)
                      |
                      +--- 3.3v voltage regulator --- esp8266 bare module VCC pin (ESP-12, etc)

    Virtual keypad uses an NPN transistor to pull the data line low - most small signal NPN transistors should
    be suitable, for example:
     -- 2N3904
     -- BC547, BC548, BC549

    Issues and (especially) pull requests are welcome:
    https://github.com/taligentx/dscKeybusInterface

    This example code is in the public domain.
*/

#include <ESP8266WiFi.h>
#include <dscKeybusInterface.h>

const char* wifiSSID = "smdnetwifi";
const char* wifiPassword = "saNdu1988solymar";

bool ledOn = false;
long lastBeep = 0;
const short int BUILTIN_LED2 = 16;//GPIO16
const short int BEEP_THRESHOLD = 1000;


WiFiClient client;

// Configures the Keybus interface with the specified pins.
#define dscClockPin D2  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
#define dscReadPin D1  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
#define dscWritePin D8  // esp8266: D1, D2, D8 (GPIO 5, 4, 15)
dscKeybusInterface dsc(dscClockPin, dscReadPin, dscWritePin);


void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  pinMode(BUILTIN_LED2, OUTPUT); // Initialize the BUILTIN_LED2 pin as an output

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) delay(100);
  Serial.println(F("WiFi connected"));
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("Gateway: "));
  Serial.println(WiFi.gatewayIP());
  Serial.print(F("DNS: "));
  Serial.println(WiFi.dnsIP());

  // Sends an email on startup to verify connectivity
  if (sendMail("Security system initializing", "")) Serial.println(F("Initialization email sent successfully."));
  else Serial.println(F("Initialization email failed to send."));

  // Starts the Keybus interface
  dsc.begin();

  Serial.println(F("DSC Keybus Interface is online."));
}

void dscLoop() {
  if (dsc.handlePanel() && dsc.statusChanged) {  // Processes data only when a valid Keybus command has been read
    
    dsc.statusChanged = false;                   // Resets the status flag

    // If the Keybus data buffer is exceeded, the sketch is too busy to process all Keybus commands.  Call
    // handlePanel() more often, or increase dscBufferSize in the library: src/dscKeybusInterface.h
    if (dsc.bufferOverflow) Serial.println(F("Keybus buffer overflow"));
    dsc.bufferOverflow = false;

    if (dsc.keypadFireAlarm) {
      dsc.keypadFireAlarm = false;  // Resets the keypad fire alarm status flag
      sendMail("Security system fire alarm button pressed", "");
    }

    if (dsc.keypadAuxAlarm) {
      dsc.keypadAuxAlarm = false;  // Resets the keypad auxiliary alarm status flag
      sendMail("Security system aux alarm button pressed", "");
    }

    if (dsc.keypadPanicAlarm) {
      dsc.keypadPanicAlarm = false;  // Resets the keypad panic alarm status flag
      sendMail("Security system panic alarm button pressed", "");
    }

    if (dsc.powerChanged) {
      dsc.powerChanged = false;  // Resets the power trouble status flag
      if (dsc.powerTrouble) sendMail("Security system AC power trouble", "");
      else sendMail("Security system AC power restored", "");
    }

    // Checks status per partition
    for (byte partition = 0; partition < dscPartitions; partition++) {

      if (dsc.alarmChanged[partition]) {
        dsc.alarmChanged[partition] = false;  // Resets the partition alarm status flag

        char emailBody[12] = "Partition ";
        char partitionNumber[2];
        itoa(partition + 1, partitionNumber, 10);
        strcat(emailBody, partitionNumber);

        if (dsc.alarm[partition]) sendMail("Security system in alarm", emailBody);
        else sendMail("Security system disarmed after alarm", emailBody);
      }

      if (dsc.armedChanged[partition]) {
        dsc.armedChanged[partition] = false;  // Resets the partition alarm status flag

        char emailBody[12] = "Partition ";
        char partitionNumber[2];
        itoa(partition + 1, partitionNumber, 10);
        strcat(emailBody, partitionNumber);

        if (dsc.armed[partition]) sendMail("Security system is armed normal", emailBody);
        else if (dsc.armedAway[partition]) sendMail("Security system is armed away", emailBody);
        else if (dsc.armedStay[partition]) sendMail("Security system is armed stay", emailBody);
        else sendMail("Security system disarmed", emailBody);
      }

      if (dsc.fireChanged[partition]) {
        dsc.fireChanged[partition] = false;  // Resets the fire status flag

        char emailBody[12] = "Partition ";
        char partitionNumber[2];
        itoa(partition + 1, partitionNumber, 10);
        strcat(emailBody, partitionNumber);

        if (dsc.fire[partition]) sendMail("Security system fire alarm", emailBody);
        else sendMail("Security system fire alarm restored", emailBody);
      }
    }
  }
}

// Prints a timestamp in seconds (with 2 decimal precision) - this is useful to determine when
// the panel sends a group of messages immediately after each other due to an event.
void printTimestamp() {
  float timeStamp = millis() / 1000.0;
  if (timeStamp < 10) Serial.print("    ");
  else if (timeStamp < 100) Serial.print("   ");
  else if (timeStamp < 1000) Serial.print("  ");
  else if (timeStamp < 10000) Serial.print(" ");
  Serial.print(timeStamp, 2);
  Serial.print(F(":"));
}

void loop() {
  dscLoop();
  if(millis() - lastBeep > BEEP_THRESHOLD) {
    if(ledOn) {
      ledOn = false;
      Serial.println("pong");
    } else {
      ledOn = true;
      Serial.println("ping");
    }
    digitalWrite(BUILTIN_LED2, ledOn);
    lastBeep = millis();
  }

  if (dsc.handlePanel()) {

    // If the Keybus data buffer is exceeded, the sketch is too busy to process all Keybus commands.  Call
    // handlePanel() more often, or increase dscBufferSize in the library: src/dscKeybusInterface.h
    if (dsc.bufferOverflow) Serial.println(F("Keybus buffer overflow"));
    dsc.bufferOverflow = false;

    // Prints panel data
    printTimestamp();
    Serial.print(" ");
    dsc.printPanelBinary();   // Optionally prints without spaces: printPanelBinary(false);
    Serial.print(" [");
    dsc.printPanelCommand();  // Prints the panel command as hex
    Serial.print("] ");
    dsc.printPanelMessage();  // Prints the decoded message
    Serial.println();

    // Prints keypad and module data when valid panel data is printed
    if (dsc.handleModule()) {
      printTimestamp();
      Serial.print(" ");
      dsc.printModuleBinary();   // Optionally prints without spaces: printKeybusBinary(false);
      Serial.print(" ");
      dsc.printModuleMessage();  // Prints the decoded message
      Serial.println();
    }
  }

  // Prints keypad and module data when valid panel data is not available
  else if (dsc.handleModule()) {
    printTimestamp();
    Serial.print(" ");
    dsc.printModuleBinary();  // Optionally prints without spaces: printKeybusBinary(false);
    Serial.print(" ");
    dsc.printModuleMessage();
    Serial.println();
  }
  
}

byte eRcv()
{
  byte respCode;
  byte thisByte;
  int loopCount = 0;
  while (!client.available())
  {
    delay(1);
    loopCount++;     // if nothing received for 10 seconds, timeout
    if (loopCount > 10000) {
      client.stop();
      Serial.println(F("\r\nTimeout"));
      return 0;
    }
  }

  respCode = client.peek();
  while (client.available())
  {
    thisByte = client.read();
    Serial.write(thisByte);
  }

  if (respCode >= '4')
  {
    //  efail();
    return 0;
  }
  return 1;
}

bool sendMail(const char* emailSubject, const char* emailBody) {
  byte thisByte = 0;
  byte respCode;

  if (client.connect("192.168.1.2", 25) == 1) {
    Serial.println(F("connected"));
  } else {
    Serial.println(F("connection failed"));
    return 0;
  }
  if (!eRcv()) return 0;

  Serial.println(F("Sending EHLO"));
  client.println("EHLO DSC");

  if (!eRcv()) return 0;
  Serial.println(F("Sending From"));   // change to your email address (sender)
  client.println(F("MAIL From: backupjmsmuy@gmail.com"));// not important

  if (!eRcv()) return 0;   // change to recipient address
  Serial.println(F("Sending To"));
  client.println(F("RCPT To: jmsmuy@gmail.com"));

  if (!eRcv()) return 0;
  Serial.println(F("Sending DATA"));
  client.println(F("DATA"));

  if (!eRcv()) return 0;
  Serial.println(F("Sending email"));   // change to recipient address
  client.println(F("To: jmsmuy@gmail.com"));   // change to your address
  client.println(F("From: backupjmsmuy@gmail.com"));
  client.print(F("Subject: "));
  client.print(emailSubject);
  client.println(F("\r\n"));

  client.println(emailBody);
  client.println(F("."));


  if (!eRcv()) return 0;
  Serial.println(F("Sending QUIT"));
  client.println(F("QUIT"));

  if (!eRcv()) return 0;
  client.stop();
  Serial.println(F("disconnected"));
  return 1;

}
