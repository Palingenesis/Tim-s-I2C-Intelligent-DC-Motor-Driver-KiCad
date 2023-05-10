
/*
	A small program to send commands via Serial to Tim's I2C Intelligent DC Motor Driver.

	By Tim Jackson.1960
		More info on Instructables. https://www.instructables.com/member/Palingenesis/instructables/

	This Sketch was written for the Arduino Nano 33 IoT. But should work on any Arduino Device.
	Note if using a web server, it takes ages to load.

	Arduino SAMD21. Arduino Nano 33 IoT (NINA)
		Sketch uses 33956 bytes (12%) of program storage space. Maximum is 262144 bytes.
		Global variables use 4960 bytes (15%) of dynamic memory, leaving 27808 bytes for local variables. Maximum is 32768 bytes.


	Wire request buffer of the Motor Driver was originaly set/configured for 32 bit values but was reduced to 24+ bit numbers.
	The original code is commented out should I change microcontroller with more memory.
	I say 24+ bit number becouse I have used a seperate Byte for negative flags.
	This gives a number range of: -16,777,215 to 16,777,215.
	To make it more universal, all values are Ticks of the Motors Quadratic Encoder.

	Wire_Request converts as follows:

		1st byte Flags_1 true if:
			LSB		1	=	CurPossition < 0
					2	=	MaxPossition < 0
					4	=	MinPossition < 0
					8	=	Station_F < 0
					16	=	Station_R < 0
					32	=	HasMAX
					64	=	HasMIN
			MSB		128	=	HasStaton

		2nd byte Flags_2 true if:
			LSB		1	=	MIN_Hit
					2	=	MAX_Hit
					4	=
					8	=
					16	=
					32	=
					64	=
			MSB		128	=

		bytes 3 to 6 Current position.
			CurPossition	LSB1
			CurPossition	LSB2
			CurPossition	MSB1
			CurPossition	MSB2
		byte 7 I2C Addres of device. (The Address Arduino uses to talk to the device)
			My_I2C_Address
		bytes 8 and 9 Motor Slowdown point.
			Motor_Slowdown_point LSB
			Motor_Slowdown_point MSB
		bytes 10 to 13 Max position.
			MaxPossition	LSB1
			MaxPossition	LSB2
			MaxPossition	MSB1
			MaxPossition	MSB2
		bytes 14 to 17 Min position.
			MinPossition	LSB1
			MinPossition	LSB2
			MinPossition	MSB1
			MinPossition	MSB2
		bytes 18 to 21 Station Moving Forward position.
			Station_F		LSB1
			Station_F		LSB2
			Station_F		MSB1
			Station_F		MSB2
		bytes 22 to 25 Station Moving Reverse position.
			Station_R		LSB1
			Station_R		LSB2
			Station_R		MSB1
			Station_R		MSB2
		byte 26 Motor Speed Reduction percentage when comming to a stop.
			Motor_Speed_Reduction

	The buffer holds 32 bytes, the rest are not used.
		Currently hold data version an maker.


	Web Server.
	AP	= Access Point.
	WPA	= Wi-Fi Protected Access.
	WEP	= Wireless Emergency Alerts.

	When creating a web page, it should be split into sections no larger than 4000 bytes.

*/

/*	BEBUGING	*/
#define	DEBUG

#include <Wire.h>
#include <SPI.h>				/*	SPI is required to save data to external memory.	*/
#include <WiFiNINA.h>
#include "Simple_Web.h"

/*	WiFi	*/
char ssid[] = "Robot Arm";		/*	Server Name.					*/
char pass[] = "123456789";		/*	WPA (Wi-Fi Protected Access).	*/
int keyIndex = 0;				/*	WEP (Wireless Emergency Alerts).*/
int status = WL_IDLE_STATUS;	/*	Wi-Fi Status.					*/
IPAddress ip(192, 168, 50, 20);	/*	Web Page IP Address.			*/
WiFiServer server(80);			/*	Server Instance.				*/


//#define TARGET_I2C_ADDRESS	0x30    /*  0x30=48 0x60=96 0x32=50Target Address.	*/
int Target_I2C_Address = 0x30;

