/* Tehtävää:
 SIM900 lähetysfunktion kutsuminen automaattisesti jossain kohtaa koodia ja datan lähetys serverille.
 Käyttötuntilaskuri? Töpselin irrotuksen havaitsemiseminen?
 Nollaustoiminnon non-blockaus, ettei nappulan tarkistelu keskeydy hetkeksikään.
 Jos antureita nollattaessa moottoria ei pysäyttäisi kokonaan, niin se olisi käyttövalmis paljon nopeammin.
 Läimäyskäynnistys.
 1K sarjavastus nappulan pinniin PC3, ettei vahingossa pinni kärähdä, jos sen pistää outputiksi.
 Hälytysledin ja kajarin käskeminen yhdellä kertaa I2Cllä, eikä vuoron perään.
 Toisen suodattimen mahdollinen keltainen valo jäätävä päälle hälytyksen ajaksi?
 Antureilla kestää noin 5 s lämmetä koodin uppauksen jälkeen.
 Joku fiksu nollaus 5 s virtojen kytkemisen jälkeen.
 Lämpenemisaika voi olla pidempikin, vaikka minuutteja, jos anturit ovat kylmiä.
 Antureiden viilentäminen kylmäkallella laskee niiden painelukemat 10 minuutissa
 negatiivisiin lukemiin -5, -19 ja -6 Pa. Alussa lukemat olivat 0 Pa.*/

 
// 3 analogista paineanturia ja SIM900 ja MCP23017
#include <EEPROM.h>
#include <PID_v1.h> // https://github.com/br3ttb/Arduino-PID-Library
#include <SoftwareSerial.h>
#include <Wire.h>


// PID muuttujat:
double paine1 = 0, kaasu = 0, asetus = 0;
double Kp, Ki, Kd;
PID ohjain(&paine1, &kaasu, &asetus, Kp, Ki, Kd, DIRECT);


// Pinnit 2 ja 9 on varattu moottorin ohjaukseen.
// A3 on nappula ja 3 on sen ledi.
const uint8_t mittauspinni1 = A6, mittauspinni2 = A7, mittauspinni3 = A0,
nappula_pin = A3, led_pin = 3, anturienVirtaPin = 7;
SoftwareSerial SIM900(10, 6); // RX, TX


// Valitaan kumpaa sarjaporttia tietokone käyttää
#define USB Serial


// Muut globaalit muuttujat
uint16_t mitattu_rpm, kierroslaskuri = 0;
const uint16_t kaasu_max = 4000; // PWM säätö on välillä 0 - kaasu_max
const uint16_t anturinHerkkyys = 1000; // mV/kPa
const uint16_t kaasu_pid_max = kaasu_max * 0.9;

// Tila 'p' = paineohjaus, 'r' = tyhjakaynti, 'o' = pallon pomputtelu
int8_t tila = 'p', halytysledi, suodatinTila, puskuri[64] = {0}; 
const float paineresoluutio = 5.0 * 1000000 / anturinHerkkyys / 1023; // pascalia
float ilmanpaine1, ilmanpaine2, ilmanpaine3, paine2, paine3;
bool tyhjakaynti = true, numeroVastaanotettu = false, tuoreetPaineet = false;
bool halytys = false, halytyslediPaalla = false, sim_toiminnassa = true;
uint32_t kaynnistyshetki = 0, lepohetki = 0x7FFFFFFF, aika, napinTarkistusvali = 0;
uint8_t GPIOA = 0xFF, GPIOB = 0x01; // MCP23017 General Purpose Input/Output rekisterit


// EEPROMiin tallennettavia muuttujia
uint8_t laiteid;
uint16_t haluttu_rpm, resetlaskuri, kierroksia10k_mem;
uint32_t ajastin, nollausAjastin, oskillointi, kierroksia10k;


// Muuttujien sijainnit EEPROMissa
const uint8_t
	haluttu_rpm_mem = 0,
	asetus_mem = 4,
	oskillointi_mem = 8,
	ajastin_mem = 12,
	Kp_mem = 16,
	Ki_mem = 20,
	Kd_mem = 24,
	laiteid_mem = 32,
	nollausAjastin_mem = 36,
	resetlaskuri_mem = 40;
	// Lisäksi kierroslaskurille on varattu muistipaikat 96-999.

	
// Puhaltimen kierroslaskuri
volatile uint8_t pulssit = 0;
volatile uint32_t aikaVanha = 0;
ISR(INT0_vect){
	const uint32_t aikaNyt = micros();
	if (aikaNyt - aikaVanha >= 15000){ // Hylätään mahdottomat yli 4000 rpm pulssit.
		aikaVanha = aikaNyt;
		pulssit++;
	}
}


