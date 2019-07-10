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

const long utcOffsetInSeconds = -18000;

char daysOfTheWeek[7][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

Metro npt_timer = Metro(5000);

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String curtain_state = "off";

// Assign output variables to GPIO pins
const int output5 = BUILTIN_LED;

void setup() {
	Serial.begin(115200);

	// Initialize the output variables as outputs
	pinMode(output5, OUTPUT);
	// Set outputs to LOW
	digitalWrite(output5, LOW);

	// Connect to Wi-Fi network with SSID and password
	Serial.print("Connecting to ");
	Serial.println(WLAN_SSID);
	WiFi.begin(WLAN_SSID, WLAN_PASS);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	// Print local IP address and start web server
	Serial.println("");
	Serial.println("WiFi connected.");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	server.begin();

	timeClient.begin();
}

void loop() {
	if (npt_timer.check()) {
		GetTime();
	}
	WebPageServer();
}

void GetTime() {
	timeClient.update();

	Serial.print(daysOfTheWeek[timeClient.getDay()]);
	Serial.print(", ");
	Serial.print(timeClient.getHours());
	Serial.print(":");
	Serial.print(timeClient.getMinutes());
	Serial.print(":");
	Serial.println(timeClient.getSeconds());
	//Serial.println(timeClient.getFormattedTime());
}

void WebPageServer() {
	WiFiClient client = server.available();   // Listen for incoming clients

	if (client) {                             // If a new client connects,
		Serial.println("New Client.");          // print a message out in the serial port
		String currentLine = "";                // make a String to hold incoming data from the client
		while (client.connected()) {            // loop while the client's connected
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

						// turns the GPIOs on and off
						if (header.indexOf("GET /5/on") >= 0) {
							Serial.println("curtain open");
							curtain_state = "open";
							digitalWrite(output5, HIGH);
						} else if (header.indexOf("GET /5/off") >= 0) {
							Serial.println("curtain closed");
							curtain_state = "closed";
							digitalWrite(output5, LOW);
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
						// If the curtain_state is off, it displays the ON button       
						if (curtain_state == "closed") {
							client.println("<p><a href=\"/5/on\"><button class=\"button\">OPEN</button></a></p>");
						} else {
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