#define BUFF_64				64      /*  What is the longest message Arduino can store?	*/
#define BUFF_16				16
#define BUFF_24				24
#define BUFF_32				32

/*	SERIAL MESSAGES	*/
char Buffer_TX[BUFF_64];		/* message				*/
uint8_t Buffer_RX[BUFF_32];		/* message				*/
unsigned int no_data = 0;
unsigned int sofar;				/* size of Buffer_TX	*/
bool isComment = false;
bool procsessingString = false;
short cmd = -1;
bool slaveProcesing = false;
bool ProcsessingCommand = false;

/*	Debug	*/
long TimeNow = millis();
long Period = 1000;
long TimeOut = TimeNow + Period;

void setup() {
	/*
	  Start serial.
	*/
	Serial.begin(115200);
	/*
	  Start Wire.
		  Used only as Master atm. (address optional for master)
	*/
	Wire.begin();
	Wire.setClock(400000);
	Wire.onRequest(Wire_Request);

	/*	Debug	*/
	pinMode(LED_BUILTIN, OUTPUT);

	/*	Start Wi-Fi.	*/
	byte Attempts = 0;
	while (status != WL_AP_LISTENING && Attempts < 10) {
		WiFi.config(ip);
		status = WiFi.beginAP(ssid, pass);
		/*	Give it a few Seconds	*/
		delay(10000);
		Serial.println(Attempts);
		Attempts += 1;
	}
	/*	Start the Web Server on Port 80	*/
	if (status == WL_AP_LISTENING) {
		Serial.println("Wi-Fi Listening for a Connection");
		server.begin();
		Serial_Print_Wifi_Status();
	}

	
}