// Tuulettimen nopeuden laskeminen
void mittaaRPM(){
	static uint32_t aikaMuinainen = 0;
	if (pulssit > 0) {
		EIMSK &= ~(1 << INT0); // Interrupt pois päältä siksi aikaa kun sen muuttujat luetaan.
		const uint32_t aikaViime = aikaVanha;
		const uint8_t kiekat = pulssit;
		pulssit = 0;
		EIMSK |= 1 << INT0; // Interrupt takaisin päälle.
		mitattu_rpm = 60 * 1000000 * kiekat / (aikaViime - aikaMuinainen);
		aikaMuinainen = aikaViime;
		kierroslaskuri += kiekat;

		// Tallennellaan kierroksia välillä EEPROMiin, jotta ne muistetaan virran katkaisun jälkeenkin.
		if (kierroslaskuri >= 10000) {
			kierroksia10k++;
			USB.println(F("Tallennetaan kierrokset EEPROMiin."));
			EEPROM.put(kierroksia10k_mem,kierroksia10k);
			kierroslaskuri -= 10000;

			// Tasoitetaan EEPROMin kulumista
			if (kierroksia10k % 10000 == 0) {
				USB.println(F("Siirretaan kierrosten tallennuspaikkaa EEPROMissa."));
				kierroksia10k_mem++;
				if (kierroksia10k_mem > 999) {
					kierroksia10k_mem = 100;
				}	
				EEPROM.put(96, kierroksia10k_mem);
			}
		}
	}
	else if (mitattu_rpm != 0 && micros() - aikaMuinainen >= 1000000) mitattu_rpm = 0; // Tuuletin pysähtynyt.
}


// Arduinon analogRead muokattuna. Muokkaus ei juuri nopeuttanut koodia.
int analogLuku(){
	uint8_t low, high;
	ADCSRA |= 1 << ADSC; // aloita konversio
	while (bit_is_set(ADCSRA, ADSC)); // konversion jälkeen ADSC on tyhjennetty
	low  = ADCL;
	high = ADCH;
	return (high << 8) | low;
}


/*Jumittamaton ylinäytteistysfunktio käytönaikaiseen mittailuun, jottei nappulassa olisi viivettä.
  Funktio ottaa tuhansia mittauksia ja laskee niistä keskiarvon.
  Parametri 1 on pin, josta jännitettä mitataan.
  Parametri 2 on otettavien näytteiden lukumäärä.
  Parametri 2 on valinnainen. Jos tätä parametria ei anneta, käytetään oletusarvoa.
  Palauttaa -1 jos ylinäytteistys on kesken.
  Palauttaa tuloksena luvun väliltä 0-1023 kun ylinäytteistys on valmis.
  Kutsumme funktiota noin 11000 kertaa sekunnissa ja nopeammin jos vain mahdollista.
  Siksi funktio vienee suurimman osan CPU ajasta ja on syytä saada mahdollisimman nopeaksi.*/
float ylinaytteista(uint8_t pin, const uint16_t nayteLukumaara = 3000){
	static bool kesken = false; // Onko mittaus kesken
	static uint32_t summa, i;
	if (!kesken){ // Aloitetaan mittaus
		if (pin >= 14) pin -= 14; // allow for channel or pin numbers
		ADMUX = 1 << REFS0 | pin & 0x07; // MUX mittauspinniin.
		summa = analogLuku();
		if (nayteLukumaara == 1){
			return summa;
		}
		else {
			i = 1;
			kesken = true;
			return -1;
		}
	}
	else {
		if (i < nayteLukumaara){
			const uint8_t pienisilmukka = 30; // Jos liian suuri luku, napin tarkistus viivästyy liikaa.
			for (uint8_t c = 0; c < pienisilmukka; c++){ // Pienisilmukka nopeuttaa ylinäytteistystä.
				summa += analogLuku(); // Summataan näytteitä yhteen.
			}
			i += pienisilmukka;
			return -1;
		}
		else {
			kesken = false;
	  	return (float)summa / i; // Skaalataan summa luvuksi välille 0-1023.
		}
	}
}


// Jumittava ylinäytteistysfunktio antureiden nollaamiseen. Ottaa noin 68000 näytettä sekunnissa.
float ylinaytteista_nollaus(uint8_t pin, const uint8_t bits = 7){
	if (pin >= 14) pin -= 14; // allow for channel or pin numbers
	ADMUX = 1 << REFS0 | pin & 0x07; // MUX mittauspinniin.
  const uint32_t nayteLukumaara = 1UL << (bits * 2);
  uint32_t summa = 0; // Summataan kaikki näytteet yhteen.
  for (uint32_t i = 0; i < nayteLukumaara; i++) {
	  summa += analogLuku();
  }
  return (float)(summa >> bits) / (1 << bits); // Skaalataan summa luvuksi välille 0-1023.
}


