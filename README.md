# ConveyorBelt-mqtt

Project realized under the **SNS Automatyk** student research group.  
This project focuses on **remotely and autonomous controlling a conveyor belt** and a fan while collecting data from sensors (DHT11, HC-SR04) via the **MQTT** protocol.
Mqtt Broker is set on Raspberry Pi 4B and ESP32s publish and subscribe topics (mentioned in scripts).
Additionally, we use a **Raspberry Pi camera** for measuring objects on the conveyor (OpenCV).


## Table of Contents
1. [Project Description](#project-description)  
2. [Hardware Setup](#hardware-setup)  
3. [Technologies Used](#technologies-used)  
4. [Installation and Launch (Python/Flask)](#installation-and-launch-pythonflask)    

---

## Project Description

The project was designed to measure the inlet and outlet temperatures in order to properly cool the element placed on the belt.
The speed of the fan and the engine was to be adjusted to the size of the object and its temperature, 
but the DHT11 is not a fast temperature sensor. So the speed is controlled only by the size of the object.
The previously mentioned sensor was placed near the engine to monitor the temperature around it, similarly to applications in automation.
Now the conveyor belt works on program delays, because the distance sensor does not always read the distance correctly for small elements.

1. **Sensor Handling**: Read temperature & humidity (DHT11) and measure distance between measured item settled on designated place by HC-SR04.  
2. **Vision-Based Object Measurement**: A Raspberry Pi with camera (libcamera) and OpenCV processes object dimensions on the conveyor belt.  
3. **Control**:  
   - **Automatic Mode** - based on sensor readings and vision measurements (natively set, after switching mode to manual we can back to auto-mode by clicking "ESP32 Control"
   - **Manual Mode**    - the user can directly control conveyor and fan speed by API. Before this "Manual Control" button must be clicked in Web API
4. **MQTT Communication**: Raspberry Pi and ESP32 boards exchange messages through an MQTT Protocol.  
5. **Database**: Sensor data (DHT temperature, humidity) is stored in SQLite (using SQLAlchemy).  
6. **Visualization**: Flask pages display real-time video, measured object size, and sensor data charts (temperatures & humidity).

---

## Hardware Setup

- **Raspberry Pi** (with camera) – to run the Flask app and capture video via libcamera and set the broker
- **2× ESP32** boards – for sensor readings and motor/fan control.  
- **DHT11** – temperature/humidity sensor.  
- **HC-SR04** – ultrasonic distance sensor.  
- **DC Motor** – drives the conveyor belt.  
- **DC Fan** – controlled via PWM signal.  
- **IBT_2 driver** – used to control the DC motor from the ESP32.  
- **Conveyor belt model** (could be 3D-printed or any other build).

---

## Technologies Used

- **Python** (Flask, threading, etc.)  
- **Flask-SQLAlchemy** & **SQLite** – data storage  
- **Flask-MQTT** – MQTT integration  
- **OpenCV** + **imutils** – image processing for object measurement  
- **scipy** – distance calculations  
- **libcamera** – capturing camera stream (on Raspberry Pi)  
- **HTML / JS** (Flask templates) – charts and control panel

---

## Installation and Launch 

## 1.  Clone the repository  
   ```bash
   git clone https://github.com/YourGitHubUsername/ConveyorBelt-mqtt.git
   cd ConveyorBelt-mqtt  
  ```
## 2.  Create and activate a Python virtual environment

   ```bash
python3 -m venv venv
source venv/bin/activate
```
## 3. Install requirements after entering pi/venv
   ```bash
pip install -r requirements.txt
```
---
> **Note**: The `static/` and `templates/` folders must be included into folder Venv, so that Flask can locate those files and properly serve pages/styles. <
---
## 4. Start the application
   ```bash
python app.py
```
## 5. Verify the application
   ```bash
http://localhost:5000/
```
