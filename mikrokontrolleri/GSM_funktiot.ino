// SIM900 koodit alkaa

const char PINKOODI[] = "";
const char URL[] = "";
const char PORTTI[] = "";
const char PASSWD[] = "";
uint8_t kohta = 0;
uint8_t kohta2 = 0;
char numero[14];

// Kopioi SIM900 tulostamat merkit serial monitoriin
void toSerial() {
  while(SIM900.available()) {
    USB.write(SIM900.read());
  }
}

// Kopioi SIM900 tulostamat merkit puskuriin ja serial monitoriin
void toBuffer(){
  uint8_t i = 0;
  while(SIM900.available()) {
    char a = SIM900.read();
    puskuri[i] = a;
    USB.write(a);
    i++;
  }
  puskuri[i] = '\0';
}

void readSerial() {
	toBuffer();
  if (strstr(puskuri, "+CREG: 0,1") == NULL) {
    kohta -= 2;
  }
  for (uint8_t j = 0; j < sizeof puskuri; j++){
    puskuri[j] = NULL;                  // kaikki indeksit nulliksi
  }
}

void readSerial2() {
	toBuffer();
  if (strstr(puskuri, "+CSQ: 0,0") != NULL) {
    kohta -= 2;
  }
  for (uint8_t j = 0; j < sizeof puskuri; j++){
    puskuri[j] = NULL;                  // kaikki indeksit nulliksi
  }
}

void readServerResponse() {
	toBuffer();
  if (strstr(puskuri, "SEND OK\r\n")) {
    if (strstr(puskuri, "+")) {
      char *ptr1 = strstr(puskuri, "+");
      numero[13] = '\0';
      strncpy(numero, ptr1, 13);
      numero[13] = '\0';
      USB.println(numero);
    }
  }
}

// Funktio SIM900 alustamiseen ja tukiasemaan yhdistämiseen.
// Palauttaa ajan millisekunteina, joka odotetaan ennen kuin funktiota saa kutsua uudestaan.
// Funktio jatkaa siitä kohtaa, mihin viime kerralla jäätiin.
// Palauttaa 0xFFFFFFFF, kun ollaan tultu funktion loppuun.
uint32_t sim_alustus(){
	uint32_t viive = 0;
	switch(kohta) {
		case 0 :
			SIM900.println("AT");
			viive = 1000;
			break;
		case 1 :
			toSerial();
			SIM900.print("AT+CPIN=\"");
			SIM900.print(PINKOODI);
			SIM900.println("\"");
			viive = 15000;
			break;
		case 2 :
			toSerial();
			SIM900.println("AT+CREG?");
			viive = 4000;
			break;
		case 3 :
			readSerial();
			SIM900.println("AT+CGATT=1");
			viive = 1000;
			break;
		case 4 :
			toSerial();
			SIM900.println("AT+CSQ");
			viive = 1000;
			break;
		case 5 :
			readSerial2();
			break;
		default :
			viive = 0xFFFFFFFF;
			kohta = 0;
			break;
			
	}
	kohta++;
	return viive;
}

// Funktio puhelinnumeron pyytämiseen serveriltä ja tekstarin lähetykseen siihen numeroon
uint32_t sim_serveriyhteys() {
  uint32_t viive = 0;
  switch(kohta2) {
    case 0 :
      SIM900.println("AT+CIPSHUT");
      viive = 2000;
      break;
    case 1 :
      toSerial();
      SIM900.println("AT+CIPSTATUS");
      viive = 2000;
      break;
    case 2 :
      toSerial();
      SIM900.println("AT+CIPMUX=0");
      viive = 2000;
      break;
    case 3 :
      toSerial();
      SIM900.println("AT+CSTT=\"internet\",\"rlnet\",\"internet\""); //MUUTETAAN APN, jne.
      viive = 2000;
      break;
    case 4 :
      toSerial();
      SIM900.println("AT+CIICR");
      viive = 2500;
      break;
    case 5 :
      toSerial();
      SIM900.println("AT+CIFSR");
      viive = 2000;
      break;
    case 6 :
      toSerial();
      SIM900.print("AT+CIPSTART=\"TCP\",\"");
      SIM900.print(URL);
      SIM900.print("\",\"");
      SIM900.print(PORTTI);
      SIM900.println("\"");
      viive = 2000;
      break;
    case 7 :
      toSerial();
      SIM900.println("AT+CIPSEND");
      viive = 2000;
      break;
    case 8 :
      toSerial();
	  SIM900.print(PASSWD);
	  SIM900.print(",");
	  SIM900.print(laiteid);
	  SIM900.print(",");
	  SIM900.print(suodatinTila);
	  SIM900.print(",");
	  SIM900.print(kierroksia10k);
      SIM900.println((char)26); // End AT command with a ^Z, ASCII code 26
      viive = 2000;
      break;
    case 9 :
      readServerResponse();
      SIM900.println("AT+CIPSHUT");
      viive = 2000;
      break;
    case 10 :
      toSerial();
      if (numero[0] != NULL) {
    	  SIM900.println("AT+CMGF=1");
    	  viive = 2000;
    	  numeroVastaanotettu = true;
      }  
    	else {
    		kohta2 = 0;
    		viive = 0xFFFFFFFF;
    		numeroVastaanotettu = false;
    	}
    	break;
    case 11 :
      toSerial();
      if(suodatinTila!=0){
      	SIM900.print("AT+CMGS=\"");
	      SIM900.print(numero);
	      SIM900.println("\"");
      	kohta2 = 0;
      	viive = 0xFFFFFFFF;
      }
      else viive = 2000;
    	break;
    case 12 :
      toSerial();
      if(suodatinTila == 1) SIM900.print(F("Karkeasuodatin vaihdettava heti!"));
      else if(suodatinTila == 2) SIM900.print(F("Hienosuodatin vaihdettava heti!"));
      else if(suodatinTila == 3) SIM900.print(F("Molemmat suodattimet vaihdettava heti!"));
      SIM900.println((char)26);
      viive =	2000;
    default :
      kohta2 = 0;
      viive = 0xFFFFFFFF;
      break;
      
  }
  kohta2++;
  return viive;
}