// Paineantureiden nollaus
void nollaus(){
	OCR1A = 0; // Pysäytetään puhallin asettamalla PWM nollaan.
	USB.println(F("Odotetaan, etta puhallin pysahtyy."));
	uint32_t aikaleima = millis(), aloitusaika = aikaleima;
	do {
		aika = millis();
		tarkista_nappi();
		if (pulssit > 0){
			pulssit = 0;
			aikaleima = aika;
		}
		if(aika - aloitusaika > 20000){
			USB.println(F("Tuuletin ei suostu pysahtymaan, mutta nollataan silti."));
			break;
		}
	} while (aika - aikaleima < 1000);
	OCR1A = 200; // Käynnistetään puhallin heti pysähtymisen jälkeen.
	// Nollaus ehditään tehdä sillä välin kun puhallin mietiskelee,
	// että kumpaankohan suuntaan lähtisi pyörimään.
  USB.println(F("Nollaus alkaa."));
  ilmanpaine1 = ylinaytteista_nollaus(mittauspinni1);
  ilmanpaine2 = ylinaytteista_nollaus(mittauspinni2);
  ilmanpaine3 = ylinaytteista_nollaus(mittauspinni3);
  USB.println(F("Nollattu."));
}


// Tallennettujen asetusten tulostus
void printEEPROM(){
	USB.print(F("Tyhjakayntikierrosluku: "));
	EEPROM.get(haluttu_rpm_mem, haluttu_rpm);
	USB.print(haluttu_rpm);
	USB.println(F(" rpm."));
	kaasunKorjauskaava();

	USB.print(F("Dynaaminen paine: "));
	EEPROM.get(asetus_mem, asetus);
	USB.print(asetus);
	USB.println(F(" Pa."));

	USB.print(F("Pallon pompotteluvali: "));
	EEPROM.get(oskillointi_mem, oskillointi);
	USB.print(oskillointi);
	USB.println(F(" ms."));

	USB.print(F("Sammutusajastin: "));
	EEPROM.get(ajastin_mem, ajastin);
	USB.print(ajastin);
	USB.println(F(" s."));

	USB.print(F("Nollausajastin: "));
	EEPROM.get(nollausAjastin_mem, nollausAjastin);
	USB.print(nollausAjastin);
	USB.println(F(" s."));

	USB.print(F("Kierroslaskuri: "));
	USB.print(kierroksia10k*10000+kierroslaskuri); // Mitä jos kierroksia onkin yli 0xFFFFFFFF ?
	USB.println(F(" kierrosta."));

	USB.print(F("Resetoitu "));
	USB.print(resetlaskuri);
	USB.println(F(" kertaa."));

	USB.print(F("Laite id: "));
	EEPROM.get(laiteid_mem,laiteid);
	USB.println(laiteid);

	USB.print(F("Kp = "));
	EEPROM.get(Kp_mem, Kp);
	USB.print(Kp);
	USB.print(F(", Ki = "));
	EEPROM.get(Ki_mem, Ki);
	USB.print(Ki);
	USB.print(F(", Kd = "));
	EEPROM.get(Kd_mem, Kd);
	USB.println(Kd);
}


// Funktio I/O expanderin rekistereihin kirjoittamiseen.
// Ensimmäinen parametri on rekisteri, johon kirjoitetaan.
// Toinen parametri on rekisteriin kirjoitettava arvo.
void iox(const uint8_t rekisteri, const uint8_t arvo){
	Wire.beginTransmission(0x20); // Valitaan I2C osoite.
	Wire.write(rekisteri);
	Wire.write(arvo);
	Wire.endTransmission();
}


/*Funktio ledien värin vaihtamiseen.
  Parametri 1 valitsee ledin (1, 2 tai 3).
  Jos parametri 1 on 0, vaihdetaan kaikkia ledejä kerralla.
  Parametri 2 on lediin asetettava RGB väri binäärimuodossa:
  0b010 = vihreä
  0b110 = keltainen
  0b100 = punainen*/
void ledi(const int8_t led, const uint8_t vari){
	switch (led){
		case 0: // Kaikki ledit kerralla
			GPIOA = 0xFF;
			GPIOB |= 1;
			if (vari & 0b100) GPIOA = 0b10110110;
			if (vari & 0b010) GPIOA &= 0b01101101;
			if (vari & 0b001){
				GPIOA &= 0b11011011;
				GPIOB &= 0b11111110;
			}
			iox(0x13, GPIOB);
			break;
		case 1: // Ledi 1
			GPIOA |= 0b00000111;
			if (vari & 0b100) GPIOA &= 0b11111110; // Punainen
			if (vari & 0b010) GPIOA &= 0b11111101; // Vihreä
			if (vari & 0b001) GPIOA &= 0b11111011; // Sininen
			break;
		case 2: // Ledi 2
			GPIOA |= 0b00111000;
			if (vari & 0b100) GPIOA &= 0b11110111;
			if (vari & 0b010) GPIOA &= 0b11101111;
			if (vari & 0b001) GPIOA &= 0b11011111;
			break;
		case 3: // Ledi 3
			GPIOA |= 0b11000000;
			GPIOB |= 0b00000001;
			if (vari & 0b100) GPIOA &= 0b10111111;
			if (vari & 0b010) GPIOA &= 0b01111111;
			if (vari & 0b001) GPIOB &= 0b11111110;
			iox(0x13, GPIOB);
			break;
	}
	iox(0x12, GPIOA);
}