void loop() {
	/*
		Tick - Tock
	*/
	TimeNow = millis();
	/*
		Get next command when finish current command.
	*/
	if (!procsessingString) { ReadSerial(); }

	if (TimeNow > TimeOut) {
		TimeOut = TimeNow + Period;
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
	}
	/*
		Wi-Fi.
		Listen for Incoming Clients.
	*/
	WiFiClient client = server.available();
	/*	If a Client Connects.	*/
	if (client) {
		Serial.println("Client Connected");
		Do_Web_Request(client);
		Serial.println("Client Disconnected");
	}
}
/*
	Read Data in Serial Buffer if available.
*/
void ReadSerial() {
	/*
		Holder
	*/
	char c;
	/*
		Read in characters if available.
	*/
	if (Serial.available() > 0) {
		c = Serial.read();
		no_data = 0;
		/*
			Chech for newlines.
		*/
		if (c != '\r' || c != '\n') {
			/*
				Check for comments.
					These are used in G-Code.
			*/
			if (c == '(' || c == ';') {
				isComment = true;
			}
			/*
				If we're not in "isComment" mode, add it to our array.
			*/
			if (!isComment) {
				Buffer_TX[sofar++] = c;
			}
			/*
				End of isComment - start listening again.
			*/
			if (c == ')') {
				isComment = false;
			}
		}
	}
	else {
		no_data++;
		delayMicroseconds(100); // 100
		/*
			Process any code that has been recived
		*/
		if (sofar && (c == '\n' || c == '\r' || no_data > 100)) {
			no_data = 0;
			sofar--;

#ifdef PRIN_SERIAL
			Serial.println(Buffer_TX);
			delay(50);
#endif  //  PRIN_SERIAL

			procsessingString = true;
			processCommand();
			init_process_string();
		}
	}

}
/*
	Set ready for next command.
*/
void init_process_string() {

	for (byte i = 0; i < BUFF_64; i++) { Buffer_TX[i] = 0; }
	sofar = 0;
	procsessingString = false;
	isComment = false;
	cmd = -1;
	Wire.flush();
	Serial.flush();
	Serial.println("ok");
	delay(4);
}
/*
	Change command numbers to a long.
*/
long Parse_Number(char code, long val) {
	/*
		start at the beginning of Buffer_TX.
	*/
	char* ptr = Buffer_TX;
	/*
	  Go char to char through string.
	*/
	while ((long)ptr > 1 && (*ptr) && (long)ptr < (long)Buffer_TX + sofar) {
		/*
			if you find code as you go through string.
		*/
		if (*ptr == code) {
			/*
				convert the digits that follow into a long and return it.
			*/
			return atol(ptr + 1);
		}
		/*
			take a step from here to the next char after the next space.
		*/
		ptr = strchr(ptr, ' ') + 1;
	}
	/*
		If the end is reached and nothing found, return val.
	*/
	return val;
}
/*
	Process Commands.
*/
void processCommand() {

	uint32_t _Val = 0;
	uint8_t _Flag1 = 0;
	uint8_t _Flag2 = 0;

	/*	Get I2C Address if given. (defalt 0x30 or last sent)	*/
	Target_I2C_Address = Parse_Number('D', Target_I2C_Address);

	/*	get start of Buffer_TX (see what the command starts with)	*/
	char* ptr = Buffer_TX; // 

	/*
		======================
		Chose GET, Process or SEND
		======================
	*/
	if (*ptr == 'X') {
		/*	GET	*/
		int _numberOfBytes = Parse_Number('X', -1);
		if (_numberOfBytes == 0) {
			_numberOfBytes = BUFF_32;
		}

		/*	Clear Buffer	*/
		for (size_t i = 0; i < BUFF_32; i++) {
			Buffer_RX[i] = 0;
		}

		Wire.requestFrom(Target_I2C_Address, _numberOfBytes);
		delayMicroseconds(5);

		if (Wire.available()) {
			/*	Read Buffer	*/
			for (size_t i = 0; i < _numberOfBytes; i++) {
				uint8_t C = Wire.read();
				Buffer_RX[i] = C;
				//Serial.print(C);
				//Serial.print(", ");
			}
			//Serial.println();
		}
		/*	Buffer in Binary	*/
		if (_numberOfBytes > 7) {
			for (size_t i = BUFF_32; i > 0; i--) {

				Serial.print("Buffer_RX[");
				Serial.print(i - 1);
				Serial.print("] \tBIN = ");
				Serial.println(Buffer_RX[i - 1], BIN);
			}
		}


		/*
			===============
			Use and Format Info
			===============
		*/

		/*	Slave_Buffer_Tx[7] = Flags	*/
		_Flag1 = Buffer_RX[0];
		if (Buffer_RX[6] == 0) {
			Serial.println();
			Serial.print("No Address ");
			Serial.print(Target_I2C_Address, DEC);
			Serial.print(" (0x");
			Serial.print(Target_I2C_Address, HEX);
			Serial.print(") found.");
			Serial.println();
		}
		else {
			Serial.println();
			Serial.println("Formated:");
			Serial.println();

			/*	Currrent Possition	*/
			//_Val = Buffer_RX[5];
			//_Val = _Val << 0x08;
			_Val |= Buffer_RX[4];
			_Val = _Val << 0x08;
			_Val |= Buffer_RX[3];
			_Val = _Val << 0x08;
			_Val |= Buffer_RX[2];
			Serial.print("Currrent Position\tDEC = ");
			_Flag1 = (Buffer_RX[0] & 1);
			if (_Flag1 == 1) { Serial.print("-"); }
			else { Serial.print(" "); }
			Serial.println(_Val, DEC);

			/*	Limit Switches	*/
			_Flag2 = (Buffer_RX[1] & 1);
			if (_Flag2 == 1) { Serial.println("MIN Stop Hit"); }
			else { Serial.println("MIN OK"); }
			_Flag2 = (Buffer_RX[1] & 2);
			if (_Flag2 == 2) { Serial.println("MAX Stop Hit"); }
			else { Serial.println("MAX OK"); }

			if (_numberOfBytes > 7) {

				/*	My_I2C_Address		*/
				Serial.print("I2C Address\t\t\tDEC =  ");
				Serial.print(Buffer_RX[6], DEC);
				Serial.print("(HEX = 0x");
				Serial.print(Buffer_RX[6], HEX);
				Serial.println(")");

				/*  I2C Speed  */
				Serial.print("I2C Speed\t\t\tDEC =  ");
				_Flag2 = (Buffer_RX[1] & 4);
				if (_Flag2 == 4) { Serial.println("400000"); }
				else { Serial.println("100000"); }


				/*	Motor_Slowdown		*/
				_Val = Buffer_RX[8];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[7];
				Serial.print("Motor Slowdown\t\tDEC =  ");
				Serial.println(_Val);

				/*	MaxPossition		*/
				//_Val = Buffer_RX[12];
				//_Val = _Val << 0x08;
				_Val = Buffer_RX[11];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[10];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[9];
				Serial.print("Max Possition    \tDEC = ");
				_Flag1 = (Buffer_RX[0] & 2);
				if (_Flag1 == 2) { Serial.print("-"); }
				else { Serial.print(" "); }
				Serial.println(_Val, DEC);

				/*	MinPossition		*/
				//_Val = Buffer_RX[16];
				//_Val = _Val << 0x08;
				_Val = Buffer_RX[15];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[14];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[13];
				Serial.print("Min Possition    \tDEC = ");
				_Flag1 = (Buffer_RX[0] & 4);
				if (_Flag1 == 4) { Serial.print("-"); }
				else { Serial.print(" "); }
				Serial.println(_Val, DEC);

				/*	Station F		*/
				//_Val = Buffer_RX[20];
				//_Val = _Val << 0x08;
				//_Val |= Buffer_RX[19];
				//_Val = _Val << 0x08;
				_Val = Buffer_RX[18];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[17];
				Serial.print("Station Forward\t\tDEC = ");
				_Flag1 = (Buffer_RX[0] & 8);
				if (_Flag1 == 8) { Serial.print("-"); }
				else { Serial.print(" "); }
				Serial.println(_Val, DEC);

				/*	Station R		*/
				//_Val = Buffer_RX[24];
				//_Val = _Val << 0x08;
				//_Val |= Buffer_RX[23];
				//_Val = _Val << 0x08;
				_Val = Buffer_RX[22];
				_Val = _Val << 0x08;
				_Val |= Buffer_RX[21];
				Serial.print("Station Reverse\t\tDEC = ");
				_Flag1 = (Buffer_RX[0] & 16);
				if (_Flag1 == 16) { Serial.print("-"); }
				else { Serial.print(" "); }
				Serial.println(_Val, DEC);

				/*	HasFlags		*/
				_Flag1 = (Buffer_RX[0] & 32);
				if (_Flag1 == 32) { Serial.println("\tHasMAX       = true"); }
				else { Serial.println("\tHasMAX       = false"); }
				_Flag1 = (Buffer_RX[0] & 64);
				if (_Flag1 == 64) { Serial.println("\tHasMIN       = true"); }
				else { Serial.println("\tHasMIN       = false"); }
				_Flag1 = (Buffer_RX[0] & 128);
				if (_Flag1 == 128) { Serial.println("\tHasStation   = true"); }
				else { Serial.println("\tHasStation   = false"); }

				/*	Motor Speed Reduction	*/
				Serial.print("Motor Speed Reduction = ");
				Serial.print(Buffer_RX[25]);
				Serial.println("%");

				/*	Tim			*/
				for (size_t i = 29; i < 32; i++) {
					Serial.print((char)Buffer_RX[i]);
				}
				Serial.println();

			}

			delayMicroseconds(5);
		}
	}
	else if (*ptr == 'P') {

	}
	else {
		/*	SEND	*/
		ProcsessingCommand = true;
		Wire.flush();
		SendBufferOnI2C(Target_I2C_Address);
		delay(4);
		ProcsessingCommand = false;

	}
}
/*
	Send Recived Commands on to a Device on the I2C bus.
*/
void SendBufferOnI2C(int I2C_address) {

	Wire.beginTransmission(I2C_address);	//	Get device at I2C_address attention
	Wire.write(Buffer_TX, sofar);			//	Send Buffer_TX
	Wire.endTransmission();					//	Stop transmitting

#ifdef PRIN_SERIAL
	Serial.print("Buffer_TX ");
	Serial.print(Buffer_TX);
	Serial.print("\tsofar ");
	Serial.println(sofar);
	delay(50);
#endif  //  PRIN_SERIAL

}
/*
	I2C Request Data
*/
void Wire_Request() {

}

