# Serveri.py file

import socket
import sys
import datetime as dt
import sqlite3
import time
import smtplib


check = 'abc'

# Luo taulukon jos sellaista ei ole:
def create_table():
    c.execute('CREATE TABLE IF NOT EXISTS dataToStore (datestamp TEXT, name TEXT, value REAL)')


# Asettaa annetun listan alkiot tietokantaan
def data_tietokantaan(data_list):
    unix = time.time()
    date = str(dt.datetime.fromtimestamp(unix).strftime("%d-%m-%Y %H:%M:%S"))
    c.execute("INSERT INTO dataToStore (datestamp, name, value) VALUES (?, ?, ?)", (date, str(data_list[1]), int(data_list[2])))
    conn.commit()

# Lahetetaan annetun laitteen kayttajalle email:
def send_email(laiteID):
    sender = ''
    receiver = ''
    password = ''
    header = 'To:' + receiver + '\n' + 'From: ' + sender + '\n' + 'Subject:Testing \n'
    message = header + "\nHello!\nThis is a test message."

    try:
        emailserver = smtplib.SMTP("smtp.gmail.com", 587)
        emailserver.starttls()
        emailserver.login(sender, password)
        emailserver.sendmail(sender, receiver, message)
        emailserver.close()
        print("Mail sent")
    except:
        print("Failed to send mail")



try:
    sokettis = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
except OSError as err:
    print("Failed to create socket: {0}".format(err))
    sys.exit()


server_ip = socket.gethostbyname("")
server_address = (server_ip, 8080)

try:
    sokettis.bind(server_address)
except OSError as err:
    print("Failed to bind socket: {0}".format(err))
    sys.exit()

sokettis.listen(5)


while True:

    print("Waiting for connection..")
    connection, client_address = sokettis.accept()
    print("Got a connection from: " + str(client_address[0]) + " " + str(client_address[1]))

    data = connection.recv(4096)
    data = data.decode()
    print("Data from client: " + data)

    # Pilkotaan vastaan otettu merkkijono listaan:
    data_lista = data.split(",")

    # Siirretaan listan alkiot tietokantaan, jonka jalkeen suljetaan yhteydet:
    if str(data_lista[0]) == check:
        conn = sqlite3.connect('testi.db')
        c = conn.cursor()
        create_table()
        data_tietokantaan(data_lista)
        print("Data saved, closing connection")
        c.close()
        conn.close()
        connection.close()

    else:
        print("Wrong data, closing connection")
        connection.close()