// Kytkee kajarin yhtäjaksoisesti päälle/pois jos parametrina annetaan true.
// Piipauttaa kajaria niin lyhyesti kuin mahdollista ja sammuttaa sen,
// jos parametria ei anneta.
void piip(const bool jatkuva = false){
	bool paalle = true, pois = true;
	if(jatkuva){
		if(GPIOB & 0x02) paalle = false;
		else pois = false;
	}
	if(paalle){
		GPIOB |= 0x02;
		iox(0x13, GPIOB);
	}
	if(pois){
		GPIOB &= 0xFD;
		iox(0x13, GPIOB);
	}
}


// kuuluvat nuotit = {1400,600,200}
// hiljaiset nuotit = {2500,1600,500}
void melodia(){
	uint16_t nuotit[3] = {2500, 1600, 500};
	for(uint8_t i = 0; i < 3; i++){
		uint32_t kesto = millis() + 150;
		while(millis() < kesto){
			piip();
			delayMicroseconds(nuotit[i]);
		}
	}
}


//tuottaa äänen
void sweep(){
	for (uint16_t nuotti=0xFF;nuotti>0;nuotti--){
		piip();
		delayMicroseconds(nuotti);
	}
}


void setup() {
	//laitetaan pinnit päälle
	pinMode(mittauspinni1, INPUT);
	pinMode(mittauspinni2, INPUT);
	pinMode(mittauspinni3, INPUT);
	pinMode(nappula_pin, INPUT_PULLUP);
	pinMode(led_pin, OUTPUT);
	pinMode(anturienVirtaPin, OUTPUT);
	digitalWrite(anturienVirtaPin, HIGH);
	
	USB.begin(250000); // USB communicaatio tietsikkaan.
	SIM900.begin(19200); // SIM900 communicaatio. 1200-115200.

	// LED-piirin alkuvalmistelut
	Wire.begin();
	iox(0x00, 0x00); // IODIRA rekisterin pinnit 0-7 outputiksi.
	iox(0x01, 0xFC); // IODIRB rekisterin pinnit 0-1 outputiksi.
	ledi(0, 0); // Sammuta ledit.

 	PORTD |= 1 << PD2; // Pinnin 2 ylösvetovastus päälle. Liitä tuulettimen sense johto pinniin 2.
	EIMSK |= 1 << INT0; // External Interrupt MaSK register. Turns on INT0.
	EICRA = 0b10; // External Interrupt Control Register. Trigger interrupt from falling edge.
  
	// Nopeutetaan AD-muunninta. Lisätietoja löytyy sivulta 319 datasheetistä:
	// www.atmel.com/Images/Atmel-42735-8-bit-AVR-Microcontroller-ATmega328-328P_Datasheet.pdf
	ADCSRA &= 0b11111000; // Nollataan rekisterin 3 vikaa bittiä.
	ADCSRA |= 4; // Ja asetetaan 3 vikaa bittiä uudelleen. 4=nopein, 7=hitain (oletus).

	DDRB |= 1 << PB1; // Data Direction Register B. Pin 9 output.
	TCCR1A = 1 << COM1A1; // Timer/Counter Control Register. Compare Output Mode. non-inverting PWM 
	TCCR1B = 1 << WGM13 | 1 << CS10; // Waveform Generation Mode. Mode 8 sivulla 172. Clock Select. No prescaling.
	ICR1 = kaasu_max; // Input Capture Register. TOP counter value.
	OCR1A = 0;
	ohjain.SetOutputLimits(200, kaasu_pid_max);
	ohjain.SetMode(AUTOMATIC);
	ohjain.SetSampleTime(200);

	EEPROM.get(resetlaskuri_mem,resetlaskuri);
	resetlaskuri++;
	EEPROM.put(resetlaskuri_mem,resetlaskuri);

	EEPROM.get(96, kierroksia10k_mem);
	if(kierroksia10k_mem < 100 || kierroksia10k_mem > 999){
		USB.println(F("Kierrosten muistipaikka alustettu."));
		kierroksia10k_mem = 100;
		EEPROM.put(96, kierroksia10k_mem);
		kierroksia10k = 0;
		EEPROM.put(kierroksia10k_mem, kierroksia10k);
	}
	else EEPROM.get(kierroksia10k_mem, kierroksia10k);
	printEEPROM();
	//melodia();
	piip();
}


