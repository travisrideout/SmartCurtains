/*
 Name:		SmartCurtains.ino
 Created:	7/9/2019 7:33:52 PM
 Author:	Travi
*/

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Rideout.h>
#include <Metro.h>
#include <ArduinoOTA.h>
#include <AccelStepper.h>
#include <Timezone.h>

/*
TODO:

DST offset
ICON png
Is_opening and is_closing states
Show time
Open in AM
Auto update pages

*/

const long utcOffsetInSeconds = 0;	// -18000;
// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = { "EDT", Second, Sun, Mar, 2, -240 };  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = { "EST", First, Sun, Nov, 2, -300 };   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

char daysOfTheWeek[7][12] = { 
	"Sunday", "Monday", "Tuesday", 
	"Wednesday", "Thursday", "Friday", "Saturday" };

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

Metro npt_timer = Metro(5000);
Metro motor_running_timer = Metro(15000);

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String curtain_state = "open";

// Assign output variables to GPIO pins
const int output5 = BUILTIN_LED;
const int STEP_PIN = 4;
const int DIR_PIN = 5;
const int EN_PIN = 12;
const int MIN_PULSE_WIDTH = 25;	//microseconds

bool is_motor_running = false;
bool is_faulted = false;
bool morning_open = false;

AccelStepper motor(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
	

void setup() {
	Serial.begin(115200);

	// Initialize the output variables as outputs
	pinMode(output5, OUTPUT);
	pinMode(STEP_PIN, OUTPUT);
	pinMode(DIR_PIN, OUTPUT);
	pinMode(EN_PIN, OUTPUT);
	// Set outputs to LOW
	digitalWrite(output5, HIGH);
	digitalWrite(STEP_PIN, LOW);
	digitalWrite(DIR_PIN, LOW);
	digitalWrite(EN_PIN, LOW);

	motor.setEnablePin(EN_PIN);
	motor.setPinsInverted(true, false, false);
	motor.setMinPulseWidth(MIN_PULSE_WIDTH);
	motor.setMaxSpeed(4000);
	motor.setAcceleration(3000); 
	motor.disableOutputs();

	// Connect to Wi-Fi network with SSID and password
	Serial.print("Connecting to ");
	Serial.println(WLAN_SSID);
	WiFi.mode(WIFI_STA);
	WiFi.begin(WLAN_SSID, WLAN_PASS);
	IPAddress ip(192, 168, 200, 152);
	IPAddress gateway(192, 168, 200, 1);
	IPAddress subnet(255, 255, 255, 0);
	WiFi.config(ip, gateway, subnet);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	//OTA Functions
	ArduinoOTA.onStart([]() {
		Serial.println("Start");
		});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
		});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
		});
	ArduinoOTA.begin();

	// Print local IP address and start web server
	Serial.println("");
	Serial.println("WiFi connected.");
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
	server.begin();

	timeClient.begin();
}

void loop() {
	if (npt_timer.check()) {
		GetTime();
	}
	WebPageServer();
	ArduinoOTA.handle();
	MotorControl();
}

void GetTime() {
	timeClient.update();

	if (usET.utcIsDST(timeClient.getEpochTime())) {
		timeClient.setTimeOffset(usEDT.offset*60);
		Serial.print("In Daylight savings time: ");
	} else {
		timeClient.setTimeOffset(usEST.offset*60);
		Serial.print("In standard time: ");
	}

	if (timeClient.getHours() > 6 && curtain_state == "closed"
		&& !is_faulted && !morning_open) {
		Serial.println("Goodmorning! Opening curtains");
		OpenCurtains();
		morning_open = true;
	} else if (timeClient.getHours() == 24 && morning_open) {
		morning_open = false;
	}
	
	Serial.print(daysOfTheWeek[timeClient.getDay()]);
	Serial.print(", ");
	Serial.println(timeClient.getFormattedTime());
}

