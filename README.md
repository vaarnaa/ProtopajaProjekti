# Prototype project

The project consists of a prototype for a local air cleaner device with IoT features. The prototype was built in the summer 2017 at the Aalto University's Protocamp course in co-operation with Consair Oy. The primary purpose of the device is to reduce construction workers exposure to waste particles while mixing mortar.

## How it works

### Mechanics

The enclosure of the device was built of plywood with the help of a 3D model. The air flow enters through the air vent in the front panel, passes through the filters in the middle and the fan in the rear of the device and then exits through the roof outlet.

### Microcontroller

The operation of the device is controlled by the microcontroller. It reads the values measured by the pressure sensors and adjusts the fan speed based on the measurement results. The airflow passing through the device is kept constant regardless of the degree of clogging of the filters.

### PID Control

The fan speed is controlled by a specific piece of software in the microcontroller. The code-based PID controller adjusts the speed to the desired value based on the difference between the measured and the desired speed.

### Networking

While the device is in operation, the GSM module connects to the server and transmits information about the status of the device. The server stores the data in the database. The person in charge of the device is informed by email or text message if the filters are clogged.
