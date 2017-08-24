# Serveri.py file

""" Vaatii Python 3.3(?) tai sita uudemman version. Testattu 3.5:lla.

"""

# Standard libraries:
import socket
import sys
import datetime as dt
import sqlite3
import time
import smtplib
import logging

# Tekstiviestin lahetys ja puhelun soittaminen:
from twilio.rest import Client  # Twilion tekstiviesti API ei vielä Suomessa, mutta tulossa pian(?).
# Asennus: pip install twilio       https://www.twilio.com/docs/libraries/python

# Omista tiedostoista:
from topsecret import PASSWD, SENDER  # Gmail-kayttajan tiedot
from topsecret import ACCOUNT_SID, AUTH_TOKEN, TWLO_NUM, TWLO_URL  # Twilio-kayttajan tiedot
from topsecret import HOSTNAME, PORT
from topsecret import CHECK  # Vastaanotettavan datan tarkastus
# import CreateDB  # Kaytetaan tietokannan luomiseen


# Loggauksen formaatti:
DATE_FORMAT = '%Y-%m-%d %H:%M:%S'  # Vuosi-kuukausi-paiva Tunti:minuutit:sekunnit
LOG_FORMAT = "%(levelname)s %(asctime)s - %(message)s"

logging.basicConfig(filename="logfile.log",
                    level=logging.DEBUG,
                    format=LOG_FORMAT,
                    datefmt=DATE_FORMAT)  # filemode = 'w' overwrites the file if the file exists

logger = logging.getLogger()


# Asettaa annetun listan alkiot tietokannan Mittaukset-taulukkoon ja paivittaa laitteeen kayttokerrat Laitteet-taulukkoon:
def data_to_db(cursor, conn_db, datalist):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime(DATE_FORMAT))  # datetime-objekti
    cursor.execute("INSERT INTO Mittaukset VALUES (?, ?, ?, ?)", (int(datalist[1]), date,
                                                                  int(datalist[2]), float(datalist[3])))

    cursor.execute("UPDATE Laitteet SET kaytot = kaytot + 1 WHERE numero = ?", (str(datalist[1])))
    conn_db.commit()


# Hakee tietokannasta vastuuhenkilon valitseman ilmoitustavan, spostin ja puh. numeron. Palauttaa ne tuplena.
def find_preference(cursor, conn_db, device_id):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime(DATE_FORMAT))  # datetime-objekti
    cursor.execute(
        "SELECT ilmoitusTila, sposti, puh FROM Asiakkaat WHERE nimi IN (SELECT asiakasNimi FROM Vuokraukset WHERE laiteNum=? AND ? BETWEEN alkuaika AND loppuaika)",
        (device_id, date))
    preference = cursor.fetchone()
    return preference  # tuple: (ilmoitustila, sposti, puh)


# Lahetetaan annetun laitteen vastuuhenkilolle email suodattimien vaihdon tarpeesta:
def send_email(receiver, device_id, filter_status):
    if int(filter_status) == 1:
        filter = 'the coarse filter'
    elif int(filter_status) == 2:
        filter = 'the fine filter'
    else:
        filter = 'both filters'
    header = 'To:' + receiver + '\n' + 'From: ' + SENDER + '\n' + 'Subject:Filters Clogged! \n'
    message = header + "\nHello!\n\nPlease change " + filter + " of the device number " + device_id + "!\n\nBest Regards,\nProto Dev Team."

    try:
        emailserver = smtplib.SMTP("smtp.gmail.com", 587)
        emailserver.starttls()
        emailserver.login(SENDER, PASSWD)
        emailserver.sendmail(SENDER, receiver, message)
        emailserver.close()
        print("Mail sent")
        logger.debug("Mail sent")
    except:
        print("Failed to send mail")
        logger.debug("Failed to send mail")


# Soittaa nauhoitetun puhelun laitteen vastuuhenkilolle Twilion API:a kayttaen:
def call_user(phone_num):
    client = Client(ACCOUNT_SID, AUTH_TOKEN)
    call = client.calls.create(to=phone_num, from_=TWLO_NUM, url=TWLO_URL)
    logger.debug("Call sid:" + call.sid)


# Lahetetaan tekstiviesti laitteen vastuuhenkilolle Twilion API:a kayttaen:
def send_sms(phone_num):
    client = Client(ACCOUNT_SID, AUTH_TOKEN)
    message = 'Hello!'
    client.messages.create(to=phone_num, from_=TWLO_NUM, body=message)
    logger.debug("Message sent")


# Funktio luo TCP/IP soketin ja palauttaa sen:
def init_server(hostname, port):
    try:
        soketti = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except OSError as err:
        print("Failed to create socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    server_address = (hostname, port)

    try:
        soketti.bind(server_address)
    except OSError as err:
        print("Failed to bind socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    soketti.listen(5)  # number of unaccepted connections that the system will allow before refusing new connections
    return soketti


# main funktio:
def main():

    # CreateDB.init_db()  # Luo tietokannan jos sita ei ole
    soket = init_server(HOSTNAME, PORT)

    while True:

        print("Waiting for connection..")
        connection, client_address = soket.accept()
        print("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))
        logger.debug("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))

        connection.settimeout(15)  # Katkaisee yhteyden clienttiin jos dataa ei vastaanoteta x sekuntiin. Odottaa uutta yhteytta taman jalkeen.

        try:
            data = connection.recv(4096)
            data = data.decode()
            print("Data from client: " + data)

            data_list = data.split(",")  # Pilkotaan vastaanotettu merkkijono listaan

            # Siirretaan listan alkiot tietokantaan, jonka jalkeen suljetaan yhteydet:
            if str(data_list[0]) == CHECK:  # Tarkastetaan että data on meidan laitteesta
                conn = sqlite3.connect('PROTO.db')
                c = conn.cursor()
                data_to_db(c, conn, data_list)
                print("Data saved, closing connection to client")
                logger.debug("Data saved")

                if int(data_list[2]) != 0:  # jos suodatin on tukossa
                    pref = find_preference(c, conn, data_list[1])  # Millaisen ilmoituksen vastuuhenkilo haluaa?
                    if pref[0] == 0:
                        send_email(pref[1], data_list[1], data_list[2])
                    elif pref[0] == 1:
                        connection.sendall(pref[2].encode())  # lahetetaan vastuuhenkilon puh numero MCU:lle tekstiviestin lahetysta varten
                    elif pref[0] == 2:
                        send_email(pref[1], data_list[1], data_list[2])
                        connection.sendall(pref[2].encode())
                c.close()
                conn.close()
                connection.close()
            else:
                print("Wrong data, closing connection")
                connection.close()

        # Virheiden hallintaa:
        except socket.timeout:
            logger.debug("Timeout")
            connection.close()
            print("NO DATA RECEIVED! TIMEOUT!")

        except ConnectionError as err:
            print("Connection Error: {0}".format(err))
            logger.error(err)

        except UnicodeError as err:
            print("Failed to decode data: {0}".format(err))
            logger.error(err)
            print("Closing connection")
            connection.close()

        except ValueError as err:
            print("Failed to split data: {0}".format(err))
            logger.error(err)
            connection.close()


if __name__ == '__main__':
    main()