/*	=====	Server	=====	*/
/*
	Process Web Page.
*/
void Do_Web_Request(WiFiClient client) {

	String currentLine = "";
	String LineRecived = "";
	boolean EndOfRequest = false;
	boolean RefreshPage = true;
	while (client.connected()) {		/*	Loop while the client's connected.				*/
		/*
			A delay is required for the Arduino Nano RP2040 Connect
				- otherwise it will loop so fast that SPI will never be served.
		*/
		delayMicroseconds(10);


		if (client.available()) {		/*	If there's bytes to be read from the client.	*/
			char p = client.peek();		/*	Peek byte.										*/
			char c = client.read();		/*	Read byte.										*/
			//Serial.write(c);			/*	Print byte to the Serial monitor.				*/
			/*
				If the byte is a newline character.
			*/
			if (c == '\n') {
				/*
					If the current line is blank, you got two newline characters in a row.
					That's the end of the client HTTP request, so send a response:
				*/
				if (currentLine.length() == 0) {

					//if (LineRecived.startsWith("GET / ")) {
					//	RefreshPage = true;
					//}
					//else {
					//	RefreshPage = false;
					//}

					/*
						Acknowlage Request.
							Data to be sent in blocks less than 4096 bytea in length.
					*/
					Server_Send(client, "200", "text/html", RefreshPage, SIMPLE_HTML_1, SIMPLE_HTML_2, SIMPLE_HTML_3, SIMPLE_HTML_4, SIMPLE_HTML_5, SIMPLE_HTML_6, SIMPLE_HTML_7);

					break;
				}
				/*
					If you got a newline, then clear currentLine.
					I only expect to recive one line.
				*/
				else {
					currentLine = "";
					EndOfRequest = true;	/*	Flag to keep finished line.	*/
				}
			}
			/*
				Add charectors to string.
			*/
			else if (c != '\r') {
				currentLine += c;
				if (!EndOfRequest) {
					LineRecived = currentLine;
				}
			}

		}
	}
	client.stop();

	Serial.println("client stop");
#ifdef DEBUG
	Serial.print("Line Recived: ");
	Serial.println(LineRecived);
#endif // DEBUG


	if (LineRecived.startsWith("GET /command")) {

		/*
			Strip recived sting of garbage.
			Add to buffer replacing HTML %20 white spaces, with ASCII spaces.
		*/
		String _thisCommand = LineRecived.substring(19, LineRecived.length() - 9);
		sofar = _thisCommand.length();
		int _trueCount = 0;
		for (size_t i = 0; i < sofar; i++) {
			if (_thisCommand[i] == '%') {
				Buffer_TX[_trueCount] = ' ';
				i += 3;
				_trueCount += 1;
			}
			Buffer_TX[_trueCount] = _thisCommand[i];
			_trueCount += 1;
		}
		Serial.print("Buffer_TX: ");
		Serial.println(Buffer_TX);

		/*	Process recived command.	*/
		procsessingString = true;
		processCommand();
		init_process_string();

	}



}