void loop() {	// Yksi kierros kesti 51 µs kun pienisilmukkaa ei vielä ollut ylinäytteistyksen sisällä.
	aika = millis(); // 2 µs
	tarkista_nappi(); // Tän funktion kutsuminen hidastaa looppii 3 µs.
	mittaaRPM(); // Tää 4 µs.

	// Mitataan paineet ylinäytteistäen kolmesta eri anturista.
	// Ylinäytteistetään yksi anturi kerrallaan, eikä mitattavaa
	// anturia jatkuvasti vaihdellen, jotta multiplekseri voidaan
	// pitää ylinäytteistyksen ajan samassa mittauspinnissä.
	// Tämä tekee ylinäytteistyksestä nopeampaa.
	// Mitataan dynaaminen paine viimeisimpänä, jotta PID-säädin saa tuoreimman
	// painelukeman.
	// Suodattimien paineantureita ei tarvitsisi mitata yhtä usein kuin moottorin
	// paineanturia, joten suodattimien antureiden tarkistusväliä voisi harventaa?
	// Moottorinkin anturia tarvitsisi mitata vain yhtä usein kuin PID-säädin laskee
	// uusia säätöarvoja, eli 5 kertaa sekunnissa.
	// Antureita ei tarvitse mitata lainkaan silloin kun puhallin on tyhjäkäynnillä,
	// tosin ei siitä kai haittaakaan ole.
	// Tällä hetkellä antureita mitataan niin usein kuin microcontrolleri pystyy.
	static uint8_t anturi = 0;
	if (anturi == 0){
		const float os = ylinaytteista(mittauspinni3);
		if (os != -1){
			paine3 = (os - ilmanpaine3) * paineresoluutio;
			anturi++;
		}
	}
	else if (anturi == 1){
		const float os = ylinaytteista(mittauspinni2);
		if (os != -1){
			paine2 = (os - ilmanpaine2) * paineresoluutio;
			anturi++;
		}
	}
	else if (anturi == 2){
		const float os = ylinaytteista(mittauspinni1);
		if (os != -1){
			paine1 = (os - ilmanpaine1) * paineresoluutio;
			anturi = 0;
			tuoreetPaineet = true;
		}
	}

	// Moottorin ohjaus painelukemalla (1 µs tyhjäkäynnillä)
	if (!tyhjakaynti && tila == 'p'){
		if (mitattu_rpm < 300) {
			kaasu = 600;
		}	
		else ohjain.Compute(); // EHDOTUS: Computataan heti kun dynaaminen paine on mitattu.
	}
	

	// Pallon pompottelutoiminto
	static uint32_t ajankohta = 0;
	if (tila == 'o' && !tyhjakaynti && aika - ajankohta > oskillointi){
		ajankohta = aika;

		// Ledien vilkuttelua
		static byte colors[3];
		byte led,color;
		led = random(1,4);
		do {
			color = random(0,8);
		} while (colors[led-1] == color);
		colors[led-1] = color;
		piip();
		ledi(led, color);

		if (kaasu == 200) {
			kaasu = kaasu_max;
		}
		else {
			kaasu = 200;
		}	
		digitalWrite(led_pin, kaasu == kaasu_max?HIGH:LOW); //MITÄ TARKOITTAA????
	} // Tämä tarkoittaa sitä, että jos moottorille syötettävä PWM signaali on
	// saavuttanut maksimiarvonsa (kaasu_max), pistetään ledi päälle (HIGH) ja
	// kun PWM signaali on pian taas minimissään (200), ledi sammutetaan (LOW).
	// Näin ledi saadaan vilkkumaan samaan tahtiin moottorin kanssa.

	// Nollataan anturit jonkin aikaa sammutuksen jälkeen. (if lause vie 6 µs tyhjäkäynnillä)
	if (tyhjakaynti && tila!='t' && aika - lepohetki >= 1000UL * nollausAjastin) {
		nollaus();
		lepohetki = aika;
	}

	OCR1A = kaasu; // Nopeuskäsky moottorille.

	// Lukujen tulostus tietokoneelle
	static uint32_t tulostushetki = 0;
	const uint32_t aikaViimeTulostuksesta = aika - tulostushetki;
	// Tulostetaan lukemat vain, jos uusia lukemia on saatavilla.
	if(tuoreetPaineet && (aikaViimeTulostuksesta >= 2000 || (tila=='t' && aikaViimeTulostuksesta >= 200))){
		tulostushetki = aika;

		// Tulostetaan paineet
		USB.print(paine1,1);
		USB.print(", ");
		USB.print(paine2,1);
		//USB.print(suodatettu_paine,1);
		//USB.print(", ");
		//USB.print(sqrt(suodatettu_paine*2/1.25));
		USB.print(", ");
		USB.print(paine3,1);
		USB.print(", ");
		/*USB.print(3.6*10.6*sqrt(paine2));
		USB.print(", ");*/
		USB.print(mitattu_rpm);
		USB.print(", ");
		USB.print(kaasu,0);
		USB.print(", ");
		USB.println(napinTarkistusvali);
		napinTarkistusvali = 0;

		// Testaustila (kokeiltava toimiiko vielä), jolla Excel painekuvaajat tehtiin
		if (tila == 't') {
			kaasu += 10;
			if (kaasu > 3000) {
				USB.println(F("Testaus valmis."));
				kaasu = 200;
				OCR1A = kaasu;
				tila = 'r';
				toiminto('r');
				while (!USB.available());
			}
		}
	}

	// Tarkistetaan ovatko suodattimet tukossa
	static uint32_t ledihetki1 = 0, ledihetki2 = 0, ledihetki3 = 0;
	if(tuoreetPaineet){
		tuoreetPaineet = false;
		char tukos;
		if (tukos = onTukossa()){
			if (tukos & 0b000010){
				USB.println(F("Karkeasuodatin aivan tukossa"));
				toiminto('r'); // Moottori tyhjäkäynnille.
				halytys = true;
				halytysledi = 1;
				suodatinTila = 1;
				sim_toiminnassa = true;
			}
			else if (tukos & 0b000001){
				USB.println(F("Karkeasuodatin melko tukossa"));
				piip();
				ledi(1, 0b110);
				ledihetki1 = aika;
			}
			if (tukos & 0b001000){
				USB.println(F("Hienosuodatin aivan tukossa"));
				toiminto('r');
				halytys = true;
				halytysledi = 2;
				if(suodatinTila == 1) suodatinTila = 3;
				else suodatinTila = 2;
				sim_toiminnassa = true;
			}
			else if (tukos & 0b000100){
				USB.println(F("Hienosuodatin melko tukossa"));
				piip();
				ledi(2, 0b110);
				ledihetki2 = aika;
			}
			if (tukos & 0b100000){
				USB.println(F("Imuaukko tai poistoputki aivan tukossa"));
				toiminto('r');
				halytys = true;
				halytysledi = 3;
				sim_toiminnassa = true;
			}
			else if (tukos & 0b010000){
				USB.println(F("Imuaukko tai poistoputki melko tukossa"));
				ledi(3, 0b110);
				ledihetki3 = aika;
			}
		}
	}
	
	// Sammutetaan keltaiset ledit vähän ajan päästä
	if(tila == 'p'){
		if ((GPIOA & 0b00000011) == 0 && aika-ledihetki1 > 5000) ledi(1, 0b010);
		if ((GPIOA & 0b00011000) == 0 && aika-ledihetki2 > 5000) ledi(2, 0b010);
		if ((GPIOA & 0b11000000) == 0 && aika-ledihetki3 > 5000) ledi(3, 0b010);
	}

	// Punaisten ledien vilkuttelu ja piippailu tukkeutumisen jälkeen
	static uint32_t halytyshetki = 0;
	if(halytys && aika-halytyshetki > 500){
		halytyshetki = aika;
		piip();
		if(halytyslediPaalla) ledi(halytysledi, 0b000);
		else ledi(halytysledi, 0b100);
		halytyslediPaalla = !halytyslediPaalla;
	}

	// Kutsutaan sim funktiota sen palauttamin aikavälein
	static bool sim_alustettu = false;
	static uint32_t sim_hetki = 0, sim_viive = 0;
	if (sim_toiminnassa && aika - sim_hetki >= sim_viive){
		if (!sim_alustettu){
			sim_viive = sim_alustus();
			if (sim_viive == 0xFFFFFFFF){
				USB.println(F("Sim alustettu."));
				sim_viive = 0;
				sim_alustettu = true;
				if(!halytys) sim_toiminnassa = false;
			}
		}
		else {
			sim_viive = sim_serveriyhteys();
			if (sim_viive == 0xFFFFFFFF){
				if (numeroVastaanotettu) {
					USB.println(F("Tekstiviesti lähetetty."));
				}
				else {
					USB.println(F("Numeron vastaanotto epäonnistuí."));
				}  
				sim_viive = 0;
				sim_toiminnassa = false;
			}
		}
		sim_hetki = aika;
	}


	// Käskyjen vastaanotto tietokoneelta:
	static int8_t kasky[20];
	if (USB.available()){ // Jatkuva available() tarkastelu hidastaa looppia 5 µs.
		delay(10); // Odotetaan, että kaikki data ehtii saapua serial bufferiin.
		int8_t kirjain;
		for (uint8_t i = 0; i<sizeof kasky; i++){
			kirjain = USB.read();
			if(kirjain == -1){
				kasky[i] = 0;
				break;
			}
			else kasky[i]=kirjain;
		}
		while (USB.read() != -1); // Tyhjennetään serial buffer.
		switch (kasky[0]){
			case 'r':
				kasky[0] = ' ';
				haluttu_rpm = atoi(kasky);
				if (haluttu_rpm < 0) haluttu_rpm = 0;
				else if (haluttu_rpm > kaasu_max) haluttu_rpm = kaasu_max;
				kaasunKorjauskaava();
				USB.print(F("Asetetaan RPM "));
				USB.println(haluttu_rpm);
				EEPROM.put(haluttu_rpm_mem, haluttu_rpm);
				break;
			case 'p':
				kasky[0] = ' ';
				asetus = atoi(kasky);
				USB.print(F("Asetetaan paine "));
				USB.println(asetus, 0);
				EEPROM.put(asetus_mem, asetus);
				break;
			case 'o':
				tila = 'o';
				kasky[0] = ' ';
				oskillointi = atoi(kasky);
				EEPROM.put(oskillointi_mem, oskillointi);
				break;
			case 'a':
				kasky[0] = ' ';
				ajastin = atoi(kasky);
				USB.print(F("Asetetaan ajastin: "));
				USB.println(ajastin);
				EEPROM.put(ajastin_mem, ajastin);
				break;
			case 'm':
				kasky[0] = ' ';
				nollausAjastin = atoi(kasky);
				USB.print(F("Asetetaan nollausajastin: "));
				USB.println(nollausAjastin);
				EEPROM.put(nollausAjastin_mem, nollausAjastin);
				break;
			case 't':
				toiminto('r');
				tila = 't';
				USB.println(F("Antureiden testaus..."));
				kaasu = 200;
				OCR1A = kaasu;
				do{
					mittaaRPM();
					if(millis() - aika > 20000){
						USB.println(F("Moottori ei tottele tai RPM piuha irti."));
						tila = 'r';
						toiminto('r');
						break;
					}
				}while (mitattu_rpm > 320 || mitattu_rpm < 310);
				break;
			case 'n':
				nollaus();
				break;
			case 's':
				if (!sim_alustettu) {
					USB.println(F("Alustetaan SIM900..."));
				}
				else {
					USB.println(F("Lahetetaan serverille jotain..."));
				}
				sim_toiminnassa = true;
				break;
			case 'i':
				printEEPROM();
				break;
			case 'k': // PID kertoimien muuttaminen lennosta
				switch (kasky[1]){
					case 'p':
						kasky[0] = ' ';
						kasky[1] = ' ';
						Kp = (double)atoi(kasky) / 100;
						USB.print(F("Kp = "));
						USB.println(Kp);
						EEPROM.put(Kp_mem, Kp);
						break;
					case 'i':
						kasky[0] = ' ';
						kasky[1] = ' ';
						Ki = (double)atoi(kasky) / 100;
						USB.print(F("Ki = "));
						USB.println(Ki);
						EEPROM.put(Ki_mem, Ki);
						break;
					case 'd':
						kasky[0] = ' ';
						kasky[1] = ' ';
						Kd = (double)atoi(kasky) / 100;
						USB.print(F("Kd = "));
						USB.println(Kd);
						EEPROM.put(Kd_mem, Kd);
						break;
				}
				ohjain.SetTunings(Kp, Ki, Kd);
				break;
			default:
				USB.println(F("r1000 p100 o1000 a10 t n i s Kp100 Ki100 Kd100"));
		}
	}
}

