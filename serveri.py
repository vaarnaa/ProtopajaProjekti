# Serveri.py file

""" Vaatii Python 3.3(?) tai sita uudemman version.


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
from twilio.rest import Client

# Omista tiedostoista:
from topsecret import PASSWD, SENDER  # Gmail-kayttajan tiedot
from topsecret import ACCOUNT_SID, AUTH_TOKEN, TWLO_NUM, TWLO_URL  # Twilio-kayttajan tiedot
from topsecret import HOSTNAME, PORT
from topsecret import CHECK  # Vastaanotettavan datan tarkastus
import CreateDB  # Kaytetaan tietokannan luomiseen


# Loggauksen formaatti:
DATE_FORMAT = '%Y-%m-%d %H:%M:%S'  # Vuosi-kuukausi-paiva
LOG_FORMAT = "%(levelname)s %(asctime)s - %(message)s"

logging.basicConfig(filename="logfile.log",
                    level=logging.DEBUG,
                    format=LOG_FORMAT,
                    datefmt=DATE_FORMAT)  # filemode = 'w' overwrites the file if the file exists. If the file does not exist, creates a new file for writing.

logger = logging.getLogger()


# Asettaa annetun listan alkiot tietokannan Mittaukset-taulukkoon:
def data_to_db(cursor, conn_db, datalist):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime(DATE_FORMAT))  # datetime-objekti
    cursor.execute("INSERT INTO Mittaukset VALUES (?, ?, ?, ?)", (str(datalist[1]), date,
                                                                  int(datalist[2]), float(datalist[3])))

    cursor.execute("UPDATE Laitteet SET kaytot = kaytot + 1 WHERE numero = ?", (str(datalist[1])))
    conn_db.commit()


# Lahetetaan annetun laitteen vastuuhenkilolle email:
def send_email(cursor, conn_db, device_id):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime(DATE_FORMAT))  # datetime-objekti
    cursor.execute('SELECT asiakasSposti FROM Vuokraukset WHERE laiteNum=? AND ? BETWEEN alkuaika AND loppuaika',
                   (device_id, date))
    receiver = cursor.fetchone()[0]

    header = 'To:' + receiver + '\n' + 'From: ' + SENDER + '\n' + 'Subject:WARNING! \n'
    message = header + "\nMoi!\nThis is a test message\nBest Regards, Proto Dev Team."

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


# Soitetaan laitteen vastuuhenkilolle Twilion API:a kayttaen:
def call_user(phone_num):
    client = Client(ACCOUNT_SID, AUTH_TOKEN)
    call = client.calls.create(to=phone_num, from_=TWLO_NUM, url=TWLO_URL)
    logger.debug("Call sid:" + call.sid)


# Lahetetaan tekstiviesti kayttajalle Twilion API:a kayttaen
def send_sms(phone_num):
    client = Client(ACCOUNT_SID, AUTH_TOKEN)
    message = 'Hello!'
    client.messages.create(to=phone_num, from_=TWLO_NUM, body=message)
    logger.debug("Message sent")


# Funktio luo TCP/IP soketin ja palauttaa sen:
def init_server():
    try:
        soketti = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except OSError as err:
        print("Failed to create socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    server_ip = socket.gethostbyname(HOSTNAME)
    server_address = (server_ip, PORT)

    try:
        soketti.bind(server_address)
    except OSError as err:
        print("Failed to bind socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    soketti.listen(5)
    return soketti


# main funktio:
def main():

    CreateDB.init_db()  # Luo tietokannan jos sita ei ole
    soket = init_server()

    while True:

        print("Waiting for connection..")
        connection, client_address = soket.accept()
        print("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))
        logger.debug("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))

        connection.settimeout(15)  # Katkaisee yhteyden clienttiin jos dataa ei vastaan oteta x sekuntiin. Odottaa uutta yhteytta taman jalkeen.

        try:
            data = connection.recv(4096)
            data = data.decode()
            print("Data from client: " + data)

            data_list = data.split(",")  # Pilkotaan vastaanotettu merkkijono listaan

            # Siirretaan listan alkiot tietokantaan, jonka jalkeen suljetaan yhteydet:
            if str(data_list[0]) == CHECK:
                conn = sqlite3.connect('PROTO.db')
                c = conn.cursor()
                data_to_db(c, conn, data_list)
                print("Data saved, closing connection to client")
                logger.debug("Data saved")

                if int(data_list[2]) != 0:  # jos suodatin on tukossa
                    send_email(c, conn, data_list[1])

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