/*
	HTTP request: On Connect
*/
//void Server_Send(WiFiClient client, String stateCode, String dataType, String webPage) {
void Server_Send(WiFiClient client, String stateCode, String dataType,boolean refreshPage, String webPage1, String webPage2, String webPage3, String webPage4, String webPage5, String webPage6, String webPage7) {

	/*	Return code ie. 200	*/
	client.println("HTTP/1.1 " + stateCode + " OK");
	if (refreshPage){
	/*	Data type ie. "text/html"	*/
	client.println("Content-type:" + dataType);
	client.println();
	/*	Page to return	*/
	client.print(webPage1);
	client.print(webPage2);
	client.print(webPage3);
	client.print(webPage4);
	client.print(webPage5);
	client.print(webPage6);
	client.print(webPage7);
	}
	client.println();

}
void Serial_Print_Wifi_Status() {
	/*
		Print the SSID of the network you're attached to.
	*/
	Serial.print("SSID: ");
	Serial.println(WiFi.SSID());
	/*
		Print the web  page IP address.
	*/
	Serial.println("When connected:");
	Serial.print("IP Address of web page: ");
	Serial.println(ip);
	/*
		Print the signal strength.
	*/
	long rssi = WiFi.RSSI();
	Serial.print("signal strength (RSSI): ");
	Serial.print(rssi);
	Serial.println(" dBm");
}