// Nappulan painamisen tarkistus.
// Funktio vie aikaa 8-20 µs digitalReadilla, 460 µs I2C:llä
// tai 4-16 µs PINillä. Käytämme PINiä, koska se on nopein.
// Funktio suorittaa 100 kertaa sekunnissa, kun taas
// ylinäytteistys pyörii 10000 kertaa sekunnissa, joten I2C:n käyttö
// hidastaisi silti loop timeä vain 2 µs, eli 4 %.
void tarkista_nappi(){
	//uint32_t test=micros();
	static uint32_t napinViimeTarkistushetki = 0;
	const uint32_t aikaViimeTarkistuksesta = aika - napinViimeTarkistushetki;
	if (aikaViimeTarkistuksesta >= 10){
		if (aikaViimeTarkistuksesta > napinTarkistusvali) {
			napinTarkistusvali = aikaViimeTarkistuksesta;
		}	
		napinViimeTarkistushetki = aika;

		// Tarkistetaan, kauanko nappia painetaan
		char painettu = 0;
		static bool viritetty = true, viritettyPitka = true;
		static uint32_t aikaleima = 0;
		const bool ylhaalla = PINC & 1<<PC3; // Nappulan luku pinnistä PC3.
		//const bool ylhaalla = digitalRead(nappula_pin);
		/*Wire.beginTransmission(0x20);
		Wire.write(0x13);
		Wire.endTransmission();
		Wire.requestFrom(0x20,1);
		const bool ylhaalla = Wire.read() & 0b00001000;*/
		if (!ylhaalla){ // Nappia painetaan
			if (viritetty){ // Kertapainallus
				viritetty = false;
				painettu = 1;
				aikaleima = aika;
			}
			else if (viritettyPitka && aika - aikaleima >= 2000){ // Pitkä painallus.
				viritettyPitka = false;
				painettu = 2;
				aikaleima = aika;
			}
		}
		else if (!viritetty && aika - aikaleima >= 50){ // Viritetään nappi uudelleen.
			viritetty = true;
			viritettyPitka = true;
		}

		// Päätetään mitä tehdään kun nappia on painettu
		if (tyhjakaynti && painettu == 1) {
			piip();
			if(halytys){
				halytys=false;
				halytyslediPaalla=false;
				ledi(halytysledi,0b100);
				suodatinTila=0;
			}
			else toiminto('p');
		}
		else if (!tyhjakaynti && (painettu == 1 || aika - kaynnistyshetki >= 1000UL * ajastin)) {
			piip();
			toiminto('r'); // Lepotilaan 5 min kuluttua tai napin painalluksesta.
			sim_toiminnassa=true;
		}
		else if (painettu == 2) { // Pitkä painallus
			sweep();
			if (tila == 'p') {
				toiminto('x');
			}
			else if (tila == 'x') {
				toiminto('o');
			}
			else if (tila == 'o'){
				toiminto('p');
			}
		}
		//USB.println(micros()-test);
	}
}