void WebPageServer() {
	WiFiClient client = server.available();   // Listen for incoming clients

	if (client) {                             // If a new client connects,
		Serial.println("New Client.");          // print a message out in the serial port
		String currentLine = "";                // make a String to hold incoming data from the client
		while (client.connected()) {            // loop while the client's connected
			MotorControl();
			if (client.available()) {             // if there's bytes to read from the client,
				char c = client.read();             // read a byte, then
				Serial.write(c);                    // print it out the serial monitor
				header += c;
				if (c == '\n') {                    // if the byte is a newline character
				  // if the current line is blank, you got two newline characters in a row.
				  // that's the end of the client HTTP request, so send a response:
					if (currentLine.length() == 0) {
						// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
						// and a content-type so the client knows what's coming, then a blank line:
						client.println("HTTP/1.1 200 OK");
						client.println("Content-type:text/html");
						client.println("Connection: close");
						client.println();

						// Opens or closes the curtains
						if (!is_faulted && header.indexOf("GET /5/on") >= 0) {
							OpenCurtains();
						} else if (header.indexOf("GET /5/off") >= 0) {
							CloseCurtains();
						} else if (header.indexOf("GET /5/fault") >= 0) {
							Serial.println("curtain is on fault page");
							is_faulted = false;
							curtain_state = "open";
						}

						// Display the HTML web page
						client.println("<!DOCTYPE html><html>");
						client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
						client.println("<link rel=\"icon\" href=\"data:,\">");
						// CSS to style the on/off buttons 
						// Feel free to change the background-color and font-size attributes to fit your preferences
						client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
						client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
						client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
						client.println(".button2 {background-color: #77878A;}</style></head>");

						// Web Page Heading
						client.println("<body><h1>Livingroom Curtain</h1>");

						// Display current state, 
						client.println("<p>Curtain is " + curtain_state + "</p>");
						String the_day = daysOfTheWeek[timeClient.getDay()];
						client.println("<p>Time is " + the_day + ", " + timeClient.getFormattedTime() + "</p>");	//   
						// If the curtain_state is off, it displays the ON button 
						if (is_faulted) {
							client.println("<p><a href=\"/5/fault\"><button class=\"button\">RESET</button></a></p>");
						} else if (curtain_state == "closed") {
							client.println("<p><a href=\"/5/on\"><button class=\"button\">OPEN</button></a></p>");
						} else if(curtain_state == "open"){
							client.println("<p><a href=\"/5/off\"><button class=\"button button2\">CLOSE</button></a></p>");
						}
												
						client.println("</body></html>");

						// The HTTP response ends with another blank line
						client.println();
						// Break out of the while loop
						break;
					} else { // if you got a newline, then clear currentLine
						currentLine = "";
					}
				} else if (c != '\r') {  // if you got anything else but a carriage return character,
					currentLine += c;      // add it to the end of the currentLine
				}
			}
		}
		// Clear the header variable
		header = "";
		// Close the connection
		client.stop();
		Serial.println("Client disconnected.");
		Serial.println("");
	}
}

void MotorControl() {
	motor.run();
	if (is_motor_running && motor.distanceToGo() == 0) {
		is_motor_running = false;
		Serial.println("At Target");
		motor.disableOutputs();
		digitalWrite(output5, HIGH);
	}

	if (is_motor_running && motor_running_timer.check()) {
		is_faulted = true;
		is_motor_running = false;
		Serial.println("Motor faulted");
		curtain_state = "faulted";
		motor.disableOutputs();
		digitalWrite(output5, HIGH);
	}
}

void OpenCurtains() {
	Serial.println("curtain open");
	curtain_state = "open";
	digitalWrite(output5, LOW);
	motor.enableOutputs();
	delay(25);
	is_motor_running = true;
	motor_running_timer.reset();
	motor.moveTo(0);
}

void CloseCurtains() {
	if (!is_faulted) {
		Serial.println("curtain closed");
		curtain_state = "closed";
		digitalWrite(output5, LOW);
		motor.enableOutputs();
		delay(25);
		is_motor_running = true;
		motor_running_timer.reset();
		motor.moveTo(-17000);
	}
}
	