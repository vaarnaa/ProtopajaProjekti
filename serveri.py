# Serveri.py file

"""Testattu Python 3.5:lla. Vaatii Python 3.3(?) tai sita uudemman version."""

# Standard libraries:
import socket
import sys
import datetime as dt
import sqlite3
import time
import smtplib
import logging

# pip install twilio
from twilio.rest import Client

# Omasta tiedostosta:
from Credentials import PASSWD, SENDER  # Gmail-kayttajan tiedot
from Credentials import ACCOUNT_SID, AUTH_TOKEN, TWLO_NUM, TWLO_URL  # Twilio-kayttajan tiedot
from Credentials import HOSTNAME, PORT
from Credentials import CHECK  # Vastaanotettavan datan tarkastus
import CreateDB  # Kaytetaan tietokannan luomiseen


# Loggauksen formaatti:
DATE_FORMAT = '%Y-%m-%d %H:%M:%S'  # Vuosi-kuukausi-paiva
LOG_FORMAT = "%(levelname)s %(asctime)s - %(message)s"

logging.basicConfig(filename="logfile.log",
                    level=logging.DEBUG,
                    format=LOG_FORMAT,
                    datefmt=DATE_FORMAT)  # filemode = 'w'

logger = logging.getLogger()


# Asettaa annetun listan alkiot tietokannan Mittaukset-taulukkoon:
def data_to_db(cursor, conn_db, datalist):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime(DATE_FORMAT))
    cursor.execute("INSERT INTO Mittaukset VALUES (?, ?, ?, ?)", (str(datalist[1]), date,
                                                                  int(datalist[2]), float(datalist[3])))
    conn_db.commit()


# Lahetetaan annetun laitteen kayttajalle email:
def send_email():
    receiver = ''  # haetaan SQL subquerylla kayttaen funktiolle annettuja parametreja (laiteId ja aika)
    header = 'To:' + receiver + '\n' + 'From: ' + SENDER + '\n' + 'Subject:WARNING! \n'
    message = header + "\nMoi!\nThis is a test message\nBest Regards."

    try:
        emailserver = smtplib.SMTP("smtp.gmail.com", 587)
        emailserver.starttls()
        emailserver.login(SENDER, PASSWD)
        emailserver.sendmail(SENDER, receiver, message)
        emailserver.close()
        print("Mail sent")
    except:
        print("Failed to send mail")
        logger.debug("Failed to send mail")


# Soitetaan annetun laitteen kayttajalle Twilion API:a kayttaen:
def call_user():
    client = Client(ACCOUNT_SID, AUTH_TOKEN)
    call = client.calls.create(to='', from_=TWLO_NUM, url=TWLO_URL)  # to= Haetaan laitteen vuokraajan puh.numero tietokannasta
    logger.debug(call.sid)


# main funktio:
def main():

    CreateDB.init_db()  # Luo tietokannan jos sita ei ole
    try:
        sokettis = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except OSError as err:
        print("Failed to create socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    server_ip = socket.gethostbyname(HOSTNAME)
    server_address = (server_ip, PORT)

    try:
        sokettis.bind(server_address)
    except OSError as err:
        print("Failed to bind socket: {0}".format(err))
        logger.critical(err)
        sys.exit()

    sokettis.listen(5)

    while True:

        print("Waiting for connection..")
        connection, client_address = sokettis.accept()
        print("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))

        connection.settimeout(5)  # Katkaisee yhteyden clienttiin jos dataa ei vastaan oteta x sekuntiin. Odottaa uutta yhteytta taman jalkeen.

        try:
            data = connection.recv(4096)
            data = data.decode()
            print("Data from client: " + data)

            # Pilkotaan vastaan otettu merkkijono listaan:
            data_list = data.split(",")

            # Siirretaan listan alkiot tietokantaan, jonka jalkeen suljetaan yhteydet:
            if str(data_list[0]) == CHECK:
                conn = sqlite3.connect('PROTO.db')
                c = conn.cursor()
                data_to_db(c, conn, data_list)
                print("Data saved, closing connection")
                logger.debug("Data saved")
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