// Palauttaa binäärilukuna, mitkä suodattimet ovat tukossa.
// Paluuarvon bitit 0-1 kertovat karkeasuodattimen tukkoisuuden,
// bitit 2-3 hienosuodattimen tukkoisuuden ja bitit 4-5
// poisto- tai imuaukon tukkoisuuden.
// Jos suodatin ei ole tukossa, sen bitit ovat 0b00.
// Jos suodatin on melko tukossa, sen bitit ovat 0b01.
// Jos suodatin on aivan tukossa, sen bitit ovat 0b10.
uint8_t onTukossa(){
	uint8_t tukot=0;
	static uint32_t
		viiveKarkeaAivan=aika,
		viiveKarkeaMelko=aika,
		viiveHienoAivan=aika,
		viiveHienoMelko=aika,
		viiveKierrosluku=aika;
	bool
		karkeaAivanTukossa = false,
		karkeaMelkoTukossa = false,
		hienoAivanTukossa = false,
		hienoMelkoTukossa = false;
		
	if (!tyhjakaynti && tila == 'p'){ // Tarkistetaan, onko oikea toiminto päällä.

		// Tukkoisuuden painerajat
		if (paine2 > 250) karkeaAivanTukossa=true;
		else if (paine2 > 100) karkeaMelkoTukossa=true;
		if (paine3 > 450) hienoAivanTukossa=true;
		else if (paine3 > 200) hienoMelkoTukossa=true;

		// Tarkistetaan, että tukkoisuus on kestänyt riittävän kauan
		if (karkeaAivanTukossa){
			if (aika - viiveKarkeaAivan > 1000){
				viiveKarkeaAivan=aika;
				tukot |= 0b000010;
			}
		}
		else viiveKarkeaAivan=aika;
		
		if (karkeaMelkoTukossa){ 
			if (aika - viiveKarkeaMelko > 1000){
				viiveKarkeaMelko=aika;
				tukot |= 0b000001;
			}
		}
		else viiveKarkeaMelko=aika;
		
		if (hienoAivanTukossa){
			if (aika - viiveHienoAivan > 1000){
				viiveHienoAivan=aika;
				tukot |= 0b001000;
			}
		}
		else viiveHienoAivan=aika;

		if (hienoMelkoTukossa){
			if (aika - viiveHienoMelko > 1000){
				viiveHienoMelko=aika;
				tukot |= 0b000100;
			}
		}
		else viiveHienoMelko=aika;

		// Jos moottori on pyörinyt täysillä jonkin aikaa
		if (kaasu == kaasu_pid_max && aika - viiveKierrosluku > 1000){
			viiveKierrosluku = aika;
			const float yhteispaine = paine2 + paine3;
			if (yhteispaine < 300) tukot |= 0b100000; // Poistoputki aivan tukossa.
			else if (yhteispaine < 500) tukot |= 0b010000; // Poistoputki melko tukossa.
		}
	}
	return tukot;
}

// Moottorin RPM säädön korjauskaava
void kaasunKorjauskaava(){
	kaasu = 1.0385 * haluttu_rpm - 109;
	if (kaasu > kaasu_max) {
		kaasu = kaasu_max;
	}	
	else if (kaasu < 0) {
		kaasu = 0;
	}
}

// Käynnistää eri toimintoja, kuten tyhjäkäynti 'r', nopeuden vakiona pito 'p' tai pallon pomputtelu 'o'
void toiminto(const char toivomus){
	if (toivomus == 'r'){
		digitalWrite(led_pin,LOW);
		ledi(0,0);
		ohjain.SetMode(0);
		kaasunKorjauskaava();
		tyhjakaynti = true;
		USB.println(F("Tyhjakaynti"));
		lepohetki = aika;
	}
	else {
		tila = toivomus;
		tyhjakaynti = false;
		kaynnistyshetki = aika;
		if (toivomus == 'p'){
			digitalWrite(led_pin,HIGH);
			ledi(0,0b010); // Kaikki ledit vihreiksi
			ohjain.SetMode(1);
			USB.print(F("Kaynnistetaan "));
			USB.print(ajastin);
			USB.print(F(" sekunniksi paineeseen "));
			USB.print(asetus,0);
			USB.println(F(" Pa."));
		}
		else if (toivomus == 'o'){
			ohjain.SetMode(0);
			USB.println(F("Pallon pomputtelu"));
		}
		else if(toivomus=='x'){
			ohjain.SetMode(0);
			kaasu=kaasu_max;
			ledi(0,0b111);
			USB.println(F("Taysteho"));
		}
	}
}
